/**
 * @file
 * @brief The IPlugin class is an interface that defines the structure and
 * behavior of a plugin in the system. It provides pure virtual functions that
 * must be implemented by any class that inherits from IPlugin, ensuring that
 * all plugins adhere to a consistent interface.
 */

#pragma once

#include <args.hxx>
#include <interfaces/renderer.hpp>
#include <interfaces/tool.hpp>
#include <interfaces/tool_provider.hpp>
#include <module.hpp>
#include <registry.hpp>

using ToolRegistry = Registry<explo::ITool>;
using ToolProviderRegistry = Registry<explo::IToolProvider>;
using RendererRegistry = Registry<explo::IRenderer>;

extern template class Registry<explo::ITool>;
extern template class Registry<explo::IToolProvider>;
extern template class Registry<explo::IRenderer>;
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
