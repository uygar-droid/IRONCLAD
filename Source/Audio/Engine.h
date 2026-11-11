#pragma once

// IRONCLAD audio engine. Framework-independent: no iPlug2 / OS headers here.
//
// Thread contract
//   prepare()       : non-real-time thread, audio stopped. May allocate.
//   setParameter()  : any thread, lock-free (atomic store).
//   process()       : audio thread only. Never allocates, locks, blocks or throws.

#include <array>
#include <atomic>
#include <vector>

#include "Audio/SmoothedValue.h"
#include "Parameters/Parameters.h"

namespace ironclad
{

struct ProcessSpec
{
  double sampleRate = 48000.0;
  int maxBlockSize = 512;
  int numInputs = 2;
  int numOutputs = 2;
};

class Engine
{
public:
  Engine();

  void prepare(const ProcessSpec& spec);
  bool isPrepared() const noexcept { return mPrepared; }
  const ProcessSpec& spec() const noexcept { return mSpec; }

  /// Plain (non-normalised) value; clamped to the parameter range.
  void setParameter(int index, double plainValue) noexcept;
  double getParameter(int index) const noexcept;

  /// Processes `numFrames` samples. `numFrames` may exceed maxBlockSize (it is split
  /// internally) and channel counts may differ from those given to prepare();
  /// inputs/outputs may alias (in-place processing is safe).
  void process(const double* const* inputs, int numInputs, double* const* outputs, int numOutputs,
               int numFrames) noexcept;

  /// Clears all DSP state without allocating (transport jumps, bypass toggles).
  void reset() noexcept;

  int latencySamples() const noexcept { return 0; }

private:
  void processChunk(const double* const* inputs, int numInputs, double* const* outputs, int numOutputs,
                    int offset, int numFrames) noexcept;
  void pullParameterTargets() noexcept;

  ProcessSpec mSpec{};
  bool mPrepared = false;

  std::array<std::atomic<double>, kNumParams> mParamValues;
  SmoothedValue mInputGain;  // linear
  SmoothedValue mOutputGain; // linear

  std::vector<double> mMono; // maxBlockSize, allocated in prepare()
};

double dbToGain(double db) noexcept;

} // namespace ironclad
