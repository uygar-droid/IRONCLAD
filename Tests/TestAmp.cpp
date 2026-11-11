// Milestone 2 tests: NAM capture loading, switching and real-time safety through the
// full engine (loader thread + audio-side AmpStage).

#include "doctest/doctest.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "AllocationTracking.h"
#include "Audio/Engine.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

using namespace ironclad;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

std::string modelPath(const char* name)
{
  return (std::filesystem::path(IRONCLAD_NAM_EXAMPLE_MODELS_DIR) / name).string();
}

struct Rig
{
  Engine engine;
  int block;
  std::vector<double> zeros, in, out;
  long phase = 0;
  double sr;

  Rig(double sampleRate, int blockSize)
  : block(blockSize)
  , zeros(static_cast<std::size_t>(blockSize), 0.0)
  , in(static_cast<std::size_t>(blockSize), 0.0)
  , out(static_cast<std::size_t>(blockSize), 0.0)
  , sr(sampleRate)
  {
    engine.prepare({sampleRate, blockSize, 1, 1});
  }

  void run(const double* src)
  {
    const double* i[1] = {src};
    double* o[1] = {out.data()};
    engine.process(i, 1, o, 1, block);
  }
  void silence() { run(zeros.data()); }
  void reprepare(double sampleRate, int blockSize)
  {
    sr = sampleRate;
    block = blockSize;
    zeros.assign(static_cast<std::size_t>(blockSize), 0.0);
    in.assign(static_cast<std::size_t>(blockSize), 0.0);
    out.assign(static_cast<std::size_t>(blockSize), 0.0);
    engine.prepare({sampleRate, blockSize, 1, 1});
  }
  void sine(double amp = 0.3, double freq = 220.0)
  {
    for (int k = 0; k < block; ++k, ++phase)
      in[static_cast<std::size_t>(k)] = amp * std::sin(kTwoPi * freq * static_cast<double>(phase) / sr);
    run(in.data());
  }
  /// Waits for the loader, then runs one block so the audio side picks the capture up.
  void activate()
  {
    REQUIRE(engine.waitForLoader(20000));
    silence();
  }
  void finishCrossfade()
  {
    for (int k = 0; k < 200 && engine.ampStage().isCrossfading(); ++k)
      silence();
    REQUIRE_FALSE(engine.ampStage().isCrossfading());
  }
};

double rms(const std::vector<double>& v)
{
  double s = 0.0;
  for (double x : v) s += x * x;
  return std::sqrt(s / static_cast<double>(v.size()));
}

std::string writeTempFile(const char* name, const char* content)
{
  const auto p = std::filesystem::temp_directory_path() / name;
  FILE* f = std::fopen(p.string().c_str(), "wb");
  REQUIRE(f != nullptr);
  std::fputs(content, f);
  std::fclose(f);
  return p.string();
}
} // namespace

TEST_CASE("Capture loads in the background and reports metadata")
{
  Rig rig(48000.0, 256);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  CHECK(rig.engine.captureStatus().state == CaptureStatus::State::Loading);
  rig.activate();

  const auto st = rig.engine.captureStatus();
  REQUIRE(st.state == CaptureStatus::State::Ready);
  CHECK(st.hasActive);
  CHECK(st.active.architecture == "WaveNet");
  CHECK(st.active.sampleRate == doctest::Approx(48000.0));
  CHECK(st.active.fileName == "wavenet.nam");
  CHECK_FALSE(st.active.displayName.empty());
  CHECK_FALSE(st.resampling);
  CHECK(rig.engine.ampStage().activationCount() == 1);
  CHECK(rig.engine.latencySamples() == 0);
  CHECK(rig.engine.captureStatePath() == modelPath("wavenet.nam"));
}

TEST_CASE("Engine output equals NAM Core's own output for the same capture (bit-accurate path)")
{
  const int block = 128;
  Rig rig(48000.0, block);
  rig.engine.setParameter(kParamAmpNormalize, 1.0);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate(); // 1 silent block processed by the new capture

  auto ref = nam::get_dsp(std::filesystem::path(modelPath("wavenet.nam")));
  ref->Reset(48000.0, block);
  std::vector<double> rin(static_cast<std::size_t>(block), 0.0), rout(static_cast<std::size_t>(block), 0.0);
  double* ri[1] = {rin.data()};
  double* ro[1] = {rout.data()};
  ref->process(ri, ro, block); // mirror the activation block

  int silentBlocks = 0;
  while (rig.engine.ampStage().isCrossfading())
  {
    rig.silence();
    std::fill(rin.begin(), rin.end(), 0.0);
    ref->process(ri, ro, block);
    ++silentBlocks;
  }
  CHECK(silentBlocks > 0);

  const auto st = rig.engine.captureStatus();
  const double norm = st.active.loudnessDb ? std::pow(10.0, (-18.0 - *st.active.loudnessDb) / 20.0) : 1.0;

  double maxErr = 0.0, peak = 0.0;
  for (int b = 0; b < 100; ++b)
  {
    rig.sine();
    std::copy(rig.in.begin(), rig.in.end(), rin.begin());
    ref->process(ri, ro, block);
    for (int k = 0; k < block; ++k)
    {
      maxErr = std::max(maxErr, std::abs(rig.out[static_cast<std::size_t>(k)] - rout[static_cast<std::size_t>(k)] * norm));
      peak = std::max(peak, std::abs(rig.out[static_cast<std::size_t>(k)]));
    }
  }
  CHECK(peak > 1e-3);
  CHECK(maxErr < 1e-9);
}

