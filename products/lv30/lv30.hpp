// SW LV30 Recorder — an always-on backup recorder (spec: 仕様書 v1.0「LV30 Recorder」). The sound passes untouched (bit for bit, delay 0, no Auto gain, no Delta). The audio thread only copies the block into a lock-free ring (2^18 frames, 5.5 s at 48 kHz);
//   a writer thread (started by start(), stopped by stop() and the destructor) writes the file, so the audio thread never waits for the disk. When the ring is full the frames are dropped and counted (droppedFrames()).
//   Files: RIFF/WAVE, 16 / 24 bit PCM or 32 bit float, 2 channels (a mono input is written to both), at the host's rate. A 28-byte JUNK chunk after "WAVE" is reserved for the ds64 chunk: the header (sizes) is rewritten once a second, so a crash loses at most the last second
//   and the file stays playable. Over 4 GB (rf64Threshold(), 0xFFFFFFFF - 64 KiB) the header becomes RF64 with a ds64 chunk (64-bit sizes). Split hourly On: a new file after splitSeconds() = 3600 s of audio.
//   Marks: mark(label) (not an audio-thread call; works any time, also while the screen is locked) records the position; the CSV "<file base>_marks.csv" gets "elapsed_seconds,file,label" at once, and when a file is closed the marks inside it are also written as WAV cue points
//   ("cue " + LIST/adtl/labl chunks after the data).
//   **Differences from the spec: FLAC is not written** (selecting it records WAV and flacFallback() is true) **and no sample-rate conversion**: the file has the host's rate (Rate 44.1 / 48 is the wish: rateFallback() is true when they differ).
//   Auto start On starts at prepare() — only when a folder has been chosen (setFolder(): the screen's folder picker; the default "Documents" is not touched until the person has chosen one, so a plain load writes nothing). The folder is saved with the project (saveExtra).
//   Low disk space: the writer checks the free space once a second; under 200 MB it stops with lowDisk() true (the spec's open point: no old files are deleted).
#pragma once
#include "sw/copy_atomic.hpp"
#include "sw/param.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sw::lv30 {

enum ParamId { Container, Rate, Bits, AutoStart, SplitHourly, kNumParams };
enum ContainerId { Wav = 0, Flac = 1 };
enum BitsId { Pcm16 = 0, Pcm24 = 1, Float32 = 2 };

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    ~Processor();
    Processor(const Processor&) = delete; Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete; Processor& operator=(Processor&&) = delete;
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void setFolder(const std::string& path) { std::lock_guard<std::mutex> l(m_); folder_ = path; }
    std::string folder() const { std::lock_guard<std::mutex> l(m_); return folder_; }
    bool start();                 // false: no folder / already running / not prepared
    void stop();                  // writes the last data, the cue points and the final header
    bool recording() const { return running_.load(); }
    int marksMade() const { return marksMade_.load(); }   // marks since this recording started (the screen draws a triangle for each new one)
    double hostRate() const { return fs_; }
    void mark(const std::string& label = "");
    std::string currentFile() const { std::lock_guard<std::mutex> l(m_); return file_; }
    int filesWritten() const { return files_.load(); }
    uint64_t droppedFrames() const { return dropped_.load(); }
    double secondsRecorded() const { return static_cast<double>(framesPushed_.load()) / fs_; }
    bool flacFallback() const { return target_[Container] > 0.5; }
    bool rateFallback() const { const double want = target_[Rate] > 0.5 ? 48000.0 : 44100.0; return std::abs(fs_ - want) > 1.0; }
    bool lowDisk() const { return lowDisk_.load(); }
    void setSplitSeconds(double s) { splitSeconds_ = s; }
    void setRf64Threshold(uint64_t bytes) { rf64At_ = bytes; }
    void saveExtra(std::vector<uint8_t>& out) const;
    void loadExtra(const uint8_t* data, size_t size);

private:
    struct Mark { double elapsed; std::string label; };
    void writer();
    double fs_ = 48000.0;
    bool prepared_ = false;
    std::array<CopyAtomic<double>, kNumParams> target_{};   // written by the host's automation (audio thread), read by start() and the writer thread
    // ring
    std::vector<float> ring_; size_t mask_ = 0;
    std::atomic<size_t> head_{0}, tail_{0};      // frames
    std::atomic<uint64_t> framesPushed_{0}, dropped_{0};
    std::atomic<bool> running_{false}, stopReq_{false}, lowDisk_{false};
    std::atomic<int> files_{0};
    std::atomic<int> marksMade_{0};
    std::thread thread_;
    mutable std::mutex m_;
    std::string folder_, file_;
    std::vector<Mark> marks_;                    // pending marks (the writer takes them)
    double splitSeconds_ = 3600.0; uint64_t rf64At_ = 0xFFFFFFFFull - 65536ull;
    int bitsAtStart_ = 24;
};

}  // namespace sw::lv30
