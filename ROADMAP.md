# IRONCLAD — Roadmap

Status legend: ✅ implemented & tested · 🟡 implemented, not fully tested · 🟠 partial ·
⬜ planned · ⛔ blocked

**Final goal:** a finished, genuinely functional IRONCLAD **VST3** that installs and works in a
real DAW on **Windows x64** and **macOS (Apple Silicon + Intel)**, delivered as compiled binaries
with source, build scripts, documentation and an installation guide. Linux and AU are out of scope.

Last updated: 2026-11-12.

| # | Milestone | Status | Deliverable |
|---|---|---|---|
| 0 | Planning, research, licensing | ✅ approved 2026-11-12 | Architecture docs |
| 1 | Foundation | 🟡 built + validated in CI; real-host check pending | CI-built, validated, loadable VST3 (Win + macOS) with smoothed I/O gain |
| 2 | NAM integration | ⬜ | VST3 that loads and plays `.nam` captures |
| 3 | Cabinet engine | ⬜ | + dual-IR zero-latency cabinet |
| 4 | Core effects | ⬜ | + gate, comp, drives, EQ, chorus, delay, reverb, wah |
| 5 | Presets, state, automation, MIDI | ⬜ | Reliable DAW-integrated plugin |
| 6 | Graphical interface | ⬜ ⛔ Blender not available in this container | The photorealistic IRONCLAD UI |
| 7 | Advanced features | ⬜ | Remaining effects, routing, optimisation |
| 8 | Release | ⬜ | **Release VST3 binaries for Windows and macOS**, installation guide, release notes |

Every milestone ends with: build of both targets in CI → automated tests → VST3 validator →
report of what was actually run. Milestones 2–8 additionally require a **real-host check in
REAPER** on Windows (and on macOS whenever a Mac is available) before they are marked ✅.

---

## Milestone 1 — Foundation

Goal: a repository that builds an IRONCLAD VST3 (audio passthrough with smoothed input/output
gain) natively on Windows and macOS CI runners, passes the Steinberg validator, and has the
framework-free engine with its first passing tests.

| Task | Status |
|---|---|
| Git repository, pinned submodules (iPlug2, NAM Core, doctest) | ✅ |
| Top-level CMake (C++20, Windows/macOS only for the plugin) + CMakePresets | ✅ |
| Engine skeleton: `Engine`, `SmoothedValue`, `SpscQueue`, denormal guard, `ParamRegistry` | ✅ |
| NAM Core compiled as an IRONCLAD library target (`ironclad_nam`) | ✅ |
| Tests: gain/smoothing, sample-rate × block-size × channel matrix, SPSC, allocation guard, parameter-ID snapshot, NAM model load + process smoke test | ✅ |
| VST3 shell (`Source/Plugin`), temporary minimal UI (replaced in M6) | ✅ |
| Scripts: bootstrap, build, validate (PowerShell + bash) | ✅ |
| GitHub Actions: Windows + macOS build, tests, validator, artifacts | ✅ |
| **CI actually run, logs reviewed** | ✅ run 43102977856 (see TESTING.md) |
| Real-host smoke test (REAPER, Windows) | ⬜ after first CI artifact |

Acceptance criteria (shown with real output):
- [x] CI green on Windows and macOS runners.
- [x] Steinberg `validator` passes on both VST3 builds (47/47 each).
- [x] Engine + NAM Core tests pass on both runners.
- [ ] VST3 from CI loads in REAPER on Windows; audio passes; gain knobs work and automate.

## Milestone 2 — NAM integration
Loader thread with command/ready/retire queues; capture browser (folder scan, metadata);
user-facing error messages; crossfaded switching; resampling for non-48 kHz hosts with reported
latency; input calibration and output normalisation; stress test (switch every block, zero
allocations); CPU benchmark per architecture; float vs double decision.
Real-host gate: load several community captures in REAPER, play guitar/test signal, switch
captures during playback without clicks.

## Milestone 3 — Cabinet
WAV parser tests (16/24/32-int, 32-float, mono/stereo, corrupt files); r8brain IR resampling;
zero-latency partitioned convolver verified against direct convolution (error < −120 dBFS);
dual IR with level/pan/polarity; HPF/LPF; waveform thumbnail.

## Milestone 4 — Core effects
Noise gate, compressor, overdrive, distortion, fuzz, 3-band + parametric EQ, chorus,
digital/analog delay (host sync, tap tempo), room/hall/plate reverb, wah. Each with impulse/
step/sine tests, extreme-setting stability tests and a bypass click test.

## Milestone 5 — Presets, state, automation, MIDI
JSON schema v1 + migrations, missing-file resolver, factory/user libraries, categories,
favourites, rename/duplicate/delete, undo/redo, MIDI learn (CC → parameter, expression for
wah/volume, tap tempo). Real-host gate: automation lanes, project save → close → reopen
restores everything in REAPER.

## Milestone 6 — Graphical interface
Blender pipeline (render scripts, filmstrip packer), photorealistic amp head, STOMP / AMP / CAB /
POST FX pages, preset browser, meters, tuner, 100–200 % scaling. Every control functional and
host-synchronised. Requires Blender on a workstation.

## Milestone 7 — Advanced
Octaver, pitch shifter, graphic EQ, phaser, flanger, tremolo, vibrato, rotary, envelope filter,
tape/ping-pong/stereo delay, spring reverb; reorderable routing; performance optimisation;
multi-capture research for authentic tone controls.

## Milestone 8 — Release
Full regression; Release VST3 binaries for Windows x64 and macOS universal attached to a
GitHub Release; Windows installer (Inno Setup) and macOS pkg; installation guide; release notes.
Signing: Windows code-signing certificate (recommended) and Apple Developer ID + notarisation
(required to avoid Gatekeeper warnings) — both must be provided by the product owner.
