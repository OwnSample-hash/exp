#include <pipeline.hpp>
#include <process.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace enc {

using codec::Bytes;

namespace {

std::string lookup(const std::map<std::string, std::string> &inputs, const std::string &key,
                   const std::string &fallback = "") {
  auto it = inputs.find(key);
  return it != inputs.end() ? it->second : fallback;
}

// Expand ${name} references using the resolved inputs.
std::string expand(const std::string &tmpl, const std::map<std::string, std::string> &inputs) {
  std::string out;
  out.reserve(tmpl.size());
  for (size_t i = 0; i < tmpl.size();) {
    if (tmpl[i] == '$' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
      const size_t end = tmpl.find('}', i + 2);
      if (end != std::string::npos) {
        out += lookup(inputs, tmpl.substr(i + 2, end - (i + 2)));
        i = end + 1;
        continue;
      }
    }
    out.push_back(tmpl[i++]);
  }
  return out;
}

Bytes readFile(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot read file: " + path.string());
  return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path &path, const Bytes &data,
               std::filesystem::perms perms = std::filesystem::perms::owner_read |
                                              std::filesystem::perms::owner_write) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f)
    throw std::runtime_error("cannot write file: " + path.string());
  f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
  f.close();
  std::filesystem::permissions(path, perms);
}

// Interpret an input value as bytes: "@path" reads a file, "0x.." is hex,
// anything else is taken literally.
Bytes resolveBytes(const std::string &value, const std::filesystem::path &baseDir) {
  if (!value.empty() && value[0] == '@') {
    std::filesystem::path p = value.substr(1);
    if (p.is_relative())
      p = baseDir / p;
    return readFile(p);
  }
  return codec::keyToBytes(value);
}

Bytes applyStep(const TransformStep &step, Bytes data, const std::map<std::string, std::string> &inputs,
                const std::filesystem::path &baseDir) {
  const std::string &op = step.op;
  if (op == "xor") {
    return codec::xorBytes(data, resolveBytes(expand(step.key, inputs), baseDir));
  } else if (op == "add") {
    return codec::addBytes(data, static_cast<uint8_t>(step.amount));
  } else if (op == "sub") {
    return codec::subBytes(data, static_cast<uint8_t>(step.amount));
  } else if (op == "rot") {
    return codec::rotBytes(data, step.amount);
  } else if (op == "reverse") {
    return codec::reverseBytes(data);
  } else if (op == "hex") {
    return codec::toBytes(codec::toHex(data));
  } else if (op == "unhex") {
    return codec::fromHex(codec::toString(data));
  } else if (op == "base64") {
    return codec::toBytes(codec::toBase64(data));
  } else if (op == "unbase64") {
    return codec::fromBase64(codec::toString(data));
  }
  throw std::runtime_error("unknown transform op: " + op);
}

std::string renderFormat(const std::string &format, const Bytes &blob, const std::string &name) {
  if (format == "raw" or format == "exe")
    return codec::formatRaw(blob);
  if (format == "hex")
    return codec::formatHex(blob);
  if (format == "escaped")
    return codec::formatEscaped(blob);
  if (format == "c")
    return codec::formatC(blob, name);
  if (format == "python" || format == "py")
    return codec::formatPython(blob, name);
  if (format == "base64" || format == "b64")
    return codec::toBase64(blob);
  throw std::runtime_error("unknown output format: " + format);
}

Bytes runTransform(const EncoderDef &def, const std::map<std::string, std::string> &inputs) {
  Bytes data = resolveBytes(lookup(inputs, def.dataInput), def.sourceDir);
  for (const auto &step : def.steps)
    data = applyStep(step, std::move(data), inputs, def.sourceDir);
  return data;
}

