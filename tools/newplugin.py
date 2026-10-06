#!/usr/bin/env python3
"""Add the plugin layer of a finished product: plugin/clap/<code>_clap.cpp, CMake entries, tasks.md check.
usage: tools/newplugin.py dy01 "SW DY01 FET" Dy01 COMPRESSOR "FET character compressor" Output - Mix
  args: code, display name, AU subtype (4 chars), CLAP feature suffix (COMPRESSOR, EQUALIZER, DISTORTION, ...),
        description, Output enum name or -, In enum name or -, Mix enum name or -"""
import re, sys, pathlib
code, name, au, feat, desc, out, inn, mix = sys.argv[1:9]
root = pathlib.Path(__file__).resolve().parent.parent
ns = code.lower(); cls = code.capitalize()
ver = re.search(r"project\(sw-audio VERSION ([\d.]+)", (root / "CMakeLists.txt").read_text()).group(1)
ref = lambda e: "-1" if e == "-" else f"sw::{ns}::{e}"
(root / f"plugin/clap/{ns}_clap.cpp").write_text(f'''// {name} — CLAP plugin traits
#include "clap_adapter.hpp"
#include "{ns}/{ns}.hpp"

namespace {{
struct {cls} {{
    using Core = sw::{ns}::Processor;
    static const std::vector<sw::ParamSpec>& specs() {{ return sw::{ns}::specs(); }}
    static constexpr int kOutputParam = {ref(out)};
    static constexpr int kInParam = {ref(inn)};
    static constexpr int kMixParam = {ref(mix)};
    static const clap_plugin_descriptor_t* descriptor() {{
        static const char* const f[] = {{CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_{feat}, CLAP_PLUGIN_FEATURE_STEREO, nullptr}};
        static const clap_plugin_descriptor_t d = {{CLAP_VERSION_INIT, "com.seventh-well.sw-audio.{ns}", "{name}", "SEVENTHWELL",
                                                   "https://seventh-well.com", "", "", "{ver}", "{desc}", f}};
        return &d;
    }}
}};
}}  // namespace

SW_CLAP_ENTRY({ns}, {cls})
''')
c = (root / "CMakeLists.txt").read_text()
m = re.search(r"set\(SW_PRODUCTS (.*?)\)", c)
if ns not in m.group(1).split():
    c = c.replace(m.group(0), "set(SW_PRODUCTS " + m.group(1) + " " + ns + ")")
line = f'    sw_add_plugin({ns} "{name}" {au})\n'
if line not in c:
    idx = c.rindex("endif()")
    c = c[:idx] + line + c[idx:]
(root / "CMakeLists.txt").write_text(c)
