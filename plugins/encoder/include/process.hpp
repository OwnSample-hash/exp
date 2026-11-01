/**
 * @file process.hpp
 * @brief Minimal, shell-free subprocess runner used by the compile pipeline.
 *
 * Commands are executed with execvp() over an explicit argv vector, never
 * through a shell, so values taken from encoder definitions or user input are
 * passed as literal arguments and are not subject to shell interpolation.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enc {

struct ProcResult {
  bool spawned = false; // the child was successfully started
  int exitCode = -1;    // process exit status (or -signal when killed)
  std::vector<uint8_t> out;
  std::string err;

  bool ok() const { return spawned && exitCode == 0; }
};

/**
 * @brief Run argv[0] with the given arguments, optionally feeding `input` to its
 * stdin, and capture stdout (bytes) and stderr (text).
 */
ProcResult runProcess(const std::vector<std::string> &argv, const std::vector<uint8_t> &input = {});

/** @brief Resolve an executable against $PATH; returns "" when not found. */
std::string which(const std::string &exe);

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
