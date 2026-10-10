// SWINGBY (SW IN07): the demo songs, every sound a SWINGBY instance (the kit: tools/in07_songkit.hpp; the songs: tools/songs/).
//   g++ -std=c++17 -O2 -Icore/include -Iproducts -Itools tools/in07_song.cpp products/in07/*.cpp products/ms01/ms01.cpp products/ms04/ms04.cpp
//       core/src/*.cpp core/third_party/monocypher/*.c -o build/in07_song
//   build/in07_song <song> <out.wav> [the loud sections' loudness, LUFS: the song's own when left out]     (run from the repository's root)
//   SW_SONG_STEMS=<dir>: each track as a WAV too; SW_SONG_EVENTS=<file.json>: every track's values and events (tools/in07_song_video.py)
#include "in07_songkit.hpp"
#include "songs/slingshot.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    using sw::in07::songkit::Song;
    const std::vector<std::pair<const char*, std::function<Song()>>> songs = {
        {"slingshot", sw::in07::songs::slingshot},
    };
    if (argc < 3) {
        std::fprintf(stderr, "usage: in07_song <song> <out.wav> [target LUFS]\nsongs:");
        for (const auto& s : songs) std::fprintf(stderr, " %s", s.first);
        std::fprintf(stderr, "\n");
        return 2;
    }
    for (const auto& s : songs)
        if (std::strcmp(s.first, argv[1]) == 0) {
            Song song = s.second();
            const double target = argc > 3 ? std::atof(argv[3]) : song.target;
            std::printf("%s (%s, %.0f bpm, %d bars)\n", song.title.c_str(), song.style.c_str(), song.bpm, song.bars);
            return sw::in07::songkit::make(song, argv[2], target);
        }
    std::fprintf(stderr, "no song %s\n", argv[1]);
    return 2;
}
