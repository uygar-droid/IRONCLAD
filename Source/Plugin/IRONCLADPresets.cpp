// IRONCLAD plugin: presets, undo/redo and MIDI (UI-thread glue around the engine-side
// Presets/ and Midi/ modules). Split from IRONCLAD.cpp, which holds the iPlug2 entry point.

#include "IRONCLAD.h"

#if IPLUG_EDITOR
  #include "IControls.h"
  #include "PluginControls.h"
#endif

#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include "Presets/FileResolver.h"
#include "Presets/StateCodec.h"
#include "Utilities/Paths.h"

namespace
{
double NowSeconds()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string ParentDir(const std::string& utf8Path)
{
  if (utf8Path.empty())
    return {};
  return ironclad::utf8FromPath(ironclad::pathFromUtf8(utf8Path).parent_path());
}

std::string TargetName(int target)
{
  if (const char* action = ironclad::midiActionName(target))
    return action;
  if (target >= 0 && target < ironclad::kNumParams)
    return std::string(ironclad::paramSpec(target).name);
  return "?";
}

constexpr int kMenuDefault = -3;
const IColor kStatusAmber(255, 255, 176, 64);
const IColor kStatusRed(255, 255, 92, 80);
} // namespace

// ============================================================================ files

ironclad::FileRef IRONCLAD::FileRefFor(const std::string& path) const
{
  {
    std::lock_guard<std::mutex> lock(mFileRefMutex);
    auto it = mFileRefs.find(path);
    if (it != mFileRefs.end())
      return it->second;
  }
  ironclad::FileRef r; // not hashed yet: the path and name still identify it
  r.path = path;
  r.fileName = ironclad::utf8FromPath(ironclad::pathFromUtf8(path).filename());
  return r;
}

void IRONCLAD::RememberFileRef(const std::string& path)
{
  if (path.empty())
    return;
  {
    std::lock_guard<std::mutex> lock(mFileRefMutex);
    if (mFileRefs.count(path))
      return;
  }
  ironclad::FileRef r = ironclad::makeFileRef(path, {}); // hashes the file (UI thread, once per file)
  std::lock_guard<std::mutex> lock(mFileRefMutex);
  if (mFileRefs.size() > 256)
    mFileRefs.clear();
  mFileRefs[path] = r;
}

void IRONCLAD::LoadCaptureFile(const std::string& path, bool recordUndo)
{
  if (recordUndo)
    mUndo.record(CurrentSnapshot(), ironclad::UndoStack::kNoCoalesce, NowSeconds());
  if (path.empty())
    mEngine.clearCapture();
  else
  {
    RememberFileRef(path);
    mEngine.loadCapture(path);
  }
  if (recordUndo)
  {
    mPresetModified = true;
    mPresetBarDirty = true;
  }
  RefreshCaptureDisplay(true);
}

void IRONCLAD::LoadIrFile(int slot, const std::string& path, bool recordUndo)
{
  if (recordUndo)
    mUndo.record(CurrentSnapshot(), ironclad::UndoStack::kNoCoalesce, NowSeconds());
  if (path.empty())
    mEngine.clearIr(slot);
  else
  {
    RememberFileRef(path);
    mEngine.loadIr(slot, path);
  }
  if (recordUndo)
  {
    mPresetModified = true;
    mPresetBarDirty = true;
  }
  RefreshIrDisplay(true);
}

std::vector<std::string> IRONCLAD::FileSearchRoots(const std::string& savedPath) const
{
  std::vector<std::string> roots;
  auto add = [&roots](const std::string& p) {
    if (!p.empty() && std::find(roots.begin(), roots.end(), p) == roots.end())
      roots.push_back(p);
  };
  add(ironclad::userDataDir()); // the whole IRONCLAD folder (Packs, User, ...)
  add(mLastCaptureDir.Get());
  add(mLastIrDir.Get());
  // The folder the file used to be in, one level up (e.g. a renamed sub-folder).
  add(ParentDir(ParentDir(savedPath)));
  return roots;
}

// ============================================================================ presets

