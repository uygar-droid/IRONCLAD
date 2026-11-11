#pragma once

// iPlug2 adapter. Thin by design: it maps host parameters and audio buffers onto
// ironclad::Engine and builds the editor. No DSP lives here.

#include "IPlug_include_in_plug_hdr.h"

#include "Audio/Engine.h"

using namespace iplug;
using namespace igraphics;

class IRONCLAD final : public Plugin
{
public:
  IRONCLAD(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

private:
  void PushAllParametersToEngine();

  ironclad::Engine mEngine;
};
