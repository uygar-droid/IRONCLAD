#pragma once

// Audio-thread side of the cabinet: two IR slots (A, B), mono in -> stereo out.
//
// Prepared IRs arrive through readyQueue() from IrLoader. A slot switches IRs with a
// 25 ms equal-power crossfade; replaced IRs go back through retireQueue() and are
// destroyed by the loader thread. process() never allocates, frees, locks or waits.
//
// Mix per slot : IR -> level -> polarity -> balance(pan) -> slot gain (blend & on/off)
// Blend        : linear between A and B when both are active (keeps the level of similar IRs);
//                a single active slot plays at full level.
// Output       : HPF -> LPF on the cabinet sum, mixed against the dry signal when the
//                cabinet is off or no IR is loaded (both smoothed).

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Audio/SmoothedValue.h"
#include "Dsp/Biquad.h"
#include "Cabinet/ImpulseResponse.h"
#include "Utilities/SpscQueue.h"

namespace ironclad
{

struct PreparedIr
{
  std::unique_ptr<ImpulseResponse> ir; // nullptr = slot cleared
  int slot = 0;
  std::uint64_t generation = 0;
};

using IrQueue = SpscQueue<PreparedIr*, 32>;

struct CabTargets
{
  bool enabled = true;
  bool slotEnabled[2] = {true, true};
  double levelDb[2] = {0.0, 0.0};
  double pan[2] = {0.0, 0.0};   // -1 .. +1
  bool invert[2] = {false, false};
  double blend = 0.5;           // 0 = A only, 1 = B only
  double highPassHz = 20.0;   // <= kHpfOffHz: high-pass bypassed
  double lowPassHz = 20000.0; // >= kLpfOffHz: low-pass bypassed
};

inline constexpr double kHpfOffHz = 20.5;
inline constexpr double kLpfOffHz = 19990.0;

class CabStage
{
public:
  static constexpr int kSlots = 2;
  static constexpr double kCrossfadeMs = 25.0;

  CabStage() = default;
  ~CabStage(); // the loader thread must already be stopped
  CabStage(const CabStage&) = delete;
  CabStage& operator=(const CabStage&) = delete;

  /// Non-real-time, audio stopped, IrLoader lock held (Engine::prepare does this).
  void prepare(double sampleRate, int maxBlockSize, std::uint64_t generation);

  void setTargets(const CabTargets& t) noexcept { mTargets = t; }
  void process(const double* mono, double* outLeft, double* outRight, int numFrames) noexcept;

  IrQueue& readyQueue() noexcept { return mReady; }
  IrQueue& retireQueue() noexcept { return mRetire; }
  std::uint64_t activationCount() const noexcept { return mActivations.load(std::memory_order_relaxed); }
  bool isCrossfading() const noexcept { return mSlots[0].fadeActive || mSlots[1].fadeActive; }

private:
  struct Slot
  {
    PreparedIr* live = nullptr;
    PreparedIr* fading = nullptr;
    PreparedIr* held = nullptr; // arrived while a crossfade was running
    bool fadeActive = false;
    int fadePos = 0;
    SmoothedValue gain;     // level * blend/enable (linear)
    SmoothedValue polarity; // +1 / -1
    SmoothedValue balL, balR;
  };

  void retire(PreparedIr* p) noexcept;
  void flushRetired() noexcept;
  void acceptIncoming() noexcept;
  static bool hasIr(const PreparedIr* p) noexcept { return p != nullptr && p->ir != nullptr; }
  void renderSlot(int index, const double* mono, int n) noexcept;

  IrQueue mReady, mRetire;
  Slot mSlots[kSlots];
  PreparedIr* mPendingRetire[16] = {};
  int mNumPendingRetire = 0;
  std::uint64_t mGeneration = 0;
  bool mPrepared = false;
  double mSampleRate = 48000.0;
  int mFadeLen = 1;

  CabTargets mTargets;
  SmoothedValue mWet;              // 0 = dry (cab off / nothing loaded), 1 = cabinet
  SmoothedValue mHpfLog2, mLpfLog2; // log2(Hz)
  SmoothedValue mHpfAmount, mLpfAmount; // 0 = filter bypassed (at its extreme setting)
  Biquad mHpf[2], mLpf[2];

  std::vector<double> mDry, mAL, mAR, mBL, mBR, mSumL, mSumR;
  std::atomic<std::uint64_t> mActivations{0};
};

} // namespace ironclad