ironclad::PresetLibrary& IRONCLAD::Library()
{
  if (!mLibrary)
  {
    // Creates IRONCLAD/{IRONCLAD, Packs, User, Default.ironclad} on first use. When the shared
    // folder (macOS: /Library/Audio/Presets/IRONCLAD) is in use, presets saved earlier in the
    // per-user folder are moved into it first.
    mLibrary = std::make_unique<ironclad::PresetLibrary>(ironclad::userDataDir());
    if (ironclad::userDataDir() != ironclad::perUserDataDir())
      mLibrary->importFrom(ironclad::perUserDataDir());
    mLibrary->ensureLayout();
  }
  return *mLibrary;
}

ironclad::PresetData IRONCLAD::CurrentPresetData() const
{
  ironclad::PresetData p;
  p.name = mPresetName;
  p.category = mPresetCategory;
  for (int i = 0; i < ironclad::kNumParams; ++i)
    p.params[std::string(ironclad::paramSpec(i).id)] = mEngine.getParameter(i);
  const std::string cap = mEngine.captureStatePath();
  if (!cap.empty())
    p.capture = FileRefFor(cap);
  for (int slot = 0; slot < 2; ++slot)
  {
    const std::string ir = mEngine.irStatePath(slot);
    if (!ir.empty())
      p.ir[slot] = FileRefFor(ir);
  }
  return p;
}

void IRONCLAD::ApplyPreset(const ironclad::PresetData& p, const std::string& presetDir, bool fadeTransition,
                           bool recordUndo, bool informHost)
{
  if (recordUndo)
    mUndo.record(CurrentSnapshot(), ironclad::UndoStack::kNoCoalesce, NowSeconds());

  // Fade out first; the engine applies the new values while silent, then fades back in.
  if (fadeTransition)
    mEngine.beginTransition();

  std::array<double, ironclad::kNumParams> values{};
  for (int i = 0; i < ironclad::kNumParams; ++i)
    values[static_cast<std::size_t>(i)] = p.valueFor(i);
  ++mSuppressUndo;
  ApplyParameterValues(values.data(), informHost);

  std::string missing;
  if (!p.templateOnly)
  {
    auto resolve = [&](const std::optional<ironclad::FileRef>& ref, const char* ext) -> std::string {
      if (!ref)
        return {};
      ironclad::ResolveOptions opt;
      opt.presetDir = presetDir;
      opt.searchRoots = FileSearchRoots(ref->path);
      opt.extensions = {ext};
      const ironclad::ResolvedFile found = ironclad::resolveFile(*ref, opt);
      if (found.how == ironclad::ResolvedFile::How::NotFound)
      {
        missing += (missing.empty() ? "" : ", ") + (ref->fileName.empty() ? ref->path : ref->fileName);
        return ref->path; // keep the reference: the loader reports it and the project keeps it
      }
      if (!ref->sha256.empty())
      {
        ironclad::FileRef known = *ref;
        known.path = found.path;
        std::lock_guard<std::mutex> lock(mFileRefMutex);
        mFileRefs[found.path] = known;
      }
      return found.path;
    };
    const std::string cap = resolve(p.capture, ".nam");
    if (cap != mEngine.captureStatePath())
      LoadCaptureFile(cap, false);
    for (int slot = 0; slot < 2; ++slot)
    {
      const std::string ir = resolve(p.ir[slot], ".wav");
      if (ir != mEngine.irStatePath(slot))
        LoadIrFile(slot, ir, false);
    }
  }
  --mSuppressUndo;

  mPresetName = p.name;
  mPresetCategory = p.category;
  mPresetModified = false;
  if (!missing.empty())
    SetPresetStatus("File not found: " + missing + " (put it in IRONCLAD's Captures / IRs folder, or load it again)",
                    true);
  else
    SetPresetStatus({}, false);
  mPresetBarDirty = true;
}

int IRONCLAD::CurrentPresetIndex()
{
  auto& lib = Library();
  if (!mPresetPath.empty())
    return lib.indexOfPath(mPresetPath);
  for (int i = 0; i < static_cast<int>(lib.entries().size()); ++i)
  {
    const auto& e = lib.entries()[static_cast<std::size_t>(i)];
    if (e.builtinIndex >= 0 && e.name == mPresetName && e.category == mPresetCategory)
      return i;
  }
  return -1;
}

bool IRONCLAD::CurrentPresetWritable()
{
  const int idx = CurrentPresetIndex();
  return idx >= 0 && !Library().entries()[static_cast<std::size_t>(idx)].readOnly();
}

