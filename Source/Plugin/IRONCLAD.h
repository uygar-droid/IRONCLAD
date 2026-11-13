#pragma once

// iPlug2 adapter. Thin by design: it maps host parameters, audio buffers and state
// onto ironclad::Engine and builds the editor. No DSP lives here; preset, undo and MIDI
// logic live in the framework-independent Presets/ and Midi/ modules.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IPlug_include_in_plug_hdr.h"

#include "Audio/Engine.h"
#include "Effects/TapTempo.h"
#include "Midi/MidiMap.h"
#include "Presets/PresetData.h"
#include "Presets/PresetLibrary.h"
#include "Presets/UndoStack.h"
#include "Utilities/SpscQueue.h"

using namespace iplug;
using namespace igraphics;

enum ECtrlTags
{
  kCtrlTagCaptureName = 0,
  kCtrlTagCaptureMeta,
  kCtrlTagCaptureStatus,
  kCtrlTagIrName0,
  kCtrlTagIrName1,
  kCtrlTagIrInfo0,
  kCtrlTagIrInfo1,
  kCtrlTagIrStatus0,
  kCtrlTagIrStatus1,
  kCtrlTagIrWave0,
  kCtrlTagIrWave1,
  kCtrlTagPresetName,
  kCtrlTagPresetStatus,
  kCtrlTagFavorite,
  kCtrlTagUndo,
  kCtrlTagRedo,
  kCtrlTagMidi,
  kNumCtrlTags
};

class IRONCLAD final : public Plugin
{
public:
  IRONCLAD(const InstanceInfo& info);

  // State: see Presets/StateCodec.h (v3 JSON; M1, v1 and v2 states are still read).
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

  void OnIdle() override;
  void OnUIOpen() override;
  void OnParamChangeUI(int paramIdx, EParamSource source) override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

private:
  // ---- engine / files ----
  void PushAllParametersToEngine();
  void RefreshCaptureDisplay(bool force);
  void RefreshIrDisplay(bool force);
  void BrowseForCapture();
  void BrowseForIr(int slot);
  void LoadCaptureFile(const std::string& path, bool recordUndo);
  void LoadIrFile(int slot, const std::string& path, bool recordUndo);
  void ShowPage(int page);
  void TapDelayTempo(double timeSeconds);

  // ---- presets ----
  ironclad::PresetLibrary& Library();
  ironclad::PresetData CurrentPresetData() const;
  /// Applies a preset / session state. Files are resolved (moved / renamed files are found
  /// again); a file that cannot be found is reported, never fatal.
  void ApplyPreset(const ironclad::PresetData& p, const std::string& presetDir, bool fadeTransition, bool recordUndo,
                   bool informHost);
  ironclad::FileRef FileRefFor(const std::string& path) const;
  void RememberFileRef(const std::string& path);
  void LoadPresetIndex(int index);
  void StepPreset(int delta);
  void SavePreset(bool saveAs);
  void SavePresetAs(const std::string& name);
  void RenamePreset(const std::string& name);
  void DuplicatePreset();
  void DeletePreset();
  void ToggleFavorite();
  void SetPresetStatus(const std::string& message, bool isError);
  void RefreshPresetBar();
  int CurrentPresetIndex();
  bool CurrentPresetWritable();
  void LoadDefaultPreset();
  std::vector<std::string> FileSearchRoots(const std::string& savedPath) const;

  // ---- undo ----
  ironclad::UndoSnapshot CurrentSnapshot() const;
  void ApplySnapshot(const ironclad::UndoSnapshot& s);
  void ApplyParameterValues(const double* values, bool informHost);
  void Undo();
  void Redo();

  // ---- MIDI ----
  void DrainMidiEvents();
  void SetParamFromPlugin(int idx, double plainValue);
  void SaveDefaultMidiMap();

  // ---- UI helpers ----
  void ShowPresetMenu(IControl* pCaller);
  void ShowPresetActionsMenu(IControl* pCaller);
  void ShowMidiMenu(IControl* pCaller);
  void PromptForName(const std::string& initial, std::function<void(const std::string&)> done);

  ironclad::Engine mEngine;
  std::uint64_t mShownStatusSerial = UINT64_MAX;
  std::uint64_t mShownIrSerial = UINT64_MAX;
  WDL_String mLastCaptureFile, mLastCaptureDir;
  WDL_String mLastIrFile, mLastIrDir;
  int mPage = 1; // 0 STOMP, 1 AMP, 2 CAB, 3 POST FX
  ironclad::TapTempo mTapTempo;

  // Presets (UI thread).
  std::unique_ptr<ironclad::PresetLibrary> mLibrary;
  std::string mPresetName = "Default";
  std::string mPresetCategory;
  std::string mPresetPath; // "" = factory preset or none
  bool mPresetModified = false;
  std::string mPresetStatus;
  bool mPresetStatusIsError = false;
  double mPresetStatusTime = 0.0;

  // Undo (UI thread). mCommitted mirrors the parameter values the undo history knows about.
  ironclad::UndoStack mUndo;
  bool mPresetBarDirty = true;
  std::array<double, ironclad::kNumParams> mCommitted{};
  int mSuppressUndo = 0; // > 0 while the plugin itself is changing parameters

  // MIDI: the audio thread applies CCs to the engine at once and queues them here so the
  // host, the UI and the undo history follow (drained in OnIdle).
  ironclad::MidiMap mMidi;
  ironclad::SpscQueue<ironclad::MidiEvent, 512> mMidiEvents;
  bool mLearnOnTouch = false;
  bool mLearnToggle = false;

  // Content hashes of loaded files (computed once on the UI thread, read by SerializeState).
  mutable std::mutex mFileRefMutex;
  std::map<std::string, ironclad::FileRef> mFileRefs;

  // Popup menus must outlive an asynchronous (macOS) menu session.
  IPopupMenu mPresetMenu, mActionsMenu, mMidiPopup;
};
