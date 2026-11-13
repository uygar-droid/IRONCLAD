#include "IRONCLAD.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "IControls.h"
  #include "PluginControls.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "Parameters/Parameters.h"
#include "Presets/PresetLibrary.h"
#include "Presets/StateCodec.h"
#include "Utilities/Paths.h"

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
  case ironclad::ParamUnit::Seconds: return "s";
  case ironclad::ParamUnit::Ratio: return ":1";
  case ironclad::ParamUnit::Dbu: return "dBu";
  default: return "";
  }
}

constexpr int kNumFactoryPresets = 1; // iPlug2's own preset slot (unused; IRONCLAD has its own library)

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
    else if (spec.unit == ironclad::ParamUnit::Choice)
    {
      const int count = static_cast<int>(spec.maxValue) + 1;
      GetParam(spec.index)->InitEnum(spec.name.data(), static_cast<int>(spec.defaultValue), count);
      std::string_view rest = spec.choices;
      for (int k = 0; k < count && !rest.empty(); ++k)
      {
        const auto bar = rest.find('|');
        const std::string label(rest.substr(0, bar));
        GetParam(spec.index)->SetDisplayText(k, label.c_str());
        rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
      }
    }
    else if (spec.logScale)
      GetParam(spec.index)->InitDouble(spec.name.data(), spec.defaultValue, spec.minValue, spec.maxValue, spec.step,
                                       UnitLabel(spec.unit), 0, "", IParam::ShapeExp());
    else
      GetParam(spec.index)->InitDouble(spec.name.data(), spec.defaultValue, spec.minValue, spec.maxValue, spec.step,
                                       UnitLabel(spec.unit));
  }

  MakeDefaultPreset("Default", kNumFactoryPresets);
  PushAllParametersToEngine();
  for (int i = 0; i < ironclad::kNumParams; ++i)
    mCommitted[static_cast<std::size_t>(i)] = GetParam(i)->Value();

  // A new instance starts with the user's default MIDI map (MIDI > Save as default).
  try
  {
    const std::string dir = ironclad::userDataDir();
    if (!dir.empty())
      mMidi.setData(ironclad::midiMapFromJson(
        ironclad::readTextFile(ironclad::utf8FromPath(ironclad::pathFromUtf8(dir) / "midi-default.json"))));
  }
  catch (const std::exception&)
  {
  }

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // TEMPORARY functional editor (Milestones 1-5). Every control here is real and
    // host-synchronised; the photorealistic IRONCLAD interface replaces it in Milestone 6.
    // Pages follow the final product's navigation: STOMP / AMP / CAB / POST FX.
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
                            .WithLabelText(IText(13.f, kTextHi))
                            .WithValueText(IText(12.f, kAmber));
    const IVStyle buttonStyle = style.WithLabelText(IText(14.f, kAmber));
    static const char* kGroups[4] = {"stomp", "amp", "cab", "post"};

    const char* group = "";
    auto attach = [&](IControl* c, int tag = kNoTag) { pGraphics->AttachControl(c, tag, group); };
    auto knob = [&](const IRECT& r, int param, const char* label) { attach(new IVKnobControl(r, param, label, style)); };
    auto toggle = [&](const IRECT& r, int param, const char* label) {
      attach(new IVToggleControl(r, param, label, style, "OFF", "ON"));
    };
    auto choice = [&](const IRECT& r, int param, const char* label) { attach(new IVMenuButtonControl(r, param, label, style)); };
    auto panel = [&](const IRECT& r) { attach(new IPanelControl(r, kPanel)); };
    auto title = [&](const IRECT& r, const char* text) {
      attach(new ITextControl(r, text, IText(16.f, kAmber, nullptr, EAlign::Near)));
    };
    /// A pedal: panel, title, ON switch and a row of controls.
    struct Ctl { int param; const char* label; int kind; }; // kind 0 knob, 1 toggle, 2 menu
    auto pedal = [&](const IRECT& r, const char* name, int enableParam, std::initializer_list<Ctl> ctls) {
      panel(r);
      const IRECT in = r.GetPadded(-10.f);
      title(in.GetFromTop(22.f).GetReducedFromRight(80.f), name);
      toggle(in.GetFromTop(52.f).GetFromRight(76.f), enableParam, "");
      const IRECT row = in.GetReducedFromTop(58.f);
      const int n = static_cast<int>(ctls.size());
      int k = 0;
      for (const auto& c : ctls)
      {
        const IRECT cell = row.GetGridCell(k++, 1, n);
        if (c.kind == 0) knob(cell.GetPadded(-3.f), c.param, c.label);
        else if (c.kind == 1) toggle(cell.GetCentredInside(std::min(cell.W(), 80.f), 64.f), c.param, c.label);
        else choice(cell.GetCentredInside(std::min(cell.W() - 6.f, 120.f), 54.f), c.param, c.label);
      }
    };

    // ---- Top bar: presets, undo/redo, MIDI. Page navigation at the bottom. ----
    {
      const IRECT bar = bounds.GetFromTop(34.f);
      float x = bar.L;
      auto next = [&](float w) {
        const IRECT r(x, bar.T, x + w, bar.B);
        x += w + 6.f;
        return r;
      };
      pGraphics->AttachControl(new ITextControl(next(150.f), "IRONCLAD", IText(28.f, kTextHi, nullptr, EAlign::Near)));
      auto barButton = [&](float w, const char* label, std::function<void(IControl*)> f, int tag = kNoTag) {
        pGraphics->AttachControl(new IVButtonControl(next(w), [f](IControl* c) {
          SplashClickActionFunc(c);
          f(c);
        }, label, buttonStyle.WithDrawShadows(false), true, false), tag);
      };
      barButton(34.f, "<", [this](IControl*) { StepPreset(-1); });
      pGraphics->AttachControl(new ironclad_ui::ClickTextControl(next(330.f), IText(17.f, kTextHi, nullptr, EAlign::Near),
                                                                 IColor(255, 40, 40, 44),
                                                                 [this]() {
                                                                   if (IGraphics* g = GetUI())
                                                                     ShowPresetMenu(g->GetControlWithTag(kCtrlTagPresetName));
                                                                 }),
                               kCtrlTagPresetName);
      barButton(34.f, ">", [this](IControl*) { StepPreset(+1); });
      barButton(48.f, "FAV", [this](IControl*) { ToggleFavorite(); }, kCtrlTagFavorite);
      barButton(64.f, "SAVE", [this](IControl*) { SavePreset(false); });
      barButton(40.f, "...", [this](IControl* c) { ShowPresetActionsMenu(c); });
      x += 14.f;
      barButton(64.f, "UNDO", [this](IControl*) { Undo(); }, kCtrlTagUndo);
      barButton(64.f, "REDO", [this](IControl*) { Redo(); }, kCtrlTagRedo);
      barButton(64.f, "MIDI", [this](IControl* c) { ShowMidiMenu(c); }, kCtrlTagMidi);
      pGraphics->AttachControl(new ITextControl(IRECT(bounds.L + 156.f, bar.B + 2.f, bounds.R, bar.B + 18.f), "",
                                                IText(13.f, kAmber, nullptr, EAlign::Near)),
                               kCtrlTagPresetStatus);
    }
    const IRECT content = IRECT(bounds.L, bounds.T + 56.f, bounds.R, bounds.B - 62.f);
    pGraphics->AttachControl(new IVTabSwitchControl(bounds.GetFromBottom(44.f).GetReducedFromBottom(14.f),
                                                    [this](IControl* pCaller) {
                                                      ShowPage(static_cast<int>(std::lround(pCaller->GetValue() * 3.0)));
                                                    },
                                                    {"STOMP", "AMP", "CAB", "POST FX"}, "", buttonStyle));

    // ================= STOMP =================
    group = kGroups[0];
    {
      const IRECT r1 = content.GetFromTop(content.H() * 0.5f).GetPadded(-4.f);
      const IRECT r2 = content.GetFromBottom(content.H() * 0.5f).GetPadded(-4.f);
      pedal(IRECT(r1.L, r1.T, r1.L + r1.W() * 0.25f, r1.B).GetPadded(-4.f), "NOISE GATE", ironclad::kParamGateEnabled,
            {{ironclad::kParamGateThreshold, "Threshold", 0}, {ironclad::kParamGateRelease, "Release", 0}});
      pedal(IRECT(r1.L + r1.W() * 0.25f, r1.T, r1.L + r1.W() * 0.75f, r1.B).GetPadded(-4.f), "COMPRESSOR",
            ironclad::kParamCompEnabled,
            {{ironclad::kParamCompThreshold, "Threshold", 0}, {ironclad::kParamCompRatio, "Ratio", 0},
             {ironclad::kParamCompAttack, "Attack", 0}, {ironclad::kParamCompRelease, "Release", 0},
             {ironclad::kParamCompMakeup, "Makeup", 0}, {ironclad::kParamCompMix, "Mix", 0}});
      pedal(IRECT(r1.L + r1.W() * 0.75f, r1.T, r1.R, r1.B).GetPadded(-4.f), "BOOST", ironclad::kParamBoostEnabled,
            {{ironclad::kParamBoostGain, "Gain", 0}, {ironclad::kParamBoostBright, "Bright", 1}});
      pedal(r2.GetGridCell(0, 1, 4).GetPadded(-4.f), "OVERDRIVE", ironclad::kParamOdEnabled,
            {{ironclad::kParamOdDrive, "Drive", 0}, {ironclad::kParamOdTone, "Tone", 0}, {ironclad::kParamOdLevel, "Level", 0}});
      pedal(r2.GetGridCell(1, 1, 4).GetPadded(-4.f), "DISTORTION", ironclad::kParamDistEnabled,
            {{ironclad::kParamDistDrive, "Drive", 0}, {ironclad::kParamDistTone, "Tone", 0}, {ironclad::kParamDistLevel, "Level", 0}});
      pedal(r2.GetGridCell(2, 1, 4).GetPadded(-4.f), "FUZZ", ironclad::kParamFuzzEnabled,
            {{ironclad::kParamFuzzFuzz, "Fuzz", 0}, {ironclad::kParamFuzzTone, "Tone", 0}, {ironclad::kParamFuzzLevel, "Level", 0}});
      pedal(r2.GetGridCell(3, 1, 4).GetPadded(-4.f), "WAH", ironclad::kParamWahEnabled,
            {{ironclad::kParamWahPosition, "Pedal", 0}, {ironclad::kParamWahResonance, "Reso", 0}, {ironclad::kParamWahMix, "Mix", 0}});
    }

    // ================= AMP =================
    group = kGroups[1];
    {
      const IRECT capture = content.GetFromTop(150.f);
      panel(capture);
      const IRECT capInner = capture.GetPadded(-14.f);
      const IRECT capButtons = capInner.GetFromLeft(180.f);
      attach(new IVButtonControl(capButtons.GetFromTop(42.f), [this](IControl* pCaller) {
        SplashClickActionFunc(pCaller);
        BrowseForCapture();
      }, "LOAD CAPTURE...", buttonStyle, true, false));
      attach(new IVButtonControl(capButtons.GetFromTop(42.f).GetVShifted(50.f), [this](IControl* pCaller) {
        SplashClickActionFunc(pCaller);
        LoadCaptureFile({}, true);
      }, "CLEAR", style, true, false));
      const IRECT capInfo = capInner.GetReducedFromLeft(200.f);
      attach(new ITextControl(capInfo.GetFromTop(36.f), "No capture loaded", IText(26.f, kTextHi, nullptr, EAlign::Near)),
             kCtrlTagCaptureName);
      attach(new ITextControl(capInfo.GetFromTop(62.f).GetReducedFromTop(38.f), "", IText(15.f, kTextLo, nullptr, EAlign::Near)),
             kCtrlTagCaptureMeta);
      attach(new ITextControl(capInfo.GetFromTop(88.f).GetReducedFromTop(64.f), "", IText(14.f, kAmber, nullptr, EAlign::Near)),
             kCtrlTagCaptureStatus);

      const IRECT ampRow = content.GetReducedFromTop(170.f).GetFromTop(170.f);
      knob(ampRow.GetGridCell(0, 1, 6).GetPadded(-6.f), ironclad::kParamInputGain, "Input");
      knob(ampRow.GetGridCell(1, 1, 6).GetPadded(-6.f), ironclad::kParamAmpInputGain, "Amp Input");
      knob(ampRow.GetGridCell(2, 1, 6).GetPadded(-6.f), ironclad::kParamAmpOutputGain, "Amp Output");
      knob(ampRow.GetGridCell(3, 1, 6).GetPadded(-6.f), ironclad::kParamOutputGain, "Output");
      toggle(ampRow.GetGridCell(4, 1, 6).GetCentredInside(100.f, 80.f), ironclad::kParamAmpEnabled, "Amp");
      toggle(ampRow.GetGridCell(5, 1, 6).GetCentredInside(100.f, 80.f), ironclad::kParamAmpNormalize, "Normalize");
      const IRECT calRow = content.GetReducedFromTop(360.f).GetFromTop(150.f);
      toggle(calRow.GetGridCell(0, 1, 6).GetCentredInside(110.f, 80.f), ironclad::kParamAmpCalibrate, "Input Calibration");
      knob(calRow.GetGridCell(1, 1, 6).GetPadded(-6.f), ironclad::kParamInterfaceDbu, "Interface Level");
      knob(calRow.GetGridCell(3, 1, 6).GetPadded(-6.f), ironclad::kParamVolume, "Volume Pedal");
      attach(new ITextControl(content.GetReducedFromTop(520.f).GetFromTop(60.f),
                              "Amp Input / Amp Output set the level into and out of the capture. A capture records one amp "
                              "setting; tone shaping is done with the EQ on the POST FX page. Input Calibration: set "
                              "Interface Level to your interface's input level at 0 dBFS (dBu, see its manual).",
                              IText(13.f, kTextLo)));
    }

    // ================= CAB =================
    group = kGroups[2];
    {
      const IRECT cab = content.GetReducedFromBottom(150.f);
      for (int slot = 0; slot < 2; ++slot)
      {
        const IRECT pnl = cab.GetGridCell(slot, 1, 2).GetPadded(-4.f);
        panel(pnl);
        const IRECT in = pnl.GetPadded(-12.f);
        title(in.GetFromTop(22.f).GetFromLeft(60.f), slot == 0 ? "IR A" : "IR B");
        const IRECT btnRow = in.GetFromTop(32.f).GetReducedFromLeft(70.f);
        attach(new IVButtonControl(btnRow.GetGridCell(0, 1, 2).GetPadded(-3.f), [this, slot](IControl* pCaller) {
          SplashClickActionFunc(pCaller);
          BrowseForIr(slot);
        }, "LOAD IR...", buttonStyle, true, false));
        attach(new IVButtonControl(btnRow.GetGridCell(1, 1, 2).GetPadded(-3.f), [this, slot](IControl* pCaller) {
          SplashClickActionFunc(pCaller);
          LoadIrFile(slot, {}, true);
        }, "CLEAR", style, true, false));
        attach(new ITextControl(in.GetReducedFromTop(38.f).GetFromTop(24.f), "No IR loaded", IText(18.f, kTextHi, nullptr, EAlign::Near)),
               slot == 0 ? kCtrlTagIrName0 : kCtrlTagIrName1);
        attach(new ITextControl(in.GetReducedFromTop(62.f).GetFromTop(18.f), "", IText(13.f, kTextLo, nullptr, EAlign::Near)),
               slot == 0 ? kCtrlTagIrInfo0 : kCtrlTagIrInfo1);
        attach(new ITextControl(in.GetReducedFromTop(80.f).GetFromTop(18.f), "", IText(13.f, kAmber, nullptr, EAlign::Near)),
               slot == 0 ? kCtrlTagIrStatus0 : kCtrlTagIrStatus1);
        attach(new IrWaveformControl(in.GetReducedFromTop(102.f).GetFromTop(44.f)), slot == 0 ? kCtrlTagIrWave0 : kCtrlTagIrWave1);
        const IRECT knobs = in.GetReducedFromTop(150.f);
        knob(knobs.GetGridCell(0, 1, 4).GetPadded(-4.f), slot == 0 ? ironclad::kParamCabALevel : ironclad::kParamCabBLevel, "Level");
        knob(knobs.GetGridCell(1, 1, 4).GetPadded(-4.f), slot == 0 ? ironclad::kParamCabAPan : ironclad::kParamCabBPan, "Pan");
        toggle(knobs.GetGridCell(2, 1, 4).GetCentredInside(80.f, 70.f), slot == 0 ? ironclad::kParamCabAEnabled : ironclad::kParamCabBEnabled, "On");
        toggle(knobs.GetGridCell(3, 1, 4).GetCentredInside(80.f, 70.f), slot == 0 ? ironclad::kParamCabAInvert : ironclad::kParamCabBInvert, "Invert");
      }
      const IRECT cabRow = content.GetFromBottom(140.f);
      knob(cabRow.GetGridCell(0, 1, 5).GetPadded(-6.f), ironclad::kParamCabBlend, "A / B Blend");
      knob(cabRow.GetGridCell(1, 1, 5).GetPadded(-6.f), ironclad::kParamCabHighPass, "Cab HPF");
      knob(cabRow.GetGridCell(2, 1, 5).GetPadded(-6.f), ironclad::kParamCabLowPass, "Cab LPF");
      toggle(cabRow.GetGridCell(3, 1, 5).GetCentredInside(100.f, 80.f), ironclad::kParamCabEnabled, "Cab");
    }

    // ================= POST FX =================
    group = kGroups[3];
    {
      const IRECT r1 = content.GetFromTop(content.H() * 0.5f).GetPadded(-4.f);
      const IRECT r2 = content.GetFromBottom(content.H() * 0.5f).GetPadded(-4.f);
      pedal(IRECT(r1.L, r1.T, r1.L + r1.W() * 0.6f, r1.B).GetPadded(-4.f), "EQ", ironclad::kParamEqEnabled,
            {{ironclad::kParamEqLow, "Bass", 0}, {ironclad::kParamEqMid, "Mid", 0}, {ironclad::kParamEqMidFreq, "Mid Freq", 0},
             {ironclad::kParamEqHigh, "Treble", 0}, {ironclad::kParamEqLevel, "Level", 0}});
      pedal(IRECT(r1.L + r1.W() * 0.6f, r1.T, r1.R, r1.B).GetPadded(-4.f), "CHORUS", ironclad::kParamChorusEnabled,
            {{ironclad::kParamChorusRate, "Rate", 0}, {ironclad::kParamChorusDepth, "Depth", 0}, {ironclad::kParamChorusMix, "Mix", 0}});
      const IRECT delayRect = IRECT(r2.L, r2.T, r2.L + r2.W() * 0.62f, r2.B).GetPadded(-4.f);
      pedal(delayRect, "DELAY", ironclad::kParamDelayEnabled,
            {{ironclad::kParamDelayType, "Type", 2}, {ironclad::kParamDelayTime, "Time", 0}, {ironclad::kParamDelaySync, "Sync", 1},
             {ironclad::kParamDelayDivision, "Division", 2}, {ironclad::kParamDelayFeedback, "Feedback", 0},
             {ironclad::kParamDelayMix, "Mix", 0}, {ironclad::kParamDelayLowCut, "Low Cut", 0}, {ironclad::kParamDelayHighCut, "High Cut", 0}});
      attach(new IVButtonControl(delayRect.GetPadded(-10.f).GetFromTop(26.f).GetFromRight(160.f).GetHShifted(-84.f),
                                 [this](IControl* pCaller) {
                                   SplashClickActionFunc(pCaller);
                                   TapDelayTempo(std::chrono::duration<double>(
                                     std::chrono::steady_clock::now().time_since_epoch()).count());
                                 },
                                 "TAP", buttonStyle, true, false));
      pedal(IRECT(r2.L + r2.W() * 0.62f, r2.T, r2.R, r2.B).GetPadded(-4.f), "REVERB", ironclad::kParamReverbEnabled,
            {{ironclad::kParamReverbType, "Type", 2}, {ironclad::kParamReverbDecay, "Decay", 0},
             {ironclad::kParamReverbPreDelay, "Pre-Delay", 0}, {ironclad::kParamReverbDamping, "Damping", 0},
             {ironclad::kParamReverbMix, "Mix", 0}});
    }
    group = "";

    WDL_String buildInfo;
    GetBuildInfoStr(buildInfo, __DATE__, __TIME__);
    pGraphics->AttachControl(new ITextControl(bounds.GetFromBottom(14.f), buildInfo.Get(),
                                              IText(11.f, IColor(255, 110, 110, 110), nullptr, EAlign::Far)));
    ShowPage(mPage);
    RefreshPresetBar();
    mShownStatusSerial = UINT64_MAX;
    mShownIrSerial = UINT64_MAX;
    RefreshCaptureDisplay(true);
    RefreshIrDisplay(true);
  };
