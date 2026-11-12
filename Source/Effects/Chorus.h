#pragma once

// Stereo chorus: one modulated delay per channel (base 7 ms, up to +6 ms sweep), sine LFOs
// 90 degrees apart for width, cubic interpolation, wet/dry mix (50 % = classic chorus).

#include <algorithm>
#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/DelayLine.h"

namespace ironclad
{

class Chorus
{
public:
  static constexpr double kBaseMs = 7.0;
  static constexpr double kMaxSweepMs = 6.0;

  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    const int maxDelay = static_cast<int>(sampleRate * (kBaseMs + kMaxSweepMs) * 0.001) + 8;
    mLine[0].prepare(maxDelay);
    mLine[1].prepare(maxDelay);
    mRate.reset(sampleRate, 50.0, mRate.target() == 0.0 ? 0.8 : mRate.target());
    mDepth.reset(sampleRate, 50.0, mDepth.target());
    mMix.reset(sampleRate, 30.0, mMix.target());
    reset();
  }
  void reset() noexcept
  {
    mLine[0].reset();
    mLine[1].reset();
    mPhase = 0.0;
  }
  void setRateHz(double v) noexcept { mRate.setTarget(v); }
  void setDepth(double fraction) noexcept { mDepth.setTarget(std::clamp(fraction, 0.0, 1.0)); }
  void setMix(double fraction) noexcept { mMix.setTarget(std::clamp(fraction, 0.0, 1.0)); }

  void process(double* l, double* r, int n) noexcept
  {
    constexpr double twoPi = 6.283185307179586476925286766559;
    const double msToSamples = mSr * 0.001;
    for (int i = 0; i < n; ++i)
    {
      mPhase += mRate.next() / mSr;
      mPhase -= std::floor(mPhase);
      const double depth = mDepth.next() * kMaxSweepMs * msToSamples;
      const double base = kBaseMs * msToSamples;
      const double dL = base + depth * 0.5 * (1.0 + std::sin(twoPi * mPhase));
      const double dR = base + depth * 0.5 * (1.0 + std::sin(twoPi * (mPhase + 0.25)));
      mLine[0].push(l[i]);
      mLine[1].push(r[i]);
      const double wL = mLine[0].read(dL);
      const double wR = mLine[1].read(dR);
      const double m = mMix.next();
      l[i] += m * (wL - l[i]);
      r[i] += m * (wR - r[i]);
    }
  }

private:
  double mSr = 48000.0;
  DelayLine mLine[2];
  SmoothedValue mRate, mDepth, mMix;
  double mPhase = 0.0;
};

} // namespace ironclad
