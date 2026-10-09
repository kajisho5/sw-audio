#!/usr/bin/env python3
"""SWINGBY: the plug-in window as a web page to try in a browser (the "SWINGBY Window Preview" artifact): the real page (ui/in07) with the
preview's mock of the native side (tools/in07_ui_preview.py: values stay in the page, presets from the boot data, a fictional user folder,
a trial licence; no sound), the pictures and fonts as files next to it, scaled to fit the browser window.
usage: tools/in07_ui_artifact.py <out dir>     -> <out dir>/index.html and <out dir>/assets/*
Needs build/in07_ui_data (tools/in07_ui_data.cpp)."""
import json, pathlib, shutil, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from in07_ui_preview import ASSETS, MOCK, REPO, SCRIPTS, UI  # noqa: E402

out = pathlib.Path(sys.argv[1]).resolve()
(out / 'assets').mkdir(parents=True, exist_ok=True)
data_tool = REPO / 'build/in07_ui_data'
boot = json.loads(subprocess.run([str(data_tool), '0'], capture_output=True, check=True).stdout)
init = json.loads(subprocess.run([str(data_tool), '-1'], capture_output=True, check=True).stdout)['values']
values = [json.loads(subprocess.run([str(data_tool), str(i)], capture_output=True, check=True).stdout)['values'] for i in range(len(boot['presets']))]
urls = {}
for name, src in ASSETS.items():
    dst = out / 'assets' / (name + src.suffix)
    shutil.copy2(src, dst)
    urls[name] = 'assets/' + dst.name
boot.update({'settings': {'motion': '60', 'theme': 'dark', 'zoom': '100'}, 'licence': {'state': 'demo'}, 'assets': [], 'assetUrls': urls,
             'server': '', 'version': 'preview'})
FIT = """<style>
html, body { background: #030405; }
#fit { position: absolute; left: 0; top: 0; width: 1280px; height: 860px; transform-origin: 0 0; }
.pv-note { position: fixed; right: 10px; bottom: 8px; z-index: 100; font: 11px/1.3 'Barlow Condensed', 'Arial Narrow', sans-serif; letter-spacing: .12em;
  color: #8e978f; background: rgba(3,4,5,.7); padding: 4px 8px; border-radius: 6px; pointer-events: none; transition: opacity 1s; }
.pv-note.gone { opacity: 0; }
</style>
<div id="fit"><div id="sw" class="sw-dark"></div></div>
<div class="pv-note">PREVIEW · 音は出ません · 値はこのページの中だけ</div>
<script>
(function () {
  const f = document.getElementById('fit');
  const fit = () => {
    const k = Math.min(window.innerWidth / 1280, window.innerHeight / 860);
    f.style.transform = 'scale(' + k + ')';
    f.style.left = Math.max(0, (window.innerWidth - 1280 * k) / 2) + 'px';
    f.style.top = Math.max(0, (window.innerHeight - 860 * k) / 2) + 'px';
  };
  window.addEventListener('resize', fit);
  fit();
  setTimeout(() => { const n = document.querySelector('.pv-note'); if (n) n.classList.add('gone'); }, 6000);
})();
</script>"""
page = ['<title>SWINGBY Window Preview</title><style>', (UI / 'in07.css').read_text(), '</style>', FIT,
        '<script>window.SWBOOT=', json.dumps(boot), ';window.PRESET_VALUES=', json.dumps(values), ';window.INIT_VALUES=', json.dumps(init), ';</script>',
        '<script>', MOCK, 'window.MOCK_CC = [-1, -1, -1, -1, -1, -1, -1, -1];</script>']   # nothing learned yet
for s in SCRIPTS:
    page += ['<script>', (UI / s).read_text(), '</script>']
(out / 'index.html').write_text(''.join(page))
print(out / 'index.html', sum(1 for _ in (out / 'assets').iterdir()), 'assets')
