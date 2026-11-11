#!/usr/bin/env bash
# Fetches every third-party dependency at its pinned version.
# Works on macOS and on Windows (Git Bash). Usage:
#   Scripts/bootstrap.sh            # submodules + VST3 SDK
#   Scripts/bootstrap.sh validator  # ... and build the Steinberg VST3 validator
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VST3_SDK_TAG="v3.8.1_build_84"   # MIT-licensed VST 3 SDK (see LICENSES.md)
EIGEN_COMMIT="bc3b39870ecb690a623a3f49149a358b95c5781d"  # pinned by NeuralAmpModelerCore
EIGEN_FALLBACK_URL="https://github.com/eigen-mirror/eigen.git"

echo "==> Submodules (iPlug2, NeuralAmpModelerCore, doctest)"
git submodule sync --recursive
git submodule update --init ThirdParty/iPlug2 ThirdParty/NeuralAmpModelerCore ThirdParty/doctest

echo "==> NeuralAmpModelerCore dependencies (AudioDSPTools, Eigen)"
NAM="ThirdParty/NeuralAmpModelerCore"
git -C "$NAM" submodule update --init Dependencies/AudioDSPTools
if ! git -C "$NAM" submodule update --init Dependencies/eigen; then
  # Eigen's home is GitLab. If it is unreachable, fetch the *same commit* from a
  # GitHub mirror; git verifies the commit hash, so the content is identical.
  echo "   GitLab unreachable - fetching Eigen ${EIGEN_COMMIT} from ${EIGEN_FALLBACK_URL}"
  rm -rf "$NAM/Dependencies/eigen"
  git init -q "$NAM/Dependencies/eigen"
  git -C "$NAM/Dependencies/eigen" fetch -q --depth 1 "$EIGEN_FALLBACK_URL" "$EIGEN_COMMIT"
  git -C "$NAM/Dependencies/eigen" checkout -q FETCH_HEAD
fi
test "$(git -C "$NAM/Dependencies/eigen" rev-parse HEAD)" = "$EIGEN_COMMIT" || { echo "Eigen commit mismatch"; exit 1; }

echo "==> VST3 SDK ${VST3_SDK_TAG}"
SDK="ThirdParty/iPlug2/Dependencies/IPlug/VST3_SDK"
if [ -f "$SDK/.ironclad-tag" ] && [ "$(cat "$SDK/.ironclad-tag")" = "$VST3_SDK_TAG" ]; then
  echo "   already present"
else
  rm -rf "$SDK"
  git clone -q --depth 1 --branch "$VST3_SDK_TAG" https://github.com/steinbergmedia/vst3sdk.git "$SDK"
  git -C "$SDK" submodule update -q --init --depth 1 base cmake pluginterfaces public.sdk
  echo "$VST3_SDK_TAG" > "$SDK/.ironclad-tag"
fi

if [ "${1:-}" = "validator" ]; then
  echo "==> Building Steinberg VST3 validator"
  BUILD="$ROOT/build/vst3sdk"
  GEN=(-G Ninja)
  case "${OSTYPE:-}" in
    msys*|cygwin*) command -v cl >/dev/null 2>&1 || GEN=() ;;  # no MSVC env -> default VS generator
  esac
  command -v ninja >/dev/null 2>&1 || GEN=()
  cmake -S "$SDK" -B "$BUILD" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release \
    -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
    -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_CREATE_PLUGIN_LINK=OFF -DSMTG_RUN_VST_VALIDATOR=OFF
  cmake --build "$BUILD" --target validator --config Release
  VALIDATOR="$(find "$BUILD" -type f \( -name validator -o -name validator.exe \) | head -n 1)"
  test -n "$VALIDATOR" || { echo "validator binary not found"; exit 1; }
  mkdir -p "$ROOT/build/tools"
  cp "$VALIDATOR" "$ROOT/build/tools/"
  echo "   -> build/tools/$(basename "$VALIDATOR")"
fi

echo "==> Bootstrap complete"
