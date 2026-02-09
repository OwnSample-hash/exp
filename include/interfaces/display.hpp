/**
 * @file
 * @brief Display interface and UI components.
 */

#pragma once

#include <color.hpp>
#include <functional>
#include <interfaces/mod.hpp>
#include <string_view>

namespace explo {

/**
 * @class IDisplay
 * @brief Interface for display modules, providing methods for screen
 * manipulation and text rendering.
 *
 */
class IDisplay : public IMod {
public:
  virtual ~IDisplay() = default;

  /**
   * @brief Method to clear the screen.
   */
  virtual void clearScreen() = 0;

  /**
   * @brief Method to update the screen, refreshing the display with any
   * changes made since the last update.
   */
  virtual void updateScreen() = 0;

  /**
   * @brief Method to get the width and height of the displayn
   *
   * @return The width of the display as integers.
   */
  virtual int getWidth() const = 0;

  /**
   * @brief Method to get the height of the display.
   *
   * @return The height of the display as an integer.
   */
  virtual int getHeight() const = 0;

  /**
   * @brief Method to draw text on the display at a specified position with
   * specified foreground and background colors.
   *
   * @param x The x-coordinate of the position where the text should be drawn.
   * @param y The y-coordinate of the position where the text should be drawn.
   * @param text The text to be drawn on the display, provided as a string view.
   * @param fg The foreground color of the text, specified as a Color enum value
   * (default is Color::WHITE).
   * @param bg The background color of the text, specified as a Color enum value
   * (default is Color::BLACK).
   */
  virtual void drawText(int x, int y, std::string_view text,
                        Color fg = Color::WHITE, Color bg = Color::BLACK) = 0;

  virtual void onInputChar(std::function<void(char)>) = 0;
};

/**
 * @class IUIComponent
 * @brief Interface for UI components that can be rendered on the display,
 * providing a method to get the component's ID and a method to render the
 * component on the display.
 *
 */
class IUIComponent {
public:
  /**
   * @brief Method to get the unique identifier of the UI component
   *
   * @return std::string The unique identifier of the UI component.
   */
  virtual const std::string getId() = 0;

  virtual ~IUIComponent() = default;

  /**
   * @brief Method to render the UI component on the display, taking an IDisplay
   * reference as a parameter to perform the rendering operations.
   *
   * @param display Reference to the IDisplay instance on which the UI component
   * should be rendered.
   */
  virtual void render(IDisplay &display) = 0;
};

/**
 * @brief Interface for interactive UI components that can return a result when
 * interacted with, extending the IUIComponent interface.
 *
 * @tparam T The type of the result returned by the interactive UI component
 * when interacted with.
 * @return The result of the interaction with the UI component, of type T.
 */
template <typename T> class IInetarctableUIComponent : public IUIComponent {
public:
  virtual ~IInetarctableUIComponent() = default;

  /**
   * @brief Method to get the result of the interaction with the UI component,
   * returning a value of type T.
   *
   * @return T result of the interaction with the UI component.
   */
  virtual T getResult() = 0;
};

}; // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
