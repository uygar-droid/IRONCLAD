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
| Amp (M2) | Background load + metadata; engine output equals NAM Core's own output (< 1e-9); all host rates 44.1–192 kHz with resampling (level within 1 dB); damaged / missing / non-.nam / unknown-architecture files → error, previous capture keeps playing; missing file kept in state; clear → dry; crossfade continuity; Amp Off → dry; request coalescing; SR/block change during load and while active | M2 |
| Amp (M2) | **Stress: capture switch every 3 ms for 1.5 s at 48 and 44.1 kHz — 0 allocations / 0 frees on the audio thread** (≈450 switches each) | M2 |
| Cabinet DSP (M3) | FFT vs naive DFT and round trip; convolver = direct convolution (< 1e-10) for IR lengths 1–4800 and block sizes 1/7/64/100/512/random; impulse reproduces IR at sample 0 (zero latency); no allocation in process/reset; offline resampler 44.1↔48↔96↔192 kHz (error < 1e-3); WAV reader 16/24/32-int, float32/64, extensible, LIST chunk, stereo; rejects empty / non-WAVE / no data / ADPCM / zero-length / missing | M3 |
| Cabinet (M3) | Mono IR applied exactly (unit-energy) on L and R; stereo IR keeps channels; 44.1 kHz IR at 44.1/48/96 kHz hosts keeps unit energy; blend 25 %; slot off → other slot full level; pan hard left; invert; level; Cab off → dry; HPF/LPF response (−40 dB at 100 Hz with HPF 1 kHz, −38 dB at 8 kHz with LPF 1 kHz, −3.0 dB at corner, transparent at extremes); missing / non-WAV / damaged / silent files → error, previous IR kept; 2 s IR truncated; clear → dry; SR change with IR active and loading | M3 |
| Cabinet (M3) | **Stress: IR switch every 3 ms for 1.5 s (102 activations) — 0 allocations / 0 frees on the audio thread** | M3 |
| Effects (M4) | Gate open/closed levels; compressor steady-state gain vs formula; drive harmonics vs drive, bounded output, silence in → silence out; ADAA aliasing vs naive clipper; wah resonance vs pedal; EQ band gains / flatness; chorus transparency at mix 0 and stereo width; delay echo times and feedback, ping-pong alternation, sync divisions, tap tempo; analog delay bounded at 95 % feedback; reverb RT60 within 10 % for 3 types × 2 decays, stable at 10 s for 20 s of noise, click-free type changes | M4 |
| Effect chain (M4) | All effects off → bit-transparent; each effect switched on/off while playing → no step larger than 0.08 on a 0.3-amplitude sine; all 11 effects on with parameter changes for 2000 blocks → finite, **0 audio-thread allocations**; all effects at 44.1–192 kHz | M4 |
| Test hygiene (M4) | All `doctest::Approx(...).epsilon(e)` checks now use `.scale(0)` (strictly relative). Doctest's default scale adds an absolute term, which had made some earlier checks looser than intended; re-running with strict tolerances exposed one test comparison bug and one real product deviation (plate RT60), both fixed. | M4 |
| Convolver (0.4.1) | Equality with direct convolution extended to IR lengths at every stage boundary (511/512/513, 4095/4096/4097) and 20 000 / 48 000 taps, block sizes 1 / 333 / 4096 / random | 0.4.1 |
| Presets / state (M5) | SHA-256 = FIPS 180-4 vectors (incl. 1 M × 'a', incremental, file); preset JSON round trip, non-ASCII names, unknown ids ignored, defaults for missing ids, clamping; damaged / foreign / newer-schema / schema-less files rejected with messages; DAW state M1 / v1 / v2 decoded from byte-exact legacy layouts (also after a host prefix); v3 round trip incl. MIDI map; truncated / corrupt / newer / null data → refused | M5 |
| File resolution (M5) | Saved path; relative to the preset; moved into the library (found by name, hash-verified); moved and renamed (found by content); same-named different file not accepted; missing / empty / nonexistent roots → NotFound (no throw); paths with non-ASCII folders | M5 |
| Preset library (M5) | Factory first; save into category; overwrite protection; empty name refused; duplicate naming; rename keeps favourite; favourites persist across instances; factory presets read-only; delete; damaged file listed but reports an error; refs saved with hash and relative path; Windows/macOS-safe names; 12 factory templates use valid ids and in-range values | M5 |
| Undo (M5) | Coalescing within a gesture, separate steps later, undo/redo round trip, redo cleared by a new edit, 200-step cap | M5 |
| MIDI (M5) | Learn; linear and log-taper parameter mapping; latching footswitch; toggle on rising edge only; actions on press only; re-learn moves the CC; JSON / data round trip; invalid CC / targets ignored; **0 allocations in the audio-thread path (learn included)** | M5 |
| Engine (M5) | Volume pedal taper, 15 ms ramp, delay tail continues after the pedal closes; **preset transition: silence reached, no step for a +12 dB jump, new gain fully applied (blocks 1/64/512)**; input calibration equals the same gain on Amp Input (lstm.nam, 18.3 dBu) and is neutral at equal levels | M5 |
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
- [ ] (M5) Save a preset, change things, load it back; rename / duplicate / delete; favourites
      survive a REAPER restart; factory templates keep the loaded capture and IRs.
