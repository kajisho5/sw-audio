#include "lv30/lv30.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace sw::lv30 {
namespace fs = std::filesystem;

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv30.container", "Format",    0, 1, 0, Curve::Step, 1, {0, 1}, "", {"WAV", "FLAC"}},
        {"lv30.rate",      "Rate",      0, 1, 1, Curve::Step, 1, {0, 1}, "", {"44.1 kHz", "48 kHz"}},
        {"lv30.bits",      "Bit depth", 0, 2, 1, Curve::Step, 1, {0, 1, 2}, "", {"16 bit", "24 bit", "32 bit float"}},
        {"lv30.autostart", "Auto start", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"lv30.split",     "Split hourly", 0, 1, 1, Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
    };
    return s;
}

namespace {
void w16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void w32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void w64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void tag(std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(t[i])); }

struct WavFile {
    std::fstream f; std::string path; int bits = 24; bool isFloat = false; uint32_t rate = 48000; uint64_t dataBytes = 0; bool rf64 = false; uint64_t extra = 0, limit = 0xFFFFFFFFull - 65536ull;
    static constexpr std::streamoff kDataStart = 12 + 8 + 28 + 8 + 16 + 8;   // RIFF header, JUNK, fmt, data header
    bool open(const std::string& p, int b, bool fl, uint32_t r) {
        path = p; bits = b; isFloat = fl; rate = r; dataBytes = 0; rf64 = false; extra = 0;
        f.open(p, std::ios::binary | std::ios::out | std::ios::in | std::ios::trunc); if (!f) return false;
        writeHeader(); return f.good();
    }
    void writeHeader() {
        std::vector<uint8_t> h; const uint64_t riffSize = kDataStart - 8 + dataBytes + extra; rf64 = riffSize > limit;
        tag(h, rf64 ? "RF64" : "RIFF"); w32(h, rf64 ? 0xFFFFFFFFu : static_cast<uint32_t>(riffSize)); tag(h, "WAVE");
        if (rf64) { tag(h, "ds64"); w32(h, 28); w64(h, riffSize); w64(h, dataBytes); w64(h, dataBytes / (static_cast<uint64_t>(bits / 8) * 2)); w32(h, 0); }
        else { tag(h, "JUNK"); w32(h, 28); for (int i = 0; i < 28; ++i) h.push_back(0); }
        tag(h, "fmt "); w32(h, 16); w16(h, isFloat ? 3 : 1); w16(h, 2); w32(h, rate); w32(h, rate * 2 * static_cast<uint32_t>(bits / 8)); w16(h, static_cast<uint16_t>(2 * (bits / 8))); w16(h, static_cast<uint16_t>(bits));
        tag(h, "data"); w32(h, rf64 ? 0xFFFFFFFFu : static_cast<uint32_t>(std::min<uint64_t>(dataBytes, 0xFFFFFFFFull)));
        f.seekp(0); f.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size())); f.seekp(0, std::ios::end); f.flush();
    }
    void write(const std::vector<uint8_t>& b) { f.seekp(0, std::ios::end); f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size())); dataBytes += b.size(); }
    // cue points + labels after the data, then the final header
    void close(const std::vector<std::pair<uint64_t, std::string>>& cues) {
        if (!f.is_open()) return;
        f.seekp(0, std::ios::end);
        if (dataBytes & 1) { const char z = 0; f.write(&z, 1); extra += 1; }
        if (!cues.empty()) {
            std::vector<uint8_t> c; tag(c, "cue "); w32(c, static_cast<uint32_t>(4 + 24 * cues.size())); w32(c, static_cast<uint32_t>(cues.size()));
            for (size_t i = 0; i < cues.size(); ++i) { w32(c, static_cast<uint32_t>(i + 1)); w32(c, static_cast<uint32_t>(cues[i].first)); tag(c, "data"); w32(c, 0); w32(c, 0); w32(c, static_cast<uint32_t>(cues[i].first)); }
            std::vector<uint8_t> list; tag(list, "adtl");
            for (size_t i = 0; i < cues.size(); ++i) { const std::string t = cues[i].second.empty() ? "Mark " + std::to_string(i + 1) : cues[i].second; const uint32_t len = static_cast<uint32_t>(4 + t.size() + 1); tag(list, "labl"); w32(list, len); w32(list, static_cast<uint32_t>(i + 1)); list.insert(list.end(), t.begin(), t.end()); list.push_back(0); if (len & 1) list.push_back(0); }
            tag(c, "LIST"); w32(c, static_cast<uint32_t>(list.size())); c.insert(c.end(), list.begin(), list.end());
            f.write(reinterpret_cast<const char*>(c.data()), static_cast<std::streamsize>(c.size())); extra += c.size();
        }
        writeHeader(); f.close();
    }
};
std::string stamp() { const std::time_t t = std::time(nullptr); std::tm tm{}; 
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y%m%d_%H%M%S", &tm); return b; }
}  // namespace

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }
Processor::~Processor() { stop(); }

