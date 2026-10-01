#pragma once

#include <string>

namespace explo::lib {
class ITLSClient {
public:
  ITLSClient() = default;
  ~ITLSClient() = default;

  ITLSClient(const ITLSClient &) = delete;
  ITLSClient(ITLSClient &&) = delete;
  ITLSClient &operator=(const ITLSClient &) = delete;
  ITLSClient &operator=(ITLSClient &&) = delete;

  virtual bool connect(std::string_view host, int port) = 0;
  virtual int sendData(const std::string &data) = 0;
  virtual std::string recvData(int bufSize = 4096) = 0;
  virtual void close() = 0;
  virtual std::string getLastError() = 0;
};
} // namespace explo::lib
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
