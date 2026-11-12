#pragma once

// Zero-latency partitioned convolution (mono).
//
//   * Taps [0, B) are applied with a direct-form FIR per sample (no delay).
//   * Taps [B, L) are applied with uniformly partitioned overlap-save FFT convolution
//     (partition size B, FFT size 2B). Because the tail starts at tap B, its result for
//     the frame being played only depends on input frames that are already complete,
//     so it is computed one frame ahead and adds no latency.
//
// prepare() allocates; process()/reset() are real-time safe and accept any block size.

#include <complex>
#include <vector>

#include "Cabinet/Fft.h"

namespace ironclad
{

class Convolver
{
public:
  static constexpr int kPartition = 64; // B

  void prepare(const std::vector<double>& impulseResponse);
  void reset() noexcept;
  void process(const double* input, double* output, int numFrames) noexcept;

  std::size_t irLength() const noexcept { return mLength; }
  int numTailPartitions() const noexcept { return mNumPartitions; }

private:
  void onFrameComplete() noexcept;

  std::size_t mLength = 0;
  std::vector<double> mHead;        // B taps (zero-padded)
  int mHeadTaps = 0;
  std::vector<double> mHistory;     // 2B: previous frame | current frame
  int mPos = 0;                     // position inside the current frame

  int mNumPartitions = 0;           // tail partitions (taps B..L)
  Fft mFft;
  std::vector<std::complex<double>> mPartitionSpectra; // P x 2B
  std::vector<std::complex<double>> mFdl;               // P x 2B ring of input spectra
  int mFdlIndex = 0;
  std::vector<std::complex<double>> mWork;               // 2B
  std::vector<std::complex<double>> mAccum;              // 2B
  std::vector<double> mTailOut;                          // B, output of tail for the current frame
};

} // namespace ironclad
