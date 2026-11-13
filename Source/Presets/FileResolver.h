#pragma once

// Finds a preset's capture / IR file again: at its saved path, next to the preset, or by
// searching library folders for the same file name or the same content (SHA-256).
// Never throws; a file that cannot be found yields NotFound (the preset still loads).

#include <string>
#include <vector>

#include "Presets/PresetData.h"

namespace ironclad
{

struct ResolvedFile
{
  enum class How
  {
    SavedPath,
    RelativeToPreset,
    SameName,
    SameContent,
    NotFound
  };
  std::string path;
  How how = How::NotFound;
};

struct ResolveOptions
{
  std::string presetDir;                 // folder of the preset file ("" for session state)
  std::vector<std::string> searchRoots;  // folders searched recursively
  std::vector<std::string> extensions;   // lower-case, with dot: {".nam"} or {".wav"}
  int maxFilesScanned = 20000;           // keeps a search over a huge folder bounded
};

ResolvedFile resolveFile(const FileRef& ref, const ResolveOptions& opt);

/// Builds a FileRef for a file being saved into a preset (hash, size, name, relative path).
FileRef makeFileRef(const std::string& utf8Path, const std::string& presetDir);

} // namespace ironclad