void IRONCLAD::LoadPresetIndex(int index)
{
  auto& lib = Library();
  if (index < 0 || index >= static_cast<int>(lib.entries().size()))
    return;
  const ironclad::PresetEntry entry = lib.entries()[static_cast<std::size_t>(index)];
  try
  {
    const ironclad::PresetData p = lib.load(index);
    ApplyPreset(p, ParentDir(entry.path), true, true, true);
    mPresetPath = entry.path;
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus("Could not load \"" + entry.name + "\": " + ex.what(), true);
  }
  mPresetBarDirty = true;
}

void IRONCLAD::LoadDefaultPreset()
{
  ApplyPreset(Library().loadDefault(), Library().rootDir(), true, true, true);
  mPresetPath.clear();
  mPresetBarDirty = true;
}

void IRONCLAD::StepPreset(int delta)
{
  auto& lib = Library();
  lib.rescan();
  const int n = static_cast<int>(lib.entries().size());
  if (n == 0)
    return;
  int idx = CurrentPresetIndex();
  idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
  LoadPresetIndex(idx);
}

void IRONCLAD::SavePreset(bool saveAs)
{
  // Factory / pack presets and Default are read-only here: saving asks for a new name.
  if (saveAs || !CurrentPresetWritable())
  {
    PromptForName(mPresetName, [this](const std::string& name) { SavePresetAs(name); });
    return;
  }
  try
  {
    ironclad::PresetData p = CurrentPresetData();
    const auto& e = Library().entries()[static_cast<std::size_t>(CurrentPresetIndex())];
    p.name = e.name;
    p.category = e.category;
    mPresetPath = Library().save(p, true);
    mPresetModified = false;
    SetPresetStatus("Saved \"" + p.name + "\".", false);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Save failed: ") + ex.what(), true);
  }
  mPresetBarDirty = true;
}

void IRONCLAD::SavePresetAs(const std::string& rawName)
{
  // "Category / Name" puts the preset in a category folder.
  std::string category = CurrentPresetWritable() ? mPresetCategory : std::string{};
  std::string name = rawName;
  const auto slash = rawName.find('/');
  if (slash != std::string::npos)
  {
    auto trim = [](std::string s) {
      const auto a = s.find_first_not_of(' ');
      const auto b = s.find_last_not_of(' ');
      return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
    };
    category = trim(rawName.substr(0, slash));
    name = trim(rawName.substr(slash + 1));
  }
  try
  {
    ironclad::PresetData p = CurrentPresetData();
    p.name = name;
    p.category = category;
    auto& lib = Library();
    const std::string target = lib.pathFor(category, ironclad::PresetLibrary::sanitizeName(name));
    std::error_code ec;
    if (std::filesystem::exists(ironclad::pathFromUtf8(target), ec))
    {
#if IPLUG_EDITOR
      if (IGraphics* g = GetUI())
      {
        g->ShowMessageBox(("A preset named \"" + name + "\" already exists. Replace it?").c_str(), "IRONCLAD", kMB_YESNO,
                          [this, p](EMsgBoxResult result) {
                            if (result != kYES)
                              return;
                            try
                            {
                              mPresetPath = Library().save(p, true);
                              mPresetName = ironclad::PresetLibrary::sanitizeName(p.name);
                              mPresetCategory = p.category;
                              mPresetModified = false;
                              SetPresetStatus("Saved \"" + mPresetName + "\".", false);
                            }
                            catch (const std::exception& ex)
                            {
                              SetPresetStatus(std::string("Save failed: ") + ex.what(), true);
                            }
                            mPresetBarDirty = true;
                          });
        return;
      }
#endif
    }
    mPresetPath = lib.save(p, false);
    mPresetName = ironclad::PresetLibrary::sanitizeName(name);
    mPresetCategory = category;
    mPresetModified = false;
    SetPresetStatus("Saved \"" + mPresetName + "\".", false);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Save failed: ") + ex.what(), true);
  }
  mPresetBarDirty = true;
}

void IRONCLAD::RenamePreset(const std::string& name)
{
  try
  {
    auto& lib = Library();
    const int idx = lib.indexOfPath(mPresetPath);
    if (idx < 0)
      throw std::runtime_error("Save the preset first.");
    mPresetPath = lib.rename(idx, name);
    mPresetName = ironclad::utf8FromPath(ironclad::pathFromUtf8(mPresetPath).stem());
    SetPresetStatus("Renamed to \"" + mPresetName + "\".", false);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Rename failed: ") + ex.what(), true);
  }
  mPresetBarDirty = true;
}

