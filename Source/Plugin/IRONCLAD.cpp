#include "IRONCLAD.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "IControls.h"
#endif

#include <type_traits>

#include "Parameters/Parameters.h"

static_assert(std::is_same_v<iplug::sample, double>, "IRONCLAD engine processes double-precision buffers");

namespace
{
const char* UnitLabel(ironclad::ParamUnit unit)
{
  switch (unit)
  {
  case ironclad::ParamUnit::Decibels: return "dB";
  case ironclad::ParamUnit::Percent: return "%";
  case ironclad::ParamUnit::Milliseconds: return "ms";
  case ironclad::ParamUnit::Hertz: return "Hz";
  default: return "";
  }
}

constexpr int kNumFactoryPresets = 1;
} // namespace

IRONCLAD::IRONCLAD(const InstanceInfo& info)
: Plugin(info, MakeConfig(ironclad::kNumParams, kNumFactoryPresets))
{
  // Host parameters are generated from the engine's parameter table, so indices
  // and ranges can never drift apart.
  for (const auto& spec : ironclad::kParamSpecs)
  {
    // string_views in kParamSpecs refer to string literals, so data() is NUL-terminated.
    GetParam(spec.index)->InitDouble(spec.name.data(), spec.defaultValue, spec.minValue, spec.maxValue, spec.step,
                                     UnitLabel(spec.unit));
  }

  MakeDefaultPreset("Default", kNumFactoryPresets);
  PushAllParametersToEngine();

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // TEMPORARY Milestone-1 editor: proves the GUI pipeline and parameter sync.
    // It is replaced by the photorealistic IRONCLAD interface in Milestone 6.
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(IColor(255, 22, 22, 24));
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    const IRECT bounds = pGraphics->GetBounds();
    const IVStyle style = DEFAULT_STYLE.WithColor(kFG, IColor(255, 40, 40, 44))
                            .WithColor(kPR, IColor(255, 255, 176, 64))
                            .WithColor(kFR, IColor(255, 90, 90, 96))
                            .WithLabelText(IText(18.f, IColor(255, 220, 220, 220)))
                            .WithValueText(IText(15.f, IColor(255, 255, 176, 64)));

    pGraphics->AttachControl(new ITextControl(bounds.GetFromTop(90.f), "IRONCLAD",
                                              IText(48.f, IColor(255, 230, 230, 230))));

    const IRECT knobs = bounds.GetCentredInside(420.f, 220.f);
    pGraphics->AttachControl(new IVKnobControl(knobs.GetGridCell(0, 1, 2), ironclad::kParamInputGain, ironclad::paramSpec(ironclad::kParamInputGain).name.data(), style));
    pGraphics->AttachControl(new IVKnobControl(knobs.GetGridCell(1, 1, 2), ironclad::kParamOutputGain, ironclad::paramSpec(ironclad::kParamOutputGain).name.data(), style));

    WDL_String buildInfo;
    GetBuildInfoStr(buildInfo, __DATE__, __TIME__);
    pGraphics->AttachControl(new ITextControl(bounds.GetFromBottom(28.f).GetPadded(-10.f), buildInfo.Get(),
                                              IText(13.f, IColor(255, 120, 120, 120), nullptr, EAlign::Far)));
  };
#endif
}

void IRONCLAD::PushAllParametersToEngine()
{
  for (int i = 0; i < ironclad::kNumParams; ++i)
    mEngine.setParameter(i, GetParam(i)->Value());
}

#if IPLUG_DSP
void IRONCLAD::OnReset()
{
  // Called by the host outside the audio callback when sample rate / block size change.
  ironclad::ProcessSpec spec;
  spec.sampleRate = GetSampleRate();
  spec.maxBlockSize = GetBlockSize();
  spec.numInputs = MaxNChannels(ERoute::kInput);
  spec.numOutputs = MaxNChannels(ERoute::kOutput);
  PushAllParametersToEngine();
  mEngine.prepare(spec);
  SetLatency(mEngine.latencySamples());
}

void IRONCLAD::OnParamChange(int paramIdx)
{
  // May be called on the audio thread; Engine::setParameter is a lock-free atomic store.
  mEngine.setParameter(paramIdx, GetParam(paramIdx)->Value());
}

void IRONCLAD::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  mEngine.process(inputs, NInChansConnected(), outputs, NOutChansConnected(), nFrames);
}
#endif
