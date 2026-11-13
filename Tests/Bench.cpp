// CPU benchmark of the engine, stage by stage. Not a unit test: prints the cost of each
// configuration as a percentage of real time on ONE core (100 % = cannot keep up).
//   ironclad_bench [sampleRate] [blockSize]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Audio/Engine.h"
#include "WavTestUtils.h"

using namespace ironclad;

namespace
{
double gWorstPercent = 0.0; // slowest single block, % of that block's duration

std::string irFile(const char* name, double seconds, int channels)
{
  const auto n = static_cast<std::size_t>(48000 * seconds);
  std::vector<std::vector<double>> ch(static_cast<std::size_t>(channels), std::vector<double>(n));
  for (int c = 0; c < channels; ++c)
    for (std::size_t i = 0; i < n; ++i)
      ch[static_cast<std::size_t>(c)][i] = std::sin(0.37 * (i + 1 + c)) * std::exp(-static_cast<double>(i) / (0.2 * n));
  test::WavSpec spec;
  spec.bits = 32;
  spec.isFloat = true;
  return test::writeFile(name, test::makeWav(ch, spec));
}

double measure(Engine& e, double sr, int block, double seconds)
{
  std::vector<double> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
  const int blocks = static_cast<int>(sr * seconds / block);
  long n = 0;
  auto fill = [&] {
    for (int i = 0; i < block; ++i)
      l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = 0.3 * std::sin(6.283185307 * 196.0 * static_cast<double>(n++) / sr);
  };
  for (int b = 0; b < 200; ++b) // warm-up, crossfades finish
  {
    fill();
    double* io[2] = {l.data(), r.data()};
    e.process(io, 2, io, 2, block);
  }
  double elapsed = 0.0, worst = 0.0;
  for (int b = 0; b < blocks; ++b)
  {
    fill();
    double* io[2] = {l.data(), r.data()};
    const auto t0 = std::chrono::steady_clock::now();
    e.process(io, 2, io, 2, block);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    elapsed += dt;
    worst = std::max(worst, dt);
  }
  gWorstPercent = 100.0 * worst / (block / sr);
  return 100.0 * elapsed / (static_cast<double>(blocks) * block / sr);
}
} // namespace

int main(int argc, char** argv)
{
  const double sr = argc > 1 ? std::atof(argv[1]) : 48000.0;
  const int block = argc > 2 ? std::atoi(argv[2]) : 128;
  const std::string capture = (std::filesystem::path(IRONCLAD_NAM_EXAMPLE_MODELS_DIR) / "wavenet_a1_standard.nam").string();
  const auto irShort = irFile("bench_ir_2048.wav", 2048.0 / 48000.0, 1);
  const auto irLong = irFile("bench_ir_1s.wav", 1.0, 2);

  std::printf("IRONCLAD benchmark: %.0f Hz, block %d, %% of real time on one core\n", sr, block);
  const int effects[] = {kParamGateEnabled, kParamCompEnabled, kParamBoostEnabled, kParamOdEnabled, kParamDistEnabled,
                         kParamFuzzEnabled, kParamWahEnabled, kParamEqEnabled, kParamChorusEnabled, kParamDelayEnabled,
                         kParamReverbEnabled};
  const char* names[] = {"gate", "comp", "boost", "overdrive", "distortion", "fuzz", "wah", "eq", "chorus", "delay", "reverb"};

  auto run = [&](const char* label, auto&& configure) {
    Engine e;
    configure(e);
    e.prepare({sr, block, 2, 2});
    e.waitForLoader(60000);
    const double avg = measure(e, sr, block, 10.0);
    std::printf("  %-44s %7.2f %%   (worst block %6.1f %%)\n", label, avg, gWorstPercent);
    std::fflush(stdout);
  };

  run("empty chain (all off, no capture, no IR)", [](Engine&) {});
  run("NAM standard WaveNet capture", [&](Engine& e) { e.loadCapture(capture); });
  run("cab: one 2048-tap mono IR", [&](Engine& e) { e.loadIr(0, irShort); });
  run("cab: two 1 s stereo IRs (worst case)", [&](Engine& e) { e.loadIr(0, irLong); e.loadIr(1, irLong); });
  for (int k = 0; k < 11; ++k)
  {
    std::string label = std::string("effect: ") + names[k];
    run(label.c_str(), [&](Engine& e) { e.setParameter(effects[k], 1.0); });
  }
  run("everything: capture + 2x2048 IR + all effects", [&](Engine& e) {
    e.loadCapture(capture);
    e.loadIr(0, irShort);
    e.loadIr(1, irShort);
    for (int p : effects) e.setParameter(p, 1.0);
  });
  return 0;
}
