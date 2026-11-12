#pragma once

// Tap-tempo helper (UI thread). Feed it tap times in seconds; after two taps it returns the
// average of the last (up to) three intervals in milliseconds. A pause longer than 2 s
// starts a new sequence. Intervals are clamped to the delay's range.

#include <array>
#include <optional>

namespace ironclad
{

class TapTempo
{
public:
  static constexpr double kTimeoutSeconds = 2.0;

  std::optional<double> tap(double timeSeconds) noexcept
  {
    if (mCount > 0 && (timeSeconds - mLast > kTimeoutSeconds || timeSeconds <= mLast))
      mCount = 0;
    if (mCount > 0)
    {
      mIntervals[mNext] = timeSeconds - mLast;
      mNext = (mNext + 1) % mIntervals.size();
      mNumIntervals = std::min<int>(mNumIntervals + 1, static_cast<int>(mIntervals.size()));
    }
    else
      mNumIntervals = 0;
    mLast = timeSeconds;
    ++mCount;
    if (mNumIntervals == 0)
      return std::nullopt;
    double sum = 0.0;
    for (int i = 0; i < mNumIntervals; ++i)
      sum += mIntervals[static_cast<std::size_t>(i)];
    double ms = 1000.0 * sum / mNumIntervals;
    return ms < 10.0 ? 10.0 : (ms > 2000.0 ? 2000.0 : ms);
  }

private:
  std::array<double, 3> mIntervals{};
  std::size_t mNext = 0;
  int mNumIntervals = 0;
  int mCount = 0;
  double mLast = 0.0;
};

} // namespace ironclad
