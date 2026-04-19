#include <dispatcher.hpp>
#include <module.hpp>
#include <spdlog/spdlog.h>
#include <sys/select.h>

namespace explo {

Dispatcher::Dispatcher(
    std::unordered_map<std::string, initArgs> &pluginInitArgs,
    const std::string &preferredRenderer) {
  IMod *rendererModuleRaw = nullptr;
  for (const auto &[name, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::ModuleType::RENDERER) {
        if (!preferredRenderer.empty() && mod.name != preferredRenderer) {
          spdlog::debug("Skipping renderer module: {} from plugin: {} as it "
                        "does not match preferred renderer: {}",
                        mod.name, name, preferredRenderer);
          continue;
        }
        rendererModuleRaw = mod.instance.get();
        spdlog::info("Using display module: {} from plugin: {}", mod.name,
                     name);
        break;
      } else {
        spdlog::debug("Module: {} from plugin: {} is not a display module",
                      mod.name, name);
      }
    }
    if (rendererModuleRaw) {
      break;
    }
  }

  try {
    rendererModule = dynamic_cast<IRenderer *>(rendererModuleRaw);
    if (!rendererModule) {
      throw std::runtime_error("No valid display module found");
    }
  } catch (const std::exception &e) {
    spdlog::error("Error initializing display module: {}", e.what());
    return;
  }

  rendererModule->initialize();
  initialized = true;
}

void Dispatcher::setBaseWidget(UIWidget *widget) {
  if (widget == nullptr) {
    widget = const_cast<UIWidget *>(rendererModule->getBuiltInWidget());
  }
  if (widget->type != UIType::HContainer &&
      widget->type != UIType::VContainer) {
    throw std::invalid_argument(
        "Base widget must be a container (HContainer or VContainer)");
  }
  if (rendererModule) {
    rendererModule->setWidgetBase(widget);
  }
}

void Dispatcher::runLoop() {
  if (!rendererModule) {
    throw std::runtime_error("No renderer module available");
  }
  if (rendererModule->getRenderType() == UIRenderType::OneShot) {
    struct timeval timeout = {0, 100'000}; // 100ms
    fd_set in;
    FD_ZERO(&in);
    FD_SET(STDIN_FILENO, &in);
    while (rendererModule->shouldQuit() == false) {
      rendererModule->render();
      int ret = select(STDIN_FILENO + 1, &in, nullptr, nullptr, &timeout);
      if (ret > 0) {
        spdlog::debug("Input detected, firing event");
        char buf[256];
        ssize_t bytesRead = read(STDIN_FILENO, buf, sizeof(buf) - 1);
        if (bytesRead > 0) {
          buf[bytesRead] = '\0';
          EventData ed;
          ed.input = std::string(buf);
          fireEvent(UIEvent::Key, ed);
        } else if (bytesRead < 0) {
          spdlog::error("Error reading input: {}", strerror(errno));
        }
      } else if (ret == 0) {
        continue; // Timeout, just render again
      } else {
        spdlog::error("Error in select: {}", strerror(errno));
      }
    }
  } else {
    spdlog::debug("Starting event-driven render loop");
    rendererModule->runLoop();
  }
  spdlog::info("Exiting main loop");
}

bool Dispatcher::fireEvent(UIEvent ev, const EventData &ed) {
  spdlog::debug("Firing event: {} with data: {}", static_cast<int>(ev),
                ed.input);
  if (rendererModule) {
    return rendererModule->fireEvent(ev, ed);
  }
  return false;
}

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
