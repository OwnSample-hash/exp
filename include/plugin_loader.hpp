#pragma once
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE LibHandle;
#else
#include <dlfcn.h>
typedef void *LibHandle;
#endif

class PluginLoader {
public:
  static PluginLoader &instance() {
    static PluginLoader loader;
    return loader;
  }

  bool loadPlugin(const std::string &path) {
    LibHandle handle = nullptr;

#ifdef _WIN32
    handle = LoadLibraryA(path.c_str());
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
      lastError_ = dlerror();
      return false;
    }
#endif

    if (handle) {
      handles_.push_back(handle);
      return true;
    }
    return false;
  }

  std::string getLastError() const { return lastError_; }

  ~PluginLoader() {
    for (auto handle : handles_) {
#ifdef _WIN32
      FreeLibrary(handle);
#else
      dlclose(handle);
#endif
    }
  }

  std::vector<LibHandle> GetHandles() const { return handles_; }

private:
  PluginLoader() = default;
  std::vector<LibHandle> handles_;
  std::string lastError_;
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
