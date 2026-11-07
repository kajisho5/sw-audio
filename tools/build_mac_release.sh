#!/usr/bin/env bash
# The macOS release build for runway's mac-notarize (Developer ID signature + Apple notarization): Universal (Apple silicon + Intel), CLAP + VST3 + AU,
# one part of the products (every <shards>th sw_add_plugin line from <shard>), so that one part stays well inside runway's 90-minute job.
#   usage: tools/build_mac_release.sh <shard> <shards>      e.g. tools/build_mac_release.sh 0 4      -> build/plugins/*.clap *.vst3 *.component
set -euo pipefail
cd "$(dirname "$0")/.."
SHARD=${1:-0}; SHARDS=${2:-1}
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSW_BUILD_TESTS=OFF \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
T=""; i=0
for c in $(grep -o 'sw_add_plugin([a-z][a-z0-9]*' CMakeLists.txt | sed 's/sw_add_plugin(//'); do
    if [ $((i % SHARDS)) = "$SHARD" ]; then T="$T sw-$c-plugin_all"; fi
    i=$((i + 1))
done
echo "part $((SHARD + 1)) of $SHARDS:$T"
cmake --build build --config Release --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" --target $T
# every binary must hold both architectures
bad=0
for b in build/plugins/*.clap build/plugins/*.vst3 build/plugins/*.component; do
    exe="$b/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$b/Contents/Info.plist")"
    archs=$(lipo -archs "$exe")
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "::error::not universal ($archs): $b"; bad=1 ;; esac
done
[ $bad = 0 ] && echo "all bundles are universal (arm64 + x86_64)"
exit $bad
