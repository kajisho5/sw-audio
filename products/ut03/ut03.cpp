#include "ut03/ut03.hpp"
#include "sw/base64.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sw::ut03 {
namespace {
constexpr double kPi = 3.14159265358979323846;
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// one sample of `bytes` bytes starting at p, as -1..1
double pcmSample(const uint8_t* p, int bytes, bool bigEndian, bool isFloat) {
    uint8_t b[8]; for (int i = 0; i < bytes; ++i) b[i] = bigEndian ? p[bytes - 1 - i] : p[i];   // b is little endian now
    if (isFloat) {
        if (bytes == 4) { float f; std::memcpy(&f, b, 4); return f; }
        double d; std::memcpy(&d, b, 8); return d;
    }
    if (bytes == 1) return bigEndian ? (static_cast<int>(static_cast<int8_t>(b[0]))) / 128.0 : (static_cast<int>(b[0]) - 128) / 128.0;   // WAV 8 bit is unsigned, AIFF signed
    uint32_t u = 0; for (int i = 0; i < bytes; ++i) u |= static_cast<uint32_t>(b[i]) << (8 * i);
    const int sh = 32 - 8 * bytes; const int32_t v = static_cast<int32_t>(u << sh) >> sh;   // sign-extend from `bytes` bytes
    return v / std::ldexp(1.0, 8 * bytes - 1);
}

double aiffRate(const uint8_t* p) {   // 80-bit extended
    const int e = ((p[0] & 0x7F) << 8) | p[1];
    const uint64_t m = (uint64_t(be32(p + 2)) << 32) | be32(p + 6);
    if (e == 0 && m == 0) return 0;
    return std::ldexp(static_cast<double>(m), e - 16383 - 63);
}
}  // namespace

bool decodeAudio(const uint8_t* d, size_t size, Decoded& out) {
    out = Decoded{};
    if (!d || size < 12) return false;
    int nch = 0, bytes = 0; bool isFloat = false, big = false; double rate = 0; const uint8_t* pcm = nullptr; size_t pcmBytes = 0;
    if (!std::memcmp(d, "RIFF", 4) && !std::memcmp(d + 8, "WAVE", 4)) {
        int tag = 0, bits = 0; size_t pos = 12;
        while (pos + 8 <= size) {
            const uint32_t len = le32(d + pos + 4); const uint8_t* body = d + pos + 8; const size_t avail = size - (pos + 8);
            if (!std::memcmp(d + pos, "fmt ", 4) && len >= 16 && avail >= 16) {
                tag = le16(body); nch = le16(body + 2); rate = le32(body + 4); bits = le16(body + 14);
                if (tag == 0xFFFE && len >= 26 && avail >= 26) tag = le16(body + 24);   // extensible: sub-format GUID starts with the tag
            } else if (!std::memcmp(d + pos, "data", 4)) { pcm = body; pcmBytes = std::min<size_t>(len, avail); break; }
            pos += 8 + static_cast<size_t>(len) + (len & 1);
        }
        if (!pcm) return false;
        if (tag != 1 && tag != 3) return false;
        isFloat = tag == 3; bytes = bits / 8;
        if (bits % 8 != 0 || (isFloat ? !(bytes == 4 || bytes == 8) : !(bytes >= 1 && bytes <= 4))) return false;
    } else if (!std::memcmp(d, "FORM", 4) && (!std::memcmp(d + 8, "AIFF", 4) || !std::memcmp(d + 8, "AIFC", 4))) {
        const bool aifc = !std::memcmp(d + 8, "AIFC", 4); size_t pos = 12; int bits = 0; bool little = false;
        while (pos + 8 <= size) {
            const uint32_t len = be32(d + pos + 4); const uint8_t* body = d + pos + 8; const size_t avail = size - (pos + 8);
            if (!std::memcmp(d + pos, "COMM", 4) && len >= 18 && avail >= 18) {
                nch = be16(body); bits = be16(body + 6); rate = aiffRate(body + 8);
                if (aifc) {
                    if (len < 22 || avail < 22) return false;
                    if (!std::memcmp(body + 18, "sowt", 4)) little = true;
                    else if (!std::memcmp(body + 18, "fl32", 4)) { isFloat = true; }
                    else if (std::memcmp(body + 18, "NONE", 4)) return false;
                }
            } else if (!std::memcmp(d + pos, "SSND", 4) && len >= 8 && avail >= 8) {
                const uint32_t off = be32(body); if (8 + size_t(off) > avail) return false;
                pcm = body + 8 + off; pcmBytes = std::min<size_t>(len - 8 - off, avail - 8 - off); break;
            }
            pos += 8 + static_cast<size_t>(len) + (len & 1);
        }
        if (!pcm || !nch) return false;
        big = !little; bytes = bits / 8;
        if (bits % 8 != 0 || (isFloat ? bytes != 4 : !(bytes >= 1 && bytes <= 4))) return false;
    } else return false;
    if (nch < 1 || nch > 16 || rate < 8000 || rate > 768000) return false;
    const size_t frame = static_cast<size_t>(nch) * static_cast<size_t>(bytes); const size_t frames = pcmBytes / frame;
    if (frames == 0) return false;
    out.rate = rate; out.l.resize(frames); out.r.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* f = pcm + i * frame;
        const double a = pcmSample(f, bytes, big, isFloat);
        const double b = nch > 1 ? pcmSample(f + bytes, bytes, big, isFloat) : a;
        out.l[i] = static_cast<float>(std::clamp(a, -16.0, 16.0)); out.r[i] = static_cast<float>(std::clamp(b, -16.0, 16.0));
    }
    return true;
}

