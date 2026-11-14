#include <atomic>
#include <builtin/simple_ui.hpp>
#include <cmd.hpp>
#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <termios.h>
#endif

namespace explo {
namespace builtin {

void UI::printHelp(const std::vector<std::string> &options) {
  std::cout << "\n[?] Available options:\n";
  if (options.empty()) {
    std::cout << "  (none)\n";
  } else {
    for (const auto &o : options)
      std::cout << "  " << o << "\n";
  }
  std::cout << std::flush;
}

void UI::printAutocomplete(const std::string &buf, bool unique) {
  auto &cp = cmd::CommandProcessor::instance();
  auto vars = cp.vars();
  auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
  std::string prompt = vars.expand(rawPrompt);
  spdlog::trace("Autocomplete: buf='{}', unique={}", buf, unique);
  std::cout << "\r\033[2K" << prompt << buf << std::flush;
}

void UI::printResult(const cmd::ExecutionResult &r) {
  if (!r.message.empty())
    std::cout << r.message << "\n";
  if (!r.success)
    std::cout << "[exit " << r.exitCode << "]\n";
}

void UI::printError(const std::string &msg) { std::cerr << "\033[31m[error] " << msg << "\033[0m\n"; }

void UI::redraw(const std::string &prompt, const std::string &buffer, std::size_t cursorPos) {
  std::cout << "\r\033[0K" << prompt << buffer;
  if (cursorPos != buffer.size())
    std::cout << "\033[" << (buffer.size() - cursorPos) << "D";
  std::cout << std::flush;
}

inline int UI::getch() {
  unsigned char c = 0;
  ssize_t r;
  while ((r = read(STDIN_FILENO, &c, sizeof(c))) < 0) {
    if (errno == EINTR)
      continue; // interrupted by a signal – retry rather than feed a junk byte
    logger->error("Error reading from stdin: {}", strerror(errno));
    return 0;
  }
  return r == 0 ? 0 : c; // r == 0 means EOF
}

void UI::runLoop() {
  logger->info("Starting UI loop...");
  cmd::CommandProcessor &cp = cmd::CommandProcessor::instance();
  std::cout << "Type '?' for help, TAB to autocomplete.\n";

  auto vars = cp.vars();
  auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
  std::string prompt = vars.expand(rawPrompt);
  std::cout << prompt << std::flush;

  int ch = 0;
  std::string buf;
  while ((ch = getch())) {
    if (ch == 0x04 && cp.buffer().empty()) { // Ctrl-D to exit
      std::cout << "\nExiting.\n";
      break;
    }
    if (ch == '\r')
      ch = '\n'; // convert CR to LF

    buf += ch;
    if (ch == '\n') {
      std::cout << "\n" << std::flush;
      if (buf == "run") {
        logger->info("");
        this->setSignalHandling(true);
        buf.clear();
      }
    }

    switch (cp.feed(ch)) {
    case cmd::InputResult::Escape: {
      // Feed the rest of the escape sequence byte-by-byte until the processor
      // reports it is complete (arrow keys, Home/End, Delete, …).
      cmd::InputResult res = cmd::InputResult::Escape;
      while (res == cmd::InputResult::Escape) {
        int ech = getch();
        if (ech <= 0)
          break;
        logger->trace("esc ch: {}", ech);
        res = cp.feed((char)ech);
      }
      auto vars = cp.vars();
      auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
      std::string prompt = vars.expand(rawPrompt);
      redraw(prompt, cp.buffer(), cp.cursorPos());
      break;
    }

    case cmd::InputResult::Help: {
      auto vars = cp.vars();
      auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
      std::string prompt = vars.expand(rawPrompt);
      std::cout << prompt << cp.buffer() << std::flush;
      break;
    }
    case cmd::InputResult::Autocompleted:
    case cmd::InputResult::Error:
      break;
    case cmd::InputResult::Consumed: {
      auto vars = cp.vars();
      auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
      std::string prompt = vars.expand(rawPrompt);
      bool printable = (ch >= 0x20 && ch != 127);
      if (printable && cp.cursorPos() == cp.buffer().size()) {
        // Fast path: an ordinary character appended at the end of the line.
        std::cout << (char)ch << std::flush;
      } else {
        // Any edit that moves the cursor or changes the middle/tail of the
        // line (backspace, Ctrl-K/W, cursor moves, mid-line insert).
        redraw(prompt, cp.buffer(), cp.cursorPos());
      }
      break;
    }
    case cmd::InputResult::Cleared:
      std::cout << "\r\033[K" << prompt << std::flush;
      break;
    case cmd::InputResult::Executed:
      auto vars = cp.vars();
      auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
      std::string prompt = vars.expand(rawPrompt);
      std::cout << prompt << std::flush;
      setSignalHandling(false);
      break;
    }
  }
}

#define SIGS X(0x3, SIGINT) // Ctrl-C

void handleSignal(int *pipes, std::atomic<bool> &running, std::atomic<bool> &ignoring) {
  signal(SIGINT, SIG_IGN); // ignore Ctrl-C in this thread, we handle it manually
  while (running.load()) {
    unsigned char sig = 0;
    ssize_t r = read(STDIN_FILENO, &sig, sizeof(sig));
    if (r < 0) {
      if (errno == EINTR)
        continue; // interrupted by a signal – retry rather than feed a junk byte
      break;
    }
    if (r == 0) {
      break;
    }
    if (ignoring.load()) {
      write(pipes[1], &sig, sizeof(sig));
      continue; // ignore signals while the command processor is executing a command
    }
    switch (sig) {
#define X(num, name)                                                                                                   \
  case num:                                                                                                            \
    kill(0, name);                                                                                                     \
    break;
      SIGS
#undef X
          default : write(pipes[1], &sig, sizeof(sig));
      break;
    }
  }
}

#ifdef __linux__
struct termios origTermios = {};
#endif

void UI::setSignalHandling(bool enabled) { ignoring.store(!enabled); }

void UI::initialize(initArgs &args) {
  if (!basicInitDone) {
    this->logger = args.logger;
    basicInitDone = true;
    logger->info("Initializing simple UI module");
    return;
  }
  assert(this->logger != nullptr);

  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);
  logger->info("Initializing simple UI...");
  logger->debug("Setting terminal to raw mode...");
#ifdef __linux__
  struct termios newTermios;
  tcgetattr(STDIN_FILENO, &origTermios);
  atexit([]() { tcsetattr(STDIN_FILENO, TCSANOW, &origTermios); });
  std::memcpy(&newTermios, &origTermios, sizeof(newTermios));
  // Disable canonical mode and echo so we get bytes as they are typed, and
  // disable ISIG/IEXTEN/IXON so Ctrl-C, Ctrl-V and Ctrl-S/Q are delivered to
  // us as raw bytes (feed() handles them) instead of being eaten by the tty.
  newTermios.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
  newTermios.c_iflag &= ~(IXON);
  tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
#endif
  // signal(SIGINT, SIG_IGN);
  logger->debug("Terminal set to raw mode.");
  logger->debug("Registering command callbacks...");
  cmd::CommandProcessor &cp = cmd::CommandProcessor::instance();
  cp.onHelp(printHelp);
  cp.onAutocomplete(printAutocomplete);
  cp.onExecute(printResult);
  cp.onError(printError);

  if (pipe(pipes) == -1) {
    logger->error("Failed to create pipe: {}", strerror(errno));
  }

  // signalThread = std::thread([this]() {
  //   logger->info("Signal handling thread started.");
  //   handleSignal(pipes, running, ignoring);
  // });
  // signalThread.detach();
}

void UI::shutdown() {
  logger->info("Shutting down simple UI...");
  ignoring.store(true);
  running.store(false);
  close(pipes[0]);
  close(pipes[1]);
  logger->info("Simple UI shutdown complete.");
}

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
