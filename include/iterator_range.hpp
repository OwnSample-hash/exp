// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
/**
 * @file
 * @brief The iterator_range class is a simple utility that provides a
 * convenient way to represent a range of elements defined by a pair of
 * iterators.
 * @note Taken from LLVM
 */

#pragma once

#include <type_traits>
#include <utility>

namespace explo {

/**
 * @brief The iterator_range class is a simple utility that provides a
 * convenient way to represent a range of elements defined by a pair of
 * iterators.
 *
 * @tparam IterT The type of the iterators that define the range.
 * @param C A container that can be used to construct the iterator_range.
 */
template <typename IterT> class iterator_range {
  IterT Begin;
  IterT End;
  template <typename From, typename To>
  using explicitly_converted_t = decltype(static_cast<To>(std::declval<std::add_rvalue_reference_t<From>>()));

public:
  /**
   * @brief Constructor for the iterator_range class. It takes a container C as
   * an argument and initializes the member variables Begin and End with the
   * beginning and ending
   *
   * @tparam Container The type of the container that can be used to construct
   * the iterator_range.
   * @param C A container that can be used to construct the iterator_range.
   * @return The range defined by the iterators of the container C.
   */
  template <typename Container>
  iterator_range(Container &&C)
      : Begin(std::begin(std::forward<Container>(C))), End(std::end(std::forward<Container>(C))) {}

  /**
   * @brief Constructor for the iterator_range class.
   *
   * @param begin The beginning iterator of the range.
   * @param end The ending iterator of the range.
   */
  iterator_range(IterT begin, IterT end) : Begin(std::move(begin)), End(std::move(end)) {}

  /**
   * @brief Returns the beginning and ending iterators of the range.
   *
   * @return A pair of iterators where the first element is the beginning
   * iterator and
   */
  IterT begin() const { return Begin; }
  /**
   * @brief Returns the ending iterator of the range.
   *
   * @return The ending iterator of the range.
   */
  IterT end() const { return End; }
  /**
   * @brief Checks if the range is empty by comparing the beginning and ending
   * iterators.
   *
   * @return true if the range is empty false otherwise.
   */
  bool empty() const { return Begin == End; }
};

/**
 * @brief A helper function to create an iterator_range from a pair of
 * iterators.
 *
 * @tparam T The type of the iterators that define the range.
 * @param x The beginning iterator of the range.
 * @param y The ending iterator of the range.
 * @return The range defined by the iterators x and y.
 */
template <class T> iterator_range<T> make_range(T x, T y) { return iterator_range<T>(std::move(x), std::move(y)); }

/**
 * @brief A helper function to create an iterator_range from a pair of
 * iterators.
 *
 * @tparam T The type of the iterators that define the range.
 * @param p A pair of iterators where the first element is the beginning
 * iterator and the second element is the ending iterator of the range.
 * @return The range defined by the iterators in the pair p.
 */
template <typename T> iterator_range<T> make_range(std::pair<T, T> p) {
  return iterator_range<T>(std::move(p.first), std::move(p.second));
}
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
