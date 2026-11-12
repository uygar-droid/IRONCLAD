#include "Cabinet/IrLoader.h"

#include "Cabinet/WavReader.h"

namespace ironclad
{

IrLoader::IrLoader(IrQueue& ready, IrQueue& retire)
: mReady(ready)
, mRetire(retire)
, mThread([this] { run(); })
{
}

IrLoader::~IrLoader()
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

void IrLoader::requestLoad(int slot, const std::string& utf8Path)
{
  if (slot < 0 || slot >= CabStage::kSlots)
    return;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mPending[slot] = Request{false, utf8Path};
    mStatus.slot[slot].state = IrSlotStatus::State::Loading;
    mStatus.slot[slot].requestedPath = utf8Path;
    ++mStatus.serial;
  }
  mCv.notify_all();
}

void IrLoader::requestClear(int slot)
{
  if (slot < 0 || slot >= CabStage::kSlots)
    return;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mPending[slot] = Request{true, {}};
    mStatus.slot[slot].state = IrSlotStatus::State::Loading;
    mStatus.slot[slot].requestedPath.clear();
    ++mStatus.serial;
  }
  mCv.notify_all();
}

IrStatus IrLoader::status() const
{
  std::lock_guard<std::mutex> lock(mMutex);
  return mStatus;
}

bool IrLoader::waitUntilIdle(std::chrono::milliseconds timeout) const
{
  std::unique_lock<std::mutex> lock(mMutex);
  return mCv.wait_for(lock, timeout, [this] { return !mBusy && !hasWork(); });
}

void IrLoader::drainRetired()
{
  PreparedIr* p = nullptr;
  while (mRetire.pop(p))
    delete p;
}

void IrLoader::run()
{
  std::unique_lock<std::mutex> lock(mMutex);
  while (!mQuit)
  {
    mCv.wait_for(lock, std::chrono::milliseconds(20), [this] { return mQuit || hasWork(); });
    if (mQuit)
      break;
    lock.unlock();
    drainRetired();
    lock.lock();
    if (!hasWork())
      continue;

    const int slot = mPending[0] ? 0 : 1;
    const Request request = *mPending[slot];
    mPending[slot].reset();
    mBusy = true;
    double sampleRate = mSampleRate;
    std::uint64_t generation = mGeneration;
    lock.unlock();

    auto item = std::make_unique<PreparedIr>();
    item->slot = slot;
    std::string error, detail;
    try
    {
      if (!request.clear)
      {
        item->ir = ImpulseResponse::load(request.path);
        item->ir->prepare(sampleRate);
      }
    }
    catch (const AudioFileError& e)
    {
      error = e.what();
      detail = e.technicalDetail();
    }
    catch (const std::exception& e)
    {
      error = "Could not load impulse response.";
      detail = e.what();
    }
    catch (...)
    {
      error = "Could not load impulse response.";
      detail = "Unknown error";
    }

    lock.lock();
    while (error.empty() && item->ir && generation != mGeneration)
    {
      sampleRate = mSampleRate;
      generation = mGeneration;
      lock.unlock();
      try
      {
        item->ir->prepare(sampleRate);
      }
      catch (const std::exception& e)
      {
        error = "Could not prepare the IR for the current sample rate.";
        detail = e.what();
      }
      lock.lock();
    }

    const bool superseded = mPending[slot].has_value();
    IrSlotStatus& st = mStatus.slot[slot];
    if (error.empty())
    {
      IrInfo info = item->ir ? item->ir->info() : IrInfo{};
      item->generation = generation;
      PreparedIr* raw = item.release();
      while (!mReady.push(std::move(raw)))
      {
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
        st.state = request.clear ? IrSlotStatus::State::Empty : IrSlotStatus::State::Ready;
        st.hasActive = !request.clear;
        st.active = request.clear ? IrInfo{} : std::move(info);
        st.errorMessage.clear();
        st.errorDetail.clear();
        ++mStatus.serial;
      }
    }
    else if (!superseded)
    {
      st.state = IrSlotStatus::State::Error;
      st.errorMessage = error;
      st.errorDetail = detail;
      ++mStatus.serial;
    }
    mBusy = false;
    mCv.notify_all();
  }
}

} // namespace ironclad
