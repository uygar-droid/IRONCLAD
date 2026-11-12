#pragma once

// RBJ-cookbook biquads (high/low-pass, band-pass, peaking, shelves), transposed direct
// form II, double precision. setup() is cheap and allocation-free, so it may be called
// from the audio thread (e.g. every few samples while a frequency is being smoothed).

#include <algorithm>
#include <cmath>

namespace ironclad
{

class Biquad
{
public:
  enum class Type
  {
    HighPass,
    LowPass,
    BandPass, // constant 0 dB peak gain
    Peak,
    LowShelf,
    HighShelf
  };

  void setup(Type type, double sampleRate, double freq, double q = 0.70710678118654752, double gainDb = 0.0) noexcept
  {
    freq = std::clamp(freq, 5.0, 0.45 * sampleRate);
    q = std::max(q, 0.05);
    const double w0 = 6.283185307179586476925286766559 * freq / sampleRate;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
    case Type::LowPass:
      b0 = (1.0 - cs) * 0.5; b1 = 1.0 - cs; b2 = b0;
      a0 = 1.0 + alpha; a1 = -2.0 * cs; a2 = 1.0 - alpha;
      break;
    case Type::HighPass:
      b0 = (1.0 + cs) * 0.5; b1 = -(1.0 + cs); b2 = b0;
      a0 = 1.0 + alpha; a1 = -2.0 * cs; a2 = 1.0 - alpha;
      break;
    case Type::BandPass:
      b0 = alpha; b1 = 0.0; b2 = -alpha;
      a0 = 1.0 + alpha; a1 = -2.0 * cs; a2 = 1.0 - alpha;
      break;
    case Type::Peak:
      b0 = 1.0 + alpha * A; b1 = -2.0 * cs; b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A; a1 = -2.0 * cs; a2 = 1.0 - alpha / A;
      break;
    case Type::LowShelf:
    {
      const double s = 2.0 * std::sqrt(A) * alpha;
      b0 = A * ((A + 1) - (A - 1) * cs + s);
      b1 = 2 * A * ((A - 1) - (A + 1) * cs);
      b2 = A * ((A + 1) - (A - 1) * cs - s);
      a0 = (A + 1) + (A - 1) * cs + s;
      a1 = -2 * ((A - 1) + (A + 1) * cs);
      a2 = (A + 1) + (A - 1) * cs - s;
      break;
    }
    case Type::HighShelf:
    {
      const double s = 2.0 * std::sqrt(A) * alpha;
      b0 = A * ((A + 1) + (A - 1) * cs + s);
      b1 = -2 * A * ((A - 1) + (A + 1) * cs);
      b2 = A * ((A + 1) + (A - 1) * cs - s);
      a0 = (A + 1) - (A - 1) * cs + s;
      a1 = 2 * ((A - 1) - (A + 1) * cs);
      a2 = (A + 1) - (A - 1) * cs - s;
      break;
    }
    }
    mB0 = b0 / a0;
    mB1 = b1 / a0;
    mB2 = b2 / a0;
    mA1 = a1 / a0;
    mA2 = a2 / a0;
  }

  double process(double x) noexcept
  {
    const double y = mB0 * x + mZ1;
    mZ1 = mB1 * x - mA1 * y + mZ2;
    mZ2 = mB2 * x - mA2 * y;
    return y;
  }

  void reset() noexcept { mZ1 = mZ2 = 0.0; }

private:
  double mB0 = 1.0, mB1 = 0.0, mB2 = 0.0, mA1 = 0.0, mA2 = 0.0;
  double mZ1 = 0.0, mZ2 = 0.0;
};

/// One-pole smoothing coefficient for a time constant in milliseconds.
inline double onePoleCoef(double sampleRate, double ms) noexcept
{
  return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (sampleRate * ms * 0.001));
}

} // namespace ironclad
