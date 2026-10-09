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


ACTIONS = json.load(open(os.path.join(ROOT, 'ui/actions.json'), encoding='utf-8')) if os.path.exists(os.path.join(ROOT, 'ui/actions.json')) else {}


def host_params(code):
    sp = SPECS['specs'][code.lower()]
    tr = SPECS['traits'][code.lower()]
    items = [dict(p, i=i) for i, p in enumerate(sp)]
    if tr.get('autoGain'):
        items.append({'id': 'common.autogain', 'name': 'Auto gain', 'curve': 'step', 'steps': [0, 1], 'labels': ['Off', 'On'], 'i': len(items)})
    if tr.get('delta'):
        items.append({'id': 'common.delta', 'name': 'Delta', 'curve': 'step', 'steps': [0, 1], 'labels': ['Off', 'On'], 'i': len(items)})
    if tr.get('bypass'):
        items.append({'id': 'common.bypass', 'name': 'Bypass', 'curve': 'step', 'steps': [0, 1], 'labels': ['Off', 'On'], 'i': len(items)})
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
    cand = [p for p in params if norm(p['name']) and (norm(p['name']).startswith(n) or n.startswith(norm(p['name'])))]   # (a name like 'Ø' normalises to '' and would match every label)
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
    lab = ctl.select_one('.lbl, .rl')
    value = ctl.select_one('.val, .rv')
    text = value.get_text().strip() if value else st.get('text', '')
    for t in ctl.select('.dk, .knob, .rk, .pos, .rng, .val, .rv'):
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
    for ctl in list(root.select('.ctl, .rc')):
        lab = ctl.select_one('.lbl, .rl')
        if not lab or not ctl.select_one('.dk, .knob, .rk'):
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
                c.select_one('.lbl, .rl').string = e['labels'][k]
                c['data-p'] = str(e['dup'][k]); c.select_one('.dk, .knob, .rk')['data-dial'] = '1'
                ctl.insert_before(c)
            ctl.decompose(); continue
        if 'label' in e:
            lab.string = e['label']
        if 'p' in e:
            ctl['data-p'] = str(e['p']); ctl.select_one('.dk, .knob, .rk')['data-dial'] = '1'
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
    holder['style'] = 'display:flex;justify-content:flex-start;gap:14px;align-items:center;height:100%;padding:0 14px;overflow-x:auto;overflow-y:hidden;scrollbar-width:thin;scrollbar-color:#3a3b40 #0b0b0c'   # eight slots: the row scrolls
    labels = next(p['labels'] for p in params if p['name'] == 'Pedal 1 Type')
    idx = {p['name']: p['i'] for p in params}
    types = []
    for k in range(1, 9):
        t, on, a, b, c = (idx['Pedal %d %s' % (k, n)] for n in ('Type', 'On', 'A', 'B', 'C'))
        types.append(t)
        knobs = ''.join(
            '<div class="ctl" data-p="%d" title="%s" style="position:absolute"><div class="pk" data-dial="1">'
            '<div class="pcap"></div><div class="ptr"><i></i></div></div></div>' % (i, nm) for i, nm in ((a, 'A'), (b, 'B'), (c, 'C')))
        html = ('<div class="pedal" data-typep="%d" data-names=\'%s\' data-slot="' + json.dumps([t, on, a, b, c]).replace('"', '&quot;') + '" style="position:relative;width:140px;height:226px;flex:none;cursor:grab">'
                '<div class="pshell"></div><span class="pname" title="Type">Comp</span>%s'
                '<button class="pled" data-p="%d" data-toggle="1"></button><button class="pstomp" data-p="%d" data-toggle="1"></button></div>') % (t, json.dumps(labels), knobs, on, on)
        holder.append(BeautifulSoup(html, 'html.parser'))
    for bt in root.find_all('button'):
        if bt.get_text().strip() == 'Add pedal':
            bt['data-addslot'] = json.dumps(types)
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