// windowed-sinc (Blackman, 32 taps each side at the lower of the two rates); `from == to` copies
std::vector<float> resample(const std::vector<float>& in, double from, double to) {
    if (in.empty() || std::abs(from - to) < 1e-9) return in;
    const double ratio = from / to; const double cut = std::min(1.0, to / from);
    const int half = 32; const size_t outN = static_cast<size_t>(std::floor(static_cast<double>(in.size()) / ratio));
    std::vector<float> o(outN);
    for (size_t i = 0; i < outN; ++i) {
        const double t = static_cast<double>(i) * ratio; const long long c = static_cast<long long>(std::floor(t)); double acc = 0, wsum = 0;
        const int reach = static_cast<int>(std::ceil(half / cut));
        for (long long k = c - reach + 1; k <= c + reach; ++k) {
            const double x = (static_cast<double>(k) - t) * cut; const double u = (static_cast<double>(k) - t) / reach;   // u in (-1, 1]
            if (std::abs(u) >= 1.0) continue;
            const double w = 0.42 + 0.5 * std::cos(kPi * u) + 0.08 * std::cos(2 * kPi * u);
            const double s = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double h = s * w * cut; wsum += h;
            if (k >= 0 && k < static_cast<long long>(in.size())) acc += h * in[static_cast<size_t>(k)];
        }
        o[i] = static_cast<float>(wsum != 0 ? acc / wsum : 0);
    }
    return o;
}

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"ut03.source",   "Source",         0, 2, 0,   Curve::Step, 1, {0, 1, 2}, "", {"A Mix", "B Ref 1", "C Ref 2"}},
        {"ut03.match",    "Loudness match", 0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"ut03.xfade",    "Crossfade",      0, 500, 50, Curve::Lin, 1, {}, "ms"},
        {"ut03.loop",     "Loop",           0, 3, 2,   Curve::Step, 1, {0, 1, 2, 3}, "", {"Intro", "Verse", "Chorus", "Custom"}},
        {"ut03.sync",     "Sync play",      0, 1, 1,   Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"ut03.level",    "Level",          -24, 24, 0, Curve::Lin, 1, {}, "dB"},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate; meter_.setup(fs_, 2, 30.0);
    retired_.clear();   // the audio thread is not running while a plug-in is (re)activated
    prepared_ = true;
    for (int i = 0; i < 2; ++i) {
        if (owned_[i] && (!owned_[i]->measured || std::abs(owned_[i]->rate - fs_) > 0.5)) publish(i, convert(*owned_[i]));
        free_[i] = 0; lastEnd_[i] = -1; jumpFade_[i] = 0;
    }
    retired_.clear();
    snapToTargets();
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); }
void Processor::snapToTargets() {
    if (!prepared_) return;
    const int src = static_cast<int>(target_[Source]); for (int k = 0; k < 3; ++k) w_[k] = k == src ? 1.0 : 0.0;
    gDb_[0] = gDb_[1] = 0; matchDb_ = 0;
}

namespace {
// integrated loudness of the whole file and the start (in samples) of its loudest 20 s (mean square of 100 ms blocks, 1 s steps)
void measureRef(const std::vector<float>& l, const std::vector<float>& r, double fs, double& lufs, int& chorus, std::array<double, BandSpectrum::kBands>& bands, bool& hasBands) {
    {   // the long-term spectrum (mono) for SW Link
        BandSpectrum sp; sp.prepare(fs); sp.start();
        std::vector<float> mono(4096);
        for (size_t off = 0; off < l.size(); off += 4096) { const size_t n = std::min<size_t>(4096, l.size() - off); for (size_t i = 0; i < n; ++i) mono[i] = 0.5f * (l[off + i] + r[off + i]); sp.process(mono.data(), static_cast<int>(n)); }
        hasBands = sp.levels(bands.data());
    }
    IntegratedLoudness m; m.setup(fs, 2, 0.0);
    for (size_t off = 0; off < l.size(); off += 4096) { const int n = static_cast<int>(std::min<size_t>(4096, l.size() - off)); const float* c[2] = {l.data() + off, r.data() + off}; m.process(c, 2, n); }
    lufs = m.integrated();
    const size_t blk = std::max<size_t>(1, static_cast<size_t>(std::lround(fs * 0.1))), nb = l.size() / blk;
    std::vector<double> e(nb + 1, 0.0);
    for (size_t b = 0; b < nb; ++b) { double s = 0; for (size_t k = 0; k < blk; ++k) { const double v = 0.5 * (double(l[b * blk + k]) + r[b * blk + k]); s += v * v; } e[b + 1] = e[b] + s; }
    const size_t win = 200; chorus = 0;
    if (nb > win) { double best = -1; for (size_t b = 0; b + win <= nb; b += 10) { const double s = e[b + win] - e[b]; if (s > best) { best = s; chorus = static_cast<int>(b * blk); } } }
}
}  // namespace

std::shared_ptr<const Processor::Ref> Processor::build(Decoded&& d) const {
    auto r = std::make_shared<Ref>();
    if (!prepared_ || std::abs(d.rate - fs_) < 0.5) { r->rate = d.rate; r->l = std::move(d.l); r->r = std::move(d.r); }
    else { r->rate = fs_; r->l = resample(d.l, d.rate, fs_); r->r = resample(d.r, d.rate, fs_); }
    if (prepared_) { measureRef(r->l, r->r, fs_, r->lufs, r->chorus, r->bands, r->hasBands); r->measured = true; }
    return r;
}
std::shared_ptr<const Processor::Ref> Processor::convert(const Ref& o) const {
    auto r = std::make_shared<Ref>();
    r->rate = fs_; r->l = resample(o.l, o.rate, fs_); r->r = resample(o.r, o.rate, fs_);
    measureRef(r->l, r->r, fs_, r->lufs, r->chorus, r->bands, r->hasBands); r->measured = true;
    return r;
}

// the new reference becomes visible to the audio thread with one store; the old one waits in retired_ until a block that started after this has run
void Processor::publish(int i, std::shared_ptr<const Ref> n) {
    if (n) { auto* m = const_cast<Ref*>(n.get()); m->id = serial_.load() + 1; serial_.store(m->id); }   // not yet visible to anyone
    std::shared_ptr<const Ref> old = std::move(owned_[i]);
    owned_[i] = std::move(n);
    pub_[i].store(owned_[i].get());
    const unsigned g = gen_.load() + 1; gen_.store(g);
    if (old) retired_.emplace_back(std::move(old), g);
    reap();
}
void Processor::reap() {
    const unsigned a = acked_.load();
    for (size_t k = 0; k < retired_.size();) { if (retired_[k].second <= a) retired_.erase(retired_.begin() + static_cast<long>(k)); else ++k; }
}

// the reference that SW Link shows to the other plug-ins: the one the Source selects, else the first that is loaded
unsigned Processor::linkSerial() const {
    const int pick = static_cast<int>(target_[Source]);
    const Ref* order[2] = {pub_[pick == RefC ? 1 : 0].load(), pub_[pick == RefC ? 0 : 1].load()};
    for (const Ref* r : order) if (r && r->hasBands) return r->id;
    return 0;
}
bool Processor::linkBands(double* db) const {
    const int pick = static_cast<int>(target_[Source]);
    const Ref* order[2] = {pub_[pick == RefC ? 1 : 0].load(), pub_[pick == RefC ? 0 : 1].load()};
    for (const Ref* r : order) if (r && r->hasBands) { for (int b = 0; b < BandSpectrum::kBands; ++b) db[b] = r->bands[static_cast<size_t>(b)]; return true; }
    return false;
}

bool Processor::loadReference(int slot, const uint8_t* data, size_t size) {
    if (slot < 1 || slot > 2) return false;
    Decoded dec;
    if (!decodeAudio(data, size, dec)) { failed_.store(failed_.load() + 1); return false; }
    publish(slot - 1, build(std::move(dec)));
    done_.store(done_.load() + 1);
    return true;
}
void Processor::clearReference(int slot) { if (slot >= 1 && slot <= 2) publish(slot - 1, nullptr); }

