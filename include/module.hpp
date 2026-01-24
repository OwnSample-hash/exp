#pragma once
#include <interfaces/mod.hpp>
#include <memory>

namespace explo {

enum ModuleType {
  MODULE_TYPE_EXTENSION = 0x0,
  MODULE_TYPE_CORE = 0x1,

  MODULE_TYPE_STATIC = 0x2,
  MODULE_TYPE_DYNMAIC = 0x4,

  MODULE_TYPE_DISPLAY,
};

struct Module {
  int id = -1;
  const char *name;
  int type;
  std::unique_ptr<IMod> instance;

  Module(const char *name, int type, std::unique_ptr<IMod> instance)
      : name(name), type(type), instance(std::move(instance)) {}
  Module(Module &&) = default;
  Module(const Module &) = delete;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
