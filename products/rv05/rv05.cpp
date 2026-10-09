#include "rv05/rv05.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = [] {
        std::vector<ParamSpec> v = {
            {"rv05.room",        "Room",         0, 2, 1,    Curve::Step, 1, {0, 1, 2}, "", {"Small", "Medium", "Large"}},
            {"rv05.decay",       "Decay",        0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rv05.micdistance", "Mic distance", 0, 100, 50, Curve::Lin, 1, {}, "%"},
            {"rv05.speakertilt", "Speaker tilt", 0, 10, 5,   Curve::Lin, 1, {}, ""},
            {"rv05.tone",        "Tone",         0, 100, 50, Curve::Lin, 1, {}, "%"},
            {"rv05.mix",         "Mix",          0, 100, 30, Curve::Lin, 1, {}, "%"},
            unitSpec("rv05.unit"),
        };
        v[MicDistance].minLabel = "Near"; v[MicDistance].maxLabel = "Far"; v[Tone].minLabel = "Dark"; v[Tone].maxLabel = "Bright";
        return v;
    }();
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kSound = 343.0, kWall = 0.85;
struct SourceMic { double sx, sy, sz, mx, my, mz; };
SourceMic positions(int room, double micPct) {
    const RoomDims r = roomDims(room);
    const double d = micDistanceMeters(room, micPct);
    return {0.12 * r.lx, 0.35 * r.ly, 0.4 * r.lz, 0.12 * r.lx + d, 0.35 * r.ly, 0.4 * r.lz};
}
struct LateDesign { double minMs, maxMs, preMs; };
constexpr LateDesign kLate[3] = {{10.0, 35.0, 7.0}, {20.0, 65.0, 12.0}, {35.0, 110.0, 21.0}};
double dampingHz(double pct) { return 2000.0 * std::pow(7.0, pct * 0.01); }   // Dark 2 kHz .. Bright 14 kHz
}

RoomDims roomDims(int room) {
    static const RoomDims d[3] = {{5.0, 4.0, 3.2}, {8.0, 6.0, 4.0}, {14.0, 10.0, 5.5}};
    return d[std::clamp(room, 0, 2)];
}
double micDistanceMeters(int room, double micPct) { return 0.5 + std::clamp(micPct, 0.0, 100.0) * 0.01 * (0.7 * roomDims(room).lx - 0.5); }
double decaySeconds(double knob) { return 0.4 * std::pow(10.0, std::clamp(knob, 0.0, 10.0) * 0.1); }

