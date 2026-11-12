#include "Cabinet/WavReader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Utilities/Paths.h"

namespace ironclad
{
namespace
{
constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;
constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u; // an IR is never this large

std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
} // namespace

WavData parseWav(const std::vector<unsigned char>& b, const std::string& name)
{
  auto fail = [&](const std::string& detail) -> AudioFileError {
    return AudioFileError("Invalid or damaged WAV file: " + name, detail);
  };

  if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
    throw AudioFileError("Not a WAV file: " + name, "Missing RIFF/WAVE header");

  bool haveFmt = false;
  std::uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
  std::uint32_t sampleRate = 0;
  const unsigned char* data = nullptr;
  std::size_t dataSize = 0;

  std::size_t pos = 12;
  while (pos + 8 <= b.size())
  {
    const unsigned char* chunk = b.data() + pos;
    const std::uint32_t size = le32(chunk + 4);
    const std::size_t bodyStart = pos + 8;
    const std::size_t available = b.size() - bodyStart;
    const std::size_t bodySize = std::min<std::size_t>(size, available); // tolerate truncated last chunk

    if (std::memcmp(chunk, "fmt ", 4) == 0)
    {
      if (bodySize < 16)
        throw fail("fmt chunk too small");
      const unsigned char* f = b.data() + bodyStart;
      format = le16(f);
      channels = le16(f + 2);
      sampleRate = le32(f + 4);
      blockAlign = le16(f + 12);
      bits = le16(f + 14);
      if (format == kFormatExtensible)
      {
        if (bodySize < 40)
          throw fail("WAVE_FORMAT_EXTENSIBLE chunk too small");
        format = le16(f + 24); // first two bytes of the SubFormat GUID
      }
      haveFmt = true;
    }
    else if (std::memcmp(chunk, "data", 4) == 0)
    {
      data = b.data() + bodyStart;
      dataSize = bodySize;
    }
    // Chunks are word-aligned: odd sizes carry one pad byte.
    const std::size_t advance = 8 + static_cast<std::size_t>(size) + (size & 1u);
    if (advance < 8 || pos + advance <= pos)
      break;
    pos += advance;
  }

  if (!haveFmt)
    throw fail("No fmt chunk");
  if (data == nullptr)
    throw fail("No data chunk");
  if (channels == 0 || sampleRate == 0)
    throw fail("Zero channels or sample rate");
  if (sampleRate < 8000 || sampleRate > 768000)
    throw fail("Implausible sample rate " + std::to_string(sampleRate));

  const bool isFloat = format == kFormatFloat;
  if (!(format == kFormatPcm || isFloat))
    throw AudioFileError("Unsupported WAV encoding in " + name + " (use PCM or float WAV)",
                         "Format tag " + std::to_string(format));
  if (isFloat ? !(bits == 32 || bits == 64) : !(bits == 16 || bits == 24 || bits == 32 || bits == 8))
    throw AudioFileError("Unsupported WAV bit depth in " + name, std::to_string(bits) + " bits");

  const std::size_t bytesPerSample = bits / 8;
  const std::size_t frameBytes = bytesPerSample * channels;
  if (blockAlign != 0 && blockAlign != frameBytes)
    throw fail("blockAlign mismatch");
  const std::size_t frames = dataSize / frameBytes;
  if (frames == 0)
    throw AudioFileError("WAV file contains no audio: " + name, "data chunk empty");

  WavData out;
  out.sampleRate = sampleRate;
  out.bitsPerSample = bits;
  out.isFloat = isFloat;
  out.channels.assign(channels, std::vector<double>(frames, 0.0));

  for (std::size_t i = 0; i < frames; ++i)
  {
    for (std::size_t c = 0; c < channels; ++c)
    {
      const unsigned char* s = data + i * frameBytes + c * bytesPerSample;
      double v = 0.0;
      if (isFloat && bits == 32)
      {
        float f;
        std::uint32_t u = le32(s);
        std::memcpy(&f, &u, 4);
        v = f;
      }
      else if (isFloat && bits == 64)
      {
        std::uint64_t u = static_cast<std::uint64_t>(le32(s)) | (static_cast<std::uint64_t>(le32(s + 4)) << 32);
        std::memcpy(&v, &u, 8);
      }
      else if (bits == 8)
        v = (static_cast<int>(s[0]) - 128) / 128.0; // 8-bit WAV is unsigned
      else if (bits == 16)
        v = static_cast<std::int16_t>(le16(s)) / 32768.0;
      else if (bits == 24)
      {
        std::int32_t x = s[0] | (s[1] << 8) | (s[2] << 16);
        if (x & 0x800000)
          x |= ~0xFFFFFF;
        v = x / 8388608.0;
      }
      else // 32-bit int
        v = static_cast<std::int32_t>(le32(s)) / 2147483648.0;
      if (!std::isfinite(v))
        throw fail("Non-finite sample value");
      out.channels[c][i] = v;
    }
  }
  return out;
}

WavData readWavFile(const std::string& utf8Path)
{
  const std::filesystem::path path = pathFromUtf8(utf8Path);
  const std::string name = utf8FromPath(path.filename());
  std::error_code ec;
  if (utf8Path.empty() || !std::filesystem::exists(path, ec))
    throw AudioFileError("IR file not found: " + name, "Path: " + utf8Path);
  if (std::filesystem::is_directory(path, ec))
    throw AudioFileError("Not an IR file: " + name, "Path is a directory");
  const auto size = std::filesystem::file_size(path, ec);
  if (ec)
    throw AudioFileError("Cannot read IR file: " + name, ec.message());
  if (size > kMaxFileBytes)
    throw AudioFileError("IR file is too large: " + name, std::to_string(size) + " bytes");

  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw AudioFileError("Cannot open IR file: " + name, "ifstream failed");
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  if (size > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    throw AudioFileError("Cannot read IR file: " + name, "short read");
  return parseWav(bytes, name);
}

} // namespace ironclad
