#pragma once
#include <interfaces/renderer.hpp>
#include <plugin_interface.hpp>
#include <string>
#include <ui.hpp>
#include <unordered_map>

namespace explo {

class Dispatcher {
  bool initialized = false;
  IRenderer *rendererModule = nullptr;

public:
  Dispatcher(std::unordered_map<std::string, initArgs> &pluginInitArgs);

  ~Dispatcher() {
    if (rendererModule) {
      rendererModule->shutdown();
    }
  }

  void setBaseWidget(UIWidget *widget);

  void runLoop();

  bool fireEvent(UIEvent ev, const EventData &ed);

  bool isInitialized() const { return initialized; }
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
