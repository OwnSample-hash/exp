#include <cmd.hpp>

namespace explo {

void printHelp(const std::vector<std::string> &options);
void printAutocomplete(const std::string &buf, bool unique);
void printResult(const cmd::ExecutionResult &r);
void printError(const std::string &msg);
inline int getch();
void runInteractive(cmd::CommandProcessor &cp);

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
