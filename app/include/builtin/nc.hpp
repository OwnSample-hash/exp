/**
 * @file nc.hpp
 * @brief Header file for the nc module.
 */
#pragma once

#include <arpa/inet.h>
#include <interfaces/tool.hpp>
#include <memory>
#include <spdlog/logger.h>

namespace explo {
namespace builtin {

struct NCConfig {
  std::string host = "127.0.0.1";
  uint16_t port = htons(80);
  bool isTcp = true;
};

class NC final : public ITool {
  std::shared_ptr<spdlog::logger> logger;
  NCConfig config;

  static void send_all(int fd, const char *data, std::size_t len);
  static void write_all(int fd, const char *data, std::size_t len);
  static void set_nonblocking(int fd);

public:
  NC() {};
  NC(std::shared_ptr<spdlog::logger> logger) : logger(std::move(logger)) {};
  ~NC() override = default;

  const char *getName() const override { return "nc"; }
  const char *getVersion() const override { return "0.0.1"; }

  void initialize() override;
  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void suppress() override;

  const std::vector<std::string> &getTags() const override {
    static std::vector<std::string> tags = {"network", "utility"};
    return tags;
  }
};

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
