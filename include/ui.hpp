#pragma once

#include <event_registry.hpp>
#include <spdlog/spdlog.h>
#include <string>

namespace explo {

enum class UIType {
  Label,
  List,
  Button,
  Input,
  HContainer,
  VContainer,
  Container,
};

enum class UIState {
  Normal,
  Hovered,
  Pressed,
  Disabled,
};

enum class UIEvent {
  Click,
  Key,
  Hover,
  Focus,
  Blur,
};

struct EventData {
  std::string input; // For key events, this can hold the input string.
};

struct Coord {
  int p1;
  int p2;
};

struct UIWidget;

struct UIWidget {
  const UIType type;
  std::string id;

  std::string text = "";
  UIState state = UIState::Normal;
  std::string hint = "";
  Coord startPos = {-1, -1};
  Coord endPos = {-1, -1};

  std::vector<UIWidget> child;

  // returns true if the ui should be invalidated and redrawn
  using ER =
      eventRegistry<bool, UIWidget &, UIEvent, const EventData &, void *>;
  ER events = ER();

  void *data = nullptr;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
