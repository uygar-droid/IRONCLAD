#pragma once

// Algorithmic stereo reverb (after the delay).
//
//   pre-delay -> 4 series all-pass diffusers -> 8-line feedback delay network
//   (Householder matrix, per-line decay gain from RT60, one-pole damping in each line,
//   slow modulation on four lines against metallic ringing) -> two decorrelated outputs.
//
//   Room  : short lines (x0.45), moderate diffusion.
//   Hall  : long lines (x1.0), moderate diffusion.
//   Plate : medium lines (x0.6), dense diffusion, brighter damping.
// Changing the type fades the reverb out, rebuilds and fades it back in (no clicks).
// Output = dry + mix * wet.

#include <array>

#include "Audio/SmoothedValue.h"
#include "Dsp/DelayLine.h"

namespace ironclad
{

class Reverb
{
public:
  enum class Type
  {
    Room = 0,
    Hall = 1,
    Plate = 2
  };
  static constexpr int kLines = 8;
  static constexpr int kDiffusers = 4;

  void prepare(double sampleRate, int maxBlock);
  void reset() noexcept;

  void setType(Type t) noexcept;
  void setDecaySeconds(double s) noexcept { mDecay = s; }
  void setPreDelayMs(double ms) noexcept;
  void setDamping(double fraction) noexcept { mDamping = fraction; }
  void setMix(double fraction) noexcept { mMix.setTarget(fraction); }

  void process(double* l, double* r, int n) noexcept;

private:
  void configure(Type t) noexcept; // lengths/diffusion for a type (no allocation)
  void updateGains() noexcept;

  double mSr = 48000.0;
  Type mType = Type::Hall, mPendingType = Type::Hall;
  bool mTypeChange = false;
  double mDecay = 1.8, mDamping = 0.5;
  double mAppliedDecay = -1.0, mAppliedDamping = -1.0;

  DelayLine mPreDelay;
  SmoothedValue mPreDelaySamples, mMix, mFade;

  DelayLine mDiff[kDiffusers];
  std::array<double, kDiffusers> mDiffLen{};
  double mDiffGain = 0.7;

  DelayLine mLines[kLines];
  std::array<double, kLines> mLen{};
  std::array<double, kLines> mGain{};
  std::array<double, kLines> mLp{};
  double mDampCoef = 0.3;
  double mModPhase = 0.0;
  double mModDepth = 0.0;
};

} // namespace ironclad
