#include "doctest/doctest.h"

#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "AllocationTracking.h"
#include "Cabinet/Convolver.h"
#include "Cabinet/Fft.h"
#include "Cabinet/Resample.h"
#include "Cabinet/WavReader.h"
#include "WavTestUtils.h"

using namespace ironclad;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

std::vector<double> directConvolution(const std::vector<double>& x, const std::vector<double>& h)
{
  std::vector<double> y(x.size(), 0.0);
  for (std::size_t n = 0; n < x.size(); ++n)
  {
    double acc = 0.0;
    const std::size_t kMax = std::min(h.size(), n + 1);
    for (std::size_t k = 0; k < kMax; ++k)
      acc += h[k] * x[n - k];
    y[n] = acc;
  }
  return y;
}
} // namespace

TEST_CASE("FFT matches a naive DFT and round-trips")
{
  for (int n : {2, 8, 128, 1024})
  {
    CAPTURE(n);
    Fft fft;
    fft.prepare(n);
    std::mt19937 rng(n);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<std::complex<double>> x(static_cast<std::size_t>(n));
    for (auto& v : x) v = {d(rng), d(rng)};
    auto X = x;
    fft.forward(X.data());
    double maxErr = 0.0;
    for (int k = 0; k < n; ++k)
    {
      std::complex<double> ref = 0.0;
      for (int t = 0; t < n; ++t)
        ref += x[static_cast<std::size_t>(t)] * std::polar(1.0, -kTwoPi * k * t / n);
      maxErr = std::max(maxErr, std::abs(ref - X[static_cast<std::size_t>(k)]));
    }
    CHECK(maxErr < 1e-9 * n);
    fft.inverse(X.data());
    double rtErr = 0.0;
    for (int t = 0; t < n; ++t)
      rtErr = std::max(rtErr, std::abs(X[static_cast<std::size_t>(t)] / static_cast<double>(n) - x[static_cast<std::size_t>(t)]));
    CHECK(rtErr < 1e-12);
  }
}

TEST_CASE("Convolver equals direct convolution for any IR length and any block size, with zero latency")
{
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> d(-1.0, 1.0);
  // Lengths cover every stage boundary of the non-uniform partitioning (64 / 512 / 4096).
  for (std::size_t L : {1u, 2u, 63u, 64u, 65u, 127u, 128u, 129u, 511u, 512u, 513u, 1000u, 4095u, 4096u, 4097u, 4800u, 20000u, 48000u})
  {
    std::vector<double> h(L);
    for (auto& v : h) v = d(rng) * std::exp(-static_cast<double>(&v - h.data()) / (0.3 * static_cast<double>(L) + 10.0));
    std::vector<double> x(L > 4800 ? L + 9000 : 6000);
    for (auto& v : x) v = d(rng);
    const auto ref = directConvolution(x, h);

    for (int block : L > 4800 ? std::vector<int>{1, 333, 4096, 0} : std::vector<int>{1, 7, 64, 100, 512, 0 /* random */})
    {
      CAPTURE(L);
      CAPTURE(block);
      Convolver conv;
      conv.prepare(h);
      std::vector<double> y(x.size(), 0.0);
      std::uniform_int_distribution<int> rb(1, 700);
      std::size_t pos = 0;
      while (pos < x.size())
      {
        int n = block > 0 ? block : rb(rng);
        n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(n), x.size() - pos));
        conv.process(x.data() + pos, y.data() + pos, n);
        pos += static_cast<std::size_t>(n);
      }
      double maxErr = 0.0;
      for (std::size_t i = 0; i < x.size(); ++i)
        maxErr = std::max(maxErr, std::abs(y[i] - ref[i]));
      CHECK(maxErr < 1e-10);
    }
  }
}

TEST_CASE("Convolver: an impulse reproduces the IR immediately (zero latency)")
{
  std::vector<double> h(300);
  for (std::size_t i = 0; i < h.size(); ++i) h[i] = std::sin(0.1 * static_cast<double>(i)) / (1.0 + 0.01 * static_cast<double>(i));
  Convolver conv;
  conv.prepare(h);
  std::vector<double> x(400, 0.0), y(400, 0.0);
  x[0] = 1.0;
  conv.process(x.data(), y.data(), 400);
  for (std::size_t i = 0; i < h.size(); ++i)
    CHECK(y[i] == doctest::Approx(h[i]).epsilon(1e-12).scale(1.0));
  CHECK(std::abs(y[350]) < 1e-12);
}

