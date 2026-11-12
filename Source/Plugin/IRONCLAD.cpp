#include "IRONCLAD.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "IControls.h"
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

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
const IColor kPanel(255, 30, 30, 33);

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

std::string IrMeta(const ironclad::IrInfo& info)
{
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s  /  %.1f kHz  /  %.0f ms%s", info.fileChannels >= 2 ? "Stereo" : "Mono",
                info.fileSampleRate / 1000.0, info.durationMs, info.truncated ? "  (truncated to 1 s)" : "");
  return buf;
}

#if IPLUG_EDITOR
/// Peak-envelope display of an impulse response (data comes from the IR loader).
class IrWaveformControl : public IControl
{
public:
  explicit IrWaveformControl(const IRECT& bounds)
  : IControl(bounds)
  {
    mIgnoreMouse = true;
  }

  void SetPoints(const std::vector<float>& points)
  {
    mPoints = points;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(IColor(255, 18, 18, 20), mRECT);
    const float mid = mRECT.MH();
    g.DrawLine(IColor(255, 60, 60, 64), mRECT.L, mid, mRECT.R, mid);
    if (mPoints.empty())
      return;
    const float w = mRECT.W() / static_cast<float>(mPoints.size());
    const float h = mRECT.H() * 0.45f;
    for (std::size_t i = 0; i < mPoints.size(); ++i)
    {
      const float x = mRECT.L + (static_cast<float>(i) + 0.5f) * w;
      const float a = mPoints[i] * h;
      g.DrawLine(kAmber, x, mid - a, x, mid + a);
    }
  }

private:
  std::vector<float> mPoints;
};
#endif
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
    else if (spec.logScale)
      GetParam(spec.index)->InitDouble(spec.name.data(), spec.defaultValue, spec.minValue, spec.maxValue, spec.step,
                                       UnitLabel(spec.unit), 0, "", IParam::ShapeExp());
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

    const IRECT bounds = pGraphics->GetBounds().GetPadded(-20.f);
    const IVStyle style = DEFAULT_STYLE.WithColor(kBG, IColor(255, 22, 22, 24))
                            .WithColor(kFG, IColor(255, 44, 44, 48))
                            .WithColor(kPR, IColor(255, 70, 70, 76))
                            .WithColor(kFR, IColor(255, 90, 90, 96))
                            .WithColor(kHL, IColor(60, 255, 176, 64))
                            .WithColor(kX1, kAmber)
                            .WithLabelText(IText(14.f, kTextHi))
                            .WithValueText(IText(13.f, kAmber));
    const IVStyle buttonStyle = style.WithLabelText(IText(15.f, kAmber));

    auto addKnob = [&](const IRECT& r, int param, const char* label) {
      pGraphics->AttachControl(new IVKnobControl(r, param, label, style));
    };
    auto addToggle = [&](const IRECT& r, int param, const char* label) {
      pGraphics->AttachControl(new IVToggleControl(r, param, label, style, "OFF", "ON"));
    };

    // ---- Header ----
    pGraphics->AttachControl(new ITextControl(bounds.GetFromTop(44.f), "IRONCLAD", IText(36.f, kTextHi)));

    // ---- AMP: capture panel ----
    const IRECT capture = IRECT(bounds.L, bounds.T + 50.f, bounds.R, bounds.T + 170.f);
    pGraphics->AttachControl(new IPanelControl(capture, kPanel));
    const IRECT capInner = capture.GetPadded(-14.f);
    const IRECT capButtons = capInner.GetFromLeft(180.f);
    pGraphics->AttachControl(new IVButtonControl(capButtons.GetFromTop(42.f), [this](IControl* pCaller) {
      SplashClickActionFunc(pCaller);
      BrowseForCapture();
    }, "LOAD CAPTURE...", buttonStyle, true, false));
    pGraphics->AttachControl(new IVButtonControl(capButtons.GetFromTop(42.f).GetVShifted(50.f), [this](IControl* pCaller) {
      SplashClickActionFunc(pCaller);
      mEngine.clearCapture();
    }, "CLEAR", style, true, false));
    const IRECT capInfo = capInner.GetReducedFromLeft(200.f);
    pGraphics->AttachControl(new ITextControl(capInfo.GetFromTop(36.f), "No capture loaded",
                                              IText(26.f, kTextHi, nullptr, EAlign::Near)), kCtrlTagCaptureName);
    pGraphics->AttachControl(new ITextControl(capInfo.GetFromTop(62.f).GetReducedFromTop(38.f), "",
                                              IText(15.f, kTextLo, nullptr, EAlign::Near)), kCtrlTagCaptureMeta);
    pGraphics->AttachControl(new ITextControl(capInfo.GetFromTop(88.f).GetReducedFromTop(64.f), "",
                                              IText(14.f, kAmber, nullptr, EAlign::Near)), kCtrlTagCaptureStatus);

    // ---- AMP: controls row ----
    const IRECT ampRow = IRECT(bounds.L, bounds.T + 176.f, bounds.R, bounds.T + 316.f);
    addKnob(ampRow.GetGridCell(0, 1, 6).GetPadded(-6.f), ironclad::kParamInputGain, "Input");
    addKnob(ampRow.GetGridCell(1, 1, 6).GetPadded(-6.f), ironclad::kParamAmpInputGain, "Amp Input");
    addKnob(ampRow.GetGridCell(2, 1, 6).GetPadded(-6.f), ironclad::kParamAmpOutputGain, "Amp Output");
    addKnob(ampRow.GetGridCell(3, 1, 6).GetPadded(-6.f), ironclad::kParamOutputGain, "Output");
    addToggle(ampRow.GetGridCell(4, 1, 6).GetCentredInside(100.f, 80.f), ironclad::kParamAmpEnabled, "Amp");
    addToggle(ampRow.GetGridCell(5, 1, 6).GetCentredInside(100.f, 80.f), ironclad::kParamAmpNormalize, "Normalize");

    // ---- CAB: two IR slots ----
    const IRECT cab = IRECT(bounds.L, bounds.T + 326.f, bounds.R, bounds.B - 150.f);
    for (int slot = 0; slot < 2; ++slot)
    {
      const IRECT panel = cab.GetGridCell(slot, 1, 2).GetPadded(-4.f);
      pGraphics->AttachControl(new IPanelControl(panel, kPanel));
      const IRECT in = panel.GetPadded(-12.f);
      pGraphics->AttachControl(new ITextControl(in.GetFromTop(22.f).GetFromLeft(60.f), slot == 0 ? "IR A" : "IR B",
                                                IText(18.f, kAmber, nullptr, EAlign::Near)));
      const IRECT btnRow = in.GetFromTop(32.f).GetReducedFromLeft(70.f);
      pGraphics->AttachControl(new IVButtonControl(btnRow.GetGridCell(0, 1, 2).GetPadded(-3.f), [this, slot](IControl* pCaller) {
        SplashClickActionFunc(pCaller);
        BrowseForIr(slot);
      }, "LOAD IR...", buttonStyle, true, false));
      pGraphics->AttachControl(new IVButtonControl(btnRow.GetGridCell(1, 1, 2).GetPadded(-3.f), [this, slot](IControl* pCaller) {
        SplashClickActionFunc(pCaller);
        mEngine.clearIr(slot);
      }, "CLEAR", style, true, false));

      pGraphics->AttachControl(new ITextControl(in.GetReducedFromTop(38.f).GetFromTop(24.f), "No IR loaded",
                                                IText(18.f, kTextHi, nullptr, EAlign::Near)),
                               slot == 0 ? kCtrlTagIrName0 : kCtrlTagIrName1);
      pGraphics->AttachControl(new ITextControl(in.GetReducedFromTop(62.f).GetFromTop(18.f), "",
                                                IText(13.f, kTextLo, nullptr, EAlign::Near)),
                               slot == 0 ? kCtrlTagIrInfo0 : kCtrlTagIrInfo1);
      pGraphics->AttachControl(new ITextControl(in.GetReducedFromTop(80.f).GetFromTop(18.f), "",
                                                IText(13.f, kAmber, nullptr, EAlign::Near)),
                               slot == 0 ? kCtrlTagIrStatus0 : kCtrlTagIrStatus1);
      pGraphics->AttachControl(new IrWaveformControl(in.GetReducedFromTop(102.f).GetFromTop(44.f)),
                               slot == 0 ? kCtrlTagIrWave0 : kCtrlTagIrWave1);

      const IRECT knobs = in.GetReducedFromTop(150.f);
      const int on = slot == 0 ? ironclad::kParamCabAEnabled : ironclad::kParamCabBEnabled;
      const int lvl = slot == 0 ? ironclad::kParamCabALevel : ironclad::kParamCabBLevel;
      const int pan = slot == 0 ? ironclad::kParamCabAPan : ironclad::kParamCabBPan;
      const int inv = slot == 0 ? ironclad::kParamCabAInvert : ironclad::kParamCabBInvert;
      addKnob(knobs.GetGridCell(0, 1, 4).GetPadded(-4.f), lvl, "Level");
      addKnob(knobs.GetGridCell(1, 1, 4).GetPadded(-4.f), pan, "Pan");
      addToggle(knobs.GetGridCell(2, 1, 4).GetCentredInside(80.f, 70.f), on, "On");
      addToggle(knobs.GetGridCell(3, 1, 4).GetCentredInside(80.f, 70.f), inv, "Invert");
    }

    // ---- CAB: global row ----
    const IRECT cabRow = IRECT(bounds.L, bounds.B - 144.f, bounds.R, bounds.B - 24.f);
    addKnob(cabRow.GetGridCell(0, 1, 5).GetPadded(-6.f), ironclad::kParamCabBlend, "A / B Blend");
    addKnob(cabRow.GetGridCell(1, 1, 5).GetPadded(-6.f), ironclad::kParamCabHighPass, "Cab HPF");
    addKnob(cabRow.GetGridCell(2, 1, 5).GetPadded(-6.f), ironclad::kParamCabLowPass, "Cab LPF");
    addToggle(cabRow.GetGridCell(3, 1, 5).GetCentredInside(100.f, 80.f), ironclad::kParamCabEnabled, "Cab");

    WDL_String buildInfo;
    GetBuildInfoStr(buildInfo, __DATE__, __TIME__);
    pGraphics->AttachControl(new ITextControl(bounds.GetFromBottom(18.f), buildInfo.Get(),
                                              IText(12.f, IColor(255, 110, 110, 110), nullptr, EAlign::Far)));
    mShownStatusSerial = UINT64_MAX;
    mShownIrSerial = UINT64_MAX;
    RefreshCaptureDisplay(true);
    RefreshIrDisplay(true);
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

