#pragma once

// iPlug2 adapter. Thin by design: it maps host parameters, audio buffers and state
// onto ironclad::Engine and builds the editor. No DSP lives here.

#include <cstdint>

#include "IPlug_include_in_plug_hdr.h"

#include "Audio/Engine.h"

using namespace iplug;
using namespace igraphics;

enum ECtrlTags
{
  kCtrlTagCaptureName = 0,
  kCtrlTagCaptureMeta,
  kCtrlTagCaptureStatus,
  kNumCtrlTags
};

class IRONCLAD final : public Plugin
{
public:
  IRONCLAD(const InstanceInfo& info);

  // State: "IRCL" + format version + parameters + capture path. Older (M1) projects
  // stored parameters only and are still read.
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

  void OnIdle() override;
  void OnUIOpen() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

  static constexpr int kStateVersion = 1;

private:
  void PushAllParametersToEngine();
  void RefreshCaptureDisplay(bool force);
  void BrowseForCapture();
  int UnserializeLegacyParams(const IByteChunk& chunk, int startPos);

  ironclad::Engine mEngine;
  std::uint64_t mShownStatusSerial = UINT64_MAX;
  WDL_String mLastCaptureFile, mLastCaptureDir;
};
