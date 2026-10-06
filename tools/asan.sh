#!/bin/bash
# Incremental ASan + UBSan run of all unit tests (build-asan/, plugins off).
set -euo pipefail
cd "$(dirname "$0")/.."
[ -d build-asan ] || cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSW_BUILD_PLUGINS=OFF \
    -DCMAKE_CXX_FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" >/dev/null
cmake --build build-asan -j4 2>&1 | grep -E "error|FAILED" || true
build-asan/sw-tests 2>&1 | grep -E "ERROR|runtime error|test cases|AddressSanitizer" | sed 's/\[doctest\] /asan /'
