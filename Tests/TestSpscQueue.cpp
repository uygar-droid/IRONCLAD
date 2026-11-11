#include "doctest/doctest.h"

#include <atomic>
#include <memory>
#include <thread>

#include "AllocationTracking.h"
#include "Utilities/SpscQueue.h"

using namespace ironclad;

TEST_CASE("SpscQueue basic FIFO semantics and capacity")
{
  SpscQueue<int, 8> q;
  CHECK(q.empty());
  CHECK(SpscQueue<int, 8>::capacity() == 7);
  for (int i = 0; i < 7; ++i)
    CHECK(q.push(i));
  CHECK_FALSE(q.push(99)); // full
  int v = -1;
  for (int i = 0; i < 7; ++i)
  {
    REQUIRE(q.pop(v));
    CHECK(v == i);
  }
  CHECK_FALSE(q.pop(v));
  CHECK(q.empty());
}

TEST_CASE("SpscQueue moves ownership of move-only payloads")
{
  SpscQueue<std::unique_ptr<int>, 4> q;
  auto p = std::make_unique<int>(42);
  CHECK(q.push(std::move(p)));
  CHECK(p == nullptr);
  std::unique_ptr<int> out;
  REQUIRE(q.pop(out));
  CHECK(*out == 42);
}

TEST_CASE("SpscQueue push/pop never allocate")
{
  SpscQueue<int*, 16> q;
  int x = 0;
  int* out = nullptr;
  test::ScopedAllocationCheck check;
  for (int i = 0; i < 1000; ++i)
  {
    q.push(&x);
    q.pop(out);
  }
  CHECK(check.finish().allocations == 0);
}

TEST_CASE("SpscQueue transfers 1,000,000 items across threads in order")
{
  static SpscQueue<std::uint32_t, 1024> q; // static: large, keep off the stack
  constexpr std::uint32_t kCount = 1'000'000;
  std::atomic<bool> ok{true};

  std::thread consumer([&] {
    std::uint32_t expected = 0, v = 0;
    while (expected < kCount)
    {
      if (q.pop(v))
      {
        if (v != expected)
          ok = false;
        ++expected;
      }
      else
        std::this_thread::yield();
    }
  });

  for (std::uint32_t i = 0; i < kCount; ++i)
    while (!q.push(i))
      std::this_thread::yield();

  consumer.join();
  CHECK(ok.load());
}
