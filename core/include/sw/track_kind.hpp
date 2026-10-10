// SW AUDIO core — what kind of track a name is (the host gives the track's name: CLAP track-info). The kinds are UT01's (the numbers are saved in its state: do not renumber).
// Used by UT01 (a Gain per kind) and by SW Link (every instance tells the others what kind of track it is on).
#pragma once
#include <cctype>
#include <initializer_list>
#include <string>

namespace sw {

enum TrackKindId { kTrackVocal = 0, kTrackDrums, kTrackBass, kTrackGuitar, kTrackKeys, kTrackBus, kTrackOther, kTrackKinds, kTrackUnknown = 255 };

// keywords in English and Japanese, the first group that matches wins
inline int classifyTrackName(const std::string& name) {
    std::string n; for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](std::initializer_list<const char*> keys) { for (const char* k : keys) if (n.find(k) != std::string::npos) return true; return false; };
    if (has({"vox", "vocal", "voice", "lead v", "bv", "ボーカル", "歌", "コーラス"})) return kTrackVocal;
    if (has({"drum", "kick", "snare", "hat", "tom", "cymbal", "perc", "ドラム", "キック", "スネア"})) return kTrackDrums;
    if (has({"bass", "ベース"})) return kTrackBass;
    if (has({"guitar", "gtr", "ギター"})) return kTrackGuitar;
    if (has({"key", "piano", "synth", "organ", "pad", "keys", "ピアノ", "シンセ", "キー"})) return kTrackKeys;
    if (has({"bus", "master", "mix", "group", "stem", "バス", "マスター"})) return kTrackBus;
    return kTrackOther;
}

}  // namespace sw
