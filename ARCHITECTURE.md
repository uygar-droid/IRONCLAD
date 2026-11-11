# IRONCLAD — Architecture

Status: **Approved 2026-11-12; implementation in progress (Milestone 1).** Implementation status
of each part is tracked in `ROADMAP.md`. Every statement below about third-party code was checked
against the actual source repositories cloned on 2026-11-11 (commit hashes in `DEPENDENCIES.md`).

---

## 0. Product scope (binding)

| | Supported | Not supported |
|---|---|---|
| Operating systems | **Windows 10/11 x64**, **macOS 13+** (universal: Apple Silicon + Intel) | Linux (not a target, never a deliverable) |
| Plugin format | **VST3 only** | AU, AUv3, CLAP, AAX, VST2, standalone app |
| Deliverable | **Compiled, installable, working VST3 binaries** for both OSes, validated and tested in a real host (REAPER preferred), plus source, build scripts, docs and an installation guide | Source-only or prototype hand-offs |

Linux may be used **internally** to compile and unit-test platform-independent engine code
during development. Nothing Linux-specific is built, tested in CI, documented for users or shipped.
Native binaries are produced on GitHub-hosted **Windows and macOS runners** (§13).

---

## 1. Framework decision

### 1.1 Candidates evaluated

| Criterion | iPlug2 (upstream) | JUCE 9 |
|---|---|---|
| License | zlib-style permissive (`LICENSE.txt`). Commercial closed-source use allowed, no fee, no revenue limit. | Dual: AGPLv3 **or** commercial JUCE 9 EULA. Closed-source commercial requires a paid tier once revenue exceeds the free Starter limit (USD 20k Starter / 300k Indie / unlimited Pro, per the JUCE 8 EULA which JUCE 9 keeps unchanged). |
| VST3 | Yes | Yes |
| AU (v2) | Yes (`Scripts/cmake/AUv2.cmake`) | Yes |
| AUv3 / CLAP / AAX | Yes / Yes / Yes (AAX needs Avid SDK) | Yes / via extension / Yes |
| Standalone | Yes (Win32 + macOS app, RtAudio/RtMidi) | Yes |
| Linux | **Not an official target** (README: macOS, iOS, Windows, Web) | Yes |
| CMake | Yes — `iPlug2.cmake` + `iplug_add_plugin()`; relatively new upstream | Yes, mature |
| Graphics | IGraphics: NanoVG (GL/Metal) or Skia; bitmap filmstrip knobs (`IBKnobControl`), SVG, high-DPI scale factors | Full component framework, OpenGL, image filmstrips |
| NAM integration | **Proven**: official NAM plugin is iPlug2-based | Possible (community plugins do it), but no official reference |
| Community / docs | Smaller; docs thinner; maintainer-led | Large, well documented |
| Long-term risk | Fewer contributors; we must be comfortable reading framework source | Licence/pricing changes are outside our control (they changed in JUCE 8) |

### 1.2 Decision (pending your approval)

**Use upstream iPlug2 as the plugin/GUI framework, and integrate NeuralAmpModelerCore directly.**

Reasons, in priority order:

1. **Licensing.** iPlug2, NAM Core, NAM Plugin, AudioDSPTools, VST3 SDK (since 3.8) and the
   other proposed dependencies are all permissive (zlib/MIT/BSD-like/MPL-2.0). The whole product
   can be closed-source and commercial with attribution only. JUCE would require AGPL (source
   disclosure) or a paid licence tied to company revenue.
2. **NAM integration is already proven on iPlug2**, including resampling, state chunks and IGraphics UI.
3. **IGraphics fits the visual plan.** Pre-rendered Blender filmstrips + layered bitmaps are exactly
   what `IBKnobControl` / `IBSwitchControl` / `IBitmapControl` are designed for. No real-time 3D needed.
4. iPlug2 covers exactly the two required targets (Windows VST3, macOS VST3). Its lack of
   Linux support is irrelevant because Linux is not a target. The **audio engine is kept
   framework-free** (§2) so DSP can be unit-tested without a plugin host.

Accepted costs: smaller ecosystem than JUCE; iPlug2 CMake support is newer than its Xcode/VS
project files and must be proven in Milestone 1; the plugin itself can only be built on
Windows/macOS machines or CI runners.

### 1.3 Reuse the official NAM plugin — as reference, not as a fork

The spec asks whether extending `NeuralAmpModelerPlugin` is better than starting fresh.
Inspection of `NeuralAmpModeler/NeuralAmpModeler.cpp` (commit `7704e68`) shows a well-made
single-amp plugin, but its structure does not meet IRONCLAD's real-time requirements:

| Finding (file:function) | Why it conflicts with the spec |
|---|---|
| `_ApplyDSPStaging()` runs inside `ProcessBlock` and does `mModel = std::move(mStagedModel)` | The **previous model is destroyed on the audio thread** (deallocation, potentially large). Spec §4 forbids this. |
| `mStagedModel` is a plain `std::unique_ptr` written by `_StageModel()` (UI thread) and read in `ProcessBlock` (audio thread) | Unsynchronised cross-thread access to the same pointer — a data race. |
| `_StageModel()` calls `nam::get_dsp()` + `Reset()` synchronously from the UI message | Large captures block the UI thread while loading. Spec asks for background loading. |
| `_PrepareBuffers()` calls `std::vector::resize` from `ProcessBlock` on every block-size change, and `new[]` (via `_PrepareIOPointers`) on channel-count change | Allocation on the audio thread whenever the block grows or the channel layout changes — common with variable-block hosts. |
| One monolithic `NeuralAmpModeler` class with an index-based parameter enum | Hard to extend to a modular chain of ~40 effects with re-routing. |
| Depends on a **fork** `sdatkinson/iPlug2` (pinned `de5a4fb`) | We want upstream iPlug2 for long-term maintenance. |

So IRONCLAD will **not** fork the plugin repository. It will:

- use **NeuralAmpModelerCore** unmodified as a pinned submodule (the inference engine — spec §6);
- study and, where useful, port pieces of the NAM plugin under its MIT licence with attribution
  (e.g. the resampling-wrapper idea in `ResamplingNAM.h`, model-metadata handling, calibration);
- use **upstream iPlug2**.

This is the decision that most changes the product's shape; it is listed again in §12 for approval.

---

## 2. Layering

```
+--------------------------------------------------------------+
|  Plugin adapter (iPlug2)        Source/Plugin/               |
|  - IronCladPlugin : iplug::Plugin                            |
|  - maps host params <-> engine ParamStore                    |
|  - SerializeState/UnserializeState -> engine StateCodec      |
|  - ProcessBlock -> Engine::process()                         |
+-----------------------------+--------------------------------+
|  GUI (IGraphics)            |   (no DSP, no file parsing in   |
|  Source/GUI/                |    the render path)             |
|  - talks ONLY via params,   |                                 |
|    command queue, meters    |                                 |
+-----------------------------+--------------------------------+
|  ENGINE  (static lib `ironclad_engine`, pure C++20,          |
|           no iPlug2 headers — unit-testable without a host)  |
|  Audio/  Engine, SignalChain, ProcessContext, buffers        |
|  NAM/    AmpSlot (wraps nam::DSP + resampler), loader        |
|  Cabinet/ IR loader, resampler, partitioned convolver, dual  |
|  Effects/ one folder per effect, common IEffect interface    |
|  Parameters/ ParamSpec registry, stable IDs, smoothing       |
|  Presets/ JSON schema, versioning, file-reference resolver   |
|  MIDI/   MIDI-learn map, CC -> param, tap tempo              |
|  Utilities/ SPSC queues, RT-safe swap, denormals, logging    |
+--------------------------------------------------------------+
|  Third party: NeuralAmpModelerCore, Eigen, nlohmann/json,    |
|  AudioDSPTools, PFFFT, r8brain-free-src, (test) doctest      |
+--------------------------------------------------------------+
```

Rules:

- `ironclad_engine` must never include iPlug2 or OS headers. This is enforced by the build:
  the engine target and its test executable have no iPlug2 include paths, so any such include
  fails to compile.
- The GUI never calls engine DSP code. It reads parameter values, sends commands
  (e.g. "load capture at path X") through a queue, and receives meter/status data from
  lock-free FIFOs (iPlug2 `ISender` on the adapter side).
