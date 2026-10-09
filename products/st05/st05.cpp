#include "st05/st05.hpp"
#include "sw/tail.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sw::st05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"st05.speakers", "Speakers", 0, 2, 0,  Curve::Step, 1, {0, 1, 2}, "", {"Nearfield", "Mains", "Car"}},
        {"st05.room",     "Room",     0, 2, 0,  Curve::Step, 1, {0, 1, 2}, "", {"Studio A", "Studio B", "Living"}},
        {"st05.angle",    "Angle",    0, 60, 30, Curve::Lin, 1, {}, "deg"},
        {"st05.headsize", "Head size", 0, 2, 1, Curve::Step, 1, {0, 1, 2}, "", {"Small", "Medium", "Large"}},
        {"st05.tracking", "Tracking", 0, 1, 0,  Curve::Step, 1, {0, 1}, "", {"Off", "On"}},
        {"st05.profile",  "Phones profile", 0, 3, 0, Curve::Step, 1, {0, 1, 2, 3}, "", {"Off", "Closed", "Open", "Earbud"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kSound = 343.0, kMixSeconds = 0.030;
struct RoomGeo { double w, l, h, rt; };
RoomGeo roomOf(int speakers, int room) {
    static const RoomGeo rooms[3] = {{5.0, 6.0, 3.0, 0.25}, {6.0, 8.0, 3.5, 0.40}, {4.0, 5.0, 2.5, 0.55}};
    if (speakers == Car) return {1.6, 2.4, 1.2, 0.08};
    return rooms[std::clamp(room, 0, 2)];
}
double wrapDeg(double a) { while (a > 180.0) a -= 360.0; while (a <= -180.0) a += 360.0; return a; }
double axisImage(int n, double len, double s) { return n * len + ((n & 1) ? len - s : s); }
struct Lcg { uint32_t s; double u() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0 / 16777216.0); } double white() { return (u() + u() + u() - 1.5) * 2.0; } };
}

double headRadius(int head) { static const double a[3] = {0.075, 0.0875, 0.100}; return a[std::clamp(head, 0, 2)]; }
double itdSeconds(double lateralDeg, int head) { const double th = std::min(std::abs(lateralDeg), 90.0) * kPi / 180.0; return headRadius(head) / kSound * (th + std::sin(th)); }
double roomRt60(int speakers, int room) { return roomOf(speakers, room).rt; }
double roomVolume(int speakers, int room) { const RoomGeo g = roomOf(speakers, room); return g.w * g.l * g.h; }
double speakerDistance(int speakers) { static const double d[3] = {1.2, 3.0, 0.8}; return d[std::clamp(speakers, 0, 2)]; }

