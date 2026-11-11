#include "IRONCLAD.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "IControls.h"
#endif

#include <cstdio>
#include <cstring>
#include <string>
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
constexpr char kStateMagic[] = "IRCL";

const IColor kAmber(255, 255, 176, 64);
const IColor kTextHi(255, 230, 230, 230);
const IColor kTextLo(255, 150, 150, 156);
const IColor kErrorRed(255, 255, 92, 80);

std::string JoinMeta(const ironclad::CaptureStatus& st)
{
  const auto& a = st.active;
  std::string out;
  auto add = [&out](const std::string& s) {
    if (s.empty()) return;
    if (!out.empty()) out += "  /  ";
    out += s;
  };
  add(a.architecture);
  if (a.sampleRate > 0.0)
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f kHz", a.sampleRate / 1000.0);
    add(buf);
  }
  std::string gear = a.gearMake;
  if (!a.gearModel.empty()) gear += (gear.empty() ? "" : " ") + a.gearModel;
  add(gear);
  add(a.toneType);
  if (!a.modeledBy.empty()) add("by " + a.modeledBy);
  if (st.resampling) add("resampled to host rate");
  return out;
}
} // namespace

IRONCLAD::IRONCLAD(const InstanceInfo& info)
: Plugin(info, MakeConfig(ironclad::kNumParams, kNumFactoryPresets))
{
  // Host parameters are generated from the engine's parameter table, so indices
  // and ranges can never drift apart.
  for (const auto& spec : ironclad::kParamSpecs)
  {
    // string_views in kParamSpecs refer to string literals, so data() is NUL-terminated.
    if (spec.unit == ironclad::ParamUnit::Boolean)
      GetParam(spec.index)->InitBool(spec.name.data(), spec.defaultValue >= 0.5);
    else
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
    // TEMPORARY functional editor (Milestones 1-5). Every control here is real and
    // host-synchronised; the photorealistic IRONCLAD interface replaces it in Milestone 6.
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(IColor(255, 22, 22, 24));
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    const IRECT bounds = pGraphics->GetBounds().GetPadded(-24.f);
    const IVStyle style = DEFAULT_STYLE.WithColor(kBG, IColor(255, 22, 22, 24))
                            .WithColor(kFG, IColor(255, 44, 44, 48))
                            .WithColor(kPR, IColor(255, 70, 70, 76))
                            .WithColor(kFR, IColor(255, 90, 90, 96))
                            .WithColor(kHL, IColor(60, 255, 176, 64))
                            .WithColor(kX1, kAmber)
                            .WithLabelText(IText(16.f, kTextHi))
                            .WithValueText(IText(14.f, kAmber));

    // Header
    pGraphics->AttachControl(new ITextControl(bounds.GetFromTop(56.f), "IRONCLAD", IText(44.f, kTextHi)));

    // Capture panel
    const IRECT capture = bounds.GetFromTop(250.f).GetReducedFromTop(76.f);
    pGraphics->AttachControl(new IPanelControl(capture, IColor(255, 30, 30, 33)));
    const IRECT captureInner = capture.GetPadded(-18.f);
    const IRECT buttons = captureInner.GetFromLeft(200.f);
    pGraphics->AttachControl(new IVButtonControl(buttons.GetFromTop(52.f), [this](IControl* pCaller) {
      SplashClickActionFunc(pCaller);
      BrowseForCapture();
    }, "LOAD CAPTURE...", style.WithLabelText(IText(17.f, kAmber)), true, false));
    pGraphics->AttachControl(new IVButtonControl(buttons.GetFromTop(52.f).GetVShifted(64.f), [this](IControl* pCaller) {
      SplashClickActionFunc(pCaller);
      mEngine.clearCapture();
    }, "CLEAR", style, true, false));

    const IRECT info = captureInner.GetReducedFromLeft(224.f);
    pGraphics->AttachControl(new ITextControl(info.GetFromTop(44.f), "No capture loaded",
                                              IText(30.f, kTextHi, nullptr, EAlign::Near)),
                             kCtrlTagCaptureName);
    pGraphics->AttachControl(new ITextControl(info.GetFromTop(80.f).GetReducedFromTop(48.f), "",
                                              IText(16.f, kTextLo, nullptr, EAlign::Near)),
                             kCtrlTagCaptureMeta);
    pGraphics->AttachControl(new ITextControl(info.GetFromTop(120.f).GetReducedFromTop(88.f), "",
                                              IText(15.f, kAmber, nullptr, EAlign::Near)),
                             kCtrlTagCaptureStatus);

    // Controls row
    const IRECT row = bounds.GetReducedFromTop(270.f).GetFromTop(230.f);
    const int cols = 6;
    pGraphics->AttachControl(new IVKnobControl(row.GetGridCell(0, 1, cols).GetPadded(-10.f), ironclad::kParamInputGain,
                                               "Input", style));
    pGraphics->AttachControl(new IVKnobControl(row.GetGridCell(1, 1, cols).GetPadded(-10.f), ironclad::kParamAmpInputGain,
                                               "Amp Input", style));
    pGraphics->AttachControl(new IVKnobControl(row.GetGridCell(2, 1, cols).GetPadded(-10.f), ironclad::kParamAmpOutputGain,
                                               "Amp Output", style));
    pGraphics->AttachControl(new IVKnobControl(row.GetGridCell(3, 1, cols).GetPadded(-10.f), ironclad::kParamOutputGain,
                                               "Output", style));
    pGraphics->AttachControl(new IVToggleControl(row.GetGridCell(4, 1, cols).GetCentredInside(110.f, 90.f),
                                                 ironclad::kParamAmpEnabled, "Amp", style, "OFF", "ON"));
    pGraphics->AttachControl(new IVToggleControl(row.GetGridCell(5, 1, cols).GetCentredInside(110.f, 90.f),
                                                 ironclad::kParamAmpNormalize, "Normalize", style, "OFF", "ON"));

    WDL_String buildInfo;
    GetBuildInfoStr(buildInfo, __DATE__, __TIME__);
    pGraphics->AttachControl(new ITextControl(bounds.GetFromBottom(22.f), buildInfo.Get(),
                                              IText(13.f, IColor(255, 110, 110, 110), nullptr, EAlign::Far)));
    mShownStatusSerial = UINT64_MAX;
    RefreshCaptureDisplay(true);
  };
#endif
}

