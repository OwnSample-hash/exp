/**
 * @file event_registry.hpp
 * @brief A simple event registry for managing event handlers.
 */
#pragma once

#include <functional>
#include <spdlog/spdlog.h>
#include <type_traits>

namespace explo {

/**
 * @class eventRegistry
 * @brief A simple event registry for managing event handlers.
 *
 * @tparam CBRet The return type of the callback functions.
 * @tparam Args The argument types for the callback functions.
 */
template <typename CBRet, typename... Args>
  requires std::is_invocable_v<std::function<CBRet(Args &&...)>, Args &&...>
class eventRegistry {
public:
  using onEventFN = std::function<CBRet(Args &&...)>;

private:
  std::vector<onEventFN> handlers;

public:
  eventRegistry() = default;
  eventRegistry(onEventFN handler) { handlers.push_back(handler); }

  void operator+=(onEventFN handler) {
    spdlog::debug("Adding event handler of type: {}", handler.target_type().name());
    handlers.push_back(handler);
  }

  void operator-=(const onEventFN &handler) {
    spdlog::debug("Removing event handler of type: {}", handler.target_type().name());
    std::erase_if(handlers, [&handler](const auto &h) { return h.target_type() == handler.target_type(); });
  }

  // template <typename T> T fire(Args &&...args) {
  //   static_assert(std::is_same_v<T, void> || std::is_same_v<T, bool>,
  //                 "fire can only be called with void or bool return type");
  //   if constexpr (std::is_same_v<T, void>) {
  //     fire(std::forward<Args>(args)...);
  //   } else if constexpr (std::is_same_v<T, bool>) {
  //     return fire(std::forward<Args>(args)...);
  //   }
  // }

  // void fire(Args &&...args) {
  //   spdlog::debug("Triggering event with {} handlers", handlers.size());
  //   for (const auto &handler : handlers) {
  //     handler(std::forward<Args>(args)...);
  //   }
  // }

  bool fire(Args &&...args) {
    spdlog::debug("Triggering event with {} handlers", handlers.size());
    for (const auto &handler : handlers) {
      if (handler(std::forward<Args>(args)...)) {
        return true;
      }
    }
    return false;
  }
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
