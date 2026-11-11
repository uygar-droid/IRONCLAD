#include "Amp/CaptureLoader.h"

#include <vector>

namespace ironclad
{

CaptureLoader::CaptureLoader(AmpQueue& ready, AmpQueue& retire)
: mReady(ready)
, mRetire(retire)
, mThread([this] { run(); })
{
}

CaptureLoader::~CaptureLoader()
{
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mQuit = true;
  }
  mCv.notify_all();
  if (mThread.joinable())
    mThread.join();
  drainRetired();
}

void CaptureLoader::requestLoad(const std::string& utf8Path)
{
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mPending = Request{false, utf8Path};
    mStatus.state = CaptureStatus::State::Loading;
    mStatus.requestedPath = utf8Path;
    ++mStatus.serial;
  }
  mCv.notify_all();
}

void CaptureLoader::requestClear()
{
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mPending = Request{true, {}};
    mStatus.state = CaptureStatus::State::Loading;
    mStatus.requestedPath.clear();
    ++mStatus.serial;
  }
  mCv.notify_all();
}

CaptureStatus CaptureLoader::status() const
{
  std::lock_guard<std::mutex> lock(mMutex);
  return mStatus;
}

bool CaptureLoader::waitUntilIdle(std::chrono::milliseconds timeout) const
{
  std::unique_lock<std::mutex> lock(mMutex);
  return mCv.wait_for(lock, timeout, [this] { return !mBusy && (!mPending || !mConfigured); });
}

void CaptureLoader::drainRetired()
{
  PreparedAmp* p = nullptr;
  while (mRetire.pop(p))
    delete p; // destroys the capture off the audio thread
}

void CaptureLoader::run()
{
  std::unique_lock<std::mutex> lock(mMutex);
  while (!mQuit)
  {
    mCv.wait_for(lock, std::chrono::milliseconds(20), [this] { return mQuit || (mPending && mConfigured); });
    if (mQuit)
      break;

    lock.unlock();
    drainRetired();
    lock.lock();

    if (!(mPending && mConfigured))
      continue;

    const Request request = *mPending;
    mPending.reset();
    mBusy = true;
    double sampleRate = mSampleRate;
    int maxBlock = mMaxBlock;
    std::uint64_t generation = mGeneration;
    lock.unlock();

    // ---- Build outside the lock (slow: file I/O, network construction, prewarm) ----
    auto item = std::make_unique<PreparedAmp>();
    std::string error, detail;
    CaptureInfo info;
    try
    {
      if (!request.clear)
      {
        item->model = AmpModel::load(request.path);
        item->model->prepare(sampleRate, maxBlock);
        info = item->model->info();
      }
    }
    catch (const CaptureLoadError& e)
    {
      error = e.what();
      detail = e.technicalDetail();
    }
    catch (const std::exception& e)
    {
      error = "Could not load capture.";
      detail = e.what();
    }
    catch (...)
    {
      error = "Could not load capture.";
      detail = "Unknown error";
    }

    lock.lock();
    // The host may have changed sample rate / block size while we were building.
    while (error.empty() && item->model && generation != mGeneration)
    {
      sampleRate = mSampleRate;
      maxBlock = mMaxBlock;
      generation = mGeneration;
      lock.unlock();
      try
      {
        item->model->prepare(sampleRate, maxBlock);
      }
      catch (const std::exception& e)
      {
        error = "Could not prepare capture for the current sample rate.";
        detail = e.what();
      }
      lock.lock();
    }

    const bool superseded = mPending.has_value(); // a newer request arrived meanwhile
    if (error.empty())
    {
      item->generation = generation;
      PreparedAmp* raw = item.release();
      while (!mReady.push(std::move(raw)))
      {
        // Queue full (audio thread not running?). Wait without holding the lock.
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        drainRetired();
        lock.lock();
        if (mQuit)
        {
          delete raw;
          break;
        }
      }
      if (!superseded)
      {
        mStatus.state = request.clear ? CaptureStatus::State::Empty : CaptureStatus::State::Ready;
        mStatus.hasActive = !request.clear;
        mStatus.active = request.clear ? CaptureInfo{} : info;
        mStatus.resampling = !request.clear && info.sampleRate > 0.0 && info.sampleRate != sampleRate;
        mStatus.errorMessage.clear();
        mStatus.errorDetail.clear();
        ++mStatus.serial;
      }
    }
    else if (!superseded)
    {
      // Keep whatever was playing; report the failure. requestedPath keeps the failed
      // path so a missing file is not silently dropped from the project.
      mStatus.state = CaptureStatus::State::Error;
      mStatus.errorMessage = error;
      mStatus.errorDetail = detail;
      ++mStatus.serial;
    }
    mBusy = false;
    mCv.notify_all();
  }
}

} // namespace ironclad
