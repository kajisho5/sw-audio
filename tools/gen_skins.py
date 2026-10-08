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
SPECS = json.load(open(os.path.join(ROOT, 'ui/specs.json'), encoding='utf-8'))
ALIAS_FILE = os.path.join(ROOT, 'ui/skin_aliases.json')
ALIASES = json.load(open(ALIAS_FILE, encoding='utf-8')) if os.path.exists(ALIAS_FILE) else {}
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


def band_groups(params, alias):
    """Parameters that repeat per band/voice/tap: {suffix: [host index of band 0, band 1, ...]} (names 'Band 2 Gain', or a stride given in the aliases)."""
    groups = {}
    st = alias.get('_stride')
    if st:
        for k in range(st['count']):
            for j in range(st['stride']):
                i = st['start'] + k * st['stride'] + j
                if i < len(params):
                    groups.setdefault(norm(params[i]['name']), []).append(i)
        return groups
    for p in params:
        m = re.match(r'^(?:Band|Voice|Tap|Pedal|Module|Slot)\s+(\d+)\s+(.+)$', p['name'])
        if m:
            groups.setdefault(norm(m.group(2)), []).append(p['i'])
    return groups


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
            continue
        ctl['data-p'] = str(i)
        dial['data-dial'] = '1'
        nb += 1
    # per-band controls: the selector buttons choose the band, the knobs follow it (data-pb = the host index of each band's parameter)
    groups = band_groups(params, alias)
    if groups:
        sel = alias.get('_bands') or {}
        labels = [norm(x) for x in sel.get('labels', [])]
        for ctl in root.select('.ctl'):
            lab = ctl.select_one('.lbl'); dial = ctl.select_one('.dk, .knob')
            if not lab or not dial or ctl.get('data-p'):
                continue
            g = groups.get(norm(lab.get_text())) or groups.get(alias.get('_alias', {}).get(norm(lab.get_text()), ''))
            if g:
                ctl['data-pb'] = json.dumps(g); dial['data-dial'] = '1'; nb += 1
        for b in root.select('button'):
            t = b.get_text().strip(); n = norm(t)
            if not n or b.get('data-p') or b.get('data-act'):
                continue
            k = labels.index(n) if n in labels else None
            if k is None and not labels:
                m = re.match(r'^(?:(?:band|voice|tap|pedal|module|slot) )?(\d+)$', n)
                if m:
                    k = int(m.group(1)) - 1
            if k is not None:
                b['data-band'] = str(k); nbt += 1; nbb += 1
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
        if b.get('data-p') or b.get('data-pb') or b.get('data-band') or b.get('data-act') or b.find_parent(attrs={'data-p': True}):
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
        if groups and not b.get('data-band'):
            gk = n if n in groups else alias.get('_alias', {}).get(n)
            if gk in groups and params[groups[gk][0]]['curve'] == 'step' and len(params[groups[gk][0]]['steps']) == 2:
                b['data-pb'] = json.dumps(groups[gk]); b['data-toggle'] = '1'; nbb += 1; continue
            hit = []
            for suffix, idxs in groups.items():
                p0 = params[idxs[0]]
                if p0['curve'] != 'step':
                    continue
                for k2, v2 in enumerate(p0['steps']):
                    lbl2 = p0['labels'][k2] if p0.get('labels') and k2 < len(p0['labels']) else str(v2)
                    if norm(lbl2) == n:
                        hit.append((idxs, v2))
            if len(hit) == 1:
                b['data-pb'] = json.dumps(hit[0][0]); b['data-v'] = str(hit[0][1]); nbb += 1; continue
        report.setdefault(code, []).append('btn:' + t)
    for ctl in root.select('.ctl'):
        if ctl.select_one('.dk, .knob') and ctl.select_one('.lbl') and not ctl.get('data-p') and not ctl.get('data-pb'):
            report.setdefault(code, []).append('knob:' + ctl.select_one('.lbl').get_text())
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
        if not os.path.exists(path) or open(path, encoding='utf-8').read() != text:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)


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
    if '--emit' not in sys.argv:    # the preview data (ui/skins.json) is for the browser preview only
        with open(os.path.join(ROOT, 'ui/skins.json'), 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, separators=(',', ':'))
    if '--emit' in sys.argv:
        emit(out, sys.argv[sys.argv.index('--emit') + 1])
    kb = sum(v['knobs'][0] for k, v in out.items() if k != '_base'); kt = sum(v['knobs'][1] for k, v in out.items() if k != '_base')
    bb = sum(v['buttons'][0] for k, v in out.items() if k != '_base'); bt = sum(v['buttons'][1] for k, v in out.items() if k != '_base')
    print('skins: %d products, knobs bound %d/%d, buttons bound %d/%d, size %d KB' % (len(out) - 1, kb, kt, bb, bt, len(json.dumps(out, ensure_ascii=False)) // 1024))
    if '--report' in sys.argv:
        for k, v in report.items():
            print(k, v[:12])


if __name__ == '__main__':
    main()
