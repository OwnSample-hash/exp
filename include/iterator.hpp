// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
/**
 * @file
 * @brief IteratorFacade is a CRTP base class that provides default
 * implementations of common iterator operations based on the derived class's
 * implementation of a few core operations.
 * @note Taken from LLVM
 */

#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace explo {
/**
 * @brief IteratorFacade is a CRTP base class that provides default
 * implementations of common iterator operations based on the derived class's
 * implementation of a few core operations.
 *
 * @tparam DerivedT The derived class that inherits from IteratorFacade. This
 * class must implement the core iterator operations (like dereference,
 * increment, and equality comparison) for the IteratorFacade to work correctly.
 * @tparam IterCatT The iterator category (e.g., std::input_iterator_tag,
 * std::forward_iterator_tag, std::bidirectional_iterator_tag,
 * std::random_access_iterator_tag) that specifies the capabilities of the
 * iterator. This is used to enable or disable certain operations based on the
 * iterator category.
 * @tparam T The type of the elements that the iterator points to. This is used
 * to define the value_type, pointer, and reference types for the iterator.
 * @tparam DiffT The type used for representing the difference between iterators
 * (default is std::ptrdiff_t). This is used for operations like addition and
 * subtraction of iterators.
 * @param PtrT The pointer type for the iterator (default is T*). This is used
 * for the operator-> implementation.
 * @param RefT The reference type for the iterator (default is T&). This is used
 * for the operator* implementation.
 * @return
 */
template <typename DerivedT, typename IterCatT, typename T,
          typename DiffT = std::ptrdiff_t, typename PtrT = T *,
          typename RefT = T &>
class IteratorFacade {
public:
  using iterator_category = IterCatT; /**< @brief Type alias for IterCatT */
  using value_type = T;               /**< @brief Type alias for T */
  using difference_type = DiffT;      /**< @brief Type alias for DiffT */
  using pointer = PtrT;               /**< @brief Type alias for PtrT */
  using reference = RefT;             /**< @brief Type alias for RefT */

  /**
   * @brief Compile-time checks to determine the capabilities of the iterator
   * based on the iterator category. These checks enable or disable certain
   * operations based on whether the iterator is a random access iterator or a
   * bidirectional iterator.
   */
  enum Is {
    IsRandomAccess =
        std::is_base_of<std::random_access_iterator_tag, IterCatT>::value,
    IsBidirectional =
        std::is_base_of<std::bidirectional_iterator_tag, IterCatT>::value,
  };

protected:
  /**
   * @class ReferenceProxy
   * @brief A proxy class used to implement the operator[] for random access
   * iterators. It holds a reference to the element pointed to by the iterator
   * and allows for both reading and writing through the operator[].
   */
  class ReferenceProxy {
    friend IteratorFacade;

    /**
     * @brief The iterator that this proxy holds a reference to. This is used to
     * access the element pointed to by the iterator when implementing the
     * operator[] for random access iterators.
     */
    DerivedT I;

    /**
     * @brief Constructor for the ReferenceProxy class. It takes an iterator (of
     * type DerivedT) as an argument and initializes the member variable I with
     * it.
     *
     * @param iter The iterator that this proxy will hold a reference to.
     */
    ReferenceProxy(DerivedT iter) : I(std::move(iter)) {}

  public:
    /**
     * @brief Implicit conversion operator to RefT.
     *
     * @return A reference to the element pointed to by the iterator .
     */
    operator RefT() const { return *I; }
  };

  /**
   * @class PointerProxy
   * @brief A proxy class used to implement the operator-> for iterators. It
   * holds a reference to the element pointed to by the iterator and allows for
   * access to the element's members through the operator->.
   */
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
