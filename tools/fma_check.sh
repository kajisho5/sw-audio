#!/bin/bash
# Parameter-bounds fuzz test of every product compiled the way arm64 compilers do by default (fused multiply-add contraction): x86 builds hide
# things like sqrt(1 - v * 0.01) = sqrt(-2e-17) = NaN. usage: tools/fma_check.sh [-k 'fuzz parameter bounds: rv01']
cd "$(dirname "$0")/.."
D=build-asan/_deps/doctest-src
[ -d "$D" ] || { echo "run tools/asan.sh once first (it fetches doctest)"; exit 1; }
mkdir -p build-fma
g++ -std=c++17 -O2 -mfma -ffp-contract=fast -Icore/include -Iproducts -Itests -I$D tests/test_main.cpp tests/test_fuzz_bounds.cpp $(ls products/*/*.cpp) -o build-fma/fuzz 2>&1 | grep -E "error" && exit 1
build-fma/fuzz "$@" 2>&1 | tail -15