std::vector<Tap> earlyReflections(int room, double micPct) {
    const RoomDims r = roomDims(room);
    const SourceMic p = positions(room, micPct);
    std::vector<Tap> taps;
    auto axis = [](int n, double len, double s) { return n * len + ((n & 1) ? len - s : s); };   // the n-th image of the source along one axis
    for (int order = 0; order <= 2; ++order)
        for (int nx = -2; nx <= 2; ++nx) for (int ny = -2; ny <= 2; ++ny) for (int nz = -2; nz <= 2; ++nz) {
            if (std::abs(nx) + std::abs(ny) + std::abs(nz) != order) continue;
            const double ix = axis(nx, r.lx, p.sx), iy = axis(ny, r.ly, p.sy), iz = axis(nz, r.lz, p.sz);
            const double dx = ix - p.mx, dy = iy - p.my, dz = iz - p.mz, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            taps.push_back({dist / kSound, std::pow(kWall, order) / std::max(dist, 0.3), std::clamp(dy / std::max(dist, 0.3) * 1.5, -1.0, 1.0)});
        }
    return taps;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::updateRoom() {
    const int room = static_cast<int>(target_[Room] + 0.5);
    taps_ = earlyReflections(room, target_[MicDistance]);
    nTaps_ = static_cast<int>(std::min<size_t>(taps_.size(), tapDelay_.size()));
    for (int k = 0; k < nTaps_; ++k) {
        const Tap& t = taps_[static_cast<size_t>(k)];
        const double ang = (t.pan + 1.0) * 0.25 * kPi;
        tapDelay_[static_cast<size_t>(k)] = t.delaySeconds * fs_;
        tapL_[static_cast<size_t>(k)] = t.gain * std::cos(ang); tapR_[static_cast<size_t>(k)] = t.gain * std::sin(ang);
    }
    // air absorption grows with the distance: one-pole low-pass on the early sum
    const double d = micDistanceMeters(room, target_[MicDistance]);
    airC_ = 1.0 - std::exp(-2.0 * kPi * std::min(18000.0 / (1.0 + d / 4.0), 0.45 * fs_) / fs_);
    // the late tail: lengths for the room, decay, damping; level from the critical distance r_c = 0.057 sqrt(V / RT60)
    const LateDesign& ld = kLate[room];
    const int n = fdn_.lines();
    for (int i = 0; i < n; ++i) fdn_.setLength(i, ld.minMs * std::pow(ld.maxMs / ld.minMs, static_cast<double>(i) / (n - 1)) * 0.001 * fs_);
    const double rt = decaySeconds(target_[Decay]);
    fdn_.setModulation(4.0, 0.35); fdn_.setDecay(rt); fdn_.setDamping(dampingHz(target_[Tone]));
    const RoomDims rd = roomDims(room);
    const double rc = 0.057 * std::sqrt(rd.lx * rd.ly * rd.lz / rt);
    lateGain_ = 1.0 / rc;
    preLateLen_ = ld.preMs * 0.001 * fs_;
}

void Processor::updateFilters() {
    const double g = (target_[Tilt] - 5.0) * 1.2;   // +-6 dB: lows down and highs up (or the opposite) around 600 Hz
    for (int c = 0; c < 1; ++c) {
        shelfLo_[static_cast<size_t>(c)].setup(Svf::Mode::LowShelf, 600.0, fs_, 0.70710678, -g);
        shelfHi_[static_cast<size_t>(c)].setup(Svf::Mode::HighShelf, 600.0, fs_, 0.70710678, g);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    fdn_.prepare(fs_, 16, 0.2);
    buf_.assign(static_cast<size_t>(0.35 * fs_) + 8, 0.0f);
    late_.assign(static_cast<size_t>(0.05 * fs_) + 8, 0.0f);
    for (int k = 0; k < 4; ++k) { static const double ms[4] = {3.0, 2.2, 7.9, 5.8}; ap_[static_cast<size_t>(k)].buf.assign(std::max<size_t>(1, static_cast<size_t>(std::lround(ms[k] * 0.001 * fs_))), 0.0f); ap_[static_cast<size_t>(k)].pos = 0; }
    pos_ = latePos_ = 0; airLp_[0] = airLp_[1] = 0;
    curDelay_.fill(0.0);
    updateRoom(); fdn_.snapLengths(); updateFilters();
    curDelay_ = tapDelay_;
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Decay]) / fdn_.meanLengthSeconds());
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (!prepared_) return;
    switch (id) {
        case Room: case Decay: case MicDistance: case Tone: updateRoom(); break;
        case Tilt: updateFilters(); break;
        default: break;
    }
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    updateRoom(); fdn_.snapLengths(); updateFilters(); curDelay_ = tapDelay_;
    lateTrim_ = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Decay]) / fdn_.meanLengthSeconds());
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const size_t sz = buf_.size(), lsz = late_.size();
    const double trimTarget = 1.0 / std::sqrt(Fdn::kEnergyConstant * decaySeconds(target_[Decay]) / fdn_.meanLengthSeconds());
    const double apG = 0.62;
    for (int i = 0; i < n; ++i) {
        lateTrim_ += 0.0005 * (trimTarget - lateTrim_);
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double x = shelfHi_[0].process(shelfLo_[0].process(m));   // to the loudspeaker
        buf_[pos_] = static_cast<float>(x);
        double er[2] = {0, 0};
        for (int k = 0; k < nTaps_; ++k) {
            const size_t kk = static_cast<size_t>(k);
            curDelay_[kk] += std::clamp(tapDelay_[kk] - curDelay_[kk], -0.5, 0.5);
            double rp = static_cast<double>(pos_) - std::min(curDelay_[kk], static_cast<double>(sz - 2)); if (rp < 0) rp += static_cast<double>(sz);
            const size_t i0 = static_cast<size_t>(rp) % sz, i1 = (i0 + 1) % sz; const double fr = rp - std::floor(rp);
            const double v = buf_[i0] + fr * (buf_[i1] - buf_[i0]);
            er[0] += tapL_[kk] * v; er[1] += tapR_[kk] * v;
        }
        pos_ = (pos_ + 1) % sz;
        airLp_[0] += airC_ * (er[0] - airLp_[0]); airLp_[1] += airC_ * (er[1] - airLp_[1]);
        // late tail: delayed input through the diffusers into the FDN
        late_[latePos_] = static_cast<float>(x);
        double lp = static_cast<double>(latePos_) - preLateLen_; if (lp < 0) lp += static_cast<double>(lsz);
        double d = late_[static_cast<size_t>(lp) % lsz];
        latePos_ = (latePos_ + 1) % lsz;
        for (auto& a : ap_) d = a.process(d, apG);
        double fl, fr2; fdn_.process(d, fl, fr2);
        double l = airLp_[0] + lateGain_ * lateTrim_ * fl, r = airLp_[1] + lateGain_ * lateTrim_ * fr2;
        if (std::abs(l) < 1e-30) l = 0.0; if (std::abs(r) < 1e-30) r = 0.0;
        ch[0][i] = static_cast<float>(l);
        if (nch > 1) ch[1][i] = static_cast<float>(r);
    }
}

// the tail: Decay (the knob 0..10 is 0.4 s .. 4 s for 60 dB) taken down to -80 dB
double Processor::tailSeconds() const { return tail::fromRt60(decaySeconds(target_[Decay])) + 0.4; }

}  // namespace sw::rv05
