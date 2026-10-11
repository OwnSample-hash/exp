#pragma once

#include <functional>

namespace explo {

struct AtStart {
  virtual void operator()() {
    if (func)
      func();
  }

protected:
  std::function<void()> func;
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: