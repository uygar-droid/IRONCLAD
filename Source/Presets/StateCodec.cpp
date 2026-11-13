#include "Presets/StateCodec.h"

#include <cstring>
#include <stdexcept>

#include "json.hpp"

namespace ironclad
{
namespace
{
using nlohmann::json;
constexpr const char* kMagic = "IRCL";

void putBytes(std::vector<std::uint8_t>& out, const void* p, std::size_t n)
{
  const auto* b = static_cast<const std::uint8_t*>(p);
  out.insert(out.end(), b, b + n);
}
void putInt(std::vector<std::uint8_t>& out, std::int32_t v) { putBytes(out, &v, sizeof v); }
void putStr(std::vector<std::uint8_t>& out, const std::string& s)
{
  putInt(out, static_cast<std::int32_t>(s.size()));
  putBytes(out, s.data(), s.size());
}

struct Reader
{
  const std::uint8_t* data;
  int size;
  int pos;
  bool get(void* dst, int n)
  {
    if (pos < 0 || n < 0 || pos + n > size)
      return false;
    std::memcpy(dst, data + pos, static_cast<std::size_t>(n));
    pos += n;
    return true;
  }
  bool getInt(std::int32_t& v) { return get(&v, sizeof v); }
  bool getDouble(double& v) { return get(&v, sizeof v); }
  bool getStr(std::string& s)
  {
    std::int32_t len = 0;
    const int start = pos;
    if (!getInt(len) || len < 0 || pos + len > size)
    {
      pos = start;
      return false;
    }
    s.assign(reinterpret_cast<const char*>(data + pos), static_cast<std::size_t>(len));
    pos += len;
    return true;
  }
};

std::string targetToString(int target)
{
  switch (target)
  {
  case kMidiActionTapTempo: return "action:tap_tempo";
  case kMidiActionPresetNext: return "action:preset_next";
  case kMidiActionPresetPrev: return "action:preset_prev";
  default: return target >= 0 && target < kNumParams ? std::string(paramSpec(target).id) : std::string{};
  }
}

int targetFromString(const std::string& s)
{
  if (s == "action:tap_tempo") return kMidiActionTapTempo;
  if (s == "action:preset_next") return kMidiActionPresetNext;
  if (s == "action:preset_prev") return kMidiActionPresetPrev;
  const int idx = findParamIndex(s);
  return idx >= 0 ? idx : kMidiNoTarget;
}

json midiToJ(const MidiMapData& m)
{
  json arr = json::array();
  for (const auto& [cc, a] : m.cc)
  {
    const std::string t = targetToString(a.target);
    if (!t.empty())
      arr.push_back({{"cc", cc}, {"target", t}, {"toggle", a.toggle}});
  }
  return arr;
}

MidiMapData midiFromJ(const json& arr)
{
  MidiMapData m;
  if (!arr.is_array())
    return m;
  for (const auto& e : arr)
  {
    if (!e.is_object())
      continue;
    const int cc = e.value("cc", -1);
    const int target = targetFromString(e.value("target", std::string{}));
    if (cc >= 0 && cc < 128 && target != kMidiNoTarget)
      m.cc[cc] = {target, e.value("toggle", false)};
  }
  return m;
}

/// Index-ordered parameter values (legacy formats) -> id-keyed map.
void readLegacyParams(Reader& r, int count, PresetData& p)
{
  for (int i = 0; count < 0 || i < count; ++i)
  {
    double v = 0.0;
    if (!r.getDouble(v))
      break;
    if (i < kNumParams)
      p.params[std::string(paramSpec(i).id)] = v;
  }
}

std::optional<FileRef> pathRef(const std::string& path)
{
  if (path.empty())
    return std::nullopt;
  FileRef f;
  f.path = path;
  const auto slash = path.find_last_of("/\\");
  f.fileName = slash == std::string::npos ? path : path.substr(slash + 1);
  return f;
}
} // namespace

std::string midiMapToJson(const MidiMapData& m) { return midiToJ(m).dump(2); }

MidiMapData midiMapFromJson(const std::string& text)
{
  try
  {
    const json j = json::parse(text);
    return midiFromJ(j.is_object() ? j.value("midi", json::array()) : j);
  }
  catch (const std::exception&)
  {
    return {};
  }
}

std::vector<std::uint8_t> encodeState(const SessionState& s)
{
  json j;
  j["format"] = "IRONCLAD state";
  j["preset"] = json::parse(presetToJson(s.preset));
  j["presetPath"] = s.presetPath;
  j["presetModified"] = s.presetModified;
  j["midi"] = midiToJ(s.midi);

  std::vector<std::uint8_t> out;
  putStr(out, kMagic);
  putInt(out, kStateVersion);
  putStr(out, j.dump());
  return out;
}

int decodeState(const std::uint8_t* data, int size, int startPos, SessionState& out)
{
  if (data == nullptr || size < 0 || startPos < 0 || startPos > size)
    return -1;
  out = SessionState{};
  Reader r{data, size, startPos};

  std::string magic;
  if (!r.getStr(magic) || magic != kMagic)
  {
    // Milestone-1 state: raw parameter values until the end (a whole number of doubles).
    const int bytes = size - startPos;
    if (bytes <= 0 || bytes % static_cast<int>(sizeof(double)) != 0)
      return -1;
    r.pos = startPos;
    readLegacyParams(r, -1, out.preset);
    out.sourceVersion = 0;
    return r.pos;
  }

  std::int32_t version = 0;
  if (!r.getInt(version) || version < 1 || version > kStateVersion)
    return -1;
  out.sourceVersion = version;

  if (version <= 2)
  {
    std::int32_t numParams = 6; // v1 stored exactly 6 parameters (M2)
    if (version == 2 && (!r.getInt(numParams) || numParams < 0 || numParams > 100000))
      return -1;
    readLegacyParams(r, numParams, out.preset);
    std::string capture, ir[2];
    r.getStr(capture);
    if (version == 2)
      for (auto& p : ir)
        r.getStr(p);
    out.preset.capture = pathRef(capture);
    out.preset.ir[0] = pathRef(ir[0]);
    out.preset.ir[1] = pathRef(ir[1]);
    return r.pos;
  }

  std::string text;
  if (!r.getStr(text))
    return -1;
  try
  {
    const json j = json::parse(text);
    if (!j.is_object() || j.value("format", std::string{}) != "IRONCLAD state" || !j.contains("preset"))
      return -1;
    out.preset = presetFromJson(j["preset"].dump());
    out.presetPath = j.value("presetPath", std::string{});
    out.presetModified = j.value("presetModified", false);
    out.midi = midiFromJ(j.value("midi", json::array()));
  }
  catch (const std::exception&)
  {
    return -1;
  }
  return r.pos;
}

} // namespace ironclad
