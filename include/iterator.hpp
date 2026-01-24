#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace explo {
template <typename DerivedT, typename IterCatT, typename T,
          typename DiffT = std::ptrdiff_t, typename PtrT = T *,
          typename RefT = T &>
class IteratorFacade {
public:
  using iterator_category = IterCatT;
  using value_type = T;
  using difference_type = DiffT;
  using pointer = PtrT;
  using reference = RefT;

  enum {
    IsRandomAccess =
        std::is_base_of<std::random_access_iterator_tag, IterCatT>::value,
    IsBidirectional =
        std::is_base_of<std::bidirectional_iterator_tag, IterCatT>::value,
  };

protected:
  class ReferenceProxy {
    friend IteratorFacade;

    DerivedT I;

    ReferenceProxy(DerivedT iter) : I(std::move(iter)) {}

  public:
    operator RefT() const { return *I; }
  };

  class PointerProxy {
    friend IteratorFacade;

    RefT R;

    template <typename ReferT>
    PointerProxy(ReferT &&R) : R(std::forward<ReferT>(R)) {}

  public:
    PtrT operator->() const { return &R; }
  };

public:
  DerivedT operator+(DiffT n) const {
    static_assert(std::is_base_of<IteratorFacade, IteratorFacade>::value,
                  "DerivedT must inherit from IteratorFacade");
    static_assert(IsRandomAccess,
                  "operator+ is only available for random access iterators");
    DerivedT temp = *static_cast<const DerivedT &>(*this);
    return temp += n;
  }

  friend DerivedT operator+(DiffT n, const DerivedT &it) {
    static_assert(IsRandomAccess,
                  "operator+ is only available for random access iterators");
    return it + n;
  }

  DerivedT operator-(DiffT n) const {
    static_assert(IsRandomAccess,
                  "operator- is only available for random access iterators");
    DerivedT temp = *static_cast<const DerivedT &>(*this);
    return temp -= n;
  }

  DerivedT &operator++() {
    static_assert(std::is_base_of<IteratorFacade, IteratorFacade>::value,
                  "DerivedT must inherit from IteratorFacade");
    return static_cast<DerivedT *>(this)->operator+=(1);
  }

  DerivedT operator++(int) {
    DerivedT temp = *static_cast<DerivedT *>(this);
    ++temp;
    return temp;
  }

  DerivedT &operator--() {
    static_assert(IsBidirectional,
                  "operator-- is only available for bidirectional iterators");
    return static_cast<DerivedT *>(this)->operator-=(1);
  }

  DerivedT operator--(int) {
    static_assert(IsBidirectional,
                  "operator-- is only available for bidirectional iterators");
    DerivedT temp = *static_cast<DerivedT *>(this);
    --temp;
    return temp;
  }

#ifndef __cpp_impl_three_way_comparison
  bool operator!=(const DerivedT &RHS) const {
    return !(static_cast<const DerivedT &>(*this) == RHS);
  }
#endif

  bool operator>(const DerivedT &RHS) const {
    static_assert(IsRandomAccess,
                  "operator> is only available for random access iterators");
    return !(static_cast<const DerivedT &>(*this) < RHS) &&
           !(static_cast<const DerivedT &>(*this) == RHS);
  }

  bool operator<=(const DerivedT &RHS) const {
    static_assert(IsRandomAccess,
                  "operator<= is only available for random access iterators");
    return !(static_cast<const DerivedT &>(*this) > RHS);
  }

  bool operator>=(const DerivedT &RHS) const {
    static_assert(IsRandomAccess,
                  "operator>= is only available for random access iterators");
    return !(static_cast<const DerivedT &>(*this) < RHS);
  }

  PointerProxy operator->() {
    return static_cast<DerivedT *>(this)->operator*();
  }
  ReferenceProxy operator[](DiffT n) {
    static_assert(IsRandomAccess,
                  "operator[] is only available for random access iterators");
    return static_cast<DerivedT *>(this)->operator+(n);
  }
};
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
