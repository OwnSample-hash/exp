#pragma once

#include <list>
#include <module.hpp>
#include <registry.hpp>

// Base interface that all plugins must implement
class IPlugin {
public:
  virtual ~IPlugin() = default;
  virtual std::string getName() const = 0;
  virtual std::string getVersion() const = 0;

  // TODO: Add ref argument to register plugin mods
  virtual void initialize(std::list<explo::Module> &) = 0;

  virtual void execute() = 0;
};

typedef Registry<IPlugin> PluginRegistry;
extern template class Registry<IPlugin>;
// Vim: set expandtab tabstop=2 shiftwidth=2:
