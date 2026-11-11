# Changelog

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
