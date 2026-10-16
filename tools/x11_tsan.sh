#!/usr/bin/env bash
# The Linux window under ThreadSanitizer: tools/x11_host_test.cpp (an X11 host) loads a plug-in built with -fsanitize=thread, opens its window under Xvfb and uses it (a drag, the 100% chip, hide / show, destroy).
# The helper process (sw-audio-gui) is built without the sanitizer (GTK and WebKit are not instrumented: it would only report them); the plug-in's side of the conversation (the view's thread, the zoom
# request handed over to the host's main thread) is what is looked at. Needs GTK 3 + WebKitGTK dev packages, Xvfb and xdotool.   usage: tools/x11_tsan.sh
set -u
cd "$(dirname "$0")/.."
B=build-tsan
if [ ! -f $B/build.ninja ]; then
    cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSW_BUILD_TESTS=OFF \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1 -g" \
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread -DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread > /dev/null || exit 2
fi
cmake --build $B --target sw-x11-host sw-eq01-plugin_clap sw-dy08-plugin_clap > $B/x11_build.log 2>&1 || { tail -20 $B/x11_build.log; exit 2; }
mkdir -p $B/plain
W=webkit2gtk-4.1; pkg-config --exists $W || W=webkit2gtk-4.0
g++ -std=c++17 -O1 -Iplugin/clap plugin/clap/gui_x11_helper.cpp $(pkg-config --cflags --libs gtk+-3.0 $W x11) -pthread -o $B/plain/sw-audio-gui || exit 2
export SW_AUDIO_GUI_HELPER="$PWD/$B/plain/sw-audio-gui" WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 TSAN_OPTIONS="halt_on_error=0"
fail=0
for spec in "SW EQ01 Passive|788 20|195 242|--preset 488 20 426 116 537 116" "SW DY08 Clean|831 536|47 436|"; do
    IFS='|' read -r name chip knob extra <<< "$spec"
    log="$B/x11_$(echo "$name" | tr ' ' '_').log"
    xvfb-run -a -s "-screen 0 1600x1000x24" $B/sw-x11-host "$B/plugins/$name.clap" --chip $chip --knob $knob $extra > "$log" 2>&1; rc=$?
    races=$(grep -c "WARNING: ThreadSanitizer" "$log")
    if [ $rc -ne 0 ] || [ "$races" != 0 ]; then echo "FAIL $name (exit $rc, $races TSan report(s)): $log"; grep -A28 -m3 "WARNING: ThreadSanitizer" "$log" | cut -c1-240; grep -E "^(ok|FAIL)" "$log"; fail=1
    else echo "ok   $name: $(grep -c '^ok' "$log") checks, no race"; fi
done
echo "Linux window: $([ $fail = 0 ] && echo 'no race and no failure' || echo 'FAILURES')"
exit $fail