void IRONCLAD::DuplicatePreset()
{
  try
  {
    auto& lib = Library();
    const int idx = CurrentPresetIndex();
    if (idx < 0)
      throw std::runtime_error("Save the preset first.");
    const std::string path = lib.duplicate(idx);
    LoadPresetIndex(lib.indexOfPath(path));
    SetPresetStatus("Duplicated as \"" + mPresetName + "\".", false);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Duplicate failed: ") + ex.what(), true);
  }
}

void IRONCLAD::DeletePreset()
{
  auto doDelete = [this]() {
    try
    {
      auto& lib = Library();
      const int idx = lib.indexOfPath(mPresetPath);
      if (idx < 0)
        throw std::runtime_error("The preset was not found.");
      const std::string name = mPresetName;
      lib.remove(idx);
      mPresetPath.clear();
      mPresetModified = true; // the sound is still loaded, but no longer saved anywhere
      SetPresetStatus("Deleted \"" + name + "\".", false);
    }
    catch (const std::exception& ex)
    {
      SetPresetStatus(std::string("Delete failed: ") + ex.what(), true);
    }
    mPresetBarDirty = true;
  };
  if (!CurrentPresetWritable())
  {
    SetPresetStatus("Factory and pack presets cannot be deleted.", true);
    return;
  }
#if IPLUG_EDITOR
  if (IGraphics* g = GetUI())
  {
    g->ShowMessageBox(("Delete the preset \"" + mPresetName + "\"?").c_str(), "IRONCLAD", kMB_YESNO,
                      [doDelete](EMsgBoxResult result) {
                        if (result == kYES)
                          doDelete();
                      });
    return;
  }
#endif
  doDelete();
}

void IRONCLAD::ToggleFavorite()
{
  auto& lib = Library();
  const int idx = CurrentPresetIndex();
  if (idx < 0)
  {
    SetPresetStatus("Save the preset first.", true);
    return;
  }
  try
  {
    lib.setFavorite(idx, !lib.entries()[static_cast<std::size_t>(idx)].favorite);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Could not update favourites: ") + ex.what(), true);
  }
  mPresetBarDirty = true;
}

void IRONCLAD::SetPresetStatus(const std::string& message, bool isError)
{
  mPresetStatus = message;
  mPresetStatusIsError = isError;
  mPresetStatusTime = NowSeconds();
  mPresetBarDirty = true;
}

void IRONCLAD::RefreshPresetBar()
{
#if IPLUG_EDITOR
  IGraphics* g = GetUI();
  if (!g)
    return;
  mPresetBarDirty = false;
  std::string label = mPresetCategory.empty() ? mPresetName : mPresetCategory + " / " + mPresetName;
  {
    const int i = CurrentPresetIndex();
    if (i >= 0 && Library().entries()[static_cast<std::size_t>(i)].isFactory())
      label += "  (factory)";
    else if (i >= 0 && Library().entries()[static_cast<std::size_t>(i)].source == ironclad::PresetEntry::Source::Pack)
      label += "  (pack)";
  }
  if (mPresetModified)
    label += "  *";
  if (auto* c = dynamic_cast<ironclad_ui::ClickTextControl*>(g->GetControlWithTag(kCtrlTagPresetName)))
    c->SetLabel(label);
  if (auto* c = dynamic_cast<ITextControl*>(g->GetControlWithTag(kCtrlTagPresetStatus)))
  {
    c->SetText(c->GetText().WithFGColor(mPresetStatusIsError ? kStatusRed : kStatusAmber));
    c->SetStr(mPresetStatus.c_str());
  }
  const int idx = CurrentPresetIndex();
  const bool favorite = idx >= 0 && Library().entries()[static_cast<std::size_t>(idx)].favorite;
  if (auto* c = dynamic_cast<IVButtonControl*>(g->GetControlWithTag(kCtrlTagFavorite)))
    c->SetLabelStr(favorite ? "FAV *" : "FAV");
  if (IControl* c = g->GetControlWithTag(kCtrlTagUndo))
    c->SetDisabled(!mUndo.canUndo());
  if (IControl* c = g->GetControlWithTag(kCtrlTagRedo))
    c->SetDisabled(!mUndo.canRedo());
  const bool learning = mLearnOnTouch || mMidi.learnTarget() != ironclad::kMidiNoTarget;
  if (auto* c = dynamic_cast<IVButtonControl*>(g->GetControlWithTag(kCtrlTagMidi)))
    c->SetLabelStr(learning ? "LEARN" : "MIDI");
  g->SetAllControlsDirty();
#endif
}

