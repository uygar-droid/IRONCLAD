#pragma once

// Drive pedals (mono): Overdrive, Distortion and Fuzz share one structure with different
// voicing. Waveshaping uses first-order ADAA (Dsp/Adaa.h) to reduce aliasing without
// oversampling latency.
//
//   Overdrive : HPF 720 Hz on the clipped path (mid-focused, keeps lows clean via a clean
//               blend), soft asymmetric tanh, gain 1x..60x, tone = LPF 1.2..7 kHz.
//   Distortion: HPF 90 Hz, gain 4x..600x, hard clip, fixed LPF 7 kHz, tone = LPF 0.9..9 kHz.
//   Fuzz      : gain 2x..1500x into a strongly biased tanh (asymmetric, octave-ish harmonics),
//               DC blocker, tone = LPF 0.6..6 kHz.
// Level is an output trim in dB applied after a fixed per-type loudness compensation.

#include <algorithm>
#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/Adaa.h"
#include "Dsp/Biquad.h"

namespace ironclad
{

class Drive
{
public:
  enum class Character
  {
    Overdrive,
    Distortion,
    Fuzz
  };

  explicit Drive(Character c = Character::Overdrive)
  : mChar(c)
  {
  }

  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    mDrive.reset(sampleRate, 40.0, mDrive.target());
    mLevel.reset(sampleRate, 30.0, mLevel.target() == 0.0 ? 1.0 : mLevel.target());
    mToneHz.reset(sampleRate, 30.0, mToneHz.target() == 0.0 ? 3000.0 : mToneHz.target());
    switch (mChar)
    {
    case Character::Overdrive: mPre.setup(Biquad::Type::HighPass, sampleRate, 720.0, 0.5); break;
    case Character::Distortion: mPre.setup(Biquad::Type::HighPass, sampleRate, 90.0); break;
    case Character::Fuzz: mPre.setup(Biquad::Type::HighPass, sampleRate, 40.0); break;
    }
    mPost.setup(Biquad::Type::LowPass, sampleRate, 7000.0);
    mDcBlock.setup(Biquad::Type::HighPass, sampleRate, 15.0);
    mSoft.shape.setBias(mChar == Character::Fuzz ? 0.6 : 0.12);
    updateTone();
    reset();
  }

  void reset() noexcept
  {
    mPre.reset();
    mPost.reset();
    mTone.reset();
    mDcBlock.reset();
    mSoft.reset();
    mHard.reset();
    mToneCounter = 0;
  }

  /// 0..1
  void setDrive(double amount) noexcept { mDrive.setTarget(std::clamp(amount, 0.0, 1.0)); }
  /// 0..1
  void setTone(double amount) noexcept
  {
    amount = std::clamp(amount, 0.0, 1.0);
    double lo = 1200.0, hi = 7000.0;
    if (mChar == Character::Distortion) { lo = 900.0; hi = 9000.0; }
    if (mChar == Character::Fuzz) { lo = 600.0; hi = 6000.0; }
    mToneHz.setTarget(lo * std::pow(hi / lo, amount));
  }
  void setLevelDb(double db) noexcept { mLevel.setTarget(std::pow(10.0, db / 20.0)); }

  /// Linear pre-clip gain for a drive amount (exposed for tests).
  double gainFor(double drive) const noexcept
  {
    switch (mChar)
    {
    case Character::Overdrive: return std::pow(60.0, drive);
    case Character::Distortion: return 4.0 * std::pow(150.0, drive);
    case Character::Fuzz: return 2.0 * std::pow(750.0, drive);
    }
    return 1.0;
  }

  void process(double* x, int n) noexcept
  {
    const double comp = mChar == Character::Overdrive ? 0.7 : (mChar == Character::Distortion ? 0.35 : 0.3);
    for (int i = 0; i < n; ++i)
    {
      if (mToneCounter-- <= 0)
      {
        updateTone();
        mToneCounter = 16;
      }
      const double drive = mDrive.next();
      mToneHz.next(); // advanced per sample; applied every 16 samples by updateTone()
      const double in = x[i];
      double y = in;
      switch (mChar)
      {
      case Character::Overdrive:
      {
        // Clean low end + clipped mids, as in classic "screamer" circuits.
        const double clipped = mSoft.process(mPre.process(in) * gainFor(drive));
        y = 0.5 * in + clipped;
        break;
      }
      case Character::Distortion: y = mPost.process(mHard.process(mPre.process(in) * gainFor(drive))); break;
      case Character::Fuzz: y = mDcBlock.process(mSoft.process(in * gainFor(drive))); break;
      }
      x[i] = mTone.process(y) * comp * mLevel.next();
    }
  }

private:
  void updateTone() noexcept { mTone.setup(Biquad::Type::LowPass, mSr, mToneHz.current(), 0.6); }

  Character mChar;
  double mSr = 48000.0;
  SmoothedValue mDrive, mLevel, mToneHz;
  Biquad mPre, mPost, mTone, mDcBlock;
  Adaa1<TanhShape> mSoft;
  Adaa1<HardClipShape> mHard;
  int mToneCounter = 0;
};

} // namespace ironclad
