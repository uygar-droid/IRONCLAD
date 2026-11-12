#pragma once

// Iterative radix-2 complex FFT with precomputed twiddles and bit-reversal table.
// prepare() allocates; forward()/inverse() are allocation-free and real-time safe.
// inverse() is unnormalised (caller scales by 1/N).

#include <complex>
#include <vector>

namespace ironclad
{

class Fft
{
public:
  void prepare(int size); // size must be a power of two >= 2
  int size() const noexcept { return mSize; }
  void forward(std::complex<double>* data) const noexcept { transform(data, false); }
  void inverse(std::complex<double>* data) const noexcept { transform(data, true); }

private:
  void transform(std::complex<double>* data, bool inverse) const noexcept;

  int mSize = 0;
  std::vector<std::complex<double>> mTwiddles; // e^{-2πik/N}, k < N/2
  std::vector<int> mBitReverse;
};

} // namespace ironclad
