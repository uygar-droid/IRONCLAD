#pragma once

// Single source of truth for every host-visible parameter.
//
// STABILITY RULES (DAW automation and saved projects depend on these):
//   * Never reorder, renumber or reuse an entry. New parameters are appended only.
//   * Never change an `id` string once released.
//   * A parameter that is no longer used stays in the table (mark it kHidden).
//   * Tests/param_ids.snapshot must be a prefix of this table; the test suite
//     fails on any other change.

#include <array>
#include <cstdint>
#include <string_view>

namespace ironclad
{

enum class ParamUnit : std::uint8_t
{
  None,
  Decibels,
  Percent,
  Milliseconds,
  Hertz,
  Boolean,
  Choice,  // integer index into `choices` ("A|B|C")
  Seconds,
  Ratio
};

enum ParamFlags : std::uint32_t
{
  kParamNoFlags = 0,
  kParamHidden = 1u << 0,      // kept for compatibility, not shown in host lists
  kParamNotAutomatable = 1u << 1
};

struct ParamSpec
{
  int index;
  std::string_view id;
  std::string_view name;
  double minValue;
  double maxValue;
  double defaultValue;
  double step;
  ParamUnit unit;
  double smoothingMs; // 0 = applied without smoothing
  std::uint32_t flags;
  bool logScale = false; // host/UI knob taper (frequencies)
  std::string_view choices = {}; // Choice params: labels separated by '|'
};

enum ParamIndex : int
{
  kParamInputGain = 0,
  kParamOutputGain = 1,
  kParamAmpEnabled = 2,
  kParamAmpInputGain = 3,
  kParamAmpOutputGain = 4,
  kParamAmpNormalize = 5,
  kParamCabEnabled = 6,
  kParamCabAEnabled = 7,
  kParamCabALevel = 8,
  kParamCabAPan = 9,
  kParamCabAInvert = 10,
  kParamCabBEnabled = 11,
  kParamCabBLevel = 12,
  kParamCabBPan = 13,
  kParamCabBInvert = 14,
  kParamCabBlend = 15,
  kParamCabHighPass = 16,
  kParamCabLowPass = 17,
  kParamGateEnabled = 18,
  kParamGateThreshold = 19,
  kParamGateRelease = 20,
  kParamCompEnabled = 21,
  kParamCompThreshold = 22,
  kParamCompRatio = 23,
  kParamCompAttack = 24,
  kParamCompRelease = 25,
  kParamCompMakeup = 26,
  kParamCompMix = 27,
  kParamBoostEnabled = 28,
  kParamBoostGain = 29,
  kParamBoostBright = 30,
  kParamOdEnabled = 31,
  kParamOdDrive = 32,
  kParamOdTone = 33,
  kParamOdLevel = 34,
  kParamDistEnabled = 35,
  kParamDistDrive = 36,
  kParamDistTone = 37,
  kParamDistLevel = 38,
  kParamFuzzEnabled = 39,
  kParamFuzzFuzz = 40,
  kParamFuzzTone = 41,
  kParamFuzzLevel = 42,
  kParamWahEnabled = 43,
  kParamWahPosition = 44,
  kParamWahResonance = 45,
  kParamWahMix = 46,
  kParamEqEnabled = 47,
  kParamEqLow = 48,
  kParamEqMid = 49,
  kParamEqMidFreq = 50,
  kParamEqHigh = 51,
  kParamEqLevel = 52,
  kParamChorusEnabled = 53,
  kParamChorusRate = 54,
  kParamChorusDepth = 55,
  kParamChorusMix = 56,
  kParamDelayEnabled = 57,
  kParamDelayType = 58,
  kParamDelayTime = 59,
  kParamDelaySync = 60,
  kParamDelayDivision = 61,
  kParamDelayFeedback = 62,
  kParamDelayMix = 63,
  kParamDelayLowCut = 64,
  kParamDelayHighCut = 65,
  kParamReverbEnabled = 66,
  kParamReverbType = 67,
  kParamReverbDecay = 68,
  kParamReverbPreDelay = 69,
  kParamReverbDamping = 70,
  kParamReverbMix = 71,
  kNumParams
};

inline constexpr std::array<ParamSpec, kNumParams> kParamSpecs{{
  {kParamInputGain, "global.input_db", "Input", -24.0, 24.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamOutputGain, "global.output_db", "Output", -60.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  // Amp (NAM capture) stage. These are gain-staging controls around the capture, not
  // the original amplifier's knobs (a capture is a snapshot of one amp setting).
  {kParamAmpEnabled, "amp.enabled", "Amp On", 0.0, 1.0, 1.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamAmpInputGain, "amp.input_db", "Amp Input", -24.0, 24.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamAmpOutputGain, "amp.output_db", "Amp Output", -40.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamAmpNormalize, "amp.normalize", "Normalize", 0.0, 1.0, 1.0, 1.0, ParamUnit::Boolean, 30.0, kParamNoFlags},
  // Cabinet (impulse responses). Slot A and B are independent; Blend mixes them with equal
  // power when both are loaded. HPF/LPF act on the cabinet output.
  {kParamCabEnabled, "cab.enabled", "Cab On", 0.0, 1.0, 1.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCabAEnabled, "cab.a.enabled", "IR A On", 0.0, 1.0, 1.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCabALevel, "cab.a.level_db", "IR A Level", -40.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamCabAPan, "cab.a.pan", "IR A Pan", -100.0, 100.0, 0.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamCabAInvert, "cab.a.invert", "IR A Invert", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCabBEnabled, "cab.b.enabled", "IR B On", 0.0, 1.0, 1.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCabBLevel, "cab.b.level_db", "IR B Level", -40.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamCabBPan, "cab.b.pan", "IR B Pan", -100.0, 100.0, 0.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamCabBInvert, "cab.b.invert", "IR B Invert", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCabBlend, "cab.blend", "IR Blend", 0.0, 100.0, 50.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamCabHighPass, "cab.hpf_hz", "Cab HPF", 20.0, 1000.0, 20.0, 1.0, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  {kParamCabLowPass, "cab.lpf_hz", "Cab LPF", 1000.0, 20000.0, 20000.0, 1.0, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  // ---- Effects (Milestone 4) ----
  {kParamGateEnabled, "gate.enabled", "Gate On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamGateThreshold, "gate.threshold_db", "Gate Threshold", -90.0, 0.0, -60.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamGateRelease, "gate.release_ms", "Gate Release", 5.0, 500.0, 80.0, 1.0, ParamUnit::Milliseconds, 30.0, kParamNoFlags, true},
  {kParamCompEnabled, "comp.enabled", "Comp On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamCompThreshold, "comp.threshold_db", "Comp Threshold", -60.0, 0.0, -20.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamCompRatio, "comp.ratio", "Comp Ratio", 1.0, 20.0, 4.0, 0.1, ParamUnit::Ratio, 30.0, kParamNoFlags, true},
  {kParamCompAttack, "comp.attack_ms", "Comp Attack", 0.1, 100.0, 5.0, 0.1, ParamUnit::Milliseconds, 30.0, kParamNoFlags, true},
  {kParamCompRelease, "comp.release_ms", "Comp Release", 10.0, 1000.0, 120.0, 1.0, ParamUnit::Milliseconds, 30.0, kParamNoFlags, true},
  {kParamCompMakeup, "comp.makeup_db", "Comp Makeup", -12.0, 24.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamCompMix, "comp.mix", "Comp Mix", 0.0, 100.0, 100.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamBoostEnabled, "boost.enabled", "Boost On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamBoostGain, "boost.gain_db", "Boost Gain", 0.0, 24.0, 6.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamBoostBright, "boost.bright", "Boost Bright", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 30.0, kParamNoFlags},
  {kParamOdEnabled, "od.enabled", "Overdrive On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamOdDrive, "od.drive", "Overdrive Drive", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamOdTone, "od.tone", "Overdrive Tone", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamOdLevel, "od.level_db", "Overdrive Level", -24.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamDistEnabled, "dist.enabled", "Distortion On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamDistDrive, "dist.drive", "Distortion Drive", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamDistTone, "dist.tone", "Distortion Tone", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamDistLevel, "dist.level_db", "Distortion Level", -24.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamFuzzEnabled, "fuzz.enabled", "Fuzz On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamFuzzFuzz, "fuzz.fuzz", "Fuzz Amount", 0.0, 100.0, 60.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamFuzzTone, "fuzz.tone", "Fuzz Tone", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamFuzzLevel, "fuzz.level_db", "Fuzz Level", -24.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamWahEnabled, "wah.enabled", "Wah On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamWahPosition, "wah.position", "Wah Position", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 15.0, kParamNoFlags},
  {kParamWahResonance, "wah.resonance", "Wah Resonance", 0.0, 100.0, 60.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamWahMix, "wah.mix", "Wah Mix", 0.0, 100.0, 100.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamEqEnabled, "eq.enabled", "EQ On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamEqLow, "eq.low_db", "EQ Bass", -12.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamEqMid, "eq.mid_db", "EQ Mid", -12.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamEqMidFreq, "eq.mid_hz", "EQ Mid Freq", 200.0, 5000.0, 800.0, 1.0, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  {kParamEqHigh, "eq.high_db", "EQ Treble", -12.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamEqLevel, "eq.level_db", "EQ Level", -12.0, 12.0, 0.0, 0.1, ParamUnit::Decibels, 30.0, kParamNoFlags},
  {kParamChorusEnabled, "chorus.enabled", "Chorus On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamChorusRate, "chorus.rate_hz", "Chorus Rate", 0.05, 5.0, 0.8, 0.01, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  {kParamChorusDepth, "chorus.depth", "Chorus Depth", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamChorusMix, "chorus.mix", "Chorus Mix", 0.0, 100.0, 50.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamDelayEnabled, "delay.enabled", "Delay On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamDelayType, "delay.type", "Delay Type", 0.0, 2.0, 0.0, 1.0, ParamUnit::Choice, 0.0, kParamNoFlags, false, "Digital|Analog|Ping-Pong"},
  {kParamDelayTime, "delay.time_ms", "Delay Time", 10.0, 2000.0, 400.0, 1.0, ParamUnit::Milliseconds, 0.0, kParamNoFlags, true},
  {kParamDelaySync, "delay.sync", "Delay Sync", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 0.0, kParamNoFlags},
  {kParamDelayDivision, "delay.division", "Delay Division", 0.0, 9.0, 3.0, 1.0, ParamUnit::Choice, 0.0, kParamNoFlags, false, "1/1|1/2|1/2 dot|1/4|1/4 dot|1/4 trip|1/8|1/8 dot|1/8 trip|1/16"},
  {kParamDelayFeedback, "delay.feedback", "Delay Feedback", 0.0, 95.0, 35.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamDelayMix, "delay.mix", "Delay Mix", 0.0, 100.0, 30.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamDelayLowCut, "delay.lowcut_hz", "Delay Low Cut", 20.0, 1000.0, 80.0, 1.0, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  {kParamDelayHighCut, "delay.highcut_hz", "Delay High Cut", 1000.0, 20000.0, 8000.0, 1.0, ParamUnit::Hertz, 30.0, kParamNoFlags, true},
  {kParamReverbEnabled, "reverb.enabled", "Reverb On", 0.0, 1.0, 0.0, 1.0, ParamUnit::Boolean, 20.0, kParamNoFlags},
  {kParamReverbType, "reverb.type", "Reverb Type", 0.0, 2.0, 1.0, 1.0, ParamUnit::Choice, 0.0, kParamNoFlags, false, "Room|Hall|Plate"},
  {kParamReverbDecay, "reverb.decay_s", "Reverb Decay", 0.2, 10.0, 1.8, 0.01, ParamUnit::Seconds, 0.0, kParamNoFlags, true},
  {kParamReverbPreDelay, "reverb.predelay_ms", "Reverb Pre-Delay", 0.0, 200.0, 20.0, 0.1, ParamUnit::Milliseconds, 0.0, kParamNoFlags},
  {kParamReverbDamping, "reverb.damping", "Reverb Damping", 0.0, 100.0, 50.0, 0.1, ParamUnit::Percent, 30.0, kParamNoFlags},
  {kParamReverbMix, "reverb.mix", "Reverb Mix", 0.0, 100.0, 25.0, 1.0, ParamUnit::Percent, 30.0, kParamNoFlags},
}};

constexpr const ParamSpec& paramSpec(int index) noexcept { return kParamSpecs[static_cast<std::size_t>(index)]; }

/// Returns the index for a stable id, or -1. Not real-time critical.
constexpr int findParamIndex(std::string_view id) noexcept
{
  for (const auto& spec : kParamSpecs)
    if (spec.id == id)
      return spec.index;
  return -1;
}

constexpr double clampToRange(const ParamSpec& spec, double v) noexcept
{
  return v < spec.minValue ? spec.minValue : (v > spec.maxValue ? spec.maxValue : v);
}

// Compile-time integrity checks.
constexpr bool paramTableIsConsistent()
{
  for (std::size_t i = 0; i < kParamSpecs.size(); ++i)
  {
    const auto& s = kParamSpecs[i];
    if (s.index != static_cast<int>(i)) return false;
    if (!(s.minValue < s.maxValue)) return false;
    if (s.defaultValue < s.minValue || s.defaultValue > s.maxValue) return false;
    for (std::size_t j = i + 1; j < kParamSpecs.size(); ++j)
      if (kParamSpecs[j].id == s.id) return false;
  }
  return true;
}
static_assert(paramTableIsConsistent(), "Parameter table is inconsistent (index order, range or duplicate id)");

} // namespace ironclad
