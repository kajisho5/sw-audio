#include "rv07/rv07.hpp"
#include <algorithm>
#include <cmath>

namespace sw::rv07 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"rv07.use",      "Use",       0, 2, 0,        Curve::Step, 1, {0, 1, 2}, "", {"Dialog", "Instrument", "Foley"}},
        {"rv07.distance", "Distance",  0.5, 20, 3.5,   Curve::Log, 1, {}, "m"},
        {"rv07.angle",    "Angle",     -90, 90, 15,    Curve::Lin, 1, {}, "deg"},
        {"rv07.roomsize", "Room size", 0, 2, 1,        Curve::Step, 1, {0, 1, 2}, "", {"Small", "Medium", "Large"}},
        {"rv07.wall",     "Wall",      0, 3, 0,        Curve::Step, 1, {0, 1, 2, 3}, "", {"Wood", "Concrete", "Glass", "Curtain"}},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kSound = 343.0;
double useGain(int use) { static const double g[3] = {0.7, 1.0, 1.4}; return g[std::clamp(use, 0, 2)]; }
struct WallDesign { double rho, lpHz; };
constexpr WallDesign kWall[4] = {{0.80, 6000.0}, {0.95, 14000.0}, {0.90, 10000.0}, {0.35, 2500.0}};
struct Geo { double lx, ly, lz, lisX, lisY, lisZ, srcX, srcY, srcZ; };
Geo geometry(int roomSize, double d, double angleDeg) {
    static const RoomDims base[3] = {{4.0, 3.0, 2.6}, {8.0, 6.0, 3.5}, {20.0, 14.0, 6.0}};
    RoomDims r = base[std::clamp(roomSize, 0, 2)];
    const double th = angleDeg * kPi / 180.0;
    const double k = std::max({1.0, d * std::max(std::cos(th), 0.0) / (0.65 * r.lx), d * std::abs(std::sin(th)) / (0.45 * r.ly)});
    r = {r.lx * k, r.ly * k, r.lz * k};
    const double lisZ = std::min(1.6, 0.6 * r.lz), srcZ = std::min(1.5, 0.6 * r.lz);
    const double lx = 0.3 * r.lx, ly = 0.5 * r.ly;
    // forward is +x, the listener's left is +y: positive angles are to the right
    return {r.lx, r.ly, r.lz, lx, ly, lisZ, lx + d * std::cos(th), ly - d * std::sin(th), srcZ};
}
}

double wallReflectivity(int wall) { return kWall[std::clamp(wall, 0, 3)].rho; }

RoomDims roomDims(int roomSize, double distance, double angleDeg) { const Geo g = geometry(roomSize, distance, angleDeg); return {g.lx, g.ly, g.lz}; }