void IRONCLAD::BrowseForIr(int slot)
{
#if IPLUG_EDITOR
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  WDL_String file(mLastIrFile.Get()), dir(mLastIrDir.Get());
  pGraphics->PromptForFile(file, dir, EFileAction::Open, "wav",
                           [this, slot](const WDL_String& fileName, const WDL_String& path) {
                             if (fileName.GetLength() == 0)
                               return;
                             mLastIrFile.Set(fileName.Get());
                             mLastIrDir.Set(path.Get());
                             mEngine.loadIr(slot, fileName.Get());
                             RefreshIrDisplay(true);
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

void IRONCLAD::RefreshIrDisplay(bool force)
{
#if IPLUG_EDITOR
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  const ironclad::IrStatus st = mEngine.irStatus();
  if (!force && st.serial == mShownIrSerial)
    return;
  mShownIrSerial = st.serial;

  using State = ironclad::IrSlotStatus::State;
  for (int slot = 0; slot < 2; ++slot)
  {
    const auto& s = st.slot[slot];
    const std::string name = s.hasActive ? s.active.displayName : "No IR loaded";
    const std::string meta = s.hasActive ? IrMeta(s.active) : "";
    std::string status;
    IColor color = kAmber;
    switch (s.state)
    {
    case State::Empty: status = "Load a cabinet impulse response (.wav)."; break;
    case State::Loading: status = "Loading..."; break;
    case State::Ready: status = ""; break;
    case State::Error:
      status = s.errorMessage + (s.hasActive ? "  (previous IR still active)" : "");
      color = kErrorRed;
      break;
    }
    if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(slot == 0 ? kCtrlTagIrName0 : kCtrlTagIrName1)))
      c->SetStr(name.c_str());
    if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(slot == 0 ? kCtrlTagIrInfo0 : kCtrlTagIrInfo1)))
      c->SetStr(meta.c_str());
    if (auto* c = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(slot == 0 ? kCtrlTagIrStatus0 : kCtrlTagIrStatus1)))
    {
      c->SetText(c->GetText().WithFGColor(color));
      c->SetStr(status.c_str());
    }
    if (auto* w = dynamic_cast<IrWaveformControl*>(pGraphics->GetControlWithTag(slot == 0 ? kCtrlTagIrWave0 : kCtrlTagIrWave1)))
      w->SetPoints(s.hasActive ? s.active.thumbnail : std::vector<float>{});
  }
  pGraphics->SetAllControlsDirty();
