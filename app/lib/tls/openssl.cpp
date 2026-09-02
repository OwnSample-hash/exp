#include <lib.hpp>
#include <tls.hpp>
#include <unistd.h>

namespace explo::lib {
namespace impl {

OpenSSL_TLSClient::OpenSSL_TLSClient() { this->init(); }

OpenSSL_TLSClient::~OpenSSL_TLSClient() { this->close(); }

void OpenSSL_TLSClient::init() {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

  ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx)
    throw std::runtime_error("Failed to create SSL context");

  // Enforce minimum TLS 1.2
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

  // Load system CA certificates for verification
  SSL_CTX_set_default_verify_paths(ctx);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
}

bool OpenSSL_TLSClient::connect(std::string_view host, int port) {

  addrinfo hints{}, *res;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.data(), std::to_string(port).c_str(), &hints, &res) != 0)
    return false;

  // 2. Create TCP socket
  sock = explo::lib::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(res);
    return false;
  }

  // 3. TCP connect
  if (explo::lib::connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
    freeaddrinfo(res);
    return false;
  }
  freeaddrinfo(res);

  // 4. Wrap socket with TLS
  ssl = SSL_new(ctx);
  SSL_set_fd(ssl, sock);

  // 5. SNI — required by most servers
  SSL_set_tlsext_host_name(ssl, host.data());

  // 6. TLS handshake
  if (SSL_connect(ssl) != 1) {
    ERR_print_errors_fp(stderr);
    return false;
  }

  return true;
}

int OpenSSL_TLSClient::sendData(const std::string &data) { return SSL_write(ssl, data.c_str(), data.size()); }

std::string OpenSSL_TLSClient::recvData(int buf_size) {
  std::stringstream ss;
  std::string result(buf_size, '\0');
  while (true) {
    int bytes_read = SSL_read(ssl, &result[0], buf_size);
    if (bytes_read > 0) {
      ss.write(result.data(), bytes_read);
      if (bytes_read < buf_size)
        break; // No more data available
    } else {
      break; // Error or connection closed
    }
  }
  return ss.str();
}

void OpenSSL_TLSClient::close() {
  if (ssl) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
  }
  if (sock >= 0)
    explo::lib::close(sock);
  if (ctx)
    SSL_CTX_free(ctx);
}

std::string OpenSSL_TLSClient::getLastError() {
  unsigned long errCode = ERR_get_error();
  if (errCode == 0)
    return "No error";
  char buf[256];
  ERR_error_string_n(errCode, buf, sizeof(buf));
  return std::string(buf);
}

OpenSSL_TLSClient *createTLSClient() { return new OpenSSL_TLSClient(); }

} // namespace impl
} // namespace explo::lib
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
