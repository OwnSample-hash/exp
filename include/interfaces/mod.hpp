/**
 * @file
 * @brief Module interface definition, providing a base class for all modules in
 * the system.
 */

#pragma once

namespace explo {

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
   * @return The version of the module as a C-style string.
   */
  virtual const char *getVersion() const = 0;

  /**
   * @brief Method to initialize the module, performing any necessary setup or
   * configuration before the module can be used.
   */
  virtual void initialize() = 0;

  /**
   * @brief Method to shut down the module, performing any necessary cleanup or
   * resource deallocation when the module is no longer needed.
   */
  virtual void shutdown() = 0;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