void Processor::prepare(double sampleRate, int) {
    stop();
    fs_ = sampleRate; size_t sz = 1u << 18; ring_.assign(sz * 2, 0.0f); mask_ = sz - 1; head_ = tail_ = 0; framesPushed_ = 0; dropped_ = 0; lowDisk_ = false; files_ = 0; prepared_ = true;
    if (target_[AutoStart] > 0.5 && !folder().empty()) start();
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0 || !running_.load(std::memory_order_relaxed)) return;
    size_t h = head_.load(std::memory_order_relaxed); const size_t t = tail_.load(std::memory_order_acquire), cap = mask_ + 1;
    if (cap - (h - t) < static_cast<size_t>(n)) { dropped_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed); return; }
    for (int i = 0; i < n; ++i) { const size_t k = (h + static_cast<size_t>(i)) & mask_; ring_[2 * k] = ch[0][i]; ring_[2 * k + 1] = numCh > 1 ? ch[1][i] : ch[0][i]; }
    head_.store(h + static_cast<size_t>(n), std::memory_order_release); framesPushed_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
}

bool Processor::start() {
    if (!prepared_ || running_.load()) return false;
    { std::lock_guard<std::mutex> l(m_); if (folder_.empty()) return false; std::error_code ec; fs::create_directories(folder_, ec); if (ec) return false; marks_.clear(); }
    head_ = tail_ = 0; framesPushed_ = 0; stopReq_ = false; bitsAtStart_ = target_[Bits] < 0.5 ? 16 : target_[Bits] < 1.5 ? 24 : 32;
    running_ = true; thread_ = std::thread([this] { writer(); }); return true;
}
void Processor::stop() {
    if (!running_.load() && !thread_.joinable()) return;
    stopReq_ = true; if (thread_.joinable()) thread_.join(); running_ = false;
}
void Processor::mark(const std::string& label) { if (!running_.load()) return; std::lock_guard<std::mutex> l(m_); marks_.push_back({static_cast<double>(framesPushed_.load()) / fs_, label}); }

