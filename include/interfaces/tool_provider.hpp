/**
 * @file tool_provider.hpp
 * @brief Tool provider interface definition, allowing for dynamic loading and
 * management of tools.
 */
#pragma once

#include "interfaces/mod.hpp"
namespace explo {

class IToolProvider : public IMod {
public:
  virtual ~IToolProvider() = default;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
