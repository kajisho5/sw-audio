// SW LV23 Loudness — a broadcast loudness meter (spec: 仕様書 v1.0「LV23 Loudness」: "MT01 と同じ計測エンジンの LIVE 版"). The sound passes untouched; delay 0; no Auto gain, no Delta.
//   The engine is sw::mt01::Processor itself (Momentary, Short-term, Integrated, Range, True peak). Preset: ARIB -24 / EBU -23 / Stream -14 (the Custom of MT01 is not offered). Tolerance (0.5..3 LU, default 1: the width of the band around the target in which inBand() is true) is added to the spec's table — a plug-in with a single parameter fails clap-validator's param-set-events check (a random value can equal the current one).
//   Status: deadAirSeen() — the 3 s loudness has been under -60 LUFS for 2 s or more since the last reset (the screen shows "Dead air OK" while it is false); tpOver() — a true peak over -1 dBTP has occurred (the screen shows "No TP over" while it is false).
//   Log (EVO, class A): once a second the audio thread writes a record (elapsed seconds, momentary, short-term, integrated, range, true peak so far, dead-air flag) into a preallocated ring of 24 h (86400 records, 3.4 MB); exportCsv() makes the CSV text
//   (not real time: the screen calls it from another thread while stopped, or after a reset). reset() clears the meter and the log; setStartTime(unix seconds) gives the CSV a wall-clock column.
//   **The delivery format of the log is not known (the spec's open point); the CSV here is: header "elapsed,clock,momentary_lufs,short_term_lufs,integrated_lufs,range_lu,true_peak_dbtp,dead_air", one line per second.**
#pragma once
#include "mt01/mt01.hpp"
#include "sw/copy_atomic.hpp"
#include "sw/param.hpp"
#include <array>
#include <string>
#include <vector>

namespace sw::lv23 {

enum ParamId { Preset, Tolerance, kNumParams };
enum PresetId { Arib = 0, Ebu = 1, Stream = 2 };
constexpr int kLogSeconds = 24 * 3600;

struct LogRecord { float momentary, shortTerm, integrated, range, truePeak; unsigned char dead; };
// one row of the log in the ring: the audio thread writes it, the GUI thread reads it for Export log. Every field is an atomic (no data race; a row the audio thread is overwriting
// while it is read may mix two seconds, which is the "one row of the newest second" the export accepts)
struct LogSlot {
    CopyAtomic<float> momentary{0.f}, shortTerm{0.f}, integrated{0.f}, range{0.f}, truePeak{0.f};
    CopyAtomic<unsigned char> dead{0};
    void store(const LogRecord& r) { momentary.store(r.momentary); shortTerm.store(r.shortTerm); integrated.store(r.integrated); range.store(r.range); truePeak.store(r.truePeak); dead.store(r.dead); }
    LogRecord load() const { return LogRecord{momentary.load(), shortTerm.load(), integrated.load(), range.load(), truePeak.load(), dead.load()}; }
};

const std::vector<ParamSpec>& specs();

class Processor {
public:
    Processor();
    void prepare(double sampleRate, int maxBlock);
    void setParam(int id, double plainValue);
    void snapToTargets() {}
    void process(float** ch, int numCh, int n);
    int latencySamples() const { return 0; }
    void reset();
    double momentary() const { return mt_.momentary(); }
    double shortTerm() const { return mt_.shortTerm(); }
    double integrated() const { return mt_.integrated(); }
    double range() const { return mt_.range(); }
    double truePeakDb() const { return mt_.truePeakDb(); }
    double target() const { return mt_.target(); }
    double difference() const { return mt_.difference(); }
    bool inBand() const { return mt_.inBand(); }
    bool deadAirSeen() const { return dead_; }
    bool tpOver() const { return tpOver_; }
    int logCount() const { return static_cast<int>(std::min<long long>(records_.load(), kLogSeconds)); }
    void setStartTime(double unixSeconds) { start_ = unixSeconds; }
    std::string exportCsv() const;

private:
    double fs_ = 48000.0, start_ = 0.0, quiet_ = 0.0, sinceLog_ = 0.0;
    bool prepared_ = false, dead_ = false, tpOver_ = false;
    CopyAtomic<long long> records_{0};   // rows written (the audio thread; read by Export log on the GUI thread)
    std::array<double, kNumParams> target_{};
    mt01::Processor mt_;
    std::vector<LogSlot> log_;
};

}  // namespace sw::lv23