void IRONCLAD::PushAllParametersToEngine()
{
  for (int i = 0; i < ironclad::kNumParams; ++i)
    mEngine.setParameter(i, GetParam(i)->Value());
}

void IRONCLAD::BrowseForCapture()
{
#if IPLUG_EDITOR
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  WDL_String file(mLastCaptureFile.Get()), dir(mLastCaptureDir.Get());
  pGraphics->PromptForFile(file, dir, EFileAction::Open, "nam",
                           [this](const WDL_String& fileName, const WDL_String& path) {
                             if (fileName.GetLength() == 0)
                               return; // cancelled
                             mLastCaptureFile.Set(fileName.Get());
                             mLastCaptureDir.Set(path.Get());
                             mEngine.loadCapture(fileName.Get());
                             RefreshCaptureDisplay(true);
                           });
#endif
}

void IRONCLAD::RefreshCaptureDisplay(bool force)
{
#if IPLUG_EDITOR
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  const ironclad::CaptureStatus st = mEngine.captureStatus();
  if (!force && st.serial == mShownStatusSerial)
    return;
  mShownStatusSerial = st.serial;

  using State = ironclad::CaptureStatus::State;
  const std::string name = st.hasActive ? st.active.displayName : "No capture loaded";
  const std::string meta = st.hasActive ? JoinMeta(st) : "";
  std::string status;
  IColor statusColor = kAmber;
  switch (st.state)
  {
  case State::Empty: status = "Click LOAD CAPTURE to choose a Neural Amp Modeler (.nam) file."; break;
  case State::Loading: status = "Loading..."; break;
  case State::Ready: status = ""; break;
  case State::Error:
    status = st.errorMessage + (st.hasActive ? "  (previous capture still active)" : "");
    statusColor = kErrorRed;
    break;
  }

  if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(kCtrlTagCaptureName)))
    c->SetStr(name.c_str());
  if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(kCtrlTagCaptureMeta)))
    c->SetStr(meta.c_str());
  if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(kCtrlTagCaptureStatus)))
  {
    c->SetText(c->GetText().WithFGColor(statusColor));
    c->SetStr(status.c_str());
  }
  pGraphics->SetAllControlsDirty();
#endif
}

void IRONCLAD::OnUIOpen()
{
  Plugin::OnUIOpen();
  RefreshCaptureDisplay(true);
}

void IRONCLAD::OnIdle()
{
  // Main thread. Latency changes when a capture needs resampling at this host rate.
  const int latency = mEngine.latencySamples();
  if (latency != GetLatency())
    SetLatency(latency);
  RefreshCaptureDisplay(false);
}

bool IRONCLAD::SerializeState(IByteChunk& chunk) const
{
  chunk.PutStr(kStateMagic);
  const int version = kStateVersion;
  chunk.Put(&version);
  if (!SerializeParams(chunk))
    return false;
  chunk.PutStr(mEngine.captureStatePath().c_str());
  return true;
}

int IRONCLAD::UnserializeLegacyParams(const IByteChunk& chunk, int startPos)
{
  // Milestone-1 builds stored only the parameter values (2 doubles). Read what is there
  // and leave newer parameters at their defaults.
  int pos = startPos;
  for (int i = 0; i < NParams(); ++i)
  {
    double v = 0.0;
    const int next = chunk.Get(&v, pos);
    if (next < 0)
      break;
    GetParam(i)->Set(v);
    pos = next;
  }
  return pos;
}

int IRONCLAD::UnserializeState(const IByteChunk& chunk, int startPos)
{
  WDL_String magic;
  int pos = chunk.GetStr(magic, startPos);
  if (pos < 0 || std::strcmp(magic.Get(), kStateMagic) != 0)
  {
    pos = UnserializeLegacyParams(chunk, startPos);
    PushAllParametersToEngine();
    return pos;
  }

  int version = 0;
  pos = chunk.Get(&version, pos);
  if (pos < 0 || version < 1)
    return -1;

  pos = UnserializeParams(chunk, pos);
  if (pos < 0)
    return -1;

  WDL_String capturePath;
  const int afterPath = chunk.GetStr(capturePath, pos);
  if (afterPath >= 0)
    pos = afterPath;

  PushAllParametersToEngine();
  if (capturePath.GetLength() > 0)
    mEngine.loadCapture(capturePath.Get());
  else
    mEngine.clearCapture();
  return pos;
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
