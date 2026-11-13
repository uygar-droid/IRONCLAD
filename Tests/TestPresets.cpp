// Milestone 5 tests: SHA-256, preset JSON, DAW state formats (M1/v1/v2/v3), file
// resolution, preset library, factory presets, undo/redo and the MIDI map.

#include "doctest/doctest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "AllocationTracking.h"
#include "Midi/MidiMap.h"
#include "Presets/FactoryPresets.h"
#include "Presets/FileResolver.h"
#include "Presets/PresetData.h"
#include "Presets/PresetLibrary.h"
#include "Presets/Sha256.h"
#include "Presets/StateCodec.h"
#include "Presets/UndoStack.h"
#include "Utilities/Paths.h"

using namespace ironclad;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
  fs::path path;
  explicit TempDir(const char* name)
  {
    // A non-ASCII folder name checks the UTF-8 path handling as well.
    path = fs::temp_directory_path() / pathFromUtf8(std::string(name) + "_M\xC3\xBCzik");
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  std::string str(const std::string& rel = {}) const { return utf8FromPath(rel.empty() ? path : path / pathFromUtf8(rel)); }
};

void writeFile(const std::string& utf8Path, const std::string& content)
{
  fs::create_directories(pathFromUtf8(utf8Path).parent_path());
  std::ofstream out(pathFromUtf8(utf8Path), std::ios::binary);
  out << content;
}

// Legacy (pre-v3) state writers, byte-for-byte what IByteChunk produced.
void putInt(std::vector<std::uint8_t>& v, std::int32_t x)
{
  const auto* b = reinterpret_cast<const std::uint8_t*>(&x);
  v.insert(v.end(), b, b + 4);
}
void putDouble(std::vector<std::uint8_t>& v, double x)
{
  const auto* b = reinterpret_cast<const std::uint8_t*>(&x);
  v.insert(v.end(), b, b + 8);
}
void putStr(std::vector<std::uint8_t>& v, const std::string& s)
{
  putInt(v, static_cast<std::int32_t>(s.size()));
  v.insert(v.end(), s.begin(), s.end());
}
} // namespace

