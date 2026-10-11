#include <config/config.hpp>
#include <python_exp.hpp>
#include <string>
#include <tool.hpp>

using namespace std::string_literals;

void python_exp::initialize(initArgs &args) {
  this->logger = args.logger;
  this->parser = args.parser;
  Config &instance = Config::instance();

  instance.addHive("python_exp", "python_exp",
                   ConfigMap{
#define X(name, value) {name, value},
                       CONFIG_PYTHON_EXP_OPTS
#undef X
                   });

  if (!instance.loadHive("python_exp", "python_exp", "yaml")) {
    logger->warn("Failed to load python_exp config hive");
    instance.saveConfigFile("python_exp");
  } else {
    logger->info("Loaded python_exp config hive");
  }

  explo::Hive &hive = instance.get("python_exp");

  this->directory.emplace(*parser, "directory",
                          "Directory to search for Python scripts. Default:" CONFIG_PYTHON_EXP_DIRECTORY,
                          args::Matcher{"directory"}, std::string(CONFIG_PYTHON_EXP_DIRECTORY));

  this->lib_dir.emplace(*parser, "lib_dir",
                        "Directory containing Python libraries. Default: " CONFIG_PYTHON_EXP_LIB_DIR,
                        args::Matcher{"lib_dir"}, std::string(CONFIG_PYTHON_EXP_LIB_DIR));

  std::string directoryStr =
      hive.at<std::string>("directory", *directory).get<std::string>(CONFIG_PYTHON_EXP_DIRECTORY);

  std::string libDirStr = hive.at<std::string>("lib_dir", *lib_dir).get<std::string>(CONFIG_PYTHON_EXP_LIB_DIR);

  for (const auto &entry : std::filesystem::recursive_directory_iterator(directoryStr)) {
    if (entry.is_directory() && std::filesystem::is_regular_file(entry.path() / "__init__.py")) {
      logger->info("Found Python tool package: {}", entry.path().string());
      auto tool = std::make_shared<PythonTool>(entry.path(), logger, parser);
      tool->initialize(args);
      if (tool->isValid() && tool->isLoaded())
        Tools.emplace(tool->getName(), std::move(tool));
      else {
        logger->error("Failed to load Python tool: {}", tool->getName());
        tool->shutdown();
      }
    }
  }
  logger->info("Loaded {} Python tools from directory: {}", Tools.size(), directoryStr);
}

void python_exp::shutdown() {
  logger->trace("Shutting down python_exp tool provider {}", Tools.size());
  for (const auto &[name, tool] : Tools) {
    logger->trace("Shutting down Python tool: {}", name);
    tool->shutdown();
  }
}

static ToolProviderRegistry::Add<python_exp> python_expRegistry("python_exp");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
