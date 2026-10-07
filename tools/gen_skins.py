#!/usr/bin/env python3
"""Turn the design canvas (docs/design/canvas/project/<CODE>.dc.html) into ui/skins.json.

For each product: the design's own <style> and the 960x550 root markup, with the controls that can be tied to a parameter
marked data-p="<host index>" (knobs, toggles) and data-v="<plain value>" (one option of a stepped parameter).
Controls that cannot be tied unambiguously stay as drawn (static) and are listed in the report.
Usage: tools/gen_skins.py [--report]
"""
import json, os, re, sys
from bs4 import BeautifulSoup

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CANVAS = os.path.join(ROOT, 'docs/design/canvas/project')
SPECS = json.load(open(os.path.join(ROOT, 'ui/specs.json')))
ALIAS_FILE = os.path.join(ROOT, 'ui/skin_aliases.json')
ALIASES = json.load(open(ALIAS_FILE)) if os.path.exists(ALIAS_FILE) else {}
UNITS = {'hz', 'khz', 'db', 'ms', 's', 'sec', 'pct', 'x'}


def norm(t):
    return re.sub(r'[^a-z0-9]+', ' ', t.lower()).strip()


def toks(t):
    return [w for w in norm(t).split() if w not in UNITS]


def host_params(code):
    sp = SPECS['specs'][code.lower()]
    tr = SPECS['traits'][code.lower()]
    items = [dict(p, i=i) for i, p in enumerate(sp)]
    if tr.get('autoGain'):
        items.append({'id': 'common.autogain', 'name': 'Auto gain', 'curve': 'step', 'steps': [0, 1], 'labels': ['Off', 'On'], 'i': len(items)})
    if tr.get('delta'):
        items.append({'id': 'common.delta', 'name': 'Delta', 'curve': 'step', 'steps': [0, 1], 'labels': ['Off', 'On'], 'i': len(items)})
    return items


def match_knob(label, params, alias):
    n = norm(label)
    if n in alias:                       # a number, or a list used one by one in the order the labels appear
        a = alias[n]
        if isinstance(a, list):
            return a.pop(0) if a else None
        return a
    exact = [p for p in params if norm(p['name']) == n]
    if len(exact) == 1:
        return exact[0]['i']
    lt = toks(label)
    if not lt:
        return None
    cand = [p for p in params if set(lt) <= set(toks(p['name']))]
    if len(cand) == 1:
        return cand[0]['i']
    cand = [p for p in params if norm(p['name']).startswith(n) or n.startswith(norm(p['name']))]
    return cand[0]['i'] if len(cand) == 1 else None


BASE = {}


def build(code, report):
    src = open(os.path.join(CANVAS, code + '.dc.html'), encoding='utf-8').read()
    soup = BeautifulSoup(src, 'html.parser')
    style = '\n'.join(s.get_text() for s in soup.select('helmet style'))
    root = soup.select_one('x-dc > div')
    if root is None:
        return None
    params = host_params(code)
    alias = json.loads(json.dumps(ALIASES.get(code, {})))   # a copy: lists are consumed
    nb = nk = nbt = nbb = 0
    # knobs: .ctl with a .dk / .knob, label in .lbl
    for ctl in root.select('.ctl'):
        lab = ctl.select_one('.lbl')
        dial = ctl.select_one('.dk, .knob')
        if not lab or not dial:
            continue
        nk += 1
        i = match_knob(lab.get_text(), params, alias)
        if i is None:
            report.setdefault(code, []).append('knob:' + lab.get_text())
            continue
        ctl['data-p'] = str(i)
        dial['data-dial'] = '1'
        nb += 1
    # buttons: an option of a stepped parameter, or the on/off of a 2-step parameter
    opts = {}
    for p in params:
        if p['curve'] != 'step':
            continue
        for k, s in enumerate(p['steps']):
            lbl = p['labels'][k] if p.get('labels') and k < len(p['labels']) else str(s)
            opts.setdefault(norm(lbl), []).append((p['i'], s))
    names = {}
    for p in params:
        if p['curve'] == 'step' and len(p['steps']) == 2:
            names.setdefault(norm(p['name']), []).append(p['i'])
    delta = [p['i'] for p in params if p['id'] == 'common.delta']
    for tb in root.select('.tb'):
        for b in tb.select('button'):
            lab = (b.get('aria-label') or '').lower(); t = b.get_text().strip()
            if lab in ('undo', 'redo'):
                b['data-act'] = lab
            elif t in ('A', 'B') and not b.get('data-act'):
                b['data-act'] = t
    for b in root.select('button, .btn, .chip, .bigbtn'):
        if b.get('data-p') or b.get('data-act') or b.find_parent(attrs={'data-p': True}):
            continue
        t = b.get_text().strip()
        if t == 'Δ' and delta:
            b['data-p'] = str(delta[0]); b['data-toggle'] = '1'; nbt += 1; nbb += 1; continue
        if not t:
            continue
        nbt += 1
        n = norm(t)
        al = alias.get('btn:' + n)
        if al is not None:
            b['data-p'] = str(al[0]); b['data-v'] = str(al[1]); nbb += 1; continue
        if n in names and len(names[n]) == 1:
            b['data-p'] = str(names[n][0]); b['data-toggle'] = '1'; nbb += 1; continue
        if n in opts and len(opts[n]) == 1:
            b['data-p'] = str(opts[n][0][0]); b['data-v'] = str(opts[n][0][1]); nbb += 1; continue
        report.setdefault(code, []).append('btn:' + t)
    style = re.sub(r'@import[^;]*;', '', style)
    m = re.match(r'<div[^>]*style="([^"]*)"', str(root))
    st = m.group(1) if m else ''
    mw, mh = re.search(r'(?:^|;)\s*width:(\d+)px', st), re.search(r'(?:^|;)\s*height:(\d+)px', st)
    width, height = int(mw.group(1)) if mw else 960, int(mh.group(1)) if mh else 550
    # the Blender-rendered images (base64 in custom properties) are the same in every design: kept once in BASE
    def take(m):
        name, val = m.group(1), m.group(2)
        if BASE.setdefault(name, val) != val:
            return m.group(0)       # differs between products: stays in the product
        return ''
    style = re.sub(r'(--r-[A-Za-z0-9_-]+)\s*:\s*(url\(data:image/[a-z+]+;base64,[A-Za-z0-9+/=]+\))\s*;?', take, style)
    for part in (style, str(root)):
        if re.search(r'</script', part, re.I):
            raise SystemExit(code + ': the design contains a closing script tag')
    return {'css': style, 'html': str(root), 'w': width, 'h': height, 'knobs': [nb, nk], 'buttons': [nbb, nbt]}


def arr(name, data):
    rows = [','.join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return 'static const unsigned char %s[] = {\n%s};\nstatic const unsigned long %sSize = %d;\n' % (name, ',\n'.join(rows) if rows else '0', name, len(data))


def emit(out, directory):
    """One header per product (build/gen/skins/skin_<code>.hpp): the design's css (shared images first) and markup, and its size."""
    os.makedirs(directory, exist_ok=True)
    base = out['_base']['css']
    for code, v in out.items():
        if code == '_base':
            continue
        text = '// generated by tools/gen_skins.py — do not edit\n#pragma once\nnamespace sw::gui_assets {\n'
        text += arr('kSkinCss', (base + '\n' + v['css']).encode('utf-8')) + arr('kSkinHtml', v['html'].encode('utf-8'))
        text += 'static const int kSkinW = %d, kSkinH = %d;\n}  // namespace sw::gui_assets\n' % (v['w'], v['h'])
        path = os.path.join(directory, 'skin_%s.hpp' % code.lower())
        if not os.path.exists(path) or open(path).read() != text:
            open(path, 'w').write(text)


def main():
    report = {}
    out = {}
    for code in sorted(SPECS['specs']):
        c = code.upper()
        if not os.path.exists(os.path.join(CANVAS, c + '.dc.html')):
            continue
        r = build(c, report)
        if r:
            out[c] = r
    out['_base'] = {'css': '.p{' + ';'.join(k + ':' + v for k, v in BASE.items()) + '}'}
    json.dump(out, open(os.path.join(ROOT, 'ui/skins.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    if '--emit' in sys.argv:
        emit(out, sys.argv[sys.argv.index('--emit') + 1])
    kb = sum(v['knobs'][0] for k, v in out.items() if k != '_base'); kt = sum(v['knobs'][1] for k, v in out.items() if k != '_base')
    bb = sum(v['buttons'][0] for k, v in out.items() if k != '_base'); bt = sum(v['buttons'][1] for k, v in out.items() if k != '_base')
    print('skins: %d products, knobs bound %d/%d, buttons bound %d/%d, size %d KB' % (len(out) - 1, kb, kt, bb, bt, os.path.getsize(os.path.join(ROOT, 'ui/skins.json')) // 1024))
    if '--report' in sys.argv:
        for k, v in report.items():
            print(k, v[:12])


if __name__ == '__main__':
    main()
