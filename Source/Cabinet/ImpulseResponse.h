#pragma once

// A loaded cabinet impulse response.
//   load()    : loader thread — reads the WAV, keeps the original samples.
//   prepare() : non-real-time — resamples to the host rate, normalises, builds convolvers.
//   process() : audio thread — mono in, stereo out, allocation-free.

#include <memory>
#include <string>
#include <vector>

#include "Cabinet/Convolver.h"

namespace ironclad
{

struct IrInfo
{
  std::string path;
  std::string fileName;
  std::string displayName;
  double fileSampleRate = 0.0;
  int fileChannels = 0;      // channels in the file (IRONCLAD uses the first two)
  std::size_t fileFrames = 0;
  double durationMs = 0.0;   // after truncation
  bool truncated = false;    // longer than kMaxSeconds
  std::vector<float> thumbnail; // kThumbnailPoints peak envelope, 0..1 (for the UI)
};

class ImpulseResponse
{
public:
  static constexpr double kMaxSeconds = 1.0;
  static constexpr int kThumbnailPoints = 200;

  static std::unique_ptr<ImpulseResponse> load(const std::string& utf8Path);

  void prepare(double hostSampleRate);
  void reset() noexcept;
  void process(const double* input, double* outLeft, double* outRight, int numFrames) noexcept;

  bool isStereo() const noexcept { return mStereo; }
  const IrInfo& info() const noexcept { return mInfo; }
  std::size_t preparedLength() const noexcept { return mLeft.irLength(); }
  /// Normalisation gain applied in prepare() (unit-energy IR).
  double normalisationGain() const noexcept { return mNormGain; }

private:
  ImpulseResponse() = default;

  IrInfo mInfo;
  std::vector<std::vector<double>> mOriginal; // 1 or 2 channels at mInfo.fileSampleRate
  bool mStereo = false;
  double mNormGain = 1.0;
  Convolver mLeft, mRight;
};

} // namespace ironclad
