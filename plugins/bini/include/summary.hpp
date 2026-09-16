#pragma once

#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <map>
#include <string>
#include <vector>

enum class Status {
  NotApplicable = -1,
  None,
  Partial,
  Full,
};

struct summary {
  Status NX;
  Status PIE;
  Status RELRO;
  Status canary;
  Status CFI;
  Status ICallCFI;
  Status VCallCFI;
  Status ShadowCallStack;
  Status SafeStack;
  Status RPATH;
  Status RunPath;
  std::size_t symbolCount;
  std::size_t FortifiedCount;
  std::size_t UnfortifiedCount;
};

using namespace llvm;
using namespace llvm::object;

struct FortifyInfo {
  std::vector<std::string> FortifiedSyms;   // __memcpy_chk etc.
  std::vector<std::string> UnfortifiedSyms; // memcpy when fortified twin exists
  int Total = 0;
  int Fortified = 0;
};

struct CFIInfo {
  bool HasCFI = false;
  bool HasICallCFI = false;        // indirect call protection
  bool HasVCallCFI = false;        // virtual call protection
  bool HasShadowCallStack = false; // return protection
  bool HasSafeStack = false;       // safe stack separation
};

struct RPATHInfo {
  std::vector<std::string> RPaths;   // DT_RPATH entries
  std::vector<std::string> RunPaths; // DT_RUNPATH entries
  bool HasOrigin = false;            // uses $ORIGIN (relative path)
  bool HasAbsolutePath = false;      // hardcoded absolute path (risky)
};

// Known pairs: unfortified -> fortified name
static const std::map<std::string, std::string> FortifyPairs = {
    {"memcpy", "__memcpy_chk"},     {"memmove", "__memmove_chk"},     {"mempcpy", "__mempcpy_chk"},
    {"memset", "__memset_chk"},     {"stpcpy", "__stpcpy_chk"},       {"stpncpy", "__stpncpy_chk"},
    {"strcpy", "__strcpy_chk"},     {"strncpy", "__strncpy_chk"},     {"strcat", "__strcat_chk"},
    {"strncat", "__strncat_chk"},   {"sprintf", "__sprintf_chk"},     {"snprintf", "__snprintf_chk"},
    {"vsprintf", "__vsprintf_chk"}, {"vsnprintf", "__vsnprintf_chk"}, {"fprintf", "__fprintf_chk"},
    {"printf", "__printf_chk"},     {"vfprintf", "__vfprintf_chk"},   {"vprintf", "__vprintf_chk"},
};

FortifyInfo checkFortification(const llvm::object::ObjectFile &Obj);

CFIInfo checkCFI(const llvm::object::ObjectFile &Obj);

template <typename ELFT> StringRef getDynStrTab(const ELFFile<ELFT> &ELF) {
  auto SectionsOrErr = ELF.sections();
  if (!SectionsOrErr)
    return {};

  for (const auto &Shdr : *SectionsOrErr) {
    if (Shdr.sh_type != ELF::SHT_STRTAB)
      continue;

    auto NameOrErr = ELF.getSectionName(Shdr);
    if (!NameOrErr)
      continue;
    if (*NameOrErr != ".dynstr")
      continue;

    auto ContentsOrErr = ELF.getSectionContents(Shdr);
    if (!ContentsOrErr)
      continue;

    return StringRef(reinterpret_cast<const char *>(ContentsOrErr->data()), ContentsOrErr->size());
  }
  return {};
}

template <typename ELFT> RPATHInfo checkRPATH(const ELFFile<ELFT> &ELF) {
  RPATHInfo Info;

  StringRef StrTab = getDynStrTab(ELF);
  if (StrTab.empty())
    return Info;

  auto DynOrErr = ELF.dynamicEntries();
  if (!DynOrErr)
    return Info;

  for (const auto &Dyn : *DynOrErr) {
    if (Dyn.d_tag != ELF::DT_RPATH && Dyn.d_tag != ELF::DT_RUNPATH)
      continue;

    uint64_t Offset = Dyn.d_un.d_val;
    if (Offset >= StrTab.size())
      continue;

    StringRef Path(StrTab.data() + Offset);

    // Split on ':' and store
    while (!Path.empty()) {
      auto [Head, Tail] = Path.split(':');
      std::string P = Head.str();

      if (P.find("$ORIGIN") != std::string::npos)
        Info.HasOrigin = true;
      else if (!P.empty() && P[0] == '/')
        Info.HasAbsolutePath = true;

      if (Dyn.d_tag == ELF::DT_RPATH)
        Info.RPaths.push_back(P);
      else
        Info.RunPaths.push_back(P);

      Path = Tail;
    }
  }

  return Info;
}

