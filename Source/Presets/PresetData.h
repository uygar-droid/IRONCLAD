#pragma once

// Everything a preset (and the DAW session) stores, independent of file format.
//
// Parameters are keyed by their STABLE string IDs (Parameters.h), never by index, so presets
// survive parameter-table growth. A parameter missing from a preset takes its default value:
// loading a preset always defines the complete sound.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Parameters/Parameters.h"

namespace ironclad
{

/// Reference to an external file (capture or IR). Several clues are kept so the file can be
/// found again on another machine or after it has been moved.
struct FileRef
{
  std::string path;     // absolute UTF-8 path when saved
  std::string relative; // relative to the preset file's folder (preset files only)
  std::string fileName; // e.g. "Plexi.nam"
  std::string sha256;   // content hash ("" if unknown)
  std::uint64_t size = 0;
  bool empty() const { return path.empty() && fileName.empty(); }
};

struct PresetData
{
  static constexpr int kSchema = 1; // preset JSON schema version written by this build

  std::string name;
  std::string category;
  std::map<std::string, double> params; // stable id -> plain value
  std::optional<FileRef> capture;
  std::optional<FileRef> ir[2];
  /// Factory templates: keep whatever capture / IRs are loaded (not stored in files).
  bool templateOnly = false;

  /// Value for a parameter index: the stored value, else the default, clamped to range.
  double valueFor(int paramIndex) const;
  /// Sets every parameter from an index-ordered array.
  void setFromArray(const double* values, int count);
};

/// Serialise to / parse from the preset JSON text. Parse throws std::runtime_error with a
/// user-facing message on invalid or unsupported files.
std::string presetToJson(const PresetData& p);
PresetData presetFromJson(const std::string& text);

} // namespace ironclad
