#pragma once

// IRONCLAD audio engine. Framework-independent: no iPlug2 / OS headers here.
//
// Thread contract
//   prepare()       : non-real-time thread, audio stopped. May allocate.
//   setParameter()  : any thread, lock-free (atomic store).
//   process()       : audio thread only. Never allocates, locks, blocks or throws.

#include <array>
#include <chrono>
#include <string>
#include <atomic>
#include <vector>

#include "Amp/AmpStage.h"
#include "Amp/CaptureLoader.h"
#include "Audio/SmoothedValue.h"
#include "Cabinet/CabStage.h"
#include "Cabinet/IrLoader.h"
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

  int latencySamples() const noexcept { return mAmp.latencySamples(); }

  // ---- Capture management (UI / host threads; never the audio thread) ----
  void loadCapture(const std::string& utf8Path) { mLoader.requestLoad(utf8Path); }
  void clearCapture() { mLoader.requestClear(); }
  CaptureStatus captureStatus() const { return mLoader.status(); }
  /// Path to store in presets / DAW state: the playing capture, or - if none could be
  /// loaded (e.g. file missing on this machine) - the one that was requested, so it is
  /// not silently dropped from the project.
  std::string captureStatePath() const;
  bool waitForLoader(int timeoutMs) const
  {
    return mLoader.waitUntilIdle(std::chrono::milliseconds(timeoutMs))
           && mIrLoader.waitUntilIdle(std::chrono::milliseconds(timeoutMs));
  }
  const AmpStage& ampStage() const noexcept { return mAmp; }

  // ---- Cabinet IR management (UI / host threads) ----
  void loadIr(int slot, const std::string& utf8Path) { mIrLoader.requestLoad(slot, utf8Path); }
  void clearIr(int slot) { mIrLoader.requestClear(slot); }
  IrStatus irStatus() const { return mIrLoader.status(); }
  std::string irStatePath(int slot) const;
  const CabStage& cabStage() const noexcept { return mCab; }

private:
  void processChunk(const double* const* inputs, int numInputs, double* const* outputs, int numOutputs,
                    int offset, int numFrames) noexcept;
  void pullParameterTargets() noexcept;
  void pullAmpTargets() noexcept;
  void pullCabTargets() noexcept;

  ProcessSpec mSpec{};
  bool mPrepared = false;

  std::array<std::atomic<double>, kNumParams> mParamValues;
  SmoothedValue mInputGain;  // linear
  SmoothedValue mOutputGain; // linear

  std::vector<double> mMono; // maxBlockSize, allocated in prepare()
  std::vector<double> mLeft, mRight;

  AmpStage mAmp;
  CaptureLoader mLoader{mAmp.readyQueue(), mAmp.retireQueue()}; // declared after mAmp: destroyed first
  CabStage mCab;
  IrLoader mIrLoader{mCab.readyQueue(), mCab.retireQueue()};     // declared after mCab: destroyed first
};

double dbToGain(double db) noexcept;

} // namespace ironclad
