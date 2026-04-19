#include <builtin/simple_ui.hpp>
#include <cmd.hpp>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace explo {
namespace builtin {

void UI::printHelp(const std::vector<std::string> &options) {
  std::cout << "\n[?] Available options:\n";
  if (options.empty()) {
    std::cout << "  (none)\n";
  } else {
    for (auto &o : options)
      std::cout << "  " << o << "\n";
  }
  std::cout << std::flush;
}

void UI::printAutocomplete(const std::string &buf, bool unique) {
  auto &cp = cmd::CommandProcessor::instance();
  auto prompt = cp.vars().get("prompt").has_value()
                    ? cp.vars().get("prompt")->toString()
                    : "> ";
  if (unique)
    std::cout << "\r\033[K" << prompt << buf;
  else {
    std::cout << "\a";
  }
  std::cout << std::flush;
}

void UI::printResult(const cmd::ExecutionResult &r) {
  if (!r.message.empty())
    std::cout << r.message << "\n";
  if (!r.success)
    std::cout << "[exit " << r.exitCode << "]\n";
}

void UI::printError(const std::string &msg) {
  std::cerr << "\033[31m[error] " << msg << "\033[0m\n";
}

inline int UI::getch() {
  int r = 0;
  unsigned char c = 0;
  if ((r = read(STDIN_FILENO, &c, sizeof(c))) < 0) {
    logger->error("Error reading from stdin: {}", strerror(errno));
    return r;
  } else {
    return c;
  }
}

void UI::runLoop() {
  logger->info("Starting UI loop...");
  cmd::CommandProcessor &cp = cmd::CommandProcessor::instance();
  std::cout << "Type '?' for help, TAB to autocomplete.\n";

  auto prompt = cp.vars().get("prompt").has_value()
                    ? cp.vars().get("prompt")->toString()
                    : "> ";
  std::cout << prompt << std::flush;

  int ch = 0;
  while ((ch = getch())) {
    if (ch == '' && cp.buffer().empty()) { // Ctrl-D to exit
      std::cout << "\nExiting.\n";
      break;
    }
    if (ch == '\r')
      ch = '\n'; // convert CR to LF

    if (ch == '\n') {
      std::cout << "\n" << std::flush;
    }

    switch (cp.feed(ch)) {
    case cmd::InputResult::Escape: {
      int ch = getch();
      auto res = cp.feed(ch);
      while (res == cmd::InputResult::Escape) {
        ch = getch();
        res = cp.feed(ch);
      }
      auto prompt = cp.vars().get("prompt").has_value()
                        ? cp.vars().get("prompt")->toString()
                        : "> ";
      std::cout << "\r\033[K" << prompt << cp.buffer() << std::flush;
      break;
    }

    case cmd::InputResult::Help: {
      auto prompt = cp.vars().get("prompt").has_value()
                        ? cp.vars().get("prompt")->toString()
                        : "> ";
      std::cout << prompt << cp.buffer() << std::flush;
      break;
    }
    case cmd::InputResult::Autocompleted:
    case cmd::InputResult::Error:
      break;
    case cmd::InputResult::Consumed:
      if (ch == '\b' || ch == 127) {
        auto prompt = cp.vars().get("prompt").has_value()
                          ? cp.vars().get("prompt")->toString()
                          : "> ";
        std::cout << "\r\033[K" << prompt << std::flush;
        std::cout << cp.buffer() << std::flush;
      } else {
        std::cout << (char)ch << std::flush;
      }
      break;
    case cmd::InputResult::Cleared:
      std::cout << "\r\033[K" << prompt << std::flush;
      break;
    case cmd::InputResult::Executed:
      auto prompt = cp.vars().get("prompt").has_value()
                        ? cp.vars().get("prompt")->toString()
                        : "> ";
      std::cout << prompt << std::flush;
      break;
    }
  }
}

struct termios origTermios = {};

void UI::initialize() {
  logger->info("Initializing simple UI...");
  logger->debug("Setting terminal to raw mode...");
  struct termios newTermios;
  tcgetattr(STDIN_FILENO, &origTermios);
  atexit([]() { tcsetattr(STDIN_FILENO, TCSANOW, &origTermios); });
  std::memcpy(&newTermios, &origTermios, sizeof(newTermios));
  newTermios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
  signal(SIGINT, SIG_IGN);
  logger->debug("Terminal set to raw mode.");
  logger->debug("Registering command callbacks...");
  cmd::CommandProcessor &cp = cmd::CommandProcessor::instance();
  cp.onHelp(printHelp);
  cp.onAutocomplete(printAutocomplete);
  cp.onExecute(printResult);
  cp.onError(printError);
}

} // namespace builtin
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
