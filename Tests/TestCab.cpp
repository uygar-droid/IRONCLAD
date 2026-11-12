// Milestone 3 tests: cabinet IR loading and processing through the full engine.

#include "doctest/doctest.h"

#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "AllocationTracking.h"
#include "Audio/Engine.h"
#include "WavTestUtils.h"

using namespace ironclad;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

std::vector<double> decayingIr(std::size_t n, double seed)
{
  std::vector<double> h(n);
  for (std::size_t i = 0; i < n; ++i)
    h[i] = std::sin(seed * static_cast<double>(i + 1)) * std::exp(-static_cast<double>(i) / (0.2 * static_cast<double>(n)));
  return h;
}

std::string makeIrFile(const std::string& name, const std::vector<std::vector<double>>& ch, int rate = 48000)
{
  test::WavSpec spec;
  spec.sampleRate = rate;
  spec.bits = 32;
  spec.isFloat = true;
  return test::writeFile(name, test::makeWav(ch, spec));
}

double unitEnergyGain(const std::vector<std::vector<double>>& ch)
{
  double e = 0.0;
  for (const auto& c : ch)
    for (double v : c)
      e += static_cast<double>(static_cast<float>(v)) * static_cast<float>(v);
  return 1.0 / std::sqrt(e / static_cast<double>(ch.size()));
}

std::string writeTempNotes() { return test::writeFile("ironclad_notes_ir.txt", {'h', 'i'}); }

struct CabRig
{
  Engine engine;
  int block;
  double sr;
  std::vector<double> in, l, r;

  CabRig(double sampleRate, int blockSize)
  : block(blockSize)
  , sr(sampleRate)
  , in(static_cast<std::size_t>(blockSize), 0.0)
  , l(static_cast<std::size_t>(blockSize), 0.0)
  , r(static_cast<std::size_t>(blockSize), 0.0)
  {
    engine.prepare({sampleRate, blockSize, 1, 2});
  }
  void run()
  {
    const double* i[1] = {in.data()};
    double* o[2] = {l.data(), r.data()};
    engine.process(i, 1, o, 2, block);
  }
  void silence(int blocks = 1)
  {
    std::fill(in.begin(), in.end(), 0.0);
    for (int b = 0; b < blocks; ++b) run();
  }
  void settle()
  {
    REQUIRE(engine.waitForLoader(20000));
    silence(static_cast<int>(sr * 0.15 / block) + 2); // crossfades and wet ramp finished, tails flushed
  }
  /// Feeds an impulse and records `frames` output samples per channel.
  void impulseResponse(int frames, std::vector<double>& outL, std::vector<double>& outR)
  {
    outL.clear();
    outR.clear();
    bool first = true;
    while (static_cast<int>(outL.size()) < frames)
    {
      std::fill(in.begin(), in.end(), 0.0);
      if (first) in[0] = 1.0;
      first = false;
      run();
      outL.insert(outL.end(), l.begin(), l.end());
      outR.insert(outR.end(), r.begin(), r.end());
    }
  }
};
} // namespace

TEST_CASE("A mono IR is applied exactly (unit-energy normalised) to both channels")
{
  const auto h = decayingIr(1500, 0.37);
  const std::vector<std::vector<double>> ch{h};
  CabRig rig(48000.0, 64);
  rig.engine.loadIr(0, makeIrFile("ironclad_mono_ir.wav", ch));
  rig.settle();

  const auto st = rig.engine.irStatus().slot[0];
  REQUIRE(st.state == IrSlotStatus::State::Ready);
  CHECK(st.active.fileChannels == 1);
  CHECK(st.active.fileSampleRate == 48000.0);
  CHECK(st.active.thumbnail.size() == static_cast<std::size_t>(ImpulseResponse::kThumbnailPoints));

  std::vector<double> L, R;
  rig.impulseResponse(1600, L, R);
  const double g = unitEnergyGain(ch);
  double err = 0.0;
  for (std::size_t i = 0; i < h.size(); ++i)
  {
    const double expected = static_cast<double>(static_cast<float>(h[i])) * g;
    err = std::max(err, std::abs(L[i] - expected));
    err = std::max(err, std::abs(R[i] - expected));
  }
  CHECK(err < 1e-9);
  CHECK(std::abs(L[1550]) < 1e-12);
  CHECK(rig.engine.latencySamples() == 0);
}