- Each effect is self-contained: DSP + `ParamSpec` list + serialisation + tests + GUI panel.

## 3. Threads and real-time safety

| Thread | Owner | May do | Must never do |
|---|---|---|---|
| **Audio** | host | `Engine::process()`; read smoothed params; pop prepared resources; push retired resources | allocate/free, lock a mutex, file I/O, log, throw, call `nam::get_dsp`, wait |
| **Main/UI** | host | GUI, parameter edits, enqueue load commands, free retired resources | block for long periods (loading is delegated) |
| **Loader worker** | IRONCLAD (one `std::thread`) | parse `.nam`, build `nam::DSP`, `Reset()`/prewarm, read/resample WAV IRs, build FFT partitions, parse presets | touch live audio objects |

### 3.1 Resource hand-over (captures, IRs, and later effect resources)

```
UI: "load X" --cmdQueue--> Loader: build PreparedAmp (allocated, Reset(sr, maxBlock), prewarmed)
                                 |
                                 +--readyQueue (SPSC, lock-free)--> Audio thread, at block start:
                                                                     new = pop(); old = live; live = new;
                                                                     start crossfade old->new (N ms)
                                                                     when fade done: push(old) to retireQueue
Main/Loader <--retireQueue (SPSC, lock-free)-- destroys `old` off the audio thread
```

- Ownership is single-owner raw transfer through the queues (no `shared_ptr` refcount
  decrements on the audio thread). Queues are fixed-capacity, pre-allocated at construction.
- During the crossfade both models run (≈2× amp CPU for ~20–50 ms) so switching is click-free.
- If loading fails, nothing is pushed: **the active capture keeps playing** (spec §6) and the
  error is reported to the UI through a status FIFO.
- `OnReset()` (sample-rate / max-block change) is handled by re-preparing all live resources on
  the loader thread while the adapter outputs silence/bypass until the new set is installed;
  this is the only time audio is intentionally interrupted, and hosts already expect it.

### 3.2 Verifying real-time safety

- A test build replaces global `operator new/delete` with counting versions and sets a
  thread-local "in audio callback" flag; any allocation inside `Engine::process()` fails the test
  (NAM Core already uses the same technique in `tools/test/allocation_tracking.cpp`).
- Engine buffers are sized for `maxBlockSize` at `prepare()`. Hosts that exceed the announced
  block size are handled by internally splitting the block, never by resizing.
- Denormals are flushed (FTZ/DAZ) at the start of every block.

## 4. Signal flow and channels

Initial fixed serial chain (spec §5):

```
in (L/R or mono) -> input select/sum -> INPUT GAIN -> TUNER (analysis only)
 -> GATE -> COMP -> DRIVE slot(s) -> NAM AMP -> CAB IR -> EQ -> MOD -> DELAY -> REVERB -> OUTPUT
 |<------------------------ mono ------------------------>|<-------- stereo -------->|
```

- NAM models are 1-in/1-out (the NAM plugin rejects anything else), so everything up to the
  amp is mono. Stereo begins at the cabinet (stereo IR or dual-IR panning) and post-FX.
- Mono-out hosts receive a mono sum. Supported VST3 bus layouts: mono→mono, mono→stereo,
  stereo→stereo (stereo input is summed or the left channel used, user-selectable).
- Routing is modelled from day one as an ordered list of `ModuleId`s inside `SignalChain`, so
  moving modules later (spec §5 examples) is a data change, not a rewrite. Milestone 1–5 ship
  the fixed order; Milestone 7 exposes reordering. Reordering on the audio thread is done by
  swapping a pre-built routing table, never by rebuilding the graph in the callback.

## 5. NAM engine

- Load: `nam::get_dsp(path, returnedConfig)` on the loader thread, inside `try/catch`.
- Validate: 1 input / 1 output channel; version check (`nam::verify_config_version`);
  file existence/readability; JSON parse errors → user-friendly message + technical detail
  stored for the Advanced panel.