#endif
}

void IRONCLAD::ShowPage(int page)
{
#if IPLUG_EDITOR
  mPage = std::clamp(page, 0, 3);
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  static const char* kGroups[4] = {"stomp", "amp", "cab", "post"};
  for (int p = 0; p < 4; ++p)
    pGraphics->ForControlInGroup(kGroups[p], [p, this](IControl* c) { c->Hide(p != mPage); });
  // Keep the tab switch in sync (e.g. when the editor is reopened).
  for (int i = 0; i < pGraphics->NControls(); ++i)
    if (auto* tabs = dynamic_cast<IVTabSwitchControl*>(pGraphics->GetControl(i)))
      tabs->SetValue(mPage / 3.0);
  pGraphics->SetAllControlsDirty();
#endif
}

void IRONCLAD::TapDelayTempo(double timeSeconds)
{
  const auto ms = mTapTempo.tap(timeSeconds);
  if (!ms)
    return;
  SetParamFromPlugin(ironclad::kParamDelaySync, 0.0); // a tapped time replaces tempo sync
  SetParamFromPlugin(ironclad::kParamDelayTime, *ms);
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
                             LoadCaptureFile(fileName.Get(), true);
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
                             LoadIrFile(slot, fileName.Get(), true);
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
  RefreshPresetBar();
}

