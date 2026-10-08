#!/usr/bin/env python3
"""Screenshot .dc.html boards without the canvas runtime: a small stand-in expands {{holes}}, sc-for, sc-if and dc-import
in Chromium (Playwright), maps /_blob/ ids to local files, and takes a PNG. Static frame (no animation loop; Orbit gets t).
usage: dc_shot.py <project dir> <out dir> <Board>[:<props json>][=<png name>] ...
  e.g. tools/design/dc_shot.py docs/design/in07/project /tmp/shots SW_Main=play_dark SW_Main_Light=play_light
The fonts (Barlow Condensed, Michroma, Space Mono) are fetched once from Google Fonts into <out dir>/fonts with curl.
Needs: python3 -m pip install playwright (Chromium from Playwright). The pictures are the static first frame, not the canvas runtime itself.
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

from playwright.sync_api import sync_playwright

REPO = pathlib.Path(__file__).resolve().parents[2]
R = REPO / 'docs/design/design-system/project/assets'
BLOB = {
    'eea73410a66867fa6abf11bd1a56eee6': R / 'product-logos/swingby-dark.svg',
    '702f4b752a177d6e1601ccac384e7886': R / 'product-logos/swingby-light.svg',
    '90121e1a7bee5f359e51e024081e48ca': R / 'renders/in07/core-alpha.webp',
    '68aab30db9e061801ca79d72df3ac6e2': R / 'renders/in07/daycore.webp',
    'e6ee8b6569e63ef27cedfc1666d39d47': R / 'renders/in07/bodies/ring.webp',
    '7709cc70a363f53b1b2d78aa4da28c56': R / 'renders/in07/bodies/pearl.webp',
    '8c95e5bce1ae22c9f1954ef07f849f45': R / 'renders/in07/bodies/crater.webp',
    'af099d0a5b96dfa72328a1af6c2cb648': R / 'renders/in07/bodies/crystal.webp',
    'd939afb85ed95fb8e9491d08127f2208': R / 'renders/in07/bodies/lfo.webp',
    'e38db7eb052c9ac25fd71783f0559f60': R / 'renders/in07/bodies/bead.webp',
    'fe87118687e9fb3359a02b9dbc9fdcbd': R / 'renders/in07/fx/fx_drive.webp',
    'ce619443ef6e3e08f54434c374143598': R / 'renders/in07/fx/fx_chorus.webp',
    '043458125b4f450392ec146178f4c7a4': R / 'renders/in07/fx/fx_delay.webp',
    '7ebc58bb2417d398eb1ee5ceacc9c52a': R / 'renders/in07/fx/fx_reverb.webp',
    '56941051af29746414654413d55d0cf3': R / 'renders/in07/fx/fx_eq.webp',
    '4a8c4eb3f89a1d3aaaa204b0dbd11874': R / 'renders/in07/fx/fx_limit.webp',
}

RUNTIME = r'''
const FILES = __FILES__, BLOB = __BLOB__;
class DCLogic { constructor() { this.props = {}; this.state = {}; } setState(o) { this.state = Object.assign({}, this.state, typeof o === 'function' ? o(this.state) : o); } forceUpdate() {} }
const compiled = {};
function compile(name) {
  if (compiled[name]) return compiled[name];
  let src = FILES[name];
  if (!src) throw new Error('no file ' + name);
  // sc-for / sc-if inside <select> would be dropped by the HTML parser: carry them as <template>
  src = src.replace(/<sc-(for|if)\b/g, '<template data-sc="$1"').replace(/<\/sc-(for|if)>/g, '</template>');
  const doc = new DOMParser().parseFromString(src, 'text/html');
  const xdc = doc.querySelector('x-dc');
  const helmet = xdc.querySelector('helmet');
  if (helmet) { helmet.querySelectorAll('style').forEach(s => { const t = document.createElement('style'); t.textContent = s.textContent; document.head.appendChild(t); }); helmet.remove(); }
  const js = doc.querySelector('script[type="text/x-dc"]').textContent;
  const C = new Function('DCLogic', js + '\nreturn Component;')(DCLogic);
  compiled[name] = { xdc, C };
  return compiled[name];
}
function lookup(path, scope) {
  path = path.trim();
  if (path === 'true') return true; if (path === 'false') return false;
  if (/^-?\d+(\.\d+)?$/.test(path)) return Number(path);
  const segs = path.split('.');
  let o;
  for (let i = scope.length - 1; i >= 0; i--) if (scope[i] && Object.prototype.hasOwnProperty.call(scope[i], segs[0])) { o = scope[i][segs[0]]; break; }
  for (let k = 1; k < segs.length; k++) { if (o == null) return undefined; o = o[segs[k]]; }
  return o;
}
function interp(str, scope) { return str.replace(/\{\{([^}]*)\}\}/g, (m, p) => { const v = lookup(p, scope); return v == null ? '' : String(v); }); }
function blob(s) { return s.replace(/\/_blob\/([0-9a-f]{32})/g, (m, id) => BLOB[id] || m); }
function single(str) { const m = /^\s*\{\{([^}]*)\}\}\s*$/.exec(str); return m ? m[1] : null; }
function kids(n) { return n.content ? Array.from(n.content.childNodes) : Array.from(n.childNodes); }
function renderNodes(nodes, scope, out) { nodes.forEach(n => renderNode(n, scope, out)); }
function renderNode(n, scope, out) {
  if (n.nodeType === 3) { out.appendChild(document.createTextNode(interp(n.textContent, scope))); return; }
  if (n.nodeType !== 1) return;
  const tag = n.localName;
  const sc = n.getAttribute && n.getAttribute('data-sc');
  if (sc === 'for' || tag === 'sc-for') {
    const list = lookup(single(n.getAttribute('list')), scope) || [];
    const as = n.getAttribute('as');
    list.forEach((item, i) => { const s = {}; s[as] = item; s.$index = i; renderNodes(kids(n), scope.concat([s]), out); });
    return;
  }
  if (sc === 'if' || tag === 'sc-if') { if (lookup(single(n.getAttribute('value')), scope)) renderNodes(kids(n), scope, out); return; }
  if (tag === 'dc-import') {
    const props = {};
    for (const a of Array.from(n.attributes)) {
      if (a.name === 'name' || a.name === 'hint-size') continue;
      const key = a.name.replace(/-([a-z])/g, (m, c) => c.toUpperCase());
      const one = single(a.value);
      props[key] = one !== null ? lookup(one, scope) : a.value;
    }
    out.appendChild(mount(n.getAttribute('name'), props));
    return;
  }
  const el = n.namespaceURI === 'http://www.w3.org/2000/svg' ? document.createElementNS(n.namespaceURI, tag) : document.createElement(tag);
  let selValue = null;
  for (const a of Array.from(n.attributes)) {
    if (/^on[A-Z]/.test(a.name) || /^on[a-z]+$/.test(a.name)) continue;
    let v = a.value;
    if (v.includes('{{')) { const one = single(v); if (one !== null && typeof lookup(one, scope) === 'function') continue; v = interp(v, scope); }
    v = blob(v);
    if (tag === 'select' && a.name === 'value') { selValue = v; continue; }
    if (tag === 'input' && a.name === 'value') { el.value = v; }
    try { el.setAttribute(a.name, v); } catch (e) {}
  }
  renderNodes(kids(n), scope, el);
  if (selValue !== null) el.value = selValue;
  out.appendChild(el);
}
function mount(name, props) {
  const { xdc, C } = compile(name);
  const c = new C(); c.props = props || {}; c.state = name === 'Orbit' || /Motion/.test(name) ? { t: window.__T || 2.6 } : {};
  if (c.__init) c.__init();
  const vals = c.renderVals();
  const frag = document.createDocumentFragment();
  renderNodes(Array.from(xdc.childNodes), [vals], frag);
  const wrap = document.createElement('div'); wrap.style.cssText = 'position: relative;'; wrap.appendChild(frag);
  return wrap;
}
window.__render = (name, props) => { document.getElementById('root').appendChild(mount(name, props)); };
'''


def fonts(out):
    """the screens' web fonts as local files (Chromium does not need the network then)"""
    css = out / 'fonts_local.css'
    if css.exists():
        return
    ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
    url = 'https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@300;400;500;600&family=Michroma&family=Space+Mono:wght@400;700&display=swap'
    text = subprocess.run(['curl', '-s', '-A', ua, url], capture_output=True, text=True, check=True).stdout
    (out / 'fonts').mkdir(exist_ok=True)

    def local(m):
        name = 'fonts/' + hashlib.md5(m.group(1).encode()).hexdigest()[:12] + '.woff2'
        if not (out / name).exists():
            subprocess.run(['curl', '-s', '-o', str(out / name), m.group(1)], check=True)
        return 'url(' + name + ')'
    css.write_text(re.sub(r'url\((https://[^)]*\.woff2)\)', local, text))


def main():
    proj = pathlib.Path(sys.argv[1]).resolve(); out = pathlib.Path(sys.argv[2]).resolve(); out.mkdir(parents=True, exist_ok=True)
    fonts(out)
    files = {p.name[:-len('.dc.html')]: p.read_text() for p in proj.glob('*.dc.html')}
    blob = {k: v.resolve().as_uri() for k, v in BLOB.items()}
    page = out / '_page.html'
    page.write_text('<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="fonts_local.css">'
                    '<style>body{margin:0;background:#000}</style></head><body><div id="root"></div>'
                    '<script>' + RUNTIME.replace('__FILES__', json.dumps(files).replace('</', '<\\/')).replace('__BLOB__', json.dumps(blob)) + '</script></body></html>')
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--allow-file-access-from-files'])
        for spec in sys.argv[3:]:
            name, png = spec, None
            if '=' in spec: name, png = spec.split('=', 1)
            props = {}
            if ':' in name: name, pj = name.split(':', 1); props = json.loads(pj)
            w, h = (820, 740) if name == 'Orbit' else (1280, 860)
            pg = br.new_page(viewport={'width': w, 'height': h}, device_scale_factor=1)
            errors = []
            pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(page.as_uri())
            pg.evaluate('([n, p]) => window.__render(n, p)', [name, props])
            pg.wait_for_timeout(800)
            pg.evaluate('document.fonts.ready')
            target = out / ((png or name) + '.png')
            pg.screenshot(path=str(target), clip={'x': 0, 'y': 0, 'width': w, 'height': h})
            print(target.name, 'errors:', errors[:3])
            pg.close()
        br.close()


if __name__ == '__main__':
    main()
