#!/usr/bin/env python3
"""SWINGBY (SW IN07) browser trial: the plug-in's own window (ui/in07) and engine (products/in07, as WebAssembly in an AudioWorklet) in a
web page. Nothing to install; computer keys or a MIDI keyboard play it. Saving presets, user presets and the licence stay the plug-in's.
  tools/in07_web.py <out dir> [--check]
writes <out>/index.html, worklet.js, swingby.wasm, assets/* (serve the folder over http(s): browsers do not run a worklet from file://),
and <out>/artifact.html: the same page without its document skeleton (a claude.ai artifact wraps one around it).
--check: serves it, opens it in Chromium (Playwright), starts the audio, plays a note and a preset, and reads the engine's level back.
Needs: python -m ziglang (pip install ziglang: the C++ compiler for wasm32-wasi), build/in07_ui_data (built here), Playwright for --check."""
import http.server, json, pathlib, shutil, socketserver, subprocess, sys, threading

REPO = pathlib.Path(__file__).resolve().parents[1]
UI = REPO / 'ui/in07'
WEB = REPO / 'tools/in07_web'
MANIFEST = json.loads((UI / 'manifest.json').read_text())
SOURCES = ['tools/in07_web/swingby_wasm.cpp', 'products/in07/in07.cpp', 'products/in07/osc.cpp', 'products/in07/presets.cpp', 'products/in07/presets_more.cpp']


def build_wasm(dst):
    cmd = [sys.executable, '-m', 'ziglang', 'c++', '--target=wasm32-wasi', '-mexec-model=reactor', '-msimd128', '-O2', '-std=c++17',
           '-fno-exceptions', '-DSW_NO_FILES', '-Icore/include', '-Iproducts', *SOURCES, '-Wl,--strip-all', '-o', str(dst)]
    subprocess.run(cmd, cwd=REPO, check=True)


def ui_data():
    exe = REPO / 'build/in07_ui_data'
    (REPO / 'build').mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(REPO / 'tools/embed_ui.py'), str(REPO / 'build/gui_assets.hpp')], cwd=REPO, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['g++', '-std=c++17', '-O1', '-Icore/include', '-Iproducts', '-Iplugin/clap', '-Ibuild', 'tools/in07_ui_data.cpp',
                    *map(str, sorted((REPO / 'products/in07').glob('*.cpp'))), *map(str, sorted((REPO / 'core/src').glob('*.cpp'))),
                    *map(str, sorted((REPO / 'core/third_party/monocypher').glob('*.c'))), '-o', str(exe)], cwd=REPO, check=True)
    run = lambda i: json.loads(subprocess.run([str(exe), str(i)], capture_output=True, check=True).stdout)
    boot = run(0)
    return boot, run(-1)['values'], [run(i)['values'] for i in range(len(boot['presets']))]


PAGE_CSS = r'''
/* the plug-in's own window under a bar: dark first (the window's night look); the day look with the system's light setting, the page's light
   choice, or the window's own sun button (body.sw-light) */
:root { --bar-bg: #05080a; --bar-ink: #cfe6dd; --bar-sub: #7d978e; --bar-acc: #8de0c3; --bar-line: rgba(141,224,195,.18); --page: #030405; --veil: rgba(3,4,5,.72); color-scheme: dark; }
@media (prefers-color-scheme: light) { :root:not([data-theme="dark"]) { --bar-bg: #e2ece8; --bar-ink: #0b3a2b; --bar-sub: #4f6b62; --bar-acc: #0b5a41; --bar-line: rgba(11,90,65,.2); --page: #e2ece8; --veil: rgba(226,236,232,.8); color-scheme: light; } }
:root[data-theme="light"] { --bar-bg: #e2ece8; --bar-ink: #0b3a2b; --bar-sub: #4f6b62; --bar-acc: #0b5a41; --bar-line: rgba(11,90,65,.2); --page: #e2ece8; --veil: rgba(226,236,232,.8); color-scheme: light; }
body.sw-light { --bar-bg: #e2ece8; --bar-ink: #0b3a2b; --bar-sub: #4f6b62; --bar-acc: #0b5a41; --bar-line: rgba(11,90,65,.2); --page: #e2ece8; --veil: rgba(226,236,232,.8); }
body.sw-dark-page { --bar-bg: #05080a; --bar-ink: #cfe6dd; --bar-sub: #7d978e; --bar-acc: #8de0c3; --bar-line: rgba(141,224,195,.18); --page: #030405; --veil: rgba(3,4,5,.72); }
html, body { height: auto; min-height: 100%; overflow-x: hidden; overflow-y: auto; }
body { margin: 0; padding-inline: 16px; background: var(--page); color: var(--bar-ink); font-family: 'Barlow Condensed', 'Arial Narrow', sans-serif; }
#bar { display: flex; flex-wrap: wrap; align-items: center; gap: 10px 18px; padding: 10px 16px; border-bottom: 1px solid var(--bar-line); background: var(--bar-bg); font-size: 14px; letter-spacing: .06em; }
#bar b { font-family: Michroma, sans-serif; font-weight: 400; letter-spacing: .2em; color: var(--bar-acc); font-size: 13px; }
#bar .sub { color: var(--bar-sub); }
#bar .grow { flex: 1; }
#bar button, #bar input { font: inherit; color: var(--bar-ink); background: transparent; border: 1px solid var(--bar-line); border-radius: 999px; padding: 6px 14px; }
#bar button { cursor: pointer; }
#bar input { width: 64px; text-align: center; }
#bar .meter { width: 80px; height: 6px; border-radius: 3px; background: var(--bar-line); overflow: hidden; }
#bar .meter i { display: block; height: 100%; width: 0; background: var(--bar-acc); }
#stage { position: relative; margin: 0 auto; }
#start { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 16px; background: var(--veil); z-index: 100; text-align: center; padding: 16px; }
#bar { margin-inline: -16px; }
#bar button:focus-visible, #bar input:focus-visible, #start button:focus-visible { outline: 2px solid var(--bar-acc); outline-offset: 2px; }
#start button { font-family: Michroma, sans-serif; font-size: 18px; letter-spacing: .25em; padding: 18px 34px; border-radius: 999px; border: 1px solid var(--bar-acc); color: var(--bar-ink); background: transparent; cursor: pointer; }
#start p { margin: 0; max-width: 560px; font-size: 17px; line-height: 1.6; color: var(--bar-ink); }
'''

PAGE_JS = r'''
(function () {
  const W = window.SWWEB, app = document.getElementById('sw'), stage = document.getElementById('stage');
  const fit = () => {   // the window (1280 x 860) to the page's width
    const z = Math.min(1, (document.documentElement.clientWidth - 32) / 1280);
    app.style.transformOrigin = '0 0'; app.style.transform = z === 1 ? '' : 'scale(' + z + ')';
    stage.style.width = (1280 * z) + 'px'; stage.style.height = (860 * z) + 'px';
  };
  window.addEventListener('resize', fit); fit();
  const go = document.getElementById('go'), cover = document.getElementById('start'), st = document.getElementById('state');
  go.addEventListener('click', async () => {
    go.disabled = true; go.textContent = 'STARTING…';
    try { await W.start(); cover.remove(); st.textContent = 'AUDIO ON'; }
    catch (e) { go.textContent = 'COULD NOT START'; st.textContent = String(e && e.message || e); W.started = false; go.disabled = false; }
  });
  document.getElementById('midi').addEventListener('click', async ev => { ev.target.textContent = await W.midi(); });
  const bpm = document.getElementById('bpm');
  bpm.addEventListener('change', () => { const v = Number(bpm.value); if (v >= 30 && v <= 300) W.tempo(v); else bpm.value = W.bpm; });
  const oct = document.getElementById('oct');
  W.onOctave = o => { oct.textContent = 'C' + o; };
  const bar = document.querySelector('#bar .meter i');
  setInterval(() => { bar.style.width = Math.min(100, Math.round(100 * Math.sqrt(W.peak || 0))) + '%'; }, 120);
  const theme = () => document.body.classList.toggle('sw-dark-page', !document.body.classList.contains('sw-light'));
  new MutationObserver(theme).observe(document.body, { attributes: true, attributeFilter: ['class'] }); theme();
})();
'''


def build(out):
    out.mkdir(parents=True, exist_ok=True)
    wasm = REPO / 'build/in07_web/swingby.wasm'
    wasm.parent.mkdir(parents=True, exist_ok=True)
    newest = max((REPO / s).stat().st_mtime for s in SOURCES + ['core/include/sw/simd2.hpp', 'products/in07/in07.hpp', 'products/in07/fx.hpp', 'products/in07/osc.hpp', 'products/in07/presets.hpp'])
    if not wasm.exists() or wasm.stat().st_mtime < newest:
        build_wasm(wasm)
    shutil.copy(wasm, out / 'swingby.wasm')
    shutil.copy(WEB / 'worklet.js', out / 'worklet.js')
    (out / 'assets').mkdir(exist_ok=True)
    urls = {}
    for name, rel in MANIFEST['assets'].items():
        src = REPO / rel
        shutil.copy(src, out / 'assets' / src.name)
        urls[name] = 'assets/' + src.name
    boot, init, values = ui_data()
    boot.update({'settings': {'motion': '60', 'theme': 'dark', 'zoom': '100'}, 'licence': {'state': 'demo'}, 'assets': [], 'assetUrls': urls,
                 'server': '', 'version': 'web'})
    css = (UI / 'in07.css').read_text()
    scripts = [(UI / pathlib.Path(s).relative_to('ui/in07')).read_text() for s in MANIFEST['scripts'] if (UI / pathlib.Path(s).relative_to('ui/in07')).exists()]
    fonts = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
             '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@300;400;500;600&family=Michroma&family=Space+Mono:wght@400;700&display=swap">')
    head = ['<title>SWINGBY Browser Trial</title>', fonts, '<style>', css, PAGE_CSS, '</style>']   # the faces the window ships, from a second source too
    page = [
            '<div id="bar"><b>SWINGBY</b><span class="sub">BROWSER TRIAL · the plug-in\'s own engine and window</span><span class="grow"></span>',
            '<span class="sub">KEYS</span><span>A–K · Z / X octave <span id="oct">C4</span></span>',
            '<span class="sub">BPM</span><input id="bpm" type="number" min="30" max="300" value="120" aria-label="Tempo">',
            '<button id="midi" type="button">USE MIDI KEYBOARD</button><span class="meter" aria-hidden="true"><i></i></span><span id="state" class="sub">AUDIO OFF</span></div>',
            '<div id="stage"><div id="sw" class="sw-dark"></div>',
            '<div id="start"><button id="go" type="button">START AUDIO</button>',
            '<p>SWINGBY runs here in your browser: the same engine and window as the plug-in. Play with the computer keys (A to K, Z / X for the '
            'octave), the ribbon, or a MIDI keyboard. Saving presets is the plug-in\'s.</p></div></div>',
            '<script>window.SWBOOT=', json.dumps(boot), ';window.PRESET_VALUES=', json.dumps(values), ';window.INIT_VALUES=', json.dumps(init), ';</script>',
            '<script>', (WEB / 'host.js').read_text(), '</script>']
    for s in scripts:
        page += ['<script>', s, '</script>']
    page += ['<script>', PAGE_JS, '</script>']
    full = ['<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">']
    (out / 'index.html').write_text(''.join(full + head + ['</head><body>'] + page + ['</body></html>']))
    (out / 'artifact.html').write_text(''.join(head + page))   # for a claude.ai artifact (it adds the skeleton)
    total = sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
    print(f'{out}: index.html {(out / "index.html").stat().st_size / 1e6:.2f} MB, swingby.wasm {(out / "swingby.wasm").stat().st_size / 1e3:.0f} kB, '
          f'{len(urls)} assets; {total / 1e6:.2f} MB in all')


def check(out):
    from playwright.sync_api import sync_playwright
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **k): super().__init__(*a, directory=str(out), **k)
        def log_message(self, *a): pass
    srv = socketserver.TCPServer(('127.0.0.1', 0), Quiet)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{srv.server_address[1]}/index.html'
    ok = True
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--autoplay-policy=no-user-gesture-required'])
        pg = br.new_page(viewport={'width': 1300, 'height': 960})
        errors = []
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.on('console', lambda m: errors.append('console: ' + m.text) if m.type == 'error' else None)
        pg.goto(url)
        pg.wait_for_timeout(1500)
        pg.click('#go')
        pg.wait_for_function('window.SWWEB.node !== null', timeout=20000)
        pg.wait_for_timeout(300)
        silent = pg.evaluate('window.SWWEB.peak')
        pg.keyboard.down('a'); pg.wait_for_timeout(800)
        peak = pg.evaluate('window.SWWEB.peak'); voices = pg.evaluate('window.SWWEB.voices')
        pg.keyboard.up('a'); pg.wait_for_timeout(1500)
        pg.evaluate('SW.loadPreset(SW.presets.factory[6])'); pg.wait_for_timeout(300)   # Polar Bass
        pg.keyboard.down('d'); pg.wait_for_timeout(600)
        peak2 = pg.evaluate('window.SWWEB.peak')
        pg.keyboard.up('d')
        title = pg.evaluate('document.querySelector(".disp").textContent')
        print(f'started; peak before a key {silent:.3f}, holding A (C4) {peak:.3f} with {voices} voice(s); after loading preset 7 ({title}), E4 {peak2:.3f}')
        print('errors:', errors[:5])
        ok = peak > 0.01 and peak2 > 0.01 and not errors
        pg.screenshot(path=str(out.parent / 'web_check.png'))
        br.close()
    srv.shutdown()
    if not ok:
        sys.exit('the browser trial did not play')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    o = pathlib.Path(sys.argv[1]).resolve()
    build(o)
    if '--check' in sys.argv:
        check(o)
