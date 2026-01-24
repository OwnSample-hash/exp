#pragma once
#include "iterator.hpp"
#include "iterator_range.hpp"
#include <functional>
#include <iterator>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

template <typename T, typename... CTorParamTypes> class SRE {
  using FactoryFN = std::function<std::unique_ptr<T>(CTorParamTypes &&...)>;
  const std::string Name;
  FactoryFN Ctor;

public:
  SRE(const std::string &name, FactoryFN ctor) : Name(name), Ctor(ctor) {
    spdlog::debug("Registering SRE: {}", Name);
  }

  std::string getName() const { return Name; }
  std::unique_ptr<T> create(CTorParamTypes &&...params) const {
    return Ctor(std::forward<CTorParamTypes>(params)...);
  }
};

template <typename T, typename... CTorParamTypes> class Registry;

template <typename R> struct IsRegistryType : std::false_type {};
template <typename T, typename... CTorParamTypes>
struct IsRegistryType<Registry<T, CTorParamTypes...>> : std::true_type {};

// Registry for plugin factories
template <typename T, typename... CTorParamTypes> class Registry {
  static_assert(!IsRegistryType<T>::value,
                "Nested Registry types are not allowed");

public:
  using type = T;
  using entry = SRE<T, CTorParamTypes...>;
  static constexpr bool HasCtorParams =
      sizeof...(CTorParamTypes) != 0; // Check if there are constructor params

  class node;
  class iterator;

private:
  Registry() = delete;
  friend class node;

  static inline node *Head = nullptr;
  static inline node *Tail = nullptr;

public:
  class node {
    friend class iterator;
    friend Registry<T, CTorParamTypes...>;

    node *Next;
    const entry &Val;

  public:
    node(const entry &V) : Next(nullptr), Val(V) {}
  };

  static void add_node(node *N) {
    if (Tail) {
      Tail->Next = N;
    } else {
      Head = N;
    }
    Tail = N;
  }

  class iterator
      : public explo::IteratorFacade<iterator, std::forward_iterator_tag,
                                     const entry> {
    const node *Current;

  public:
    explicit iterator(const node *start) : Current(start) {}

    bool operator==(const iterator &That) const {
      return Current == That.Current;
    }

    iterator &operator++() {
      Current = Current->Next;
      return *this;
    }
    const entry &operator*() const { return Current->Val; }
  };

  static iterator begin() { return iterator(Head); }
  static iterator end() { return iterator(nullptr); }

  static explo::iterator_range<iterator> entries() {
    return iterator_range<iterator>(begin(), end());
  }

  template <typename V> class Add {
    entry Entry;
    node Node;

    static std::unique_ptr<T> CtorFn(CTorParamTypes &&...params) {
      return std::make_unique<V>(std::forward<CTorParamTypes>(params)...);
    }

  public:
    Add(const std::string &name) : Entry(name, CtorFn), Node(Entry) {
      add_node(&Node);
    }
  };
};

#define INSTANTIATE_REGISTRY(REGISTRY_CLASS)                                   \
  template class Registry<REGISTRY_CLASS::type>;                               \
  static_assert(!REGISTRY_CLASS::HasCtorParams,                                \
                "Constructor parameters not supported");

// Vim: set expandtab tabstop=2 shiftwidth=2:
