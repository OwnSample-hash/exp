// clang-format off
#pragma once
#include <color.hpp>
#include <interfaces/display.hpp>
#include <functional>
#include <string_view>

using namespace exp;

void module_entry_%s();

class %s : public IDisplay {
public:
  %s();
  ~%s() override;

  const char *getName() const override;
  const char *getVersion() const override;

  void initialize() override;
  void shutdown() override;

  void clearScreen() override;
  void updateScreen() override;

  int getWidth() const override;
  int getHeight() const override;

  void drawText(int x, int y, std::string_view text, Color fg = Color::WHITE,
                Color bg = Color::BLACK) override;

  void onInputChar(std::function<void(char)>) override;
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
