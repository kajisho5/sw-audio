#!/bin/bash
# Build, run unit tests and validate every plugin (Linux). Prints one line per plugin.
# needs: clap-validator, vst3-validator (Steinberg validator) on PATH
set -uo pipefail
cd "$(dirname "$0")/.."
B=${1:-build-cmake}
cmake --build "$B" -j2 > /tmp/sw_build.log 2>&1 || { echo "BUILD FAILED"; grep -m10 -E "error" /tmp/sw_build.log; exit 1; }
"$B/sw-tests" > /tmp/sw_tests.log 2>&1; grep "test cases" /tmp/sw_tests.log | sed 's/\[doctest\] /unit /'
grep -E "ERROR" /tmp/sw_tests.log | head -5
for c in "$B"/plugins/*.clap; do
  n=$(basename "$c" .clap)
  # preset-discovery-crawl/-load: left out (clap-validator deadlocks at a container's second preset; tools/clap_note_host.cpp covers them)
  cr=$(NO_COLOR=1 clap-validator validate -x "preset-discovery-(crawl|load)" "$c" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep "tests run" | sed 's/ tests run,/ run/')
  vr=$(vst3-validator "$B/plugins/$n.vst3" 2>&1 | grep "Result:" | tail -1 | sed 's/Result: //')
  printf "%-28s CLAP: %-55s VST3: %s\n" "$n" "$cr" "$vr"
done