bool Processor::stageBegin(int slot) {
    stageOpen_ = false; stage_.clear(); stage_.shrink_to_fit();
    if (slot < 1 || slot > 2) return false;
    stageSlot_ = slot; stageOpen_ = true; stageBroken_ = false;
    return true;
}
bool Processor::stageAppend(const uint8_t* data, size_t size) {
    if (!stageOpen_ || stageBroken_) return false;
    if (stage_.size() + size > stageLimit_) { stageBroken_ = true; stage_.clear(); stage_.shrink_to_fit(); return false; }
    stage_.insert(stage_.end(), data, data + size);
    return true;
}
bool Processor::stageAppendBase64(const char* text) {
    if (!stageOpen_ || stageBroken_) return false;
    std::vector<uint8_t> bytes; bytes.reserve(text ? std::strlen(text) / 4 * 3 : 0);
    if (!base64Decode(text, bytes)) { stageBroken_ = true; stage_.clear(); stage_.shrink_to_fit(); return false; }
    return stageAppend(bytes.data(), bytes.size());
}
bool Processor::stageCommit() {
    if (!stageOpen_) return false;
    stageOpen_ = false;
    bool ok = false;
    if (stageBroken_) failed_.store(failed_.load() + 1); else ok = loadReference(stageSlot_, stage_.data(), stage_.size());
    stage_.clear(); stage_.shrink_to_fit();
    return ok;
}
void Processor::stageAbort() { stageOpen_ = false; stage_.clear(); stage_.shrink_to_fit(); }

double Processor::referenceSeconds(int slot) const {
    const Ref* r = (slot >= 1 && slot <= 2) ? pub_[slot - 1].load() : nullptr;
    return r && r->rate > 0 ? static_cast<double>(r->l.size()) / r->rate : 0.0;
}
void Processor::setLoopRegion(double a, double b) { const double s = std::max(0.0, a); customA_.store(s); customB_.store(std::max(s, b)); }

