// Milestone 4 tests: individual effects (behaviour against their specification) and the
// effect chain inside the engine (bypass, click-free switching, real-time safety).

#include "doctest/doctest.h"

#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "AllocationTracking.h"
#include "Audio/Engine.h"
#include "Effects/Chorus.h"
#include "Effects/Compressor.h"
#include "Effects/Delay.h"
#include "Effects/Drive.h"
#include "Effects/Equalizer.h"
#include "Effects/NoiseGate.h"
#include "Effects/Reverb.h"
#include "Effects/TapTempo.h"
#include "Effects/Wah.h"

using namespace ironclad;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kSr = 48000.0;

std::vector<double> sine(double freq, double amp, int n, double sr = kSr)
{
  std::vector<double> v(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = amp * std::sin(kTwoPi * freq * i / sr);
  return v;
}
double peak(const std::vector<double>& v, std::size_t from = 0)
{
  double p = 0.0;
  for (std::size_t i = from; i < v.size(); ++i) p = std::max(p, std::abs(v[i]));
  return p;
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }
/// Magnitude of one frequency bin (Goertzel-style DFT at an arbitrary frequency).
double toneMagnitude(const std::vector<double>& v, double freq, std::size_t from)
{
  std::complex<double> acc = 0.0;
  for (std::size_t i = from; i < v.size(); ++i) acc += v[i] * std::polar(1.0, -kTwoPi * freq * static_cast<double>(i) / kSr);
  return 2.0 * std::abs(acc) / static_cast<double>(v.size() - from);
}
template <typename Fx>
void runMono(Fx& fx, std::vector<double>& x, int block = 64)
{
  for (std::size_t p = 0; p < x.size(); p += static_cast<std::size_t>(block))
    fx.process(x.data() + p, static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - p)));
}
template <typename Fx>
void runStereo(Fx& fx, std::vector<double>& l, std::vector<double>& r, int block = 64)
{
  for (std::size_t p = 0; p < l.size(); p += static_cast<std::size_t>(block))
  {
    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), l.size() - p));
    fx.process(l.data() + p, r.data() + p, n);
  }
}
} // namespace

// ---------------------------------------------------------------- Noise gate
TEST_CASE("Noise gate passes signal above threshold and silences signal below it")
{
  NoiseGate g;
  g.setThresholdDb(-50.0);
  g.prepare(kSr, 64);
  auto loud = sine(200.0, 0.1, 24000); // -20 dBFS
  runMono(g, loud);
  CHECK(db(peak(loud, 12000) / 0.1) > -0.1);

  auto quiet = sine(200.0, 0.001, 48000); // -60 dBFS
  runMono(g, quiet);
  CHECK(db(peak(quiet, 24000)) < -140.0); // closed: -60 dB signal * -90 dB gate
}

