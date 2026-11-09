#include <atomic>
#include <cmd.hpp>
#include <interfaces/renderer.hpp>

namespace explo {
namespace builtin {

class UI final : public IRenderer {
  static void printHelp(const std::vector<std::string> &options);
  static void printAutocomplete(const std::string &buf, bool unique);
  static void printResult(const cmd::ExecutionResult &r);
  static void printError(const std::string &msg);
  // Redraw the current line (prompt + buffer) and place the terminal cursor at
  // cursorPos within the buffer. Used after any edit that is not a plain
  // append (backspace, delete, cursor moves, line kills, …).
  static void redraw(const std::string &prompt, const std::string &buffer, std::size_t cursorPos);
  inline int getch();

  std::shared_ptr<spdlog::logger> logger;

  bool basicInitDone = false;

  int pipes[2] = {-1, -1};

  std::thread signalThread;
  std::atomic<bool> running = true, ignoring = false;

public:
  UI() = default;
  ~UI() override = default;

  const char *getName() const override { return "simple_ui"; }
  const SemVer getVersion() const override { return {1, 0, 0}; }

  void initialize(initArgs &) override;
  void shutdown() override;

  void runLoop() override;

  ModuleType getModuleType() const override { return ModuleType::RENDERER; }

  void setSignalHandling(bool enabled) override;
};

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