void Processor::region(const Ref& r, long long& a, long long& b) const {
    const long long len = static_cast<long long>(r.l.size()); const long long span = static_cast<long long>(std::lround(20.0 * fs_));
    const double ca = customA_.load(), cb = customB_.load();
    a = 0; b = len;
    if (len <= span) { if (static_cast<int>(target_[Loop]) == Custom) { a = std::min<long long>(len - 1, std::llround(ca * fs_)); b = std::min<long long>(len, std::llround(cb * fs_)); } }
    else switch (static_cast<int>(target_[Loop])) {
        case Intro: a = 0; b = span; break;
        case Verse: a = span; b = std::min(len, 2 * span); break;
        case Chorus: a = r.chorus; b = std::min(len, a + span); break;
        default: a = std::min<long long>(len - 1, std::llround(ca * fs_)); b = std::min<long long>(len, std::llround(cb * fs_)); break;
    }
    if (b - a < 16) { a = 0; b = len; }
}
void Processor::regionOf(int slot, double& s, double& e) const {
    s = e = 0; const Ref* r = (slot >= 1 && slot <= 2) ? pub_[slot - 1].load() : nullptr; if (!r || r->l.empty()) return;
    long long a, b; region(*r, a, b); s = static_cast<double>(a) / fs_; e = static_cast<double>(b) / fs_;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    const bool stereo = numCh > 1;
    // the references as they are now (a reference replaced during this block stays alive until the next one starts)
    acked_.store(gen_.load());
    const Ref* rf[2] = {pub_[0].load(), pub_[1].load()};
    for (int i = 0; i < 2; ++i) { const unsigned id = rf[i] ? rf[i]->id : 0; if (id != seenId_[i]) { seenId_[i] = id; free_[i] = 0; lastEnd_[i] = -1; } }
    { const float* in[2] = {ch[0], stereo ? ch[1] : ch[0]}; meter_.process(in, 2, n); }
    const int srcSel = static_cast<int>(target_[Source]);
    const bool matchOn = target_[LoudnessMatch] > 0.5;
    const double inL = meter_.integrated();
    // per reference: target gain in dB (match + level), smoothed per block (~50 ms)
    const double blockA = 1.0 - std::exp(-static_cast<double>(n) / (0.05 * fs_));
    double g[2];
    for (int i = 0; i < 2; ++i) {
        double m = 0;
        if (matchOn && rf[i] && rf[i]->lufs > -199.0 && inL > -199.0) m = std::clamp(inL - rf[i]->lufs, -24.0, 24.0);
        if (i == (srcSel == RefC ? 1 : 0)) matchDb_ = matchOn ? m : 0.0;
        gDb_[i] += blockA * ((m + target_[Level]) - gDb_[i]);
        g[i] = std::pow(10.0, gDb_[i] / 20.0);
    }
    const double step = target_[Crossfade] > 0.0 ? 1.0 / (fs_ * target_[Crossfade] / 1000.0) : 2.0;
    const bool sync = target_[Sync] > 0.5 && hostSec_ >= 0.0;
    const int ne = std::max(1, static_cast<int>(fs_ * 0.005));
    long long ra[2] = {0, 0}, rb[2] = {0, 0}, pos[2] = {0, 0}; bool live[2] = {false, false};
    for (int i = 0; i < 2; ++i) {
        if (!rf[i] || rf[i]->l.empty()) continue;
        region(*rf[i], ra[i], rb[i]); const long long len = rb[i] - ra[i];
        if (sync) {
            if (!hostPlaying_) { lastEnd_[i] = -1; continue; }   // host stands still: the reference is silent
            const long long hs = static_cast<long long>(std::floor(hostSec_ * fs_));
            long long p = hs % len; if (p < 0) p += len;
            if (lastEnd_[i] >= 0) { long long d = (p - lastEnd_[i]) % len; if (d > len / 2) d -= len; if (d < -len / 2) d += len; if (std::llabs(d) > 2) jumpFade_[i] = ne; }
            else jumpFade_[i] = ne;
            pos[i] = ra[i] + p;
        } else {
            if (free_[i] < ra[i] || free_[i] >= rb[i]) free_[i] = ra[i];
            pos[i] = free_[i];
        }
        live[i] = true;
    }
    for (int k = 0; k < n; ++k) {
        for (int s = 0; s < 3; ++s) { const double t = s == srcSel ? 1.0 : 0.0; if (w_[s] < t) w_[s] = std::min(t, w_[s] + step); else if (w_[s] > t) w_[s] = std::max(t, w_[s] - step); }
        const double gm = w_[0] >= 1.0 ? 1.0 : (w_[0] <= 0.0 ? 0.0 : std::sin(w_[0] * kPi / 2));
        double aL = 0, aR = 0;
        for (int i = 0; i < 2; ++i) {
            const double w = w_[1 + i];
            if (w <= 0.0 || !live[i]) { if (live[i]) {} continue; }
            const long long a = ra[i], b = rb[i]; long long p = pos[i] + k; const long long len = b - a;
            p = a + ((p - a) % len);
            const long long dStart = p - a, dEnd = b - 1 - p; double e = 1.0;
            if (dStart < ne) e = std::min(e, static_cast<double>(dStart) / ne); if (dEnd < ne) e = std::min(e, static_cast<double>(dEnd) / ne);
            if (jumpFade_[i] > 0) { e = std::min(e, 1.0 - static_cast<double>(jumpFade_[i]) / ne); }
            const double wg = (w >= 1.0 ? 1.0 : std::sin(w * kPi / 2)) * g[i] * e;
            aL += wg * rf[i]->l[static_cast<size_t>(p)]; aR += wg * rf[i]->r[static_cast<size_t>(p)];
        }
        for (int i = 0; i < 2; ++i) if (jumpFade_[i] > 0) --jumpFade_[i];
        if (gm == 1.0 && aL == 0.0 && aR == 0.0) continue;   // plain input: untouched
        if (stereo) { ch[0][k] = static_cast<float>(gm * ch[0][k] + aL); ch[1][k] = static_cast<float>(gm * ch[1][k] + aR); if (std::abs(ch[0][k]) < 1e-30f) ch[0][k] = 0; if (std::abs(ch[1][k]) < 1e-30f) ch[1][k] = 0; }
        else { ch[0][k] = static_cast<float>(gm * ch[0][k] + 0.5 * (aL + aR)); if (std::abs(ch[0][k]) < 1e-30f) ch[0][k] = 0; }
    }
    for (int i = 0; i < 2; ++i) {
        if (!live[i]) continue;
        const long long len = rb[i] - ra[i]; const long long end = ra[i] + ((pos[i] - ra[i] + n) % len);
        if (sync) lastEnd_[i] = end - ra[i]; else free_[i] = end;
        if (!sync) lastEnd_[i] = -1;
    }
}

}  // namespace sw::ut03
