#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "AllocationTracking.h"
#include "Audio/Engine.h"
#include "Audio/SmoothedValue.h"

using namespace ironclad;

namespace
{
constexpr double kSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
constexpr int kBlockSizes[] = {1, 16, 32, 64, 128, 256, 512, 1024, 2048};
struct Layout
{
  int in, out;
};
constexpr Layout kLayouts[] = {{1, 1}, {1, 2}, {2, 2}};

struct Buffers
{
  std::vector<std::vector<double>> in, out;
  std::vector<const double*> inPtr;
  std::vector<double*> outPtr;

  Buffers(int nIn, int nOut, int frames)
  : in(static_cast<std::size_t>(nIn), std::vector<double>(static_cast<std::size_t>(frames), 0.0))
  , out(static_cast<std::size_t>(nOut), std::vector<double>(static_cast<std::size_t>(frames), 0.0))
  {
    for (auto& v : in) inPtr.push_back(v.data());
    for (auto& v : out) outPtr.push_back(v.data());
  }

  // Pointers offset by `start` samples (used to feed variable-size blocks).
  std::vector<const double*> inAt(int start) const
  {
    std::vector<const double*> p;
    for (auto& v : in) p.push_back(v.data() + start);
    return p;
  }
  std::vector<double*> outAt(int start)
  {
    std::vector<double*> p;
    for (auto& v : out) p.push_back(v.data() + start);
    return p;
  }
};

void fillConstant(Buffers& b, double value)
{
  for (auto& ch : b.in) std::fill(ch.begin(), ch.end(), value);
}

/// Runs `total` frames through the engine in blocks of `block` (or random sizes if block<=0).
void run(Engine& e, Buffers& b, int total, int block, std::mt19937& rng, int maxBlock)
{
  int pos = 0;
  std::uniform_int_distribution<int> dist(1, maxBlock * 2); // deliberately exceeds maxBlock too
  while (pos < total)
  {
    int n = block > 0 ? block : dist(rng);
    n = std::min(n, total - pos);
    auto ip = b.inAt(pos);
    auto op = b.outAt(pos);
    e.process(ip.data(), static_cast<int>(ip.size()), op.data(), static_cast<int>(op.size()), n);
    pos += n;
  }
}
} // namespace

TEST_CASE("SmoothedValue reaches its target exactly after the ramp")
{
  SmoothedValue s;
  s.reset(48000.0, 10.0, 0.0); // 480 samples
  CHECK(s.rampSamples() == 480);
  s.setTarget(1.0);
  double prev = 0.0;
  for (int i = 0; i < 480; ++i)
  {
    const double v = s.next();
    CHECK(v >= prev);
    CHECK(v - prev <= 1.0 / 480.0 + 1e-12);
    prev = v;
  }
  CHECK(s.current() == 1.0);
  CHECK_FALSE(s.isSmoothing());
}

TEST_CASE("SmoothedValue retargeting mid-ramp stays continuous")
{
  SmoothedValue s;
  s.reset(48000.0, 10.0, 0.0);
  s.setTarget(1.0);
  for (int i = 0; i < 100; ++i) s.next();
  const double before = s.current();
  s.setTarget(-1.0);
  const double after = s.next();
  CHECK(std::abs(after - before) <= 2.0 / 480.0 + 1e-12);
}

TEST_CASE("Unity gain passes audio unchanged — all sample rates, block sizes and layouts")
{
  std::mt19937 rng(1234);
  for (double sr : kSampleRates)
    for (int block : kBlockSizes)
      for (auto layout : kLayouts)
      {
        CAPTURE(sr);
        CAPTURE(block);
        CAPTURE(layout.in);
        CAPTURE(layout.out);
        Engine e;
        e.prepare({sr, block, layout.in, layout.out});
        const int total = 4096;
        Buffers b(layout.in, layout.out, total);
        std::uniform_real_distribution<double> noise(-1.0, 1.0);
        for (int i = 0; i < total; ++i)
        {
          const double x = noise(rng);
          for (auto& ch : b.in) ch[static_cast<std::size_t>(i)] = x; // dual-mono
        }
        run(e, b, total, block, rng, block);
        double maxErr = 0.0;
        for (const auto& ch : b.out)
          for (int i = 0; i < total; ++i)
            maxErr = std::max(maxErr, std::abs(ch[static_cast<std::size_t>(i)] - b.in[0][static_cast<std::size_t>(i)]));
        CHECK(maxErr < 1e-12);
      }
}

