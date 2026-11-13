#include "Cabinet/Convolver.h"

#include <algorithm>

namespace ironclad
{

/// One uniformly partitioned overlap-save stage covering taps [begin, end).
///
/// Immediate stage (begin == B): at the end of input frame k it computes the output of
/// frame k+1 at once (small B only, so the burst is small).
/// Spread stage (begin == 2B): the output of frame k+2 is computed during frame k+1, a few
/// operations per sample (input FFT, then the multiply-accumulates, then the inverse FFT on
/// separate samples), so large partitions never cause a CPU burst in one host block.
struct Convolver::Stage
{
  int B = 0;           // partition size
  int N = 0;           // FFT size (2B)
  int bins = 0;        // N/2 + 1
  int numPartitions = 0;
  bool spread = false;
  Fft fft;
  std::vector<std::complex<double>> partitions; // P x bins
  std::vector<std::complex<double>> fdl;        // P x bins ring of input spectra
  int fdlIndex = 0;                             // slot of the newest input spectrum
  std::vector<double> window;                   // 2B: previous frame | current frame
  int pos = 0;
  std::vector<double> out, outNext;             // B each
  std::vector<std::complex<double>> fwd, acc;   // N each

  // Spread job state
  bool jobActive = false;
  int jobPhase = 0;            // 0 = input FFT, 1 = MACs, 2 = inverse FFT
  std::size_t macDone = 0, macTotal = 0, macPerTick = 1;

  void prepare(const std::vector<double>& ir, int partition, std::size_t begin, std::size_t end, bool spreadWork)
  {
    B = partition;
    N = 2 * B;
    bins = N / 2 + 1;
    spread = spreadWork;
    numPartitions = static_cast<int>((end - begin + B - 1) / B);
    fft.prepare(N);
    fwd.assign(static_cast<std::size_t>(N), {0.0, 0.0});
    acc.assign(static_cast<std::size_t>(N), {0.0, 0.0});
    partitions.assign(static_cast<std::size_t>(numPartitions) * bins, {0.0, 0.0});
    for (int p = 0; p < numPartitions; ++p)
    {
      std::fill(fwd.begin(), fwd.end(), std::complex<double>(0.0, 0.0));
      const std::size_t start = begin + static_cast<std::size_t>(p) * B;
      for (int i = 0; i < B && start + i < end; ++i)
        fwd[static_cast<std::size_t>(i)] = ir[start + static_cast<std::size_t>(i)];
      fft.forward(fwd.data());
      std::copy(fwd.begin(), fwd.begin() + bins, partitions.begin() + static_cast<std::ptrdiff_t>(p) * bins);
    }
    fdl.assign(static_cast<std::size_t>(numPartitions) * bins, {0.0, 0.0});
    window.assign(static_cast<std::size_t>(N), 0.0);
    out.assign(static_cast<std::size_t>(B), 0.0);
    outNext.assign(static_cast<std::size_t>(B), 0.0);
    macTotal = static_cast<std::size_t>(numPartitions) * bins;
    macPerTick = (macTotal + static_cast<std::size_t>(B) - 5) / static_cast<std::size_t>(B - 4);
    reset();
  }

  void reset() noexcept
  {
    std::fill(fdl.begin(), fdl.end(), std::complex<double>(0.0, 0.0));
    std::fill(window.begin(), window.end(), 0.0);
    std::fill(out.begin(), out.end(), 0.0);
    std::fill(outNext.begin(), outNext.end(), 0.0);
    fdlIndex = numPartitions - 1;
    pos = 0;
    jobActive = false;
  }

  double tick(double x) noexcept
  {
    window[static_cast<std::size_t>(B + pos)] = x;
    const double y = out[static_cast<std::size_t>(pos)];
    ++pos;
    if (jobActive)
      advanceJob();
    if (pos == B)
      frameComplete();
    return y;
  }

