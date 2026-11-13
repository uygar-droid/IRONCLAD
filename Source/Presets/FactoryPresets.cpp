#include "Presets/FactoryPresets.h"

#include <initializer_list>
#include <utility>

namespace ironclad
{
namespace
{
PresetData make(const char* category, const char* name, std::initializer_list<std::pair<const char*, double>> values)
{
  PresetData p;
  p.name = name;
  p.category = category;
  p.templateOnly = true;
  for (const auto& [id, v] : values)
    p.params[id] = v;
  return p;
}
} // namespace

const std::vector<PresetData>& factoryPresets()
{
  // Parameters not listed take their defaults (all effects off, unity gain).
  static const std::vector<PresetData> presets = {
    make("Basic", "Init", {}),
    make("Clean", "Studio Clean",
         {{"comp.enabled", 1}, {"comp.threshold_db", -24}, {"comp.ratio", 3}, {"comp.attack_ms", 10},
          {"comp.release_ms", 150}, {"comp.makeup_db", 4}, {"comp.mix", 80}, {"reverb.enabled", 1},
          {"reverb.type", 0}, {"reverb.decay_s", 1.0}, {"reverb.mix", 15}}),
    make("Clean", "Chorus Clean",
         {{"comp.enabled", 1}, {"comp.threshold_db", -22}, {"comp.ratio", 4}, {"comp.makeup_db", 3},
          {"chorus.enabled", 1}, {"chorus.rate_hz", 0.6}, {"chorus.depth", 45}, {"chorus.mix", 45},
          {"reverb.enabled", 1}, {"reverb.type", 1}, {"reverb.decay_s", 2.2}, {"reverb.mix", 22}}),
    make("Clean", "Funk Wah",
         {{"comp.enabled", 1}, {"comp.threshold_db", -26}, {"comp.ratio", 6}, {"comp.attack_ms", 3},
          {"comp.makeup_db", 6}, {"wah.enabled", 1}, {"wah.resonance", 70}, {"reverb.enabled", 1},
          {"reverb.type", 0}, {"reverb.decay_s", 0.8}, {"reverb.mix", 12}}),
    make("Crunch", "Edge of Breakup",
         {{"boost.enabled", 1}, {"boost.gain_db", 4}, {"eq.enabled", 1}, {"eq.mid_db", 1.5}, {"eq.mid_hz", 900},
          {"reverb.enabled", 1}, {"reverb.type", 0}, {"reverb.decay_s", 1.2}, {"reverb.mix", 15}}),
    make("Crunch", "Pushed Crunch",
         {{"od.enabled", 1}, {"od.drive", 25}, {"od.tone", 55}, {"od.level_db", 4}, {"delay.enabled", 1},
          {"delay.type", 1}, {"delay.time_ms", 120}, {"delay.feedback", 10}, {"delay.mix", 15}}),
    make("Lead", "Classic Lead",
         {{"od.enabled", 1}, {"od.drive", 40}, {"od.tone", 60}, {"od.level_db", 3}, {"delay.enabled", 1},
          {"delay.type", 1}, {"delay.sync", 1}, {"delay.division", 4}, {"delay.feedback", 30}, {"delay.mix", 22},
          {"reverb.enabled", 1}, {"reverb.type", 1}, {"reverb.decay_s", 2.4}, {"reverb.mix", 18}}),
    make("Lead", "Fuzz Lead",
         {{"fuzz.enabled", 1}, {"fuzz.fuzz", 70}, {"fuzz.tone", 55}, {"fuzz.level_db", -3}, {"delay.enabled", 1},
          {"delay.type", 0}, {"delay.time_ms", 380}, {"delay.feedback", 28}, {"delay.mix", 20}}),
    make("High Gain", "Tight Rhythm",
         {{"gate.enabled", 1}, {"gate.threshold_db", -55}, {"gate.release_ms", 60}, {"boost.enabled", 1},
          {"boost.gain_db", 8}, {"eq.enabled", 1}, {"eq.low_db", -1.5}, {"eq.mid_db", 1}, {"eq.mid_hz", 750},
          {"eq.high_db", -1}}),
    make("High Gain", "Modern Lead",
         {{"gate.enabled", 1}, {"gate.threshold_db", -60}, {"boost.enabled", 1}, {"boost.gain_db", 6},
          {"delay.enabled", 1}, {"delay.type", 2}, {"delay.sync", 1}, {"delay.division", 7}, {"delay.feedback", 25},
          {"delay.mix", 18}, {"reverb.enabled", 1}, {"reverb.type", 2}, {"reverb.decay_s", 1.8}, {"reverb.mix", 15}}),
    make("Ambient", "Ambient Swells",
         {{"comp.enabled", 1}, {"comp.threshold_db", -28}, {"comp.ratio", 4}, {"comp.makeup_db", 5},
          {"delay.enabled", 1}, {"delay.type", 2}, {"delay.sync", 1}, {"delay.division", 4}, {"delay.feedback", 55},
          {"delay.mix", 35}, {"delay.highcut_hz", 5000}, {"reverb.enabled", 1}, {"reverb.type", 1},
          {"reverb.decay_s", 6.0}, {"reverb.predelay_ms", 40}, {"reverb.mix", 45}}),
    make("Ambient", "Slapback Plate",
         {{"delay.enabled", 1}, {"delay.type", 1}, {"delay.time_ms", 110}, {"delay.feedback", 8}, {"delay.mix", 25},
          {"reverb.enabled", 1}, {"reverb.type", 2}, {"reverb.decay_s", 1.6}, {"reverb.mix", 20}}),
  };
  return presets;
}

} // namespace ironclad
