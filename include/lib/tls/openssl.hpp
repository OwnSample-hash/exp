#pragma once

#ifndef EXPLO_LIB_TLS
#error "This file should not be included directly. Include tls.hpp instead."
#endif

#include "../../itls.hpp"
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string_view>

namespace explo::lib {
namespace impl {

class OpenSSL_TLSClient : public ITLSClient {
  SSL_CTX *ctx = nullptr;
  SSL *ssl = nullptr;
  int sock = -1;

  void init();

public:
  OpenSSL_TLSClient();
  ~OpenSSL_TLSClient();

  OpenSSL_TLSClient(const OpenSSL_TLSClient &) = delete;
  OpenSSL_TLSClient(OpenSSL_TLSClient &&) = delete;
  OpenSSL_TLSClient &operator=(const OpenSSL_TLSClient &) = delete;
  OpenSSL_TLSClient &operator=(OpenSSL_TLSClient &&) = delete;

  bool connect(std::string_view host, int port) override;
  int sendData(const std::string &data) override;
  std::string recvData(int bufSize = 4096) override;
  void close() override;
  std::string getLastError() override;
};

extern OpenSSL_TLSClient *createTLSClient();

} // namespace impl

using TLSClient = impl::OpenSSL_TLSClient;

}; // namespace explo::lib
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
