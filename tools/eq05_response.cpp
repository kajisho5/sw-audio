// Measure SW EQ05 impulse responses for a few settings (low level, Drive 0) -> raw float64 files
// usage: eq05_response <outdir>
#include "eq05/eq05.hpp"
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
using namespace sw::eq05;

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const double fs = 48000.0, amp = 1e-3;
    const int n = 1 << 16;
    const std::vector<std::pair<const char*, std::vector<std::pair<int, double>>>> cases = {
        {"flat", {}},
        {"HF shelf +6 dB @ 8 kHz", {{HfGain, 6}}},
        {"HF bell +6 dB @ 8 kHz", {{HfGain, 6}, {HfShape, 1}}},
        {"HMF -9 dB @ 2 kHz Q 2", {{HmfGain, -9}, {HmfQ, 2}}},
        {"LMF +6 dB @ 600 Hz Q 0.7", {{LmfGain, 6}, {LmfQ, 0.7}}},
        {"LF shelf +6 dB @ 100 Hz", {{LfGain, 6}}},
        {"HPF 80 Hz (18 dB/oct)", {{Hpf, 80}}},
        {"LPF 12 kHz (12 dB/oct)", {{Lpf, 12000}}},
    };
    FILE* idx = std::fopen((out + "/index.txt").c_str(), "w");
    int k = 0;
    for (const auto& c : cases) {
        Processor p; p.prepare(fs, n); p.setParam(Drive, 0);
        for (const auto& s : c.second) p.setParam(s.first, s.second);
        p.snapToTargets();
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        l[0] = r[0] = static_cast<float>(amp);
        float* ch[2] = {l.data(), r.data()};
        p.process(ch, 2, n);
        std::vector<double> d(l.begin(), l.end());
        for (double& v : d) v /= amp;
        const std::string f = out + "/ir" + std::to_string(k) + ".f64";
        FILE* fp = std::fopen(f.c_str(), "wb"); std::fwrite(d.data(), 8, d.size(), fp); std::fclose(fp);
        std::fprintf(idx, "ir%d.f64\t%s\n", k++, c.first);
    }
    std::fclose(idx);
}
