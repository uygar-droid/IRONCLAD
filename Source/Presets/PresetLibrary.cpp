#include "Presets/PresetLibrary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "Presets/FactoryPresets.h"
#include "Presets/FileResolver.h"
#include "Utilities/Paths.h"
#include "json.hpp"

namespace ironclad
{
namespace fs = std::filesystem;
namespace
{
std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
} // namespace

void writeTextFileAtomic(const std::string& utf8Path, const std::string& text)
{
  const fs::path target = pathFromUtf8(utf8Path);
  std::error_code ec;
  if (target.has_parent_path())
    fs::create_directories(target.parent_path(), ec);
  fs::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error("Could not write \"" + utf8FromPath(target.filename()) + "\" (folder not writable?).");
    out << text;
    out.flush();
    if (!out)
      throw std::runtime_error("Could not write \"" + utf8FromPath(target.filename()) + "\" (disk full?).");
  }
  fs::rename(tmp, target, ec); // replaces an existing file on both platforms
  if (ec)
  {
    fs::remove(tmp, ec);
    throw std::runtime_error("Could not save \"" + utf8FromPath(target.filename()) + "\".");
  }
}

std::string readTextFile(const std::string& utf8Path)
{
  std::ifstream in(pathFromUtf8(utf8Path), std::ios::binary);
  if (!in)
    throw std::runtime_error("The file could not be opened.");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

PresetLibrary::PresetLibrary(std::string rootDir)
: mRoot(std::move(rootDir))
{
  rescan();
}

std::string PresetLibrary::sanitizeName(const std::string& name)
{
  std::string out;
  for (char c : name)
  {
    const auto u = static_cast<unsigned char>(c);
    if (u < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
      out.push_back('_');
    else
      out.push_back(c);
  }
  // Trim spaces and dots at the ends (Windows rejects trailing dots/spaces).
  const auto first = out.find_first_not_of(" .");
  const auto last = out.find_last_not_of(" .");
  out = first == std::string::npos ? std::string{} : out.substr(first, last - first + 1);
  if (out.size() > 100)
    out.resize(100);
  static const char* kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3"};
  for (const char* r : kReserved)
    if (lower(out) == r)
      out += "_";
  return out;
}

std::string PresetLibrary::userDir() const { return utf8FromPath(pathFromUtf8(mRoot) / kUserFolder); }

std::string PresetLibrary::defaultPresetPath() const { return utf8FromPath(pathFromUtf8(mRoot) / kDefaultFile); }

std::string PresetLibrary::pathFor(const std::string& category, const std::string& name) const
{
  fs::path p = pathFromUtf8(userDir());
  const std::string cat = sanitizeName(category);
  if (!cat.empty())
    p /= pathFromUtf8(cat);
  p /= pathFromUtf8(sanitizeName(name) + kExtension);
  return utf8FromPath(p);
}

namespace
{
std::string relativeKey(const std::string& path, const std::string& root)
{
  std::error_code ec;
  const fs::path rel = fs::relative(pathFromUtf8(path), pathFromUtf8(root), ec);
  std::string key = ec || rel.empty() ? path : utf8FromPath(rel);
  std::replace(key.begin(), key.end(), '\\', '/');
  return key;
}

std::string factoryFilePath(const std::string& root, const PresetData& p)
{
  fs::path f = pathFromUtf8(root) / PresetLibrary::kFactoryFolder;
  if (!p.category.empty())
    f /= pathFromUtf8(PresetLibrary::sanitizeName(p.category));
  f /= pathFromUtf8(PresetLibrary::sanitizeName(p.name) + PresetLibrary::kExtension);
  return utf8FromPath(f);
}
} // namespace

namespace
{
/// Moves everything in `from` into `to`, merging folders and never replacing existing files.
void moveChildren(const fs::path& from, const fs::path& to, const fs::path& skipName = {})
{
  std::error_code ec;
  if (!fs::is_directory(from, ec))
    return;
  fs::create_directories(to, ec);
  std::vector<fs::path> items;
  for (const auto& item : fs::directory_iterator(from, ec))
    items.push_back(item.path());
  for (const auto& item : items)
  {
    if (!skipName.empty() && item.filename() == skipName)
      continue;
    const fs::path target = to / item.filename();
    std::error_code e2;
    if (fs::exists(target, e2))
    {
      if (fs::is_directory(item, e2) && fs::is_directory(target, e2))
        moveChildren(item, target);
      continue; // never overwrite
    }
    fs::rename(item, target, e2);
    if (e2) // another volume: copy, then remove the original
    {
      std::error_code e3;
      fs::copy(item, target, fs::copy_options::recursive, e3);
      if (!e3)
        fs::remove_all(item, e3);
    }
  }
  fs::remove(from, ec); // only succeeds if it is empty now
}

std::vector<std::string> readFavoriteList(const fs::path& file)
{
  std::vector<std::string> out;
  try
  {
    const auto j = nlohmann::json::parse(readTextFile(utf8FromPath(file)));
    if (j.is_object())
      for (const auto& f : j.value("favorites", nlohmann::json::array()))
        if (f.is_string())
          out.push_back(f.get<std::string>());
  }
  catch (const std::exception&)
  {
  }
  return out;
}
} // namespace

void PresetLibrary::importFrom(const std::string& oldRoot)
{
  if (oldRoot.empty() || mRoot.empty())
    return;
  std::error_code ec;
  const fs::path from = pathFromUtf8(oldRoot);
  const fs::path to = pathFromUtf8(mRoot);
  if (!fs::is_directory(from, ec))
    return;
  const bool sameRoot = fs::equivalent(from, to, ec);

  loadFavorites();
  auto addFavorite = [this](const std::string& key) {
    if (std::find(mFavorites.begin(), mFavorites.end(), key) == mFavorites.end())
      mFavorites.push_back(key);
  };
  bool favoritesChanged = false;

  // 0.5.0: <root>/Presets/<Category>/x.ironclad with "user:<rel>" favourites.
  for (const auto& key : readFavoriteList(from / "Presets" / "favorites.json"))
    if (key.rfind("user:", 0) == 0)
    {
      addFavorite(std::string(kUserFolder) + "/" + key.substr(5));
      favoritesChanged = true;
    }
  fs::remove(from / "Presets" / "favorites.json", ec);
  moveChildren(from / "Presets", to / kUserFolder);

  if (!sameRoot)
  {
    // 0.5.1 layout in another folder (the per-user one): same relative favourite keys.
    for (const auto& key : readFavoriteList(from / ".favorites.json"))
    {
      addFavorite(key);
      favoritesChanged = true;
    }
    fs::remove(from / ".favorites.json", ec);
    moveChildren(from / kUserFolder, to / kUserFolder);
    moveChildren(from / kPacksFolder, to / kPacksFolder);
    const std::pair<const char*, const char*> files[] = {
      {kDefaultFile, kDefaultFile}, {".midi-default.json", ".midi-default.json"}, {"midi-default.json", ".midi-default.json"}};
    for (const auto& [src, dst] : files)
    {
      std::error_code e2;
      if (!fs::exists(from / src, e2) || fs::exists(to / dst, e2))
        continue;
      fs::rename(from / src, to / dst, e2);
      if (e2 && fs::copy_file(from / src, to / dst, e2))
        fs::remove(from / src, e2);
    }
    // The factory folder is rewritten from the built-in set; the old copy is not needed.
    fs::remove_all(from / kFactoryFolder, ec);
    fs::remove(from, ec); // only if nothing else is left
  }
  if (favoritesChanged)
  {
    try
    {
      saveFavorites();
    }
    catch (const std::exception&)
    {
    }
  }
}

std::string PresetLibrary::favoriteKey(const PresetEntry& e) const
{
  if (e.path.empty())
    return "builtin:" + e.category + "/" + e.name;
  return relativeKey(e.path, mRoot);
}

void PresetLibrary::loadFavorites()
{
  mFavorites.clear();
  try
  {
    const auto j = nlohmann::json::parse(readTextFile(utf8FromPath(pathFromUtf8(mRoot) / ".favorites.json")));
    if (j.is_object() && j.contains("favorites") && j["favorites"].is_array())
      for (const auto& f : j["favorites"])
        if (f.is_string())
          mFavorites.push_back(f.get<std::string>());
  }
  catch (const std::exception&)
  {
  }
}

void PresetLibrary::saveFavorites() const
{
  nlohmann::json j;
  j["favorites"] = mFavorites;
  writeTextFileAtomic(utf8FromPath(pathFromUtf8(mRoot) / ".favorites.json"), j.dump(2));
}

bool PresetLibrary::ensureLayout()
{
  if (mRoot.empty())
    return false;
  bool ok = true;
  std::error_code ec;
  const fs::path root = pathFromUtf8(mRoot);
  for (const char* sub : {kFactoryFolder, kPacksFolder, kUserFolder})
  {
    fs::create_directories(root / sub, ec);
    ok = ok && !ec;
  }

  // Factory presets: (re)written from the built-in set when missing or different, so an
  // update of IRONCLAD updates them. They are read-only in the browser.
  for (const auto& p : factoryPresets())
  {
    const std::string path = factoryFilePath(mRoot, p);
    const std::string text = presetToJson(p);
    try
    {
      std::string existing;
      try
      {
        existing = readTextFile(path);
      }
      catch (const std::exception&)
      {
      }
      if (existing != text)
        writeTextFileAtomic(path, text);
    }
    catch (const std::exception&)
    {
      ok = false;
    }
  }

  // Presets saved by 0.5.0 lived in <root>/Presets: move them into User.
  importFrom(mRoot);

  if (!fs::exists(pathFromUtf8(defaultPresetPath()), ec))
  {
    try
    {
      resetDefault();
    }
    catch (const std::exception&)
    {
      ok = false;
    }
  }
  rescan();
  return ok;
}

void PresetLibrary::scanFolder(const std::string& dir, PresetEntry::Source source, int maxDepth,
                               std::vector<PresetEntry>& out) const
{
  std::error_code ec;
  const fs::path base = pathFromUtf8(dir);
  if (dir.empty() || !fs::is_directory(base, ec))
    return;
  fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
  int guard = 0;
  for (; !ec && it != end && guard < 20000; it.increment(ec), ++guard)
  {
    if (it.depth() >= maxDepth)
      it.disable_recursion_pending();
    std::error_code ec2;
    if (!it->is_regular_file(ec2) || lower(utf8FromPath(it->path().extension())) != kExtension)
      continue;
    PresetEntry e;
    e.source = source;
    e.path = utf8FromPath(it->path());
    e.name = utf8FromPath(it->path().stem());
    const fs::path rel = it->path().parent_path().lexically_relative(base);
    for (const auto& part : rel)
    {
      const std::string s = utf8FromPath(part);
      if (s.empty() || s == ".")
        continue;
      e.category += (e.category.empty() ? "" : " / ") + s;
    }
    out.push_back(std::move(e));
  }
}

void PresetLibrary::rescan()
{
  loadFavorites();
  mEntries.clear();
  const fs::path root = pathFromUtf8(mRoot);
  const auto& builtin = factoryPresets();

  std::vector<PresetEntry> factory, packs, user;
  if (!mRoot.empty())
  {
    scanFolder(utf8FromPath(root / kFactoryFolder), PresetEntry::Source::Factory, 1, factory);
    scanFolder(utf8FromPath(root / kPacksFolder), PresetEntry::Source::Pack, 2, packs);
    scanFolder(userDir(), PresetEntry::Source::User, 1, user);
  }
  if (factory.empty()) // folder not available: use the built-in set directly
    for (int i = 0; i < static_cast<int>(builtin.size()); ++i)
    {
      PresetEntry e;
      e.name = builtin[static_cast<std::size_t>(i)].name;
      e.category = builtin[static_cast<std::size_t>(i)].category;
      e.source = PresetEntry::Source::Factory;
      e.builtinIndex = i;
      factory.push_back(e);
    }

  // Factory presets in their designed order (categories as listed in FactoryPresets.cpp),
  // anything else alphabetically by category then name.
  auto builtinRank = [&builtin](const PresetEntry& e) {
    for (std::size_t i = 0; i < builtin.size(); ++i)
      if (builtin[i].name == e.name && builtin[i].category == e.category)
        return static_cast<int>(i);
    return static_cast<int>(builtin.size());
  };
  auto alpha = [](const PresetEntry& a, const PresetEntry& b) {
    const auto ca = lower(a.category), cb = lower(b.category);
    return ca != cb ? ca < cb : lower(a.name) < lower(b.name);
  };
  std::stable_sort(factory.begin(), factory.end(), [&](const PresetEntry& a, const PresetEntry& b) {
    const int ra = builtinRank(a), rb = builtinRank(b);
    return ra != rb ? ra < rb : alpha(a, b);
  });
  std::sort(packs.begin(), packs.end(), alpha);
  std::sort(user.begin(), user.end(), alpha);
  for (auto* group : {&factory, &packs, &user})
    mEntries.insert(mEntries.end(), group->begin(), group->end());
  for (auto& e : mEntries)
    e.favorite = std::find(mFavorites.begin(), mFavorites.end(), favoriteKey(e)) != mFavorites.end();
}

int PresetLibrary::indexOfPath(const std::string& path) const
{
  if (path.empty())
    return -1;
  std::error_code ec;
  for (int i = 0; i < static_cast<int>(mEntries.size()); ++i)
  {
    const auto& e = mEntries[static_cast<std::size_t>(i)];
    if (!e.path.empty() && (e.path == path || fs::equivalent(pathFromUtf8(e.path), pathFromUtf8(path), ec)))
      return i;
  }
  return -1;
}

int PresetLibrary::indexOfFactory(const std::string& name) const
{
  for (int i = 0; i < static_cast<int>(mEntries.size()); ++i)
    if (mEntries[static_cast<std::size_t>(i)].isFactory() && mEntries[static_cast<std::size_t>(i)].name == name)
      return i;
  return -1;
}

PresetData PresetLibrary::loadFile(const std::string& path)
{
  std::string text;
  try
  {
    text = readTextFile(path);
  }
  catch (const std::exception&)
  {
    throw std::runtime_error("The preset file could not be opened.");
  }
  PresetData p = presetFromJson(text);
  if (p.name.empty())
    p.name = utf8FromPath(pathFromUtf8(path).stem());
  return p;
}

PresetData PresetLibrary::load(int index) const
{
  if (index < 0 || index >= static_cast<int>(mEntries.size()))
    throw std::runtime_error("No such preset.");
  const auto& e = mEntries[static_cast<std::size_t>(index)];
  if (e.builtinIndex >= 0)
    return factoryPresets()[static_cast<std::size_t>(e.builtinIndex)];
  PresetData p = loadFile(e.path);
  p.name = e.name; // the file name is the preset's name in the browser
  p.category = e.category;
  return p;
}

std::string PresetLibrary::save(PresetData p, bool overwrite)
{
  p.name = sanitizeName(p.name);
  if (p.name.empty())
    throw std::runtime_error("Please enter a preset name.");
  const std::string path = pathFor(p.category, p.name);
  std::error_code ec;
  if (!overwrite && fs::exists(pathFromUtf8(path), ec))
    throw std::runtime_error("A preset named \"" + p.name + "\" already exists.");
  const std::string dir = utf8FromPath(pathFromUtf8(path).parent_path());
  auto refresh = [&](std::optional<FileRef>& r) {
    if (r && !r->path.empty())
    {
      FileRef fresh = makeFileRef(r->path, dir);
      if (fresh.sha256.empty()) // file currently missing: keep what we knew about it
        fresh = *r;
      r = fresh;
    }
  };
  refresh(p.capture);
  refresh(p.ir[0]);
  refresh(p.ir[1]);
  writeTextFileAtomic(path, presetToJson(p));
  rescan();
  return path;
}

std::string PresetLibrary::rename(int index, const std::string& newName)
{
  if (index < 0 || index >= static_cast<int>(mEntries.size()))
    throw std::runtime_error("No such preset.");
  const PresetEntry e = mEntries[static_cast<std::size_t>(index)];
  if (e.readOnly())
    throw std::runtime_error("Factory and pack presets cannot be renamed.");
  const std::string name = sanitizeName(newName);
  if (name.empty())
    throw std::runtime_error("Please enter a preset name.");
  const std::string target = utf8FromPath(pathFromUtf8(e.path).parent_path() / pathFromUtf8(name + kExtension));
  std::error_code ec;
  if (fs::exists(pathFromUtf8(target), ec) && !fs::equivalent(pathFromUtf8(target), pathFromUtf8(e.path), ec))
    throw std::runtime_error("A preset named \"" + name + "\" already exists.");
  PresetData p = loadFile(e.path);
  p.name = name;
  writeTextFileAtomic(target, presetToJson(p));
  if (target != e.path)
    fs::remove(pathFromUtf8(e.path), ec);
  const bool wasFavorite = e.favorite;
  const std::string oldKey = favoriteKey(e);
  mFavorites.erase(std::remove(mFavorites.begin(), mFavorites.end(), oldKey), mFavorites.end());
  rescan();
  if (wasFavorite)
    setFavorite(indexOfPath(target), true);
  return target;
}

std::string PresetLibrary::duplicate(int index)
{
  PresetData p = load(index);
  if (mEntries[static_cast<std::size_t>(index)].source == PresetEntry::Source::Pack)
    p.category = mEntries[static_cast<std::size_t>(index)].category.substr(
      0, mEntries[static_cast<std::size_t>(index)].category.find(" / ")); // pack name
  const std::string base = p.name;
  for (int n = 2; n < 1000; ++n)
  {
    p.name = base + " " + std::to_string(n);
    std::error_code ec;
    if (!fs::exists(pathFromUtf8(pathFor(p.category, p.name)), ec))
      return save(p, false);
  }
  throw std::runtime_error("Could not find a free name for the copy.");
}

void PresetLibrary::remove(int index)
{
  if (index < 0 || index >= static_cast<int>(mEntries.size()))
    throw std::runtime_error("No such preset.");
  const PresetEntry e = mEntries[static_cast<std::size_t>(index)];
  if (e.readOnly())
    throw std::runtime_error("Factory and pack presets cannot be deleted.");
  std::error_code ec;
  if (!fs::remove(pathFromUtf8(e.path), ec) || ec)
    throw std::runtime_error("The preset could not be deleted.");
  mFavorites.erase(std::remove(mFavorites.begin(), mFavorites.end(), favoriteKey(e)), mFavorites.end());
  try
  {
    saveFavorites();
  }
  catch (const std::exception&)
  {
  }
  rescan();
}

void PresetLibrary::setFavorite(int index, bool favorite)
{
  if (index < 0 || index >= static_cast<int>(mEntries.size()))
    return;
  auto& e = mEntries[static_cast<std::size_t>(index)];
  const std::string key = favoriteKey(e);
  mFavorites.erase(std::remove(mFavorites.begin(), mFavorites.end(), key), mFavorites.end());
  if (favorite)
    mFavorites.push_back(key);
  e.favorite = favorite;
  saveFavorites();
}

PresetData PresetLibrary::loadDefault() const
{
  try
  {
    PresetData p = loadFile(defaultPresetPath());
    p.name = "Default";
    p.category.clear();
    return p;
  }
  catch (const std::exception&)
  {
    PresetData p = factoryPresets().front(); // "Init"
    p.name = "Default";
    p.category.clear();
    return p;
  }
}

void PresetLibrary::saveDefault(PresetData p)
{
  p.name = "Default";
  p.category.clear();
  const std::string dir = mRoot;
  auto refresh = [&](std::optional<FileRef>& r) {
    if (r && !r->path.empty())
    {
      FileRef fresh = makeFileRef(r->path, dir);
      if (fresh.sha256.empty())
        fresh = *r;
      r = fresh;
    }
  };
  refresh(p.capture);
  refresh(p.ir[0]);
  refresh(p.ir[1]);
  writeTextFileAtomic(defaultPresetPath(), presetToJson(p));
}

void PresetLibrary::resetDefault()
{
  PresetData p = factoryPresets().front(); // "Init": all effects off, unity gain
  p.name = "Default";
  p.category.clear();
  writeTextFileAtomic(defaultPresetPath(), presetToJson(p));
}

} // namespace ironclad
