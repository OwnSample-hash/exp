#pragma once

#include <type_traits>
#include <utility>

namespace explo {

template <typename IterT> class iterator_range {
  IterT Begin;
  IterT End;
  template <typename From, typename To>
  using explicitly_converted_t = decltype(static_cast<To>(
      std::declval<std::add_rvalue_reference_t<From>>()));

public:
  template <typename Container>
  iterator_range(Container &&C)
      : Begin(std::begin(std::forward<Container>(C))),
        End(std::end(std::forward<Container>(C))) {}

  iterator_range(IterT begin, IterT end)
      : Begin(std::move(begin)), End(std::move(end)) {}

  IterT begin() const { return Begin; }
  IterT end() const { return End; }
  bool empty() const { return Begin == End; }
};

template <class T> iterator_range<T> make_range(T x, T y) {
  return iterator_range<T>(std::move(x), std::move(y));
}

template <typename T> iterator_range<T> make_range(std::pair<T, T> p) {
  return iterator_range<T>(std::move(p.first), std::move(p.second));
}
} // namespace explo