  // ---- steps shared by both modes ----
  void inputFft() noexcept
  {
    fdlIndex = (fdlIndex + 1) % numPartitions;
    std::complex<double>* slot = &fdl[static_cast<std::size_t>(fdlIndex) * bins];
    fft.forward(fwd.data());
    std::copy(fwd.begin(), fwd.begin() + bins, slot);
    std::fill(acc.begin(), acc.end(), std::complex<double>(0.0, 0.0));
    macDone = 0;
  }
  void macs(std::size_t count) noexcept
  {
    const std::size_t stop = std::min(macTotal, macDone + count);
    while (macDone < stop)
    {
      const auto p = static_cast<int>(macDone / static_cast<std::size_t>(bins));
      const auto i0 = static_cast<int>(macDone % static_cast<std::size_t>(bins));
      const int i1 = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(bins), static_cast<std::size_t>(i0) + (stop - macDone)));
      int idx = fdlIndex - p;
      if (idx < 0)
        idx += numPartitions;
      const std::complex<double>* xs = &fdl[static_cast<std::size_t>(idx) * bins];
      const std::complex<double>* hs = &partitions[static_cast<std::size_t>(p) * bins];
      for (int i = i0; i < i1; ++i)
        acc[static_cast<std::size_t>(i)] += xs[i] * hs[i];
      macDone += static_cast<std::size_t>(i1 - i0);
    }
  }
  void inverseInto(std::vector<double>& dst) noexcept
  {
    for (int i = 1; i < N / 2; ++i)
      acc[static_cast<std::size_t>(N - i)] = std::conj(acc[static_cast<std::size_t>(i)]);
    fft.inverse(acc.data());
    const double scale = 1.0 / N;
    for (int i = 0; i < B; ++i)
      dst[static_cast<std::size_t>(i)] = acc[static_cast<std::size_t>(B + i)].real() * scale;
  }

  void advanceJob() noexcept
  {
    switch (jobPhase)
    {
    case 0: inputFft(); jobPhase = 1; break;
    case 1:
      macs(macPerTick);
      if (macDone >= macTotal)
        jobPhase = 2;
      break;
    default:
      inverseInto(outNext);
      jobActive = false;
      break;
    }
  }

  void frameComplete() noexcept
  {
    pos = 0;
    for (int i = 0; i < N; ++i)
      fwd[static_cast<std::size_t>(i)] = window[static_cast<std::size_t>(i)];
    std::copy(window.begin() + B, window.end(), window.begin());

    if (!spread)
    {
      inputFft();
      macs(macTotal);
      inverseInto(out); // output of the next frame
      return;
    }
    // Spread: finish the previous job (normally already done), publish its result as the
    // next frame's output, then start computing the frame after that.
    while (jobActive)
      advanceJob();
    out.swap(outNext);
    jobActive = true;
    jobPhase = 0;
  }
};

Convolver::Convolver() = default;
Convolver::~Convolver() = default;
Convolver::Convolver(Convolver&&) noexcept = default;
Convolver& Convolver::operator=(Convolver&&) noexcept = default;

int Convolver::numStages() const noexcept { return static_cast<int>(mStages.size()); }

void Convolver::prepare(const std::vector<double>& ir)
{
  mLength = ir.size();
  mHeadTaps = static_cast<int>(std::min<std::size_t>(ir.size(), kHead));
  mHead.assign(kHead, 0.0);
  std::copy(ir.begin(), ir.begin() + mHeadTaps, mHead.begin());
  mHistory.assign(2 * kHead, 0.0);
  mHistPos = 0;

  // Stage layout: (partition, first tap, end tap). Each first tap equals its partition size.
  // Immediate stage of 64 from tap 64; spread stages start two partitions in.
  struct Layout { int partition; std::size_t begin, end; bool spread; };
  const Layout layout[] = {{64, 64, 1024, false}, {512, 1024, 8192, true}, {4096, 8192, static_cast<std::size_t>(-1), true}};
  mStages.clear();
  for (const auto& l : layout)
  {
    if (ir.size() <= l.begin)
      break;
    auto stage = std::make_unique<Stage>();
    stage->prepare(ir, l.partition, l.begin, std::min(ir.size(), l.end), l.spread);
    mStages.push_back(std::move(stage));
  }
}

void Convolver::reset() noexcept
{
  std::fill(mHistory.begin(), mHistory.end(), 0.0);
  mHistPos = 0;
  for (auto& s : mStages)
    s->reset();
}

void Convolver::process(const double* input, double* output, int numFrames) noexcept
{
  if (mLength == 0)
  {
    std::fill(output, output + numFrames, 0.0);
    return;
  }
  const double* head = mHead.data();
  for (int n = 0; n < numFrames; ++n)
  {
    const double x = input[n];
    // History is written twice (pos and pos + kHead) so the newest kHead samples are always
    // contiguous ending at index pos + kHead.
    mHistory[static_cast<std::size_t>(mHistPos)] = x;
    mHistory[static_cast<std::size_t>(mHistPos + kHead)] = x;
    const double* xp = mHistory.data() + mHistPos + kHead; // xp[0] = x[t], xp[-k] = x[t-k]
    double acc = 0.0;
    for (int k = 0; k < mHeadTaps; ++k)
      acc += head[k] * xp[-k];
    mHistPos = (mHistPos + 1) & (kHead - 1);
    for (auto& s : mStages)
      acc += s->tick(x);
    output[n] = acc;
  }
}

} // namespace ironclad
