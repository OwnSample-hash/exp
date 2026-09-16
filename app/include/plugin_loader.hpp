/**
 * @file
 * @brief PluginLoader class for loading dynamic libraries (plugins) at runtime
 * in a cross-platform manner.
 */

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

/**
 * @class PluginLoader
 * @brief The PluginLoader class is responsible for loading dynamic libraries
 * (plugins) at runtime.
 */
class PluginLoader {
public:
  /**
   * @brief Returns a reference to the singleton instance of the PluginLoader
   * class.
   *
   * @return A reference to the singleton instance of the PluginLoader class.
   */
  static PluginLoader &instance() {
    static PluginLoader loader;
    return loader;
  }

  /**
   * @brief Loads a plugin from the specified path. This function attempts to
   * load a dynamic library (plugin) from the given path and stores the handle
   * to the loaded library in a vector for later use. If the loading fails, it
   * stores the error message in the lastError_ member variable for retrieval.
   * The function returns true if the plugin is loaded successfully and false
   * otherwise.
   *
   * @param path The file path to the plugin (dynamic library) that should be
   * loaded.
   * @return true if the plugin is loaded successfully, false otherwise.
   */
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

  /**
   * @brief Returns the last error message that occurred during plugin loading.
   *
   * @return A string containing the last error message that occurred during
   * plugin loading. If no error has occurred, it returns an empty string.
   */
  std::string getLastError() const { return lastError_; }

  /**
   * @brief Destructor for the PluginLoader class. This destructor is
   * responsible for cleaning up any resources used by the PluginLoader,
   * specifically by closing all loaded plugin handles.
   */
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
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
