# Changelog

## [Unreleased] — 0.5.0 (Milestone 5: presets, state, undo, MIDI)

### Added
- Preset bar at the top: previous / next, preset browser (Favourites, Factory templates,
  My presets by category), FAV, SAVE, … (Save As, Rename, Duplicate, Delete, Init, show
  folder), UNDO, REDO, MIDI. `*` marks unsaved changes.
- User presets are `.ironclad` JSON files in `~/Library/Application Support/IRONCLAD/Presets`
  (macOS) or `%APPDATA%\IRONCLAD\Presets` (Windows); a sub-folder is a category. Type
  `Category / Name` in Save As to file a preset in a category.
- 12 factory templates (Clean, Crunch, Lead, High Gain, Ambient). They set effects and levels
  only, so your own capture and IRs stay loaded.
- Presets remember captures and IRs with their content hash: a file that was moved or renamed
  is found again (next to the preset, or in IRONCLAD's Captures / IRs / Presets folders, or in
  the folder last browsed). A file that cannot be found is reported; loading never fails.
- Click-free preset changes (short fade-out / fade-in).
- Undo / redo for knob and switch changes, capture / IR loads and preset loads.
- MIDI learn (MIDI button): any knob or switch, momentary-footswitch toggle mode, Tap Tempo,
  Next / Previous preset; any MIDI channel. Assignments are saved with the project and can be
  saved as the default for new instances.
- Volume Pedal and Input Calibration (Interface Level, dBu) on the AMP page.

### Changed
- Project state format v3 (JSON). Projects saved with 0.1–0.4 still open.
- The plugin now has a MIDI input (needed for MIDI learn).

### Not possible
- MIDI Program Change cannot select presets: VST3 does not deliver Program Change to plugins
  as MIDI. Use a CC assigned to Next / Previous preset instead.

## [Unreleased] — 0.4.1 (performance)

### Changed
- Cabinet convolution is now non-uniformly partitioned (64 / 512 / 4096) and still has zero
  latency. Long IRs are much cheaper: two 1 s stereo IRs went from 57.9 % to 5.5 % of one core
  (benchmark, 48 kHz / 128). The large partitions' work is spread over time, so the slowest
  64-sample block with those IRs went from 521 % to 69 % of the block duration (no CPU bursts).
- Added `ironclad_bench` (stage-by-stage CPU benchmark with worst-block measurement).

### Measured, unchanged
- A standard WaveNet capture costs ~27 % of one core of the 2.1 GHz Xeon used for the
  benchmark — the cost of NeuralAmpModelerCore itself, identical to the official NAM plugin
  (both compute in double at the interface; switching NAM to float made no difference).
  All 11 effects together cost ~4 %.

## [Unreleased] — 0.4.0 (Milestone 4: core effects)

### Added
- STOMP: Noise Gate, Compressor, Boost, Overdrive, Distortion, Fuzz, Wah (before the amp).
- POST FX: 3-band EQ, Chorus, Delay (Digital / Analog / Ping-Pong, host tempo sync with note
  divisions, TAP button), Reverb (Room / Hall / Plate) — after the cabinet.
- Editor pages STOMP / AMP / CAB / POST FX (temporary functional design until Milestone 6).
- 54 automatable parameters (stable IDs).

### Fixed (found by the new tests before release)
- Noise gate Release now means "time to close" (−60 dB), not a time constant.
- Fuzz amount had almost no effect (range started fully saturated).
- Plate reverb decayed ~15 % longer than set at short decay times; decay calibrated.

## [Unreleased] — 0.3.0 (Milestone 3: cabinet)

### Added
- Two cabinet impulse-response slots (A/B) loading WAV files (16/24/32-bit PCM, 32/64-bit float,
  mono or stereo, any sample rate — resampled to the project rate).
- Zero-latency partitioned convolution; IRs up to 1 s (longer files are truncated and flagged).
- Per slot: Level, Pan, Invert, On. Global: A/B Blend, Cab HPF, Cab LPF, Cab On.
- IRs are normalised to unit energy so different IR files play at comparable loudness.
- IR switching during playback is crossfaded (no clicks); errors keep the current IR.
- Waveform thumbnail and file information for each slot.
- The plugin output is now true stereo after the cabinet.
- Project state v2 stores both IR paths (0.1.x and 0.2.x projects still open).

## [Unreleased] — 0.2.0 (Milestone 2: NAM integration)

### Added
- Load Neural Amp Modeler `.nam` captures (NeuralAmpModelerCore) on a background thread;
  switch captures during playback with a 25 ms equal-power crossfade.
- Automatic resampling when the host rate differs from the capture's rate; plugin latency is
  reported to the host.
- Capture metadata display; clear error messages for missing, damaged, wrong-type and
  unsupported files (the current capture keeps playing).
- Parameters: Amp On, Amp Input, Amp Output, Normalize (−18 dB loudness target).
- Project state now stores the capture path (format "IRCL" v1). Projects saved with 0.1.x
  still open.

### Changed
- Engine source folder for the amp stage is `Source/Amp/` (avoids clashing with NAM Core's
  `NAM/` include path).

## [Unreleased] — 0.1.0 (Milestone 1: foundation)

### Added
- Scope fixed: VST3 only, Windows x64 + macOS universal; deliverable is working binaries.
- Pinned submodules: iPlug2 `0cf0403`, NeuralAmpModelerCore `d3189ed` (+ Eigen `bc3b398`,
  AudioDSPTools `0827c6c`), doctest v2.4.12. VST3 SDK v3.8.1 fetched by `Scripts/bootstrap.sh`.
- CMake (C++20) with presets for Windows (Ninja/VS2022) and macOS (Ninja/Xcode, universal).
- `ironclad_nam`: NeuralAmpModelerCore built as a library, linked whole-archive so model
  architectures register correctly.
- `ironclad_engine` (framework-free): `Engine` (input→mono→input gain→[chain]→output gain),
  `SmoothedValue`, `SpscQueue`, `ScopedNoDenormals` (x86-64 + ARM64), parameter table with
  stable IDs and compile-time integrity checks.
- IRONCLAD VST3 shell (iPlug2): parameters generated from the engine table, temporary editor.
- Unit tests incl. allocation guard and NAM smoke tests; parameter-ID snapshot.
- Scripts: `bootstrap.sh`, `validate.sh`, `build-windows.ps1`.
- GitHub Actions: Windows x64 + macOS universal build, tests, Steinberg validator, zipped artifacts,
  release upload on tags.
- Docs: INSTALL, README, TESTING; ARCHITECTURE/ROADMAP/DEPENDENCIES/LICENSES updated for the
  VST3-only, Windows/macOS-only scope.
