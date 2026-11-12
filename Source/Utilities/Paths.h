#pragma once

// UTF-8 <-> std::filesystem::path helpers. IRONCLAD stores every path as UTF-8 so that
// non-ASCII folder names (e.g. "Müzik", "Masaüstü") work on Windows as well as macOS.

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

} // namespace ironclad
