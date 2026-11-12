#pragma once

// 3-band EQ (stereo, after the cabinet): Bass = low shelf 120 Hz, Mid = peak (200 Hz..5 kHz,
// Q 0.8), Treble = high shelf 3.5 kHz, plus output level. Gains/frequency are smoothed and
// the coefficients recomputed every 16 samples while they move.

#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/Biquad.h"

namespace ironclad
{

class Equalizer
{
public:
  static constexpr double kLowHz = 120.0;
  static constexpr double kHighHz = 3500.0;
  static constexpr double kMidQ = 0.8;

  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    mLow.reset(sampleRate, 30.0, mLow.target());
    mMid.reset(sampleRate, 30.0, mMid.target());
    mHigh.reset(sampleRate, 30.0, mHigh.target());
    mMidHzLog.reset(sampleRate, 30.0, mMidHzLog.target() == 0.0 ? std::log2(800.0) : mMidHzLog.target());
    mLevel.reset(sampleRate, 30.0, mLevel.target() == 0.0 ? 1.0 : mLevel.target());
    updateCoefficients();
    reset();
  }
  void reset() noexcept
  {
    for (auto* b : {&mLs[0], &mLs[1], &mPk[0], &mPk[1], &mHs[0], &mHs[1]})
      b->reset();
  }
  void setLowDb(double v) noexcept { mLow.setTarget(v); }
  void setMidDb(double v) noexcept { mMid.setTarget(v); }
  void setMidHz(double v) noexcept { mMidHzLog.setTarget(std::log2(v)); }
  void setHighDb(double v) noexcept { mHigh.setTarget(v); }
  void setLevelDb(double v) noexcept { mLevel.setTarget(std::pow(10.0, v / 20.0)); }

  void process(double* l, double* r, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const bool moving = mLow.isSmoothing() || mMid.isSmoothing() || mHigh.isSmoothing() || mMidHzLog.isSmoothing();
      mLow.next();
      mMid.next();
      mHigh.next();
      mMidHzLog.next();
      if (moving && (++mCounter & 15) == 0)
        updateCoefficients();
      else if (!moving && mDirty)
        updateCoefficients();
      const double g = mLevel.next();
      l[i] = mHs[0].process(mPk[0].process(mLs[0].process(l[i]))) * g;
      r[i] = mHs[1].process(mPk[1].process(mLs[1].process(r[i]))) * g;
      mDirty = moving;
    }
  }

private:
  void updateCoefficients() noexcept
  {
    const double midHz = std::exp2(mMidHzLog.current());
    for (int c = 0; c < 2; ++c)
    {
      mLs[c].setup(Biquad::Type::LowShelf, mSr, kLowHz, 0.7071, mLow.current());
      mPk[c].setup(Biquad::Type::Peak, mSr, midHz, kMidQ, mMid.current());
      mHs[c].setup(Biquad::Type::HighShelf, mSr, kHighHz, 0.7071, mHigh.current());
    }
  }

  double mSr = 48000.0;
  SmoothedValue mLow, mMid, mHigh, mMidHzLog, mLevel;
  Biquad mLs[2], mPk[2], mHs[2];
  int mCounter = 0;
  bool mDirty = true;
};

} // namespace ironclad
