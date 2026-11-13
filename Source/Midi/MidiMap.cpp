#include "Midi/MidiMap.h"

#include "Parameters/Parameters.h"

namespace ironclad
{
namespace
{
constexpr std::int32_t kEmpty = -1;
constexpr int kTargetOffset = 4096; // keeps encoded targets (actions are negative) positive

bool validTarget(int target) noexcept
{
  return (target >= kMidiFirstAction && target < 0) || (target >= 0 && target < kNumParams);
}
} // namespace

const char* midiActionName(int target) noexcept
{
  switch (target)
  {
  case kMidiActionTapTempo: return "Tap Tempo";
  case kMidiActionPresetNext: return "Next Preset";
  case kMidiActionPresetPrev: return "Previous Preset";
  default: return nullptr;
  }
}

std::int32_t MidiMap::encode(MidiAssignment a) noexcept
{
  if (!validTarget(a.target))
    return kEmpty;
  return ((a.target + kTargetOffset) << 1) | (a.toggle ? 1 : 0);
}

MidiAssignment MidiMap::decode(std::int32_t v) noexcept
{
  if (v < 0)
    return {};
  return {(v >> 1) - kTargetOffset, (v & 1) != 0};
}

MidiMap::MidiMap()
{
  for (auto& s : mSlots)
    s.store(kEmpty, std::memory_order_relaxed);
  mLearn.store(kEmpty, std::memory_order_relaxed);
}

void MidiMap::setData(const MidiMapData& data)
{
  for (auto& s : mSlots)
    s.store(kEmpty, std::memory_order_relaxed);
  for (const auto& [cc, a] : data.cc)
    if (cc >= 0 && cc < 128)
      assign(cc, a);
}

MidiMapData MidiMap::data() const
{
  MidiMapData d;
  for (int cc = 0; cc < 128; ++cc)
  {
    const MidiAssignment a = assignment(cc);
    if (a.target != kMidiNoTarget)
      d.cc[cc] = a;
  }
  return d;
}

void MidiMap::assign(int cc, MidiAssignment a)
{
  if (cc < 0 || cc >= 128)
    return;
  if (validTarget(a.target))
    clearTarget(a.target); // one CC per target keeps the UI and learn behaviour simple
  mSlots[static_cast<std::size_t>(cc)].store(encode(a), std::memory_order_release);
}

void MidiMap::clearCc(int cc)
{
  if (cc >= 0 && cc < 128)
    mSlots[static_cast<std::size_t>(cc)].store(kEmpty, std::memory_order_release);
}

void MidiMap::clearTarget(int target)
{
  for (auto& s : mSlots)
    if (decode(s.load(std::memory_order_acquire)).target == target)
      s.store(kEmpty, std::memory_order_release);
}

void MidiMap::clearAll()
{
  for (auto& s : mSlots)
    s.store(kEmpty, std::memory_order_release);
  cancelLearn();
}

int MidiMap::ccForTarget(int target) const
{
  for (int cc = 0; cc < 128; ++cc)
    if (assignment(cc).target == target)
      return cc;
  return -1;
}

MidiAssignment MidiMap::assignment(int cc) const
{
  if (cc < 0 || cc >= 128)
    return {};
  return decode(mSlots[static_cast<std::size_t>(cc)].load(std::memory_order_acquire));
}

void MidiMap::armLearn(int target, bool toggle)
{
  mLearn.store(validTarget(target) ? encode({target, toggle}) : kEmpty, std::memory_order_release);
}

void MidiMap::cancelLearn() { mLearn.store(kEmpty, std::memory_order_release); }

int MidiMap::learnTarget() const { return decode(mLearn.load(std::memory_order_acquire)).target; }

bool MidiMap::handleCc(int cc, double normalized, MidiEvent& out) noexcept
{
  if (cc < 0 || cc >= 128)
    return false;
  const auto idx = static_cast<std::size_t>(cc);
  const bool high = normalized >= 0.5;
  const bool rising = high && !mHigh[idx];
  mHigh[idx] = high;

  // MIDI learn: the first CC to arrive takes the armed target.
  std::int32_t armed = mLearn.load(std::memory_order_acquire);
  if (armed != kEmpty && mLearn.compare_exchange_strong(armed, kEmpty, std::memory_order_acq_rel))
  {
    const MidiAssignment a = decode(armed);
    for (auto& s : mSlots) // clearTarget, inlined: lock-free, no allocation
      if (decode(s.load(std::memory_order_acquire)).target == a.target)
        s.store(kEmpty, std::memory_order_release);
    mSlots[idx].store(armed, std::memory_order_release);
    out = {MidiEvent::Kind::Learned, a.target, cc, 0.0, 0.0};
    return true;
  }

  const MidiAssignment a = decode(mSlots[idx].load(std::memory_order_acquire));
  if (a.target == kMidiNoTarget)
    return false;

  if (a.target < 0) // actions fire on a press only
  {
    if (!rising)
      return false;
    out = {MidiEvent::Kind::Action, a.target, cc, 1.0, 0.0};
    return true;
  }

  const ParamSpec& spec = paramSpec(a.target);
  if (spec.unit == ParamUnit::Boolean && a.toggle)
  {
    if (!rising)
      return false;
    out = {MidiEvent::Kind::ToggleParam, a.target, cc, 0.0, 0.0};
    return true;
  }
  out = {MidiEvent::Kind::SetParam, a.target, cc, plainFromNormalized(spec, normalized), 0.0};
  return true;
}

} // namespace ironclad
