// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
/**
 * @file
 * @brief The registry.hpp file defines a simple registry system for managing
 * and creating instances of types based on their registered names. Taken from
 * LLVM.
 */

#pragma once
#include <functional>
#include <iterator.hpp>
#include <iterator>
#include <iterator_range.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <type_traits>

/**
 * @brief The SRE (Simple Registry Entry) class represents a single entry in the
 * registry. It holds the name of the entry and a factory function (Ctor) that
 * can be used to create instances of the type T with the specified constructor
 * parameters (CTorParamTypes). The SRE class provides a method to create
 * instances of T using the factory function, allowing for dynamic creation of
 * objects based on their registered names.
 *
 * @tparam T The type of the objects that this registry entry will create.
 * @tparam CTorParamTypes The types of the parameters that the factory function
 * (Ctor) will accept when creating instances of T.
 * @param name The name of the registry entry, which can be used to identify and
 * retrieve the entry from the registry.
 * @param ctor The factory function that will be used to create instances of T.
 * @return An instance of SRE that represents a registry entry for creating
 * objects of type T with the specified constructor parameters.
 */
template <typename T, typename... CTorParamTypes> class SRE {
  using FactoryFN = std::function<std::unique_ptr<T>(CTorParamTypes &&...)>;
  const std::string Name;
  FactoryFN Ctor;

public:
  SRE(const std::string &name, FactoryFN ctor) : Name(name), Ctor(ctor) {
    spdlog::debug("Registering SRE: {}", Name);
  }

  /**
   * @brief Returns the name of the registry entry.
   *
   * @return The name of the registry entry as a std::string.
   */
  std::string getName() const { return Name; }
  /**
   * @brief Creates an instance of type T using the factory function (Ctor) with
   * the provided constructor parameters (params).
   * @param params The parameters to be passed to the factory function (Ctor)
   * when creating an instance of type T. The parameters are perfectly forwarded
   * to the factory function.
   * @return A std::unique_ptr to the created instance of type T.
   */
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
    return explo::iterator_range<iterator>(begin(), end());
  }

  template <typename V>
    requires std::is_base_of<T, V>::value
  class Add {
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
