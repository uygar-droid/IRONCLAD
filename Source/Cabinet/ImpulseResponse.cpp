#include "Cabinet/ImpulseResponse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "Cabinet/Resample.h"
#include "Cabinet/WavReader.h"
#include "Utilities/Paths.h"

namespace ironclad
{

std::unique_ptr<ImpulseResponse> ImpulseResponse::load(const std::string& utf8Path)
{
  const auto path = pathFromUtf8(utf8Path);
  const std::string fileName = utf8FromPath(path.filename());
  std::string ext = utf8FromPath(path.extension());
  for (auto& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!utf8Path.empty() && ext != ".wav")
  {
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
      throw AudioFileError("Not a WAV impulse response: " + fileName, "Extension: " + ext);
  }

  WavData wav = readWavFile(utf8Path);

  auto ir = std::unique_ptr<ImpulseResponse>(new ImpulseResponse());
  IrInfo& info = ir->mInfo;
  info.path = utf8Path;
  info.fileName = fileName;
  info.displayName = utf8FromPath(path.stem());
  info.fileSampleRate = wav.sampleRate;
  info.fileChannels = wav.numChannels();
  info.fileFrames = wav.numFrames();

  const std::size_t maxFrames = static_cast<std::size_t>(kMaxSeconds * wav.sampleRate);
  info.truncated = wav.numFrames() > maxFrames;
  const int keep = std::min(2, wav.numChannels());
  for (int c = 0; c < keep; ++c)
  {
    auto& ch = wav.channels[static_cast<std::size_t>(c)];
    if (ch.size() > maxFrames)
      ch.resize(maxFrames);
    ir->mOriginal.push_back(std::move(ch));
  }
  ir->mStereo = keep == 2;
  info.durationMs = 1000.0 * static_cast<double>(ir->mOriginal[0].size()) / wav.sampleRate;

  // Silent IRs produce silence; reject them so the user notices.
  double peak = 0.0;
  for (const auto& ch : ir->mOriginal)
    for (double v : ch)
      peak = std::max(peak, std::abs(v));
  if (peak < 1e-9)
    throw AudioFileError("This WAV file is silent and cannot be used as an IR: " + fileName, "Peak below -180 dBFS");

  // Peak-envelope thumbnail of the first channel, for the UI.
  const auto& c0 = ir->mOriginal[0];
  info.thumbnail.assign(kThumbnailPoints, 0.0f);
  for (std::size_t i = 0; i < c0.size(); ++i)
  {
    const auto bin = std::min<std::size_t>(kThumbnailPoints - 1, i * kThumbnailPoints / c0.size());
    info.thumbnail[bin] = std::max(info.thumbnail[bin], static_cast<float>(std::abs(c0[i]) / peak));
  }
  return ir;
}

void ImpulseResponse::prepare(double hostSampleRate)
{
  std::vector<std::vector<double>> taps;
  for (const auto& ch : mOriginal)
    taps.push_back(resampleOffline(ch, mInfo.fileSampleRate, hostSampleRate));

  // Normalise to unit energy (averaged over channels): a 0 dB broadband gain, so IRs with
  // very different recording levels play at a comparable loudness.
  double energy = 0.0;
  for (const auto& ch : taps)
    for (double v : ch)
      energy += v * v;
  energy /= static_cast<double>(taps.size());
  mNormGain = energy > 0.0 ? 1.0 / std::sqrt(energy) : 1.0;
  for (auto& ch : taps)
    for (double& v : ch)
      v *= mNormGain;

  mLeft.prepare(taps[0]);
  if (mStereo)
    mRight.prepare(taps[1]);
}

void ImpulseResponse::reset() noexcept
{
  mLeft.reset();
  mRight.reset();
}

void ImpulseResponse::process(const double* input, double* outLeft, double* outRight, int numFrames) noexcept
{
  mLeft.process(input, outLeft, numFrames);
  if (mStereo)
    mRight.process(input, outRight, numFrames);
  else
    std::copy(outLeft, outLeft + numFrames, outRight);
}

} // namespace ironclad
