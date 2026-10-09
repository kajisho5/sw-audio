#!/usr/bin/env python3
"""Dump every product's ParamSpec table (and the adapter traits) to ui/specs.json.
usage: tools/dump_specs.py   (needs g++; compiles products/*/*.cpp once, about a minute)"""
import json, os, re, subprocess, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
prods = sorted(d for d in os.listdir(root / "products") if (root / "products" / d).is_dir())
def expr(p, name):
    t = (root / f"plugin/clap/{p}_clap.cpp").read_text()
    m = re.search(rf"constexpr int {name} = ([^;]+);", t)
    return m.group(1).strip() if m else "-1"
tmp = pathlib.Path("/tmp/sw_dump"); tmp.mkdir(exist_ok=True)
src = ['#include <cstdio>', '#include <string>', '#include "sw/param.hpp"']
src += [f'#include "{p}/{p}.hpp"' for p in prods]
src += ['''
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\\\') { o += '\\\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; } return o; }
static const char* curve(sw::Curve c) { switch (c) { case sw::Curve::Lin: return "lin"; case sw::Curve::Log: return "log"; case sw::Curve::Skew: return "skew"; case sw::Curve::Step: return "step"; case sw::Curve::Fader: return "fader"; default: return "symlog"; } }
static void dump(const char* code, const std::vector<sw::ParamSpec>& v, bool first) {
    std::printf("%s\\"%s\\":[", first ? "" : ",", code);
    for (size_t i = 0; i < v.size(); ++i) { const auto& p = v[i];
        std::printf("%s{\\"id\\":\\"%s\\",\\"name\\":\\"%s\\",\\"min\\":%.10g,\\"max\\":%.10g,\\"def\\":%.10g,\\"curve\\":\\"%s\\",\\"skew\\":%.10g,\\"unit\\":\\"%s\\",\\"auto\\":%s,\\"rev\\":%s,\\"maxLabelNorm\\":%.6g", i ? "," : "", esc(p.id).c_str(), esc(p.name).c_str(), p.min, p.max, p.def, curve(p.curve), p.skew, esc(p.unit).c_str(), p.automatable ? "true" : "false", p.reversed ? "true" : "false", p.maxLabelNorm);
        if (p.minLabel) std::printf(",\\"minLabel\\":\\"%s\\"", esc(p.minLabel).c_str());
        if (p.maxLabel) std::printf(",\\"maxLabel\\":\\"%s\\"", esc(p.maxLabel).c_str());
        if (!p.steps.empty()) { std::printf(",\\"steps\\":["); for (size_t k = 0; k < p.steps.size(); ++k) std::printf("%s%.10g", k ? "," : "", p.steps[k]); std::printf("]"); }
        if (!p.labels.empty()) { std::printf(",\\"labels\\":["); for (size_t k = 0; k < p.labels.size(); ++k) std::printf("%s\\"%s\\"", k ? "," : "", esc(p.labels[k]).c_str()); std::printf("]"); }
        std::printf("}"); }
    std::printf("]");
}
static void dumpCurves(const char* code, const std::vector<sw::ParamSpec>& v, bool first) {
    static const double xs[] = {0.0, 0.13, 0.5, 0.77, 1.0};
    std::printf("%s\\"%s\\":[", first ? "" : ",", code);
    for (size_t i = 0; i < v.size(); ++i) { std::printf("%s[", i ? "," : ""); for (int k = 0; k < 5; ++k) { const double val = v[i].toValue(xs[k]); std::printf("%s[%.12g,%.12g]", k ? "," : "", val, v[i].toNorm(val)); } std::printf("]"); }
    std::printf("]");
}
int main() { std::printf("{\\"specs\\":{"); bool first = true;''']
src += [f'    dump("{p}", sw::{p}::specs(), first); first = false;' for p in prods]
src += ['    std::printf("},\\"curves\\":{"); first = true;']
src += [f'    dumpCurves("{p}", sw::{p}::specs(), first); first = false;' for p in prods]
src += ['    std::printf("},\\"traits\\":{");']
for i, p in enumerate(prods):
    src.append(f'    std::printf("%s\\"{p}\\":{{\\"output\\":%d,\\"in\\":%d,\\"mix\\":%d}}", {i and 1 or 0} ? "," : "", (int)({expr(p, "kOutputParam")}), (int)({expr(p, "kInParam")}), (int)({expr(p, "kMixParam")}));')
src += ['    std::printf("}}\\n"); return 0; }']
(tmp / "dump.cpp").write_text("\n".join(src))
inc = ["-I" + str(root / "core/include"), "-I" + str(root / "products")]
objs = []
for p in prods:
    o = tmp / f"{p}.o"
    if not o.exists() or o.stat().st_mtime < (root / f"products/{p}/{p}.cpp").stat().st_mtime:
        subprocess.run(["g++", "-std=c++17", "-O0", "-c", *inc, str(root / f"products/{p}/{p}.cpp"), "-o", str(o)], check=True)
    objs.append(str(o))
subprocess.run(["g++", "-std=c++17", "-O0", *inc, str(tmp / "dump.cpp"), *objs, "-pthread", "-o", str(tmp / "dump")], check=True)
out = json.loads(subprocess.run([str(tmp / "dump")], capture_output=True, text=True, check=True).stdout)
specs, traits, curves = out["specs"], out["traits"], out["curves"]
for p in prods:
    t = (root / f"plugin/clap/{p}_clap.cpp").read_text()
    traits[p].update({"autoGain": "kAutoGain = false" not in t, "delta": "kDelta = false" not in t, "bypass": traits[p].get("in", -1) < 0, "sidechain": "processWithSidechain" in (root / f"products/{p}/{p}.hpp").read_text()})
(root / "ui").mkdir(exist_ok=True)
(root / "ui/specs.json").write_text(json.dumps({"specs": specs, "traits": traits}, ensure_ascii=False, indent=None, separators=(",", ":")))
(root / "tests/ui").mkdir(parents=True, exist_ok=True)
(root / "tests/ui/curve_samples.json").write_text(json.dumps(curves, separators=(",", ":")))
print(len(specs), "products,", sum(len(v) for v in specs.values()), "parameters")
