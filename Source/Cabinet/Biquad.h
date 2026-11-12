#pragma once

// RBJ-cookbook 2nd-order high-pass / low-pass, transposed direct form II (double).

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
    LowPass
  };

  void setup(Type type, double sampleRate, double freq, double q = 0.70710678118654752) noexcept
  {
    freq = std::clamp(freq, 5.0, 0.45 * sampleRate);
    const double w0 = 6.283185307179586476925286766559 * freq / sampleRate;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    const double a0 = 1.0 + alpha;
    double b0, b1, b2;
    if (type == Type::LowPass)
    {
      b0 = (1.0 - cs) * 0.5;
      b1 = 1.0 - cs;
      b2 = b0;
    }
    else
    {
      b0 = (1.0 + cs) * 0.5;
      b1 = -(1.0 + cs);
      b2 = b0;
    }
    mB0 = b0 / a0;
    mB1 = b1 / a0;
    mB2 = b2 / a0;
    mA1 = (-2.0 * cs) / a0;
    mA2 = (1.0 - alpha) / a0;
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

} // namespace ironclad
