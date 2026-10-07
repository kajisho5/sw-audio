#!/bin/bash
# quick local test build (the canonical build is CMake); fails loudly on compile errors
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
[ -f build/doctest.h ] || cp third_party/doctest.h build/doctest.h
[ -f build/test_main.o ] || g++ -std=c++17 -O2 -Ibuild -c tests/test_main.cpp -o build/test_main.o
python3 tools/embed_ui.py build/gui_assets.hpp
rm -f build/tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Icore/include -Iproducts -Iplugin/clap -Ibuild \
    $(ls tests/test_*.cpp | grep -v test_main) $(ls products/*/*.cpp) build/test_main.o -o build/tests
