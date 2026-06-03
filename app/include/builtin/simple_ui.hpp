#include "interfaces/renderer.hpp"
#include <cmd.hpp>

namespace explo {
namespace builtin {

class UI final : public IRenderer {
  static void printHelp(const std::vector<std::string> &options);
  static void printAutocomplete(const std::string &buf, bool unique);
  static void printResult(const cmd::ExecutionResult &r);
  static void printError(const std::string &msg);
  inline int getch();

  std::shared_ptr<spdlog::logger> logger;

public:
  UI() = default;
  UI(std::shared_ptr<spdlog::logger> logger) : logger(logger) {};
  ~UI() override = default;

  const char *getName() const override { return "simple_ui"; }
  const char *getVersion() const override { return "1.0.0"; }

  void initialize() override;
  void shutdown() override {};

  void runLoop() override;
};

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
