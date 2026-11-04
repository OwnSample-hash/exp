/**
 * @file encoder_def.hpp
 * @brief Declarative description of a single encoder, loaded from a YAML file.
 *
 * Each encoder in the encoders directory is one YAML document describing its
 * name, description, inputs, outputs, requirements and the method used to turn
 * inputs into an output blob (a byte transform, or a source compilation).
 */
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace enc {

enum class Method { Transform, Compile, Unknown };

const char *methodName(Method m);
Method methodFromString(const std::string &s);

/** @brief One configurable input, surfaced to the user as a tool variable. */
struct InputSpec {
  std::string name;
  std::string type = "string"; // string | bytes | file | enum | int | list
  std::string description;
  std::string def; // default value, rendered as a string
  bool required = false;
  std::vector<std::string> values; // permitted values when type == enum
};

/** @brief How and where the finished blob is rendered. */
struct OutputSpec {
  std::string name = "output";
  std::string format = "raw"; // raw | hex | escaped | c | python | base64
  std::string file;           // optional output path; may contain ${var}
};

/** @brief A single reversible transform step (method == Transform). */
struct TransformStep {
  std::string op;  // xor | add | sub | rot | reverse | hex | unhex | base64 | unbase64
  std::string key; // key/operand spec, may contain ${var}
  int amount = 0;  // operand for rot/add/sub
};

/** @brief A build + extraction recipe for one target (method == Compile). */
struct Toolchain {
  std::string cc;              // compiler executable
  std::string mode = "object"; // object | executable
  std::vector<std::string> cflags;
  std::vector<std::string> ldflags;
  std::string objcopy;              // extractor (optional)
  std::vector<std::string> extract; // objcopy args between in/out (default: -O binary)
  std::string outExt = ".o";        // artifact extension produced by the compiler
};

struct EncoderDef {
  std::string name;
  std::string version = "0.1.0";
  std::string uuid; // optional; a stable one is derived from the name when empty
  std::string description;
  std::vector<std::string> tags;
  Method method = Method::Transform;

  std::vector<InputSpec> inputs;
  std::vector<OutputSpec> outputs;
  std::vector<std::string> requirements; // external executables this encoder needs

  // method == Transform
  std::vector<TransformStep> steps;
  std::string dataInput = "input"; // the input holding the buffer to transform

  // method == Compile
  std::map<std::string, Toolchain> toolchains; // target name -> recipe
  std::string defaultTarget;
  std::string sourceInput = "source"; // the input holding the source path
  std::string targetInput = "target"; // the input selecting the toolchain

  std::filesystem::path sourceDir; // directory of the YAML file (for relative paths)
};

/**
 * @brief Parse a single encoder YAML file. On failure returns nullopt and sets
 * `error`.
 */
std::optional<EncoderDef> parseEncoderFile(const std::filesystem::path &path, std::string &error);

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
