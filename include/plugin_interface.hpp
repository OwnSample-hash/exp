/**
 * @file
 * @brief The IPlugin class is an interface that defines the structure and
 * behavior of a plugin in the system. It provides pure virtual functions that
 * must be implemented by any class that inherits from IPlugin, ensuring that
 * all plugins adhere to a consistent interface.
 */

#pragma once

#include <args.hxx>
#include <memory>
#include <module.hpp>
#include <registry.hpp>
#include <vector>

struct initArgs {
  std::shared_ptr<std::vector<explo::Module>> modules;
  std::shared_ptr<args::Group> parser;
  std::shared_ptr<spdlog::logger> logger;
};

/**
 * @class IPlugin
 * @brief The IPlugin class is an interface that defines the structure and
 * behavior of a plugin in the system.
 *
 */
class IPlugin {
public:
  virtual ~IPlugin() = default;
  /**
   * @brief Returns the name of the plugin.
   */
  virtual const char *getName() const = 0;

  /**
   * @brief Returns the version of the plugin.
   */
  virtual const char *getVersion() const = 0;

  /**
   * @brief Initializes the plugin with a list of modules. This function is
   * called when the plugin is loaded and allows the plugin to perform any
   * necessary setup or registration of its modules with the system.
   *
   * @warning Don't rely on the constructor to do any work.
   */
  virtual void initialize(initArgs &) = 0;

  /**
   * @brief If the plugin registers any command line commands, this function is called to check the arguments for those
   * commands.
   *
   * @return true if the command was found and processed, false otherwise.
   */
  virtual bool cmdCheck() { return false; };
};

/**
 * @typedef Registry
 *
 * @brief The PluginRegistry is a type alias for a registry that holds instances
 * of IPlugin.
 */
typedef Registry<IPlugin> PluginRegistry;

/**
 * @brief Explicit instantiation of the Registry template class for IPlugin.
 */
extern template class Registry<IPlugin>;
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
