#pragma once

// Noise gate (mono). Peak detector -> threshold with 6 dB hysteresis and 15 ms hold ->
// gain ramps: 1 ms to open; to close, the gain falls by 60 dB over the Release time.
// Closed gain is -90 dB (effectively silent).

#include <cmath>

#include "Dsp/Biquad.h"

namespace ironclad
{

class NoiseGate
{
public:
  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    mDetRelease = onePoleCoef(sampleRate, 30.0);
    mOpenCoef = onePoleCoef(sampleRate, 1.0);
    mHoldSamples = static_cast<int>(sampleRate * 0.015);
    setRelease(mReleaseMs);
    reset();
  }
  void reset() noexcept
  {
    mEnv = 0.0;
    mGain = 0.0;
    mHold = 0;
    mOpen = false;
  }
  void setThresholdDb(double db) noexcept
  {
    mOpenThr = std::pow(10.0, db / 20.0);
    mCloseThr = mOpenThr * 0.5; // 6 dB hysteresis
  }
  void setRelease(double ms) noexcept
  {
    mReleaseMs = ms;
    mCloseCoef = onePoleCoef(mSr, ms / 6.908); // time constant such that -60 dB after `ms`
  }

  void process(double* x, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const double a = std::abs(x[i]);
      mEnv = a > mEnv ? a : a + mDetRelease * (mEnv - a);
      if (mEnv >= mOpenThr)
      {
        mOpen = true;
        mHold = mHoldSamples;
      }
      else if (mEnv < mCloseThr)
      {
        if (mHold > 0)
          --mHold;
        else
          mOpen = false;
      }
      const double target = mOpen ? 1.0 : kFloor;
      const double c = target > mGain ? mOpenCoef : mCloseCoef;
      mGain = target + c * (mGain - target);
      x[i] *= mGain;
    }
  }

  double currentGain() const noexcept { return mGain; }

private:
  static constexpr double kFloor = 3.16e-5; // -90 dB
  double mSr = 48000.0;
  double mOpenThr = 0.001, mCloseThr = 0.0005;
  double mReleaseMs = 80.0;
  double mDetRelease = 0.0, mOpenCoef = 0.0, mCloseCoef = 0.0;
  double mEnv = 0.0, mGain = 0.0;
  int mHold = 0, mHoldSamples = 0;
  bool mOpen = false;
};

} // namespace ironclad
