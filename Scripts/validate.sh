#!/usr/bin/env bash
# Runs the Steinberg VST3 validator on a built IRONCLAD.vst3 bundle.
# Usage: Scripts/validate.sh <path/to/IRONCLAD.vst3>
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUNDLE="${1:?path to IRONCLAD.vst3 required}"
VALIDATOR="$ROOT/build/tools/validator"
[ -x "$VALIDATOR" ] || VALIDATOR="$ROOT/build/tools/validator.exe"
[ -x "$VALIDATOR" ] || { echo "Validator not built. Run: Scripts/bootstrap.sh validator"; exit 1; }

echo "==> $VALIDATOR $BUNDLE"
"$VALIDATOR" "$BUNDLE" 2>&1 | tee "$ROOT/build/validator.log"
status=${PIPESTATUS[0]}
echo "==> validator exit code: $status"
exit "$status"
