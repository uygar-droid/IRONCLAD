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
  Boolean
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
