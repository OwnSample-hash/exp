/**
 * @file
 * @brief Display interface and UI components.
 */

#pragma once

#include <interfaces/mod.hpp>
#include <ui.hpp>

namespace explo {

/**
 * @class IRenderer
 *
 * @brief Interface for display modules, providing methods for screen
 * manipulation and text rendering.
 */
class IRenderer : public IMod {

public:
  virtual ~IRenderer() = default;
  /**
   * @brief Method to run the main rendering loop, which should handle
   * events and redraw the screen as necessary.
   */
  virtual void runLoop() = 0;

  /**
   * @brief Enable or disable signal handling for the renderer. When enabled, the renderer should handle signals such as
   * SIGINT (Ctrl-C) gracefully, allowing for clean shutdowns or interruptions.
   *
   * @param enabled A boolean indicating whether signal handling should be enabled (true) or disabled (false).
   */
  virtual void setSignalHandling(bool enabled) {}
};

}; // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
