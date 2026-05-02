/**
 * @file tool.hpp
 * @brief Tool interface definition, providing a base class for all tools.
 */
#pragma once

#include "mod.hpp"
#include <string>
#include <vector>

namespace explo {

class ITool : public IMod {

protected:
  std::string prefix;

public:
  virtual ~ITool() = default;

  /**
   * @brief Execute the tool's main functionality.
   */
  virtual void execute() = 0;

  /**
   * @brief Called when the tools is selected via the `use` command.
   *
   * @param prefix The prefix is used for setting the variable names in the
   * command context, allowing the tool to have its own namespace for variables
   * and commands.
   */
  virtual void invoke(const std::string &prefix) = 0;

  /**
   * @brief Called when the tools is selected via the `use` command, with a
   * C-style string prefix.
   *
   * @param prefix The prefix is used for setting the variable names in the
   * command context, allowing the tool to have its own namespace for variables
   * and commands.
   */
  virtual void invoke(const char *prefix) = 0;

  /**
   * @brief Called when the tool is deselected or switched to another tool,
   * allowing it to clean up any state or resources.
   */
  virtual void suppress() = 0;

  /**
   * @brief Get a list of tags associated with this tool.
   * @return A vector of strings representing the tool tags.
   */
  virtual const std::vector<std::string> &getTags() const = 0;
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2:
