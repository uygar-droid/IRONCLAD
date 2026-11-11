#pragma once

// Metadata describing a loaded NAM capture. Every field is optional in real
// .nam files; empty strings / std::nullopt mean "not provided", never invented.

#include <optional>
#include <string>

namespace ironclad
{

struct CaptureInfo
{
  std::string path;      // UTF-8 absolute path as loaded
  std::string fileName;  // e.g. "Plexi Crunch.nam"
  std::string displayName; // metadata name, else file name without extension

  std::string architecture; // WaveNet, LSTM, ConvNet, Linear, ...
  std::string fileVersion;  // .nam format version
  double sampleRate = 0.0;  // capture's training rate (0 = unknown, treated as 48 kHz)

  std::string modeledBy;
  std::string gearMake;
  std::string gearModel;
  std::string gearType;
  std::string toneType;
  std::string date;

  std::optional<double> loudnessDb;
  std::optional<double> inputLevelDbu;
  std::optional<double> outputLevelDbu;
};

} // namespace ironclad
