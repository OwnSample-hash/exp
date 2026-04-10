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

protected:
  bool needsRedraw = false;
  void *userData = nullptr;
  const UIWidget builtInWidget = {
      UIType::VContainer,
      "base",
      "Base",
      UIState::Normal,
      "This is the base widget for rendering",
      {-1, -1},
      {-1, -1},
      {
          {
              UIType::Label,
              "label1",
              "Hello, World! This is a built-in widget.",
          },
      },
  };
  UIWidget *rootWidget = nullptr;

public:
  virtual ~IRenderer() = default;

  /**
   * @brief Method to clear the screen.
   */
  virtual void clearScreen() = 0;

  /**
   * @brief Method to get the width of the root window or display.
   *
   * @return The width of the display as integers.
   */
  virtual int getWidth() const = 0;

  /**
   * @brief Method to get the height of the root window or display.
   *
   * @return The height of the display as an integer.
   */
  virtual int getHeight() const = 0;

  /**
   * @brief Method to render a widget.
   *
   * @param widget The UIWidget to be rendered on the display.
   */
  virtual void render(const UIWidget &widget, unsigned int depth = 0) = 0;

  virtual inline void render() {
    if (rootWidget) {
      render(*rootWidget);
      needsRedraw = false;
    }
  };

  /**
   * @brief Method to set the base widget for rendering, which will be the root
   * of the widget tree.
   */
  virtual inline void setWidgetBase(UIWidget *widget) {
    rootWidget = widget;
    invalidate();
  };

  /**
   * @brief Method to run the main rendering loop, which should handle
   * events and redraw the screen as necessary.
   */
  virtual void runLoop() = 0;

  /**
   * @brief Method to invalidate the current display, marking it for redraw.
   *
   */
  virtual inline void invalidate() { needsRedraw = true; }

  /**
   * @brief Method to get the base widget.
   *
   * @return A pointer to the base UIWidget.
   */
  virtual inline const UIWidget *getBuiltInWidget() const {
    return &builtInWidget;
  }

  /**
   * @brief Method to get the rendering type of the display module.
   *
   * @return The UIRenderType of the display module.
   */
  virtual UIRenderType getRenderType() const = 0;

  virtual bool fireEvent(UIEvent ev, const EventData &ed) = 0;

  virtual bool shouldQuit() const = 0;
};

}; // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
