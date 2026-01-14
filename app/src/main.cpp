#include <config.h>
#include <iostream>
#include <iterator>
#include <module.hpp>
#include <modules.hpp>
#include <spdlog/spdlog.h>
#include <vector>

#error fix app/CMakeLists.txt to link new_module correctly

explo::Module static_modules[] = {
    explo::Module{0, "core",
                  explo::MODULE_TYPE_CORE | explo::MODULE_TYPE_STATIC,
                  std::make_unique<new_module>()},

};

std::vector<explo::Module> modules{
    std::make_move_iterator(std::begin(static_modules)),
    std::make_move_iterator(std::end(static_modules)),
};

void load_module_dynamic(const char *name) {}

int main() {
  spdlog::info("Platform: {}", platform_name);
  for (const auto &mod : modules) {
    spdlog::info("Loaded module: {}", mod.name);
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
