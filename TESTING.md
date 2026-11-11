# IRONCLAD — Testing

Rule: a result is recorded here only if the test was actually executed. Each entry names the
machine/runner, date and commit.

## 1. Automated tests (`Tests/`, doctest → `ironclad_tests`)

| Area | Test | Since |
|---|---|---|
| Parameters | IDs append-only vs `Tests/param_ids.snapshot`; lookup by id; clamping | M1 |
| Smoothing | Ramp exactness and monotonicity; continuity when retargeted mid-ramp | M1 |
| Engine | Unity passthrough for 6 sample rates × 9 block sizes × 3 channel layouts (random noise, error < 1e-12) | M1 |
| Engine | Stereo→mono averaging; in-place buffers; null/disconnected channels; unprepared engine outputs silence | M1 |
| Engine | Gain change is smoothed (bounded per-sample delta) and settles exactly — 6 sample rates × blocks {1, 64, 512, random incl. > maxBlock} | M1 |
| Real-time safety | **Zero heap allocations/frees inside `Engine::process()`** (global operator new/delete instrumented); tracker self-test | M1 |
| Utilities | SPSC queue FIFO/capacity, move-only payloads, no allocation, 1,000,000 items across two threads | M1 |
| NAM Core | Loads example WaveNet (blocks 64/256/1024) and LSTM captures, output finite and non-silent; missing and malformed files throw (no crash) | M1 |

## 2. Plugin validation

| Check | Tool | Where |
|---|---|---|
| VST3 conformance | Steinberg `validator` (VST3 SDK v3.8.1) | CI, both OSes |
| Universal binary | `lipo -verify_arch arm64 x86_64` | CI, macOS |
| Code signature present | `codesign --verify` (ad-hoc until Developer ID) | CI, macOS |

## 3. Real-host QA checklist (REAPER) — run per milestone on Windows, and macOS when available

- [ ] Plugin appears after rescan; loads without error; editor opens and closes repeatedly.
- [ ] Audio passes on mono and stereo tracks; no clicks when moving knobs quickly.
- [ ] Knob movement is written as automation; automation playback moves the knobs.
- [ ] Project save → close REAPER → reopen: parameter values restored.
- [ ] Sample rates 44.1 / 48 / 96 kHz; buffer sizes 64 / 256 / 1024: no dropouts, no crash.
- [ ] Offline render matches realtime playback.
- [ ] Remove plugin while playing; undo; no crash.

## 4. Recorded results

### 2026-11-12 — internal development container (Linux x86-64, GCC 13.3, Release)
Not a product target; used only to exercise platform-independent engine code before CI exists.
- `ironclad_tests`: **21 test cases, 1288 assertions, all passed.**
- First run found a real defect: NAM architectures register through static initialisers,
  which the linker discarded from the static library ("No config parser registered for
  architecture: WaveNet"). Fixed by linking NAM Core as a whole archive (`ironclad::nam`).
  The regression is covered by the NAM smoke tests.

### 2026-11-12 — GitHub Actions run 43102977856, commit `b8a6855` (M1)
| | Windows x64 (`windows-2025`, MSVC 19.51, Ninja) | macOS universal (`macos-15`, Xcode 16.4, Ninja) |
|---|---|---|
| Build engine + tests + VST3 (Release) | ✅ | ✅ |
| `ironclad_tests` | ✅ 21/21 cases, 1288 assertions | ✅ 21/21 cases, 1288 assertions |
| Steinberg VST3 validator (SDK 3.8.1) | ✅ **47 passed, 0 failed** (32- and 64-bit processing suites) | ✅ **47 passed, 0 failed** |
| Universal binary | — | ✅ `x86_64 arm64` |
| Code signature | — | ✅ ad-hoc, `valid on disk` |

Earlier runs of the same day found and fixed two CI-script defects (wrong `lipo` argument
order; VST3-SDK cache restored before submodules). No product-code defect was found by CI.

### Real-host (REAPER) tests
Not yet executed. Requires a Windows/macOS machine with REAPER (the user's machine, or this
session linked to it through the Claude desktop app).
