#pragma once

// Clean boost (mono): smoothed gain, optional "bright" high shelf (+6 dB above ~2.5 kHz).

#include <cmath>

#include "Audio/SmoothedValue.h"
#include "Dsp/Biquad.h"

namespace ironclad
{

class Boost
{
public:
  void prepare(double sampleRate, int /*maxBlock*/)
  {
    mGain.reset(sampleRate, 30.0, mGain.target() == 0.0 ? 1.0 : mGain.target());
    mBright.reset(sampleRate, 30.0, mBright.target());
    mShelf.setup(Biquad::Type::HighShelf, sampleRate, 2500.0, 0.7071, 6.0);
    reset();
  }
  void reset() noexcept { mShelf.reset(); }
  void setGainDb(double db) noexcept { mGain.setTarget(std::pow(10.0, db / 20.0)); }
  void setBright(bool on) noexcept { mBright.setTarget(on ? 1.0 : 0.0); }

  void process(double* x, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const double b = mBright.next();
      const double s = mShelf.process(x[i]);
      x[i] = (x[i] + b * (s - x[i])) * mGain.next();
    }
  }

private:
  SmoothedValue mGain, mBright;
  Biquad mShelf;
};

} // namespace ironclad