void IRONCLAD::PromptForName(const std::string& initial, std::function<void(const std::string&)> done)
{
#if IPLUG_EDITOR
  if (IGraphics* g = GetUI())
    if (auto* c = dynamic_cast<ironclad_ui::ClickTextControl*>(g->GetControlWithTag(kCtrlTagPresetName)))
    {
      SetPresetStatus("Type a name and press Enter (\"Category / Name\" files it in a category).", false);
      c->PromptText(initial, std::move(done));
    }
#endif
}

void IRONCLAD::ShowPresetMenu(IControl* pCaller)
{
#if IPLUG_EDITOR
  IGraphics* g = GetUI();
  if (!g || !pCaller)
    return;
  auto& lib = Library();
  lib.rescan();
  const int current = CurrentPresetIndex();
  mPresetMenu.Clear();
  mPresetMenu.SetFunction([this](IPopupMenu* pMenu) {
    if (IPopupMenu::Item* item = pMenu ? pMenu->GetChosenItem() : nullptr)
    {
      if (item->GetTag() >= 0)
        LoadPresetIndex(item->GetTag());
      else if (item->GetTag() == kMenuDefault)
        LoadDefaultPreset();
    }
  });
  const auto& entries = lib.entries();
  auto addItem = [&](IPopupMenu& menu, int i) {
    const auto& e = entries[static_cast<std::size_t>(i)];
    auto* item = menu.AddItem(new IPopupMenu::Item(e.name.c_str(), IPopupMenu::Item::kNoFlags, i));
    item->SetChecked(i == current);
  };

  // Favourites, then factory templates by category, then the user's presets by category.
  bool anyFavorite = false;
  for (const auto& e : entries)
    anyFavorite = anyFavorite || e.favorite;
  if (anyFavorite)
  {
    auto* fav = new IPopupMenu();
    mPresetMenu.AddItem("Favourites", fav);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
      if (entries[static_cast<std::size_t>(i)].favorite)
        addItem(*fav, i);
  }
  auto addGroup = [&](ironclad::PresetEntry::Source source, const char* title) {
    IPopupMenu* group = nullptr;
    std::map<std::string, IPopupMenu*> categories;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
      const auto& e = entries[static_cast<std::size_t>(i)];
      if (e.source != source)
        continue;
      if (!group)
      {
        group = new IPopupMenu();
        mPresetMenu.AddItem(title, group);
      }
      if (e.category.empty())
      {
        addItem(*group, i);
        continue;
      }
      IPopupMenu*& cat = categories[e.category];
      if (!cat)
      {
        cat = new IPopupMenu();
        group->AddItem(e.category.c_str(), cat); // parent first: the submenu inherits the function
      }
      addItem(*cat, i);
    }
    return group != nullptr;
  };
  mPresetMenu.AddItem(new IPopupMenu::Item("Default", IPopupMenu::Item::kNoFlags, kMenuDefault));
  mPresetMenu.AddSeparator();
  addGroup(ironclad::PresetEntry::Source::Factory, "IRONCLAD");
  addGroup(ironclad::PresetEntry::Source::Pack, "Packs");
  if (!addGroup(ironclad::PresetEntry::Source::User, "User"))
    mPresetMenu.AddItem(new IPopupMenu::Item("(no saved presets yet: use SAVE)", IPopupMenu::Item::kDisabled, -1));
  g->CreatePopupMenu(*pCaller, mPresetMenu, pCaller->GetRECT());
#endif
}

