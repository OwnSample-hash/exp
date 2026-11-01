/**
 * @file pipeline.hpp
 * @brief Executes an EncoderDef against a set of resolved inputs, producing a
 * rendered output blob.
 */
#pragma once

#include <codec.hpp>
#include <encoder_def.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace enc {

struct EncodeResult {
  bool ok = false;
  std::string message;      // human-readable status / error detail
  codec::Bytes blob;        // the finished bytes, before formatting
  std::string rendered;     // blob rendered per the output format
  std::string outputFile;   // path written to, when an output file was requested
  std::string outputFormat; // the format used for `rendered`
};

/**
 * @brief Run an encoder.
 * @param def     the encoder description
 * @param inputs  input name -> value (strings as entered by the user)
 * @param workDir scratch directory for compile artifacts
 */
EncodeResult runEncoder(const EncoderDef &def, const std::map<std::string, std::string> &inputs,
                        const std::filesystem::path &workDir);

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
