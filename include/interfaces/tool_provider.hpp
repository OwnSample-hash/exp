/**
 * @file tool_provider.hpp
 * @brief Tool provider interface definition, allowing for dynamic loading and
 * management of tools.
 */
#pragma once

#include <interfaces/mod.hpp>
#include <interfaces/tool.hpp>
#include <map>
#include <memory>
#include <string>

namespace explo {

class IToolProvider : public IMod {
public:
  virtual ~IToolProvider() = default;

  virtual const std::map<std::string, std::shared_ptr<ITool>> &
  getTools() const = 0;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
