#pragma once

// UTF-8 <-> std::filesystem::path helpers. IRONCLAD stores every path as UTF-8 so that
// non-ASCII folder names (e.g. "Müzik", "Masaüstü") work on Windows as well as macOS.

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

/// Per-user data folder: macOS "~/Library/Application Support/IRONCLAD",
/// Windows "%APPDATA%\\IRONCLAD" (other systems: "~/.local/share/IRONCLAD", tests only).
/// Empty if it cannot be determined.
inline std::string userDataDir()
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

} // namespace ironclad
