#include "AllocationTracking.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local bool tTracking = false;
thread_local std::size_t tAllocs = 0;
thread_local std::size_t tFrees = 0;

inline void noteAlloc() noexcept
{
  if (tTracking)
    ++tAllocs;
}
inline void noteFree(void* p) noexcept
{
  if (tTracking && p != nullptr)
    ++tFrees;
}

void* allocOrThrow(std::size_t size)
{
  noteAlloc();
  if (size == 0)
    size = 1;
  if (void* p = std::malloc(size))
    return p;
  throw std::bad_alloc();
}

void* alignedAllocOrThrow(std::size_t size, std::size_t alignment)
{
  noteAlloc();
  if (size == 0)
    size = 1;
#if defined(_MSC_VER)
  if (void* p = _aligned_malloc(size, alignment))
    return p;
#else
  void* p = nullptr;
  if (alignment < sizeof(void*))
    alignment = sizeof(void*);
  if (posix_memalign(&p, alignment, size) == 0)
    return p;
#endif
  throw std::bad_alloc();
}

void alignedFree(void* p) noexcept
{
  noteFree(p);
#if defined(_MSC_VER)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
} // namespace

namespace ironclad::test
{
void beginTracking() noexcept
{
  tAllocs = 0;
  tFrees = 0;
  tTracking = true;
}

AllocationCounts endTracking() noexcept
{
  tTracking = false;
  return {tAllocs, tFrees};
}
} // namespace ironclad::test

// ---- Global replacements -----------------------------------------------------

void* operator new(std::size_t size) { return allocOrThrow(size); }
void* operator new[](std::size_t size) { return allocOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
  try { return allocOrThrow(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
  try { return allocOrThrow(size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t al) { return alignedAllocOrThrow(size, static_cast<std::size_t>(al)); }
void* operator new[](std::size_t size, std::align_val_t al) { return alignedAllocOrThrow(size, static_cast<std::size_t>(al)); }

void operator delete(void* p) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p) noexcept { noteFree(p); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { noteFree(p); std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { noteFree(p); std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