def vu_frames(root, params):
    """The VU meters (DY01, DY02, DY03, DY06, MT05): the black box around each face becomes the Blender-rendered bezel (tools/blender/vu_bezel.py),
    stretched with border-image so that every size uses the same file. The needle is moved by ui/displays.js."""
    n = 0
    for sv in root.find_all('svg'):
        if sv.get('viewbox') != '0 0 214 124':
            continue
        box = sv.parent
        st = box.get('style', '')
        m = re.search(r'padding:(\d+)px', st)
        pad = int(m.group(1)) if m else 8
        st = re.sub(r'(background|padding|box-shadow):[^;]*;?', '', st)
        box['style'] = st + ';position:relative;border:%dpx solid transparent;border-image:url(@@VUFRAME@@) 30 stretch' % pad
        for pth in sv.find_all('path'):                    # the design's flat white reflection is replaced by the rendered glass
            if pth.get('fill') == '#ffffff' and pth.get('fill-opacity') in ('0.12', '0.1', '0.14'):
                pth.decompose()
        box.append(BeautifulSoup('<div class="vuglass" style="position:absolute;inset:0;background:url(@@VUGLASS@@) center/100%% 100%% no-repeat;pointer-events:none"></div>'.replace('%%', '%'), 'html.parser'))
        n += 1
    return '' if not n else 'x'


def tape_reels(root, params):
    """DL02 and SA01: the two small circles with three lines become Blender-rendered reels (tools/blender/reel.py) that ui/displays.js turns."""
    for sv in root.find_all('svg'):
        if sv.get('viewbox') != '0 0 246 56' or len(sv.find_all('circle')) < 4:
            continue
        circles = [c for c in sv.find_all('circle')]
        hubs = [c for c in circles if c.get('r') == '8.4']
        accent = hubs[0].get('fill') if hubs else '#9a8df0'
        centers = [(float(c['cx']), float(c['cy'])) for c in hubs]
        for c in circles:
            c.decompose()
        for l in list(sv.find_all('line')):
            if l.get('stroke') == '#8a8c92':
                l.decompose()
        holder = sv.parent
        holder['style'] = holder.get('style', '') + ';position:relative'
        for k, (cx, cy) in enumerate(centers):
            pack = (21, 14)[k]
            html = ('<div style="position:absolute;left:%gpx;top:%gpx;width:48px;height:48px;pointer-events:none">'
                    '<div style="position:absolute;left:%gpx;top:%gpx;width:%dpx;height:%dpx;border-radius:50%%;background:radial-gradient(circle,#1d140c 0,#3a2a1c 60%%,#241810 100%%)"></div>'
                    '<div class="reel" style="position:absolute;inset:0;background:url(@@REEL@@) center/100%% 100%% no-repeat"></div>'
                    '<div style="position:absolute;left:%gpx;top:%gpx;width:17px;height:17px;border-radius:50%%;background:%s;opacity:.8"></div>'
                    '<div style="position:absolute;inset:0;border-radius:50%%;background:linear-gradient(135deg,rgba(255,255,255,.22),transparent 38%%)"></div></div>'
                    % (cx - 24 + 2, cy - 24 + 2, 24 - pack, 24 - pack, pack * 2, pack * 2, 15.5, 15.5, accent))
            holder.append(BeautifulSoup(html, 'html.parser'))
    return ''


def rotary_rotors(root, params):
    """MD05: the wedge drawing becomes the two Blender-rendered rotors (horn and drum, tools/blender/rotor.py) that ui/displays.js turns at the speeds of the rotary model."""
    for sv in root.find_all('svg'):
        if sv.get('viewbox') != '0 0 246 56':
            continue
        for t in sv.find_all(['circle', 'path']):
            t.decompose()
        holder = sv.parent
        holder['style'] = holder.get('style', '') + ';position:relative'
        for name, cx in (('horn', 44), ('drum', 100)):
            holder.append(BeautifulSoup('<div class="rotor" data-rotor="%s" style="position:absolute;left:%dpx;top:10px;width:40px;height:40px;background:url(@@ROTOR_%s@@) center/100%% 100%% no-repeat;pointer-events:none"></div>' % (name, cx - 20 + 2, name.upper()), 'html.parser'))
    return ''


