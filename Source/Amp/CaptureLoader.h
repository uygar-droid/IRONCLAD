#pragma once

// Background thread that loads and prepares NAM captures, and destroys captures
// the audio thread has retired. UI/host threads talk to it with requestLoad() /
// requestClear() and read results with status(). Nothing here runs on the audio thread.
//
// Requests coalesce: if several loads are requested while one is being built,
// only the most recent one is performed afterwards.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "Amp/AmpStage.h"
#include "Amp/CaptureInfo.h"

namespace ironclad
{

struct CaptureStatus
{
  enum class State
  {
    Empty,   // no capture requested
    Loading, // a request is being processed
    Ready,   // `active` is playing (or about to, within one audio block)
    Error    // the last request failed; `active` (if any) keeps playing
  };

  State state = State::Empty;
  std::string requestedPath; // what the user/project asked for last ("" = none). Persisted in state.
  bool hasActive = false;
  CaptureInfo active;
  bool resampling = false;
  std::string errorMessage; // user-facing
  std::string errorDetail;  // technical, for the advanced/details view
  std::uint64_t serial = 0; // increments on every change (cheap change detection for the UI)
};

class CaptureLoader
{
public:
  CaptureLoader(AmpQueue& ready, AmpQueue& retire);
  ~CaptureLoader();
  CaptureLoader(const CaptureLoader&) = delete;
  CaptureLoader& operator=(const CaptureLoader&) = delete;

  void requestLoad(const std::string& utf8Path);
  void requestClear();
  CaptureStatus status() const;

  /// Called by Engine::prepare with the audio stopped. `fn` runs under the loader
  /// lock so that no prepared capture is pushed while the stage is re-prepared.
  template <typename Fn>
  void reconfigure(double sampleRate, int maxBlockSize, Fn&& fn)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mSampleRate = sampleRate;
    mMaxBlock = maxBlockSize;
    ++mGeneration;
    mConfigured = true;
    fn(mGeneration);
    mCv.notify_all();
  }

  /// Test helper: waits until no request is pending or being built.
  bool waitUntilIdle(std::chrono::milliseconds timeout) const;

private:
  struct Request
  {
    bool clear = false;
    std::string path;
  };

  void run();
  void drainRetired();

  AmpQueue& mReady;
  AmpQueue& mRetire;

  mutable std::mutex mMutex;
  mutable std::condition_variable mCv;
  std::optional<Request> mPending;
  bool mBusy = false;
  bool mQuit = false;
  bool mConfigured = false;
  double mSampleRate = 48000.0;
  int mMaxBlock = 512;
  std::uint64_t mGeneration = 0;
  CaptureStatus mStatus;

  std::thread mThread; // last member: started after everything above is initialised
};

} // namespace ironclad
