#pragma once
#include <arpa/inet.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class TLSClient {
  SSL_CTX *ctx = nullptr;
  SSL *ssl = nullptr;
  int sock = -1;

public:
  std::shared_ptr<spdlog::logger> logger;

  TLSClient();
  TLSClient(const TLSClient &) = delete;
  TLSClient &operator=(const TLSClient &) = delete;
  TLSClient(TLSClient &&) = delete;
  TLSClient &operator=(TLSClient &&) = delete;
  ~TLSClient();

  bool connect(const std::string &host, int port);

  int send_data(const std::string &data);

  std::string recv_data(int buf_size = 4096);

  void close();

  std::string getLastError();
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