TEST_CASE("A stereo IR keeps its two channels")
{
  const std::vector<std::vector<double>> ch{decayingIr(800, 0.3), decayingIr(800, 0.9)};
  CabRig rig(48000.0, 128);
  rig.engine.loadIr(0, makeIrFile("ironclad_stereo_ir.wav", ch));
  rig.settle();
  std::vector<double> L, R;
  rig.impulseResponse(900, L, R);
  const double g = unitEnergyGain(ch);
  double errL = 0.0, errR = 0.0;
  for (std::size_t i = 0; i < 800; ++i)
  {
    errL = std::max(errL, std::abs(L[i] - static_cast<float>(ch[0][i]) * g));
    errR = std::max(errR, std::abs(R[i] - static_cast<float>(ch[1][i]) * g));
  }
  CHECK(errL < 1e-9);
  CHECK(errR < 1e-9);
}

TEST_CASE("IRs recorded at another rate are resampled to the host rate")
{
  const std::vector<std::vector<double>> ch{decayingIr(2205, 0.2)}; // 50 ms at 44.1 kHz
  for (double hostRate : {48000.0, 96000.0, 44100.0})
  {
    CAPTURE(hostRate);
    CabRig rig(hostRate, 256);
    rig.engine.loadIr(0, makeIrFile("ironclad_441_ir.wav", ch, 44100));
    rig.settle();
    const auto st = rig.engine.irStatus().slot[0];
    REQUIRE(st.state == IrSlotStatus::State::Ready);
    CHECK(st.active.fileSampleRate == 44100.0);
    CHECK(st.active.durationMs == doctest::Approx(50.0));
    // Output energy is ~unit (normalised) regardless of rate.
    std::vector<double> L, R;
    rig.impulseResponse(static_cast<int>(hostRate * 0.08), L, R);
    double e = 0.0;
    for (double v : L) e += v * v;
    CHECK(e == doctest::Approx(1.0).epsilon(0.02));
  }
}

TEST_CASE("Pan, invert, level, blend and slot on/off")
{
  // A = unit impulse at 0, B = unit impulse at 10 (both already unit energy).
  std::vector<double> a(32, 0.0), b(32, 0.0);
  a[0] = 1.0;
  b[10] = 1.0;
  CabRig rig(48000.0, 64);
  rig.engine.loadIr(0, makeIrFile("ironclad_irA.wav", {a}));
  rig.engine.loadIr(1, makeIrFile("ironclad_irB.wav", {b}));
  rig.settle();
  std::vector<double> L, R;

  SUBCASE("blend 25 % B")
  {
    rig.engine.setParameter(kParamCabBlend, 25.0);
    rig.settle();
    rig.impulseResponse(64, L, R);
    CHECK(L[0] == doctest::Approx(0.75));
    CHECK(L[10] == doctest::Approx(0.25));
  }
  SUBCASE("B off -> A at full level")
  {
    rig.engine.setParameter(kParamCabBEnabled, 0.0);
    rig.settle();
    rig.impulseResponse(64, L, R);
    CHECK(L[0] == doctest::Approx(1.0));
    CHECK(std::abs(L[10]) < 1e-12);
  }
  SUBCASE("A panned hard left, B inverted, A at -6 dB")
  {
    rig.engine.setParameter(kParamCabAPan, -100.0);
    rig.engine.setParameter(kParamCabBInvert, 1.0);
    rig.engine.setParameter(kParamCabALevel, -6.0);
    rig.settle();
    rig.impulseResponse(64, L, R);
    CHECK(L[0] == doctest::Approx(0.5 * std::pow(10.0, -6.0 / 20.0)));
    CHECK(std::abs(R[0]) < 1e-12);
    CHECK(L[10] == doctest::Approx(-0.5));
    CHECK(R[10] == doctest::Approx(-0.5));
  }
  SUBCASE("cabinet off -> dry")
  {
    rig.engine.setParameter(kParamCabEnabled, 0.0);
    rig.settle();
    rig.impulseResponse(64, L, R);
    CHECK(L[0] == doctest::Approx(1.0));
    CHECK(std::abs(L[10]) < 1e-12);
  }
}

