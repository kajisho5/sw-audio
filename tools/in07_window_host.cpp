// SWINGBY: the plug-in window's native side without a platform view, for a browser check of the real page against the real plug-in
// (tools/in07_window_check.py): writes the page the window would load, then reads the page's messages (one a line on stdin) and answers
// each with the script the window would evaluate (one JSON string a line on stdout). A block of audio is processed after every message,
// as a host's audio thread would. The user's folders: whatever XDG_DATA_HOME says (the check points it at a temporary folder).
//   build: see tools/in07_window_check.py (it compiles this with the CMake build's include folders)
#include "in07_traits.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
const void* noExt(const clap_host_t*, const char*) { return nullptr; }
void noop(const clap_host_t*) {}
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "window-check", "SW", "", "1", noExt, noop, noop, noop};
bool push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
uint32_t none(const clap_input_events_t*) { return 0; }
const clap_event_header_t* get(const clap_input_events_t*, uint32_t) { return nullptr; }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: in07_window_host <page.html>\n"); return 2; }
    sw::clapinst::Plugin<sw::in07clap::In07> p(&kHost);
    p.clap()->activate(p.clap(), 48000, 32, 256);
    { std::ofstream f(argv[1], std::ios::binary); f << p.guiPage(); }
    std::vector<float> l(256), r(256);
    float* ch[2] = {l.data(), r.data()};
    clap_audio_buffer_t ob{}; ob.data32 = ch; ob.channel_count = 2;
    clap_output_events_t out{nullptr, push};
    clap_input_events_t in{nullptr, none, get};
    clap_process_t pr{}; pr.frames_count = 256; pr.audio_outputs = &ob; pr.audio_outputs_count = 1; pr.in_events = &in; pr.out_events = &out; pr.steady_time = -1;
    std::string line;
    std::cout << "ready" << std::endl;
    while (std::getline(std::cin, line)) {
        const std::string s = p.guiMessage(line);
        for (int k = 0; k < 4; ++k) p.clap()->process(p.clap(), &pr);
        std::cout << sw::instgui::jsonString(s) << std::endl;
    }
    p.clap()->deactivate(p.clap());
    return 0;
}
