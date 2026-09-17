#include "interfaces/mod.hpp"
#include <bini.hpp>
#include <cmd.hpp>
#include <iostream>
#include <plugin_interface.hpp>
#include <summary.hpp>

bool bini::cmdCheck() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  if (biniCommand && !(*biniCommand)) {
    return false;
  }
  this->invoke("bini", true);
  vars.set("bini.type", cmd::VarValue(type));
  for (const auto &target : targets) {
    vars.set("bini.target", cmd::VarValue(target));
    this->execute();
  }
  this->suppress();
  return true;
}

void bini::initialize(initArgs &args) {
  this->logger = args.logger;
  {
    auto &cp = cmd::CommandProcessor::instance();
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run the tool";
      c.handler = [&](const cmd::ExecutionContext &ctx) -> std::string {
        this->execute();
        return "Tool executed successfully.";
      };
      ctx->registerCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "info";
      c.description = "Get information about the tool";
      c.handler = [&](const cmd::ExecutionContext &ctx) -> std::string {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
        const char info[] = {
#embed "../info.txt"
            , 0x0};
#pragma clang diagnostic pop
        return std::string(info);
      };
      ctx->registerCommand(c);
    }
    cp.registerContext(ctx);
  }
  biniCommand.emplace(*args.parser, "bini", "Binary info tool", [&](args::Subparser &s) {
    args::ValueFlag<std::string> type(s, "type", "Type of analysis to perform (summary, functions, strings)",
                                      {'t', "type"}, std::string("summary"));
    args::PositionalList<std::string> targetsList(s, "args", "Targets for bini");
    args::HelpFlag help(s, "help", "Display this help menu", {'h', "help"});
    s.Parse();
    this->targets.assign(targetsList.begin(), targetsList.end());
    this->type = type.Get();
  });
  logger->info("Tool {} initialized successfully.", this->getName());
}

void bini::invoke(std::string_view prefix, bool soft) {
  this->prefix = prefix;
  auto &vars = cmd::CommandProcessor::instance().vars();
  if (soft && vars.get(this->prefix + ".target").has_value()) {
    this->logger->info("Variable '{}' already exists, skipping due to soft invoke", this->prefix + ".target");
  } else {
    vars.set(this->prefix + ".target", cmd::VarValue{"a.out"});
  }
  if (soft && vars.get(this->prefix + ".type").has_value()) {
    this->logger->info("Variable '{}' already exists, skipping due to soft invoke", this->prefix + ".type");
  } else {
    vars.set(this->prefix + ".type", cmd::VarValue{"summary"});
  }
}

void bini::shutdown() { this->logger->info("Shutting down tool: {}", this->getName()); }

void bini::suppress() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  vars.unset(prefix + ".target");
  vars.unset(prefix + ".type");
}

void bini::execute() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  auto target = vars.get(prefix + ".target")->toString();
  auto type = vars.get(prefix + ".type")->toString();

  auto BufferOrErr = MemoryBuffer::getFile(target);
  if (!BufferOrErr) {
    logger->error("Failed to open file: " + target + ": " + BufferOrErr.getError().message());
    std::cerr << "Error opening file: " << BufferOrErr.getError().message() << "\n";
    return;
  }

  auto objOrErr = ObjectFile::createObjectFile(BufferOrErr->get()->getMemBufferRef());
  if (!objOrErr) {
    logger->error("Failed to create object file: " + llvm::toString(objOrErr.takeError()));
    std::cerr << "Error opening file: " << BufferOrErr.getError().message();
    return;
  }

  std::unique_ptr<ObjectFile> obj = std::move(*objOrErr);

  if (!(obj->getFileFormatName() == "elf32-x86-64" || obj->getFileFormatName() == "elf64-x86-64")) {
    logger->error("Unsupported file format: " + obj->getFileFormatName().str());
    std::cerr << "Unsupported file format: " << obj->getFileFormatName().str() << "\n";
    return;
  }

  if (type == "summary") {
    summary sum;
    if (auto E = dyn_cast<ELFObjectFile<ELF32LE>>(obj))
      sum = analyzeELF(*E);
    else if (auto E = dyn_cast<ELFObjectFile<ELF64LE>>(obj))
      sum = analyzeELF(*E);
    else if (auto E = dyn_cast<ELFObjectFile<ELF32BE>>(obj))
      sum = analyzeELF(*E);
    else if (auto E = dyn_cast<ELFObjectFile<ELF64BE>>(obj))
      sum = analyzeELF(*E);
    else {
      logger->error("Unsupported ELF format: " + obj->getFileFormatName().str());
      return;
    }

    auto toStr = [](Status s) {
      switch (s) {
      case Status::NotApplicable:
        return "\033[34mN/A\033[0m";
      case Status::None:
        return "\033[31mNo\033[0m";
      case Status::Partial:
        return "\033[33mPartial\033[0m";
      case Status::Full:
        return "\033[32mYes\033[0m";
      }
      return "Unknown";
    };

    std::cout << "Summary:\n";
    std::cout << "  RELRO:  " << toStr(sum.RELRO) << "\n";
    std::cout << "  Canary: " << toStr(sum.canary) << "\n";
    std::cout << "  CFI:    " << toStr(sum.CFI) << "\n";
    std::cout << "  ICFI:   " << toStr(sum.ICallCFI) << "\n";
    std::cout << "  VCFI:   " << toStr(sum.VCallCFI) << "\n";
    std::cout << "  ShadowCallStack: " << toStr(sum.ShadowCallStack) << "\n";
    std::cout << "  SafeStack: " << toStr(sum.SafeStack) << "\n";
    std::cout << "  NX:     " << toStr(sum.NX) << "\n";
    std::cout << "  PIE:    " << toStr(sum.PIE) << "\n";
    std::cout << "  RPATH:  " << toStr(sum.RPATH) << "\n";
    std::cout << "  RunPath: " << toStr(sum.RunPath) << "\n";
    std::cout << "  Total symbols checked: " << sum.symbolCount << "\n";
    std::cout << "  Fortified symbols: " << sum.FortifiedCount << "/" << sum.symbolCount << " ("
              << (sum.symbolCount > 0 ? (sum.FortifiedCount * 100 / sum.symbolCount) : 0) << "%)\n";
  } else {
    llvm::outs() << "Detailed information not implemented yet.\n";
  }
}

static ToolRegistry::Add<bini> biniToolRegister("bini");
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
