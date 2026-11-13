#include "Presets/FileResolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "Presets/Sha256.h"
#include "Utilities/Paths.h"

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

bool isFile(const fs::path& p)
{
  std::error_code ec;
  return !p.empty() && fs::is_regular_file(p, ec);
}

bool contentMatches(const FileRef& ref, const fs::path& p)
{
  if (ref.sha256.empty())
    return true;
  std::error_code ec;
  if (ref.size != 0 && fs::file_size(p, ec) != ref.size)
    return false;
  return Sha256::ofFile(utf8FromPath(p)) == ref.sha256;
}
} // namespace

FileRef makeFileRef(const std::string& utf8Path, const std::string& presetDir)
{
  FileRef r;
  r.path = utf8Path;
  const fs::path p = pathFromUtf8(utf8Path);
  r.fileName = utf8FromPath(p.filename());
  r.sha256 = Sha256::ofFile(utf8Path, &r.size);
  if (!presetDir.empty())
  {
    std::error_code ec;
    const fs::path rel = fs::relative(p, pathFromUtf8(presetDir), ec);
    if (!ec && !rel.empty())
    {
      r.relative = utf8FromPath(rel);
      std::replace(r.relative.begin(), r.relative.end(), '\\', '/'); // portable separator
    }
  }
  return r;
}

ResolvedFile resolveFile(const FileRef& ref, const ResolveOptions& opt)
{
  try
  {
    if (ref.empty())
      return {};
    // 1. The path it was saved with.
    if (isFile(pathFromUtf8(ref.path)))
      return {ref.path, ResolvedFile::How::SavedPath};
    // 2. Relative to the preset (presets shared together with their files).
    if (!ref.relative.empty() && !opt.presetDir.empty())
    {
      const fs::path p = (pathFromUtf8(opt.presetDir) / pathFromUtf8(ref.relative)).lexically_normal();
      if (isFile(p) && contentMatches(ref, p))
        return {utf8FromPath(p), ResolvedFile::How::RelativeToPreset};
    }
    // 3. Search the library folders: same name first, else same content.
    const std::string wantName = lower(ref.fileName);
    std::vector<fs::path> sameSize;
    int scanned = 0;
    for (const auto& root : opt.searchRoots)
    {
      std::error_code ec;
      const fs::path rootPath = pathFromUtf8(root);
      if (root.empty() || !fs::is_directory(rootPath, ec))
        continue;
      fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
      for (; !ec && it != end; it.increment(ec))
      {
        if (++scanned > opt.maxFilesScanned)
          break;
        if (it.depth() > 8)
        {
          it.disable_recursion_pending();
          continue;
        }
        std::error_code ec2;
        if (!it->is_regular_file(ec2))
          continue;
        const fs::path& p = it->path();
        const std::string ext = lower(utf8FromPath(p.extension()));
        if (!opt.extensions.empty() && std::find(opt.extensions.begin(), opt.extensions.end(), ext) == opt.extensions.end())
          continue;
        if (!wantName.empty() && lower(utf8FromPath(p.filename())) == wantName && contentMatches(ref, p))
          return {utf8FromPath(p), ResolvedFile::How::SameName};
        if (!ref.sha256.empty() && ref.size != 0 && it->file_size(ec2) == ref.size)
          sameSize.push_back(p);
      }
    }
    for (const auto& p : sameSize)
      if (Sha256::ofFile(utf8FromPath(p)) == ref.sha256)
        return {utf8FromPath(p), ResolvedFile::How::SameContent};
  }
  catch (const std::exception&)
  {
  }
  return {};
}

} // namespace ironclad
