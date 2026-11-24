#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname -s)" == Darwin ]] || { echo "Run on macOS"; exit 2; }
: "${AE_SDK_ROOT:?Set AE_SDK_ROOT to Adobe SDK Examples directory}"
archs="${GRADATION_ARCHS:-$(uname -m)}"
build="${GRADATION_BUILD_DIR:-build/macos}"
xcrun --find metal
xcrun --find Rez
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES="$archs" \
 -DAE_SDK_ROOT="$AE_SDK_ROOT"
cmake --build "$build" --config Release --parallel 2
ctest --test-dir "$build" -C Release --output-on-failure
bundle=dist/Release/Gradation.plugin
binary="$bundle/Contents/MacOS/Gradation"
test -s "$binary"
test -s "$bundle/Contents/Resources/Gradation.rsrc"
nm -gU "$binary" > "$build/exports.txt"
grep -q ' _EffectMain$' "$build/exports.txt"
grep -q ' _PluginDataEntryFunction2$' "$build/exports.txt"
lipo -info "$binary"
codesign --verify --strict "$bundle"
echo "Build ready for manual AE installation: $bundle"
