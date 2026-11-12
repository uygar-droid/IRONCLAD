# IRONCLAD — Dependencies

Last updated: 2026-11-12. iPlug2, NeuralAmpModelerCore and doctest are git submodules pinned
at the commits below; nested NAM dependencies and the VST3 SDK are pinned in `Scripts/bootstrap.sh`.

## 1. Libraries

| Dependency | Inspected commit / version | Source | Used for | Linked into product? |
|---|---|---|---|---|
| NeuralAmpModelerCore | `d3189ed` (2026-11-10), lib version 0.6.0 | github.com/sdatkinson/NeuralAmpModelerCore | NAM inference (`nam::get_dsp`, `nam::DSP`) | Yes |
| Eigen | `bc3b398` (pinned by NAM Core) | gitlab.com/libeigen/eigen | Linear algebra for NAM | Yes (header-only) |
| nlohmann/json | 3.12.0 (vendored in NAM Core) | github.com/nlohmann/json | `.nam` parsing; IRONCLAD presets/state | Yes (header-only) |
| AudioDSPTools | `0827c6c` (pinned by NAM Core) | github.com/sdatkinson/AudioDSPTools | WAV reading, resampling container, IR (reference) | Yes |
| iPlug2 (upstream) | `0cf0403` (2026-11-10) | github.com/iPlug2/iPlug2 | VST3 wrapper, IGraphics GUI | Yes |
| └ WDL, NanoVG, NanoSVG, MetalNanoVG | bundled in iPlug2 | — | iPlug2 internals | Yes |
| └ Skia (optional) | prebuilt via iPlug2 script | — | Alternative renderer (evaluated M6) | Optional |
| VST3 SDK | **v3.8.1_build_84** (fetched by `Scripts/bootstrap.sh`) | github.com/steinbergmedia/vst3sdk | VST3 format + `validator` tool | Yes |
| Apple Cocoa / Metal / CoreText | system (Xcode SDK) | Apple | macOS VST3 GUI | System frameworks |
| doctest | **v2.4.12** (`1da23a3`) | github.com/doctest/doctest | Unit tests | No (tests only) |

Not used: PFFFT, r8brain-free-src and FFTConvolver (planned in Session 1; the FFT, convolver and IR
resampler are IRONCLAD's own code, tested against direct computation — see ARCHITECTURE §6).
Not used: **JUCE** (evaluated: `d60b7a7`, JUCE 9.0.5 — rejected for licensing, see ARCHITECTURE §1).
Not used: the NAM plugin's **iPlug2 fork** (`sdatkinson/iPlug2`); IRONCLAD uses upstream iPlug2.

### Known dependency conflicts / sharp edges

1. **C++ standard.** NAM Core sets `CMAKE_CXX_STANDARD 20`; the NAM plugin builds with
   `stdcpp20` (VS) / `c++20` (Xcode). iPlug2's own code style targets C++17. → IRONCLAD compiles
   everything as **C++20**; M1 must prove iPlug2 compiles cleanly under C++20 with MSVC and
   Apple Clang.
2. **Two AudioDSPTools pins.** NAM Core pins `0827c6c`; the NAM plugin pins `e19ef4b`. Linking
   two copies would cause ODR violations. → IRONCLAD uses **exactly one** copy: the one inside
   the NAM Core submodule.
3. **One nlohmann/json copy.** Use NAM Core's vendored header for the whole project.
4. **Eigen alignment.** NAM Core's README warns about Eigen member alignment with some
   compilers/optimisation levels (NAM Core issue #67). Workaround macros
   `EIGEN_MAX_ALIGN_BYTES 0` / `EIGEN_DONT_VECTORIZE` cost performance; we test without them
   first and keep a CI job with them as a fallback check. Also define `EIGEN_MPL2_ONLY`.
5. **Eigen hosting.** Eigen lives on GitLab. From this Session-1 container GitLab and Google's
   Eigen mirror returned HTTP 403; the exact pinned commit `bc3b398` was obtained from the
   GitHub mirror `eigen-mirror/eigen` (git verifies the commit hash, so content is identical).
   CI and developer machines should use the official URL; the mirror is a documented fallback.
6. **Native builds only.** The VST3 is built on Windows and macOS (CI runners or developer
   machines). The engine and its tests are platform-independent C++.
7. **`NAM_SAMPLE` type.** NAM Core defaults to `double` unless `NAM_SAMPLE_FLOAT` is defined; the
   macro must be identical for every translation unit that includes NAM headers.

## 2. Toolchains

### Windows 10/11 x64 (your primary machine)
- Visual Studio 2022 (17.8+) with "Desktop development with C++" (MSVC v143, Windows 11 SDK)
- CMake ≥ 3.25 (bundled with VS is fine) and Ninja (optional): `winget install Kitware.CMake Ninja-build.Ninja`
- Git for Windows (+ Git LFS): `winget install Git.Git GitHub.GitLFS`
- Python 3 (helper scripts): `winget install Python.Python.3.12`
- A test host: REAPER (free evaluation) is recommended for scripting; any VST3 DAW works.

### macOS (Apple Silicon; Intel via universal binary)
- Xcode 16+ and Command Line Tools
- `brew install cmake ninja git-lfs python`
- For distribution (M8): Apple Developer ID certificate + notarisation credentials.
- A physical Mac is required for REAPER listening tests and Retina UI review.
  GitHub Actions `macos-latest` covers builds, tests and VST3 validation.

### Assets
- Blender 4.x (workstation with a GPU recommended for Cycles renders).
- Git LFS for rendered PNG filmstrips and `.blend` sources.

### Validation tools
- Steinberg `validator` (built from the VST3 SDK by `download-vst3-sdk.sh ... build-validator`).
- pluginval (Tracktion; optional extra validator).

## 3. Development container report (internal only — not a product target)

| Item | Found |
|---|---|
| OS | Ubuntu 24.04.5 LTS, x86_64, 2 vCPU, 7.8 GiB RAM |
| Compilers | GCC 13.3.0, Clang 18.1.3 |
| Build tools | CMake 3.28.3, Ninja 1.11.1, GNU Make 4.3 |
| Git | 2.43.0 (GitHub reachable; GitLab blocked) |
| Python | 3.13 |
| Audio/GUI dev libs | X11, FreeType, GL headers present; ALSA/JACK runtime only |
| Eigen (system) | not installed |
| Blender | **not installed** |
| MSVC / Xcode / Windows or macOS SDKs | **not available** (Linux container) |
| Package installs (pip/apt/npm) | not permitted from this session |

Not done in Session 1, and why:
- **NAM Core was not compiled here.** Running a build of the downloaded third-party code was
  refused by this environment's safety policy during the planning session. Building NAM Core
  and running its `run_tests` is the first concrete task of Milestone 1 (after your approval).
- No VST3 can be built here (no Windows/macOS toolchain). Native binaries come from the
  Windows/macOS GitHub Actions runners.
