/**
 * @file
 * @brief Display interface and UI components.
 */

#pragma once

#include <interfaces/mod.hpp>
#include <ui.hpp>

namespace explo {

enum class UIRenderType {
  OneShot,
  Loop,
};

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
};

}; // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
