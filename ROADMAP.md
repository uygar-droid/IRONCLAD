# IRONCLAD — Roadmap

Status legend: ✅ implemented & tested · 🟡 implemented, not fully tested · 🟠 partial ·
⬜ planned · ⛔ blocked

Last updated: 2026-11-11 (Session 1 — planning only, **no code written yet**).

| # | Milestone | Status | Deliverable |
|---|---|---|---|
| 0 | Session 1: environment, research, licensing, architecture docs | ✅ docs written · awaiting approval | ARCHITECTURE / ROADMAP / DEPENDENCIES / LICENSES |
| 1 | Foundation | ⬜ | Loadable empty VST3 (+AU, standalone) built by CI on Win + macOS |
| 2 | NAM integration | ⬜ | Functional NAM amp plugin |
| 3 | Cabinet engine | ⬜ | NAM + dual-IR cabinet |
| 4 | Core effects | ⬜ | Usable guitar effects platform |
| 5 | Presets & state | ⬜ | Reliable DAW-integrated plugin |
| 6 | Graphical interface | ⬜ ⛔ Blender not available here | Distinctive IRONCLAD UI |
| 7 | Advanced features | ⬜ | Routing, more effects, optimisation |
| 8 | Release preparation | ⬜ ⛔ needs signing certificates + Mac | Signed installers, validated builds |

---

## Milestone 1 — Foundation (detailed plan)

Goal: a repository that builds an **empty, loadable IRONCLAD plugin** (audio passthrough with
input/output gain, smoothed) as VST3 on Windows and VST3+AU on macOS, plus the framework-free
engine library with its first passing tests — all reproducible from scripts and CI.

### Tasks

1. **Repository**
   - `git init`, `.gitignore`, `.gitattributes` (LF, LFS rules for `Assets/**/*.png|*.blend`).
   - Submodules, pinned:
     - `ThirdParty/iPlug2` → upstream `iPlug2/iPlug2`
     - `ThirdParty/NeuralAmpModelerCore` → `sdatkinson/NeuralAmpModelerCore` (recursive: Eigen, AudioDSPTools)
     - `ThirdParty/VST3_SDK` → fetched by iPlug2's `download-vst3-sdk.sh` at a pinned **v3.8.x** tag (MIT)
     - `ThirdParty/doctest` (tests only)
   - `README.md`, `CHANGELOG.md`, `TESTING.md` created.
2. **CMake**
   - Top-level `CMakeLists.txt`, C++20, options: `IRONCLAD_BUILD_PLUGIN` (default ON on
     Win/macOS, OFF on Linux), `IRONCLAD_BUILD_TESTS`.
   - Target `ironclad_engine` (static lib, no framework headers).
   - Target `IRONCLAD` via `iplug_add_plugin(... FORMATS APP VST3 AU)`.
   - `CMakePresets.json`: `win-vs2022-x64`, `mac-xcode-universal`, `linux-engine-tests`.
3. **Plugin shell** (`Source/Plugin/`)
   - `config.h`: name IRONCLAD, unique 4-char plugin ID and manufacturer ID (registered/checked
     against collisions), channel I/O `1-1 1-2 2-2`, latency 0.
   - Parameters: `global.input_db`, `global.output_db` (stable IDs, index 0 and 1).
   - `ProcessBlock` → `Engine::process()` (gain + smoothing only).
   - Minimal placeholder IGraphics UI clearly marked as temporary (two knobs, version label).
4. **Engine skeleton** (`Source/Audio`, `Source/Parameters`, `Source/Utilities`)
   - `Engine::prepare(sampleRate, maxBlock, channels)`, `Engine::process()`.
   - `SmoothedValue`, `SpscQueue<T, N>` (fixed capacity), FTZ/DAZ guard.
   - `ParamRegistry` + ID snapshot test.