void IRONCLAD::OnIdle()
{
  // Main thread. Latency changes when a capture needs resampling at this host rate.
  const int latency = mEngine.latencySamples();
  if (latency != GetLatency())
    SetLatency(latency);
  RefreshCaptureDisplay(false);
  RefreshIrDisplay(false);
  DrainMidiEvents();
  // Preset messages fade out after a while.
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (!mPresetStatus.empty() && now - mPresetStatusTime > (mPresetStatusIsError ? 12.0 : 4.0) &&
      mMidi.learnTarget() == ironclad::kMidiNoTarget && !mLearnOnTouch)
    SetPresetStatus({}, false);
}

bool IRONCLAD::SerializeState(IByteChunk& chunk) const
{
  // Host thread. Everything read here is either an atomic or only changed on this thread.
  ironclad::SessionState st;
  st.preset = CurrentPresetData();
  st.presetPath = mPresetPath;
  st.presetModified = mPresetModified;
  st.midi = mMidi.data();
  const std::vector<std::uint8_t> bytes = ironclad::encodeState(st);
  chunk.PutBytes(bytes.data(), static_cast<int>(bytes.size()));
  return true;
}

int IRONCLAD::UnserializeState(const IByteChunk& chunk, int startPos)
{
  ironclad::SessionState st;
  const int pos = ironclad::decodeState(chunk.GetData(), chunk.Size(), startPos, st);
  if (pos < 0)
    return -1; // not ours / damaged / from a newer version: the host keeps the defaults

  if (st.sourceVersion >= 3)
    mMidi.setData(st.midi); // older projects had no MIDI map: keep the default one
  if (st.preset.name.empty())
    st.preset.name = "Untitled";
  ApplyPreset(st.preset, {}, false, false, false);
  mPresetPath = st.presetPath;
  mPresetModified = st.presetModified;
  mUndo.clear();
  RefreshPresetBar();
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

void IRONCLAD::ProcessMidiMsg(const IMidiMsg& msg)
{
  // Audio thread: lock-free, allocation-free. The engine reacts at once; the host, UI and
  // undo history follow from OnIdle.
  if (msg.StatusMsg() != IMidiMsg::kControlChange)
    return;
  const IMidiMsg::EControlChangeMsg cc = msg.ControlChangeIdx();
  ironclad::MidiEvent e;
  if (!mMidi.handleCc(static_cast<int>(cc), msg.ControlChange(cc), e))
    return;
  using Kind = ironclad::MidiEvent::Kind;
  if (e.kind == Kind::ToggleParam)
  {
    e.kind = Kind::SetParam;
    e.value = mEngine.getParameter(e.target) >= 0.5 ? 0.0 : 1.0;
  }
  if (e.kind == Kind::SetParam)
    mEngine.setParameter(e.target, e.value);
  else if (e.kind == Kind::Action)
    e.timeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  mMidiEvents.push(e); // if the queue is ever full the engine already has the value
}

void IRONCLAD::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  mEngine.setTempo(GetTempo());
  mEngine.process(inputs, NInChansConnected(), outputs, NOutChansConnected(), nFrames);
}
#endif
