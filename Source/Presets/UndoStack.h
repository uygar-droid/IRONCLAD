#pragma once

// Undo / redo of the sound (UI thread only).
//
// Entries are complete snapshots (every parameter + the loaded files), so undo can never
// leave the plugin in a half-applied state. Edits to the same control in quick succession
// (a knob drag, mouse-wheel steps) are coalesced into one undo step.

#include <array>
#include <deque>
#include <optional>
#include <string>

#include "Parameters/Parameters.h"

namespace ironclad
{

struct UndoSnapshot
{
  std::array<double, kNumParams> values{};
  std::string capture;
  std::string ir[2];
  bool operator==(const UndoSnapshot&) const = default;
};

class UndoStack
{
public:
  static constexpr std::size_t kCapacity = 200;
  static constexpr double kCoalesceSeconds = 1.0;
  static constexpr int kNoCoalesce = -1;

  /// Call BEFORE applying an edit, with the state as it was. `key` identifies the control
  /// (parameter index) for coalescing; kNoCoalesce for one-off edits (file load, preset).
  void record(const UndoSnapshot& before, int key, double nowSeconds)
  {
    mRedo.clear();
    if (key != kNoCoalesce && key == mLastKey && nowSeconds - mLastTime < kCoalesceSeconds && !mUndo.empty())
    {
      mLastTime = nowSeconds;
      return; // same gesture: the earlier "before" state stays the undo point
    }
    if (!mUndo.empty() && mUndo.back() == before)
    {
      mLastKey = key;
      mLastTime = nowSeconds;
      return;
    }
    mUndo.push_back(before);
    if (mUndo.size() > kCapacity)
      mUndo.pop_front();
    mLastKey = key;
    mLastTime = nowSeconds;
  }

  /// Returns the state to restore, and remembers `current` for redo.
  std::optional<UndoSnapshot> undo(const UndoSnapshot& current)
  {
    if (mUndo.empty())
      return std::nullopt;
    UndoSnapshot s = mUndo.back();
    mUndo.pop_back();
    mRedo.push_back(current);
    breakCoalescing();
    return s;
  }

  std::optional<UndoSnapshot> redo(const UndoSnapshot& current)
  {
    if (mRedo.empty())
      return std::nullopt;
    UndoSnapshot s = mRedo.back();
    mRedo.pop_back();
    mUndo.push_back(current);
    breakCoalescing();
    return s;
  }

  bool canUndo() const noexcept { return !mUndo.empty(); }
  bool canRedo() const noexcept { return !mRedo.empty(); }
  std::size_t undoDepth() const noexcept { return mUndo.size(); }
  void breakCoalescing() noexcept { mLastKey = kNoCoalesce; }
  void clear()
  {
    mUndo.clear();
    mRedo.clear();
    breakCoalescing();
  }

private:
  std::deque<UndoSnapshot> mUndo, mRedo;
  int mLastKey = kNoCoalesce;
  double mLastTime = -1.0e9;
};

} // namespace ironclad
