#pragma once

// Background thread that loads/prepares impulse responses for the two cabinet slots and
// destroys IRs the audio thread has retired. Same contract as CaptureLoader: requests
// coalesce per slot (newest wins), failures keep the current IR playing, nothing here
// runs on the audio thread.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "Cabinet/CabStage.h"

namespace ironclad
{

struct IrSlotStatus
{
  enum class State
  {
    Empty,
    Loading,
    Ready,
    Error
  };
  State state = State::Empty;
  std::string requestedPath;
  bool hasActive = false;
  IrInfo active;
  std::string errorMessage;
  std::string errorDetail;
};

struct IrStatus
{
  IrSlotStatus slot[CabStage::kSlots];
  std::uint64_t serial = 0;
};

class IrLoader
{
public:
  IrLoader(IrQueue& ready, IrQueue& retire);
  ~IrLoader();
  IrLoader(const IrLoader&) = delete;
  IrLoader& operator=(const IrLoader&) = delete;

  void requestLoad(int slot, const std::string& utf8Path);
  void requestClear(int slot);
  IrStatus status() const;

  template <typename Fn>
  void reconfigure(double sampleRate, Fn&& fn)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mSampleRate = sampleRate;
    ++mGeneration;
    mConfigured = true;
    fn(mGeneration);
    mCv.notify_all();
  }

  bool waitUntilIdle(std::chrono::milliseconds timeout) const;

private:
  struct Request
  {
    bool clear = false;
    std::string path;
  };

  bool hasWork() const { return mConfigured && (mPending[0] || mPending[1]); }
  void run();
  void drainRetired();

  IrQueue& mReady;
  IrQueue& mRetire;

  mutable std::mutex mMutex;
  mutable std::condition_variable mCv;
  std::optional<Request> mPending[CabStage::kSlots];
  bool mBusy = false;
  bool mQuit = false;
  bool mConfigured = false;
  double mSampleRate = 48000.0;
  std::uint64_t mGeneration = 0;
  IrStatus mStatus;

  std::thread mThread;
};

} // namespace ironclad
