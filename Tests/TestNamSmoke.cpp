// Proves that NeuralAmpModelerCore builds with IRONCLAD's toolchain flags and that
// real .nam files load and produce finite, non-silent audio. Uses the MIT-licensed
// example models shipped in the NAM Core repository (test-only, never shipped).

#include "doctest/doctest.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#ifndef IRONCLAD_NAM_EXAMPLE_MODELS_DIR
  #error "IRONCLAD_NAM_EXAMPLE_MODELS_DIR must be defined by the build"
#endif

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

std::filesystem::path model(const char* name)
{
  return std::filesystem::path(IRONCLAD_NAM_EXAMPLE_MODELS_DIR) / name;
}

void runModel(const char* name, double sampleRate, int blockSize)
{
  CAPTURE(name);
  CAPTURE(blockSize);
  std::unique_ptr<nam::DSP> dsp = nam::get_dsp(model(name));
  REQUIRE(dsp != nullptr);
  CHECK(dsp->NumInputChannels() == 1);
  CHECK(dsp->NumOutputChannels() == 1);
  CHECK(dsp->GetExpectedSampleRate() > 0.0);

  dsp->Reset(sampleRate, blockSize);

  std::vector<NAM_SAMPLE> in(static_cast<std::size_t>(blockSize)), out(static_cast<std::size_t>(blockSize));
  NAM_SAMPLE* inPtr[1] = {in.data()};
  NAM_SAMPLE* outPtr[1] = {out.data()};

  double energy = 0.0;
  bool finite = true;
  const int blocks = (int)(sampleRate * 0.5) / blockSize; // 0.5 s of audio
  long n = 0;
  for (int b = 0; b < blocks; ++b)
  {
    for (int i = 0; i < blockSize; ++i, ++n)
      in[static_cast<std::size_t>(i)] = static_cast<NAM_SAMPLE>(0.3 * std::sin(kTwoPi * 220.0 * static_cast<double>(n) / sampleRate));
    dsp->process(inPtr, outPtr, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
      const double y = out[static_cast<std::size_t>(i)];
      finite = finite && std::isfinite(y);
      energy += y * y;
    }
  }
  CHECK(finite);
  CHECK(energy > 1e-6);
}
} // namespace

TEST_CASE("NAM Core loads and runs the example WaveNet capture")
{
  for (int block : {64, 256, 1024})
    runModel("wavenet.nam", 48000.0, block);
}

TEST_CASE("NAM Core loads and runs the example LSTM capture")
{
  runModel("lstm.nam", 48000.0, 256);
}

TEST_CASE("NAM Core rejects a missing file with an exception (no crash)")
{
  CHECK_THROWS(nam::get_dsp(model("this_file_does_not_exist.nam")));
}

TEST_CASE("NAM Core rejects a malformed file with an exception (no crash)")
{
  const auto path = std::filesystem::temp_directory_path() / "ironclad_bad_capture.nam";
  {
    FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fputs("{ this is not valid json", f);
    std::fclose(f);
  }
  CHECK_THROWS(nam::get_dsp(path));
  std::filesystem::remove(path);
}