TEST_CASE("Captures run at every supported host sample rate (resampling when needed)")
{
  // Reference loudness of a 220 Hz sine at the capture's own rate.
  double refRms = 0.0;
  {
    Rig rig(48000.0, 256);
    rig.engine.loadCapture(modelPath("wavenet.nam"));
    rig.activate();
    rig.finishCrossfade();
    std::vector<double> acc;
    for (int b = 0; b < 200; ++b) { rig.sine(); if (b > 50) acc.insert(acc.end(), rig.out.begin(), rig.out.end()); }
    refRms = rms(acc);
  }
  REQUIRE(refRms > 1e-4);

  for (double sr : {44100.0, 88200.0, 96000.0, 176400.0, 192000.0})
  {
    CAPTURE(sr);
    Rig rig(sr, 256);
    rig.engine.loadCapture(modelPath("wavenet.nam"));
    rig.activate();
    rig.finishCrossfade();
    const auto st = rig.engine.captureStatus();
    CHECK(st.resampling);
    CHECK(rig.engine.latencySamples() > 0);
    std::vector<double> acc;
    bool finite = true;
    const int blocks = static_cast<int>(sr * 1.0 / 256);
    for (int b = 0; b < blocks; ++b)
    {
      rig.sine();
      for (double x : rig.out) finite = finite && std::isfinite(x);
      if (b > blocks / 4) acc.insert(acc.end(), rig.out.begin(), rig.out.end());
    }
    CHECK(finite);
    const double diffDb = 20.0 * std::log10(rms(acc) / refRms);
    CAPTURE(diffDb);
    CHECK(std::abs(diffDb) < 1.0);
  }
}

TEST_CASE("Invalid, missing and wrong-type files are reported and the current capture keeps playing")
{
  Rig rig(48000.0, 256);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate();
  REQUIRE(rig.engine.captureStatus().state == CaptureStatus::State::Ready);
  const auto activations = rig.engine.ampStage().activationCount();

  SUBCASE("damaged file")
  {
    rig.engine.loadCapture(writeTempFile("ironclad_damaged.nam", "{ \"version\": \"0.5.4\", \"architecture\": "));
  }
  SUBCASE("missing file")
  {
    rig.engine.loadCapture((std::filesystem::temp_directory_path() / "ironclad_missing_capture.nam").string());
  }
  SUBCASE("not a .nam file")
  {
    rig.engine.loadCapture(writeTempFile("ironclad_notes.txt", "hello"));
  }
  SUBCASE("unknown architecture")
  {
    rig.engine.loadCapture(writeTempFile("ironclad_unknown_arch.nam",
                                         "{\"version\":\"0.5.4\",\"architecture\":\"Banana\",\"config\":{},\"weights\":[]}"));
  }

  REQUIRE(rig.engine.waitForLoader(20000));
  for (int b = 0; b < 4; ++b) rig.sine();
  const auto st = rig.engine.captureStatus();
  CHECK(st.state == CaptureStatus::State::Error);
  CHECK_FALSE(st.errorMessage.empty());
  CHECK(st.hasActive);
  CHECK(st.active.fileName == "wavenet.nam");                     // previous capture still active
  CHECK(rig.engine.ampStage().activationCount() == activations);  // nothing new reached the audio thread
  CHECK(rig.engine.captureStatePath() == modelPath("wavenet.nam")); // project keeps the working capture
}

TEST_CASE("A missing capture at project load is kept in the state (not silently dropped)")
{
  Rig rig(48000.0, 256);
  const std::string missing = (std::filesystem::temp_directory_path() / "moved_away" / "Lead.nam").string();
  rig.engine.loadCapture(missing);
  REQUIRE(rig.engine.waitForLoader(20000));
  const auto st = rig.engine.captureStatus();
  CHECK(st.state == CaptureStatus::State::Error);
  CHECK_FALSE(st.hasActive);
  CHECK(st.errorMessage.find("not found") != std::string::npos);
  CHECK(rig.engine.captureStatePath() == missing);
  // Audio still passes (dry) and is finite.
  rig.sine();
  CHECK(std::abs(rig.out[10] - rig.in[10]) < 1e-12);
}

TEST_CASE("Clearing the capture crossfades back to the dry signal")
{
  Rig rig(48000.0, 256);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate();
  rig.finishCrossfade();
  rig.engine.clearCapture();
  rig.activate();
  rig.finishCrossfade();
  CHECK(rig.engine.captureStatus().state == CaptureStatus::State::Empty);
  CHECK(rig.engine.captureStatePath().empty());
  rig.sine();
  for (int k = 0; k < rig.block; ++k)
    CHECK(rig.out[static_cast<std::size_t>(k)] == doctest::Approx(rig.in[static_cast<std::size_t>(k)]).epsilon(1e-12));
}