#endif
}

void IRONCLAD::OnUIOpen()
{
  Plugin::OnUIOpen();
  RefreshCaptureDisplay(true);
  RefreshIrDisplay(true);
}

void IRONCLAD::OnIdle()
{
  // Main thread. Latency changes when a capture needs resampling at this host rate.
  const int latency = mEngine.latencySamples();
  if (latency != GetLatency())
    SetLatency(latency);
  RefreshCaptureDisplay(false);
  RefreshIrDisplay(false);
}

bool IRONCLAD::SerializeState(IByteChunk& chunk) const
{
  chunk.PutStr(kStateMagic);
  const int version = kStateVersion;
  chunk.Put(&version);
  const int numParams = NParams(); // stored from v2 so later versions can add parameters
  chunk.Put(&numParams);
  if (!SerializeParams(chunk))
    return false;
  chunk.PutStr(mEngine.captureStatePath().c_str());
  chunk.PutStr(mEngine.irStatePath(0).c_str());
  chunk.PutStr(mEngine.irStatePath(1).c_str());
  return true;
}

int IRONCLAD::UnserializeLegacyParams(const IByteChunk& chunk, int startPos, int count)
{
  // Reads `count` parameter values (doubles, index order). count < 0: read until the
  // chunk ends (Milestone-1 states held nothing else). Parameters the state does not
  // contain keep their defaults; values for parameters this build lacks are skipped.
  int pos = startPos;
  for (int i = 0; count < 0 || i < count; ++i)
  {
    double v = 0.0;
    const int next = chunk.Get(&v, pos);
    if (next < 0)
      break;
    if (i < NParams())
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
    pos = UnserializeLegacyParams(chunk, startPos, -1);
    PushAllParametersToEngine();
    return pos;
  }

  int version = 0;
  pos = chunk.Get(&version, pos);
  if (pos < 0 || version < 1)
    return -1;

  // v1 stored exactly 6 parameters (M2); from v2 the count precedes the values.
  int numParams = 6;
  if (version >= 2)
  {
    pos = chunk.Get(&numParams, pos);
    if (pos < 0 || numParams < 0 || numParams > 100000)
      return -1;
  }
  pos = UnserializeLegacyParams(chunk, pos, numParams);
  if (pos < 0)
    return -1;

  WDL_String capturePath, irPath[2];
  int next = chunk.GetStr(capturePath, pos);
  if (next >= 0)
    pos = next;
  if (version >= 2)
    for (auto& p : irPath)
    {
      next = chunk.GetStr(p, pos);
      if (next >= 0)
        pos = next;
    }

  PushAllParametersToEngine();
  if (capturePath.GetLength() > 0)
    mEngine.loadCapture(capturePath.Get());
  else
    mEngine.clearCapture();
  for (int slot = 0; slot < 2; ++slot)
  {
    if (irPath[slot].GetLength() > 0)
      mEngine.loadIr(slot, irPath[slot].Get());
    else
      mEngine.clearIr(slot);
  }
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
