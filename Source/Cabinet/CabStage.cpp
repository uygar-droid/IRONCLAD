#include "Cabinet/CabStage.h"

#include <algorithm>
#include <cmath>

namespace ironclad
{
namespace
{
constexpr double kHalfPi = 1.5707963267948966192313216916398;
constexpr int kFilterUpdateInterval = 16;
double dbToLin(double db) noexcept { return std::pow(10.0, db / 20.0); }
} // namespace

CabStage::~CabStage()
{
  for (auto& s : mSlots)
  {
    delete s.live;
    delete s.fading;
    delete s.held;
  }
  for (int i = 0; i < mNumPendingRetire; ++i)
    delete mPendingRetire[i];
  PreparedIr* p = nullptr;
  while (mReady.pop(p))
    delete p;
  while (mRetire.pop(p))
    delete p;
}

void CabStage::prepare(double sampleRate, int maxBlockSize, std::uint64_t generation)
{
  mGeneration = generation;
  mSampleRate = sampleRate;
  const auto n = static_cast<std::size_t>(std::max(1, maxBlockSize));
  for (auto* v : {&mDry, &mAL, &mAR, &mBL, &mBR, &mSumL, &mSumR})
    v->assign(n, 0.0);
  mFadeLen = std::max(1, static_cast<int>(std::lround(sampleRate * kCrossfadeMs * 0.001)));

  for (int i = 0; i < mNumPendingRetire; ++i)
    delete mPendingRetire[i];
  mNumPendingRetire = 0;

  for (auto& s : mSlots)
  {
    if (s.fadeActive)
    {
      delete s.fading;
      s.fading = nullptr;
      s.fadeActive = false;
    }
    for (PreparedIr* p : {s.live, s.held})
      if (p)
      {
        if (p->ir)
          p->ir->prepare(sampleRate);
        p->generation = generation;
      }
    s.gain.reset(sampleRate, 30.0, mPrepared ? s.gain.target() : 0.0);
    s.polarity.reset(sampleRate, 20.0, mPrepared ? s.polarity.target() : 1.0);
    s.balL.reset(sampleRate, 30.0, mPrepared ? s.balL.target() : 1.0);
    s.balR.reset(sampleRate, 30.0, mPrepared ? s.balR.target() : 1.0);
  }

  // IRs prepared for the old rate but not yet taken: re-prepare and requeue.
  std::vector<PreparedIr*> pending;
  PreparedIr* p = nullptr;
  while (mReady.pop(p))
    pending.push_back(p);
  for (auto* item : pending)
  {
    if (item->ir)
      item->ir->prepare(sampleRate);
    item->generation = generation;
    if (!mReady.push(std::move(item)))
      delete item;
  }

  mWet.reset(sampleRate, 25.0, mPrepared ? mWet.target() : 0.0);
  mHpfAmount.reset(sampleRate, 30.0, mPrepared ? mHpfAmount.target() : (mTargets.highPassHz > kHpfOffHz ? 1.0 : 0.0));
  mLpfAmount.reset(sampleRate, 30.0, mPrepared ? mLpfAmount.target() : (mTargets.lowPassHz < kLpfOffHz ? 1.0 : 0.0));
  mHpfLog2.reset(sampleRate, 30.0, std::log2(mPrepared ? std::exp2(mHpfLog2.target()) : mTargets.highPassHz));
  mLpfLog2.reset(sampleRate, 30.0, std::log2(mPrepared ? std::exp2(mLpfLog2.target()) : mTargets.lowPassHz));
  for (int c = 0; c < 2; ++c)
  {
    mHpf[c].setup(Biquad::Type::HighPass, sampleRate, std::exp2(mHpfLog2.current()));
    mLpf[c].setup(Biquad::Type::LowPass, sampleRate, std::exp2(mLpfLog2.current()));
    mHpf[c].reset();
    mLpf[c].reset();
  }
  mPrepared = true;
}

void CabStage::retire(PreparedIr* p) noexcept
{
  if (p == nullptr)
    return;
  if (!mRetire.push(std::move(p)) && mNumPendingRetire < 16)
    mPendingRetire[mNumPendingRetire++] = p; // retried next block; never freed here
}

void CabStage::flushRetired() noexcept
{
  while (mNumPendingRetire > 0)
  {
    PreparedIr* p = mPendingRetire[mNumPendingRetire - 1];
    if (!mRetire.push(std::move(p)))
      break;
    --mNumPendingRetire;
  }
}

void CabStage::acceptIncoming() noexcept
{
  // Drain the queue into per-slot "held" positions (newest wins; superseded IRs retire).
  if (mNumPendingRetire < 12)
  {
    PreparedIr* in = nullptr;
    while (mNumPendingRetire < 12 && mReady.pop(in))
    {
      if (in->generation != mGeneration || in->slot < 0 || in->slot >= kSlots)
      {
        retire(in);
        continue;
      }
      Slot& s = mSlots[in->slot];
      retire(s.held);
      s.held = in;
    }
  }
  for (auto& s : mSlots)
  {
    if (s.held && !s.fadeActive)
    {
      s.fading = s.live;
      s.live = s.held;
      s.held = nullptr;
      s.fadeActive = true;
      s.fadePos = 0;
      if (s.live->ir)
        s.live->ir->reset();
      mActivations.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void CabStage::renderSlot(int index, const double* mono, int n) noexcept
{
  Slot& s = mSlots[index];
  double* L = index == 0 ? mAL.data() : mBL.data();
  double* R = index == 0 ? mAR.data() : mBR.data();

  if (hasIr(s.live))
    s.live->ir->process(mono, L, R, n);
  else
  {
    std::fill(L, L + n, 0.0);
    std::fill(R, R + n, 0.0);
  }

  if (s.fadeActive)
  {
    // Old IR rendered into the sum buffers temporarily, then crossfaded in place.
    double* oL = mSumL.data();
    double* oR = mSumR.data();
    if (hasIr(s.fading))
      s.fading->ir->process(mono, oL, oR, n);
    else
    {
      std::fill(oL, oL + n, 0.0);
      std::fill(oR, oR + n, 0.0);
    }
    for (int i = 0; i < n; ++i)
    {
      const double t = std::min(1.0, static_cast<double>(s.fadePos) / mFadeLen);
      const double gNew = std::sin(t * kHalfPi), gOld = std::cos(t * kHalfPi);
      L[i] = L[i] * gNew + oL[i] * gOld;
      R[i] = R[i] * gNew + oR[i] * gOld;
      if (s.fadePos < mFadeLen)
        ++s.fadePos;
    }
    if (s.fadePos >= mFadeLen)
    {
      PreparedIr* old = s.fading;
      s.fading = nullptr;
      s.fadeActive = false;
      retire(old);
    }
  }
}

void CabStage::process(const double* mono, double* outLeft, double* outRight, int n) noexcept
{
  if (!mPrepared || n <= 0)
    return;

  flushRetired();
  acceptIncoming();

  // ---- Targets (once per block) ----
  bool active[kSlots];
  for (int k = 0; k < kSlots; ++k)
    active[k] = mTargets.slotEnabled[k] && (hasIr(mSlots[k].live) || hasIr(mSlots[k].held));
  double mixGain[kSlots] = {0.0, 0.0};
  if (active[0] && active[1])
  {
    mixGain[0] = 1.0 - mTargets.blend;
    mixGain[1] = mTargets.blend;
  }
  else if (active[0])
    mixGain[0] = 1.0;
  else if (active[1])
    mixGain[1] = 1.0;

  for (int k = 0; k < kSlots; ++k)
  {
    Slot& s = mSlots[k];
    s.gain.setTarget(mixGain[k] * dbToLin(mTargets.levelDb[k]));
    s.polarity.setTarget(mTargets.invert[k] ? -1.0 : 1.0);
    const double p = std::clamp(mTargets.pan[k], -1.0, 1.0);
    s.balL.setTarget(p <= 0.0 ? 1.0 : 1.0 - p);
    s.balR.setTarget(p >= 0.0 ? 1.0 : 1.0 + p);
  }
  mWet.setTarget((mTargets.enabled && (active[0] || active[1])) ? 1.0 : 0.0);
  mHpfLog2.setTarget(std::log2(std::max(1.0, mTargets.highPassHz)));
  mLpfLog2.setTarget(std::log2(std::max(1.0, mTargets.lowPassHz)));
  mHpfAmount.setTarget(mTargets.highPassHz > kHpfOffHz ? 1.0 : 0.0);
  mLpfAmount.setTarget(mTargets.lowPassHz < kLpfOffHz ? 1.0 : 0.0);

  // Keep a copy of the dry input: outputs may alias it.
  std::copy(mono, mono + n, mDry.data());
  const double* dry = mDry.data();

  // ---- Render both slots ----
  renderSlot(0, dry, n);
  renderSlot(1, dry, n);

  // ---- Mix ----
  for (int i = 0; i < n; ++i)
  {
    double l = 0.0, r = 0.0;
    for (int k = 0; k < kSlots; ++k)
    {
      Slot& s = mSlots[k];
      const double g = s.gain.next() * s.polarity.next();
      const double* L = k == 0 ? mAL.data() : mBL.data();
      const double* R = k == 0 ? mAR.data() : mBR.data();
      l += L[i] * g * s.balL.next();
      r += R[i] * g * s.balR.next();
    }

    if ((i % kFilterUpdateInterval) == 0)
    {
      const double hp = std::exp2(mHpfLog2.current()), lp = std::exp2(mLpfLog2.current());
      for (int c = 0; c < 2; ++c)
      {
        mHpf[c].setup(Biquad::Type::HighPass, mSampleRate, hp);
        mLpf[c].setup(Biquad::Type::LowPass, mSampleRate, lp);
      }
    }
    mHpfLog2.next();
    mLpfLog2.next();
    // Filters always run (state stays warm); their contribution fades in/out at the extremes.
    const double ha = mHpfAmount.next(), la = mLpfAmount.next();
    double fl = mHpf[0].process(l), fr = mHpf[1].process(r);
    l += ha * (fl - l);
    r += ha * (fr - r);
    fl = mLpf[0].process(l);
    fr = mLpf[1].process(r);
    l += la * (fl - l);
    r += la * (fr - r);

    const double w = mWet.next();
    outLeft[i] = dry[i] + w * (l - dry[i]);
    outRight[i] = dry[i] + w * (r - dry[i]);
  }
}

} // namespace ironclad
