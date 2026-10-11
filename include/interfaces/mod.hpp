/**
 * @file
 * @brief Module interface definition, providing a base class for all modules in
 * the system.
 */

#pragma once

#include <args.hxx>
#include <memory>
#include <registry.hpp>
#include <semver.hpp>
#include <version.hpp>

namespace explo {

enum class ModuleType {
  RENDERER,
  TOOLPROVIDER,
  TOOL,
};

struct initArgs {
  std::shared_ptr<args::Group> parser;
  std::shared_ptr<spdlog::logger> logger;
};

/**
 * @class IMod
 * @brief Interface for modules, providing methods for module information and
 * lifecycle management.
 */
class IMod {
public:
  virtual ~IMod() = default;

  /**
   * @brief Method to get the name of the module.
   *
   * @return The name of the module as a C-style string.
   */
  virtual const char *getName() const = 0;

  /**
   * @brief Method to get the version of the module.
   *
   * @return The version of the module.
   */
  virtual const SemVer getVersion() const = 0;

  /**
   * @brief Method to initialize the module, performing any necessary setup or
   * configuration before the module can be used.
   */
  virtual void initialize(initArgs &) = 0;

  /**
   * @brief Method to shut down the module, performing any necessary cleanup or
   * resource deallocation when the module is no longer needed.
   */
  virtual void shutdown() = 0;

  /**
   * @brief Method to get the type of the module.
   *
   * @return The type of the module.
   */
  virtual ModuleType getModuleType() const = 0;

  /**
   * @brief If the mod has registered a command it can use this method to check if it has been selected.
   *
   * @return true if the module registered command has been triggered, false otherwise.
   */
  virtual bool cmdCheck() { return false; };

  /**
   * @brief Method to get the dependencies of the module.
   *
   * @return A vector of the module's dependencies.
   */
  virtual const std::vector<Dependency> &getDependencies() {
    static const std::vector<Dependency> deps{};
    return deps;
  };
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
