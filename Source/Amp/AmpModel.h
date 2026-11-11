#pragma once

// One loaded NAM capture, ready for real-time processing.
//
//   load()     : loader thread. Reads/validates the file and builds the network.
//                Throws CaptureLoadError with a user-facing message on failure.
//   prepare()  : non-real-time. Sizes buffers / resampler for the host rate.
//   process()  : audio thread. noexcept, allocation-free.

#include <memory>
#include <stdexcept>
#include <string>

#include "Amp/CaptureInfo.h"

namespace ironclad
{

class ResamplingModel;

class CaptureLoadError : public std::runtime_error
{
public:
  CaptureLoadError(std::string userMessage, std::string technicalDetail)
  : std::runtime_error(userMessage)
  , mDetail(std::move(technicalDetail))
  {
  }
  const std::string& technicalDetail() const noexcept { return mDetail; }

private:
  std::string mDetail;
};

class AmpModel
{
public:
  static std::unique_ptr<AmpModel> load(const std::string& utf8Path);
  ~AmpModel();

  void prepare(double hostSampleRate, int maxBlockSize);
  void process(double* input, double* output, int numFrames) noexcept;

  const CaptureInfo& info() const noexcept { return mInfo; }
  int latencySamples() const noexcept;
  bool isResampling() const noexcept;

  /// Gain (dB) that brings the capture to the -18 dB loudness target; 0 if unknown.
  double normalizationDb() const noexcept { return mNormalizationDb; }

  static constexpr double kTargetLoudnessDb = -18.0;

private:
  AmpModel() = default;

  CaptureInfo mInfo;
  std::unique_ptr<ResamplingModel> mModel;
  double mNormalizationDb = 0.0;
};

} // namespace ironclad