template <typename ELFT> summary analyzeELF(const ELFObjectFile<ELFT> &Obj) {
  auto &ELF = Obj.getELFFile();
  summary result;

  auto PhdrsOrErr = ELF.program_headers();
  if (!PhdrsOrErr)
    result.NX = Status::NotApplicable;

  for (const auto &Phdr : *PhdrsOrErr) {
    if (Phdr.p_type == ELF::PT_GNU_STACK) {
      // NX enabled when stack segment is NOT executable
      result.NX = !(Phdr.p_flags & ELF::PF_X) ? Status::Full : Status::None; // Stack is executable, so no NX
      break;
    }
  }

  const auto &Hdr = ELF.getHeader();

  if (Hdr.e_type == ELF::ET_EXEC) {
    result.PIE = Status::None; // Statically linked non-PIE
  }

  if (Hdr.e_type != ELF::ET_DYN)
    result.PIE = Status::NotApplicable; // Not an executable
  bool HasInterp = false;
  for (const auto &Phdr : *PhdrsOrErr)
    if (Phdr.p_type == ELF::PT_INTERP) {
      HasInterp = true;
      break;
    }

  // Optionally confirm via DF_1_PIE in .dynamic
  bool DF1PIE = false;
  for (const auto &Phdr : *PhdrsOrErr) {
    if (Phdr.p_type != ELF::PT_DYNAMIC)
      continue;
    auto DynOrErr = ELF.dynamicEntries();
    if (!DynOrErr)
      break;
    for (const auto &Dyn : *DynOrErr) {
      if (Dyn.d_tag == ELF::DT_FLAGS_1 && (Dyn.d_un.d_val & ELF::DF_1_PIE))
        DF1PIE = true;
    }
  }

  result.PIE = DF1PIE ? Status::Full : Status::None;

  bool HasRelroSeg = false;
  bool HasBindNow = false;

  for (const auto &Phdr : *PhdrsOrErr)
    if (Phdr.p_type == ELF::PT_GNU_RELRO) {
      HasRelroSeg = true;
      break;
    }

  // Check dynamic section for BIND_NOW
  auto DynOrErr = ELF.dynamicEntries();
  if (DynOrErr) {
    for (const auto &Dyn : *DynOrErr) {
      if (Dyn.d_tag == ELF::DT_BIND_NOW) {
        HasBindNow = true;
      }
      if (Dyn.d_tag == ELF::DT_FLAGS && (Dyn.d_un.d_val & ELF::DF_BIND_NOW)) {
        HasBindNow = true;
      }
    }
  }

  if (HasRelroSeg && HasBindNow)
    result.RELRO = Status::Full;
  else if (HasRelroSeg)
    result.RELRO = Status::Partial;
  else
    result.RELRO = Status::None;

  for (const SymbolRef &Sym : Obj.getDynamicSymbolIterators()) {
    auto NameOrErr = Sym.getName();
    if (NameOrErr && NameOrErr->contains("__stack_chk_fail")) {
      result.canary = Status::Full;
      break;
    }
  }

  // Check static symbols (statically linked)
  for (const SymbolRef &Sym : Obj.symbols()) {
    auto NameOrErr = Sym.getName();
    if (NameOrErr && NameOrErr->contains("__stack_chk_fail")) {
      result.canary = Status::Full;
      break;
    }
  }

  FortifyInfo fortifyInfo = checkFortification(Obj);

  result.symbolCount = fortifyInfo.Total;
  result.FortifiedCount = fortifyInfo.Fortified;
  result.UnfortifiedCount = fortifyInfo.Total - fortifyInfo.Fortified;

  CFIInfo cfiInfo = checkCFI(Obj);

  result.CFI = cfiInfo.HasCFI ? Status::Full : Status::None;
  result.ICallCFI = cfiInfo.HasICallCFI ? Status::Full : Status::None;
  result.VCallCFI = cfiInfo.HasVCallCFI ? Status::Full : Status::None;
  result.ShadowCallStack = cfiInfo.HasShadowCallStack ? Status::Full : Status::None;
  result.SafeStack = cfiInfo.HasSafeStack ? Status::Full : Status::None;

  RPATHInfo rpathInfo = checkRPATH(ELF);

  result.RPATH = rpathInfo.RPaths.empty() ? Status::None : Status::Full;
  result.RunPath = rpathInfo.RunPaths.empty() ? Status::None : Status::Full;

  return result;
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