- [ ] (M5) Move a capture to another folder, load the preset: it is found (or named as missing).
- [ ] (M5) Preset changes while playing: no clicks. Undo / redo of knobs, capture and preset loads.
- [ ] (M5) MIDI learn: expression pedal → wah / volume, footswitch → effect on/off, Tap Tempo.
- [ ] (M5) Projects saved with 0.4.x open with the same sound.

## 4. CPU benchmark (`ironclad_bench`, development container, Xeon 2.1 GHz, one core, 48 kHz)

| Configuration | Average, block 128 | Worst block, block 64 |
|---|---|---|
| Empty chain | 0.2 % | — |
| NAM standard WaveNet | 26–27 % | ~113 % (NAM Core internal buffer management; same in the official plugin) |
| One 2048-tap IR | 0.9 % | 8 % |
| Two 1 s stereo IRs, 0.4.0 | 57.9 % | 521 % |
| Two 1 s stereo IRs, 0.4.1 | 5.5 % | 69 % |
| Each effect | 0.2–1.1 % | — |
| Capture + 2 IRs + all effects | 33.5 % | — |

Apple Silicon is considerably faster per core than this container; the REAPER figure reported by
the user (~60 % with 0.4.0) depends on the IR lengths used and on REAPER's buffer size.

## 5. Recorded results

### 2026-11-13 — GitHub Actions run 43416941491 (0.5.2, macOS shared preset folder)

Windows x64 and macOS universal (macOS job re-run: the first attempt never got a runner and was
cancelled by GitHub after 15 min): built, `ironclad_tests` **83/83**, Steinberg validator
**47 passed, 0 failed**. User REAPER test of 0.5.1 (2026-11-13): preset save works, the preset is
restored after closing and reopening REAPER, browser listing correct.

### 2026-11-13 — GitHub Actions run 43394437142 (0.5.1, preset folder layout)

Windows x64 and macOS universal: built, `ironclad_tests` **82/82** (new: folder layout,
factory files, packs, Default, 0.5.0 migration), Steinberg validator **47 passed, 0 failed**.

### 2026-11-13 — GitHub Actions run 43345267503, commit `830cf01` (M5, 0.5.0)