- Metadata: read from the JSON `metadata` object **only if present** (name, modeled_by,
  gear_make/model, tone_type, date, loudness, input/output level dBu) plus architecture and
  `GetExpectedSampleRate()`. Absent fields are shown as "—", never invented.
- Sample rate: if the host rate ≠ model rate and the model doesn't
  `SupportsArbitrarySampleRate()`, wrap in a resampler (AudioDSPTools `ResamplingContainer`, as
  the NAM plugin does). Resampling adds latency, which is reported to the host.
- Gain staging (the only "amp controls" that genuinely exist):
  - **Input** (dB) — drives the capture; optional input calibration from the model's
    `input_level_dbu`.
  - **Output** (dB), with optional loudness normalisation from metadata `loudness`.
  - **Slimmable size**, only for models that support it (`nam::SlimmableModel`).
- **Honesty rule:** tone knobs drawn on the amp face are labelled as what they are (e.g.
  "POST EQ — BASS/MID/TREBLE", implemented as filters after the capture), never as the original
  amplifier's interactive tone stack. Future authentic control requires multi-capture
  interpolation or conditioned architectures (NAM Core has `film.h` / `wavenet_condition_dsp`
  examples worth investigating in Milestone 7).

## 6. Cabinet / IR engine

- WAV loading on the loader thread (AudioDSPTools `wav.cpp`, extended or replaced if it lacks a
  format we need — to be tested: 16/24/32-bit int, 32-bit float, mono/stereo, odd lengths).
- Resample IR to host rate with **r8brain-free-src** (high-quality, MIT) on the loader thread.
- Convolution: AudioDSPTools' `ImpulseResponse` is a direct time-domain FIR (O(N·L) per sample),
  fine for short IRs, too expensive for long ones. IRONCLAD will implement a **uniformly
  partitioned overlap-save convolver** on **PFFFT**, with the first partition done as a direct
  FIR so the cabinet adds **zero latency**. HiFi-LoFi `FFTConvolver` (MIT) is the reference
  implementation and test oracle.
- Max IR length cap (default 500 ms, configurable) with a clear message when truncated.
- Dual IR: two independent slots, each with level, pan, polarity, mute; blend = equal-power
  crossfade; HPF/LPF (biquad, 12 dB/oct to start) after the sum.
- Waveform thumbnail is computed on the loader thread and sent to the GUI as a small array.

## 7. Effects

Common interface (sketch):

```cpp
class IEffect {
public:
  virtual ~IEffect() = default;
  virtual void prepare(const ProcessSpec&) = 0;      // allocate here, never in process
  virtual void reset() = 0;                           // clear state, no allocation
  virtual void process(AudioView& io, const ProcessContext&) noexcept = 0;
  virtual int  latencySamples() const { return 0; }
  virtual int  tailSamples() const { return 0; }
  static  std::span<const ParamSpec> params();       // declared per effect type
};
```

Bypass is implemented by the chain with a short crossfade (no clicks), so every effect gets it
for free; wet/dry and level are per-effect parameters where meaningful. Nonlinear effects
(drive/fuzz) get oversampling as a per-effect option, measured in Milestone 4.

## 8. Parameters

- Each parameter has a **stable string ID** (`"amp.input_db"`, `"delay1.time_ms"`) used in
  presets, and a **stable integer index** used by the host. The integer list is append-only:
  never reordered, never reused; removed parameters become hidden placeholders. A unit test
  compares the list against a committed snapshot (`Tests/param_ids.snapshot`) and fails on any
  change other than appends.
- iPlug2 requires a fixed parameter count at compile time; the registry generates it.
- Every continuous parameter is smoothed per sample or per sub-block (one-pole or linear ramp,
  per-parameter time constant); discrete ones (switches, model selection) crossfade.
- Host automation → `IParam` → engine atomic target; GUI edit → `IParam` → host notified.
  The GUI and host therefore always see the same value (standard iPlug2 behaviour).

## 9. State and presets

- **DAW session state** (`SerializeState`): a header (`"IRCL"`, format version) + UTF-8 JSON
  payload, identical in content to a preset plus UI state. One codec for both, one set of tests.
