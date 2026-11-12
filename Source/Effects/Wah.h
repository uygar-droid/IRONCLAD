#pragma once

// Wah (mono): resonant band-pass (TPT state-variable filter) swept 350 Hz .. 2.2 kHz
// (log) by the pedal position, mixed with a little low-pass for body, then wet/dry mix.
// Position is smoothed (15 ms) so automation/MIDI sweeps are zipper-free.

#include <algorithm>
#include <cmath>

#include "Audio/SmoothedValue.h"

namespace ironclad
{

class Wah
{
public:
  static constexpr double kMinHz = 350.0;
  static constexpr double kMaxHz = 2200.0;

  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mSr = sampleRate;
    mPos.reset(sampleRate, 15.0, mPos.target());
    mRes.reset(sampleRate, 30.0, mRes.target() == 0.0 ? 0.6 : mRes.target());
    mMix.reset(sampleRate, 30.0, mMix.target() == 0.0 ? 1.0 : mMix.target());
    reset();
  }
  void reset() noexcept { mIc1 = mIc2 = 0.0; }
  void setPosition(double p) noexcept { mPos.setTarget(std::clamp(p, 0.0, 1.0)); }
  void setResonance(double r) noexcept { mRes.setTarget(std::clamp(r, 0.0, 1.0)); }
  void setMix(double m) noexcept { mMix.setTarget(std::clamp(m, 0.0, 1.0)); }

  static double frequencyFor(double position) noexcept { return kMinHz * std::pow(kMaxHz / kMinHz, position); }

  void process(double* x, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const double f = frequencyFor(mPos.next());
      const double q = 1.5 + 8.5 * mRes.next();
      const double g = std::tan(3.14159265358979323846 * std::min(f, 0.45 * mSr) / mSr);
      const double k = 1.0 / q;
      const double a1 = 1.0 / (1.0 + g * (g + k));
      const double a2 = g * a1;
      const double a3 = g * a2;
      const double v0 = x[i];
      const double v3 = v0 - mIc2;
      const double v1 = a1 * mIc1 + a2 * v3;
      const double v2 = mIc2 + a2 * mIc1 + a3 * v3;
      mIc1 = 2.0 * v1 - mIc1;
      mIc2 = 2.0 * v2 - mIc2;
      const double band = v1 * k * 2.0; // ~ +6 dB at the peak, like a real wah's boost
      const double wet = band + 0.15 * v2;
      const double m = mMix.next();
      x[i] = v0 + m * (wet - v0);
    }
  }

private:
  double mSr = 48000.0;
  SmoothedValue mPos, mRes, mMix;
  double mIc1 = 0.0, mIc2 = 0.0;
};

} // namespace ironclad
