#!/usr/bin/env bash
# Data-race check with ThreadSanitizer: builds the plug-ins (CLAP only) and tools/host_stress.cpp with -fsanitize=thread in build-tsan and runs the audio thread and the
# window's thread against each plug-in at the same time. A race makes TSan print a report and the run exit non-zero.
#   usage: tools/stress_tsan.sh [seconds per plug-in = 3] [product codes ... = all]      e.g. tools/stress_tsan.sh 5 ut03 rv04
set -u
cd "$(dirname "$0")/.."
SECS=${1:-3}; shift || true
B=build-tsan
if [ ! -f $B/build.ninja ]; then
    cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSW_BUILD_TESTS=OFF \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1 -g" \
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread -DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread > /dev/null || exit 2
fi
if [ $# -gt 0 ]; then CODES="$*"; else CODES=$(grep -o 'sw_add_plugin([a-z][a-z0-9]*' CMakeLists.txt | sed 's/sw_add_plugin(//'); fi
T="sw-host-stress"; for c in $CODES; do T="$T sw-$c-plugin_clap"; done
cmake --build $B --target $T -j"$(nproc)" > $B/stress_build.log 2>&1 || { tail -20 $B/stress_build.log; exit 2; }
fail=0; n=0
for c in $CODES; do
    f=$(ls $B/plugins | grep -i "^SW $c " | grep '\.clap$' | head -1)
    [ -z "$f" ] && { echo "no plug-in for $c"; fail=1; continue; }
    log=$B/stress_$c.log
    TSAN_OPTIONS="halt_on_error=0" timeout $((SECS * 20 + 60)) $B/sw-host-stress "$B/plugins/$f" "$SECS" > "$log" 2>&1; rc=$?
    races=$(grep -c "WARNING: ThreadSanitizer" "$log")
    if [ $rc -ne 0 ] || [ "$races" != 0 ]; then echo "FAIL $c (exit $rc, $races TSan report(s)): $log"; fail=1; else tail -1 "$log"; fi
    n=$((n + 1))
done
echo "$n plug-ins, $([ $fail = 0 ] && echo 'no race and no failure' || echo 'FAILURES')"
exit $fail
