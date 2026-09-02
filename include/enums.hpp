#pragma once

#define ConnectionStatuses                                                                                             \
  Z(Open)                                                                                                              \
  Z(OpenUntested)                                                                                                      \
  Z(Filtered)                                                                                                          \
  Z(Error)                                                                                                             \
  Z(Timeout)                                                                                                           \
  Z(Refused)                                                                                                           \
  Z(Reset)                                                                                                             \
  Z(Closed)                                                                                                            \
  Z(Aborted)                                                                                                           \
  Z(NetReset)                                                                                                          \
  Z(HostUnreachable)                                                                                                   \
  Z(NetworkUnreachable)

namespace explo::ConnectionStatus {
enum Type {
#define Z(name) name,
  ConnectionStatuses
#undef Z
      Count
};
inline const char *toString(Type status) {
  switch (status) {
#define Z(name)                                                                                                        \
  case name:                                                                                                           \
    return #name;
    ConnectionStatuses
#undef Z
        default : return "Unknown";
  }
}
} // namespace explo::ConnectionStatus

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
