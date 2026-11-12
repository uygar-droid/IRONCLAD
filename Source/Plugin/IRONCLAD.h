#pragma once

// iPlug2 adapter. Thin by design: it maps host parameters, audio buffers and state
// onto ironclad::Engine and builds the editor. No DSP lives here.

#include <cstdint>

#include "IPlug_include_in_plug_hdr.h"

#include "Audio/Engine.h"
#include "Effects/TapTempo.h"

using namespace iplug;
using namespace igraphics;

enum ECtrlTags
{
  kCtrlTagCaptureName = 0,
  kCtrlTagCaptureMeta,
  kCtrlTagCaptureStatus,
  kCtrlTagIrName0,
  kCtrlTagIrName1,
  kCtrlTagIrInfo0,
  kCtrlTagIrInfo1,
  kCtrlTagIrStatus0,
  kCtrlTagIrStatus1,
  kCtrlTagIrWave0,
  kCtrlTagIrWave1,
  kNumCtrlTags
};

class IRONCLAD final : public Plugin
{
public:
  IRONCLAD(const InstanceInfo& info);

  // State: "IRCL" + format version + parameters + capture path (+ IR paths from v2).
  // Older formats (M1: parameters only; v1: no IRs) are still read.
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

  void OnIdle() override;
  void OnUIOpen() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

  static constexpr int kStateVersion = 2;

private:
  void PushAllParametersToEngine();
  void RefreshCaptureDisplay(bool force);
  void RefreshIrDisplay(bool force);
  void BrowseForCapture();
  void BrowseForIr(int slot);
  void ShowPage(int page);
  void TapDelayTempo();
  int UnserializeLegacyParams(const IByteChunk& chunk, int startPos, int count);

  ironclad::Engine mEngine;
  std::uint64_t mShownStatusSerial = UINT64_MAX;
  std::uint64_t mShownIrSerial = UINT64_MAX;
  WDL_String mLastCaptureFile, mLastCaptureDir;
  WDL_String mLastIrFile, mLastIrDir;
  int mPage = 1; // 0 STOMP, 1 AMP, 2 CAB, 3 POST FX
  ironclad::TapTempo mTapTempo;
};