TEST_CASE("SHA-256 matches the FIPS 180-4 test vectors")
{
  CHECK(Sha256::ofBytes("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256::ofBytes("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(Sha256::ofBytes(two, std::strlen(two)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  const std::string million(1000000, 'a');
  CHECK(Sha256::ofBytes(million.data(), million.size()) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  // Incremental updates in odd-sized pieces give the same digest, and so does the file API.
  Sha256 h;
  for (std::size_t pos = 0; pos < million.size(); pos += 977)
    h.update(million.data() + pos, std::min<std::size_t>(977, million.size() - pos));
  CHECK(h.hexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  TempDir dir("ironclad_sha");
  writeFile(dir.str("a.bin"), million);
  std::uint64_t size = 0;
  CHECK(Sha256::ofFile(dir.str("a.bin"), &size) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(size == 1000000);
  CHECK(Sha256::ofFile(dir.str("missing.bin")).empty());
}

TEST_CASE("Preset JSON round-trips and keys parameters by stable id")
{
  PresetData p;
  p.name = "Crunch \xC3\xA7\xC4\xB1\xC4\x9F"; // non-ASCII name
  p.category = "Rock";
  p.params["od.drive"] = 73.5;
  p.params["delay.type"] = 2.0;
  FileRef cap;
  cap.path = "/x/Plexi.nam";
  cap.fileName = "Plexi.nam";
  cap.sha256 = std::string(64, 'a');
  cap.size = 1234;
  cap.relative = "../Captures/Plexi.nam";
  p.capture = cap;

  const PresetData q = presetFromJson(presetToJson(p));
  CHECK(q.name == p.name);
  CHECK(q.category == "Rock");
  CHECK(q.params == p.params);
  REQUIRE(q.capture.has_value());
  CHECK(q.capture->path == cap.path);
  CHECK(q.capture->relative == cap.relative);
  CHECK(q.capture->sha256 == cap.sha256);
  CHECK(q.capture->size == 1234);
  CHECK_FALSE(q.ir[0].has_value());

  // Missing parameters take their default; out-of-range values are clamped.
  CHECK(q.valueFor(kParamOdDrive) == 73.5);
  CHECK(q.valueFor(kParamReverbMix) == paramSpec(kParamReverbMix).defaultValue);
  PresetData r;
  r.params["od.drive"] = 500.0;
  CHECK(r.valueFor(kParamOdDrive) == 100.0);

  // Unknown ids (from a newer build) are kept harmlessly and ignored by valueFor.
  const std::string withUnknown = R"({"format":"IRONCLAD preset","schema":1,"name":"X",
    "params":{"od.drive":10,"future.knob":3,"od.tone":"bad"},"amp":{"capture":null},"cab":{}})";
  const PresetData u = presetFromJson(withUnknown);
  CHECK(u.valueFor(kParamOdDrive) == 10.0);
  CHECK(u.params.count("od.tone") == 0); // non-numeric value skipped
}

TEST_CASE("Invalid or unsupported preset files are rejected with a message")
{
  CHECK_THROWS_WITH(presetFromJson("{ not json"), "The preset file is damaged (invalid JSON).");
  CHECK_THROWS_WITH(presetFromJson(R"({"format":"something else"})"), "This is not an IRONCLAD preset.");
  CHECK_THROWS_WITH(presetFromJson(R"({"format":"IRONCLAD preset","schema":99})"),
                    "This preset was made with a newer version of IRONCLAD.");
  CHECK_THROWS_WITH(presetFromJson(R"({"format":"IRONCLAD preset"})"),
                    "The preset file has no valid schema version.");
}

TEST_CASE("DAW state: every earlier format still loads")
{
  SUBCASE("Milestone 1: raw parameter values")
  {
    std::vector<std::uint8_t> b;
    putDouble(b, -3.0); // global.input_db
    putDouble(b, 2.5);  // global.output_db
    SessionState s;
    CHECK(decodeState(b.data(), static_cast<int>(b.size()), 0, s) == 16);
    CHECK(s.sourceVersion == 0);
    CHECK(s.preset.valueFor(kParamInputGain) == -3.0);
    CHECK(s.preset.valueFor(kParamOutputGain) == 2.5);
    CHECK(s.preset.valueFor(kParamAmpEnabled) == 1.0); // not in the state: default
    CHECK_FALSE(s.preset.capture.has_value());
  }
  SUBCASE("v1: six parameters and a capture path, after a host prefix")
  {
    std::vector<std::uint8_t> b = {0xAA, 0xBB, 0xCC}; // bytes before our chunk start
    putStr(b, "IRCL");
    putInt(b, 1);
    for (double v : {1.0, -6.0, 0.0, 4.0, -2.0, 0.0})
      putDouble(b, v);
    putStr(b, "/Users/me/Captures/Plexi.nam");
    SessionState s;
    CHECK(decodeState(b.data(), static_cast<int>(b.size()), 3, s) == static_cast<int>(b.size()));
    CHECK(s.sourceVersion == 1);
    CHECK(s.preset.valueFor(kParamOutputGain) == -6.0);
    CHECK(s.preset.valueFor(kParamAmpInputGain) == 4.0);
    CHECK(s.preset.valueFor(kParamAmpNormalize) == 0.0);
    REQUIRE(s.preset.capture.has_value());
    CHECK(s.preset.capture->path == "/Users/me/Captures/Plexi.nam");
    CHECK(s.preset.capture->fileName == "Plexi.nam");
  }
  SUBCASE("v2: counted parameters, capture and two IRs")
  {
    std::vector<std::uint8_t> b;
    putStr(b, "IRCL");
    putInt(b, 2);
    putInt(b, 72);
    for (int i = 0; i < 72; ++i)
      putDouble(b, i == kParamDelayMix ? 42.0 : paramSpec(i).defaultValue);
    putStr(b, "C:\\Caps\\Lead.nam");
    putStr(b, "");
    putStr(b, "/irs/V30.wav");
    SessionState s;
    CHECK(decodeState(b.data(), static_cast<int>(b.size()), 0, s) == static_cast<int>(b.size()));
    CHECK(s.sourceVersion == 2);
    CHECK(s.preset.valueFor(kParamDelayMix) == 42.0);
    CHECK(s.preset.valueFor(kParamVolume) == 100.0); // added after v2: default
    REQUIRE(s.preset.capture.has_value());
    CHECK(s.preset.capture->fileName == "Lead.nam");
    CHECK_FALSE(s.preset.ir[0].has_value());
    REQUIRE(s.preset.ir[1].has_value());
    CHECK(s.preset.ir[1]->path == "/irs/V30.wav");
  }
}

TEST_CASE("DAW state v3 round-trips, and bad data is refused")
{
  SessionState s;
  s.preset.name = "Song A";
  s.preset.category = "Live";
  for (int i = 0; i < kNumParams; ++i)
    s.preset.params[std::string(paramSpec(i).id)] = paramSpec(i).defaultValue;
  s.preset.params["wah.position"] = 12.5;
  FileRef ir;
  ir.path = "/irs/\xC3\x96zel.wav";
  ir.fileName = "\xC3\x96zel.wav";
  s.preset.ir[0] = ir;
  s.presetPath = "/presets/Live/Song A.ironclad";
  s.presetModified = true;
  s.midi.cc[11] = {kParamWahPosition, false};
  s.midi.cc[64] = {kParamDelayEnabled, true};
  s.midi.cc[80] = {kMidiActionTapTempo, false};

  std::vector<std::uint8_t> b = encodeState(s);
  b.insert(b.begin(), {1, 2, 3, 4}); // host prefix
  SessionState d;
  REQUIRE(decodeState(b.data(), static_cast<int>(b.size()), 4, d) == static_cast<int>(b.size()));
  CHECK(d.sourceVersion == 3);
  CHECK(d.preset.name == "Song A");
  CHECK(d.preset.params == s.preset.params);
  REQUIRE(d.preset.ir[0].has_value());
  CHECK(d.preset.ir[0]->path == ir.path);
  CHECK(d.presetPath == s.presetPath);
  CHECK(d.presetModified);
  CHECK(d.midi.cc.size() == 3);
  CHECK(d.midi.cc[64].toggle);
  CHECK(d.midi.cc[80].target == kMidiActionTapTempo);

  // Truncated, corrupt, or from a newer version: refused, never a crash.
  const std::vector<std::uint8_t> full = encodeState(s);
  for (std::size_t cut : {std::size_t(6), std::size_t(10), full.size() - 1})
    CHECK(decodeState(full.data(), static_cast<int>(cut), 0, d) == -1);
  std::vector<std::uint8_t> newer;
  putStr(newer, "IRCL");
  putInt(newer, 4);
  putStr(newer, "{}");
  CHECK(decodeState(newer.data(), static_cast<int>(newer.size()), 0, d) == -1);
  std::vector<std::uint8_t> badJson;
  putStr(badJson, "IRCL");
  putInt(badJson, 3);
  putStr(badJson, "{oops");
  CHECK(decodeState(badJson.data(), static_cast<int>(badJson.size()), 0, d) == -1);
  CHECK(decodeState(nullptr, 0, 0, d) == -1);
}

TEST_CASE("Moved, renamed and missing files are found or reported, never fatal")
{
  TempDir dir("ironclad_resolve");
  const std::string content = "fake capture content 0123456789";
  writeFile(dir.str("Old/Lead.nam"), content);
  const FileRef ref = makeFileRef(dir.str("Old/Lead.nam"), dir.str("Presets"));
  CHECK(ref.fileName == "Lead.nam");
  CHECK(ref.sha256 == Sha256::ofBytes(content.data(), content.size()));
  CHECK(ref.size == content.size());
  CHECK(ref.relative == "../Old/Lead.nam");

  ResolveOptions opt;
  opt.presetDir = dir.str("Presets");
  opt.searchRoots = {dir.str("Library")};
  opt.extensions = {".nam"};

  CHECK(resolveFile(ref, opt).how == ResolvedFile::How::SavedPath);

  // Preset folder and its files moved together: found relative to the preset.
  FileRef shared = ref;
  shared.path = "/nonexistent/Lead.nam";
  CHECK(resolveFile(shared, opt).how == ResolvedFile::How::RelativeToPreset);

  // Moved into the library: found by name (and verified by hash).
  fs::create_directories(dir.path / "Library" / "Amps");
  fs::rename(dir.path / "Old" / "Lead.nam", dir.path / "Library" / "Amps" / "Lead.nam");
  auto r = resolveFile(ref, opt);
  CHECK(r.how == ResolvedFile::How::SameName);
  CHECK(fs::equivalent(pathFromUtf8(r.path), dir.path / "Library" / "Amps" / "Lead.nam"));

  // Renamed as well: found by content.
  fs::rename(dir.path / "Library" / "Amps" / "Lead.nam", dir.path / "Library" / "Amps" / "Lead v2.NAM");
  writeFile(dir.str("Library/Decoy.nam"), "different content, same name test");
  r = resolveFile(ref, opt);
  CHECK(r.how == ResolvedFile::How::SameContent);
  CHECK(fs::equivalent(pathFromUtf8(r.path), dir.path / "Library" / "Amps" / "Lead v2.NAM"));

  // A same-named file with different content is not mistaken for it.
  writeFile(dir.str("Library/Lead.nam"), "another capture entirely");
  fs::remove(dir.path / "Library" / "Amps" / "Lead v2.NAM");
  CHECK(resolveFile(ref, opt).how == ResolvedFile::How::NotFound);

  // Missing everywhere, empty refs and unreadable search roots.
  opt.searchRoots.push_back(dir.str("does-not-exist"));
  CHECK(resolveFile(FileRef{}, opt).how == ResolvedFile::How::NotFound);
}

TEST_CASE("Preset library: save, rename, duplicate, delete, favourites")
{
  TempDir dir("ironclad_library");
  PresetLibrary lib(dir.str("Presets"));
  const int numFactory = static_cast<int>(factoryPresets().size());
  REQUIRE(static_cast<int>(lib.entries().size()) == numFactory);
  CHECK(lib.entries()[0].isFactory());

  PresetData p;
  p.name = "My: Lead?";
  p.category = "Live";
  p.params["od.drive"] = 66.0;
  const std::string path = lib.save(p, false);
  CHECK(path == lib.pathFor("Live", "My_ Lead_"));
  CHECK(fs::exists(pathFromUtf8(path)));
  REQUIRE(static_cast<int>(lib.entries().size()) == numFactory + 1);
  const int idx = lib.indexOfPath(path);
  REQUIRE(idx == numFactory);
  CHECK(lib.entries()[static_cast<std::size_t>(idx)].category == "Live");
  CHECK(lib.load(idx).valueFor(kParamOdDrive) == 66.0);

  CHECK_THROWS_WITH(lib.save(p, false), "A preset named \"My_ Lead_\" already exists.");
  p.params["od.drive"] = 10.0;
  CHECK_NOTHROW(lib.save(p, true));
  CHECK(lib.load(lib.indexOfPath(path)).valueFor(kParamOdDrive) == 10.0);
  p.name = "  ";
  CHECK_THROWS_WITH(lib.save(p, false), "Please enter a preset name.");

  lib.setFavorite(lib.indexOfPath(path), true);
  const std::string dup = lib.duplicate(lib.indexOfPath(path));
  CHECK(utf8FromPath(pathFromUtf8(dup).filename()) == "My_ Lead_ 2.ironclad");
  const std::string renamed = lib.rename(lib.indexOfPath(path), "Solo");
  CHECK_FALSE(fs::exists(pathFromUtf8(path)));
  CHECK(lib.entries()[static_cast<std::size_t>(lib.indexOfPath(renamed))].favorite); // favourite follows a rename
  CHECK(lib.load(lib.indexOfPath(renamed)).name == "Solo");

  lib.setFavorite(0, true); // factory presets can be favourites
  PresetLibrary reopened(dir.str("Presets"));
  CHECK(reopened.entries()[0].favorite);
  CHECK(reopened.entries()[static_cast<std::size_t>(reopened.indexOfPath(renamed))].favorite);
  CHECK_FALSE(reopened.entries()[static_cast<std::size_t>(reopened.indexOfPath(dup))].favorite);

  CHECK_THROWS(reopened.remove(0)); // factory: read-only
  CHECK_THROWS(reopened.rename(0, "X"));
  reopened.remove(reopened.indexOfPath(dup));
  CHECK(reopened.indexOfPath(dup) == -1);
  CHECK(static_cast<int>(reopened.entries().size()) == numFactory + 1);

  // A damaged preset file lists fine but reports an error when loaded.
  writeFile(dir.str("Presets/User/Broken.ironclad"), "{garbage");
  reopened.rescan();
  const int broken = reopened.indexOfPath(dir.str("Presets/User/Broken.ironclad"));
  REQUIRE(broken >= 0);
  CHECK(reopened.entries()[static_cast<std::size_t>(broken)].category.empty());
  CHECK_THROWS_WITH(reopened.load(broken), "The preset file is damaged (invalid JSON).");
}

TEST_CASE("Preset library stores files with hash and a path relative to the preset")
{
  TempDir dir("ironclad_library_refs");
  writeFile(dir.str("Captures/Plexi.nam"), "capture-bytes");
  PresetLibrary lib(dir.str("Presets"));
  PresetData p;
  p.name = "With Capture";
  FileRef r;
  r.path = dir.str("Captures/Plexi.nam");
  p.capture = r;
  const PresetData loaded = PresetLibrary::loadFile(lib.save(p, false));
  REQUIRE(loaded.capture.has_value());
  CHECK(loaded.capture->sha256 == Sha256::ofBytes("capture-bytes", 13));
  CHECK(loaded.capture->relative == "../../Captures/Plexi.nam"); // saved in Presets/User/
  CHECK(loaded.capture->fileName == "Plexi.nam");
}

TEST_CASE("Library folder layout: IRONCLAD / Packs / User / Default, migration from 0.5.0")
{
  TempDir dir("ironclad_layout");
  const std::string root = dir.str("IRONCLAD");
  // A 0.5.0 user preset with a favourite, saved in the old place.
  writeFile(dir.str("IRONCLAD/Presets/Live/Old Song.ironclad"), [] {
    PresetData p;
    p.name = "Old Song";
    p.params["od.drive"] = 44.0;
    return presetToJson(p);
  }());
  writeFile(dir.str("IRONCLAD/Presets/favorites.json"), R"({"favorites":["user:Live/Old Song.ironclad"]})");
  // A preset pack with a sub-folder.
  writeFile(dir.str("IRONCLAD/Packs/Studio Pack/Leads/Solo.ironclad"), [] {
    PresetData p;
    p.name = "Solo";
    return presetToJson(p);
  }());

  PresetLibrary lib(root);
  REQUIRE(lib.ensureLayout());
  for (const char* sub : {"IRONCLAD", "Packs", "User"})
    CHECK(fs::is_directory(dir.path / "IRONCLAD" / sub));
  CHECK(fs::exists(pathFromUtf8(lib.defaultPresetPath())));
  CHECK_FALSE(fs::exists(dir.path / "IRONCLAD" / "Presets")); // migrated and removed
  CHECK(fs::exists(dir.path / "IRONCLAD" / "User" / "Live" / "Old Song.ironclad"));

  // Factory presets are files now, in their designed order, read-only.
  const auto& factory = factoryPresets();
  for (std::size_t i = 0; i < factory.size(); ++i)
  {
    const auto& e = lib.entries()[i];
    CAPTURE(e.name);
    CHECK(e.isFactory());
    CHECK(e.readOnly());
    CHECK(e.name == factory[i].name);
    CHECK(e.category == factory[i].category);
    CHECK_FALSE(e.path.empty());
    CHECK(lib.load(static_cast<int>(i)).templateOnly); // the template flag survives the file
  }
  CHECK_THROWS_WITH(lib.remove(0), "Factory and pack presets cannot be deleted.");

  // Pack and user presets follow, with categories; the old favourite moved along.
  const int pack = lib.indexOfPath(dir.str("IRONCLAD/Packs/Studio Pack/Leads/Solo.ironclad"));
  REQUIRE(pack >= 0);
  CHECK(lib.entries()[static_cast<std::size_t>(pack)].source == PresetEntry::Source::Pack);
  CHECK(lib.entries()[static_cast<std::size_t>(pack)].category == "Studio Pack / Leads");
  CHECK(lib.entries()[static_cast<std::size_t>(pack)].readOnly());
  const int old = lib.indexOfPath(dir.str("IRONCLAD/User/Live/Old Song.ironclad"));
  REQUIRE(old > pack);
  CHECK(lib.entries()[static_cast<std::size_t>(old)].category == "Live");
  CHECK(lib.entries()[static_cast<std::size_t>(old)].favorite);
  CHECK(lib.load(old).valueFor(kParamOdDrive) == 44.0);

  // Duplicating a pack preset puts the copy in User/<Pack>.
  const std::string copy = lib.duplicate(pack);
  CHECK(fs::equivalent(pathFromUtf8(copy), dir.path / "IRONCLAD" / "User" / "Studio Pack" / "Solo 2.ironclad"));

  // A tampered factory file is restored by the next start.
  const std::string initPath = lib.entries()[0].path;
  writeFile(initPath, "{garbage");
  PresetLibrary again(root);
  again.ensureLayout();
  CHECK_NOTHROW(again.load(again.indexOfPath(initPath)));

  // Default: Init until the user saves another one; reset goes back to Init.
  CHECK(again.loadDefault().name == "Default");
  CHECK(again.loadDefault().valueFor(kParamOdDrive) == paramSpec(kParamOdDrive).defaultValue);
  PresetData mine;
  mine.params["od.drive"] = 12.0;
  again.saveDefault(mine);
  CHECK(again.loadDefault().valueFor(kParamOdDrive) == 12.0);
  CHECK_FALSE(again.loadDefault().templateOnly);
  PresetLibrary third(root);
  third.ensureLayout(); // never overwrites an existing Default
  CHECK(third.loadDefault().valueFor(kParamOdDrive) == 12.0);
  third.resetDefault();
  CHECK(third.loadDefault().valueFor(kParamOdDrive) == paramSpec(kParamOdDrive).defaultValue);

  // An unwritable / empty root still offers the built-in factory presets.
  PresetLibrary none{std::string{}};
  CHECK_FALSE(none.ensureLayout());
  CHECK(none.entries().size() == factory.size());
  CHECK(none.entries()[0].builtinIndex == 0);
  CHECK(none.loadDefault().name == "Default");
}

TEST_CASE("File names are made safe on Windows and macOS")
{
  CHECK(PresetLibrary::sanitizeName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
  CHECK(PresetLibrary::sanitizeName("  Clean.  ") == "Clean");
  CHECK(PresetLibrary::sanitizeName("CON") == "CON_");
  CHECK(PresetLibrary::sanitizeName("Temiz Ses \xC3\xBC\xC5\x9F") == "Temiz Ses \xC3\xBC\xC5\x9F");
  CHECK(PresetLibrary::sanitizeName(std::string(300, 'x')).size() == 100);
}

TEST_CASE("Factory presets are valid templates")
{
  std::set<std::string> names;
  for (const auto& p : factoryPresets())
  {
    CAPTURE(p.name);
    CHECK(names.insert(p.category + "/" + p.name).second);
    CHECK(p.templateOnly);
    CHECK_FALSE(p.capture.has_value());
    for (const auto& [id, v] : p.params)
    {
      CAPTURE(id);
      const int idx = findParamIndex(id);
      REQUIRE(idx >= 0);
      CHECK(v >= paramSpec(idx).minValue);
      CHECK(v <= paramSpec(idx).maxValue);
    }
  }
  CHECK(factoryPresets().size() >= 10);
}

TEST_CASE("Undo/redo: snapshots, coalescing, redo invalidation, capacity")
{
  UndoStack u;
  UndoSnapshot s0, s1, s2;
  s1.values[kParamOdDrive] = 10.0;
  s2.values[kParamOdDrive] = 20.0;
  s2.capture = "/a.nam";

  CHECK_FALSE(u.canUndo());
  u.record(s0, kParamOdDrive, 0.0);
  u.record(s1, kParamOdDrive, 0.3); // same knob, same gesture: coalesced
  CHECK(u.undoDepth() == 1);
  u.record(s1, kParamOdDrive, 2.0); // same knob, later: new step
  CHECK(u.undoDepth() == 2);
  u.record(s2, UndoStack::kNoCoalesce, 2.1); // capture load
  CHECK(u.undoDepth() == 3);

  UndoSnapshot current = s2;
  current.capture = "/b.nam";
  auto back = u.undo(current);
  REQUIRE(back.has_value());
  CHECK(*back == s2);
  CHECK(u.canRedo());
  auto fwd = u.redo(*back);
  REQUIRE(fwd.has_value());
  CHECK(fwd->capture == "/b.nam");

  u.undo(current);
  u.record(s0, kParamReverbMix, 5.0); // a new edit after undo discards the redo branch
  CHECK_FALSE(u.canRedo());

  UndoStack big;
  for (int i = 0; i < 500; ++i)
  {
    UndoSnapshot s;
    s.values[0] = i;
    big.record(s, UndoStack::kNoCoalesce, i);
  }
  CHECK(big.undoDepth() == UndoStack::kCapacity);
}

TEST_CASE("MIDI map: learn, expression, footswitches, actions")
{
  MidiMap m;
  MidiEvent e;
  CHECK_FALSE(m.handleCc(11, 0.5, e)); // unassigned

  m.armLearn(kParamWahPosition, false);
  CHECK(m.learnTarget() == kParamWahPosition);
  REQUIRE(m.handleCc(11, 0.2, e));
  CHECK(e.kind == MidiEvent::Kind::Learned);
  CHECK(e.cc == 11);
  CHECK(m.learnTarget() == kMidiNoTarget);
  CHECK(m.ccForTarget(kParamWahPosition) == 11);

  // Expression pedal: linear parameter.
  REQUIRE(m.handleCc(11, 1.0, e));
  CHECK(e.kind == MidiEvent::Kind::SetParam);
  CHECK(e.value == doctest::Approx(100.0).scale(0.0));
  REQUIRE(m.handleCc(11, 0.25, e));
  CHECK(e.value == doctest::Approx(25.0).scale(0.0));

  // Log-scaled parameter follows the same taper as the host knob.
  m.assign(12, {kParamEqMidFreq, false});
  REQUIRE(m.handleCc(12, 0.5, e));
  CHECK(e.value == doctest::Approx(std::sqrt(200.0 * 5000.0)).scale(0.0));

  // Latching footswitch.
  m.assign(20, {kParamDelayEnabled, false});
  REQUIRE(m.handleCc(20, 1.0, e));
  CHECK(e.value == 1.0);
  REQUIRE(m.handleCc(20, 0.0, e));
  CHECK(e.value == 0.0);

  // Momentary footswitch in toggle mode: one flip per press, nothing on release.
  m.assign(21, {kParamReverbEnabled, true});
  REQUIRE(m.handleCc(21, 1.0, e));
  CHECK(e.kind == MidiEvent::Kind::ToggleParam);
  CHECK_FALSE(m.handleCc(21, 1.0, e)); // repeated "on" is not a new press
  CHECK_FALSE(m.handleCc(21, 0.0, e));
  CHECK(m.handleCc(21, 1.0, e));

  // Actions fire on press only.
  m.assign(30, {kMidiActionTapTempo, false});
  REQUIRE(m.handleCc(30, 1.0, e));
  CHECK(e.kind == MidiEvent::Kind::Action);
  CHECK(e.target == kMidiActionTapTempo);
  CHECK_FALSE(m.handleCc(30, 0.0, e));

  // One CC per target: re-learning moves the assignment.
  m.armLearn(kParamWahPosition, false);
  REQUIRE(m.handleCc(4, 0.0, e));
  CHECK(m.ccForTarget(kParamWahPosition) == 4);
  CHECK(m.assignment(11).target == kMidiNoTarget);

  // Persistence through JSON and through data().
  const MidiMapData d = midiMapFromJson(midiMapToJson(m.data()));
  MidiMap copy;
  copy.setData(d);
  for (int cc = 0; cc < 128; ++cc)
    CHECK(copy.assignment(cc) == m.assignment(cc));
  CHECK(midiMapFromJson("{broken").cc.empty());

  // The audio-thread path never allocates (learn included).
  {
    m.armLearn(kParamVolume, false);
    test::ScopedAllocationCheck check;
    for (int k = 0; k < 1000; ++k)
      m.handleCc(k % 128, (k % 7) / 6.0, e);
    const auto counts = check.finish();
    CHECK(counts.allocations == 0);
    CHECK(counts.deallocations == 0);
  }

  // Invalid targets and CC numbers are ignored.
  m.assign(200, {kParamWahPosition, false});
  m.assign(5, {kNumParams + 7, false});
  CHECK(m.assignment(5).target == kMidiNoTarget);
  CHECK_FALSE(m.handleCc(-1, 1.0, e));
  m.clearAll();
  CHECK(m.data().cc.empty());
}
