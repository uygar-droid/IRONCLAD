#pragma once

// MIDI CC assignments (MIDI learn).
//
// Each of the 128 CC numbers can drive one target: a parameter (expression pedal -> wah,
// volume; footswitch -> effect on/off) or an action (tap tempo, next/previous preset).
//
// Threading: handleCc() runs on the audio thread and is lock-free and allocation-free; the
// assignment table is a set of atomics so the UI thread can change it at any time.
// Everything else is UI-thread only.

#include <array>
#include <atomic>
#include <cstdint>
#include <map>

namespace ironclad
{

enum MidiTarget : int
{
  kMidiNoTarget = -1000,
  kMidiActionTapTempo = -1,
  kMidiActionPresetNext = -2,
  kMidiActionPresetPrev = -3,
  kMidiFirstAction = -3, // lowest valid action value
};

const char* midiActionName(int target) noexcept; // "Tap Tempo", ... ; nullptr for parameters

struct MidiAssignment
{
  int target = kMidiNoTarget;
  /// Boolean targets only. false: CC >= 64 is on, < 64 is off (latching footswitch).
  /// true: every press (CC rising through 64) flips the parameter (momentary footswitch).
  bool toggle = false;
  bool operator==(const MidiAssignment&) const = default;
};

struct MidiMapData
{
  std::map<int, MidiAssignment> cc; // CC number (0..127) -> assignment
};

struct MidiEvent
{
  enum class Kind : std::uint8_t
  {
    SetParam,    // set `target` to `value` (plain units)
    ToggleParam, // flip boolean parameter `target`
    Action,      // run action `target`
    Learned      // CC `cc` was just assigned to `target`
  };
  Kind kind = Kind::SetParam;
  int target = kMidiNoTarget;
  int cc = -1;
  double value = 0.0;
  double timeSeconds = 0.0; // filled in by the caller (steady clock) for tap tempo
};

class MidiMap
{
public:
  MidiMap();

  // ---- UI thread ----
  void setData(const MidiMapData& data);
  MidiMapData data() const;
  void assign(int cc, MidiAssignment a);
  void clearCc(int cc);
  void clearTarget(int target);
  void clearAll();
  /// CC driving `target`, or -1.
  int ccForTarget(int target) const;
  MidiAssignment assignment(int cc) const;

  /// The next CC that arrives is assigned to `target` (replacing any other CC for it).
  void armLearn(int target, bool toggle);
  void cancelLearn();
  int learnTarget() const; // kMidiNoTarget when not learning

  // ---- audio thread ----
  /// Feed one CC (normalised 0..1). Returns true and fills `out` when something should happen.
  bool handleCc(int cc, double normalized, MidiEvent& out) noexcept;

private:
  static std::int32_t encode(MidiAssignment a) noexcept;
  static MidiAssignment decode(std::int32_t v) noexcept;

  std::array<std::atomic<std::int32_t>, 128> mSlots;
  std::array<bool, 128> mHigh{}; // audio thread: last state per CC (edge detection)
  std::atomic<std::int32_t> mLearn;
};

} // namespace ironclad
