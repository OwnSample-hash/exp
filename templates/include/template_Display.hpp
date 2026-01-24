// clang-format off
#pragma once
#include <color.hpp>
#include <functional>
#include <interfaces/display.hpp>
#include <list>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <string_view>

using namespace explo;

class PL_%s : public IPlugin {
public:
  PL_%s() = default;
  ~PL_%s() = default;
  std::string_view getName() const;
  std::string_view getVersion() const;
  void initialize(std::list<Module> &modules);
  void execute();
};

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
