#include "Audio/Engine.h"

#include <algorithm>
#include <cmath>

#include "Utilities/Denormals.h"

namespace ironclad
{

double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }

Engine::Engine()
{
  for (int i = 0; i < kNumParams; ++i)
    mParamValues[static_cast<std::size_t>(i)].store(paramSpec(i).defaultValue, std::memory_order_relaxed);
}

void Engine::prepare(const ProcessSpec& spec)
{
  mSpec = spec;
  mSpec.maxBlockSize = std::max(1, spec.maxBlockSize);
  mSpec.sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;

  mMono.assign(static_cast<std::size_t>(mSpec.maxBlockSize), 0.0);
  mLeft.assign(static_cast<std::size_t>(mSpec.maxBlockSize), 0.0);
  mRight.assign(static_cast<std::size_t>(mSpec.maxBlockSize), 0.0);

  mInputGain.reset(mSpec.sampleRate, paramSpec(kParamInputGain).smoothingMs,
                   dbToGain(getParameter(kParamInputGain)));
  mOutputGain.reset(mSpec.sampleRate, paramSpec(kParamOutputGain).smoothingMs,
                    dbToGain(getParameter(kParamOutputGain)));
  pullAmpTargets();
  pullCabTargets();
  pullEffectTargets();
  {
    const double sr = mSpec.sampleRate;
    const int mb = mSpec.maxBlockSize;
    mGate.prepare(sr, mb);
    mComp.prepare(sr, mb);
    mBoost.prepare(sr, mb);
    mOverdrive.prepare(sr, mb);
    mDistortion.prepare(sr, mb);
    mFuzz.prepare(sr, mb);
    mWah.prepare(sr, mb);
    mEq.prepare(sr, mb);
    mChorus.prepare(sr, mb);
    mDelay.prepare(sr, mb);
    mReverb.prepare(sr, mb);
    static constexpr int kEnableParam[kNumEffectSlots] = {
      kParamGateEnabled, kParamCompEnabled, kParamBoostEnabled, kParamOdEnabled, kParamDistEnabled, kParamFuzzEnabled,
      kParamWahEnabled, kParamEqEnabled, kParamChorusEnabled, kParamDelayEnabled, kParamReverbEnabled};
    for (int k = 0; k < kNumEffectSlots; ++k)
      mBypass[k].prepare(sr, mb, getParameter(kEnableParam[k]) >= 0.5);
  }
  mIrLoader.reconfigure(mSpec.sampleRate,
                        [this](std::uint64_t generation) { mCab.prepare(mSpec.sampleRate, mSpec.maxBlockSize, generation); });
  mLoader.reconfigure(mSpec.sampleRate, mSpec.maxBlockSize,
                      [this](std::uint64_t generation) { mAmp.prepare(mSpec.sampleRate, mSpec.maxBlockSize, generation); });
  mPrepared = true;
}

void Engine::setParameter(int index, double plainValue) noexcept
{
  if (index < 0 || index >= kNumParams)
    return;
  mParamValues[static_cast<std::size_t>(index)].store(clampToRange(paramSpec(index), plainValue),
                                                       std::memory_order_relaxed);
}

