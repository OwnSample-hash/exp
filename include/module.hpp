#pragma once
#include <interfaces/mod.hpp>
#include <memory>

namespace explo {

enum class ModuleType {
  RENDERER,
  TOOLPROVIDER,
  TOOL,
};

struct Module {
  const char *name;
  ModuleType type;
  std::unique_ptr<IMod> instance;

  Module(const char *name, ModuleType type, std::unique_ptr<IMod> instance)
      : name(name), type(type), instance(std::move(instance)) {}
  Module(Module &&) = default;
  Module(const Module &) = delete;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