// ---------------------------------------------------------------- Compressor
TEST_CASE("Compressor steady-state gain follows threshold and ratio")
{
  Compressor c;
  c.setThresholdDb(-30.0);
  c.setRatio(4.0);
  c.setAttack(1.0);
  c.setRelease(50.0);
  c.setMakeupDb(0.0);
  c.setMix(1.0);
  c.prepare(kSr, 64);
  // Square-ish signal so the peak detector sees a constant level: 0.5 (-6 dBFS).
  std::vector<double> x(48000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = (i / 24) % 2 ? 0.5 : -0.5;
  runMono(c, x);
  const double expectedDb = -30.0 + (-6.0206 + 30.0) / 4.0; // above the knee
  CHECK(db(peak(x, 40000)) == doctest::Approx(expectedDb).scale(0.0).epsilon(0.02));
  CHECK(c.computeGainDb(-40.0) == 0.0);          // below threshold: untouched
  CHECK(c.computeGainDb(-10.0) == doctest::Approx(-15.0)); // 20 dB over at 4:1
}

// ---------------------------------------------------------------- Drives
TEST_CASE("Drives: more drive -> more harmonics; output bounded; quiet when silent")
{
  for (auto ch : {Drive::Character::Overdrive, Drive::Character::Distortion, Drive::Character::Fuzz})
  {
    CAPTURE(static_cast<int>(ch));
    auto thd = [&](double drive) {
      Drive d(ch);
      d.setDrive(drive);
      d.setTone(1.0);
      d.setLevelDb(0.0);
      d.prepare(kSr, 64);
      auto x = sine(1000.0, 0.3, 24000);
      runMono(d, x);
      const double h1 = toneMagnitude(x, 1000.0, 12000);
      const double h3 = toneMagnitude(x, 3000.0, 12000) + toneMagnitude(x, 2000.0, 12000);
      CHECK(peak(x, 12000) < 4.0);
      return h3 / std::max(h1, 1e-9);
    };
    CHECK(thd(1.0) > thd(0.1));
    Drive d(ch);
    d.setDrive(1.0);
    d.prepare(kSr, 64);
    std::vector<double> silence(4800, 0.0);
    runMono(d, silence);
    CHECK(peak(silence) < 1e-9);
  }
}

TEST_CASE("ADAA clipping aliases far less than a naive clipper")
{
  // 7 kHz at high gain: the naive clipper folds harmonics back into the audio band.
  const double f = 7000.0, gain = 30.0;
  auto x = sine(f, 0.5, 48000);
  std::vector<double> naive(x.size()), adaa(x.size());
  Adaa1<HardClipShape> clip;
  clip.reset();
  for (std::size_t i = 0; i < x.size(); ++i)
  {
    naive[i] = HardClipShape::f(x[i] * gain);
    adaa[i] = clip.process(x[i] * gain);
  }
  // 7 kHz harmonics: 21 kHz (in band), 35 kHz -> aliases to 13 kHz, 49 kHz -> 1 kHz.
  const double aliasNaive = toneMagnitude(naive, 13000.0, 1000) + toneMagnitude(naive, 1000.0, 1000);
  const double aliasAdaa = toneMagnitude(adaa, 13000.0, 1000) + toneMagnitude(adaa, 1000.0, 1000);
  CAPTURE(db(aliasNaive));
  CAPTURE(db(aliasAdaa));
  CHECK(db(aliasAdaa) < db(aliasNaive) - 6.0);
}

// ---------------------------------------------------------------- Wah
TEST_CASE("Wah resonance follows the pedal position")
{
  auto response = [&](double position, double freq) {
    Wah w;
    w.setPosition(position);
    w.setResonance(0.6);
    w.setMix(1.0);
    w.prepare(kSr, 64);
    auto x = sine(freq, 0.1, 24000);
    runMono(w, x);
    return toneMagnitude(x, freq, 12000) / 0.1;
  };
  // Heel down: 400 Hz louder than 2 kHz; toe down: the opposite.
  CHECK(response(0.0, 400.0) > 3.0 * response(0.0, 2000.0));
  CHECK(response(1.0, 2000.0) > 3.0 * response(1.0, 400.0));
  CHECK(Wah::frequencyFor(0.0) == doctest::Approx(Wah::kMinHz));
  CHECK(Wah::frequencyFor(1.0) == doctest::Approx(Wah::kMaxHz));
}

// ---------------------------------------------------------------- EQ
TEST_CASE("EQ bands hit their gains at their frequencies and are flat at 0 dB")
{
  auto gainAt = [&](double low, double mid, double midHz, double high, double freq) {
    Equalizer eq;
    eq.setLowDb(low);
    eq.setMidDb(mid);
    eq.setMidHz(midHz);
    eq.setHighDb(high);
    eq.setLevelDb(0.0);
    eq.prepare(kSr, 64);
    auto l = sine(freq, 0.1, 24000), r = l;
    runStereo(eq, l, r);
    return db(toneMagnitude(l, freq, 12000) / 0.1);
  };
  CHECK(std::abs(gainAt(0, 0, 800, 0, 1000.0)) < 0.01);
  CHECK(gainAt(0, 9.0, 1000.0, 0, 1000.0) == doctest::Approx(9.0).scale(0.0).epsilon(0.02));
  CHECK(gainAt(0, -9.0, 2500.0, 0, 2500.0) == doctest::Approx(-9.0).scale(0.0).epsilon(0.02));
  CHECK(gainAt(8.0, 0, 800, 0, 40.0) == doctest::Approx(8.0).scale(0.0).epsilon(0.05));  // shelf plateau
  CHECK(gainAt(0, 0, 800, -8.0, 12000.0) == doctest::Approx(-8.0).scale(0.0).epsilon(0.05));
}

// ---------------------------------------------------------------- Chorus
TEST_CASE("Chorus: mix 0 is transparent; mix > 0 modulates and widens")
{
  Chorus c;
  c.setRateHz(1.0);
  c.setDepth(1.0);
  c.setMix(0.0);
  c.prepare(kSr, 64);
  auto l = sine(440.0, 0.3, 9600), r = l, ref = l;
  runStereo(c, l, r);
  for (std::size_t i = 0; i < l.size(); ++i) REQUIRE(l[i] == ref[i]);

  Chorus w;
  w.setRateHz(1.0);
  w.setDepth(1.0);
  w.setMix(0.5);
  w.prepare(kSr, 64);
  auto l2 = sine(440.0, 0.3, 48000), r2 = l2;
  runStereo(w, l2, r2);
  double diff = 0.0;
  for (std::size_t i = 24000; i < l2.size(); ++i) diff += std::abs(l2[i] - r2[i]);
  CHECK(diff / 24000.0 > 0.01); // left and right differ (stereo width)
}

// ---------------------------------------------------------------- Delay
TEST_CASE("Delay: echo at the exact time with the set feedback")
{
  Delay d;
  d.setType(Delay::Type::Digital);
  d.setTimeMs(250.0);
  d.setFeedback(0.5);
  d.setMix(1.0);
  d.setFilters(20.0, 20000.0);
  d.prepare(kSr, 64);
  // 20 ms burst of 1 kHz (inside the feedback filters' pass band).
  std::vector<double> l(48000, 0.0), r(48000, 0.0);
  for (int i = 0; i < 960; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = std::sin(kTwoPi * 1000.0 * i / kSr);
  runStereo(d, l, r);
  auto window = [&](std::size_t start) { // 1 kHz amplitude over 15 ms (whole periods)
    std::complex<double> acc = 0.0;
    for (std::size_t i = start + 100; i < start + 820; ++i) acc += l[i] * std::polar(1.0, -kTwoPi * 1000.0 * static_cast<double>(i) / kSr);
    return 2.0 * std::abs(acc) / 720.0;
  };
  const std::size_t t = 12000; // 250 ms
  CHECK(window(t) == doctest::Approx(1.0).scale(0.0).epsilon(0.02));
  CHECK(window(2 * t) == doctest::Approx(0.5).scale(0.0).epsilon(0.02));
  CHECK(window(3 * t) == doctest::Approx(0.25).scale(0.0).epsilon(0.03));
  double between = 0.0;
  for (std::size_t i = 2000; i < 11000; ++i) between = std::max(between, std::abs(l[i]));
  CHECK(between < 1e-9); // nothing before the first echo
}

TEST_CASE("Ping-pong delay alternates sides")
{
  Delay d;
  d.setType(Delay::Type::PingPong);
  d.setTimeMs(100.0);
  d.setFeedback(0.6);
  d.setMix(1.0);
  d.setFilters(20.0, 20000.0);
  d.prepare(kSr, 64);
  std::vector<double> l(24000, 0.0), r(24000, 0.0);
  l[0] = r[0] = 1.0;
  runStereo(d, l, r);
  const std::size_t t = 4800;
  CHECK(std::abs(l[t]) > 0.5);
  CHECK(std::abs(r[t]) < 1e-6);
  CHECK(std::abs(r[2 * t]) > 0.3);
  CHECK(std::abs(l[2 * t]) < 1e-6);
}

TEST_CASE("Delay sync divisions and tap tempo")
{
  CHECK(Delay::syncedMs(120.0, 3) == doctest::Approx(500.0)); // 1/4
  CHECK(Delay::syncedMs(120.0, 7) == doctest::Approx(375.0)); // 1/8 dotted
  CHECK(Delay::syncedMs(90.0, 0) == doctest::Approx(2666.6667));
  CHECK(Delay::syncedMs(0.0, 3) == doctest::Approx(500.0));   // unknown tempo -> 120

  TapTempo tap;
  CHECK_FALSE(tap.tap(10.0).has_value());
  CHECK(*tap.tap(10.5) == doctest::Approx(500.0));
  CHECK(*tap.tap(11.0) == doctest::Approx(500.0));
  CHECK(*tap.tap(11.4) == doctest::Approx(466.6667));
  CHECK_FALSE(tap.tap(20.0).has_value()); // > 2 s pause restarts
}

TEST_CASE("Analog delay stays bounded at maximum feedback")
{
  Delay d;
  d.setType(Delay::Type::Analog);
  d.setTimeMs(50.0);
  d.setFeedback(0.95);
  d.setMix(1.0);
  d.setFilters(20.0, 20000.0);
  d.prepare(kSr, 256);
  auto l = sine(300.0, 0.8, kSr * 10), r = l;
  runStereo(d, l, r, 256);
  CHECK(peak(l) < 20.0);
  for (double v : l) REQUIRE(std::isfinite(v));
}

// ---------------------------------------------------------------- Reverb
TEST_CASE("Reverb decay time follows the Decay setting for every type")
{
  for (auto type : {Reverb::Type::Room, Reverb::Type::Hall, Reverb::Type::Plate})
    for (double rt : {0.8, 2.5})
    {
      CAPTURE(static_cast<int>(type));
      CAPTURE(rt);
      Reverb rv;
      rv.setType(type);
      rv.setDecaySeconds(rt);
      rv.setPreDelayMs(0.0);
      rv.setDamping(0.0);
      rv.setMix(1.0);
      rv.prepare(kSr, 256);
      const int n = static_cast<int>(kSr * (rt * 1.5 + 0.3));
      std::vector<double> l(static_cast<std::size_t>(n), 0.0), r(static_cast<std::size_t>(n), 0.0);
      l[0] = r[0] = 1.0;
      runStereo(rv, l, r, 256);
      l[0] -= 1.0; // remove dry impulse
      r[0] -= 1.0;
      // Schroeder backward integration; T20 (-5 .. -25 dB) * 3.
      std::vector<double> edc(l.size());
      double acc = 0.0;
      for (std::size_t i = l.size(); i-- > 0;) { acc += l[i] * l[i] + r[i] * r[i]; edc[i] = acc; }
      const double e0 = edc[0];
      std::size_t i5 = 0, i25 = 0;
      for (std::size_t i = 0; i < edc.size(); ++i)
      {
        const double d = 10.0 * std::log10(edc[i] / e0);
        if (!i5 && d <= -5.0) i5 = i;
        if (!i25 && d <= -25.0) { i25 = i; break; }
      }
      REQUIRE(i25 > i5);
      const double measured = 3.0 * static_cast<double>(i25 - i5) / kSr;
      CAPTURE(measured);
      CHECK(std::abs(measured / rt - 1.0) < 0.10); // explicit relative tolerance
      double lr = 0.0;
      for (std::size_t i = 1000; i < 20000; ++i) lr += std::abs(l[i] - r[i]);
      CHECK(lr > 0.01); // decorrelated stereo tail
    }
}

TEST_CASE("Reverb is stable at maximum decay and survives type changes without clicks")
{
  Reverb rv;
  rv.setType(Reverb::Type::Hall);
  rv.setDecaySeconds(10.0);
  rv.setDamping(0.0);
  rv.setMix(1.0);
  rv.prepare(kSr, 256);
  std::mt19937 rng(3);
  std::uniform_real_distribution<double> u(-0.5, 0.5);
  std::vector<double> l(static_cast<std::size_t>(kSr * 20)), r;
  for (auto& v : l) v = u(rng);
  r = l;
  for (std::size_t p = 0; p < l.size(); p += 256)
  {
    if (p == static_cast<std::size_t>(kSr * 5)) rv.setType(Reverb::Type::Plate);
    if (p == static_cast<std::size_t>(kSr * 10)) rv.setType(Reverb::Type::Room);
    rv.process(l.data() + p, r.data() + p, 256);
  }
  bool finite = true;
  for (double v : l) finite = finite && std::isfinite(v);
  CHECK(finite);
  CHECK(peak(l, static_cast<std::size_t>(kSr * 15)) < 20.0);
}

// ---------------------------------------------------------------- Engine chain
namespace
{
struct ChainRig
{
  Engine engine;
  int block = 128;
  std::vector<double> in = std::vector<double>(128), l = std::vector<double>(128), r = std::vector<double>(128);
  long n = 0;
  ChainRig() { engine.prepare({kSr, 128, 1, 2}); }
  void run(double freq = 220.0)
  {
    for (auto& x : in) x = 0.3 * std::sin(kTwoPi * freq * static_cast<double>(n++) / kSr);
    const double* i[1] = {in.data()};
    double* o[2] = {l.data(), r.data()};
    engine.process(i, 1, o, 2, block);
  }
};
const int kEnable[] = {kParamGateEnabled, kParamCompEnabled, kParamBoostEnabled, kParamOdEnabled, kParamDistEnabled,
                       kParamFuzzEnabled, kParamWahEnabled, kParamEqEnabled, kParamChorusEnabled, kParamDelayEnabled,
                       kParamReverbEnabled};
} // namespace

TEST_CASE("All effects off (default): the chain is bit-transparent")
{
  ChainRig rig;
  for (int k = 0; k < Engine::kNumEffectSlots; ++k)
    CHECK(rig.engine.effectFullyOff(k));
  for (int b = 0; b < 20; ++b)
  {
    rig.run();
    for (int i = 0; i < rig.block; ++i)
    {
      REQUIRE(rig.l[static_cast<std::size_t>(i)] == rig.in[static_cast<std::size_t>(i)]);
      REQUIRE(rig.r[static_cast<std::size_t>(i)] == rig.in[static_cast<std::size_t>(i)]);
    }
  }
}

TEST_CASE("Switching each effect on and off is click-free")
{
  for (int k = 0; k < Engine::kNumEffectSlots; ++k)
  {
    CAPTURE(k);
    ChainRig rig;
    // Effects at moderate settings so the processed signal stays in a comparable range.
    rig.engine.setParameter(kParamCompThreshold, -10.0);
    rig.engine.setParameter(kParamOdDrive, 20.0);
    rig.engine.setParameter(kParamDistDrive, 10.0);
    rig.engine.setParameter(kParamFuzzFuzz, 5.0);
    rig.engine.setParameter(kParamFuzzLevel, -12.0);
    rig.engine.setParameter(kParamBoostGain, 3.0);
    for (int b = 0; b < 40; ++b) rig.run();
    double prev = rig.l.back(), maxStep = 0.0, steady = 0.0;
    auto measure = [&](int blocks, double& out) {
      for (int b = 0; b < blocks; ++b)
      {
        rig.run();
        for (double x : rig.l) { out = std::max(out, std::abs(x - prev)); prev = x; }
      }
    };
    measure(40, steady);
    rig.engine.setParameter(kEnable[k], 1.0);
    measure(60, maxStep);
    rig.engine.setParameter(kEnable[k], 0.0);
    measure(60, maxStep);
    CAPTURE(steady);
    CAPTURE(maxStep);
    // A 0.3-amplitude 220 Hz sine changes by <= 0.0087 per sample; a click would be far larger.
    CHECK(maxStep < 0.08);
    CHECK(rig.engine.effectFullyOff(k));
  }
}

TEST_CASE("Full chain with every effect on: finite output and zero audio-thread allocations")
{
  ChainRig rig;
  for (int p : kEnable) rig.engine.setParameter(p, 1.0);
  rig.engine.setParameter(kParamDelaySync, 1.0);
  rig.engine.setTempo(132.0);
  for (int b = 0; b < 50; ++b) rig.run();

  bool finite = true;
  test::ScopedAllocationCheck check;
  for (int b = 0; b < 2000; ++b)
  {
    if (b % 100 == 0)
    {
      rig.engine.setParameter(kParamWahPosition, (b / 100) % 2 ? 90.0 : 10.0);
      rig.engine.setParameter(kParamReverbType, static_cast<double>((b / 100) % 3));
      rig.engine.setParameter(kParamDelayType, static_cast<double>((b / 200) % 3));
      rig.engine.setParameter(kParamDelayTime, b % 200 ? 300.0 : 700.0);
    }
    rig.run();
    for (int i = 0; i < rig.block; ++i)
      finite = finite && std::isfinite(rig.l[static_cast<std::size_t>(i)]) && std::isfinite(rig.r[static_cast<std::size_t>(i)]);
  }
  const auto counts = check.finish();
  CHECK(finite);
  CHECK(counts.allocations == 0);
  CHECK(counts.deallocations == 0);
}

TEST_CASE("Every effect runs at every supported sample rate")
{
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
  {
    CAPTURE(sr);
    Engine e;
    for (int p : kEnable) e.setParameter(p, 1.0);
    e.prepare({sr, 512, 2, 2});
    std::vector<double> l(512), r(512);
    long n = 0;
    bool finite = true;
    double energy = 0.0;
    for (int b = 0; b < static_cast<int>(sr / 512); ++b)
    {
      for (int i = 0; i < 512; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = 0.2 * std::sin(kTwoPi * 330.0 * static_cast<double>(n++) / sr);
      double* io[2] = {l.data(), r.data()};
      e.process(io, 2, io, 2, 512);
      for (double v : l) { finite = finite && std::isfinite(v); energy += v * v; }
    }
    CHECK(finite);
    CHECK(energy > 1.0);
  }
}
