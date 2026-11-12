#pragma once

// First-order antiderivative anti-aliasing (ADAA) for memoryless waveshapers.
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),  F' = f
// with the midpoint f((x[n]+x[n-1])/2) when the difference is tiny. This suppresses most of
// the aliasing of a naive waveshaper without oversampling and without reported latency
// (it adds about half a sample of delay).

#include <cmath>

namespace ironclad
{

/// f(u) = tanh(u + bias) - tanh(bias): soft, optionally asymmetric clipper (f(0) = 0).
struct TanhShape
{
  double bias = 0.0;
  double offset = 0.0; // tanh(bias)

  void setBias(double b) noexcept
  {
    bias = b;
    offset = std::tanh(b);
  }
  double f(double u) const noexcept { return std::tanh(u + bias) - offset; }
  static double logCosh(double x) noexcept
  {
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942; // stable for large |x|
  }
  double F(double u) const noexcept { return logCosh(u + bias) - offset * u; }
};

/// f(u) = clamp(u, -1, 1): hard clipper.
struct HardClipShape
{
  static double f(double u) noexcept { return u > 1.0 ? 1.0 : (u < -1.0 ? -1.0 : u); }
  static double F(double u) noexcept
  {
    const double a = std::abs(u);
    return a <= 1.0 ? 0.5 * u * u : a - 0.5;
  }
};

template <typename Shape>
class Adaa1
{
public:
  Shape shape;

  void reset() noexcept
  {
    mX1 = 0.0;
    mF1 = shape.F(0.0);
  }

  double process(double x) noexcept
  {
    const double Fx = shape.F(x);
    const double d = x - mX1;
    const double y = (std::abs(d) < 1e-7) ? shape.f(0.5 * (x + mX1)) : (Fx - mF1) / d;
    mX1 = x;
    mF1 = Fx;
    return y;
  }

private:
  double mX1 = 0.0;
  double mF1 = 0.0;
};

} // namespace ironclad
