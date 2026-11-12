#include "Amp/AmpModel.h"

#include <cctype>
#include <filesystem>
#include <system_error>

#include "Amp/ResamplingModel.h"
#include "NAM/get_dsp.h"
#include "NAM/nam_file.h"
#include "Utilities/Paths.h"

namespace ironclad
{
namespace
{
std::string metaString(const nlohmann::json& meta, const char* key)
{
  if (meta.is_object())
  {
    auto it = meta.find(key);
    if (it != meta.end() && it->is_string())
      return it->get<std::string>();
  }
  return {};
}

} // namespace

AmpModel::~AmpModel() = default;

std::unique_ptr<AmpModel> AmpModel::load(const std::string& utf8Path)
{
  const std::filesystem::path path = pathFromUtf8(utf8Path);
  const std::string fileName = utf8FromPath(path.filename());

  std::error_code ec;
  if (utf8Path.empty() || !std::filesystem::exists(path, ec))
    throw CaptureLoadError("Capture file not found: " + fileName, "Path: " + utf8Path);
  if (std::filesystem::is_directory(path, ec))
    throw CaptureLoadError("Not a capture file: " + fileName, "Path is a directory: " + utf8Path);

  std::string ext = utf8FromPath(path.extension());
  for (auto& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext != ".nam")
    throw CaptureLoadError("Not a NAM capture (.nam) file: " + fileName, "Extension: " + ext);

  nam::dspData data;
  std::unique_ptr<nam::DSP> dsp;
  try
  {
    // Prewarm happens later in prepare(), at the real host rate.
    nam::DspLoadOptions options;
    options.prewarm = false;
    dsp = nam::get_dsp(path, data, options);
  }
  catch (const nam::NamFileValidationError& e)
  {
    throw CaptureLoadError("Invalid or damaged .nam file: " + fileName, e.what());
  }
  catch (const std::exception& e)
  {
    throw CaptureLoadError("This capture uses a format IRONCLAD cannot load: " + fileName, e.what());
  }

  if (!dsp)
    throw CaptureLoadError("Could not load capture: " + fileName, "nam::get_dsp returned null");
  if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
    throw CaptureLoadError("Unsupported capture (must be mono in / mono out): " + fileName,
                           "Channels in/out: " + std::to_string(dsp->NumInputChannels()) + "/"
                             + std::to_string(dsp->NumOutputChannels()));

  auto amp = std::unique_ptr<AmpModel>(new AmpModel());
  CaptureInfo& info = amp->mInfo;
  info.path = utf8Path;
  info.fileName = fileName;
  info.architecture = data.architecture;
  info.fileVersion = data.version;
  info.sampleRate = data.expected_sample_rate > 0.0 ? data.expected_sample_rate : 0.0;

  const nlohmann::json& meta = data.metadata;
  info.modeledBy = metaString(meta, "modeled_by");
  info.gearMake = metaString(meta, "gear_make");
  info.gearModel = metaString(meta, "gear_model");
  info.gearType = metaString(meta, "gear_type");
  info.toneType = metaString(meta, "tone_type");
  info.date = metaString(meta, "date");
  if (meta.is_object() && meta.contains("date") && meta["date"].is_object())
  {
    const auto& d = meta["date"];
    auto num = [&](const char* k) { return d.contains(k) && d[k].is_number() ? std::to_string(d[k].get<int>()) : std::string("?"); };
    info.date = num("year") + "-" + num("month") + "-" + num("day");
  }
  info.displayName = metaString(meta, "name");
  if (info.displayName.empty())
    info.displayName = utf8FromPath(path.stem());

  if (dsp->HasLoudness())
    info.loudnessDb = dsp->GetLoudness();
  if (dsp->HasInputLevel())
    info.inputLevelDbu = dsp->GetInputLevel();
  if (dsp->HasOutputLevel())
    info.outputLevelDbu = dsp->GetOutputLevel();

  amp->mNormalizationDb = info.loudnessDb ? (kTargetLoudnessDb - *info.loudnessDb) : 0.0;
  amp->mModel = std::make_unique<ResamplingModel>(std::move(dsp));
  return amp;
}

void AmpModel::prepare(double hostSampleRate, int maxBlockSize)
{
  mModel->reset(hostSampleRate, maxBlockSize);
}

void AmpModel::process(double* input, double* output, int numFrames) noexcept
{
  static_assert(sizeof(NAM_SAMPLE) == sizeof(double), "IRONCLAD builds NAM with double samples");
  mModel->process(input, output, numFrames);
}

int AmpModel::latencySamples() const noexcept { return mModel->latencySamples(); }
bool AmpModel::isResampling() const noexcept { return mModel->needsResampling(); }

} // namespace ironclad
