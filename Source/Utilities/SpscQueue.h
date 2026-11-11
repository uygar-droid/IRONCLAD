#pragma once

// Fixed-capacity, wait-free single-producer / single-consumer queue.
// All storage is allocated at construction; push() and pop() never allocate,
// never lock and never block, so either end may be the audio thread.

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace ironclad
{

template <typename T, std::size_t Capacity>
class SpscQueue
{
  static_assert(Capacity >= 2, "Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
  static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move-assignable");

public:
  SpscQueue() = default;
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  /// Producer side. Returns false (and leaves `value` untouched) when full.
  bool push(T&& value) noexcept
  {
    const std::size_t head = mHead.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) & kMask;
    if (next == mTail.load(std::memory_order_acquire))
      return false;
    mSlots[head] = std::move(value);
    mHead.store(next, std::memory_order_release);
    return true;
  }

  bool push(const T& value) noexcept
  {
    T copy = value;
    return push(std::move(copy));
  }

  /// Consumer side. Returns false when empty.
  bool pop(T& out) noexcept
  {
    const std::size_t tail = mTail.load(std::memory_order_relaxed);
    if (tail == mHead.load(std::memory_order_acquire))
      return false;
    out = std::move(mSlots[tail]);
    mTail.store((tail + 1) & kMask, std::memory_order_release);
    return true;
  }

  /// Approximate when called concurrently; exact when both ends are idle.
  bool empty() const noexcept
  {
    return mTail.load(std::memory_order_acquire) == mHead.load(std::memory_order_acquire);
  }

  /// One slot is kept free to distinguish full from empty.
  static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
  static constexpr std::size_t kMask = Capacity - 1;
  static constexpr std::size_t kLine = 64; // typical cache line; avoids false sharing

  alignas(kLine) std::atomic<std::size_t> mHead{0};
  alignas(kLine) std::atomic<std::size_t> mTail{0};
  alignas(kLine) std::array<T, Capacity> mSlots{};
};

} // namespace ironclad
