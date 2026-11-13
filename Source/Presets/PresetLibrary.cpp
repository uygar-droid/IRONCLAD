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

std::string PresetLibrary::pathFor(const std::string& category, const std::string& name) const
{
  fs::path p = pathFromUtf8(mRoot);
  const std::string cat = sanitizeName(category);
  if (!cat.empty())
    p /= pathFromUtf8(cat);
  p /= pathFromUtf8(sanitizeName(name) + kExtension);
  return utf8FromPath(p);
}

std::string PresetLibrary::favoriteKey(const PresetEntry& e) const
{
  if (e.isFactory())
    return "factory:" + e.category + "/" + e.name;
  std::error_code ec;
  const fs::path rel = fs::relative(pathFromUtf8(e.path), pathFromUtf8(mRoot), ec);
  std::string key = ec ? e.path : utf8FromPath(rel);
  std::replace(key.begin(), key.end(), '\\', '/');
  return "user:" + key;
}

void PresetLibrary::loadFavorites()
{
  mFavorites.clear();
  try
  {
    const auto j = nlohmann::json::parse(readTextFile(utf8FromPath(pathFromUtf8(mRoot) / "favorites.json")));
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
  writeTextFileAtomic(utf8FromPath(pathFromUtf8(mRoot) / "favorites.json"), j.dump(2));
}

void PresetLibrary::rescan()
{
  loadFavorites();
  mEntries.clear();
  const auto& factory = factoryPresets();
  for (int i = 0; i < static_cast<int>(factory.size()); ++i)
    mEntries.push_back({factory[static_cast<std::size_t>(i)].name, factory[static_cast<std::size_t>(i)].category, {}, i});

  std::vector<PresetEntry> user;
  std::error_code ec;
  const fs::path root = pathFromUtf8(mRoot);
  if (!mRoot.empty() && fs::is_directory(root, ec))
  {
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec))
    {
      if (it.depth() >= 1)
        it.disable_recursion_pending(); // root and one level of category folders
      std::error_code ec2;
      if (!it->is_regular_file(ec2) || lower(utf8FromPath(it->path().extension())) != kExtension)
        continue;
      PresetEntry e;
      e.path = utf8FromPath(it->path());
      e.name = utf8FromPath(it->path().stem());
      if (it.depth() == 1)
        e.category = utf8FromPath(it->path().parent_path().filename());
      user.push_back(std::move(e));
    }
  }
  std::sort(user.begin(), user.end(), [](const PresetEntry& a, const PresetEntry& b) {
    const auto ca = lower(a.category), cb = lower(b.category);
    return ca != cb ? ca < cb : lower(a.name) < lower(b.name);
  });
  mEntries.insert(mEntries.end(), user.begin(), user.end());
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
    if (!e.isFactory() && (e.path == path || fs::equivalent(pathFromUtf8(e.path), pathFromUtf8(path), ec)))
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
  if (e.isFactory())
    return factoryPresets()[static_cast<std::size_t>(e.factoryIndex)];
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
  p.templateOnly = false;
  writeTextFileAtomic(path, presetToJson(p));
  rescan();
  return path;
}

std::string PresetLibrary::rename(int index, const std::string& newName)
{
  if (index < 0 || index >= static_cast<int>(mEntries.size()))
    throw std::runtime_error("No such preset.");
  const PresetEntry e = mEntries[static_cast<std::size_t>(index)];
  if (e.isFactory())
    throw std::runtime_error("Factory presets cannot be renamed.");
  const std::string name = sanitizeName(newName);
  if (name.empty())
    throw std::runtime_error("Please enter a preset name.");
  const std::string target = pathFor(e.category, name);
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
  if (e.isFactory())
    throw std::runtime_error("Factory presets cannot be deleted.");
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

} // namespace ironclad
