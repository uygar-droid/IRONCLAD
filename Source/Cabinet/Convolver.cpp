#include "Cabinet/Convolver.h"

#include <algorithm>

namespace ironclad
{

void Convolver::prepare(const std::vector<double>& ir)
{
  constexpr int B = kPartition;
  constexpr int N = 2 * B;
  mLength = ir.size();
  mHeadTaps = static_cast<int>(std::min<std::size_t>(ir.size(), B));
  mHead.assign(B, 0.0);
  std::copy(ir.begin(), ir.begin() + mHeadTaps, mHead.begin());
  mHistory.assign(2 * B, 0.0);

  mFft.prepare(N);
  mNumPartitions = ir.size() > static_cast<std::size_t>(B)
                     ? static_cast<int>((ir.size() - B + B - 1) / B)
                     : 0;
  mPartitionSpectra.assign(static_cast<std::size_t>(mNumPartitions) * N, {0.0, 0.0});
  for (int p = 0; p < mNumPartitions; ++p)
  {
    std::complex<double>* spec = &mPartitionSpectra[static_cast<std::size_t>(p) * N];
    const std::size_t start = static_cast<std::size_t>(B) * (p + 1);
    for (int i = 0; i < B && start + i < ir.size(); ++i)
      spec[i] = ir[start + static_cast<std::size_t>(i)];
    mFft.forward(spec);
  }
  mFdl.assign(static_cast<std::size_t>(std::max(1, mNumPartitions)) * N, {0.0, 0.0});
  mWork.assign(N, {0.0, 0.0});
  mAccum.assign(N, {0.0, 0.0});
  mTailOut.assign(B, 0.0);
  mPos = 0;
  mFdlIndex = 0;
}

void Convolver::reset() noexcept
{
  std::fill(mHistory.begin(), mHistory.end(), 0.0);
  std::fill(mFdl.begin(), mFdl.end(), std::complex<double>(0.0, 0.0));
  std::fill(mTailOut.begin(), mTailOut.end(), 0.0);
  mPos = 0;
  mFdlIndex = 0;
}

void Convolver::process(const double* input, double* output, int numFrames) noexcept
{
  constexpr int B = kPartition;
  if (mLength == 0)
  {
    std::fill(output, output + numFrames, 0.0);
    return;
  }
  const double* head = mHead.data();
  double* hist = mHistory.data();
  for (int n = 0; n < numFrames; ++n)
  {
    const double x = input[n];
    hist[B + mPos] = x;
    // Direct-form head: y = sum_k h[k] * x[t - k]; x[t - k] lives at hist[B + mPos - k].
    const double* xp = hist + B + mPos;
    double acc = 0.0;
    for (int k = 0; k < mHeadTaps; ++k)
      acc += head[k] * xp[-k];
    output[n] = acc + mTailOut[static_cast<std::size_t>(mPos)];
    if (++mPos == B)
      onFrameComplete();
  }
}

void Convolver::onFrameComplete() noexcept
{
  constexpr int B = kPartition;
  constexpr int N = 2 * B;
  mPos = 0;

  if (mNumPartitions > 0)
  {
    // X_k = FFT([x_{k-1} | x_k]) into the frequency-domain delay line.
    std::complex<double>* slot = &mFdl[static_cast<std::size_t>(mFdlIndex) * N];
    for (int i = 0; i < N; ++i)
      slot[i] = mHistory[static_cast<std::size_t>(i)];
    mFft.forward(slot);

    // Tail output for the NEXT frame: sum_{j=1..P} X_{k+1-j} * H_j  (H_j = partition j-1 here).
    std::fill(mAccum.begin(), mAccum.end(), std::complex<double>(0.0, 0.0));
    int idx = mFdlIndex;
    for (int p = 0; p < mNumPartitions; ++p)
    {
      const std::complex<double>* x = &mFdl[static_cast<std::size_t>(idx) * N];
      const std::complex<double>* h = &mPartitionSpectra[static_cast<std::size_t>(p) * N];
      for (int i = 0; i < N; ++i)
        mAccum[static_cast<std::size_t>(i)] += x[i] * h[i];
      idx = (idx == 0) ? mNumPartitions - 1 : idx - 1;
    }
    mFft.inverse(mAccum.data());
    const double scale = 1.0 / N;
    for (int i = 0; i < B; ++i)
      mTailOut[static_cast<std::size_t>(i)] = mAccum[static_cast<std::size_t>(B + i)].real() * scale;

    mFdlIndex = (mFdlIndex + 1) % mNumPartitions;
  }

  // Current frame becomes the previous frame.
  std::copy(mHistory.begin() + B, mHistory.end(), mHistory.begin());
}

} // namespace ironclad
