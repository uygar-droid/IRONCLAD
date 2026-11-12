#pragma once

// Stereo delay (after the cabinet).
//   Digital   : clean repeats, feedback filtered by low-cut / high-cut.
//   Analog    : darker repeats (extra 2.8 kHz roll-off), soft saturation in the feedback loop
//               and slight wow (0.35 Hz, ±0.25 ms) for a bucket-brigade character.
//   Ping-Pong : input summed to mono, repeats alternate left / right.
// Time is free (ms) or synced to the host tempo (note divisions). Time changes glide over
// ~120 ms (tape-like) instead of jumping, so moving the knob never clicks.
// Output = dry + mix * wet (the dry signal is never attenuated).

#include <algorithm>
#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/Biquad.h"
#include "Dsp/DelayLine.h"

namespace ironclad
{

class Delay
{
public:
  enum class Type
  {
    Digital = 0,
    Analog = 1,
    PingPong = 2
  };

  static constexpr double kMaxSeconds = 2.5;
  static constexpr int kNumDivisions = 10;

  /// Length in quarter notes of each division ("1/1|1/2|1/2 dot|1/4|1/4 dot|1/4 trip|1/8|1/8 dot|1/8 trip|1/16").
  static double divisionBeats(int index) noexcept
  {
    static constexpr double beats[kNumDivisions] = {4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25};
    return beats[std::clamp(index, 0, kNumDivisions - 1)];
  }
  static double syncedMs(double bpm, int division) noexcept
  {
    if (!(bpm > 1.0))
      bpm = 120.0;
    return 60000.0 / bpm * divisionBeats(division);
  }

  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    const int maxDelay = static_cast<int>(sampleRate * kMaxSeconds) + 8;
    mLine[0].prepare(maxDelay);
    mLine[1].prepare(maxDelay);
    mTime.reset(sampleRate, 120.0, mTime.target() == 0.0 ? sampleRate * 0.4 : mTime.target());
    mFeedback.reset(sampleRate, 30.0, mFeedback.target());
    mMix.reset(sampleRate, 30.0, mMix.target());
    updateFilters();
    reset();
  }

  void reset() noexcept
  {
    mLine[0].reset();
    mLine[1].reset();
    for (int c = 0; c < 2; ++c)
    {
      mLowCut[c].reset();
      mHighCut[c].reset();
      mAnalogTone[c].reset();
    }
    mWowPhase = 0.0;
    mTime.setImmediate(mTime.target());
  }

  void setType(Type t) noexcept { mType = t; }
  void setTimeMs(double ms) noexcept
  {
    mTime.setTarget(std::clamp(ms, 1.0, kMaxSeconds * 1000.0 - 5.0) * mSr * 0.001);
  }
  void setFeedback(double fraction) noexcept { mFeedback.setTarget(std::clamp(fraction, 0.0, 0.95)); }
  void setMix(double fraction) noexcept { mMix.setTarget(std::clamp(fraction, 0.0, 1.0)); }
  void setFilters(double lowCutHz, double highCutHz) noexcept
  {
    if (lowCutHz != mLowCutHz || highCutHz != mHighCutHz)
    {
      mLowCutHz = lowCutHz;
      mHighCutHz = highCutHz;
      updateFilters();
    }
  }

  void process(double* l, double* r, int n) noexcept
  {
    constexpr double twoPi = 6.283185307179586476925286766559;
    const bool analog = mType == Type::Analog;
    const bool pingPong = mType == Type::PingPong;
    for (int i = 0; i < n; ++i)
    {
      double d = mTime.next();
      if (analog)
      {
        mWowPhase += 0.35 / mSr;
        mWowPhase -= std::floor(mWowPhase);
        d += 0.00025 * mSr * std::sin(twoPi * mWowPhase);
      }
      const double fb = mFeedback.next();
      const double wetL = mLine[0].read(d);
      const double wetR = mLine[1].read(d);

      double fbL = filter(0, wetL) * fb;
      double fbR = filter(1, wetR) * fb;
      if (analog)
      {
        fbL = std::tanh(fbL * 1.3) / 1.3;
        fbR = std::tanh(fbR * 1.3) / 1.3;
      }
      if (pingPong)
      {
        const double mono = 0.5 * (l[i] + r[i]);
        mLine[0].push(mono + fbR); // new input enters left, repeats cross over
        mLine[1].push(fbL);
      }
      else
      {
        mLine[0].push(l[i] + fbL);
        mLine[1].push(r[i] + fbR);
      }
      const double m = mMix.next();
      l[i] += m * wetL;
      r[i] += m * wetR;
    }
  }

private:
  double filter(int c, double x) noexcept
  {
    double y = mHighCut[c].process(mLowCut[c].process(x));
    if (mType == Type::Analog)
      y = mAnalogTone[c].process(y);
    return y;
  }
  void updateFilters() noexcept
  {
    for (int c = 0; c < 2; ++c)
    {
      mLowCut[c].setup(Biquad::Type::HighPass, mSr, mLowCutHz);
      mHighCut[c].setup(Biquad::Type::LowPass, mSr, std::min(mHighCutHz, 0.45 * mSr));
      mAnalogTone[c].setup(Biquad::Type::LowPass, mSr, 2800.0, 0.6);
    }
  }

  double mSr = 48000.0;
  Type mType = Type::Digital;
  DelayLine mLine[2];
  SmoothedValue mTime, mFeedback, mMix;
  Biquad mLowCut[2], mHighCut[2], mAnalogTone[2];
  double mLowCutHz = 80.0, mHighCutHz = 8000.0;
  double mWowPhase = 0.0;
};

} // namespace ironclad