void Processor::writer() {
    WavFile wav; std::string base; int fileIndex = 0; uint64_t fileFrames = 0, totalFrames = 0; std::vector<std::pair<uint64_t, std::string>> cues; std::vector<uint8_t> bytes;
    const std::string stampStr = stamp(); const bool isFloat = bitsAtStart_ == 32; auto lastHeader = std::chrono::steady_clock::now(), lastDisk = lastHeader;
    std::string folderCopy; { std::lock_guard<std::mutex> l(m_); folderCopy = folder_; }
    std::string csvPath = (fs::path(folderCopy) / ("SW_" + stampStr + "_marks.csv")).string();
    auto openNext = [&]() -> bool {
        ++fileIndex; char name[96]; std::snprintf(name, sizeof name, "SW_%s_%03d.wav", stampStr.c_str(), fileIndex);
        const std::string p = (fs::path(folderCopy) / name).string(); fileFrames = 0; cues.clear();
        wav.limit = rf64At_; if (!wav.open(p, bitsAtStart_, isFloat, static_cast<uint32_t>(std::lround(fs_)))) return false;
        { std::lock_guard<std::mutex> l(m_); file_ = p; } ++files_; return true;
    };
    auto closeFile = [&]() { wav.close(cues); };
    bool ok = openNext();
    auto drain = [&]() {
        size_t t = tail_.load(std::memory_order_relaxed); const size_t h = head_.load(std::memory_order_acquire);
        while (ok && t != h) {
            size_t frames = std::min<size_t>(h - t, 4096);
            const bool split = target_[SplitHourly] > 0.5; if (split) { const uint64_t limit = static_cast<uint64_t>(splitSeconds_ * fs_); if (fileFrames >= limit) { closeFile(); ok = openNext(); if (!ok) break; } else frames = std::min<size_t>(frames, static_cast<size_t>(limit - fileFrames)); }
            bytes.clear(); const int bps = bitsAtStart_ / 8; bytes.reserve(frames * 2 * static_cast<size_t>(bps));
            for (size_t i = 0; i < frames; ++i) for (int c = 0; c < 2; ++c) {
                const float x = std::clamp(ring_[2 * ((t + i) & mask_) + static_cast<size_t>(c)], -1.0f, 1.0f);
                if (isFloat) { uint32_t u; std::memcpy(&u, &x, 4); w32(bytes, u); }
                else if (bps == 2) { const int v = static_cast<int>(std::lround(x * 32767.0f)); w16(bytes, static_cast<uint16_t>(static_cast<int16_t>(v))); }
                else { const int v = static_cast<int>(std::lround(static_cast<double>(x) * 8388607.0)); bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); bytes.push_back(static_cast<uint8_t>(v >> 16)); }
            }
            const uint64_t fileStart = totalFrames - fileFrames; wav.write(bytes);
            { std::lock_guard<std::mutex> l(m_);   // the marks that fall inside what has been written: cue points in this file, lines in the CSV
              for (auto it = marks_.begin(); it != marks_.end();) {
                  const double at = it->elapsed * fs_;
                  if (at <= static_cast<double>(totalFrames + frames)) {
                      const uint64_t pos = at > static_cast<double>(fileStart) ? static_cast<uint64_t>(at) - fileStart : 0; cues.push_back({std::min<uint64_t>(pos, fileFrames + frames), it->label});
                      const bool fresh = !fs::exists(csvPath); std::ofstream csv(csvPath, std::ios::app | std::ios::binary);   // binary: one \n per line on every OS
                      if (csv) { if (fresh) csv << "elapsed_seconds,file,label\n"; char num[40]; std::snprintf(num, sizeof num, "%.3f", it->elapsed); csv << num << "," << fs::path(wav.path).filename().string() << "," << it->label << "\n"; }
                      it = marks_.erase(it);
                  } else ++it;
              } }
            fileFrames += frames; totalFrames += frames; t += frames; tail_.store(t, std::memory_order_release);
        }
    };
    while (!stopReq_.load()) {
        drain();
        const auto now = std::chrono::steady_clock::now();
        if (ok && now - lastHeader >= std::chrono::seconds(1)) { wav.writeHeader(); lastHeader = now; }
        if (ok && now - lastDisk >= std::chrono::seconds(1)) { lastDisk = now; std::error_code ec; const auto sp = fs::space(folderCopy, ec); if (!ec && sp.available < 200ull * 1024 * 1024) { lowDisk_ = true; break; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    drain();
    { std::lock_guard<std::mutex> l(m_); for (auto& m : marks_) cues.push_back({fileFrames, m.label}); marks_.clear(); }   // marks that arrived after the last data: at the end
    if (wav.f.is_open()) closeFile();
    running_ = false;
}

void Processor::saveExtra(std::vector<uint8_t>& out) const { const std::string f = folder(); const size_t len = std::min<size_t>(1000, f.size()); out.push_back(static_cast<uint8_t>(len & 0xFF)); out.push_back(static_cast<uint8_t>(len >> 8)); out.insert(out.end(), f.begin(), f.begin() + static_cast<long>(len)); }
// the folder comes from a project file, which may come from someone else: only an absolute path without ".." pieces or control characters
// is taken (a relative path would land wherever the host happens to run; ".." could step out of a folder the person recognises)
bool Processor::acceptableFolder(const std::string& s) {
    if (s.empty() || s.size() > 1000) return false;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return false;
    const fs::path p = fs::u8path(s);
    if (!p.is_absolute()) return false;
    for (const auto& part : p) if (part == "..") return false;
    return true;
}
void Processor::loadExtra(const uint8_t* d, size_t size) {
    if (size < 2) return;
    const size_t len = static_cast<size_t>(d[0]) | (static_cast<size_t>(d[1]) << 8);
    if (2 + len > size || len > 1000) return;
    std::string f(reinterpret_cast<const char*>(d + 2), len);
    if (acceptableFolder(f)) setFolder(f);
}

}  // namespace sw::lv30
