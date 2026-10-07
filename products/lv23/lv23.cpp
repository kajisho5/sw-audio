#include "lv23/lv23.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sw::lv23 {

const std::vector<ParamSpec>& specs() {
    static const std::vector<ParamSpec> s = {
        {"lv23.preset", "Preset", 0, 2, 0, Curve::Step, 1, {0, 1, 2}, "", {"ARIB -24", "EBU -23", "Stream -14"}},
        {"lv23.tol",    "Tolerance", 0.5, 3, 1, Curve::Lin, 1, {}, "LU"},
    };
    return s;
}

Processor::Processor() { for (int i = 0; i < kNumParams; ++i) target_[static_cast<size_t>(i)] = specs()[static_cast<size_t>(i)].def; mt_.setParam(mt01::Preset, 0); }

void Processor::prepare(double sampleRate, int maxBlock) {
    fs_ = sampleRate; mt_.setParam(mt01::Preset, target_[Preset]); mt_.setParam(mt01::Tolerance, target_[Tolerance]); mt_.prepare(fs_, maxBlock);
    log_.assign(static_cast<size_t>(kLogSeconds), LogRecord{}); records_ = 0; dead_ = tpOver_ = false; quiet_ = sinceLog_ = 0; prepared_ = true;
}
void Processor::setParam(int id, double v) { const auto& sp = specs()[static_cast<size_t>(id)]; target_[static_cast<size_t>(id)] = sp.toValue(sp.toNorm(v)); mt_.setParam(mt01::Preset, target_[Preset]); mt_.setParam(mt01::Tolerance, target_[Tolerance]); }

void Processor::reset() { mt_.reset(); records_ = 0; dead_ = tpOver_ = false; quiet_ = sinceLog_ = 0; }

void Processor::process(float** ch, int numCh, int n) {
    if (!prepared_ || numCh < 1 || n <= 0) return;
    mt_.process(ch, numCh, n);
    const double dt = static_cast<double>(n) / fs_;
    if (mt_.shortTerm() < -60.0) quiet_ += dt; else quiet_ = 0.0;
    if (quiet_ >= 2.0) dead_ = true;
    if (mt_.truePeakDb() > -1.0) tpOver_ = true;
    sinceLog_ += dt;
    while (sinceLog_ >= 1.0) {
        sinceLog_ -= 1.0;
        log_[static_cast<size_t>(records_ % kLogSeconds)] = LogRecord{static_cast<float>(mt_.momentary()), static_cast<float>(mt_.shortTerm()), static_cast<float>(mt_.integrated()), static_cast<float>(mt_.range()), static_cast<float>(mt_.truePeakDb()), static_cast<unsigned char>(quiet_ >= 2.0 ? 1 : 0)};
        ++records_;
    }
}

std::string Processor::exportCsv() const {
    std::string out = "elapsed,clock,momentary_lufs,short_term_lufs,integrated_lufs,range_lu,true_peak_dbtp,dead_air\n";
    const long long total = std::min<long long>(records_, kLogSeconds), first = records_ - total;
    char line[200];
    for (long long i = 0; i < total; ++i) {
        const long long sec = first + i + 1; const LogRecord& r = log_[static_cast<size_t>((first + i) % kLogSeconds)];
        char elapsed[16]; std::snprintf(elapsed, sizeof elapsed, "%02lld:%02lld:%02lld", sec / 3600, (sec / 60) % 60, sec % 60);
        char clock[32] = "";
        if (start_ > 0.0) { const long long t = static_cast<long long>(start_) + sec; const long long d = t % 86400; std::snprintf(clock, sizeof clock, "%02lld:%02lld:%02lld", d / 3600, (d / 60) % 60, d % 60); }
        std::snprintf(line, sizeof line, "%s,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%d\n", elapsed, clock, r.momentary, r.shortTerm, r.integrated, r.range, r.truePeak, r.dead ? 1 : 0);
        out += line;
    }
    return out;
}

}  // namespace sw::lv23
