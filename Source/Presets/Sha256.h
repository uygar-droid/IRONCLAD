#pragma once

// SHA-256 (FIPS 180-4), used to identify capture / IR files so a preset can find a file
// again after it has been moved or renamed. Not used for security.

#include <cstdint>
#include <string>
#include <vector>

namespace ironclad
{

class Sha256
{
public:
  Sha256();
  void update(const void* data, std::size_t size);
  std::string hexDigest(); // finalises; lower-case hex, 64 chars

  static std::string ofBytes(const void* data, std::size_t size);
  /// Hash of a file's contents (UTF-8 path); empty string if it cannot be read.
  static std::string ofFile(const std::string& utf8Path, std::uint64_t* sizeOut = nullptr);

private:
  void block(const std::uint8_t* p);
  std::uint32_t mH[8];
  std::uint8_t mBuf[64];
  std::size_t mBufLen = 0;
  std::uint64_t mTotal = 0;
};

} // namespace ironclad
