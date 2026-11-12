#pragma once

// Offline, high-quality sample-rate conversion for impulse responses (not real-time).
// Kaiser-windowed sinc interpolation; the cutoff follows the lower of the two Nyquist
// frequencies so downsampling does not alias.

#include <vector>

namespace ironclad
{

std::vector<double> resampleOffline(const std::vector<double>& input, double fromRate, double toRate);

} // namespace ironclad
