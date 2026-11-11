#include "Amp/AmpStage.h"

#include <algorithm>
#include <cmath>

namespace ironclad
{
namespace
{
constexpr double kHalfPi = 1.5707963267948966192313216916398;
double dbToLin(double db) noexcept { return std::pow(10.0, db / 20.0); }
} // namespace

AmpStage::~AmpStage()
{
  delete mLive;
  delete mFading;
  delete mPendingRetire;
  PreparedAmp* p = nullptr;
  while (mReady.pop(p))
    delete p;
  while (mRetire.pop(p))
    delete p;
}

void AmpStage::prepare(double sampleRate, int maxBlockSize, std::uint64_t generation)
{
  mGeneration = generation;
  const auto n = static_cast<std::size_t>(std::max(1, maxBlockSize));
  for (auto* v : {&mDry, &mDriven, &mScratch, &mOutA, &mOutB, &mIn, &mOut, &mMixBuf, &mNorm})
    v->assign(n, 0.0);

  mFadeLen = std::max(1, static_cast<int>(std::lround(sampleRate * kCrossfadeMs * 0.001)));

  // A crossfade cannot continue across a sample-rate change: finish it instantly.
  if (mFadeActive)
  {
    delete mFading;
    mFading = nullptr;
    mFadeActive = false;
  }
  delete mPendingRetire;
  mPendingRetire = nullptr;

  if (mLive && mLive->model)
    mLive->model->prepare(sampleRate, maxBlockSize);
  if (mLive)
    mLive->generation = generation;

  // Captures that were prepared for the old rate but not yet picked up: re-prepare and requeue.
  // Safe because the audio thread is stopped and the loader lock is held by the caller.
  std::vector<PreparedAmp*> pending;
  PreparedAmp* p = nullptr;
  while (mReady.pop(p))
    pending.push_back(p);
  for (auto* item : pending)
  {
    if (item->model)
      item->model->prepare(sampleRate, maxBlockSize);
    item->generation = generation;
    if (!mReady.push(std::move(item)))
      delete item;
  }

  const double sr = sampleRate;
  mInputGain.reset(sr, 30.0, mPrepared ? mInputGain.target() : 1.0);
  mOutputGain.reset(sr, 30.0, mPrepared ? mOutputGain.target() : 1.0);
  mMix.reset(sr, 20.0, mPrepared ? mMix.target() : 1.0);
  mNormAmount.reset(sr, 30.0, mPrepared ? mNormAmount.target() : 1.0);
  mLatency.store(mLive && mLive->model ? mLive->model->latencySamples() : 0, std::memory_order_relaxed);
  mPrepared = true;
}

void AmpStage::setTargets(double inputDb, double outputDb, bool enabled, bool normalize) noexcept
{
  mInputGain.setTarget(dbToLin(inputDb));
  mOutputGain.setTarget(dbToLin(outputDb));
  mMix.setTarget(enabled ? 1.0 : 0.0);
  mNormAmount.setTarget(normalize ? 1.0 : 0.0);
}

void AmpStage::retire(PreparedAmp* amp) noexcept
{
  if (amp == nullptr)
    return;
  if (!mRetire.push(std::move(amp)))
    mPendingRetire = amp; // queue full: keep it and retry next block (never free here)
}

void AmpStage::renderSlot(PreparedAmp* slot, const double* dry, const double* driven, double* scratchIn, double* out,
                          int n) noexcept
{
  if (slot == nullptr || !slot->model)
  {
    std::copy(dry, dry + n, out); // no capture loaded: the amp stage is transparent
    return;
  }
  std::copy(driven, driven + n, scratchIn); // the model gets its own copy of its input
  slot->model->process(scratchIn, out, n);
  const double norm = dbToLin(slot->model->normalizationDb());
  const double* amount = mNorm.data();
  const double* level = mOut.data();
  for (int i = 0; i < n; ++i)
    out[i] *= (1.0 + amount[i] * (norm - 1.0)) * level[i];
}

void AmpStage::process(double* io, int n) noexcept
{
  if (!mPrepared || n <= 0)
    return;

  // 1. Retire bookkeeping and pick up a newly prepared capture (one switch at a time).
  if (mPendingRetire != nullptr)
  {
    PreparedAmp* p = mPendingRetire;
    if (mRetire.push(std::move(p)))
      mPendingRetire = nullptr;
  }
  if (!mFadeActive && mPendingRetire == nullptr)
  {
    PreparedAmp* incoming = nullptr;
    if (mReady.pop(incoming))
    {
      if (incoming->generation != mGeneration)
      {
        retire(incoming); // prepared for an old sample rate; loader will rebuild on demand
      }
      else
      {
        mFading = mLive;
        mLive = incoming;
        mFadeActive = true;
        mFadePos = 0;
        mActivations.fetch_add(1, std::memory_order_relaxed);
        mLatency.store(mLive->model ? mLive->model->latencySamples() : 0, std::memory_order_relaxed);
      }
    }
  }

  // 2. Per-sample control values (shared by both slots during a crossfade).
  double* dry = mDry.data();
  double* driven = mDriven.data();
  std::copy(io, io + n, dry);
  for (int i = 0; i < n; ++i)
  {
    driven[i] = dry[i] * mInputGain.next();
    mOut[static_cast<std::size_t>(i)] = mOutputGain.next();
    mNorm[static_cast<std::size_t>(i)] = mNormAmount.next();
    mMixBuf[static_cast<std::size_t>(i)] = mMix.next();
  }

  // 3. Render live (and outgoing) capture.
  renderSlot(mLive, dry, driven, mScratch.data(), mOutA.data(), n);
  double* amp = mOutA.data();
  if (mFadeActive)
  {
    renderSlot(mFading, dry, driven, mScratch.data(), mOutB.data(), n);
    const double* oldOut = mOutB.data();
    for (int i = 0; i < n; ++i)
    {
      const double t = std::min(1.0, static_cast<double>(mFadePos) / static_cast<double>(mFadeLen));
      amp[i] = amp[i] * std::sin(t * kHalfPi) + oldOut[i] * std::cos(t * kHalfPi);
      if (mFadePos < mFadeLen)
        ++mFadePos;
    }
    if (mFadePos >= mFadeLen)
    {
      PreparedAmp* old = mFading;
      mFading = nullptr;
      mFadeActive = false;
      retire(old);
    }
  }

  // 4. Amp on/off (smoothed) against the dry signal.
  const double* mix = mMixBuf.data();
  for (int i = 0; i < n; ++i)
    io[i] = dry[i] + mix[i] * (amp[i] - dry[i]);
}

} // namespace ironclad
