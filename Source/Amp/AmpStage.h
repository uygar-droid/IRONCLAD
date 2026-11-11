#pragma once

// Audio-thread side of the amplifier slot.
//
// Prepared captures arrive through `readyQueue()` (filled by CaptureLoader on its
// own thread). When one arrives, AmpStage crossfades from the current capture to
// the new one (equal power, kCrossfadeMs) and afterwards hands the old capture to
// `retireQueue()`, where the loader thread destroys it. The audio thread therefore
// never allocates, frees, locks or waits.

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Amp/AmpModel.h"
#include "Audio/SmoothedValue.h"
#include "Utilities/SpscQueue.h"

namespace ironclad
{

/// A capture ready for the audio thread. `model == nullptr` means "no capture" (dry).
struct PreparedAmp
{
  std::unique_ptr<AmpModel> model;
  std::uint64_t generation = 0; // must match AmpStage's current generation to be used
};

using AmpQueue = SpscQueue<PreparedAmp*, 32>;

class AmpStage
{
public:
  static constexpr double kCrossfadeMs = 25.0;

  AmpStage() = default;
  ~AmpStage(); // the loader thread must already be stopped
  AmpStage(const AmpStage&) = delete;
  AmpStage& operator=(const AmpStage&) = delete;

  /// Non-real-time, audio stopped. Must be called with the CaptureLoader lock held
  /// (Engine::prepare does this) so no prepared capture is pushed concurrently.
  void prepare(double sampleRate, int maxBlockSize, std::uint64_t generation);

  /// Mono, in place. numFrames <= maxBlockSize.
  void process(double* io, int numFrames) noexcept;

  void setTargets(double inputDb, double outputDb, bool enabled, bool normalize) noexcept;

  AmpQueue& readyQueue() noexcept { return mReady; }
  AmpQueue& retireQueue() noexcept { return mRetire; }

  /// Latency of the active capture (resampling), readable from any thread.
  int latencySamples() const noexcept { return mLatency.load(std::memory_order_relaxed); }
  /// True while a capture switch crossfade is running (test/diagnostic use).
  bool isCrossfading() const noexcept { return mFadeActive; }
  /// Number of captures the audio thread has activated since construction.
  std::uint64_t activationCount() const noexcept { return mActivations.load(std::memory_order_relaxed); }

private:
  void retire(PreparedAmp* amp) noexcept;
  void renderSlot(PreparedAmp* slot, const double* dry, const double* driven, double* scratchIn, double* out,
                  int n) noexcept;

  AmpQueue mReady;
  AmpQueue mRetire;

  PreparedAmp* mLive = nullptr;
  PreparedAmp* mFading = nullptr;
  PreparedAmp* mPendingRetire = nullptr;
  bool mFadeActive = false;
  int mFadePos = 0;
  int mFadeLen = 1;
  std::uint64_t mGeneration = 0;
  bool mPrepared = false;

  SmoothedValue mInputGain, mOutputGain, mMix, mNormAmount;
  std::vector<double> mDry, mDriven, mScratch, mOutA, mOutB, mIn, mOut, mMixBuf, mNorm;

  std::atomic<int> mLatency{0};
  std::atomic<std::uint64_t> mActivations{0};
};

} // namespace ironclad