TEST_CASE("Stereo input is averaged to mono (no +6 dB jump)")
{
  Engine e;
  e.prepare({48000.0, 64, 2, 2});
  Buffers b(2, 2, 64);
  std::fill(b.in[0].begin(), b.in[0].end(), 1.0);
  std::fill(b.in[1].begin(), b.in[1].end(), 0.0);
  e.process(b.inPtr.data(), 2, b.outPtr.data(), 2, 64);
  CHECK(b.out[0][10] == doctest::Approx(0.5));
  CHECK(b.out[1][10] == doctest::Approx(0.5));
}

TEST_CASE("Gain change is smoothed (no step) and settles to the exact dB value")
{
  std::mt19937 rng(99);
  for (double sr : kSampleRates)
    for (int block : {1, 64, 512, 0 /* random sizes */})
    {
      CAPTURE(sr);
      CAPTURE(block);
      const int maxBlock = block > 0 ? block : 512;
      Engine e;
      e.prepare({sr, maxBlock, 1, 2});
      const int total = static_cast<int>(sr * 0.2); // 200 ms
      Buffers b(1, 2, total);
      fillConstant(b, 0.5);

      e.setParameter(kParamInputGain, 12.0);
      e.setParameter(kParamOutputGain, -6.0);
      run(e, b, total, block, rng, maxBlock);

      const double expected = 0.5 * dbToGain(12.0) * dbToGain(-6.0);
      CHECK(b.out[0].back() == doctest::Approx(expected).epsilon(1e-9));
      CHECK(b.out[1].back() == doctest::Approx(expected).epsilon(1e-9));

      // Largest per-sample change must be far below the full step (≈0.5 -> 1.0).
      const int ramp = static_cast<int>(std::lround(sr * 0.030));
      double maxDelta = 0.0;
      for (int i = 1; i < total; ++i)
        maxDelta = std::max(maxDelta, std::abs(b.out[0][static_cast<std::size_t>(i)] - b.out[0][static_cast<std::size_t>(i - 1)]));
      CHECK(maxDelta < 4.0 * (expected + 0.5) / ramp);
    }
}

TEST_CASE("Out-of-range parameter values are clamped")
{
  Engine e;
  e.setParameter(kParamInputGain, 1e9);
  CHECK(e.getParameter(kParamInputGain) == paramSpec(kParamInputGain).maxValue);
  e.setParameter(kParamOutputGain, -1e9);
  CHECK(e.getParameter(kParamOutputGain) == paramSpec(kParamOutputGain).minValue);
  e.setParameter(999, 1.0); // ignored, must not crash
}

TEST_CASE("In-place processing (input and output buffers alias)")
{
  Engine e;
  e.prepare({48000.0, 128, 2, 2});
  std::vector<double> l(128, 0.25), r(128, 0.25);
  double* io[2] = {l.data(), r.data()};
  e.process(io, 2, io, 2, 128);
  CHECK(l[50] == doctest::Approx(0.25));
  CHECK(r[50] == doctest::Approx(0.25));
}

TEST_CASE("Null / disconnected channels and unprepared engine are safe")
{
  Engine e;
  std::vector<double> out(32, 7.0);
  double* outs[1] = {out.data()};
  e.process(nullptr, 0, outs, 1, 32); // not prepared -> silence
  CHECK(out[5] == 0.0);

  e.prepare({48000.0, 32, 2, 2});
  const double* ins[2] = {nullptr, nullptr};
  std::fill(out.begin(), out.end(), 7.0);
  e.process(ins, 2, outs, 1, 32);
  CHECK(out[5] == 0.0);
}

TEST_CASE("process() performs zero heap allocations")
{
  for (int block : {1, 64, 512, 4096})
  {
    CAPTURE(block);
    Engine e;
    e.prepare({48000.0, 512, 2, 2});
    Buffers b(2, 2, 4096);
    fillConstant(b, 0.1);

    test::ScopedAllocationCheck check;
    for (int rep = 0; rep < 20; ++rep)
    {
      e.setParameter(kParamInputGain, rep % 2 ? 6.0 : -6.0);
      int pos = 0;
      while (pos < 4096)
      {
        const int n = std::min(block, 4096 - pos);
        const double* ip[2] = {b.in[0].data() + pos, b.in[1].data() + pos};
        double* op[2] = {b.out[0].data() + pos, b.out[1].data() + pos};
        e.process(ip, 2, op, 2, n);
        pos += n;
      }
    }
    const auto counts = check.finish();
    CHECK(counts.allocations == 0);
    CHECK(counts.deallocations == 0);
  }
}

TEST_CASE("Allocation tracker itself detects allocations")
{
  // volatile sink stops the compiler from eliding the new/delete pair
  static int* volatile sink = nullptr;
  test::ScopedAllocationCheck check;
  sink = new int(5);
  delete sink;
  const auto counts = check.finish();
  CHECK(counts.allocations == 1);
  CHECK(counts.deallocations == 1);
}
