#pragma once

// Circular delay line with fractional (cubic Hermite) reads.
// prepare() allocates; push()/read() are real-time safe.

#include <algorithm>
#include <cmath>
#include <vector>

namespace ironclad
{

class DelayLine
{
public:
  void prepare(int maxDelaySamples)
  {
    int size = 4;
    while (size < maxDelaySamples + 4)
      size <<= 1;
    mBuffer.assign(static_cast<std::size_t>(size), 0.0);
    mMask = size - 1;
    mWrite = 0;
    mMaxDelay = maxDelaySamples;
  }

  void reset() noexcept { std::fill(mBuffer.begin(), mBuffer.end(), 0.0); }

  void push(double x) noexcept
  {
    mBuffer[static_cast<std::size_t>(mWrite)] = x;
    mWrite = (mWrite + 1) & mMask;
  }

  /// Value written `delay` samples before the most recent push (delay >= 1).
  double readInteger(int delay) const noexcept
  {
    return mBuffer[static_cast<std::size_t>((mWrite - delay) & mMask)];
  }

  /// Fractional delay in samples, clamped to [1, maxDelay].
  double read(double delay) const noexcept
  {
    delay = std::clamp(delay, 1.0, static_cast<double>(mMaxDelay));
    const int d = static_cast<int>(delay);
    const double t = delay - d;
    // Points around the read position: x0 = delay d-1 (newer) ... x3 = delay d+2 (older).
    const double xm1 = readInteger(std::max(1, d - 1));
    const double x0 = readInteger(d);
    const double x1 = readInteger(d + 1);
    const double x2 = readInteger(d + 2);
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
  }

  int maxDelay() const noexcept { return mMaxDelay; }

private:
  std::vector<double> mBuffer;
  int mMask = 0;
  int mWrite = 0;
  int mMaxDelay = 0;
};

} // namespace ironclad