5. **Tests** (`Tests/`, doctest, run on all 3 OSes)
   - Gain/smoothing correctness (no step larger than ε per sample at every block size).
   - All sample rates × block sizes (incl. 1 and random variable sizes) × mono/stereo.
   - Allocation guard: zero allocations inside `process()`.
   - Parameter-ID snapshot.
   - **NAM Core's own test suite** (`run_tests`) built and run, to prove the dependency builds
     with our toolchain flags.
6. **Scripts** (`Scripts/`)
   - `bootstrap.ps1` / `bootstrap.sh`: submodules + VST3 SDK + validator build.
   - `build.ps1` / `build.sh`: configure + build a preset.
   - `validate.ps1` / `validate.sh`: run Steinberg `validator` on the built `.vst3`
     (and `auval -v aufx Ircl <Mfr>` on macOS).
7. **CI** (`.github/workflows/build.yml`)
   - `ubuntu-latest`: engine + tests.
   - `windows-latest`: VS2022 x64 → VST3 + APP, run tests + VST3 validator, upload artifacts.
   - `macos-latest` (Apple Silicon): Xcode universal (arm64;x86_64) → VST3 + AU + APP, run
     tests, VST3 validator, `auval` (ad-hoc signed).
8. **Docs**: update ROADMAP, CHANGELOG, TESTING (QA checklist v1), DEPENDENCIES pins.

### Acceptance criteria (all must be shown with real output, not asserted)

- [ ] CI green on all three runners; logs linked in the milestone report.
- [ ] `validator` passes on Windows and macOS VST3 builds.
- [ ] `auval` passes on macOS AU build.
- [ ] You (Windows machine) load the VST3 in at least one DAW and hear passthrough audio; gain
      knobs work and automate.
- [ ] Engine tests and NAM Core tests pass on Linux, Windows, macOS.

### Known risks for M1

- iPlug2's CMake path is newer than its VS/Xcode project files — if a format target fails we
  fix it locally or fall back to the generated project files, and document which.
- GitLab (Eigen's home) was unreachable from this development container; CI runners and your PC
  normally can reach it. A hash-pinned mirror is the fallback (see DEPENDENCIES.md).
- The plugin adapter cannot be compiled in this Linux container (iPlug2 has no Linux target);
  only CI and your machine can verify it.

---

## Milestones 2–8 (summary; refined at the start of each)

**M2 NAM:** loader thread + command/ready/retire queues; capture browser (folder scan, metadata
table); error messages; crossfaded switching; resampling for non-48 kHz hosts with reported
latency; input calibration and output normalisation; stress test (switch every block for
10 min, zero allocations, no crash); CPU benchmark per architecture; float vs double decision.

**M3 Cabinet:** WAV parser tests (all bit depths, mono/stereo, corrupt files); r8brain
resampling; zero-latency partitioned convolver verified against direct convolution
(error < −120 dBFS); dual IR, pan, polarity, HPF/LPF; waveform thumbnail.

**M4 Core effects:** gate, compressor, overdrive, distortion, fuzz, 3-band + parametric EQ,
chorus, digital/analog delay (host sync, tap tempo), room/hall/plate reverb, wah — each with its
own tests (impulse/step/sine responses, stability at extreme settings, bypass click test).

**M5 Presets & state:** JSON schema v1, migrations, file-reference resolver, factory/user
libraries, categories, favourites, undo/redo, MIDI learn (CC→param, expression for wah/volume,
tap tempo), DAW session round-trip tests in REAPER/Cubase/Logic/Ableton where available.

**M6 GUI:** Blender asset pipeline (render script, naming, filmstrip packer), amp head, stomp
page, cab page, post-FX page, preset browser, meters, tuner display, scaling 100–200 %.

**M7 Advanced:** remaining effects (octaver, pitch shifter, graphic EQ, phaser, flanger,
tremolo, vibrato, rotary, envelope filter, tape/ping-pong/stereo delay, spring reverb),
reorderable routing, performance optimisation, multi-capture research for authentic tone
controls.

**M8 Release:** full regression, installers (Windows Inno Setup; macOS pkg, signed +
notarised), versioning, release notes. Requires an Apple Developer ID and (recommended) a
Windows code-signing certificate.