Windows x64 and macOS universal: plugin built (incl. the new preset / MIDI code), `ironclad_tests`
**81/81 test cases passed** on both, Steinberg validator **47 passed, 0 failed** on both,
`lipo` arm64 + x86_64 and ad-hoc signature verified. The previous run (43344309425) failed on
Windows only in a test that compared a found path with mixed `/` and `\` separators; the test
now compares the files, the product code was not changed.

### 2026-11-13 — internal development container (Linux x86-64, GCC 13.3, Release), 0.5.0 engine

`ironclad_tests`: **81 test cases, 498 198 assertions, all passed** (12 new M5 test cases in
`TestPresets.cpp`, 2 in `TestEngine.cpp`, 1 in `TestAmp.cpp`). The plugin sources
(`IRONCLAD.cpp`, `IRONCLADPresets.cpp`) were syntax-checked against the iPlug2 / VST3 SDK headers
with stubbed macOS system headers (0 errors); they are compiled for real only by CI.

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

### 2026-11-13 — GitHub Actions run 43338973432 (0.4.1)
Windows x64 and macOS universal: build ✅, `ironclad_tests` 66/66 ✅, Steinberg validator 47/0 ✅.

### 2026-11-13 — Real host: REAPER on the user's Mac mini, 0.4.0 from run 43317318965
Executed by the user with a real guitar:
- ✅ Project saved with 0.3.0 opens; capture, IRs and settings restored.
- ✅ STOMP / AMP / CAB / POST FX page switching.
- ✅ Every STOMP pedal works and sounds plausible; no clicks when switching on/off.
- ✅ Delay with host tempo, Sync and Division; TAP works.
- ✅ Reverb type changes while playing are clean.
- ✅ Effect parameters record and play back as REAPER automation.
- ⚠ CPU: about 60 % in REAPER's FX CPU display with every effect on (user reports this is close
  to a Neural DSP plugin on the same machine). Profiling follow-up below.

### 2026-11-13 — GitHub Actions run 43317318965, commit `2518a16` (M4, 0.4.0)
| | Windows x64 | macOS universal (Intel runner) |
|---|---|---|
| Build (Release) | ✅ | ✅ `x86_64 arm64`, signature valid |
| `ironclad_tests` | ✅ 66/66 cases | ✅ 66/66 cases |
| Steinberg VST3 validator | ✅ 47/0 | ✅ 47/0 |

### 2026-11-13 — Real host: REAPER on the user's Mac, 0.3.0 from run 43312249410
Executed by the user with a real guitar:
- ✅ Project saved with 0.2.0 opens; capture restored.
- ✅ Mono IR in slot A: cabinet sound, waveform and file information shown.
- ✅ 44.1 kHz IR works (resampled); IRs in both slots work together.
- ✅ A/B Blend works.
- ✅ Level, Pan, Invert, On, Cab HPF and Cab LPF behave correctly while playing; no crackle or dropouts.
- ✅ Switching IRs during playback: no click.
- ✅ Save → close → reopen: capture and both IRs restored.

### 2026-11-13 — GitHub Actions run 43312249410, commit `dedc71a` (M3, 0.3.0)
| | Windows x64 | macOS universal (built on the `macos-15-intel` runner) |
|---|---|---|
| Build (Release) | ✅ | ✅ `x86_64 arm64`, signature valid |
| `ironclad_tests` | ✅ 49/49 cases | ✅ 49/49 cases |
| Steinberg VST3 validator | ✅ 47/0 | ✅ 47/0 |

The macOS arm64 hosted runners were unavailable for ~13 h (jobs never acquired, cancelled 4×);
the macOS job now runs on the Intel runner, which builds the same universal binary.

### 2026-11-12 — Real host: REAPER on the user's Mac, M2 build from run 43110255628
Executed by the user with a real guitar and community `.nam` captures:
- ✅ Project saved with the M1 build (0.1.x state format) opens correctly.
- ✅ LOAD CAPTURE loads a `.nam`; amp sound is heard; name and metadata are shown.
- ✅ Switching captures during playback: clean, no click or dropout.
- ✅ Amp Input, Amp Output, Amp ON/OFF and Normalize behave as expected.
- ✅ The macOS file dialog only allows `.nam` files (others greyed out), so wrong-type files
  cannot be chosen from the UI; the engine-level error path is covered by unit tests.
- ✅ Project save → reopen restores the same capture and all settings.

Note: the user first saw the M1 editor after installing M2 — REAPER keeps an already-loaded
plugin binary in memory until it quits. INSTALL.md now says to quit the DAW before updating.

### 2026-11-12 — GitHub Actions run 43110255628, commit `54b62e2` (M2)
| | Windows x64 | macOS universal |
|---|---|---|
| Build (Release) | ✅ | ✅ `x86_64 arm64`, signature valid |
| `ironclad_tests` | ✅ 32/32 cases, 1940 assertions (incl. M2 stress: 0 audio-thread allocations) | ✅ 32/32, 1940 |
| Steinberg VST3 validator | ✅ 47/0 | ✅ 47/0 |

Defect found by CI and fixed: MSVC build failed because `<windows.h>` (via WDL in the resampler)
defines `min`/`max` macros → `NOMINMAX` for the engine target.

### 2026-11-12 — Real host: REAPER on the user's Mac (macOS), build from run 43103645908
Executed by the user with a real guitar input:
- ✅ Plugin appears in REAPER's FX list and opens (after removing the download quarantine flag).
- ✅ Guitar audio passes through.
- ✅ Input and Output knobs change the level; knob movements are written as automation.
- ✅ Project saved, closed and reopened: knob values restored.

Install issue observed: the first scan did not list the plugin because macOS had quarantined the
downloaded bundle, and the bundle was in the system folder `/Library/...`, where the `xattr`
command needs `sudo`. INSTALL.md updated accordingly.

Windows real-host test: not yet executed (no Windows machine available so far).