TEST_CASE("Cabinet HPF / LPF")
{
  std::vector<double> a(16, 0.0);
  a[0] = 1.0;
  auto toneLevel = [&](double hpf, double lpf, double freq) {
    CabRig rig(48000.0, 256);
    rig.engine.setParameter(kParamCabHighPass, hpf);
    rig.engine.setParameter(kParamCabLowPass, lpf);
    rig.engine.loadIr(0, makeIrFile("ironclad_irF.wav", {a}));
    rig.settle();
    double peak = 0.0;
    long n = 0;
    for (int b = 0; b < 200; ++b)
    {
      for (auto& x : rig.in) x = 0.5 * std::sin(kTwoPi * freq * static_cast<double>(n++) / 48000.0);
      rig.run();
      if (b > 100) for (double v : rig.l) peak = std::max(peak, std::abs(v));
    }
    return 20.0 * std::log10(peak / 0.5);
  };
  CHECK(std::abs(toneLevel(20.0, 20000.0, 100.0)) < 0.01);  // both bypassed at the extremes
  CHECK(std::abs(toneLevel(20.0, 20000.0, 10000.0)) < 0.01);
  CHECK(toneLevel(1000.0, 20000.0, 100.0) < -35.0);         // 12 dB/oct, 3.3 octaves below
  CHECK(toneLevel(20.0, 1000.0, 8000.0) < -30.0);
  CHECK(std::abs(toneLevel(1000.0, 20000.0, 1000.0) + 3.0) < 0.2); // -3 dB at the corner
}

TEST_CASE("Bad IR files are reported and the current IR keeps playing")
{
  std::vector<double> a(16, 0.0);
  a[0] = 1.0;
  CabRig rig(48000.0, 64);
  rig.engine.loadIr(0, makeIrFile("ironclad_good_ir.wav", {a}));
  rig.settle();
  const auto activations = rig.engine.cabStage().activationCount();

  std::string expectInMessage;
  SUBCASE("missing") { rig.engine.loadIr(0, "/nowhere/cab.wav"); expectInMessage = "not found"; }
  SUBCASE("not a wav") { rig.engine.loadIr(0, writeTempNotes()); expectInMessage = "Not a WAV"; }
  SUBCASE("damaged") { rig.engine.loadIr(0, test::writeFile("ironclad_damaged_ir.wav", {'R', 'I', 'F', 'F', 1, 2})); expectInMessage = "WAV"; }
  SUBCASE("silent") { rig.engine.loadIr(0, makeIrFile("ironclad_silent_ir.wav", {std::vector<double>(100, 0.0)})); expectInMessage = "silent"; }

  rig.settle();
  const auto st = rig.engine.irStatus().slot[0];
  CHECK(st.state == IrSlotStatus::State::Error);
  CAPTURE(st.errorMessage);
  CHECK(st.errorMessage.find(expectInMessage) != std::string::npos);
  CHECK(st.hasActive);
  CHECK(st.active.fileName == "ironclad_good_ir.wav");
  CHECK(rig.engine.cabStage().activationCount() == activations);
  std::vector<double> L, R;
  rig.impulseResponse(64, L, R);
  CHECK(L[0] == doctest::Approx(1.0));
}

