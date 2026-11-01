#include <encoder_def.hpp>

#include <yaml-cpp/yaml.h>

namespace enc {

const char *methodName(Method m) {
  switch (m) {
  case Method::Transform:
    return "transform";
  case Method::Compile:
    return "compile";
  default:
    return "unknown";
  }
}

Method methodFromString(const std::string &s) {
  if (s == "transform")
    return Method::Transform;
  if (s == "compile")
    return Method::Compile;
  return Method::Unknown;
}

namespace {

std::vector<std::string> asStringList(const YAML::Node &node) {
  std::vector<std::string> out;
  if (!node)
    return out;
  if (node.IsSequence())
    for (const auto &e : node)
      out.push_back(e.as<std::string>());
  else if (node.IsScalar())
    out.push_back(node.as<std::string>());
  return out;
}

// A YAML scalar may legitimately be a string, number or bool; render it as a
// string so a single `std::string def` field can carry any default.
std::string scalarToString(const YAML::Node &node) {
  if (!node || !node.IsScalar())
    return "";
  return node.Scalar();
}

} // namespace

std::optional<EncoderDef> parseEncoderFile(const std::filesystem::path &path, std::string &error) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path.string());
  } catch (const std::exception &e) {
    error = std::string("YAML parse error: ") + e.what();
    return std::nullopt;
  }
  if (!root || !root.IsMap()) {
    error = "encoder definition must be a YAML mapping";
    return std::nullopt;
  }

  EncoderDef def;
  def.sourceDir = path.parent_path();

  try {
    if (!root["name"] || !root["name"].IsScalar()) {
      error = "encoder definition requires a 'name'";
      return std::nullopt;
    }
    def.name = root["name"].as<std::string>();
    if (root["version"])
      def.version = root["version"].as<std::string>();
    if (root["uuid"])
      def.uuid = root["uuid"].as<std::string>();
    if (root["description"])
      def.description = root["description"].as<std::string>();
    def.tags = asStringList(root["tags"]);
    def.requirements = asStringList(root["requirements"]);

    def.method = methodFromString(root["method"] ? root["method"].as<std::string>() : "transform");
    if (def.method == Method::Unknown) {
      error = "unknown method '" + root["method"].as<std::string>() + "' (expected transform or compile)";
      return std::nullopt;
    }

    for (const auto &in : (root["inputs"] ? root["inputs"] : YAML::Node(YAML::NodeType::Sequence))) {
      InputSpec spec;
      spec.name = in["name"].as<std::string>();
      if (in["type"])
        spec.type = in["type"].as<std::string>();
      if (in["description"])
        spec.description = in["description"].as<std::string>();
      if (in["default"])
        spec.def = scalarToString(in["default"]);
      if (in["required"])
        spec.required = in["required"].as<bool>();
      spec.values = asStringList(in["values"]);
      def.inputs.push_back(std::move(spec));
    }

    for (const auto &out : (root["outputs"] ? root["outputs"] : YAML::Node(YAML::NodeType::Sequence))) {
      OutputSpec spec;
      if (out["name"])
        spec.name = out["name"].as<std::string>();
      if (out["format"])
        spec.format = out["format"].as<std::string>();
      if (out["file"])
        spec.file = out["file"].as<std::string>();
      def.outputs.push_back(std::move(spec));
    }
    if (def.outputs.empty())
      def.outputs.push_back(OutputSpec{}); // default: raw to stdout

    if (def.method == Method::Transform) {
      if (root["data_input"])
        def.dataInput = root["data_input"].as<std::string>();
      for (const auto &st : (root["steps"] ? root["steps"] : YAML::Node(YAML::NodeType::Sequence))) {
        TransformStep step;
        step.op = st["op"].as<std::string>();
        if (st["key"])
          step.key = scalarToString(st["key"]);
        if (st["amount"])
          step.amount = st["amount"].as<int>();
        def.steps.push_back(std::move(step));
      }
      if (def.steps.empty()) {
        error = "transform encoder '" + def.name + "' has no steps";
        return std::nullopt;
      }
    } else { // Compile
      if (root["source_input"])
        def.sourceInput = root["source_input"].as<std::string>();
      if (root["target_input"])
        def.targetInput = root["target_input"].as<std::string>();
      if (root["default_target"])
        def.defaultTarget = root["default_target"].as<std::string>();
      const YAML::Node tc = root["toolchains"];
      if (!tc || !tc.IsMap() || tc.size() == 0) {
        error = "compile encoder '" + def.name + "' requires a non-empty 'toolchains' map";
        return std::nullopt;
      }
      for (const auto &kv : tc) {
        Toolchain chain;
        const YAML::Node &n = kv.second;
        chain.cc = n["cc"] ? n["cc"].as<std::string>() : "cc";
        if (n["mode"])
          chain.mode = n["mode"].as<std::string>();
        chain.cflags = asStringList(n["cflags"]);
        chain.ldflags = asStringList(n["ldflags"]);
        if (n["objcopy"])
          chain.objcopy = n["objcopy"].as<std::string>();
        chain.extract = asStringList(n["extract"]);
        if (n["out_ext"])
          chain.outExt = n["out_ext"].as<std::string>();
        def.toolchains.emplace(kv.first.as<std::string>(), std::move(chain));
      }
      if (def.defaultTarget.empty())
        def.defaultTarget = def.toolchains.begin()->first;
    }
  } catch (const std::exception &e) {
    error = std::string("malformed encoder definition: ") + e.what();
    return std::nullopt;
  }

  return def;
}

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
