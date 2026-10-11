#pragma once

#include <filesystem>
#include <interfaces/tool.hpp>
#include <multivalue.hpp>
#include <python3.14/Python.h>

namespace fs = std::filesystem;

using PythonType = explo::MultiValue<std::monostate, std::string, double, int64_t, bool>;

using namespace explo;

enum class PythonFlagtype {
  STRING,
  NUMBER,
  BOOLEAN,
  INTEGER,
};

class PythonTool final : public explo::ITool {

  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<args::Group> parser;
  fs::path scriptPath;
  std::string prefix;
  std::string name;
  std::string description;
  std::string version;
  std::vector<std::string> tags;
  PyObject *module = nullptr;
  PyObject *executeFunction = nullptr;
  PyObject *initializeFunction = nullptr;
  PyObject *shutdownFunction = nullptr;
  PyThreadState *interpreterState = nullptr;
  PyThreadState *hostThreadState = nullptr;
  bool rootRequired = false;
  bool makeCommand = false;
  bool loaded = false;
  bool valid = true;
  std::optional<args::Command> cmd;
  std::vector<std::tuple<std::unique_ptr<args::ValueFlagBase>, PythonFlagtype>> flags;
  std::unordered_map<std::string, PythonType> varTypes, vars;
  std::unordered_map<std::string, std::string> descs;

  void releasePythonObjects() noexcept;
  bool acquireInterpreter() noexcept;
  void releaseInterpreter() noexcept;
  bool setError(const std::string &message);
  bool requireCallable(PyObject *dict, const char *key, PyObject *&value);

public:
  explicit PythonTool(const fs::path &path, std::shared_ptr<spdlog::logger> logger, std::shared_ptr<args::Group> parser)
      : scriptPath(path), logger(logger), parser(parser) {}

  ~PythonTool() override { shutdown(); }

  const char *getName() const override { return name.c_str(); }

  const SemVer getVersion() const override { return {version}; }

  void initialize(initArgs &) override;

  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void suppress() override;

  const std::vector<std::string> &getTags() const override { return tags; }

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  bool cmdCheck() override { return makeCommand; }

  const uuids::uuid getUUID() override { return uuids::uuid{}; }

  bool isValid() const { return valid; }

  bool isLoaded() const { return loaded; }
};