TEST_CASE("Very long IRs are truncated to 1 s and flagged")
{
  CabRig rig(48000.0, 512);
  rig.engine.loadIr(1, makeIrFile("ironclad_long_ir.wav", {decayingIr(96000, 0.01)}));
  rig.settle();
  const auto st = rig.engine.irStatus().slot[1];
  REQUIRE(st.state == IrSlotStatus::State::Ready);
  CHECK(st.active.truncated);
  CHECK(st.active.durationMs == doctest::Approx(1000.0));
}

TEST_CASE("Clearing both slots returns to the dry signal; state paths follow the slots")
{
  std::vector<double> a(16, 0.0);
  a[3] = 1.0;
  CabRig rig(48000.0, 64);
  const auto path = makeIrFile("ironclad_clear_ir.wav", {a});
  rig.engine.loadIr(0, path);
  rig.settle();
  CHECK(rig.engine.irStatePath(0) == path);
  CHECK(rig.engine.irStatePath(1).empty());
  rig.engine.clearIr(0);
  rig.settle();
  CHECK(rig.engine.irStatePath(0).empty());
  std::vector<double> L, R;
  rig.impulseResponse(64, L, R);
  CHECK(L[0] == doctest::Approx(1.0));
  CHECK(std::abs(L[3]) < 1e-12);
}

TEST_CASE("Sample-rate change while an IR is active and while one is loading")
{
  const std::vector<std::vector<double>> ch{decayingIr(4800, 0.05)};
  CabRig rig(48000.0, 256);
  rig.engine.loadIr(0, makeIrFile("ironclad_sr_ir.wav", ch));
  rig.settle();
  rig.engine.loadIr(1, makeIrFile("ironclad_sr_ir2.wav", ch));
  rig.engine.prepare({96000.0, 512, 1, 2});
  rig.block = 512;
  rig.sr = 96000.0;
  rig.in.assign(512, 0.0);
  rig.l.assign(512, 0.0);
  rig.r.assign(512, 0.0);
  rig.settle();
  const auto st = rig.engine.irStatus();
  CHECK(st.slot[0].state == IrSlotStatus::State::Ready);
  CHECK(st.slot[1].state == IrSlotStatus::State::Ready);
  std::vector<double> L, R;
  rig.impulseResponse(20000, L, R);
  double e = 0.0;
  for (double v : L) e += v * v;
  CHECK(e == doctest::Approx(1.0).epsilon(0.05)); // A and B blended 50/50 (identical IRs)
}

TEST_CASE("Stress: IR switches every few ms with zero allocations on the audio thread")
{
  const auto p1 = makeIrFile("ironclad_stress1.wav", {decayingIr(9600, 0.11)});
  const auto p2 = makeIrFile("ironclad_stress2.wav", {decayingIr(2400, 0.7), decayingIr(2400, 0.5)}, 44100);
  CabRig rig(48000.0, 64);
  rig.engine.loadIr(0, p1);
  rig.engine.loadIr(1, p2);
  rig.settle();

  std::atomic<bool> stop{false};
  std::thread ui([&] {
    int i = 0;
    while (!stop.load())
    {
      rig.engine.loadIr(i % 2, (i / 2) % 2 ? p1 : p2);
      ++i;
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
  });
  bool finite = true;
  long n = 0;
  test::ScopedAllocationCheck check;
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500))
  {
    for (auto& x : rig.in) x = 0.3 * std::sin(kTwoPi * 220.0 * static_cast<double>(n++) / 48000.0);
    rig.run();
    for (int k = 0; k < rig.block; ++k) finite = finite && std::isfinite(rig.l[static_cast<std::size_t>(k)]) && std::isfinite(rig.r[static_cast<std::size_t>(k)]);
  }
  const auto counts = check.finish();
  stop = true;
  ui.join();
  const auto activations = rig.engine.cabStage().activationCount();
  CAPTURE(activations);
  CHECK(activations > 20);
  CHECK(finite);
  CHECK(counts.allocations == 0);
  CHECK(counts.deallocations == 0);
}
