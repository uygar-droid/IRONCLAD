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
| 1 | Foundation | ✅ CI + REAPER (macOS) verified; Windows host test pending | CI-built, validated, loadable VST3 (Win + macOS) with smoothed I/O gain |
| 2 | NAM integration | ✅ CI + REAPER (macOS) verified; Windows host test pending | VST3 that loads and plays `.nam` captures |
| 3 | Cabinet engine | ✅ CI + REAPER (macOS) verified; Windows host test pending | + dual-IR zero-latency cabinet |
| 4 | Core effects | 🟡 CI ✅ (both OSes); REAPER check pending | + gate, comp, drives, EQ, chorus, delay, reverb, wah |
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
| Real-host smoke test (REAPER) | ✅ macOS (user, 2026-11-12) · ⬜ Windows |

Acceptance criteria (shown with real output):
- [x] CI green on Windows and macOS runners.
- [x] Steinberg `validator` passes on both VST3 builds (47/47 each).
- [x] Engine + NAM Core tests pass on both runners.
- [x] VST3 from CI loads in REAPER (macOS); audio passes; gain knobs work, automate and restore with the project.
- [ ] Same on Windows (needs a Windows machine).

## Milestone 2 — NAM integration

| Task | Status |
|---|---|
| Background loader thread; requests coalesce (last wins) | ✅ unit-tested |
| Safe hand-over: SPSC ready/retire queues, old capture destroyed off the audio thread | ✅ unit-tested (zero allocations/frees on the audio thread during 456 switches) |
| Click-free switching: 25 ms equal-power crossfade | ✅ unit-tested (no discontinuity above steady-state level) |
| Resampling for non-native host rates (44.1–192 kHz), latency reported to host | ✅ unit-tested (level within 0.005 dB of native) |
| Sample-rate / block-size change while a capture is active or loading | ✅ unit-tested |
| Validation + user-facing errors (missing, damaged, wrong type, unknown architecture); active capture preserved on failure | ✅ unit-tested |
| Metadata (name, architecture, rate, gear, tone type, modeled by) | ✅ unit-tested |
| Output normalisation to −18 dB loudness (when the capture provides loudness) | ✅ unit-tested |
| Gain staging params: Amp On, Amp Input, Amp Output, Normalize | ✅ engine-tested |
| Plugin: LOAD CAPTURE / CLEAR, info + error display, capture path saved in the project | ✅ REAPER (macOS, user, 2026-11-12) |
| Input calibration (interface dBu ↔ capture `input_level_dbu`) | ⬜ moved to M5 (needs a per-user setting) |
| Capture browser (folder library view) | ⬜ moved to M6 (part of the final UI); M2 uses the OS file dialog |

Real-host gate: load several community captures in REAPER, play guitar, switch captures during
playback without clicks, save/reopen the project.

## Milestone 3 — Cabinet

| Task | Status |
|---|---|
| WAV reader: PCM 8/16/24/32-bit, float 32/64, WAVE_FORMAT_EXTENSIBLE, extra/odd chunks, mono/stereo/multichannel (first two used); clear errors for damaged/unsupported/empty/silent files | ✅ unit-tested |
| IR resampling to the host rate (offline Kaiser-windowed sinc, on the loader thread) | ✅ unit-tested (sine error < −54 dB re peak) |
| Zero-latency convolution: 64-tap direct head + uniformly partitioned FFT tail | ✅ unit-tested (equals direct convolution < 1e-10, any IR length / block size) |
| Two IR slots: level, pan (balance), polarity, on/off; linear A/B blend | ✅ unit-tested |
| HPF / LPF (12 dB/oct) on the cabinet output, transparent at their extreme settings | ✅ unit-tested (−3.0 dB at corner; bypassed at 20 Hz / 20 kHz) |
| IR normalisation (unit energy), 1 s length cap with "truncated" flag | ✅ unit-tested |
| Background loading, crossfaded IR switching, retire queue | ✅ unit-tested (102 switches, 0 audio-thread allocations) |
| Plugin: LOAD IR / CLEAR per slot, name + format info + error, waveform thumbnail, global Blend/HPF/LPF/Cab | ✅ REAPER (macOS, user, 2026-11-13) |
| IR paths saved in the project (state v2); 0.2.0 projects open | ✅ REAPER (macOS, user, 2026-11-13) |
| IR browser (folder library) | ⬜ M6 (final UI) |

Real-host gate: load mono and stereo IRs (44.1 and 48 kHz files), blend two IRs, move
HPF/LPF while playing, switch IRs during playback without clicks, save/reopen the project
(including one saved with 0.2.0).

## Milestone 4 — Core effects

Fixed serial chain (spec §5): Gate → Compressor → Boost → Overdrive → Distortion → Fuzz → Wah →
**Amp** → **Cab** → EQ → Chorus → Delay → Reverb. Every effect: on/off with a 20 ms crossfade
(skipped and reset while off), smoothed parameters, own source file and tests.

| Effect | Implementation | Status |
|---|---|---|
| Noise gate | peak detector, 6 dB hysteresis, 15 ms hold, release = time to −60 dB | ✅ unit-tested |
| Compressor | feed-forward, 6 dB soft knee, attack/release, makeup, parallel mix | ✅ unit-tested (steady-state gain = formula) |
| Boost | clean gain 0–24 dB, Bright shelf | ✅ chain-tested |
| Overdrive / Distortion / Fuzz | shared drive core with three voicings, ADAA anti-aliasing | ✅ unit-tested (harmonics rise with drive; ADAA −14 dB aliasing vs naive) |
| Wah | TPT state-variable band-pass 350 Hz–2.2 kHz, resonance, mix | ✅ unit-tested (resonance follows the pedal) |
| EQ | Bass shelf 120 Hz, sweepable Mid peak, Treble shelf 3.5 kHz, Level | ✅ unit-tested (band gains exact within 2 %) |
| Chorus | stereo, cubic-interpolated modulated delays | ✅ unit-tested |
| Delay | Digital / Analog / Ping-Pong, ms or host-synced divisions, tap tempo, feedback filters, glide | ✅ unit-tested (echo timing exact, feedback within 2 %) |
| Reverb | 8-line FDN, Room / Hall / Plate, decay, pre-delay, damping, mix | ✅ unit-tested (RT60 within 10 % of setting, stable at 10 s, click-free type change) |
| Editor pages STOMP / AMP / CAB / POST FX, TAP button | — | 🟡 CI + REAPER check pending |

Moved to Milestone 7 (as planned in the spec): octaver, pitch shifter, parametric/graphic EQ,
phaser, flanger, tremolo, vibrato, rotary, envelope filter, tape/stereo delay, spring reverb,
reorderable routing.

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
