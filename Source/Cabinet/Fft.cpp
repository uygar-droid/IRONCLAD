#include "Cabinet/Fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ironclad
{

void Fft::prepare(int size)
{
  if (size < 2 || (size & (size - 1)) != 0)
    throw std::invalid_argument("FFT size must be a power of two");
  mSize = size;
  mTwiddles.resize(static_cast<std::size_t>(size / 2));
  const double twoPi = 6.283185307179586476925286766559;
  for (int k = 0; k < size / 2; ++k)
    mTwiddles[static_cast<std::size_t>(k)] = std::polar(1.0, -twoPi * k / size);

  int bits = 0;
  while ((1 << bits) < size)
    ++bits;
  mBitReverse.resize(static_cast<std::size_t>(size));
  for (int i = 0; i < size; ++i)
  {
    int r = 0;
    for (int b = 0; b < bits; ++b)
      if (i & (1 << b))
        r |= 1 << (bits - 1 - b);
    mBitReverse[static_cast<std::size_t>(i)] = r;
  }
}

void Fft::transform(std::complex<double>* data, bool inverse) const noexcept
{
  const int n = mSize;
  for (int i = 0; i < n; ++i)
  {
    const int j = mBitReverse[static_cast<std::size_t>(i)];
    if (i < j)
      std::swap(data[i], data[j]);
  }
  for (int len = 2; len <= n; len <<= 1)
  {
    const int half = len >> 1;
    const int step = n / len;
    for (int start = 0; start < n; start += len)
    {
      for (int k = 0; k < half; ++k)
      {
        std::complex<double> w = mTwiddles[static_cast<std::size_t>(k * step)];
        if (inverse)
          w = std::conj(w);
        const std::complex<double> u = data[start + k];
        const std::complex<double> v = data[start + k + half] * w;
        data[start + k] = u + v;
        data[start + k + half] = u - v;
      }
    }
  }
}

} // namespace ironclad
