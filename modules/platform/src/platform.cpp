#if defined(_WIN32)
const char *platform_name = "Windows";
#elif defined(__APPLE__)
const char *platform_name = "macOS";
#else
const char *platform_name = "Linux/Unix";
#endif

// Vim: set expandtab tabstop=2 shiftwidth=2:
