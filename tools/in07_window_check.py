#!/usr/bin/env python3
"""SWINGBY: the real window page against the real plug-in, in Chromium (no platform view needed). tools/in07_window_host.cpp is the native
side: the page it writes is loaded as the plug-in's web view would load it, and every message the page posts goes to it; its answers are
evaluated in the page. Checks: the page boots without errors, the pictures and fonts arrive through the plug-in, a factory preset loads,
a value edit reaches the plug-in, a preset is saved to (and listed from) the user folder, the settings are kept, the FX order moves;
screenshots of every screen in build/in07_window/.
usage: tools/in07_window_check.py [build dir (build-cmake)]"""
import json, os, pathlib, subprocess, sys, tempfile, time
from playwright.sync_api import sync_playwright

REPO = pathlib.Path(__file__).resolve().parents[1]
B = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else REPO / 'build-cmake').resolve()
OUT = REPO / 'build/in07_window'
OUT.mkdir(parents=True, exist_ok=True)
exe = OUT / 'in07_window_host'
clap_inc = next((B / '_deps').glob('clap-src/include'))
cmd = ['g++', '-std=c++17', '-O1', f'-I{REPO}/core/include', f'-I{REPO}/products', f'-I{REPO}/plugin/clap', f'-I{B}/gen', f'-I{clap_inc}',
       str(REPO / 'tools/in07_window_host.cpp'), *map(str, (REPO / 'products/in07').glob('*.cpp')), str(REPO / 'core/src/license.cpp'), str(REPO / 'core/src/license_state.cpp'),
       *map(str, (REPO / 'core/third_party/monocypher').glob('*.c')), str(REPO / 'plugin/clap/gui_none.cpp'), '-o', str(exe)]
srcs = [REPO / 'tools/in07_window_host.cpp', B / 'gen/in07_ui_assets.hpp', REPO / 'plugin/clap/instrument_adapter.hpp', REPO / 'plugin/clap/inst_gui.hpp']
if not exe.exists() or any(s.stat().st_mtime > exe.stat().st_mtime for s in srcs):
    print('building the window host ...', flush=True)
    subprocess.run(cmd, check=True)

tmp = tempfile.mkdtemp(prefix='sw_window_')
env = dict(os.environ, XDG_DATA_HOME=tmp)
page_file = OUT / 'page.html'
host = subprocess.Popen([str(exe), str(page_file)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env, bufsize=1)
assert host.stdout.readline().strip() == 'ready'
log = []


def native(m):
    host.stdin.write(m + '\n'); host.stdin.flush()
    s = json.loads(host.stdout.readline())
    log.append((m[:40], len(s)))
    return s


fails = []
def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what)
    if not cond: fails.append(what)


with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={'width': 1280, 'height': 860})
    errors = []
    pg.on('pageerror', lambda e: errors.append(str(e)))
    pg.on('console', lambda m: errors.append('console: ' + m.text) if m.type == 'error' else None)
    pg.expose_function('swNative', native)
    # the platform's bridge: chrome.webview.postMessage (WebView2) -> the plug-in -> the script it answers with, evaluated in the page
    pg.add_init_script("window.chrome = window.chrome || {}; window.chrome.webview = { postMessage: m => window.swNative(m).then(s => { if (s) (0, eval)(s); }) };")
    pg.goto(page_file.as_uri())   # init scripts run on a navigation (set_content would skip them)
    pg.wait_for_timeout(2500)
    n_assets = pg.evaluate('SW.boot.assets.length')
    got = pg.evaluate('Object.keys(SW.img).length')
    fonts = pg.evaluate('[...document.fonts].filter(f => f.status === "loaded").map(f => f.family + " " + f.weight)')
    check(got >= n_assets - 7, f'pictures through the plug-in: {got} of {n_assets - 7}')
    check(len(fonts) >= 7, f'fonts through the plug-in: {len(fonts)}')
    check(pg.evaluate('SW.licence.machine && SW.licence.machine.length === 64'), 'licence: this computer\'s code')
    # a factory preset
    pg.evaluate('SW.loadPreset(SW.presets.factory[5])')
    pg.wait_for_timeout(300)
    check(pg.evaluate('SW.current.kind === "factory" && SW.current.index === 5'), 'factory preset 6 loaded')
    check(pg.evaluate('SW.P.stepIndex("in07.preset") === 6'), 'the selector follows')
    # a value from the page reaches the plug-in and comes back
    pg.evaluate('SW.P.tap("in07.l1.flt.cutoff", 777)')
    pg.wait_for_timeout(500)
    v = pg.evaluate('SW.P.get("in07.l1.flt.cutoff")')
    check(abs(v - 777) < 1e-6, f'cutoff edit round trip: {v}')
    # save, list
    pg.evaluate('SW.openSave()')
    pg.fill('.dialog input[aria-label="Name"]', 'Window Check')
    pg.click('.dialog button.btn.on')
    pg.wait_for_timeout(400)
    check(pg.evaluate('SW.current.kind === "user" && SW.current.name === "Window Check"'), 'saved as a user preset')
    check(pg.evaluate('SW.presets.user.some(u => u.name === "Window Check")'), 'listed from the user folder')
    check((pathlib.Path(tmp) / 'SEVENTHWELL/SWINGBY/Presets/Window Check.swpreset').exists(), 'the file is in the user folder')
    pg.evaluate('document.querySelectorAll(".scrim").forEach(x => x.remove())')
    # settings
    pg.evaluate('SW.setSetting("theme", "light")')
    pg.evaluate('SW.setSetting("motion", "off")')
    pg.wait_for_timeout(200)
    wt = pathlib.Path(tmp) / 'SEVENTHWELL/SWINGBY/window.txt'
    check(wt.exists() and 'theme=light' in wt.read_text() and 'motion=off' in wt.read_text(), 'settings kept by the plug-in')
    # FX order
    pg.evaluate('SW.go("fx")')
    pg.click('button[aria-label="Move DRIVE later"]')
    pg.wait_for_timeout(400)
    check(pg.evaluate('[0,1].map(i => SW.P.stepIndex("in07.fx.slot" + (i + 1)))') == [1, 0], 'FX order moved in the plug-in')
    for theme in ('dark', 'light'):
        pg.evaluate('t => SW.setSetting("theme", t)', theme)
        for s in ('play', 'layer', 'arp', 'mod', 'fx'):
            pg.evaluate('s => SW.go(s)', s)
            pg.wait_for_timeout(300)
            pg.screenshot(path=str(OUT / f'{s}_{theme}.png'))
    check(not errors, 'no page errors' + ('' if not errors else ': ' + '; '.join(errors[:5])))
    pg.goto('about:blank')   # the page's poll stops before the browser does
    pg.wait_for_timeout(200)
    br.close()
host.stdin.close(); host.wait(timeout=10)
print('messages:', len(log), ' largest answer:', max(n for _, n in log), 'bytes')
if os.environ.get('SW_WINDOW_LOG'): print([x for x in log if not x[0].startswith('p')][:40])
sys.exit(1 if fails else 0)
