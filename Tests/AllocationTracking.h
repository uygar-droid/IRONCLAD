#pragma once

// Counts heap allocations/deallocations made by the current thread while a
// ScopedAllocationCheck is alive. The global operator new/delete replacements
// live in AllocationTracking.cpp and are linked into the test executable only.

#include <cstddef>

namespace ironclad::test
{

struct AllocationCounts
{
  std::size_t allocations = 0;
  std::size_t deallocations = 0;
};

void beginTracking() noexcept;
AllocationCounts endTracking() noexcept;

class ScopedAllocationCheck
{
public:
  ScopedAllocationCheck() noexcept { beginTracking(); }
  ~ScopedAllocationCheck() noexcept
  {
    if (!mEnded)
      endTracking();
  }
  AllocationCounts finish() noexcept
  {
    mEnded = true;
    return endTracking();
  }

private:
  bool mEnded = false;
};

} // namespace ironclad::test