void IRONCLAD::ShowPresetActionsMenu(IControl* pCaller)
{
#if IPLUG_EDITOR
  IGraphics* g = GetUI();
  if (!g || !pCaller)
    return;
  enum { kSaveAs = 1, kRename, kDuplicate, kDelete, kRescan, kNewInit, kSaveDefault, kResetDefault };
  const bool isUser = CurrentPresetWritable();
  mActionsMenu.Clear();
  mActionsMenu.SetFunction([this](IPopupMenu* pMenu) {
    IPopupMenu::Item* item = pMenu ? pMenu->GetChosenItem() : nullptr;
    if (!item)
      return;
    switch (item->GetTag())
    {
    case kSaveAs: SavePreset(true); break;
    case kRename: PromptForName(mPresetName, [this](const std::string& n) { RenamePreset(n); }); break;
    case kDuplicate: DuplicatePreset(); break;
    case kDelete: DeletePreset(); break;
    case kRescan:
      Library().rescan();
      SetPresetStatus("Preset folder: " + Library().rootDir(), false);
      break;
    case kNewInit: LoadPresetIndex(Library().indexOfFactory("Init")); break;
    case kSaveDefault:
      try
      {
        Library().saveDefault(CurrentPresetData());
        SetPresetStatus("Saved: new IRONCLAD instances start with this sound.", false);
      }
      catch (const std::exception& ex)
      {
        SetPresetStatus(std::string("Could not save Default: ") + ex.what(), true);
      }
      break;
    case kResetDefault:
      try
      {
        Library().resetDefault();
        SetPresetStatus("Default reset to Init.", false);
      }
      catch (const std::exception& ex)
      {
        SetPresetStatus(std::string("Could not reset Default: ") + ex.what(), true);
      }
      break;
    default: break;
    }
  });
  mActionsMenu.AddItem(new IPopupMenu::Item("Save As...", IPopupMenu::Item::kNoFlags, kSaveAs));
  mActionsMenu.AddItem(new IPopupMenu::Item("Rename...", isUser ? IPopupMenu::Item::kNoFlags : IPopupMenu::Item::kDisabled, kRename));
  mActionsMenu.AddItem(new IPopupMenu::Item("Duplicate", IPopupMenu::Item::kNoFlags, kDuplicate));
  mActionsMenu.AddItem(new IPopupMenu::Item("Delete...", isUser ? IPopupMenu::Item::kNoFlags : IPopupMenu::Item::kDisabled, kDelete));
  mActionsMenu.AddSeparator();
  mActionsMenu.AddItem(new IPopupMenu::Item("Init (reset all settings)", IPopupMenu::Item::kNoFlags, kNewInit));
  mActionsMenu.AddItem(new IPopupMenu::Item("Save current sound as Default (first open)", IPopupMenu::Item::kNoFlags, kSaveDefault));
  mActionsMenu.AddItem(new IPopupMenu::Item("Reset Default to Init", IPopupMenu::Item::kNoFlags, kResetDefault));
  mActionsMenu.AddItem(new IPopupMenu::Item("Rescan / show preset folder", IPopupMenu::Item::kNoFlags, kRescan));
  g->CreatePopupMenu(*pCaller, mActionsMenu, pCaller->GetRECT());
#endif
}

// ============================================================================ undo

ironclad::UndoSnapshot IRONCLAD::CurrentSnapshot() const
{
  ironclad::UndoSnapshot s;
  s.values = mCommitted;
  s.capture = mEngine.captureStatePath();
  s.ir[0] = mEngine.irStatePath(0);
  s.ir[1] = mEngine.irStatePath(1);
  return s;
}

void IRONCLAD::ApplyParameterValues(const double* values, bool informHost)
{
  ++mSuppressUndo;
  for (int i = 0; i < ironclad::kNumParams; ++i)
  {
    GetParam(i)->Set(values[i]);
    const double v = GetParam(i)->Value();
    mEngine.setParameter(i, v);
    mCommitted[static_cast<std::size_t>(i)] = v;
  }
  if (informHost)
    DirtyParametersFromUI(); // one grouped edit for the host
  SendCurrentParamValuesFromDelegate(); // editor controls
  --mSuppressUndo;
}

void IRONCLAD::ApplySnapshot(const ironclad::UndoSnapshot& s)
{
  ++mSuppressUndo;
  ApplyParameterValues(s.values.data(), true);
  if (s.capture != mEngine.captureStatePath())
    LoadCaptureFile(s.capture, false);
  for (int slot = 0; slot < 2; ++slot)
    if (s.ir[slot] != mEngine.irStatePath(slot))
      LoadIrFile(slot, s.ir[slot], false);
  --mSuppressUndo;
  mPresetModified = true;
  mPresetBarDirty = true;
}