double Engine::getParameter(int index) const noexcept
{
  if (index < 0 || index >= kNumParams)
    return 0.0;
  return mParamValues[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
}

void Engine::reset() noexcept
{
  mInputGain.setImmediate(dbToGain(getParameter(kParamInputGain)));
  mOutputGain.setImmediate(dbToGain(getParameter(kParamOutputGain)));
  std::fill(mMono.begin(), mMono.end(), 0.0);
  std::fill(mLeft.begin(), mLeft.end(), 0.0);
  std::fill(mRight.begin(), mRight.end(), 0.0);
}

std::string Engine::captureStatePath() const
{
  const CaptureStatus st = mLoader.status();
  return st.hasActive ? st.active.path : st.requestedPath;
}

std::string Engine::irStatePath(int slot) const
{
  if (slot < 0 || slot >= CabStage::kSlots)
    return {};
  const IrStatus st = mIrLoader.status();
  const auto& s = st.slot[slot];
  return s.hasActive ? s.active.path : s.requestedPath;
}

void Engine::pullCabTargets() noexcept
{
  CabTargets t;
  t.enabled = getParameter(kParamCabEnabled) >= 0.5;
  t.slotEnabled[0] = getParameter(kParamCabAEnabled) >= 0.5;
  t.slotEnabled[1] = getParameter(kParamCabBEnabled) >= 0.5;
  t.levelDb[0] = getParameter(kParamCabALevel);
  t.levelDb[1] = getParameter(kParamCabBLevel);
  t.pan[0] = getParameter(kParamCabAPan) / 100.0;
  t.pan[1] = getParameter(kParamCabBPan) / 100.0;
  t.invert[0] = getParameter(kParamCabAInvert) >= 0.5;
  t.invert[1] = getParameter(kParamCabBInvert) >= 0.5;
  t.blend = getParameter(kParamCabBlend) / 100.0;
  t.highPassHz = getParameter(kParamCabHighPass);
  t.lowPassHz = getParameter(kParamCabLowPass);
  mCab.setTargets(t);
}

void Engine::pullEffectTargets() noexcept
{
  auto on = [this](int p) { return getParameter(p) >= 0.5; };
  auto pct = [this](int p) { return getParameter(p) / 100.0; };

  mBypass[kSlotGate].setOn(on(kParamGateEnabled));
  mGate.setThresholdDb(getParameter(kParamGateThreshold));
  mGate.setRelease(getParameter(kParamGateRelease));

  mBypass[kSlotComp].setOn(on(kParamCompEnabled));
  mComp.setThresholdDb(getParameter(kParamCompThreshold));
  mComp.setRatio(getParameter(kParamCompRatio));
  mComp.setAttack(getParameter(kParamCompAttack));
  mComp.setRelease(getParameter(kParamCompRelease));
  mComp.setMakeupDb(getParameter(kParamCompMakeup));
  mComp.setMix(pct(kParamCompMix));

  mBypass[kSlotBoost].setOn(on(kParamBoostEnabled));
  mBoost.setGainDb(getParameter(kParamBoostGain));
  mBoost.setBright(on(kParamBoostBright));

  mBypass[kSlotOverdrive].setOn(on(kParamOdEnabled));
  mOverdrive.setDrive(pct(kParamOdDrive));
  mOverdrive.setTone(pct(kParamOdTone));
  mOverdrive.setLevelDb(getParameter(kParamOdLevel));

  mBypass[kSlotDistortion].setOn(on(kParamDistEnabled));
  mDistortion.setDrive(pct(kParamDistDrive));
  mDistortion.setTone(pct(kParamDistTone));
  mDistortion.setLevelDb(getParameter(kParamDistLevel));

  mBypass[kSlotFuzz].setOn(on(kParamFuzzEnabled));
  mFuzz.setDrive(pct(kParamFuzzFuzz));
  mFuzz.setTone(pct(kParamFuzzTone));
  mFuzz.setLevelDb(getParameter(kParamFuzzLevel));

  mBypass[kSlotWah].setOn(on(kParamWahEnabled));
  mWah.setPosition(pct(kParamWahPosition));
  mWah.setResonance(pct(kParamWahResonance));
  mWah.setMix(pct(kParamWahMix));

  mBypass[kSlotEq].setOn(on(kParamEqEnabled));
  mEq.setLowDb(getParameter(kParamEqLow));
  mEq.setMidDb(getParameter(kParamEqMid));
  mEq.setMidHz(getParameter(kParamEqMidFreq));
  mEq.setHighDb(getParameter(kParamEqHigh));
  mEq.setLevelDb(getParameter(kParamEqLevel));

  mBypass[kSlotChorus].setOn(on(kParamChorusEnabled));
  mChorus.setRateHz(getParameter(kParamChorusRate));
  mChorus.setDepth(pct(kParamChorusDepth));
  mChorus.setMix(pct(kParamChorusMix));

  mBypass[kSlotDelay].setOn(on(kParamDelayEnabled));
  mDelay.setType(static_cast<Delay::Type>(static_cast<int>(std::lround(getParameter(kParamDelayType)))));
  const double delayMs = on(kParamDelaySync)
                           ? Delay::syncedMs(tempo(), static_cast<int>(std::lround(getParameter(kParamDelayDivision))))
                           : getParameter(kParamDelayTime);
  mDelay.setTimeMs(delayMs);
  mDelay.setFeedback(pct(kParamDelayFeedback));
  mDelay.setMix(pct(kParamDelayMix));
  mDelay.setFilters(getParameter(kParamDelayLowCut), getParameter(kParamDelayHighCut));

  mBypass[kSlotReverb].setOn(on(kParamReverbEnabled));
  mReverb.setType(static_cast<Reverb::Type>(static_cast<int>(std::lround(getParameter(kParamReverbType)))));
  mReverb.setDecaySeconds(getParameter(kParamReverbDecay));
  mReverb.setPreDelayMs(getParameter(kParamReverbPreDelay));
  mReverb.setDamping(pct(kParamReverbDamping));
  mReverb.setMix(pct(kParamReverbMix));
}

void Engine::pullAmpTargets() noexcept
{
  mAmp.setTargets(getParameter(kParamAmpInputGain), getParameter(kParamAmpOutputGain),
                  getParameter(kParamAmpEnabled) >= 0.5, getParameter(kParamAmpNormalize) >= 0.5);
}

void Engine::pullParameterTargets() noexcept
{
  pullAmpTargets();
  pullCabTargets();
  pullEffectTargets();
  // dB -> linear conversion happens once per block per parameter, not per sample.
  mInputGain.setTarget(dbToGain(getParameter(kParamInputGain)));
  mOutputGain.setTarget(dbToGain(getParameter(kParamOutputGain)));
}

void Engine::process(const double* const* inputs, int numInputs, double* const* outputs, int numOutputs,
                     int numFrames) noexcept
{
  if (numFrames <= 0 || outputs == nullptr || numOutputs <= 0)
    return;

  if (!mPrepared)
  {
    for (int c = 0; c < numOutputs; ++c)
      if (outputs[c] != nullptr)
        std::fill(outputs[c], outputs[c] + numFrames, 0.0);
    return;
  }

  ScopedNoDenormals noDenormals;

  int offset = 0;
  while (offset < numFrames)
  {
    const int chunk = std::min(mSpec.maxBlockSize, numFrames - offset);
    processChunk(inputs, numInputs, outputs, numOutputs, offset, chunk);
    offset += chunk;
  }
}

void Engine::processChunk(const double* const* inputs, int numInputs, double* const* outputs, int numOutputs,
                          int offset, int numFrames) noexcept
{
  pullParameterTargets();

  double* mono = mMono.data();

  // 1. Collapse input to mono. In a DAW a stereo track carrying a guitar is usually
  //    dual-mono, so the channels are averaged to avoid a +6 dB jump.
  int connectedInputs = 0;
  if (inputs != nullptr)
    for (int c = 0; c < numInputs; ++c)
      if (inputs[c] != nullptr)
        ++connectedInputs;

  if (connectedInputs == 0)
  {
    std::fill(mono, mono + numFrames, 0.0);
  }
  else
  {
    const double norm = 1.0 / static_cast<double>(connectedInputs);
    bool first = true;
    for (int c = 0; c < numInputs; ++c)
    {
      const double* in = inputs[c];
      if (in == nullptr)
        continue;
      in += offset;
      if (first)
      {
        for (int i = 0; i < numFrames; ++i)
          mono[i] = in[i] * norm;
        first = false;
      }
      else
      {
        for (int i = 0; i < numFrames; ++i)
          mono[i] += in[i] * norm;
      }
    }
  }

  // 2. Input gain (smoothed).
  for (int i = 0; i < numFrames; ++i)
    mono[i] *= mInputGain.next();

  // 3. Pre-amp effects (mono): gate -> comp -> boost -> overdrive -> distortion -> fuzz -> wah.
  const int n = numFrames;
  mBypass[kSlotGate].run(mono, nullptr, n, [&](int k) { mGate.process(mono, k); }, [&] { mGate.reset(); });
  mBypass[kSlotComp].run(mono, nullptr, n, [&](int k) { mComp.process(mono, k); }, [&] { mComp.reset(); });
  mBypass[kSlotBoost].run(mono, nullptr, n, [&](int k) { mBoost.process(mono, k); }, [&] { mBoost.reset(); });
  mBypass[kSlotOverdrive].run(mono, nullptr, n, [&](int k) { mOverdrive.process(mono, k); }, [&] { mOverdrive.reset(); });
  mBypass[kSlotDistortion].run(mono, nullptr, n, [&](int k) { mDistortion.process(mono, k); }, [&] { mDistortion.reset(); });
  mBypass[kSlotFuzz].run(mono, nullptr, n, [&](int k) { mFuzz.process(mono, k); }, [&] { mFuzz.reset(); });
  mBypass[kSlotWah].run(mono, nullptr, n, [&](int k) { mWah.process(mono, k); }, [&] { mWah.reset(); });

  // 4. Amp (NAM capture, mono) -> cabinet (IRs, mono in / stereo out).
  mAmp.process(mono, numFrames);
  double* left = mLeft.data();
  double* right = mRight.data();
  mCab.process(mono, left, right, numFrames);

  // 5. Post effects (stereo): EQ -> chorus -> delay -> reverb.
  mBypass[kSlotEq].run(left, right, n, [&](int k) { mEq.process(left, right, k); }, [&] { mEq.reset(); });
  mBypass[kSlotChorus].run(left, right, n, [&](int k) { mChorus.process(left, right, k); }, [&] { mChorus.reset(); });
  mBypass[kSlotDelay].run(left, right, n, [&](int k) { mDelay.process(left, right, k); }, [&] { mDelay.reset(); });
  mBypass[kSlotReverb].run(left, right, n, [&](int k) { mReverb.process(left, right, k); }, [&] { mReverb.reset(); });

  // 6. Output gain (smoothed), then map to the host's output channels.
  for (int i = 0; i < numFrames; ++i)
  {
    const double g = mOutputGain.next();
    left[i] *= g;
    right[i] *= g;
  }

  if (numOutputs == 1)
  {
    if (double* out = outputs[0])
      for (int i = 0; i < numFrames; ++i)
        out[offset + i] = 0.5 * (left[i] + right[i]);
    return;
  }
  for (int c = 0; c < numOutputs; ++c)
  {
    double* out = outputs[c];
    if (out == nullptr)
      continue;
    const double* src = (c % 2 == 0) ? left : right;
    std::copy(src, src + numFrames, out + offset);
  }
}

} // namespace ironclad
