#pragma once

#include <color.hpp>
#include <functional>
#include <interfaces/mod.hpp>
#include <string_view>

namespace explo {

class IDisplay : public IMod {
public:
  virtual ~IDisplay() = default;

  virtual void clearScreen() = 0;
  virtual void updateScreen() = 0;

  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;

  virtual void drawText(int x, int y, std::string_view text,
                        Color fg = Color::WHITE, Color bg = Color::BLACK) = 0;

  virtual void onInputChar(std::function<void(char)>) = 0;
};

class IUIComponent {
public:
  virtual std::string_view getId() = 0;

  virtual ~IUIComponent() = default;
  virtual void render(IDisplay &display) = 0;
};

template <typename T> class IInetarctableUIComponent : public IUIComponent {
public:
  virtual ~IInetarctableUIComponent() = default;
  virtual T getResult() = 0;
};

}; // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