TEST_CASE("Convolver process() and reset() never allocate")
{
  std::vector<double> h(24000, 0.0);
  h[0] = 1.0;
  h[23999] = 0.5;
  Convolver conv;
  conv.prepare(h);
  std::vector<double> x(4096, 0.1), y(4096, 0.0);
  test::ScopedAllocationCheck check;
  for (int r = 0; r < 10; ++r) conv.process(x.data(), y.data(), 4096);
  conv.reset();
  conv.process(x.data(), y.data(), 33);
  CHECK(check.finish().allocations == 0);
}

TEST_CASE("Offline resampler preserves frequency and level")
{
  for (auto [from, to] : std::vector<std::pair<double, double>>{{44100, 48000}, {48000, 44100}, {48000, 96000}, {96000, 44100}, {48000, 192000}})
  {
    CAPTURE(from);
    CAPTURE(to);
    const double f = 1000.0;
    std::vector<double> x(static_cast<std::size_t>(from * 0.25));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.5 * std::sin(kTwoPi * f * static_cast<double>(i) / from);
    const auto y = resampleOffline(x, from, to);
    CHECK(static_cast<double>(y.size()) == doctest::Approx(x.size() * to / from).scale(0.0).epsilon(0.001));
    // Compare the middle of the signal (edges see the finite filter) with an ideal sine.
    double maxErr = 0.0;
    for (std::size_t i = y.size() / 4; i < 3 * y.size() / 4; ++i)
      maxErr = std::max(maxErr, std::abs(y[i] - 0.5 * std::sin(kTwoPi * f * static_cast<double>(i) / to)));
    CHECK(maxErr < 1e-3); // better than -54 dB relative to the 0.5 peak
  }
}

TEST_CASE("WAV reader: bit depths, float, extensible, extra chunks, stereo")
{
  std::vector<std::vector<double>> ch(2, std::vector<double>(100));
  for (int i = 0; i < 100; ++i)
  {
    ch[0][static_cast<std::size_t>(i)] = 0.9 * std::sin(0.3 * i);
    ch[1][static_cast<std::size_t>(i)] = -0.5 * std::cos(0.2 * i);
  }
  struct Case { test::WavSpec spec; double tol; };
  const Case cases[] = {
    {{44100, 16, false, false, false}, 2.0 / 32768.0}, // writer scales by 2^(n-1)-1, reader by 2^(n-1)
    {{48000, 24, false, false, true}, 2.0 / 8388608.0},
    {{96000, 32, false, true, false}, 1e-9},
    {{48000, 32, true, false, false}, 1e-7},
    {{48000, 64, true, true, true}, 1e-15},
  };
  for (const auto& c : cases)
  {
    CAPTURE(c.spec.bits);
    CAPTURE(c.spec.isFloat);
    CAPTURE(c.spec.extensible);
    const auto wav = parseWav(test::makeWav(ch, c.spec), "t.wav");
    CHECK(wav.sampleRate == c.spec.sampleRate);
    REQUIRE(wav.numChannels() == 2);
    REQUIRE(wav.numFrames() == 100);
    double err = 0.0;
    for (int k = 0; k < 2; ++k)
      for (int i = 0; i < 100; ++i)
        err = std::max(err, std::abs(wav.channels[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] - ch[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)]));
    CHECK(err <= c.tol * 1.01);
  }
}

TEST_CASE("WAV reader rejects broken files with a clear error")
{
  std::vector<std::vector<double>> ch(1, std::vector<double>(10, 0.1));
  auto good = test::makeWav(ch, {});

  CHECK_THROWS_AS(parseWav({}, "empty.wav"), AudioFileError);
  CHECK_THROWS_AS(parseWav({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '}, "x.wav"), AudioFileError);

  auto noData = good;
  noData.resize(good.size() - 30 - 8); // cut off the data chunk entirely
  CHECK_THROWS_AS(parseWav(noData, "nodata.wav"), AudioFileError);

  auto adpcm = good;
  adpcm[20] = 2; // format tag -> MS ADPCM
  CHECK_THROWS_AS(parseWav(adpcm, "adpcm.wav"), AudioFileError);

  std::vector<std::vector<double>> none(1);
  CHECK_THROWS_AS(parseWav(test::makeWav(none, {}), "silent.wav"), AudioFileError);

  CHECK_THROWS_AS(readWavFile("/definitely/not/here.wav"), AudioFileError);
}
