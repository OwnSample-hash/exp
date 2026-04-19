#include <loader.hpp>

void luaLoader::initialize() {
  this->logger->info("Initializing Loader v{}...", getVersion());
}

void luaLoader::shutdown() {
  // Perform any necessary cleanup here
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