def geq_faders(root, params):
    """LV12 (31-band graphic EQ): each fader column is tied to its band (data-fader = the left band, data-fader-r = the right band; the Edit
    parameter chooses which one the screen shows and writes). The drawing is the design's own."""
    left = [p['i'] for p in params if p['name'].startswith('Band ')]
    right = [p['i'] for p in params if p['name'].startswith('R ')]
    edit = next((p['i'] for p in params if p['name'] == 'Edit'), -1)
    if len(left) != 31 or len(right) != 31:
        return ''
    for holder in root.find_all('div'):
        cols = holder.find_all('div', recursive=False)
        if len(cols) == 31 and 'align-items:stretch' in (holder.get('style') or ''):
            for k, col in enumerate(cols):
                track = col.find('div', recursive=False)
                track['data-fader'] = str(left[k]); track['data-fader-r'] = str(right[k]); track['data-edit'] = str(edit)
                track['style'] = track.get('style', '') + ';cursor:ns-resize;touch-action:none'
            return ''
    return ''


def cs04_bind(root, params):
    """CS04: the five knobs under the module cards follow the selected module (hidden band buttons 0..5 = Gate, EQ, Comp, Saturate, De-ess, Limit choose it; ui/displays.js draws the cards, the
    labels and the EQ curve). Slot k of each module is a parameter (or -1: the module has fewer knobs); the design shows the EQ module only, the other modules use the same row."""
    by = {p['name']: p['i'] for p in params}
    cols = [['Gate Thresh', 'Low', 'Comp Thresh', 'Sat Drive', 'De-ess Freq', 'Ceiling'],
            ['Gate Range', 'Mid freq', 'Comp Ratio', 'Sat Mix', 'De-ess Thresh', 'Limit Release'],
            ['Gate Release', 'Mid', 'Comp Attack', None, 'De-ess Range', None],
            [None, 'High', 'Comp Release', None, None, None],
            [None, 'EQ Output', 'Comp Makeup', None, None, None]]
    ctls = [c for c in root.select('.ctl') if c.select_one('.dk, .knob')]
    if len(ctls) != 5 or not all(n in by for col in cols for n in col if n):
        return
    for c, col in zip(ctls, cols):
        if c.has_attr('data-p'):
            del c['data-p']
        c['data-pb'] = json.dumps([by[n] if n else -1 for n in col])
    box = BeautifulSoup('<div style="display:none">' + ''.join('<button data-band="%d"%s></button>' % (k, ' class="on"' if k == 1 else '') for k in range(6)) + '</div>', 'html.parser').find()   # EQ first
    ctls[0].parent.append(box)


def eq02_dynamic(root, params):
    """EQ02: the "Dynamic" button and the small "Range off" dial under it belong to the selected band's Dyn Range (0 = static EQ, otherwise the band works as a dynamic EQ): the button switches it
    between 0 and -6 dB (data-dynpb), the dial sets it (data-pb; the text says "Range off" at 0)."""
    dyn = [p['i'] for p in params if p['name'] == 'Dyn Range']
    btn = next((b for b in root.find_all('button') if b.get_text().strip() == 'Dynamic'), None)
    dial = next((c for c in root.select('.ctl') if (c.select_one('.val') or c).get_text().strip().startswith('Range off') and c.select_one('.dk, .knob')), None)
    if not dyn or btn is None or dial is None:
        return ''
    btn['data-dynpb'] = json.dumps(dyn)
    dial['data-pb'] = json.dumps(dyn); dial['data-zerotext'] = 'Range off'; dial['data-valprefix'] = 'Range '
    dial.select_one('.dk, .knob')['data-dial'] = '1'
    return ''


def lv03_fader(root, params):
    """LV03's Out fader: the cap (the element drawn with the fader image) becomes a bound fader (data-fader = the Out parameter)."""
    out = next((p for p in params if p['id'] == 'lv03.out'), None)
    cap = next((e for e in root.find_all(style=True) if 'r-fader' in e.get('style', '')), None)
    if out and cap:
        cap['data-fader'] = str(out['i'])
    return ''


HOOKS = {'EQ02': eq02_dynamic, 'LV03': lv03_fader, 'LV12': geq_faders, 'MD05': rotary_rotors, 'GT03': gt03_pedals, 'DL02': tape_reels, 'SA01': tape_reels}



def bind_toggles(root, params, report, code):
    """The 3-D toggle switches (.tog) and the header's power button: "In" / "Power" = the product is in (the common Bypass parameter, inverted, or the product's own In parameter);
    two labels (<span>A</span> tog <span>B</span>) = the 2-step parameter with those two labels (up = A) or, if only A names a parameter, A = On."""
    two = [p for p in params if p['curve'] == 'step' and len(p['steps']) == 2]
    bypass = next((p for p in params if p['id'] == 'common.bypass'), None)
    own_in = next((p for p in two if norm(p['name']) == 'in' and not p['id'].startswith('common.')), None)
    inside = (own_in['i'], own_in['steps'][1]) if own_in else ((bypass['i'], bypass['steps'][0]) if bypass else None)   # (index, value when the product is in)
    n = 0
    for tb in root.select('.tb'):
        for b in tb.select('button[aria-label]'):
            if (b.get('aria-label') or '').lower() == 'bypass' and inside:
                b['data-p'] = str(inside[0]); b['data-toggle'] = '1'
                if own_in:
                    b['data-inv'] = '1'
                n += 1
    for t in root.select('.tog'):
        par = t.parent
        spans = [x.get_text(strip=True) for x in par.find_all('span', recursive=False)]
        if not spans:
            continue
        key = [norm(x) for x in spans]
        hit = None
        if len(key) == 1 and key[0] in ('in', 'power') and inside:
            hit = inside
        elif len(key) >= 2:
            a, b2 = key[0], key[-1]
            for p in two:
                lab = [norm(l) for l in p.get('labels', [])]
                if set(lab) == {a, b2} and len(lab) == 2:
                    hit = (p['i'], p['steps'][lab.index(a)]); break
            if hit is None:
                for p in two:
                    if norm(p['name']) == a:
                        hit = (p['i'], p['steps'][1]); break
        if hit is None:                       # a switch of the design that the specification has no parameter for: hidden (the layout stays), not drawn as a control that does nothing
            report.setdefault(code, []).append('tog:' + '/'.join(spans))
            par['style'] = (par.get('style') or '') + ';visibility:hidden'
            continue
        t['data-tog'] = '1'; t['data-p'] = str(hit[0]); t['data-up'] = str(hit[1]); n += 1
    return n


def bind_actions(root, code, report):
    """Buttons that call a method of the core (Randomize, Ring out, Learn noise, Reset, Tap ... ui/actions.json): data-call (+ data-arg, data-calltoggle).
    A design label that begins with the action's label is that action (LV21 "Output off" = Output)."""
    acts = ACTIONS.get(code, [])
    done = set()
    for b in root.select('button'):
        if b.get('data-p') or b.get('data-pb') or b.get('data-band') or b.get('data-act') or b.get('data-call') or b.get('data-tap'):
            continue
        tl = b.select_one('b') if 'tile' in (b.get('class') or []) else None
        t = (tl.get_text() if tl is not None else b.get_text()).strip()
        n = norm(t)
        if not n:
            continue
        for a in acts:
            la = norm(a['label'])
            if n == la or n.startswith(la + ' '):
                b['data-call'] = a['call']
                if 'arg' in a:
                    b['data-arg'] = str(a['arg'])
                if a.get('toggle'):
                    b['data-calltoggle'] = '1'
                done.add(a['label']); break
    for a in acts:
        if a['label'] not in done:
            report.setdefault(code, []).append('action-unmatched:' + a['label'])


def mark_inert(root):
    """Parts of the design whose function is not in the product yet (Low lat, 2x OS, Unit A/B/C, History, the zoom, the LIVE scene/remote/lock chips) are
    shown dimmed with a title instead of pretending to work."""
    inert = {'δ', 'auto gain', 'low lat', '2× os', '100%', 'main show', 'remote', 'lock', 'tap', 'auto', 'dynamic', 'assist', 'unmask', 'auto thresh', 'analyzer', 'add module', 'save chain', 'copy', 'paste', 'learn current', 'snapshot', 'repair'}
    bound = ('data-p', 'data-pb', 'data-band', 'data-act', 'data-call', 'data-tap', 'data-preset', 'data-compare', 'data-dynpb', 'data-analyzer', 'data-copy', 'data-paste', 'data-presetsave')
    n = 0
    for b in root.select('button'):
        if any(b.get(k) for k in bound) or b.find_parent(attrs={'data-p': True}):
            continue
        t = b.get_text().strip().lower()
        if t.startswith(('auto align', 'low cpu')):   # tiles for features the product does not have (LV14 Auto align, LV24 Low CPU)
            b['style'] = (b.get('style') or '') + ';opacity:.4;cursor:default'; b['title'] = 'Not available yet'; b['data-inert'] = '1'; n += 1; continue
        if t == 'analyzer' and root.select_one('.disp svg'):   # EQ08: the measured spectrum behind the EQ curve (the page: ui/displays.js analyzerBackdrop)
            b['data-analyzer'] = '1'; b['title'] = 'Show the measured spectrum behind the curve'; continue
        if t == 'save chain' and root.select_one('.tb button[data-preset]'):   # CS04: the chain's settings are the product's settings: the preset menu's Save
            b['data-presetsave'] = '1'; b['title'] = 'Save the chain as a preset'; continue
        if t == 'add module':   # the chain has the six modules of the specification: nothing to add
            b['style'] = (b.get('style') or '') + ';opacity:.4;cursor:default'; b['title'] = 'The chain has six modules (Gate, EQ, Comp, Saturate, De-ess, Limit)'; b['data-inert'] = '1'; continue
        if t in ('copy', 'paste') and root.select_one('.live, .tb'):   # LV03: the settings of the strip, to the next window of the product (the page + the plug-in's clipboard)
            b['data-' + t] = '1'; b['title'] = 'Copy these settings' if t == 'copy' else 'Paste the settings copied from another window of this product'; continue
        if t == 'compare a' and root.select_one('.disp svg'):   # MT02: keep the current spectrum as the reference curve (the page does it: ui/displays.js compareReference)
            b['data-compare'] = '1'; b['title'] = 'Keep the current spectrum as the reference curve; press again to clear it'; continue
        if t == 'auto fade':                    # dropped from the specification (DY03 v2): not shown
            b['style'] = (b.get('style') or '') + ';visibility:hidden'; continue
        in_evo = b.find_parent(class_='evob') is not None
        if t in inert or (b.get('aria-label') or '').lower() == 'history' or (in_evo and t in ('a', 'b', 'c')):
            b['style'] = (b.get('style') or '') + ';opacity:.4;cursor:default'
            b['title'] = 'Not available yet'; b['data-inert'] = '1'; n += 1
        elif b.find_parent(class_='tb') is not None and b.select_one('svg') and t and not b.get('aria-label'):
            b['data-preset'] = '1'   # the preset menu (sw-ui.js: the person's saved settings)
    # a dial that no parameter drives (EQ02's "Range off"): dimmed, not left looking live
    for c in root.select('.ctl, .rc'):
        if c.get('data-p') or c.get('data-pb') or c.get('data-static') or not c.select_one('.dk, .knob, .rk') or c.find_parent(attrs={'data-p': True}):
            continue
        c['style'] = (c.get('style') or '') + ';opacity:.4;pointer-events:none'; c['title'] = 'Not available yet'; c['data-inert'] = '1'; n += 1
    return n


def eq05_shapes(root, params, code):
    """EQ05: the design's four Bell/Shelf buttons are HF Shape (first pair) and LF Shape (second pair)."""
    if code != 'EQ05':
        return
    idx = {p['name']: p for p in params}
    pairs = [idx.get('HF Shape'), idx.get('LF Shape')]
    bs = [b for b in root.select('button') if norm(b.get_text()) in ('bell', 'shelf') and not b.get('data-p')]
    for k, p in enumerate(pairs):
        if p is None:
            continue
        for b in bs[2 * k:2 * k + 2]:
            lab = [norm(x) for x in p['labels']]
            n = norm(b.get_text())
            if n in lab:
                b['data-p'] = str(p['i']); b['data-v'] = str(p['steps'][lab.index(n)])


def cycle_match(text, params):
    """A chip that prints a parameter and its current option ("Dither off", "ISP 8x", "FFT 4k"): a click steps to the next option. Returns (param, prefix, lowercase) or None."""
    words = text.split()
    for p in params:
        if p['curve'] != 'step' or len(p['steps']) < 2 or p['id'].startswith('common.') or not p.get('labels'):
            continue
        pw = norm(p['name']).split()
        for k in range(min(len(words) - 1, len(pw)), 0, -1):
            if norm(' '.join(words[:k])).split() != pw[:k] or (k < len(pw) and k > 1):
                continue
            if k < len(pw) and k == 1 and pw[0] != norm(words[0]):
                continue
            rem = norm(' '.join(words[k:]))
            if rem in [norm(l) for l in p['labels']]:
                return p, ' '.join(words[:k]), ' '.join(words[k:]).islower()
    return None


def build(code, report):
    src = open(os.path.join(CANVAS, code + '.dc.html'), encoding='utf-8').read()
    soup = BeautifulSoup(src, 'html.parser')
    style = '\n'.join(s.get_text() for s in soup.select('helmet style'))
    root = soup.select_one('x-dc > div')
    if root is None:
        return None
    params = host_params(code)
    alias = json.loads(json.dumps(ALIASES.get(code, {})))   # a copy: lists are consumed
    # a dial group the design wrote without the .rc wrapper (the big Voice knob of LV01: .rk, .rl and .rv side by side) is made one
    for g in root.select('div:has(> .rk)'):
        cls = g.get('class') or []
        if 'rc' in cls or 'ctl' in cls or not g.select_one(':scope > .rl') or not g.select_one(':scope > .rv'):
            continue
        g['class'] = cls + ['rc']
    apply_edits(root, alias)
    extra_css = HOOKS[code](root, params) if code in HOOKS else ''
    if vu_frames(root, params):
        extra_css += ''
        root_html_vu = True
    nb = nk = nbt = nbb = 0
    # knobs: .ctl with a .dk / .knob, label in .lbl
    for ctl in root.select('.ctl, .rc'):
        lab = ctl.select_one('.lbl, .rl')
        dial = ctl.select_one('.dk, .knob, .rk')
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
        for ctl in root.select('.ctl, .rc'):
            lab = ctl.select_one('.lbl, .rl'); dial = ctl.select_one('.dk, .knob, .rk')
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
    bind_toggles(root, params, report, code)
    eq05_shapes(root, params, code)
    bind_actions(root, code, report)
    # buttons: an option of a stepped parameter, or the on/off of a 2-step parameter
    opts = {}
    for p in params:
        if p['curve'] != 'step':
            continue
        for k, s in enumerate(p['steps']):
            lbl = p['labels'][k] if p.get('labels') and k < len(p['labels']) else str(s)
            opts.setdefault(norm(lbl), []).append((p['i'], s))
            w = norm(lbl).split()
            if len(w) >= 3 and w[-1] in ('oct', 'octave'):                # "1/6 oct" is also "1/6" (norm turns the slash into a space)
                opts.setdefault(' '.join(w[:-1]), []).append((p['i'], s))
    names = {}
    for p in params:
        if p['curve'] == 'step' and len(p['steps']) == 2:
            names.setdefault(norm(p['name']), []).append(p['i'])
    delta = [p['i'] for p in params if p['id'] == 'common.delta']
    autogain_ids = [p['i'] for p in params if p['id'] == 'common.autogain']
    autogain = [p['i'] for p in params if p['id'] == 'common.autogain']
    for tb in root.select('.tb'):
        for b in tb.select('button'):
            lab = (b.get('aria-label') or '').lower(); t = b.get_text().strip()
            if lab in ('undo', 'redo'):
                b['data-act'] = lab
            elif lab == 'auto gain' and autogain_ids:
                b['data-p'] = str(autogain_ids[0]); b['data-toggle'] = '1'
            elif t in ('A', 'B') and not b.get('data-act'):
                b['data-act'] = t
    for b in root.select('button, .btn, .chip, .bigbtn'):
        if b.get('data-p') or b.get('data-pb') or b.get('data-band') or b.get('data-act') or b.get('data-call') or b.get('data-tap') or b.find_parent(attrs={'data-p': True}):
            continue
        t = b.get_text().strip()
        tl = b.select_one('b') if 'tile' in (b.get('class') or []) else None   # the LIVE line's tiles: <b>label</b> + <span class="tv">value</span>
        if tl is not None and tl.get_text().strip():
            t = tl.get_text().strip(); b['data-tile'] = '1'
        if (t == 'Δ' or t.startswith('Δ ')) and delta:
            b['data-p'] = str(delta[0]); b['data-toggle'] = '1'; nbt += 1; nbb += 1; continue
        tn = norm(t)
        if tn == 'in' and b.find_parent(class_='tb') is None:
            two2 = [p for p in params if p['curve'] == 'step' and len(p['steps']) == 2]
            own2 = next((p for p in two2 if norm(p['name']) == 'in' and not p['id'].startswith('common.')), None)
            byp = next((p for p in params if p['id'] == 'common.bypass'), None)
            tgt = own2 or byp
            if tgt:                                    # the panel's In: lit while the product is in (the common Bypass is 1 when out, so inverted)
                b['data-p'] = str(tgt['i']); b['data-toggle'] = '1'
                if byp and not own2:
                    b['data-inv'] = '1'
                nbt += 1; nbb += 1; continue
        mom = next((p for p in params if p['curve'] == 'step' and len(p['steps']) == 2 and p.get('auto') is False and re.match(r'^(hold to|mute$)', p['name'].lower()) and (norm(p['name']) == tn or tn.endswith(' ' + norm(p['name'])))), None)
        if mom is not None and tn.startswith('hold to'):   # press and hold (LV01 "Hold to mute" = Mute, LV11 "Hold to cough")
            b['data-p'] = str(mom['i']); b['data-hold'] = '1'; nbt += 1; nbb += 1; continue
        if t == 'Auto' and autogain and b.parent is not None and any(x.get_text().strip() == 'Δ' for x in b.parent.find_all('button', recursive=False)):
            b['data-p'] = str(autogain[0]); b['data-toggle'] = '1'; nbt += 1; nbb += 1; continue   # the panel's Auto next to the Δ button = the common Auto gain
        if not t:
            continue
        nbt += 1
        n = norm(t)
        al = alias.get('btn:' + n)
        if isinstance(al, dict) and al.get('toggle'):   # {"toggle": "Key HPF"}: a button that flips a 2-step parameter
            tp = next((p for p in params if p['name'] == al['toggle']), None)
            if tp is not None:
                b['data-p'] = str(tp['i']); b['data-toggle'] = '1'; nbb += 1; continue
        if isinstance(al, dict) and al.get('tap'):     # tap tempo (DL01): {"tap": "Time", "off": "Sync"}: the taps' average interval is written to Time (ms); the Sync switch goes off (Time would be snapped to a note otherwise)
            tp = next((p for p in params if p['name'] == al['tap']), None)
            if tp is not None:
                b['data-tap'] = str(tp['i'])
                op = next((p for p in params if p['name'] == al.get('off')), None)
                if op is not None:
                    b['data-tapoff'] = str(op['i'])
                nbb += 1; continue
        if isinstance(al, dict) and al.get('set'):     # one button sets several parameters (MS01 Character corners): {"set": [["Character X", 0], ["Character Y", 100]]}
            idx = {p['name']: p['i'] for p in params}
            b['data-set'] = json.dumps([[idx[nm], v] for nm, v in al['set']]); nbb += 1; continue
        if al is not None:
            b['data-p'] = str(al[0]); b['data-v'] = str(al[1]); nbb += 1; continue
        vc = next((e for e in alias.get('_valchip', []) if n.startswith(norm(e['text']))), None)   # a chip / tile that prints a parameter and its value ("Mix 50%"): drag or click to change it
        if vc is not None:
            b['data-valchip'] = str(vc['p'])
            if vc.get('prefix'):
                b['data-prefix'] = vc['prefix']
            if vc.get('suffix'):
                b['data-suffix'] = vc['suffix']
            nbb += 1; continue
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
        cy = cycle_match(t, params)
        if cy is not None:
            b['data-p'] = str(cy[0]['i']); b['data-cycle'] = cy[1]
            if cy[2]:
                b['data-lower'] = '1'
            nbb += 1; continue
        report.setdefault(code, []).append('btn:' + t)
    if code == 'CS04':
        cs04_bind(root, params)
    mark_inert(root)
    # the SW Link lamp of the bottom bar: SW Link is not part of this version (README), so the lamp is not green
    for evr in root.select('.evob .evr'):
        if 'SW Link' in evr.get_text():
            evr['title'] = 'SW Link is not part of this version'
            d = evr.select_one('.evd')
            if d is not None:
                d['style'] = (d.get('style') or '') + ';background:#55575c;box-shadow:none'
    # the EVO label of the bottom bar switches the product's evolution feature (an `.evo.on`-style parameter) when no other control on the screen does
    used_p = {int(x) for x in re.findall(r'data-p="(\d+)"', str(root))}
    evo_p = [p for p in params if '.evo' in p['id'] and p['curve'] == 'step' and len(p['steps']) == 2]
    if len(evo_p) == 1 and evo_p[0]['i'] not in used_p:
        for chip in root.select('.evob .evo'):
            chip['data-p'] = str(evo_p[0]['i']); chip['data-toggle'] = '1'; chip['title'] = 'EVO: ' + evo_p[0]['name'] + ' (click to switch it)'
            nbb += 1
    for ctl in root.select('.ctl, .rc'):
        if ctl.select_one('.dk, .knob, .rk') and ctl.select_one('.lbl, .rl') and not ctl.get('data-p') and not ctl.get('data-pb'):
            st = alias.get('_static', {}).get(norm(ctl.select_one('.lbl, .rl').get_text()))
            if st:
                make_static(ctl, st)    # no parameter: it is not drawn as a knob any more
                continue
            report.setdefault(code, []).append('knob:' + ctl.select_one('.lbl, .rl').get_text())
    style = re.sub(r'@import[^;]*;', '', style) + extra_css + '\n.evo[data-p]{cursor:pointer}.evo[data-p].on{background:var(--acc,#fff);color:#0c0c0d}\n'
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
    html = str(root).replace('@@VUFRAME@@', data_uri(os.path.join(RENDERS, 'parts/vu-bezel-frame.webp'))).replace('@@VUGLASS@@', data_uri(os.path.join(RENDERS, 'parts/vu-glass.webp'))).replace('@@REEL@@', data_uri(os.path.join(RENDERS, 'parts/reel.webp'))).replace('@@ROTOR_HORN@@', data_uri(os.path.join(RENDERS, 'parts/rotor-horn.webp'))).replace('@@ROTOR_DRUM@@', data_uri(os.path.join(RENDERS, 'parts/rotor-drum.webp')))
    return {'css': style, 'html': html, 'w': width, 'h': height, 'knobs': [nb, nk], 'buttons': [nbb, nbt]}


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
