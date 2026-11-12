#pragma once

// Click-free on/off for any effect. While switching, the effect runs and its output is
// crossfaded with the unprocessed signal (20 ms). When fully off, the effect is skipped
// (no CPU) and it is reset before it is switched on again, so stale state (old delay
// lines, envelopes) never leaks into the first block.

#include <algorithm>
#include <vector>

#include "Audio/SmoothedValue.h"

namespace ironclad
{

class BypassSlot
{
public:
  void prepare(double sampleRate, int maxBlockSize, bool initiallyOn)
  {
    mMix.reset(sampleRate, 20.0, initiallyOn ? 1.0 : 0.0);
    mDryL.assign(static_cast<std::size_t>(std::max(1, maxBlockSize)), 0.0);
    mDryR.assign(static_cast<std::size_t>(std::max(1, maxBlockSize)), 0.0);
    mWasOff = !initiallyOn;
  }

  void setOn(bool on) noexcept { mMix.setTarget(on ? 1.0 : 0.0); }
  bool fullyOff() const noexcept { return !mMix.isSmoothing() && mMix.current() == 0.0; }

  /// Runs `effect(n)` on the buffers (mono: right == nullptr) with crossfaded bypass.
  /// `resetEffect()` is called when the effect becomes audible after having been off.
  template <typename ProcessFn, typename ResetFn>
  void run(double* left, double* right, int n, ProcessFn&& effect, ResetFn&& resetEffect) noexcept
  {
    if (fullyOff())
    {
      mWasOff = true;
      return;
    }
    if (mWasOff)
    {
      resetEffect();
      mWasOff = false;
    }
    if (!mMix.isSmoothing() && mMix.current() == 1.0)
    {
      effect(n);
      return;
    }
    std::copy(left, left + n, mDryL.data());
    if (right)
      std::copy(right, right + n, mDryR.data());
    effect(n);
    for (int i = 0; i < n; ++i)
    {
      const double m = mMix.next();
      left[i] = mDryL[static_cast<std::size_t>(i)] + m * (left[i] - mDryL[static_cast<std::size_t>(i)]);
      if (right)
        right[i] = mDryR[static_cast<std::size_t>(i)] + m * (right[i] - mDryR[static_cast<std::size_t>(i)]);
    }
  }

private:
  SmoothedValue mMix;
  std::vector<double> mDryL, mDryR;
  bool mWasOff = true;
};

} // namespace ironclad