std::vector<Tap> earlyTaps(int roomSize, double distance, double angleDeg) {
    const Geo g = geometry(roomSize, distance, angleDeg);
    auto axis = [](int n, double len, double s) { return n * len + ((n & 1) ? len - s : s); };
    std::vector<Tap> taps;
    double r0 = 0;
    for (int order = 0; order <= 2; ++order)
        for (int nx = -2; nx <= 2; ++nx) for (int ny = -2; ny <= 2; ++ny) for (int nz = -2; nz <= 2; ++nz) {
            if (std::abs(nx) + std::abs(ny) + std::abs(nz) != order) continue;
            const double dx = axis(nx, g.lx, g.srcX) - g.lisX, dy = axis(ny, g.ly, g.srcY) - g.lisY, dz = axis(nz, g.lz, g.srcZ) - g.lisZ;
            const double r = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (order == 0) r0 = r;
            const double az = std::atan2(-dy, dx);   // to the right is positive
            taps.push_back({order == 0 ? 0.0 : (r - r0) / kSound, 1.0 / std::max(r, 0.3), std::clamp(std::sin(az), -1.0, 1.0), order});
        }
    return taps;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

void Processor::update() {
    const int size = static_cast<int>(target_[RoomSize] + 0.5), wall = static_cast<int>(target_[Wall] + 0.5), use = static_cast<int>(target_[Use] + 0.5);
    const double d = target_[Distance];
    const auto taps = earlyTaps(size, d, target_[Angle]);
    nTaps_ = static_cast<int>(std::min<size_t>(taps.size(), delay_.size()));
    useGain_ = useGain(use);
    const double rho = kWall[wall].rho;
    for (int k = 0; k < nTaps_; ++k) {
        const Tap& t = taps[static_cast<size_t>(k)];
        const double ang = (t.pan + 1.0) * 0.25 * kPi;
        const double g = t.order == 0 ? t.gain : t.gain * std::pow(rho, t.order) * useGain_;
        delay_[static_cast<size_t>(k)] = t.delaySeconds * fs_;
        gl_[static_cast<size_t>(k)] = g * std::cos(ang); gr_[static_cast<size_t>(k)] = g * std::sin(ang);
    }
    cD_ = 1.0 - std::exp(-2.0 * kPi * std::min(18000.0 / (1.0 + d / 6.0), 0.45 * fs_) / fs_);
    cE_ = 1.0 - std::exp(-2.0 * kPi * std::min(18000.0 / (1.0 + (d + 3.0) / 6.0), 0.45 * fs_) / fs_);
    cW_ = 1.0 - std::exp(-2.0 * kPi * std::min(kWall[wall].lpHz, 0.45 * fs_) / fs_);
    for (int c = 0; c < 2; ++c) {
        hp_[static_cast<size_t>(c)].setup(Svf::Mode::HighPass, use == Dialog ? 150.0 : 20.0, fs_, 0.70710678, 0);
        glassHp_[static_cast<size_t>(c)].setup(Svf::Mode::HighPass, wall == Glass ? 250.0 : 20.0, fs_, 0.70710678, 0);
    }
}

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    buf_.assign(static_cast<size_t>(0.7 * fs_) + 8, 0.0f);
    pos_ = 0; airD_[0] = airD_[1] = airE_[0] = airE_[1] = wallLp_[0] = wallLp_[1] = 0;
    update(); cur_ = delay_;
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
    if (prepared_) update();
}

void Processor::snapToTargets() { if (prepared_) { update(); cur_ = delay_; } }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const size_t sz = buf_.size();
    for (int i = 0; i < n; ++i) {
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        buf_[pos_] = static_cast<float>(m);
        double er[2] = {0, 0};
        for (int k = 1; k < nTaps_; ++k) {
            const size_t kk = static_cast<size_t>(k);
            cur_[kk] += std::clamp(delay_[kk] - cur_[kk], -0.5, 0.5);
            double rp = static_cast<double>(pos_) - std::min(cur_[kk], static_cast<double>(sz - 2)); if (rp < 0) rp += static_cast<double>(sz);
            const size_t i0 = static_cast<size_t>(rp) % sz, i1 = (i0 + 1) % sz; const double fr = rp - std::floor(rp);
            const double v = buf_[i0] + fr * (buf_[i1] - buf_[i0]);
            er[0] += gl_[kk] * v; er[1] += gr_[kk] * v;
        }
        pos_ = (pos_ + 1) % sz;
        double out[2];
        for (int c = 0; c < 2; ++c) {
            const size_t cc = static_cast<size_t>(c);
            const double direct = (c == 0 ? gl_[0] : gr_[0]) * m;
            airD_[cc] += cD_ * (direct - airD_[cc]);
            wallLp_[cc] += cW_ * (er[c] - wallLp_[cc]);
            double e = glassHp_[cc].process(hp_[cc].process(wallLp_[cc]));
            airE_[cc] += cE_ * (e - airE_[cc]);
            out[c] = airD_[cc] + airE_[cc];
            if (std::abs(out[c]) < 1e-30) out[c] = 0.0;
        }
        ch[0][i] = static_cast<float>(out[0]);
        if (nch > 1) ch[1][i] = static_cast<float>(out[1]);
    }
}

}  // namespace sw::rv07
