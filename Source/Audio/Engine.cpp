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

  mInputGain.reset(mSpec.sampleRate, paramSpec(kParamInputGain).smoothingMs,
                   dbToGain(getParameter(kParamInputGain)));
  mOutputGain.reset(mSpec.sampleRate, paramSpec(kParamOutputGain).smoothingMs,
                    dbToGain(getParameter(kParamOutputGain)));
  pullAmpTargets();
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
}

std::string Engine::captureStatePath() const
{
  const CaptureStatus st = mLoader.status();
  return st.hasActive ? st.active.path : st.requestedPath;
}

void Engine::pullAmpTargets() noexcept
{
  mAmp.setTargets(getParameter(kParamAmpInputGain), getParameter(kParamAmpOutputGain),
                  getParameter(kParamAmpEnabled) >= 0.5, getParameter(kParamAmpNormalize) >= 0.5);
}

void Engine::pullParameterTargets() noexcept
{
  pullAmpTargets();
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

  // 3. Signal chain. Amp (NAM capture); cabinet and effects follow in later milestones.
  mAmp.process(mono, numFrames);

  // 4. Output gain (smoothed), then fan out to every output channel.
  for (int i = 0; i < numFrames; ++i)
    mono[i] *= mOutputGain.next();

  for (int c = 0; c < numOutputs; ++c)
  {
    double* out = outputs[c];
    if (out == nullptr)
      continue;
    std::copy(mono, mono + numFrames, out + offset);
  }
}

} // namespace ironclad
