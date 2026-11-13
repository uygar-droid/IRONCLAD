#pragma once

// UTF-8 <-> std::filesystem::path helpers. IRONCLAD stores every path as UTF-8 so that
// non-ASCII folder names (e.g. "Müzik", "Masaüstü") work on Windows as well as macOS.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace ironclad
{

inline std::filesystem::path pathFromUtf8(const std::string& utf8)
{
#if defined(__cpp_char8_t)
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
  return std::filesystem::u8path(utf8);
#endif
}

inline std::string utf8FromPath(const std::filesystem::path& p)
{
  const auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

/// True if `dir` exists (or can be created) and a file can be written in it.
inline bool isWritableDir(const std::filesystem::path& dir)
{
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (!std::filesystem::is_directory(dir, ec))
    return false;
  const auto probe = dir / ".ironclad-write-test";
  {
    std::FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(probe.c_str(), L"wb");
#else
    f = std::fopen(probe.c_str(), "wb");
#endif
    if (f == nullptr)
      return false;
    std::fclose(f);
  }
  std::filesystem::remove(probe, ec);
  return true;
}

/// The folder IRONCLAD used before 0.5.2 (and still uses when the shared one is not writable):
/// macOS "~/Library/Application Support/IRONCLAD", Windows "%APPDATA%\IRONCLAD",
/// elsewhere "~/.local/share/IRONCLAD" (tests only). Empty if it cannot be determined.
inline std::string perUserDataDir()
{
#if defined(_WIN32)
  const wchar_t* appData = _wgetenv(L"APPDATA");
  if (appData == nullptr || *appData == 0)
    return {};
  return utf8FromPath(std::filesystem::path(appData) / "IRONCLAD");
#else
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == 0)
    return {};
#if defined(__APPLE__)
  return utf8FromPath(pathFromUtf8(home) / "Library" / "Application Support" / "IRONCLAD");
#else
  return utf8FromPath(pathFromUtf8(home) / ".local" / "share" / "IRONCLAD");
#endif
#endif
}

/// The shared preset folder, next to other amp plugins' presets:
/// macOS "/Library/Audio/Presets/IRONCLAD". Creating it needs administrator rights once
/// (the installer does it; see INSTALL.md). Empty on other systems.
inline std::string sharedDataDir()
{
#if defined(__APPLE__)
  return "/Library/Audio/Presets/IRONCLAD";
#else
  return {};
#endif
}

/// The IRONCLAD folder in use: the shared one when it is writable, else the per-user one.
/// Decided once per process.
inline std::string userDataDir()
{
  static const std::string dir = [] {
    const std::string shared = sharedDataDir();
    if (!shared.empty() && isWritableDir(pathFromUtf8(shared)))
      return shared;
    return perUserDataDir();
  }();
  return dir;
}

} // namespace ironclad
