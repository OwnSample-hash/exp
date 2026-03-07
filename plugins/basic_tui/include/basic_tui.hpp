#pragma once
#include <color.hpp>
#include <functional>
#include <interfaces/display.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <string_view>

using namespace explo;

class PL_basic_tui : public IPlugin {
  std::shared_ptr<spdlog::logger> logger;

public:
  PL_basic_tui() = default;
  ~PL_basic_tui() = default;
  const std::string getName() const override;
  const std::string getVersion() const override;
  void initialize(initArgs &args) override;
  void execute() override;
};

class basic_tui : public IDisplay {
  std::shared_ptr<spdlog::logger> logger;

public:
  basic_tui();
  basic_tui(std::shared_ptr<spdlog::logger> logger) : logger(logger) {};
  ~basic_tui() override;

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
