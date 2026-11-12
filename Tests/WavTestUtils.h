#pragma once

// Test helper: builds WAV files byte-by-byte so the reader is tested against
// independently constructed data (not against itself).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ironclad::test
{

struct WavSpec
{
  int sampleRate = 48000;
  int bits = 24;
  bool isFloat = false;
  bool extensible = false;
  bool listChunkBeforeData = false;
};

inline void put16(std::vector<unsigned char>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
inline void put32(std::vector<unsigned char>& b, std::uint32_t v)
{
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

inline std::vector<unsigned char> makeWav(const std::vector<std::vector<double>>& ch, const WavSpec& s)
{
  const auto channels = static_cast<std::uint16_t>(ch.size());
  const std::size_t frames = ch.empty() ? 0 : ch[0].size();
  const int bps = s.bits / 8;
  std::vector<unsigned char> data;
  for (std::size_t i = 0; i < frames; ++i)
    for (std::size_t c = 0; c < ch.size(); ++c)
    {
      const double v = ch[c][i];
      if (s.isFloat && s.bits == 32) { float f = static_cast<float>(v); std::uint32_t u; std::memcpy(&u, &f, 4); put32(data, u); }
      else if (s.isFloat && s.bits == 64) { std::uint64_t u; std::memcpy(&u, &v, 8); put32(data, u & 0xFFFFFFFFu); put32(data, static_cast<std::uint32_t>(u >> 32)); }
      else if (s.bits == 16) { put16(data, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767.0)))); }
      else if (s.bits == 24) { auto x = static_cast<std::int32_t>(std::lround(v * 8388607.0)); data.push_back(x & 0xFF); data.push_back((x >> 8) & 0xFF); data.push_back((x >> 16) & 0xFF); }
      else { put32(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::llround(v * 2147483647.0)))); }
    }

  std::vector<unsigned char> fmt;
  const std::uint16_t tag = s.extensible ? 0xFFFE : (s.isFloat ? 3 : 1);
  put16(fmt, tag);
  put16(fmt, channels);
  put32(fmt, static_cast<std::uint32_t>(s.sampleRate));
  put32(fmt, static_cast<std::uint32_t>(s.sampleRate * bps * channels));
  put16(fmt, static_cast<std::uint16_t>(bps * channels));
  put16(fmt, static_cast<std::uint16_t>(s.bits));
  if (s.extensible)
  {
    put16(fmt, 22);
    put16(fmt, static_cast<std::uint16_t>(s.bits));
    put32(fmt, 0);
    put16(fmt, s.isFloat ? 3 : 1); // SubFormat GUID begins with the format tag
    const unsigned char guidTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    fmt.insert(fmt.end(), guidTail, guidTail + 14);
  }

  std::vector<unsigned char> body;
  body.insert(body.end(), {'W', 'A', 'V', 'E'});
  body.insert(body.end(), {'f', 'm', 't', ' '});
  put32(body, static_cast<std::uint32_t>(fmt.size()));
  body.insert(body.end(), fmt.begin(), fmt.end());
  if (s.listChunkBeforeData)
  {
    body.insert(body.end(), {'L', 'I', 'S', 'T'});
    put32(body, 3); // odd size -> one pad byte
    body.insert(body.end(), {'a', 'b', 'c', 0});
  }
  body.insert(body.end(), {'d', 'a', 't', 'a'});
  put32(body, static_cast<std::uint32_t>(data.size()));
  body.insert(body.end(), data.begin(), data.end());

  std::vector<unsigned char> file{'R', 'I', 'F', 'F'};
  put32(file, static_cast<std::uint32_t>(body.size()));
  file.insert(file.end(), body.begin(), body.end());
  return file;
}

inline std::string writeFile(const std::string& name, const std::vector<unsigned char>& bytes)
{
  const auto p = std::filesystem::temp_directory_path() / name;
  FILE* f = std::fopen(p.string().c_str(), "wb");
  if (f)
  {
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
  }
  return p.string();
}

} // namespace ironclad::test
