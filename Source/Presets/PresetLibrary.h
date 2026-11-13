#pragma once

// The preset browser's model (UI thread only).
//
// Folder layout (root = the IRONCLAD user-data folder, see Utilities/Paths.h):
//   <root>/Default.ironclad          the sound a new IRONCLAD instance starts with
//   <root>/IRONCLAD/<Category>/...   factory presets (read-only; written from the built-in set)
//   <root>/Packs/<Pack>/[<Sub>/]...  preset packs (read-only)
//   <root>/User/[<Category>/]...     the user's presets
//   <root>/.favorites.json           favourites (hidden)
// Every operation reports failures with a std::runtime_error carrying a user-facing message;
// nothing here may crash the host.

#include <string>
#include <vector>

#include "Presets/PresetData.h"

namespace ironclad
{

struct PresetEntry
{
  enum class Source
  {
    Factory,
    Pack,
    User
  };
  std::string name;
  std::string category; // factory / user: category folder; packs: "<Pack>" or "<Pack> / <Sub>"
  std::string path;     // preset file (UTF-8); empty for a built-in factory preset
  Source source = Source::User;
  int builtinIndex = -1; // index into factoryPresets() when there is no file
  bool favorite = false;
  bool isFactory() const noexcept { return source == Source::Factory; }
  bool readOnly() const noexcept { return source != Source::User; }
};

class PresetLibrary
{
public:
  static constexpr const char* kExtension = ".ironclad";
  static constexpr const char* kFactoryFolder = "IRONCLAD";
  static constexpr const char* kPacksFolder = "Packs";
  static constexpr const char* kUserFolder = "User";
  static constexpr const char* kDefaultFile = "Default.ironclad";

  explicit PresetLibrary(std::string rootDir);

  /// Creates the folders, writes the built-in factory presets (when missing or out of date) and
  /// Default.ironclad (when missing), and moves presets saved by 0.5.0 (<root>/Presets) into
  /// User. Returns false if the folder is not writable; the library then still works with the
  /// built-in factory presets.
  bool ensureLayout();

  const std::string& rootDir() const noexcept { return mRoot; }
  std::string userDir() const;
  std::string defaultPresetPath() const;

  void rescan();
  const std::vector<PresetEntry>& entries() const noexcept { return mEntries; }
  int indexOfPath(const std::string& path) const;
  int indexOfFactory(const std::string& name) const;

  PresetData load(int index) const;
  static PresetData loadFile(const std::string& path);

  /// Writes `p` as <root>/User/<category>/<name>.ironclad. Fails if the file exists and
  /// `overwrite` is false. Returns the path. File refs are re-made relative to the file.
  std::string save(PresetData p, bool overwrite);
  std::string rename(int index, const std::string& newName);
  std::string duplicate(int index); // into User (also for factory / pack presets)
  void remove(int index);
  void setFavorite(int index, bool favorite);

  /// The first-open sound. loadDefault() falls back to the built-in "Init" template.
  PresetData loadDefault() const;
  void saveDefault(PresetData p);
  void resetDefault();

  /// Removes characters that are invalid in file names on Windows or macOS.
  static std::string sanitizeName(const std::string& name);
  std::string pathFor(const std::string& category, const std::string& name) const;

private:
  std::string favoriteKey(const PresetEntry& e) const;
  void loadFavorites();
  void saveFavorites() const;
  void scanFolder(const std::string& dir, PresetEntry::Source source, int maxDepth, std::vector<PresetEntry>& out) const;

  std::string mRoot;
  std::vector<PresetEntry> mEntries;
  std::vector<std::string> mFavorites;
};

/// Writes text to a file atomically (temp file + rename). Throws std::runtime_error.
void writeTextFileAtomic(const std::string& utf8Path, const std::string& text);
std::string readTextFile(const std::string& utf8Path); // throws std::runtime_error

} // namespace ironclad
