#pragma once

// Zero-latency, non-uniformly partitioned convolution (mono).
//
//   * Taps [0, 64) are applied with a direct-form FIR per sample (no delay).
//   * Later taps are split into stages with growing partition sizes:
//       [64, 1024)    partitions of 64   (FFT 128),  computed at the end of each frame
//       [1024, 8192)  partitions of 512  (FFT 1024), work spread over the following frame
//       [8192, L)     partitions of 4096 (FFT 8192), work spread over the following frame
//     A stage whose first tap is one partition (64) or two partitions (spread stages) into the
//     IR only needs completed input frames, so the convolver adds no latency at any host block
//     size, and the large FFTs never land in one host block as a burst.
//   * Spectra of real signals are Hermitian; only bins 0..N/2 are multiplied.
//
// Cost for a 1 s IR at 48 kHz is ~25 complex multiplies per sample instead of ~750 with a
// single 64-sample partitioning. prepare() allocates; process()/reset() are real-time safe.

#include <complex>
#include <memory>
#include <vector>

#include "Cabinet/Fft.h"

namespace ironclad
{

class Convolver
{
public:
  static constexpr int kHead = 64;

  Convolver();
  ~Convolver();
  Convolver(Convolver&&) noexcept;
  Convolver& operator=(Convolver&&) noexcept;

  void prepare(const std::vector<double>& impulseResponse);
  void reset() noexcept;
  void process(const double* input, double* output, int numFrames) noexcept;

  std::size_t irLength() const noexcept { return mLength; }
  int numStages() const noexcept;

private:
  struct Stage;

  std::size_t mLength = 0;
  std::vector<double> mHead; // kHead taps (zero-padded)
  int mHeadTaps = 0;
  std::vector<double> mHistory; // last kHead inputs, doubled for contiguous reads
  int mHistPos = 0;
  std::vector<std::unique_ptr<Stage>> mStages;
};

} // namespace ironclad
