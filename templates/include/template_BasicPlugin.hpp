// clang-format off
#pragma once
#include <color.hpp>
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

// Vim: set expandtab tabstop=2 shiftwidth=2:
