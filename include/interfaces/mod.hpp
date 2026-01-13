#pragma once

namespace explo {

class IMod {
public:
  virtual ~IMod() = default;

  virtual const char *getName() const = 0;
  virtual const char *getVersion() const = 0;

  virtual void initialize() = 0;
  virtual void shutdown() = 0;
};

} // namespace explo
