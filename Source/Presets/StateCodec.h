#pragma once

// The plugin's DAW-session state ("chunk"), independent of iPlug2 so every format version
// can be unit-tested.
//
// Byte layouts (little-endian; int32 = 4 bytes, double = 8 bytes, str = int32 length + bytes):
//   M1  : double[N]                                  (no header; params until the end)
//   v1  : str "IRCL", int32 1, double[6], str capture
//   v2  : str "IRCL", int32 2, int32 N, double[N], str capture, str irA, str irB
//   v3  : str "IRCL", int32 3, str json               (written by this build)
// v3 keys parameters and MIDI targets by stable id, so it survives parameter-table growth.

#include <cstdint>
#include <string>
#include <vector>

#include "Midi/MidiMap.h"
#include "Presets/PresetData.h"

namespace ironclad
{

struct SessionState
{
  PresetData preset;          // current sound: params + files (+ the loaded preset's name)
  std::string presetPath;     // library file of the loaded preset ("" = none / factory)
  bool presetModified = false;
  MidiMapData midi;
  int sourceVersion = 3;      // set by decodeState: 0 = M1, 1, 2, 3
};

constexpr int kStateVersion = 3;

std::vector<std::uint8_t> encodeState(const SessionState& s);

/// Decodes from data[startPos..size). Returns the end position, or -1 if the data is not a
/// state this build understands (corrupt, or written by a newer version).
int decodeState(const std::uint8_t* data, int size, int startPos, SessionState& out);

// MIDI map <-> JSON text (also used for the global default MIDI map file).
std::string midiMapToJson(const MidiMapData& m);
MidiMapData midiMapFromJson(const std::string& text); // tolerant: skips unknown entries

} // namespace ironclad
