#pragma once

// RAII guard that enables flush-to-zero / denormals-are-zero for the current
// thread and restores the previous floating-point control state on exit.
// Used at the top of every audio callback. Covers x86-64 (Windows, Intel Macs)
// and ARM64 (Apple Silicon); on anything else it is a harmless no-op.

#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
  #include <xmmintrin.h>
  #define IRONCLAD_DENORMALS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define IRONCLAD_DENORMALS_ARM64 1
#endif

namespace ironclad
{

class ScopedNoDenormals
{
public:
  ScopedNoDenormals() noexcept
  {
#if defined(IRONCLAD_DENORMALS_X86)
    mPrevious = _mm_getcsr();
    _mm_setcsr(mPrevious | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(IRONCLAD_DENORMALS_ARM64) && !defined(_MSC_VER)
    std::uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    mPrevious = fpcr;
    fpcr |= (1ull << 24); // FZ
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
  }

  ~ScopedNoDenormals() noexcept
  {
#if defined(IRONCLAD_DENORMALS_X86)
    _mm_setcsr(static_cast<unsigned int>(mPrevious));
#elif defined(IRONCLAD_DENORMALS_ARM64) && !defined(_MSC_VER)
    const std::uint64_t fpcr = mPrevious;
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
  }

  ScopedNoDenormals(const ScopedNoDenormals&) = delete;
  ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
  std::uint64_t mPrevious = 0;
};

} // namespace ironclad
