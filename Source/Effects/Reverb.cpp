#include "Effects/Reverb.h"

#include <algorithm>
#include <cmath>

namespace ironclad
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
// Mutually prime-ish base lengths (ms) for the hall; other types scale them.
constexpr double kLineMs[Reverb::kLines] = {31.13, 37.27, 41.87, 47.71, 53.29, 59.93, 67.07, 73.73};
constexpr double kDiffMs[Reverb::kDiffusers] = {4.77, 3.59, 12.73, 9.31};
constexpr double kMaxScale = 1.0;
constexpr double kMaxPreDelayMs = 200.0;
constexpr double kModMs = 0.35;
// Output sign patterns (orthogonal) for left / right.
constexpr double kOutL[Reverb::kLines] = {1, -1, 1, -1, 1, -1, 1, -1};
constexpr double kOutR[Reverb::kLines] = {1, 1, -1, -1, 1, 1, -1, -1};
constexpr double kIn[Reverb::kLines] = {1, -1, -1, 1, 1, 1, -1, -1};
} // namespace

void Reverb::prepare(double sampleRate, int /*maxBlock*/)
{
  mSr = sampleRate;
  const double ms = sampleRate * 0.001;
  mPreDelay.prepare(static_cast<int>(kMaxPreDelayMs * ms) + 4);
  for (int d = 0; d < kDiffusers; ++d)
    mDiff[d].prepare(static_cast<int>(kDiffMs[d] * 1.6 * ms) + 4);
  for (int i = 0; i < kLines; ++i)
    mLines[i].prepare(static_cast<int>((kLineMs[i] * kMaxScale + 2.0 * kModMs) * ms) + 8);
  mPreDelaySamples.reset(sampleRate, 50.0, mPreDelaySamples.target() == 0.0 ? 20.0 * ms : mPreDelaySamples.target());
  mMix.reset(sampleRate, 30.0, mMix.target());
  mFade.reset(sampleRate, 30.0, 1.0);
  mType = mPendingType;
  mTypeChange = false;
  configure(mType);
  reset();
}

void Reverb::reset() noexcept
{
  mPreDelay.reset();
  for (auto& d : mDiff)
    d.reset();
  for (auto& l : mLines)
    l.reset();
  mLp.fill(0.0);
  mModPhase = 0.0;
  mAppliedDecay = -1.0;
}

void Reverb::setType(Type t) noexcept
{
  if (t == mPendingType)
    return;
  mPendingType = t;
  mTypeChange = true;
  mFade.setTarget(0.0);
}

void Reverb::setPreDelayMs(double ms) noexcept
{
  mPreDelaySamples.setTarget(std::max(1.0, std::clamp(ms, 0.0, kMaxPreDelayMs) * mSr * 0.001));
}

void Reverb::configure(Type t) noexcept
{
  double scale = 1.0, diffScale = 1.0;
  switch (t)
  {
  case Type::Room: scale = 0.45; diffScale = 0.8; mDiffGain = 0.65; break;
  case Type::Hall: scale = 1.0; diffScale = 1.0; mDiffGain = 0.7; break;
  case Type::Plate: scale = 0.6; diffScale = 1.2; mDiffGain = 0.72; break;
  }
  const double ms = mSr * 0.001;
  for (int i = 0; i < kLines; ++i)
    mLen[static_cast<std::size_t>(i)] = kLineMs[i] * scale * ms;
  for (int d = 0; d < kDiffusers; ++d)
    mDiffLen[static_cast<std::size_t>(d)] = std::max(1.0, kDiffMs[d] * diffScale * ms);
  mModDepth = kModMs * ms * scale;
  mAppliedDecay = -1.0;
}

void Reverb::updateGains() noexcept
{
  // The in-loop damping filters and diffusers shorten the measured broadband decay by ~13 %;
  // compensate so the displayed Decay matches the measured RT60 (verified by tests).
  constexpr double kRtCalibration = 1.15;
  const double rt = std::max(0.05, mDecay) * kRtCalibration;
  for (int i = 0; i < kLines; ++i)
    mGain[static_cast<std::size_t>(i)] = std::pow(10.0, -3.0 * mLen[static_cast<std::size_t>(i)] / (rt * mSr));
  // Damping: one-pole low-pass in each line, cutoff 14 kHz (0 %) .. 1.5 kHz (100 %);
  // plates stay brighter.
  const double brightness = mType == Type::Plate ? 0.6 : 1.0;
  const double cutoff = 14000.0 * std::pow(1500.0 / 14000.0, std::clamp(mDamping * brightness, 0.0, 1.0));
  mDampCoef = std::exp(-kTwoPi * std::min(cutoff, 0.45 * mSr) / mSr);
  mAppliedDecay = mDecay;
  mAppliedDamping = mDamping;
}

void Reverb::process(double* l, double* r, int n) noexcept
{
  if (mDecay != mAppliedDecay || mDamping != mAppliedDamping)
    updateGains();

  const double inScale = 0.5;
  const double outScale = 0.6 / std::sqrt(static_cast<double>(kLines));
  for (int i = 0; i < n; ++i)
  {
    // Type change: fade out, switch while silent, fade back in.
    if (mTypeChange && !mFade.isSmoothing() && mFade.current() == 0.0)
    {
      mType = mPendingType;
      mTypeChange = false;
      configure(mType);
      reset();
      updateGains();
      mFade.setTarget(1.0);
    }

    const double in = (l[i] + r[i]) * 0.5 * inScale;
    mPreDelay.push(in);
    double x = mPreDelay.read(mPreDelaySamples.next());

    for (int d = 0; d < kDiffusers; ++d)
    {
      const double delayed = mDiff[d].read(mDiffLen[static_cast<std::size_t>(d)]);
      const double v = x + mDiffGain * delayed;
      mDiff[d].push(v);
      x = delayed - mDiffGain * v;
    }

    mModPhase += 0.7 / mSr;
    mModPhase -= std::floor(mModPhase);
    double y[kLines];
    double sum = 0.0;
    for (int k = 0; k < kLines; ++k)
    {
      double len = mLen[static_cast<std::size_t>(k)];
      if (k < 4)
        len += mModDepth * std::sin(kTwoPi * (mModPhase + 0.25 * k) * (1.0 + 0.13 * k));
      double v = mLines[k].read(len) * mGain[static_cast<std::size_t>(k)];
      double& lp = mLp[static_cast<std::size_t>(k)];
      lp = v + mDampCoef * (lp - v);
      y[k] = lp;
      sum += lp;
    }
    const double h = 2.0 / kLines * sum;
    double outL = 0.0, outR = 0.0;
    for (int k = 0; k < kLines; ++k)
    {
      mLines[k].push(y[k] - h + kIn[k] * x);
      outL += kOutL[k] * y[k];
      outR += kOutR[k] * y[k];
    }
    const double g = mMix.next() * mFade.next() * outScale;
    l[i] += g * outL;
    r[i] += g * outR;
  }
}

} // namespace ironclad
