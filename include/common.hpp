// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifndef EASY_CONTROL_INCLUDE_COMMON_H
#define EASY_CONTROL_INCLUDE_COMMON_H

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "macro.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <knownfolders.h>
#include <processthreadsapi.h>
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#else
#error "Unsupported platform"
#endif

namespace autoalg {
// =====================
// Thread & Time
// =====================

EC_INLINE void SleepSeconds(const unsigned int s) { std::this_thread::sleep_for(std::chrono::seconds(s)); }

EC_INLINE void SleepMillis(const unsigned int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

EC_INLINE void ThreadYield() { std::this_thread::yield(); }

EC_INLINE uint64_t NowUnixMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

EC_INLINE uint64_t NowSteadyMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

EC_INLINE uint64_t ThisThreadId() { return std::hash<std::thread::id>{}(std::this_thread::get_id()); }

EC_INLINE unsigned NumHWThreads() {
  const unsigned n = std::thread::hardware_concurrency();
  return n ? n : 1u;
}

// =====================
// Paths & Files
// =====================

#if defined(_WIN32)
EC_INLINE constexpr char PathSep() { return '\\'; }
#else
EC_INLINE constexpr char PathSep() { return '/'; }
#endif

EC_INLINE bool FileExists(const std::filesystem::path &p) {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}

EC_INLINE bool CreateDirs(const std::filesystem::path &p) {
  std::error_code ec;
  return std::filesystem::create_directories(p, ec) || std::filesystem::exists(p, ec);
}

EC_INLINE bool RemoveFile(const std::filesystem::path &p) {
  std::error_code ec;
  return std::filesystem::remove(p, ec);
}

// Executable absolute path (no PATH_MAX)
EC_INLINE std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
  std::wstring buf(256, L'\0');
  for (;;) {
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) return {};
    if (len < buf.size() - 1) {
      buf.resize(len);
      break;
    }
    buf.resize(buf.size() * 2);
  }
  return std::filesystem::path(buf);

#elif defined(__APPLE__)
  uint32_t size = 0;
  (void)_NSGetExecutablePath(nullptr, &size);
  std::vector<char> tmp(size + 1);
  if (_NSGetExecutablePath(tmp.data(), &size) != 0) return {};
  tmp[size] = '\0';
  std::error_code ec;
  auto p = std::filesystem::path(tmp.data());
  auto canon = std::filesystem::weakly_canonical(p, ec);
  return ec ? p : canon;

#elif defined(__linux__)
  std::vector<char> buf(256);
  for (;;) {
    const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n < 0) return {};
    if (n < static_cast<ssize_t>(buf.size() - 1)) {
      buf[n] = '\0';
      return std::filesystem::path(buf.data());
    }
    buf.resize(buf.size() * 2);
  }
#endif
}

// User home directory
EC_INLINE std::filesystem::path HomeDir() {
#if defined(_WIN32)
  PWSTR wpath = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &wpath)) && wpath) {
    std::filesystem::path p(wpath);
    CoTaskMemFree(wpath);
    return p;
  }
  if (const char *up = std::getenv("USERPROFILE")) return std::filesystem::path(up);
  return {};
#else
  if (const char *h = std::getenv("HOME")) return std::filesystem::path(h);
  return {};
#endif
}

// Temporary directory
EC_INLINE std::filesystem::path TempDir() {
  std::error_code ec;
  auto p = std::filesystem::temp_directory_path(ec);
  return ec ? std::filesystem::path{} : p;
}

// =====================
// Environment variables (UTF-8 on Windows)
// =====================

#if defined(_WIN32)
namespace detail {
EC_INLINE std::wstring Utf8ToW(std::string_view s) {
  if (s.empty()) return std::wstring();
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), wlen);
  return w;
}

EC_INLINE std::string WToUtf8(std::wstring_view w) {
  if (w.empty()) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
  return s;
}
}  // namespace detail
#endif

EC_INLINE std::string GetEnv(std::string_view key) {
#if defined(_WIN32)
  std::wstring wkey = detail::Utf8ToW(key);
  DWORD n = GetEnvironmentVariableW(wkey.c_str(), nullptr, 0);
  if (n == 0) return {};
  std::wstring wval(n, L'\0');
  GetEnvironmentVariableW(wkey.c_str(), wval.data(), n);
  if (!wval.empty() && wval.back() == L'\0') wval.pop_back();
  return detail::WToUtf8(wval);
#else
  if (const char *v = std::getenv(std::string(key).c_str())) return std::string(v);
  return {};
#endif
}

