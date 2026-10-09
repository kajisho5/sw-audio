#!/usr/bin/env python3
"""SWINGBY: the plug-in window (ui/in07) in a browser, without the plug-in: a mock of the native side (values stay in the page,
presets from the boot data, a fictional user folder, a trial licence) and screenshots of every screen, dark and light.
usage: tools/in07_ui_preview.py [out dir] [--motion 60|30|off] [--screens play,layer,...] [--interact]
Needs build/in07_ui_data (tools/in07_ui_data.cpp) and Playwright (Chromium)."""
import json
import pathlib
import subprocess
import sys

from playwright.sync_api import sync_playwright

REPO = pathlib.Path(__file__).resolve().parents[1]
UI = REPO / 'ui/in07'
R = REPO / 'docs/design/design-system/project/assets'
# the pictures, fonts and scripts the window uses: the same list the plug-in embeds (ui/in07/manifest.json, tools/embed_in07_ui.py)
MANIFEST = json.loads((UI / 'manifest.json').read_text())
ASSETS = {k: REPO / v for k, v in MANIFEST['assets'].items()}
SCRIPTS = [str(pathlib.Path(s).relative_to('ui/in07')) for s in MANIFEST['scripts']]
MOCK = r'''
window.SWMOCK = function (m) {
  const SW = window.SW, parts = m.split(' ');
  const later = f => setTimeout(f, 10);
  if (parts[0] === 'n') { SW.info.note = (SW.info.note || 0) + 1; SW.info.held = (SW.info.held || 0) + 1; return; }
  if (parts[0] === 'o') { SW.info.held = Math.max(0, (SW.info.held || 0) - 1); return; }
  if (parts[0] === 'p') { later(() => SW.update(SW.P.values().slice(), { bpm: 128, playing: false, beat: 0, note: SW.info.note, held: SW.info.held || 0, learn: window.MOCK_LEARN, cc: window.MOCK_CC })); return; }
  if (parts[0] !== 'c') return;
  const name = parts[1], arg = parts[2];
  if (name === 'learn') { window.MOCK_LEARN = Number(arg); return; }
  if (name === 'forget') { window.MOCK_CC[Number(arg)] = -1; return; }
  if (name === 'users') later(() => SW.reply('users', { folder: 'C:\\Users\\you\\Documents\\SEVENTHWELL\\SWINGBY\\Presets', list: window.MOCK_USERS }));
  if (name === 'lic') later(() => SW.reply('licence', window.MOCK_LIC));
  if (name === 'factory') later(() => {
    const i = Number(arg), v = i >= 0 ? window.PRESET_VALUES[i] : window.INIT_VALUES;
    if (v) { const sel = SW.P.idx('in07.preset'); const vv = v.slice(); vv[sel] = i + 1; SW.update(vv); }
    SW.reply('loaded', { kind: i >= 0 ? 'factory' : 'init', index: i });
  });
  if (name === 'user') later(() => { const p = SW.unb64(arg), u = window.MOCK_USERS.find(x => x.path === p); SW.reply('loaded', { kind: 'user', path: p, name: u ? u.name : '?', category: u ? u.category : '' }); });
  if (name === 'save') later(() => { const d = JSON.parse(SW.unb64(arg)); if (!d.overwrite && window.MOCK_USERS.some(u => u.name === d.name)) { SW.reply('saved', { exists: true }); return; }
    const path = '/presets/' + d.name + '.swpreset'; window.MOCK_USERS = window.MOCK_USERS.filter(u => u.name !== d.name).concat([{ name: d.name, category: d.category, author: d.author, path }]);
    SW.reply('saved', { ok: true, name: d.name, category: d.category, path }); });
  if (name === 'licfile') later(() => { window.MOCK_LIC = Object.assign({}, window.MOCK_LIC, { state: 'licensed', id: 'SW-PREVIEW', message: 'Activated', ok: true }); SW.reply('licence', window.MOCK_LIC); });
};
window.MOCK_LEARN = -1; window.MOCK_CC = [21, -1, -1, -1, 74, -1, -1, -1];
window.MOCK_USERS = [{ name: 'Night Drive Bass', category: 'BASS', author: 'You', path: '/presets/Night Drive Bass.swpreset' }, { name: 'Glass Steps', category: 'SEQ', author: 'You', path: '/presets/Glass Steps.swpreset' }];
window.MOCK_LIC = { state: 'demo', machine: '3f9c2a51d7e04b6c8a1f5e2d9b7c4a6e0f3d8c1b2a5e7f9d4c6b8a0e1f2d3c4b' };
'''


def build_page(out, motion, theme):
    data_tool = REPO / 'build/in07_ui_data'
    boot = json.loads(subprocess.run([str(data_tool), '0'], capture_output=True, check=True).stdout)
    init = json.loads(subprocess.run([str(data_tool), '-1'], capture_output=True, check=True).stdout)['values']
    values = []
    for i in range(len(boot['presets'])):
        values.append(json.loads(subprocess.run([str(data_tool), str(i)], capture_output=True, check=True).stdout)['values'])
    boot.update({'settings': {'motion': motion, 'theme': theme, 'zoom': '100'}, 'licence': {'state': 'demo'}, 'assets': [],
                 'assetUrls': {k: v.resolve().as_uri() for k, v in ASSETS.items()}, 'server': '', 'version': 'preview'})
    css = (UI / 'in07.css').read_text()
    page = ['<!doctype html><html lang="en"><head><meta charset="utf-8"><title>SWINGBY preview</title><style>', css, '</style></head><body>',
            '<div id="sw" class="sw-dark"></div><script>window.SWBOOT=', json.dumps(boot), ';window.PRESET_VALUES=', json.dumps(values),
            ';window.INIT_VALUES=', json.dumps(init), ';</script><script>', MOCK, '</script>']
    for s in SCRIPTS:
        f = UI / s
        if f.exists():
            page += ['<script>', f.read_text(), '</script>']
    page.append('</body></html>')
    path = out / 'preview.html'
    path.write_text(''.join(page))
    return path


def main():
    args = sys.argv[1:]
    out = pathlib.Path(args[0] if args and not args[0].startswith('--') else REPO / 'build/in07_ui').resolve()
    out.mkdir(parents=True, exist_ok=True)
    motion = args[args.index('--motion') + 1] if '--motion' in args else 'off'
    screens = (args[args.index('--screens') + 1] if '--screens' in args else 'play,layer,arp,mod,fx').split(',')
    page = build_page(out, motion, 'dark')
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--allow-file-access-from-files'])
        pg = br.new_page(viewport={'width': 1280, 'height': 860}, device_scale_factor=1)
        errors = []
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.on('console', lambda m: errors.append('console: ' + m.text) if m.type == 'error' else None)
        pg.goto(page.as_uri())
        pg.wait_for_timeout(1500)
        pg.evaluate('document.fonts.ready')
        for theme in ('dark', 'light'):
            pg.evaluate('t => SW.setSetting("theme", t)', theme)
            for s in screens:
                pg.evaluate('s => SW.go(s)', s)
                pg.wait_for_timeout(400)
                pg.screenshot(path=str(out / f'{s}_{theme}.png'))
        if '--interact' in args:
            pg.evaluate('t => SW.setSetting("theme", "dark")', 'dark')
            pg.evaluate('SW.go("play")')
            pg.wait_for_timeout(200)
        print('errors:', errors[:10])
        br.close()


if __name__ == '__main__':
    main()
