#include "md05/md05.hpp"
#include <algorithm>
#include <cmath>

namespace sw::md05 {
const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"md05.speed",   "Speed",        0, 2, 1,   Curve::Step, 1, {0, 1, 2}, "", {"Stop", "Slow", "Fast"}},
        {"md05.accel",   "Accel",        0, 10, 5,  Curve::Lin, 1, {}, ""},
        {"md05.horn",    "Horn",         0, 10, 7,  Curve::Lin, 1, {}, ""},
        {"md05.drum",    "Drum",         0, 10, 7,  Curve::Lin, 1, {}, ""},
        {"md05.micdist", "Mic distance", 0, 100, 50, Curve::Lin, 1, {}, "%"},
        {"md05.drive",   "Drive",        0, 10, 2,  Curve::Lin, 1, {}, ""},
        {"md05.mix",     "Mix",          0, 100, 100, Curve::Lin, 1, {}, "%"},
    };
    return s;
}
namespace {
constexpr double kPi = 3.14159265358979323846, kSound = 343.0, kSplitHz = 800.0, kCentreSeconds = 0.001;
constexpr double kHornR = 0.18, kDrumR = 0.12, kHornK = 0.7, kDrumK = 0.45, kMicAngle = kPi / 3.0;
}
double hornTargetHz(int speed) { static const double h[3] = {0.0, 0.8, 6.7}; return h[std::clamp(speed, 0, 2)]; }
double drumTargetHz(int speed) { static const double d[3] = {0.0, 0.67, 5.7}; return d[std::clamp(speed, 0, 2)]; }
double hornTau(double accel) { return 0.3 + 0.2 * accel; }
double drumTau(double accel) { return 3.0 * hornTau(accel); }

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; }

int Processor::latencySamples() const { return static_cast<int>(std::lround(kCentreSeconds * fs_)); }

void Processor::prepare(double sampleRate, int) {
    fs_ = sampleRate;
    size_t sz = 16; while (sz < static_cast<size_t>(0.02 * fs_) + 16) sz <<= 1;
    mask_ = sz - 1;
    for (auto& b : buf_) b.assign(sz, 0.0f);
    lo1_.setup(Svf::Mode::LowPass, kSplitHz, fs_, 0.7071, 0.0); lo2_.setup(Svf::Mode::LowPass, kSplitHz, fs_, 0.7071, 0.0);
    hi1_.setup(Svf::Mode::HighPass, kSplitHz, fs_, 0.7071, 0.0); hi2_.setup(Svf::Mode::HighPass, kSplitHz, fs_, 0.7071, 0.0);
    hornBell_.setup(Svf::Mode::Bell, 2500.0, fs_, 1.0, 1.5); drumBell_.setup(Svf::Mode::Bell, 110.0, fs_, 0.9, 2.0);
    lo1_.reset(); lo2_.reset(); hi1_.reset(); hi2_.reset(); hornBell_.reset(); drumBell_.reset();
    pos_ = 0; hornAngle_ = drumAngle_ = 0.0;
    const int speed = static_cast<int>(target_[Speed] + 0.5);
    hornHz_ = hornTargetHz(speed); drumHz_ = drumTargetHz(speed);
    prepared_ = true;
}

void Processor::setParam(int id, double v) {
    const auto& sp = specs()[static_cast<size_t>(id)];
    v = sp.toValue(sp.toNorm(v));
    target_[static_cast<size_t>(id)] = v;
}

void Processor::midiSpeed(int speed) {
    if (speed < Stop || speed > Fast || speed == static_cast<int>(target_[Speed] + 0.5)) return;   // no change: nothing to write
    setParam(Speed, speed); pendingSpeed_ = speed;
}
void Processor::midiControl(int cc, int value) {
    if (cc == 64) midiSpeed(value >= 64 ? Fast : Slow);
    else if (cc == 1) midiSpeed(value < 32 ? Stop : value < 96 ? Slow : Fast);
}
void Processor::midiNote(int key, bool on) {
    if (!on) return;
    if (key == 36) midiSpeed(Stop); else if (key == 37) midiSpeed(Slow); else if (key == 38) midiSpeed(Fast);
}
int Processor::takeParamWrite(int& id, double& plain) {
    if (pendingSpeed_ < 0) return 0;
    id = Speed; plain = pendingSpeed_; pendingSpeed_ = -1; return 7;
}

void Processor::snapToTargets() {
    if (!prepared_) return;
    const int speed = static_cast<int>(target_[Speed] + 0.5);
    hornHz_ = hornTargetHz(speed); drumHz_ = drumTargetHz(speed);
}

double Processor::read(int r, double d) const {
    const auto& b = buf_[static_cast<size_t>(r)];
    const double rp = static_cast<double>(pos_) - d;
    const double fl = std::floor(rp), f = rp - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) { return static_cast<double>(b[static_cast<size_t>(k) & mask_]); };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_) return;
    const int nch = std::min(numCh, 2);
    const int speed = static_cast<int>(target_[Speed] + 0.5);
    const double hTarget = hornTargetHz(speed), dTarget = drumTargetHz(speed);
    const double hk = 1.0 - std::exp(-1.0 / (hornTau(target_[Accel]) * fs_)), dk = 1.0 - std::exp(-1.0 / (drumTau(target_[Accel]) * fs_));
    const double micScale = 1.0 - 0.5 * target_[MicDistance] * 0.01;
    const double kH = kHornK * micScale, kD = kDrumK * micScale;
    const double gH = target_[Horn] / 7.0, gD = target_[Drum] / 7.0;
    sat_.setDriveDb(1.8 * target_[Drive]); sat_.setHeadroom(1.0);
    const double centre = kCentreSeconds * fs_;
    const double micPhi[2] = {kMicAngle, -kMicAngle};
    for (int i = 0; i < n; ++i) {
        hornHz_ += hk * (hTarget - hornHz_); drumHz_ += dk * (dTarget - drumHz_);
        hornAngle_ += hornHz_ / fs_; if (hornAngle_ >= 1.0) hornAngle_ -= 1.0;
        drumAngle_ += drumHz_ / fs_; if (drumAngle_ >= 1.0) drumAngle_ -= 1.0;
        const double m = nch > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double x = sat_.process(m);
        const double lo = lo2_.process(lo1_.process(x)), hi = hi2_.process(hi1_.process(x));
        buf_[0][pos_ & mask_] = static_cast<float>(hornBell_.process(hi));
        buf_[1][pos_ & mask_] = static_cast<float>(drumBell_.process(lo));
        ++pos_;
        double out[2] = {0.0, 0.0};
        for (int mic = 0; mic < 2; ++mic) {
            for (int rotor = 0; rotor < 2; ++rotor) {
                const bool horn = rotor == 0;
                const double th = 2.0 * kPi * (horn ? hornAngle_ : drumAngle_) - micPhi[mic], co = std::cos(th);
                const double d = centre - (horn ? kHornR : kDrumR) * co / kSound * fs_;
                lastDelay_[rotor][mic] = d;
                const double k = horn ? kH : kD;
                const double g = ((1.0 - k) + k * 0.5 * (1.0 + co)) / (1.0 - 0.5 * k);
                out[mic] += (horn ? gH : gD) * g * read(rotor, d);
            }
        }
        for (int c = 0; c < nch; ++c) { double y = out[c]; if (std::abs(y) < 1e-30) y = 0.0; ch[c][i] = static_cast<float>(y); }
    }
}

}  // namespace sw::md05
