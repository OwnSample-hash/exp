#include <set>
#include <string>
#include <summary.hpp>

FortifyInfo checkFortification(const llvm::object::ObjectFile &Obj) {
  FortifyInfo Info;
  std::set<std::string> ImportedSyms;

  // Collect all dynamic symbol names
  if (auto *ELFBase = dyn_cast<ELFObjectFileBase>(&Obj))
    for (const SymbolRef &Sym : ELFBase->getDynamicSymbolIterators())
      if (auto NameOrErr = Sym.getName())
        ImportedSyms.insert(NameOrErr->str());

  // Also check static symbols
  for (const SymbolRef &Sym : Obj.symbols())
    if (auto NameOrErr = Sym.getName())
      ImportedSyms.insert(NameOrErr->str());

  for (const auto &[Plain, Chk] : FortifyPairs) {
    bool hasPlain = ImportedSyms.count(Plain);
    bool hasChk = ImportedSyms.count(Chk);

    if (hasChk) {
      Info.FortifiedSyms.push_back(Chk);
      Info.Fortified++;
      Info.Total++;
    }
    if (hasPlain && hasChk) {
      // Both present = some call sites fortified, some weren't (size unknown)
      Info.UnfortifiedSyms.push_back(Plain);
      Info.Total++; // counts as an additional unfortified site
    } else if (hasPlain && !hasChk) {
      // Plain only = no fortification at all for this function
      Info.UnfortifiedSyms.push_back(Plain);
      Info.Total++;
    }
  }

  return Info;
}

CFIInfo checkCFI(const llvm::object::ObjectFile &Obj) {
  CFIInfo Info;
  std::set<std::string> Syms;

  // Collect all symbols
  if (auto *ELFBase = dyn_cast<ELFObjectFileBase>(&Obj))
    for (const SymbolRef &Sym : ELFBase->getDynamicSymbolIterators())
      if (auto N = Sym.getName())
        Syms.insert(N->str());
  for (const SymbolRef &Sym : Obj.symbols())
    if (auto N = Sym.getName())
      Syms.insert(N->str());

  for (const auto &S : Syms) {
    // CFI runtime symbols injected by Clang
    if (S.find("__cfi_check") != std::string::npos)
      Info.HasCFI = Info.HasICallCFI = true;
    if (S.find("__cfi_slowpath") != std::string::npos)
      Info.HasCFI = true;
    if (S.find("__shadow_call_stack") != std::string::npos)
      Info.HasShadowCallStack = true;
    if (S.find("__safestack") != std::string::npos)
      Info.HasSafeStack = true;
    if (S.find("_ZTI") != std::string::npos)
      Info.HasVCallCFI = true; // RTTI for vcall checks
  }

  // Check for .cfi_* sections
  for (const SectionRef &Sec : Obj.sections()) {
    auto NameOrErr = Sec.getName();
    if (!NameOrErr)
      continue;
    if (NameOrErr->starts_with(".cfi"))
      Info.HasCFI = true;
    if (NameOrErr->starts_with(".shadow_cs"))
      Info.HasShadowCallStack = true;
  }

  return Info;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
