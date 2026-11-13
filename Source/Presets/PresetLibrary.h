#pragma once

// The preset browser's model (UI thread only).
//
// User presets are "<root>/<Category>/<Name>.ironclad" JSON files; presets directly in
// <root> have no category. Factory presets come first in the list and are read-only.
// Favourites are kept in "<root>/favorites.json". Every operation reports failures with a
// std::runtime_error carrying a user-facing message; nothing here may crash the host.

#include <string>
#include <vector>

#include "Presets/PresetData.h"

namespace ironclad
{

struct PresetEntry
{
  std::string name;
  std::string category;
  std::string path;     // user preset file (UTF-8); empty for factory presets
  int factoryIndex = -1;
  bool favorite = false;
  bool isFactory() const noexcept { return factoryIndex >= 0; }
};

class PresetLibrary
{
public:
  static constexpr const char* kExtension = ".ironclad";

  explicit PresetLibrary(std::string rootDir);

  const std::string& rootDir() const noexcept { return mRoot; }
  void rescan();
  const std::vector<PresetEntry>& entries() const noexcept { return mEntries; }
  int indexOfPath(const std::string& path) const;
  int indexOfFactory(const std::string& name) const;

  PresetData load(int index) const;
  static PresetData loadFile(const std::string& path);

  /// Writes `p` as <root>/<category>/<name>.ironclad. Fails if the file exists and
  /// `overwrite` is false. Returns the path. File refs are re-made relative to the file.
  std::string save(PresetData p, bool overwrite);
  std::string rename(int index, const std::string& newName);
  std::string duplicate(int index);
  void remove(int index);
  void setFavorite(int index, bool favorite);

  /// Removes characters that are invalid in file names on Windows or macOS.
  static std::string sanitizeName(const std::string& name);
  std::string pathFor(const std::string& category, const std::string& name) const;

private:
  std::string favoriteKey(const PresetEntry& e) const;
  void loadFavorites();
  void saveFavorites() const;

  std::string mRoot;
  std::vector<PresetEntry> mEntries;
  std::vector<std::string> mFavorites;
};

/// Writes text to a file atomically (temp file + rename). Throws std::runtime_error.
void writeTextFileAtomic(const std::string& utf8Path, const std::string& text);
std::string readTextFile(const std::string& utf8Path); // throws std::runtime_error

} // namespace ironclad
