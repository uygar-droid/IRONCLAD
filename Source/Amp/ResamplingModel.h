#pragma once

// Sample-rate adapter around a nam::DSP.
//
// Ported from NeuralAmpModelerPlugin/NeuralAmpModeler/ResamplingNAM.h
// (MIT License, Copyright (c) 2022 Steven Atkinson) and modified for IRONCLAD:
//   * no iPlug2 dependency;
//   * no exceptions on the processing path (block size is guaranteed by the engine);
//   * the processing callback is bound once, so per-block calls do not allocate.
//
// Threading: construct and Reset() off the audio thread; process() on the audio thread.

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

// AudioDSPTools' resampler expects two iPlug constants. IRONCLAD's engine does not use
// iPlug2, so they are provided here. This header must stay private to engine .cpp files
// (never included from a translation unit that also includes iPlug2 headers).
namespace dsp
{
inline constexpr int DEFAULT_BLOCK_SIZE = 512;
}
namespace iplug
{
inline constexpr double PI = 3.1415926535897932384626433832795;
}
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wsign-compare"
  #pragma GCC diagnostic ignored "-Wpedantic"
  #if !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wchanges-meaning"
  #endif
#endif
#include "dsp/ResamplingContainer/ResamplingContainer.h"
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include "NAM/dsp.h"

namespace ironclad
{

/// Some early captures do not state their sample rate; like the NAM plugin, assume 48 kHz.
inline double captureSampleRate(const nam::DSP& model)
{
  const double reported = const_cast<nam::DSP&>(model).GetExpectedSampleRate();
  return reported <= 0.0 ? 48000.0 : reported;
}

class ResamplingModel
{
public:
  explicit ResamplingModel(std::unique_ptr<nam::DSP> model)
  : mModel(std::move(model))
  , mArbitraryRate(mModel->SupportsArbitrarySampleRate())
  , mModelRate(captureSampleRate(*mModel))
  , mResampler(mModelRate)
  {
    nam::DSP* raw = mModel.get();
    mProcess = [raw](NAM_SAMPLE** in, NAM_SAMPLE** out, int n) { raw->process(in, out, n); };
  }

  ResamplingModel(const ResamplingModel&) = delete;
  ResamplingModel& operator=(const ResamplingModel&) = delete;

  /// Not real-time safe (allocates). Prewarms the model (NAM default).
  void reset(double hostRate, int maxBlockSize)
  {
    mHostRate = hostRate;
    mMaxBlock = maxBlockSize;
    const double modelRate = mArbitraryRate ? hostRate : mModelRate;
    int maxInner = maxBlockSize;
    if (needsResampling())
    {
      mResampler.Reset(hostRate, maxBlockSize);
      maxInner = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) * modelRate / hostRate)) + 1;
    }
    mModel->Reset(modelRate, maxInner);
  }

  /// Real-time safe. numFrames <= maxBlockSize given to reset().
  void process(NAM_SAMPLE* input, NAM_SAMPLE* output, int numFrames) noexcept
  {
    NAM_SAMPLE* in[1] = {input};
    NAM_SAMPLE* out[1] = {output};
    if (!needsResampling())
      mModel->process(in, out, numFrames);
    else
      mResampler.ProcessBlock(in, out, numFrames, mProcess);
  }

  int latencySamples() const { return needsResampling() ? mResampler.GetLatency() : 0; }
  bool needsResampling() const { return !mArbitraryRate && mHostRate != mModelRate; }
  double modelSampleRate() const { return mModelRate; }
  nam::DSP& model() { return *mModel; }
  const nam::DSP& model() const { return *mModel; }

private:
  std::unique_ptr<nam::DSP> mModel;
  bool mArbitraryRate;
  double mModelRate;
  double mHostRate = 0.0;
  int mMaxBlock = 0;
  dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;
  std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> mProcess;
};

} // namespace ironclad
