#include "Cabinet/Resample.h"

#include <algorithm>
#include <cmath>

namespace ironclad
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 9.0;      // Kaiser shape: ~ -90 dB stop band
constexpr double kZeroCrossings = 32.0;
constexpr double kPassband = 0.95; // fraction of the lower Nyquist kept

double besselI0(double x)
{
  double sum = 1.0, term = 1.0;
  const double q = x * x / 4.0;
  for (int k = 1; k < 64; ++k)
  {
    term *= q / (static_cast<double>(k) * static_cast<double>(k));
    sum += term;
    if (term < sum * 1e-17)
      break;
  }
  return sum;
}
} // namespace

std::vector<double> resampleOffline(const std::vector<double>& input, double fromRate, double toRate)
{
  if (input.empty() || fromRate <= 0.0 || toRate <= 0.0 || fromRate == toRate)
    return input;

  const double ratio = toRate / fromRate;
  const double cutoff = std::min(1.0, ratio) * kPassband; // relative to the input Nyquist
  const double halfWidth = kZeroCrossings / cutoff;       // in input samples
  const double i0Beta = besselI0(kBeta);
  const auto inLen = static_cast<long>(input.size());
  const auto outLen = static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * ratio));

  std::vector<double> out(outLen, 0.0);
  for (std::size_t n = 0; n < outLen; ++n)
  {
    const double t = static_cast<double>(n) / ratio; // position in input samples
    const long first = std::max(0L, static_cast<long>(std::ceil(t - halfWidth)));
    const long last = std::min(inLen - 1, static_cast<long>(std::floor(t + halfWidth)));
    double acc = 0.0;
    for (long k = first; k <= last; ++k)
    {
      const double d = t - static_cast<double>(k);
      const double r = d / halfWidth;
      if (r <= -1.0 || r >= 1.0)
        continue;
      const double window = besselI0(kBeta * std::sqrt(1.0 - r * r)) / i0Beta;
      const double x = kPi * cutoff * d;
      const double sinc = (std::abs(x) < 1e-12) ? 1.0 : std::sin(x) / x;
      acc += input[static_cast<std::size_t>(k)] * cutoff * sinc * window;
    }
    out[n] = acc;
  }
  return out;
}

} // namespace ironclad
