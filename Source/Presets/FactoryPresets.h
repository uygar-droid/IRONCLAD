#pragma once

// Factory presets: effect / gain-staging templates WITHOUT captures or IRs (the user's own
// amp capture and cabinet IRs stay loaded when one is selected). Compiled in, read-only.

#include <vector>

#include "Presets/PresetData.h"

namespace ironclad
{

const std::vector<PresetData>& factoryPresets();

} // namespace ironclad