TEST_CASE("Switching captures during playback has no discontinuity")
{
  const int block = 64;
  Rig rig(48000.0, block);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate();
  rig.finishCrossfade();

  auto maxDelta = [&](int blocks, std::vector<double>* collect = nullptr) {
    double prev = rig.out.back(), m = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
      rig.sine(0.3, 110.0);
      for (double x : rig.out) { m = std::max(m, std::abs(x - prev)); prev = x; }
      if (collect) collect->insert(collect->end(), rig.out.begin(), rig.out.end());
    }
    return m;
  };

  const double steadyA = maxDelta(300);
  rig.engine.loadCapture(modelPath("lstm.nam"));
  REQUIRE(rig.engine.waitForLoader(20000));
  const double during = maxDelta(100); // includes the 25 ms crossfade
  const double steadyB = maxDelta(300);
  CAPTURE(steadyA);
  CAPTURE(during);
  CAPTURE(steadyB);
  CHECK(rig.engine.ampStage().activationCount() == 2);
  CHECK(during <= 1.25 * std::max(steadyA, steadyB) + 1e-6);
}

TEST_CASE("Amp On/Off is smoothed and returns the dry signal when off")
{
  Rig rig(48000.0, 256);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate();
  rig.finishCrossfade();
  rig.engine.setParameter(kParamAmpEnabled, 0.0);
  for (int b = 0; b < 10; ++b) rig.sine();
  for (int k = 0; k < rig.block; ++k)
    CHECK(rig.out[static_cast<std::size_t>(k)] == doctest::Approx(rig.in[static_cast<std::size_t>(k)]).epsilon(1e-12));
}

TEST_CASE("Rapid requests coalesce: the last requested capture wins")
{
  Rig rig(48000.0, 256);
  for (int i = 0; i < 10; ++i)
    rig.engine.loadCapture(modelPath(i % 2 ? "lstm.nam" : "wavenet.nam"));
  rig.engine.loadCapture(modelPath("lstm.nam"));
  REQUIRE(rig.engine.waitForLoader(20000));
  for (int b = 0; b < 50; ++b) rig.silence();
  const auto st = rig.engine.captureStatus();
  CHECK(st.state == CaptureStatus::State::Ready);
  CHECK(st.active.fileName == "lstm.nam");
}

TEST_CASE("Sample-rate change re-prepares the active capture and an in-flight load")
{
  Rig rig(48000.0, 256);
  rig.engine.loadCapture(modelPath("wavenet.nam"));
  rig.activate();
  rig.finishCrossfade();

  // Host switches to 96 kHz / 512 while a different capture is being loaded.
  rig.engine.loadCapture(modelPath("lstm.nam"));
  rig.reprepare(96000.0, 512);
  REQUIRE(rig.engine.waitForLoader(20000));
  for (int b = 0; b < 100; ++b) rig.sine();

  const auto st = rig.engine.captureStatus();
  CHECK(st.state == CaptureStatus::State::Ready);
  CHECK(st.active.fileName == "lstm.nam");
  CHECK(rig.engine.ampStage().activationCount() == 2);
  CHECK(rig.engine.latencySamples() > 0); // 48 kHz capture at a 96 kHz host -> resampling latency
  bool finite = true;
  double peak = 0.0;
  for (double x : rig.out) { finite = finite && std::isfinite(x); peak = std::max(peak, std::abs(x)); }
  CHECK(finite);
  CHECK(peak > 1e-4);

  // And back to 44.1 kHz with the capture already active.
  rig.reprepare(44100.0, 64);
  for (int b = 0; b < 200; ++b) rig.sine();
  finite = true;
  for (double x : rig.out) finite = finite && std::isfinite(x);
  CHECK(finite);
}

TEST_CASE("Stress: capture switches every few milliseconds with zero allocations on the audio thread")
{
  for (double sr : {48000.0, 44100.0}) // 44.1 kHz exercises the resampler path
  {
    CAPTURE(sr);
    const int block = 64;
    Rig rig(sr, block);
    rig.engine.loadCapture(modelPath("wavenet.nam"));
    rig.activate();
    rig.finishCrossfade();

    std::atomic<bool> stop{false};
    std::thread ui([&] {
      int i = 0;
      while (!stop.load())
      {
        rig.engine.loadCapture(modelPath(i++ % 2 ? "lstm.nam" : "wavenet.nam"));
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
    });

    bool finite = true;
    test::ScopedAllocationCheck check; // counts allocations made by THIS (audio) thread only
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500))
    {
      rig.sine();
      for (double x : rig.out) finite = finite && std::isfinite(x);
    }
    const auto counts = check.finish();
    stop = true;
    ui.join();

    const auto activations = rig.engine.ampStage().activationCount();
    CAPTURE(activations);
    CHECK(activations > 10);
    CHECK(finite);
    CHECK(counts.allocations == 0);
    CHECK(counts.deallocations == 0);
  }
}
