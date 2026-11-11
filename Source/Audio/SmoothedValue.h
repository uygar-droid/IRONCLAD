#pragma once

// Linear ramp smoother. When a new target arrives, the value moves to it in a
// straight line over a fixed number of samples (the ramp is restarted from the
// current value if the target changes mid-ramp). Deterministic and allocation-free,
// which makes its behaviour easy to test exactly.

#include <algorithm>
#include <cmath>

namespace ironclad
{

class SmoothedValue
{
public:
  /// Not real-time critical; call from prepare().
  void reset(double sampleRate, double rampTimeMs, double initialValue) noexcept
  {
    mRampSamples = std::max(1, static_cast<int>(std::lround(sampleRate * rampTimeMs * 0.001)));
    mCurrent = mTarget = initialValue;
    mStep = 0.0;
    mRemaining = 0;
  }

  void setTarget(double target) noexcept
  {
    if (target == mTarget)
      return;
    mTarget = target;
    mRemaining = mRampSamples;
    mStep = (mTarget - mCurrent) / static_cast<double>(mRampSamples);
  }

  /// Jump immediately (used on reset/state load, never during playback).
  void setImmediate(double value) noexcept
  {
    mCurrent = mTarget = value;
    mRemaining = 0;
    mStep = 0.0;
  }

  double next() noexcept
  {
    if (mRemaining > 0)
    {
      --mRemaining;
      mCurrent = (mRemaining == 0) ? mTarget : mCurrent + mStep;
    }
    return mCurrent;
  }

  bool isSmoothing() const noexcept { return mRemaining > 0; }
  double current() const noexcept { return mCurrent; }
  double target() const noexcept { return mTarget; }
  int rampSamples() const noexcept { return mRampSamples; }

private:
  double mCurrent = 0.0;
  double mTarget = 0.0;
  double mStep = 0.0;
  int mRampSamples = 1;
  int mRemaining = 0;
};

} // namespace ironclad
