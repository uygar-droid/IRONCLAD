# IRONCLAD

A guitar amplifier and effects **VST3** for **Windows x64** and **macOS** (Apple Silicon + Intel):
Neural Amp Modeler (`.nam`) capture loading, cabinet impulse responses, a full effects chain and
a photorealistic amplifier interface.

**Status:** Milestone 5 (presets, state, undo, MIDI) in testing — NAM captures, dual-IR cabinet, 11 effects, presets and MIDI learn work; the photorealistic interface (Milestone 6) is not done yet. See [ROADMAP.md](ROADMAP.md).

| Document | Contents |
|---|---|
| [INSTALL.md](INSTALL.md) | Installing the VST3 and loading it in a DAW |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Design, threading/real-time rules, decisions |
| [ROADMAP.md](ROADMAP.md) | Milestones and status |
| [TESTING.md](TESTING.md) | Test strategy, QA checklist, recorded results |
| [DEPENDENCIES.md](DEPENDENCIES.md) | Third-party code, pins, toolchains |
| [LICENSES.md](LICENSES.md) | Licence assessment |
| [CHANGELOG.md](CHANGELOG.md) | Changes per version |

## Getting binaries

Every push to `main` builds both platforms on GitHub Actions. Download
`IRONCLAD-windows-x64` / `IRONCLAD-macos-universal` from the run's *Artifacts*, or from
*Releases* for tagged versions.

## Building from source

Requirements: CMake ≥ 3.25, Ninja, Git; **Windows:** Visual Studio 2022 (Desktop C++) and
Git for Windows; **macOS:** Xcode 16+ (`brew install cmake ninja`).

```bash
git clone https://github.com/uygar-droid/IRONCLAD.git && cd IRONCLAD
Scripts/bootstrap.sh validator          # pinned submodules, VST3 SDK, Steinberg validator
```

macOS:
```bash
cmake --preset macos-ninja-release
cmake --build --preset macos-ninja-release
ctest --preset macos-ninja-release
Scripts/validate.sh build/macos-ninja-release/out/IRONCLAD.vst3
```

Windows (PowerShell):
```powershell
.\Scripts\build-windows.ps1 -Validate
```
