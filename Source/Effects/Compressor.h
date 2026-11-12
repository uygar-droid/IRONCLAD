#pragma once

// Feed-forward compressor (mono). Peak level in dB -> gain computer (threshold, ratio,
// 6 dB soft knee) -> attack/release smoothing of the gain reduction -> makeup -> parallel mix.

#include <algorithm>
#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/Biquad.h"

namespace ironclad
{

class Compressor
{
public:
  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    setAttack(mAttackMs);
    setRelease(mReleaseMs);
    mMakeup.reset(sampleRate, 30.0, mMakeup.target() == 0.0 ? 1.0 : mMakeup.target());
    mMix.reset(sampleRate, 30.0, mMix.target());
    reset();
  }
  void reset() noexcept { mReductionDb = 0.0; }

  void setThresholdDb(double v) noexcept { mThresholdDb = v; }
  void setRatio(double v) noexcept { mRatio = std::max(1.0, v); }
  void setAttack(double ms) noexcept { mAttackMs = ms; mAttackCoef = onePoleCoef(mSr, ms); }
  void setRelease(double ms) noexcept { mReleaseMs = ms; mReleaseCoef = onePoleCoef(mSr, ms); }
  void setMakeupDb(double db) noexcept { mMakeup.setTarget(std::pow(10.0, db / 20.0)); }
  void setMix(double fraction) noexcept { mMix.setTarget(std::clamp(fraction, 0.0, 1.0)); }

  /// Static gain reduction (dB, <= 0) for an input level in dB.
  double computeGainDb(double levelDb) const noexcept
  {
    const double over = levelDb - mThresholdDb;
    const double slope = 1.0 / mRatio - 1.0;
    if (over <= -kKnee * 0.5)
      return 0.0;
    if (over >= kKnee * 0.5)
      return slope * over;
    const double t = over + kKnee * 0.5;
    return slope * t * t / (2.0 * kKnee);
  }

  void process(double* x, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const double in = x[i];
      const double levelDb = 20.0 * std::log10(std::abs(in) + 1e-12);
      const double target = computeGainDb(levelDb); // <= 0
      const double c = target < mReductionDb ? mAttackCoef : mReleaseCoef;
      mReductionDb = target + c * (mReductionDb - target);
      const double wet = in * std::pow(10.0, mReductionDb / 20.0) * mMakeup.next();
      const double m = mMix.next();
      x[i] = in + m * (wet - in);
    }
  }

  double currentReductionDb() const noexcept { return mReductionDb; }

private:
  static constexpr double kKnee = 6.0;
  double mSr = 48000.0;
  double mThresholdDb = -20.0, mRatio = 4.0, mAttackMs = 5.0, mReleaseMs = 120.0;
  double mAttackCoef = 0.0, mReleaseCoef = 0.0;
  double mReductionDb = 0.0;
  SmoothedValue mMakeup, mMix;
};

} // namespace ironclad
