#!/bin/bash
# One-time setup on Linux (Ubuntu / Debian; local or a cloud session):
# build tools, clap-validator 0.4.1, Steinberg VST3 validator (built from the SDK that CMake fetches), doctest.
set -euo pipefail
cd "$(dirname "$0")/.."
SUDO=""; command -v sudo >/dev/null 2>&1 && [ "$(id -u)" != 0 ] && SUDO=sudo
$SUDO apt-get update -q && $SUDO apt-get install -y -q cmake ninja-build g++ git curl unzip python3 python3-pip python3-bs4 >/dev/null
BIN="$HOME/.local/bin"; mkdir -p "$BIN"
if ! command -v clap-validator >/dev/null 2>&1; then
  tmp=$(mktemp -d); curl -sL -o "$tmp/cv.zip" https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip
  (cd "$tmp" && unzip -q cv.zip && for t in *.tar.gz; do [ -f "$t" ] && tar xzf "$t"; done)
  install "$(find "$tmp" -type f -name clap-validator | head -1)" "$BIN/clap-validator"
fi
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
if ! command -v vst3-validator >/dev/null 2>&1; then
  rm -rf /tmp/vst3sdk-v /tmp/vst3b && cp -r build-cmake/cpm/vst3sdk /tmp/vst3sdk-v
  rm -rf /tmp/vst3sdk-v/public.sdk/samples/vst-hosting/{editorhost,audiohost,inspectorapp}  # GUI hosts need GTK
  cmake -S /tmp/vst3sdk-v -B /tmp/vst3b -G Ninja -DCMAKE_BUILD_TYPE=Release -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
        -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF
  cmake --build /tmp/vst3b --target validator
  install /tmp/vst3b/bin/Release/validator "$BIN/vst3-validator"
fi
mkdir -p third_party && [ -f third_party/doctest.h ] || curl -sL -o third_party/doctest.h https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
echo "ready. PATH must include $BIN  (export PATH=\"$BIN:\$PATH\")"
