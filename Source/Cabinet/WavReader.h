#pragma once

// Minimal, defensive WAV (RIFF/WAVE) reader for impulse responses.
// Supports PCM 16/24/32-bit integer, IEEE float 32/64-bit, WAVE_FORMAT_EXTENSIBLE,
// any channel count (callers use the first two), arbitrary chunk order and padding.
// Not real-time: allocates and performs file I/O.

#include <stdexcept>
#include <string>
#include <vector>

namespace ironclad
{

class AudioFileError : public std::runtime_error
{
public:
  AudioFileError(std::string userMessage, std::string technicalDetail)
  : std::runtime_error(std::move(userMessage))
  , mDetail(std::move(technicalDetail))
  {
  }
  const std::string& technicalDetail() const noexcept { return mDetail; }

private:
  std::string mDetail;
};

struct WavData
{
  double sampleRate = 0.0;
  int bitsPerSample = 0;
  bool isFloat = false;
  std::vector<std::vector<double>> channels; // [channel][frame], normalised to [-1, 1]
  int numChannels() const { return static_cast<int>(channels.size()); }
  std::size_t numFrames() const { return channels.empty() ? 0 : channels[0].size(); }
};

/// Parses a WAV file held in memory. Throws AudioFileError.
WavData parseWav(const std::vector<unsigned char>& bytes, const std::string& displayName);

/// Reads and parses a WAV file (UTF-8 path). Throws AudioFileError.
WavData readWavFile(const std::string& utf8Path);

} // namespace ironclad
