#include <encoder.hpp>

#include <config/config.hpp>
#include <encoder_def.hpp>
#include <filesystem>

using namespace std::string_literals;

void encoder::initialize(initArgs &args) {
  this->logger = args.logger;
  this->parser = args.parser;
  Config &instance = Config::instance();

  instance.addHive("encoder", "encoder",
                   ConfigMap{
#define X(name, value) {name, value},
                       CONFIG_ENCODER_OPTS
#undef X
                   });

  if (!instance.loadHive("encoder", "encoder", "yaml")) {
    logger->warn("Failed to load encoder config hive; writing defaults");
    instance.saveConfigFile("encoder");
  } else {
    logger->info("Loaded encoder config hive");
  }

  explo::Hive &hive = instance.get("encoder");

  this->directory.emplace(*parser, "directory", "Directory to scan for encoder definitions. Default: " CONFIG_ENCODER_DIRECTORY,
                          args::Matcher{"encoder-directory"}, std::string(CONFIG_ENCODER_DIRECTORY));
  this->workDir.emplace(*parser, "work_dir", "Scratch directory for compile artifacts. Default: " CONFIG_ENCODER_WORK_DIR,
                        args::Matcher{"encoder-work-dir"}, std::string(CONFIG_ENCODER_WORK_DIR));

  const std::string directoryStr =
      hive.at<std::string>("directory", *directory).get<std::string>(CONFIG_ENCODER_DIRECTORY);
  const std::filesystem::path workDirPath =
      hive.at<std::string>("work_dir", *workDir).get<std::string>(CONFIG_ENCODER_WORK_DIR);

  if (!std::filesystem::is_directory(directoryStr)) {
    logger->warn("Encoder definition directory '{}' does not exist; no encoders loaded", directoryStr);
    return;
  }

  for (const auto &entry : std::filesystem::recursive_directory_iterator(directoryStr)) {
    if (!entry.is_regular_file())
      continue;
    const auto ext = entry.path().extension();
    if (ext != ".yaml" && ext != ".yml")
      continue;

    std::string error;
    auto def = enc::parseEncoderFile(entry.path(), error);
    if (!def) {
      logger->error("Skipping encoder definition '{}': {}", entry.path().string(), error);
      continue;
    }
    if (Tools.count(def->name)) {
      logger->error("Duplicate encoder name '{}' from '{}'; skipping", def->name, entry.path().string());
      continue;
    }

    auto tool = std::make_shared<EncoderTool>(std::move(*def), logger, workDirPath);
    tool->initialize(args);
    if (tool->isValid()) {
      logger->info("Loaded encoder: {}", tool->getName());
      Tools.emplace(tool->getName(), std::move(tool));
    } else {
      logger->error("Failed to initialize encoder from '{}'", entry.path().string());
    }
  }

  logger->info("Loaded {} encoder(s) from '{}'", Tools.size(), directoryStr);
}

void encoder::shutdown() {
  logger->trace("Shutting down encoder provider ({} tools)", Tools.size());
  for (const auto &[name, tool] : Tools)
    tool->shutdown();
}

static ToolProviderRegistry::Add<encoder> encoderRegistry("encoder");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
