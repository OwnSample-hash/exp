#include <cmd.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace explo {

void printHelp(const std::vector<std::string> &options) {
  std::cout << "\n[?] Available options:\n";
  if (options.empty()) {
    std::cout << "  (none)\n";
  } else {
    for (auto &o : options)
      std::cout << "  " << o << "\n";
  }
  std::cout << std::flush;
}

void printAutocomplete(const std::string &buf, bool unique) {
  std::cout << "\r\033[K> " << buf;
  if (!unique)
    std::cout << "\a";
  std::cout << std::flush;
}

void printResult(const cmd::ExecutionResult &r) {
  if (!r.message.empty())
    std::cout << r.message << "\n";
  if (!r.success)
    std::cout << "[exit " << r.exitCode << "]\n";
}

void printError(const std::string &msg) {
  std::cerr << "\033[31m[error] " << msg << "\033[0m\n";
}

inline int getch() {
  int r;
  unsigned char c;
  if ((r = read(0, &c, sizeof(c))) < 0) {
    return r;
  } else {
    return c;
  }
}

void runInteractive(cmd::CommandProcessor &cp) {
  std::cout << "Type '?' for help, TAB to autocomplete.\n";

  auto prompt = cp.vars().get("prompt").has_value()
                    ? cp.vars().get("prompt")->toString()
                    : "> ";
  std::cout << prompt << std::flush;

  int ch;
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

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2:
