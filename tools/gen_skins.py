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


def make_static(ctl, st):
    """A part with no parameter (meter mode, tuner, music state) becomes a switch or a read-out, not a knob."""
    lab = ctl.select_one('.lbl')
    value = ctl.select_one('.val')
    text = value.get_text().strip() if value else st.get('text', '')
    for t in ctl.select('.dk, .knob, .pos, .rng, .val'):
        t.decompose()
    if st['kind'] == 'switch':
        seg = BeautifulSoup('<div data-seg="1" style="display:flex;gap:2px;margin-bottom:6px"></div>', 'html.parser').find()
        for k, o in enumerate(st['options']):
            b = BeautifulSoup('<button style="font:600 11px \'Space Mono\',monospace;color:#cfcfcf;background:#161617;border:1px solid #3a3a3d;padding:4px 7px;border-radius:3px;cursor:pointer"></button>', 'html.parser').find()
            b.string = o
            if k == 0:
                b['data-on'] = '1'
            seg.append(b)
        lab.insert_before(seg)
    else:
        ro = BeautifulSoup('<div data-readout="1" style="display:flex;align-items:center;gap:8px;min-width:96px;margin-bottom:6px;padding:7px 12px;background:#0c0c0d;border:1px solid #2c2c2f;border-radius:4px"><i style="width:7px;height:7px;border-radius:50%;background:var(--acc);box-shadow:0 0 6px var(--acc)"></i><span style="font:11px \'Space Mono\',monospace;color:#e6e6e6;white-space:nowrap"></span></div>', 'html.parser').find()
        ro.find('span').string = text
        lab.insert_before(ro)
    ctl['data-static'] = '1'


def apply_edits(root, alias):
    """Pattern B: the design is brought in line with the specification (see the README, 画面の項目を仕様に合わせた箇所).
    alias['_edit']: '<label>' or '<label>#<n>' (n-th control with that label, from 1) -> {label, p, rng, pos, remove, dup}
    alias['_sections']: section heading -> new heading;  alias['_drop_text']: texts of static decorations to delete."""
    edits = alias.get('_edit', {})
    seen = {}
    for ctl in list(root.select('.ctl')):
        lab = ctl.select_one('.lbl')
        if not lab or not ctl.select_one('.dk, .knob'):
            continue
        n = norm(lab.get_text())
        seen[n] = seen.get(n, 0) + 1
        e = edits.get('%s#%d' % (n, seen[n])) or edits.get(n)
        if not e:
            continue
        if e.get('remove'):
            ctl.decompose(); continue
        if e.get('dup'):                 # one knob becomes several (Crossover 1..3), each with its own parameter
            clones = []
            for k in range(len(e['dup'])):
                c = BeautifulSoup(str(ctl), 'html.parser').find()
                clones.append(c)
            for k, c in enumerate(clones):
                c.select_one('.lbl').string = e['labels'][k]
                c['data-p'] = str(e['dup'][k]); c.select_one('.dk, .knob')['data-dial'] = '1'
                ctl.insert_before(c)
            ctl.decompose(); continue
        if 'label' in e:
            lab.string = e['label']
        if 'p' in e:
            ctl['data-p'] = str(e['p']); ctl.select_one('.dk, .knob')['data-dial'] = '1'
        if 'rng' in e and ctl.select_one('.rng'):
            for sp, t in zip(ctl.select_one('.rng').find_all('span'), e['rng']):
                sp.string = t
        if 'pos' in e and ctl.select_one('.pos'):
            pos = ctl.select_one('.pos')
            for sp in pos.find_all('span'):
                sp.decompose()
            for t in e['pos']:
                sp = BeautifulSoup('<span></span>', 'html.parser').find(); sp.string = t; pos.append(sp)
    for h in root.select('.sechd'):
        t = h.get_text().strip()
        if t in alias.get('_sections', {}):
            h.string = alias['_sections'][t]
    for text in alias.get('_drop_text', []):
        for d in root.find_all(True):
            if d.get_text().strip() == text and not d.find(True):
                d.decompose()


RENDERS = os.path.join(ROOT, 'docs/design/design-system/project/assets/renders')


def data_uri(path):
    import base64
    return 'data:image/webp;base64,' + base64.b64encode(open(path, 'rb').read()).decode()


def gt03_pedals(root, params):
    """GT03: the six flat cards become Blender-rendered stomp boxes (tools/blender/pedal.py, stomp.py) whose knobs, foot switch, LED
    and type are the parameters of the slot (Pedal k Type / On / A / B / C). Returns the css it needs."""
    first = None
    for t in root.find_all(string=lambda x: x and x.strip() == 'Comp'):
        first = t.parent.parent.parent; break
    if first is None:
        return ''
    holder = first.parent
    for c in list(holder.children):
        if getattr(c, 'decompose', None):
            c.decompose()
    holder['style'] = 'display:flex;justify-content:space-evenly;align-items:center;height:100%;padding:0 12px'
    labels = next(p['labels'] for p in params if p['name'] == 'Pedal 1 Type')
    idx = {p['name']: p['i'] for p in params}
    for k in range(1, 7):
        t, on, a, b, c = (idx['Pedal %d %s' % (k, n)] for n in ('Type', 'On', 'A', 'B', 'C'))
        knobs = ''.join(
            '<div class="ctl" data-p="%d" title="%s" style="position:absolute"><div class="pk" data-dial="1">'
            '<div class="pcap"></div><div class="ptr"><i></i></div></div></div>' % (i, nm) for i, nm in ((a, 'A'), (b, 'B'), (c, 'C')))
        html = ('<div class="pedal" data-typep="%d" data-names=\'%s\' style="position:relative;width:140px;height:226px;flex:none">'
                '<div class="pshell"></div><span class="pname" title="Type">Comp</span>%s'
                '<button class="pled" data-p="%d" data-toggle="1"></button><button class="pstomp" data-p="%d" data-toggle="1"></button></div>') % (t, json.dumps(labels), knobs, on, on)
        holder.append(BeautifulSoup(html, 'html.parser'))
    css = ('.pedal .ctl{background:none;border:0;padding:0;margin:0;display:block;box-shadow:none}.pk{position:relative;width:30px;height:30px;cursor:ns-resize}.pk .pcap{position:absolute;inset:0;background:url(@@KNOB@@) center/100% 100% no-repeat;filter:drop-shadow(0 2px 2px rgba(0,0,0,.45));pointer-events:none}'
           '.pk .ptr{position:absolute;inset:0;pointer-events:none}.pk .ptr i{position:absolute;left:50%;top:4px;width:2px;height:7px;margin-left:-1px;border-radius:1px;background:#e8e8ea}'
           '.pedal .pshell{position:absolute;left:-14px;top:-14px;width:168px;height:254px;background-size:100% 100%;pointer-events:none}'
           '.pedal .pname{position:absolute;left:0;right:0;top:84px;text-align:center;font:700 14px "Barlow Condensed",sans-serif;letter-spacing:.14em;text-transform:uppercase;color:#111;cursor:pointer;user-select:none}'
           '.pedal .pled{position:absolute;left:65px;top:128px;width:10px;height:10px;padding:0;border-radius:50%;background:#2b1d1d;border:1px solid #000;cursor:pointer}'
           '.pedal .pled.on{background:#2bd14a;box-shadow:0 0 8px #2bd14a}'
           '.pedal .pstomp{position:absolute;left:41px;top:165px;width:58px;height:58px;padding:0;border:0;background:transparent center/100% 100% no-repeat;cursor:pointer}'
           '.pedal .pstomp:active{transform:translateY(1px)}'
           '.pedal.t0 .pshell{left:0;top:0;width:140px;height:226px;border:1px dashed #44464b;border-radius:10px;background:none}'
           '.pedal.t0 .ctl,.pedal.t0 .pled,.pedal.t0 .pstomp{display:none}.pedal.t0 .pname{color:#6b6d72}'
           '.pedal .pstomp{background-image:url(@@STOMP@@)}').replace('@@STOMP@@', data_uri(os.path.join(RENDERS, 'pedals/stomp.webp'))).replace('@@KNOB@@', data_uri(os.path.join(RENDERS, 'pedals/knob-small.webp')))
    layout = json.load(open(os.path.join(ROOT, 'tools/blender/pedals.json')))['types']
    for n, name in enumerate(('comp', 'drive', 'fuzz', 'chorus', 'delay', 'reverb'), 1):
        L = layout[name]; q = '.pedal.t%d ' % n
        for j, (kx, ky, ks) in enumerate(L['knobs']):
            css += '%s.ctl:nth-child(%d){left:%gpx;top:%gpx;width:%dpx;height:%dpx}%s.ctl:nth-child(%d) .pk{width:%dpx;height:%dpx}' % (q, 3 + j, kx - ks / 2, ky - ks / 2, ks, ks, q, 3 + j, ks, ks)
        lx, ly, ls = L['led']
        css += '%s.pled{left:%gpx;top:%gpx;width:%dpx;height:%dpx}' % (q, lx - ls / 2, ly - ls / 2, ls, ls)
        sx, sy, ss = L['stomp']; box = ss + 14
        css += '%s.pstomp{left:%gpx;top:%gpx;width:%dpx;height:%dpx}%s.pname{top:%dpx}' % (q, sx - box / 2, sy - box / 2, box, box, q, L['name'][1])
        css += '.pedal.t' + str(n) + ' .pshell{background-image:url(' + data_uri(os.path.join(RENDERS, 'pedals/%s.webp' % name)) + ')}'
    return css


HOOKS = {'GT03': gt03_pedals}


def build(code, report):
    src = open(os.path.join(CANVAS, code + '.dc.html'), encoding='utf-8').read()
    soup = BeautifulSoup(src, 'html.parser')
    style = '\n'.join(s.get_text() for s in soup.select('helmet style'))
    root = soup.select_one('x-dc > div')
    if root is None:
        return None
    params = host_params(code)
    alias = json.loads(json.dumps(ALIASES.get(code, {})))   # a copy: lists are consumed
    apply_edits(root, alias)
    extra_css = HOOKS[code](root, params) if code in HOOKS else ''
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
            st = alias.get('_static', {}).get(norm(ctl.select_one('.lbl').get_text()))
            if st:
                make_static(ctl, st)    # no parameter: it is not drawn as a knob any more
                continue
            report.setdefault(code, []).append('knob:' + ctl.select_one('.lbl').get_text())
    style = re.sub(r'@import[^;]*;', '', style) + extra_css
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