- **Preset files**: JSON, `"schema": N`, migrations `N→N+1` kept forever and tested.
- External files (captures/IRs) are stored as:
  `{ "name", "relative" (to the user library root), "absolute", "sha256", "size" }`.
  Restore tries absolute → relative → search library by hash; if all fail the slot shows
  "Missing: <name>" and the plugin passes audio safely (bypass that slot). Never a crash.
- Undo/redo: command stack of parameter-change groups, on the UI thread only.

## 10. GUI

- IGraphics, initially NanoVG (Metal on macOS, GL on Windows); Skia is evaluated in
  Milestone 6 for render quality of large bitmaps.
- Assets rendered in Blender at 1× and 2× (and 3× for hero elements if size allows):
  static chassis layers (background, panel, glass, shadows) + **filmstrips** for every
  rotating knob (e.g. 128 frames) and every switch state. Nothing that moves is baked in.
- Custom controls derived from IGraphics base classes add: drag (with fine-adjust modifier),
  double-click reset, value tooltip, right-click text entry, mouse-wheel.
- Window sizes: fixed aspect ratio with 3–4 preset scales; iPlug2 handles DPI scaling.
- Blender is **not installed** in the current environment; asset production happens on a
  workstation. Asset source files (`.blend`) are kept in a separate repository or Git LFS.

## 11. Testing strategy (summary — details in TESTING.md from Milestone 1)

- Engine unit tests (doctest) run natively on the Windows and macOS CI runners (and may be run
  locally on any developer machine).
- Golden-audio tests: render known input through known captures/IRs, compare with tolerance.
  NAM Core's `example_models/*.nam` and `example_audio/input.wav` are used for this.
- Real-time safety: allocation-guard tests (§3.2); stress tests that switch captures every
  block while processing.
- Matrix: 44.1/48/88.2/96/176.4/192 kHz × block sizes {1, 16, 32, 64, 128, 256, 512, 1024, 2048,
  variable/random} × {mono, stereo}.
- Plugin validation: Steinberg `validator` on the VST3 bundle on both OSes; pluginval as a
  second validator; real-host tests in REAPER on Windows and macOS (load, audio, captures, IRs,
  presets, automation, project save/reopen).

## 12. Approved decisions (2026-11-12)

1. **Framework: upstream iPlug2** (not JUCE). §1.2.
2. **Do not fork NeuralAmpModelerPlugin**; build IRONCLAD fresh on NAM Core + iPlug2, porting
   selected MIT code with attribution. §1.3.
3. **C++20.**
4. **Engine as a framework-free library.** §2.
5. **Scope:** VST3 only; Windows x64 + macOS universal only; the final deliverable is working
   binaries (§0).

## 13. Build and delivery pipeline

```
push / manual trigger
  ├─ windows-latest (VS 2022, x64, Release)  → engine tests → NAM Core tests → IRONCLAD.vst3
  │                                           → Steinberg validator → artifact IRONCLAD-win-x64.zip
  └─ macos-latest  (Xcode, arm64+x86_64)     → engine tests → NAM Core tests → IRONCLAD.vst3
                                              → codesign (ad-hoc until a Developer ID exists)
                                              → validator → artifact IRONCLAD-macos-universal.zip
tag vX.Y.Z → same jobs + GitHub Release with both zips + installation guide
```

- macOS jobs cost 10× minutes on private repositories, so they run on `main`, on tags and on
  manual dispatch, not on every branch push.
- Real-host testing (REAPER) cannot run on hosted CI runners in a meaningful way (no audio
  device, no interactive GUI). It is done on real Windows and macOS machines: the user's machine,
  or a machine reached through the Claude desktop app's computer-use bridge. Results are recorded
  in `TESTING.md` with date, OS, host version and build hash.
- Distribution on macOS: binaries downloaded from the internet are quarantined by Gatekeeper.
  Until an Apple Developer ID certificate is available, the installation guide documents the
  one-time `xattr` command; a signed and notarised build requires the certificate (M8).

Open technical questions (no approval needed; resolved by measurement):
- `NAM_SAMPLE` float vs double (CPU vs. bit-exactness with NAM reference) — benchmark in M2.
- NanoVG vs Skia — decided in M6 by visual comparison.
- Crossfade length for capture switching — tuned by listening tests in M2.