Bytes runCompile(const EncoderDef &def, const std::map<std::string, std::string> &inputs,
                 const std::filesystem::path &workDir, std::string &status) {
  const std::string target = lookup(inputs, def.targetInput, def.defaultTarget);
  auto tcIt = def.toolchains.find(target);
  if (tcIt == def.toolchains.end())
    throw std::runtime_error("unknown target '" + target + "'");
  const Toolchain &tc = tcIt->second;

  std::string sourceValue = lookup(inputs, def.sourceInput);
  if (sourceValue.empty())
    throw std::runtime_error("no source file provided (input '" + def.sourceInput + "')");
  std::filesystem::path source = sourceValue;
  if (source.is_relative())
    source = def.sourceDir / source;
  if (!std::filesystem::exists(source))
    throw std::runtime_error("source not found: " + source.string());

  std::filesystem::create_directories(workDir);
  const std::filesystem::path artifact = workDir / (def.name + "_" + target + tc.outExt);

  std::vector<std::string> cc = {tc.cc};
  if (tc.mode == "object")
    cc.push_back("-c");
  for (const auto &f : tc.cflags)
    cc.push_back(f);
  cc.push_back(source.string());
  cc.push_back("-o");
  cc.push_back(artifact.string());
  for (const auto &f : tc.ldflags)
    cc.push_back(f);

  ProcResult compileRes = runProcess(cc);
  if (!compileRes.spawned)
    throw std::runtime_error("failed to launch compiler '" + tc.cc + "': " + compileRes.err);
  if (compileRes.exitCode != 0)
    throw std::runtime_error("compiler exited " + std::to_string(compileRes.exitCode) + ":\n" + compileRes.err);

  status = "compiled " + source.filename().string() + " for " + target;

  // Optionally extract a flat binary from the artifact.
  if (!tc.objcopy.empty()) {
    const std::filesystem::path blobFile = workDir / (def.name + "_" + target + ".bin");
    std::vector<std::string> oc = {tc.objcopy};
    if (tc.extract.empty()) {
      oc.push_back("-O");
      oc.push_back("binary");
    } else {
      for (const auto &a : tc.extract)
        oc.push_back(a);
    }
    oc.push_back(artifact.string());
    oc.push_back(blobFile.string());
    ProcResult ocRes = runProcess(oc);
    if (!ocRes.spawned)
      throw std::runtime_error("failed to launch extractor '" + tc.objcopy + "': " + ocRes.err);
    if (ocRes.exitCode != 0)
      throw std::runtime_error("extractor exited " + std::to_string(ocRes.exitCode) + ":\n" + ocRes.err);
    status += ", extracted blob via " + tc.objcopy;
    return readFile(blobFile);
  }
  return readFile(artifact);
}

} // namespace

#define o_rw_ fs::perms::owner_read | std::filesystem::perms::owner_write
#define o_rwx std::filesystem::perms::owner_all

EncodeResult runEncoder(const EncoderDef &def, const std::map<std::string, std::string> &inputs,
                        const std::filesystem::path &workDir) {
  EncodeResult result;
  try {
    std::string status;
    if (def.method == Method::Transform) {
      result.blob = runTransform(def, inputs);
      status = "applied " + std::to_string(def.steps.size()) + " transform step(s)";
    } else if (def.method == Method::Compile) {
      result.blob = runCompile(def, inputs, workDir, status);
    } else {
      result.message = "encoder has no runnable method";
      return result;
    }

    const OutputSpec &out = def.outputs.front();
    result.outputFormat = out.format;
    result.rendered = renderFormat(out.format, result.blob, def.name);

    if (!out.file.empty()) {
      std::filesystem::path outPath = expand(out.file, inputs);
      if (out.format == "raw")
        writeFile(outPath, result.blob);
      else if (out.format == "exe")
        writeFile(outPath, result.blob, o_rwx);
      else
        writeFile(outPath, codec::toBytes(result.rendered));
      result.outputFile = outPath.string();
    }

    result.ok = true;
    result.message = status + "; " + std::to_string(result.blob.size()) + " bytes";
  } catch (const std::exception &e) {
    result.ok = false;
    result.message = e.what();
  }
  return result;
}

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