void IRONCLAD::Undo()
{
  if (auto s = mUndo.undo(CurrentSnapshot()))
    ApplySnapshot(*s);
  mPresetBarDirty = true;
}

void IRONCLAD::Redo()
{
  if (auto s = mUndo.redo(CurrentSnapshot()))
    ApplySnapshot(*s);
  mPresetBarDirty = true;
}

void IRONCLAD::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (paramIdx < 0 || paramIdx >= ironclad::kNumParams)
    return;
  const double v = GetParam(paramIdx)->Value();
  auto& committed = mCommitted[static_cast<std::size_t>(paramIdx)];
  if (source == kUI && mSuppressUndo == 0)
  {
    // MIDI learn by touching a control.
    if (mLearnOnTouch)
    {
      const bool isBool = ironclad::paramSpec(paramIdx).unit == ironclad::ParamUnit::Boolean;
      mMidi.armLearn(paramIdx, mLearnToggle && isBool);
      mLearnOnTouch = false;
      SetPresetStatus("MIDI learn: now move a pedal / press a switch for \"" + TargetName(paramIdx) + "\".", false);
    }
    if (committed != v)
    {
      ironclad::UndoSnapshot before = CurrentSnapshot(); // mCommitted still holds the old value
      mUndo.record(before, paramIdx, NowSeconds());
      if (!mPresetModified)
        mPresetBarDirty = true;
      mPresetModified = true;
    }
  }
  committed = v;
  if (source == kUI)
    mPresetBarDirty = true; // undo button state
}

// ============================================================================ MIDI

void IRONCLAD::SetParamFromPlugin(int idx, double plainValue)
{
  // A change made by the plugin itself (MIDI, tap tempo): host and editor follow; it is not
  // an undo step and does not mark the preset as modified.
  ++mSuppressUndo;
  const double norm = GetParam(idx)->ToNormalized(plainValue);
  BeginInformHostOfParamChangeFromUI(idx);
  SendParameterValueFromUI(idx, norm);
  EndInformHostOfParamChangeFromUI(idx);
  SendParameterValueFromDelegate(idx, norm, true);
  --mSuppressUndo;
}

void IRONCLAD::DrainMidiEvents()
{
  using Kind = ironclad::MidiEvent::Kind;
  std::array<double, ironclad::kNumParams> latest{};
  std::bitset<ironclad::kNumParams> changed;
  ironclad::MidiEvent e;
  int guard = 0;
  while (guard++ < 4096 && mMidiEvents.pop(e))
  {
    switch (e.kind)
    {
    case Kind::SetParam:
    case Kind::ToggleParam:
      if (e.target >= 0 && e.target < ironclad::kNumParams)
      {
        latest[static_cast<std::size_t>(e.target)] = e.value; // only the newest value matters
        changed.set(static_cast<std::size_t>(e.target));
      }
      break;
    case Kind::Action:
      if (e.target == ironclad::kMidiActionTapTempo)
        TapDelayTempo(e.timeSeconds);
      else if (e.target == ironclad::kMidiActionPresetNext)
        StepPreset(+1);
      else if (e.target == ironclad::kMidiActionPresetPrev)
        StepPreset(-1);
      break;
    case Kind::Learned:
      SetPresetStatus("MIDI CC " + std::to_string(e.cc) + " now controls \"" + TargetName(e.target) + "\".", false);
      break;
    }
  }
  for (int i = 0; i < ironclad::kNumParams; ++i)
    if (changed.test(static_cast<std::size_t>(i)))
    {
      SetParamFromPlugin(i, latest[static_cast<std::size_t>(i)]);
      mCommitted[static_cast<std::size_t>(i)] = GetParam(i)->Value();
    }
  if (mPresetBarDirty)
    RefreshPresetBar();
}

void IRONCLAD::SaveDefaultMidiMap()
{
  try
  {
    const std::string dir = ironclad::userDataDir();
    if (dir.empty())
      throw std::runtime_error("no user folder");
    ironclad::writeTextFileAtomic(ironclad::utf8FromPath(ironclad::pathFromUtf8(dir) / ".midi-default.json"),
                                  ironclad::midiMapToJson(mMidi.data()));
    SetPresetStatus("Saved: new IRONCLAD instances start with these MIDI assignments.", false);
  }
  catch (const std::exception& ex)
  {
    SetPresetStatus(std::string("Could not save the MIDI defaults: ") + ex.what(), true);
  }
}