void designIr(double fs, const IrSpec& sp, int speaker, int ear, size_t length, std::vector<double>& out) {
    out.assign(length, 0.0);
    const RoomGeo g = roomOf(sp.speakers, sp.room);
    const double A = sp.angleDeg * kPi / 180.0, a = headRadius(sp.head), yaw = sp.yawDeg;
    double D = speakerDistance(sp.speakers);
    const double yl = 0.45 * g.l, ze = sp.speakers == Car ? 0.9 : 1.2;
    if (yl + D * std::cos(A) > g.l - 0.3) D = std::max(0.5, (g.l - 0.3 - yl) / std::max(std::cos(A), 0.2));
    const double xl = 0.5 * g.w, side = speaker == 0 ? -1.0 : 1.0;
    const double sx = std::clamp(xl + side * D * std::sin(A), 0.15, g.w - 0.15), sy = yl + D * std::cos(A);
    const double V = g.w * g.l * g.h, S = 2.0 * (g.w * g.l + g.w * g.h + g.l * g.h);
    const double alpha = std::clamp(0.161 * V / (S * g.rt), 0.05, 0.9), rho = std::sqrt(1.0 - alpha);
    const double D0 = D;   // the level reference: a direct path of the nominal length sits at 0.5
    const double w0 = kSound / a, tau = 1.0 / (2.0 * w0), K = 2.0 * fs;
    const double earSign = ear == 0 ? -1.0 : 1.0;   // the ear's axis points to -90 / +90 degrees on the head
    auto addTap = [&](double t, double gain, double phi) {
        const double al = 1.05 + 0.95 * std::cos(1.2 * phi);
        const double b0 = 1.0 + al * tau * K, b1 = 1.0 - al * tau * K, a0 = 1.0 + tau * K, a1 = 1.0 - tau * K;
        const double pos = t * fs; const long i0 = static_cast<long>(std::floor(pos)); const double fr = pos - static_cast<double>(i0);
        // the shadow filter's response, 48 samples (it is a one-pole), added at two neighbouring positions (linear interpolation of the delay)
        double x1 = 0.0, y1 = 0.0;
        for (int k = 0; k < 64; ++k) {
            const double x = k == 0 ? 1.0 : 0.0;
            const double y = (b0 * x + b1 * x1 - a1 * y1) / a0; x1 = x; y1 = y;
            const long idx = i0 + k;
            if (idx >= 0 && static_cast<size_t>(idx) < length) out[static_cast<size_t>(idx)] += gain * (1.0 - fr) * y;
            if (idx + 1 >= 0 && static_cast<size_t>(idx + 1) < length) out[static_cast<size_t>(idx + 1)] += gain * fr * y;
            if (std::abs(y) < 1e-6 && k > 8) break;
        }
    };
    // the reference time: the direct sound's arrival at its NEAR ear (the same for both ears, so that the interaural time difference stays in the IR)
    double tRef = 0.0;
    {
        const double dx = sx - xl, dy = sy - yl, dz = 0.0, r = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double azRel = wrapDeg(std::atan2(dx, dy) * 180.0 / kPi - yaw) * kPi / 180.0;
        tRef = r / kSound - 0.5 * itdSeconds(std::asin(std::clamp(std::sin(azRel), -1.0, 1.0)) * 180.0 / kPi, sp.head);
    }
    // direct sound and images up to the 2nd order
    for (int order = 0; order <= 2; ++order)
        for (int nx = -2; nx <= 2; ++nx) for (int ny = -2; ny <= 2; ++ny) for (int nz = -2; nz <= 2; ++nz) {
            if (std::abs(nx) + std::abs(ny) + std::abs(nz) != order) continue;
            const double ix = axisImage(nx, g.w, sx), iy = axisImage(ny, g.l, sy), iz = axisImage(nz, g.h, ze);
            const double dx = ix - xl, dy = iy - yl, dz = iz - ze, r = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double el = std::asin(std::clamp(dz / std::max(r, 1e-6), -1.0, 1.0));
            const double azRel = wrapDeg(std::atan2(dx, dy) * 180.0 / kPi - yaw) * kPi / 180.0;   // to the right of the head's front: positive
            const double lat = std::asin(std::clamp(std::sin(azRel) * std::cos(el), -1.0, 1.0));  // lateral angle
            const double itd = itdSeconds(lat * 180.0 / kPi, sp.head);
            const bool nearEar = (lat >= 0.0) == (ear == 1);
            const double t = r / kSound + (nearEar ? -0.5 : 0.5) * itd;
            const double phi = std::acos(std::clamp(earSign * std::sin(lat), -1.0, 1.0));
            const double gain = 0.5 * D0 / std::max(r, 0.3) * std::pow(rho, order);
            addTap(std::max(t - tRef, -0.0004) + 0.0005, gain, phi);   // 0.5 ms of lead-in so that the filters' start is inside the buffer
        }
    // diffuse tail
    const double Dc = 0.057 * std::sqrt(V / g.rt), el = 0.25 * (D / Dc) * (D / Dc);   // E_late / E_direct = (D / Dc)^2, the direct amplitude being 0.5
    const double sigma = std::sqrt(el * 13.8 / (fs * g.rt));
    Lcg rng{static_cast<uint32_t>(0x9E3779B9u * static_cast<uint32_t>(speaker * 2 + ear + 1))};
    double lp = 0.0, env = 1.0, c = 0.0;
    const size_t mixAt = static_cast<size_t>(kMixSeconds * fs), fadeIn = static_cast<size_t>(0.010 * fs);
    const double envStep = std::exp(-6.9078 / (g.rt * fs));
    for (size_t i = 0; i < length; ++i) {
        if ((i & 31) == 0) { const double t = static_cast<double>(i) / fs; c = std::exp(-2.0 * kPi * std::max(1500.0, 10000.0 * std::exp(-t / (0.6 * g.rt))) / fs); }
        lp = (1.0 - c) * rng.white() + c * lp;
        if (i >= mixAt) {
            const double ramp = std::min(1.0, static_cast<double>(i - mixAt) / static_cast<double>(fadeIn));
            out[i] += sigma * 1.6 * lp * env * ramp;   // 1.6: the low-pass takes about 4 dB of a white noise
        }
        env *= envStep;
    }
    // the speaker's own response
    const double hp = sp.speakers == Mains ? 35.0 : sp.speakers == Car ? 80.0 : 60.0;
    Svf f1, f2, shelf;
    f1.setup(Svf::Mode::HighPass, hp, fs, 0.7071, 0.0);
    if (sp.speakers == Car) shelf.setup(Svf::Mode::LowShelf, 100.0, fs, 0.7071, 5.0);
    for (size_t i = 0; i < length; ++i) { double v = f1.process(out[i]); if (sp.speakers == Car) v = shelf.process(v); out[i] = v; }
    (void)f2;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

IrSpec Processor::wanted() const {
    IrSpec s; s.speakers = static_cast<int>(target_[Speakers] + 0.5); s.room = static_cast<int>(target_[Room] + 0.5); s.angleDeg = target_[Angle]; s.head = static_cast<int>(target_[HeadSize] + 0.5);
    s.yawDeg = target_[Tracking] > 0.5 ? std::clamp(yawTarget_, -90.0, 90.0) : 0.0;
    return s;
}

void Processor::setProfile() {
    const int p = static_cast<int>(target_[PhonesProfile] + 0.5);
    if (p == profile_) return;
    profile_ = p; eqOn_ = p != ProfOff;
    // generic curves by kind, design values: Closed (a bass bump, a dip in the upper mids), Open (little bass, a lift at the top), Earbud (a bass lift, a presence dip, air)
    struct B { Svf::Mode m; double f, q, g; };
    B b[3][3] = {{{Svf::Mode::LowShelf, 90, 0.7071, -2.5}, {Svf::Mode::Bell, 3200, 1.0, 1.5}, {Svf::Mode::HighShelf, 9000, 0.7071, 0.0}},
                 {{Svf::Mode::LowShelf, 90, 0.7071, 3.0}, {Svf::Mode::Bell, 3200, 1.0, 0.0}, {Svf::Mode::HighShelf, 9000, 0.7071, -2.0}},
                 {{Svf::Mode::LowShelf, 100, 0.7071, 2.5}, {Svf::Mode::Bell, 3000, 1.0, -3.0}, {Svf::Mode::HighShelf, 10000, 0.7071, 2.0}}};
    if (eqOn_) for (int e = 0; e < 2; ++e) for (int k = 0; k < 3; ++k) { const B& c = b[p - 1][k]; eq_[static_cast<size_t>(e)][static_cast<size_t>(k)].setup(c.m, c.f, fs_, c.q, c.g); eq_[static_cast<size_t>(e)][static_cast<size_t>(k)].reset(); }
}

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate;
    len_ = static_cast<size_t>(kIrSeconds * fs_);
    for (size_t i = 0; i < 4; ++i) {
        conv_[i].prepare(static_cast<int>(len_), static_cast<int>(0.02 * fs_), static_cast<int>(i));   // the four convolvers' heavy blocks fall in four different calls
        kernel_[i].assign(len_, 0.0);
        tmp_[i].assign(static_cast<size_t>(std::max(maxBlock, 256)), 0.0f);
    }
    profile_ = -1; setProfile();
    stage_ = Idle; dirty_ = false;
    prepared_ = true;
    // the first IR set, built at once
    built_ = job_ = wanted();
    for (int k = 0; k < 4; ++k) { designIr(fs_, job_, k >> 1, k & 1, len_, kernel_[static_cast<size_t>(k)]); conv_[static_cast<size_t>(k)].setKernel(kernel_[static_cast<size_t>(k)], true); }
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    setProfile();
    const IrSpec w = wanted();
    if (w.speakers != built_.speakers || w.room != built_.room || w.head != built_.head || w.angleDeg != built_.angleDeg || w.yawDeg != built_.yawDeg) {
        built_ = job_ = w;
        for (int k = 0; k < 4; ++k) { designIr(fs_, job_, k >> 1, k & 1, len_, kernel_[static_cast<size_t>(k)]); conv_[static_cast<size_t>(k)].setKernel(kernel_[static_cast<size_t>(k)], true); }
        stage_ = Idle; dirty_ = false;
    }
}