EC_INLINE bool SetEnv(std::string_view key, std::string_view val, bool overwrite = true) {
#if defined(_WIN32)
  std::wstring wkey = detail::Utf8ToW(key);
  std::wstring wval = detail::Utf8ToW(val);
  if (!overwrite) {
    DWORD n = GetEnvironmentVariableW(wkey.c_str(), nullptr, 0);
    if (n != 0) return true;  // already set
  }
  return SetEnvironmentVariableW(wkey.c_str(), wval.c_str()) != 0;
#else
  return ::setenv(std::string(key).c_str(), std::string(val).c_str(), overwrite ? 1 : 0) == 0;
#endif
}

EC_INLINE bool UnsetEnv(std::string_view key) {
#if defined(_WIN32)
  std::wstring wkey = detail::Utf8ToW(key);
  return SetEnvironmentVariableW(wkey.c_str(), nullptr) != 0;
#else
  return ::unsetenv(std::string(key).c_str()) == 0;
#endif
}

// =====================
// Memory
// =====================

EC_INLINE size_t PageSize() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return static_cast<size_t>(si.dwPageSize);
#else
  const long ps = ::sysconf(_SC_PAGESIZE);
  return ps > 0 ? static_cast<size_t>(ps) : 4096u;
#endif
}

EC_INLINE void *AlignedAlloc(size_t alignment, size_t size) {
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  void *p = nullptr;
  if (posix_memalign(&p, alignment, size) != 0) return nullptr;
  return p;
#endif
}

EC_INLINE void AlignedFree(void *p) {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

// =====================
// Dynamic library
// =====================

struct DynLib {
#if defined(_WIN32)
  HMODULE handle = nullptr;

  bool open_utf8(const std::string &utf8_path) {
    std::wstring w = detail::Utf8ToW(utf8_path);
    handle = ::LoadLibraryW(w.c_str());
    return handle != nullptr;
  }
  bool open_w(const std::wstring &wpath) {
    handle = ::LoadLibraryW(wpath.c_str());
    return handle != nullptr;
  }
  void *symbol(const char *name) { return handle ? reinterpret_cast<void *>(::GetProcAddress(handle, name)) : nullptr; }
  void close() {
    if (handle) {
      ::FreeLibrary(handle);
      handle = nullptr;
    }
  }
#else
  void *handle = nullptr;

  bool open(const std::string &path) {
    handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    return handle != nullptr;
  }

  void *symbol(const char *name) const { return handle ? ::dlsym(handle, name) : nullptr; }

  void close() {
    if (handle) {
      ::dlclose(handle);
      handle = nullptr;
    }
  }
#endif
};

// =====================
// Errors
// =====================

EC_INLINE std::string LastErrorString() {
#if defined(_WIN32)
  DWORD err = GetLastError();
  if (!err) return {};
  LPWSTR msg = nullptr;
  DWORD len =
      FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
  std::string out;
  if (len && msg) {
    out = detail::WToUtf8(std::wstring_view(msg, len));
  }
  if (msg) LocalFree(msg);
  return out;
#else
  const int e = errno;
  char buf[256];
#if ((_POSIX_C_SOURCE >= 200112L) && !defined(_GNU_SOURCE)) || defined(__APPLE__)
  if (strerror_r(e, buf, sizeof(buf)) == 0) return {buf};
  return {};
#else
  return std::string(strerror_r(e, buf, sizeof(buf)));
#endif
#endif
}

// =====================
// Process info (lightweight extras)
// =====================

EC_INLINE uint32_t ProcessId() {
#if defined(_WIN32)
  return static_cast<uint32_t>(::GetCurrentProcessId());
#else
  return static_cast<uint32_t>(::getpid());
#endif
}

// Current working directory (safe with error_code)
EC_INLINE std::filesystem::path CurrentDir() {
  std::error_code ec;
  auto p = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path{} : p;
}
}  // namespace autoalg

#endif  // EASY_CONTROL_INCLUDE_COMMON_HPP