void IRONCLAD::ShowMidiMenu(IControl* pCaller)
{
#if IPLUG_EDITOR
  IGraphics* g = GetUI();
  if (!g || !pCaller)
    return;
  enum
  {
    kLearn = 1,
    kLearnToggle,
    kLearnTap,
    kLearnNext,
    kLearnPrev,
    kCancel,
    kClearAll,
    kSaveDefault,
    kClearCcBase = 1000
  };
  mMidiPopup.Clear();
  mMidiPopup.SetFunction([this](IPopupMenu* pMenu) {
    IPopupMenu::Item* item = pMenu ? pMenu->GetChosenItem() : nullptr;
    if (!item)
      return;
    const int tag = item->GetTag();
    auto armAction = [this](int action) {
      mLearnOnTouch = false;
      mMidi.armLearn(action, false);
      SetPresetStatus("MIDI learn: now press the switch for \"" + TargetName(action) + "\".", false);
    };
    switch (tag)
    {
    case kLearn:
    case kLearnToggle:
      mMidi.cancelLearn();
      mLearnOnTouch = true;
      mLearnToggle = tag == kLearnToggle;
      SetPresetStatus("MIDI learn: move the control you want to assign (knob or switch)...", false);
      break;
    case kLearnTap: armAction(ironclad::kMidiActionTapTempo); break;
    case kLearnNext: armAction(ironclad::kMidiActionPresetNext); break;
    case kLearnPrev: armAction(ironclad::kMidiActionPresetPrev); break;
    case kCancel:
      mLearnOnTouch = false;
      mMidi.cancelLearn();
      SetPresetStatus("MIDI learn cancelled.", false);
      break;
    case kClearAll:
      mMidi.clearAll();
      SetPresetStatus("All MIDI assignments cleared.", false);
      break;
    case kSaveDefault: SaveDefaultMidiMap(); break;
    default:
      if (tag >= kClearCcBase && tag < kClearCcBase + 128)
      {
        mMidi.clearCc(tag - kClearCcBase);
        SetPresetStatus("MIDI CC " + std::to_string(tag - kClearCcBase) + " cleared.", false);
      }
      break;
    }
    mPresetBarDirty = true;
  });
  using Item = IPopupMenu::Item;
  mMidiPopup.AddItem(new Item("Learn: move a control, then a MIDI pedal / knob", Item::kNoFlags, kLearn));
  mMidiPopup.AddItem(new Item("Learn on/off switch (momentary footswitch toggles)", Item::kNoFlags, kLearnToggle));
  mMidiPopup.AddItem(new Item("Learn: Tap Tempo", Item::kNoFlags, kLearnTap));
  mMidiPopup.AddItem(new Item("Learn: Next Preset", Item::kNoFlags, kLearnNext));
  mMidiPopup.AddItem(new Item("Learn: Previous Preset", Item::kNoFlags, kLearnPrev));
  if (mLearnOnTouch || mMidi.learnTarget() != ironclad::kMidiNoTarget)
    mMidiPopup.AddItem(new Item("Cancel learn", Item::kNoFlags, kCancel));
  mMidiPopup.AddSeparator();
  const ironclad::MidiMapData data = mMidi.data();
  if (data.cc.empty())
    mMidiPopup.AddItem(new Item("(no MIDI assignments)", Item::kDisabled, -1));
  else
  {
    auto* list = new IPopupMenu();
    mMidiPopup.AddItem("Assignments (click to remove)", list);
    for (const auto& [cc, a] : data.cc)
    {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "CC %d  ->  %s%s", cc, TargetName(a.target).c_str(), a.toggle ? "  (toggle)" : "");
      list->AddItem(new Item(buf, Item::kNoFlags, kClearCcBase + cc));
    }
    mMidiPopup.AddItem(new Item("Clear all assignments", Item::kNoFlags, kClearAll));
  }
  mMidiPopup.AddItem(new Item("Save as default for new instances", Item::kNoFlags, kSaveDefault));
  g->CreatePopupMenu(*pCaller, mMidiPopup, pCaller->GetRECT());
#endif
}