void Processor::startJob() {
    job_ = wanted(); dirty_ = false; stage_ = Design; designIdx_ = 0;
}

void Processor::stepJob() {
    switch (stage_) {
        case Design:
            designIr(fs_, job_, designIdx_ >> 1, designIdx_ & 1, len_, kernel_[static_cast<size_t>(designIdx_)]);
            if (++designIdx_ >= 4) { for (size_t k = 0; k < 4; ++k) conv_[k].beginKernel(kernel_[k], static_cast<int>(len_)); stage_ = Load; loaded_ = false; loadIdx_ = 0; }
            break;
        case Load: {
            // one convolver per call (a partition of the longest tier costs about 2 ms)
            if (conv_[static_cast<size_t>(loadIdx_)].stepKernel(1) && ++loadIdx_ >= 4) { stage_ = Commit; commitIdx_ = 0; }
            break;
        }
        case Commit:
            conv_[static_cast<size_t>(commitIdx_)].commitKernel(false);
            if (++commitIdx_ >= 4) { built_ = job_; stage_ = Idle; }
            break;
        default: stage_ = Idle; break;
    }
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 2) return;
    setProfile();
    if (stage_ == Idle) {
        if (differs(wanted())) startJob();
    }
    if (stage_ != Idle) stepJob();
    for (int off = 0; off < n; off += static_cast<int>(tmp_[0].size())) {
        const int m = std::min(n - off, static_cast<int>(tmp_[0].size()));
        for (int i = 0; i < m; ++i) { tmp_[0][static_cast<size_t>(i)] = tmp_[1][static_cast<size_t>(i)] = ch[0][off + i]; tmp_[2][static_cast<size_t>(i)] = tmp_[3][static_cast<size_t>(i)] = ch[1][off + i]; }
        for (size_t k = 0; k < 4; ++k) conv_[k].process(tmp_[k].data(), m);
        for (int i = 0; i < m; ++i) {
            double l = static_cast<double>(tmp_[0][static_cast<size_t>(i)]) + tmp_[2][static_cast<size_t>(i)], r = static_cast<double>(tmp_[1][static_cast<size_t>(i)]) + tmp_[3][static_cast<size_t>(i)];
            if (eqOn_) { for (size_t k = 0; k < 3; ++k) { l = eq_[0][k].process(l); r = eq_[1][k].process(r); } }
            if (std::abs(l) < 1e-30) l = 0.0;
            if (std::abs(r) < 1e-30) r = 0.0;
            ch[0][off + i] = static_cast<float>(l); ch[1][off + i] = static_cast<float>(r);
        }
    }
}

// the tail: the length of the impulse responses (a fixed kIrSeconds), plus a little
double Processor::tailSeconds() const { return fs_ > 0.0 ? static_cast<double>(len_) / fs_ + 0.1 : 0.0; }

void Processor::reset() {
    for (auto& c : conv_) c.reset();
    for (auto& t : tmp_) std::fill(t.begin(), t.end(), 0.0f);
    for (auto& e : eq_) for (auto& f : e) f.reset();
}

}  // namespace sw::st05
