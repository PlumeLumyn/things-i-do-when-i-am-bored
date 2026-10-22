#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>

#endif

inline std::string getExecutablePath() {
#if defined(_WIN32) || defined(_WIN64)
  char path[MAX_PATH];
  if (GetModuleFileNameA(NULL, path, MAX_PATH) == 0) {
    throw std::runtime_error("Failed to get executable path");
  }
  return std::string(path);
#else
  char path[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
  if (count == -1) {
    throw std::runtime_error("Failed to get executable path");
  }
  return std::string(path, count);
#endif
}

inline std::string getDirectoryFromPath(const std::string &path) {
  size_t lastSlashIndex = path.find_last_of(
#if defined(_WIN32) || defined(_WIN64)
      '\\'
#else
      '/'
#endif
  );
  if (lastSlashIndex == std::string::npos) {
    throw std::runtime_error("Failed to determine directory from path");
  }
  return path.substr(0, lastSlashIndex);
}

inline void setCurrentWorkingDirectory(const std::string &directory) {
#if defined(_WIN32) || defined(_WIN64)
  if (!SetCurrentDirectoryA(directory.c_str())) {
    throw std::runtime_error("Failed to set current working directory");
  }
#else
  if (chdir(directory.c_str()) != 0) {
    throw std::runtime_error("Failed to set current working directory");
  }
#endif
}
