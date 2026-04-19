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
public:
  virtual ~ITool() = default;

  virtual void execute() = 0;

  virtual const std::vector<std::string> &getTags() const = 0;
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2:
