#!/usr/bin/env python3
"""Give the SWINGBY screens (Layer, Arp, Mod, FX) a theme prop: their dark colours become palette tokens (c.*), with a light palette
like SW_Main's. Also writes the <Name>_Light wrappers. usage: themeize.py <project dir> [Board ...]
Used once (2026-10-08) on docs/design/in07/project; kept as the record of the colour -> token mapping."""
import pathlib
import re
import sys

proj = pathlib.Path(sys.argv[1])
main = (proj / 'SW_Main.dc.html').read_text()
HEADER = main[main.index('  <header'):main.index('</header>') + len('</header>')]

PALETTE = r'''    var theme = this.props.theme === 'light' ? 'light' : 'dark', light = theme === 'light';
    // the palette (as SW_Main): the dark one is the night sky, the light one the daylight screen
    var c = light ? {
      bg: 'linear-gradient(180deg, #f1f6f4 0, #e2ece8 55%, #d4e1dc 100%)', stars: false, text: '#1b2622', strong: '#101614', sub: '#4c5f58', label: '#3a4a44', label2: '#2e3b36',
      acc: '#0b5a41', cat: '#178a64', line: 'rgba(11,90,65,.14)', line2: 'rgba(11,90,65,.35)', track: 'rgba(11,90,65,.16)', cell: 'rgba(255,255,255,.55)', cellLine: 'rgba(11,90,65,.18)',
      panel: 'linear-gradient(160deg, rgba(255,255,255,.78), rgba(255,255,255,.52) 60%)', shadow: 'inset 0 1px 0 rgba(255,255,255,.9), 8px 14px 30px rgba(40,70,60,.14)',
      sel: 'rgba(23,138,100,.12)', selLine: 'rgba(11,90,65,.45)', logo: '/_blob/702f4b752a177d6e1601ccac384e7886', accent: '#178a64', field: '#ffffff',
      off: 'background: rgba(11,90,65,.07); color: #2e3b36;', offBtn: 'border-color: rgba(11,90,65,.3); background: none; color: #2e3b36;',
      hi: '#0b5a41', dim: '#8a9a94', velHi: '#0b5a41', velLo: '#22a87c', trail: '#22a87c', probe: '#0b5a41', core: '/_blob/68aab30db9e061801ca79d72df3ac6e2'
    } : {
      bg: '__DARKBG__', stars: true, text: '#e8e8e8', strong: '#f2f2f2', sub: '#8e8e8e', label: '#a8a8a8', label2: '#cacaca',
      acc: '#8de0c3', cat: '#3fd1a0', line: 'rgba(141,224,195,.14)', line2: 'rgba(141,224,195,.35)', track: 'rgba(255,255,255,.12)', cell: 'rgba(255,255,255,.03)', cellLine: 'rgba(141,224,195,.16)',
      panel: 'linear-gradient(200deg, rgba(255,255,255,.06), rgba(255,255,255,.015) 60%)', shadow: 'inset 0 1px 0 rgba(255,255,255,.08), 0 20px 50px rgba(0,0,0,.45)',
      sel: 'rgba(63,209,160,.10)', selLine: 'rgba(141,224,195,.45)', logo: '/_blob/eea73410a66867fa6abf11bd1a56eee6', accent: '#3fd1a0', field: '#0d1314',
      off: 'background: rgba(255,255,255,.05); color: #cacaca;', offBtn: 'border-color: rgba(255,255,255,.2); background: none; color: #cacaca;',
      hi: '#e8fff6', dim: '#5c6460', velHi: '#8de0c3', velLo: '#3fd1a0', trail: '#bff3df', probe: '#e8fff6', core: '/_blob/90121e1a7bee5f359e51e024081e48ca'
    };
'''

# markup: colour -> token (order matters: longer first)
MARKUP = [
    ('background: linear-gradient(200deg, rgba(255,255,255,.06), rgba(255,255,255,.015) 60%); box-shadow: inset 0 1px 0 rgba(255,255,255,.08), 0 20px 50px rgba(0,0,0,.45);',
     'background: {{c.panel}}; box-shadow: {{c.shadow}};'),
    ('background: linear-gradient(160deg, rgba(255,255,255,.06), rgba(255,255,255,.015) 60%); box-shadow: inset 0 1px 0 rgba(255,255,255,.08), 0 20px 50px rgba(0,0,0,.45);',
     'background: {{c.panel}}; box-shadow: {{c.shadow}};'),
    ('theme="dark"', 'theme="{{theme}}"'),
    ('accent-color: #3fd1a0', 'accent-color: {{c.accent}}'),
    ('color: #a8a8a8', 'color: {{c.label}}'), ('color: #cacaca', 'color: {{c.label2}}'), ('color: #f2f2f2', 'color: {{c.strong}}'),
    ('color: #8e8e8e', 'color: {{c.sub}}'), ('color: #8de0c3', 'color: {{c.acc}}'),
    ('rgba(141,224,195,.14)', '{{c.line}}'), ('rgba(141,224,195,.12)', '{{c.line}}'), ('rgba(141,224,195,.08)', '{{c.line}}'),
    ('rgba(141,224,195,.16)', '{{c.cellLine}}'), ('rgba(141,224,195,.22)', '{{c.cellLine}}'), ('rgba(141,224,195,.25)', '{{c.cellLine}}'),
    ('rgba(141,224,195,.45)', '{{c.selLine}}'), ('rgba(141,224,195,.35)', '{{c.line2}}'),
    ('rgba(255,255,255,.12)', '{{c.track}}'), ('rgba(255,255,255,.03)', '{{c.cell}}'), ('rgba(63,209,160,.08)', '{{c.sel}}'),
    ('background: #8de0c3', 'background: {{c.acc}}'), ('stroke="#8de0c3"', 'stroke="{{c.acc}}"'), ('stroke="#3fd1a0"', 'stroke="{{c.cat}}"'),
    ('background: #0d1314', 'background: {{c.field}}'), ('fill="#bff3df"', 'fill="{{c.trail}}"'), ('fill="#e8fff6"', 'fill="{{c.probe}}"'),
    ('src="/_blob/90121e1a7bee5f359e51e024081e48ca"', 'src="{{c.core}}"'),
]
# script: literal styles -> palette
SCRIPT = [
    ("off = 'background: rgba(255,255,255,.05); color: #cacaca;'", 'off = c.off'),
    ("'border-color: rgba(255,255,255,.2); background: none; color: #cacaca;'", 'c.offBtn'),
    ("'color: #f2f2f2; box-shadow: inset 0 -2px 0 #3fd1a0;' : 'color: #8e8e8e;'", "'color: ' + c.strong + '; box-shadow: inset 0 -2px 0 ' + c.cat + ';' : 'color: ' + c.sub + ';'"),
    ("'background: none; color: #8e8e8e;'", "'background: none; color: ' + c.sub + ';'"),
    # layer cards
    ("'border: 1px solid rgba(141,224,195,.45); background: rgba(63,209,160,.10);' : 'border: 1px solid rgba(141,224,195,.12); background: rgba(255,255,255,.03);'",
     "'border: 1px solid ' + c.selLine + '; background: ' + c.sel + ';' : 'border: 1px solid ' + c.line + '; background: ' + c.cell + ';'"),
    # arp cells
    ("ink: i % 4 === 0 ? '#8de0c3' : '#3fd1a0'", 'ink: i % 4 === 0 ? c.velHi : c.velLo'),
    ("bg: p === 0 ? 'rgba(255,255,255,.03)' : 'rgba(63,209,160,.14)', ink: p === 0 ? '#8e8e8e' : '#e8fff6'", "bg: p === 0 ? c.cell : 'rgba(63,209,160,.14)', ink: p === 0 ? c.sub : c.hi"),
    ("'linear-gradient(180deg, rgba(141,224,195,.85), rgba(34,168,124,.75))' : 'rgba(255,255,255,.03)'", "'linear-gradient(180deg, rgba(141,224,195,.85), rgba(34,168,124,.75))' : c.cell"),
    ("ink: i % 4 === 0 ? '#a8a8a8' : '#5c6460'", 'ink: i % 4 === 0 ? c.label : c.dim'),
    # fx
    ("ink: en ? '#e8fff6' : '#5c7a70'", 'ink: en ? c.hi : c.dim'),
    ("line: en ? 'rgba(141,224,195,.3)' : 'rgba(141,224,195,.12)'", 'line: en ? c.line2 : c.line'),
    ("'border-color: rgba(255,255,255,.2); background: none; color: #cacaca;'", 'c.offBtn'),
]


def themeize(name):
    p = proj / (name + '.dc.html')
    s = p.read_text()
    head, script = s.split('<script type="text/x-dc"', 1)
    # the root: its dark background goes to the palette
    m = re.search(r'color: #e8e8e8; background: (radial-gradient\([^;]*\));">', head)
    dark_bg = m.group(1)
    head = head.replace(m.group(0), 'color: {{c.text}}; background: {{c.bg}};">', 1)
    # the stars only at night
    sm = re.search(r'\n  (<div aria-hidden="true" style="position: absolute; inset: 0; background-image: [^"]*"></div>)', head)
    head = head.replace(sm.group(0), '\n  <sc-if value="{{c.stars}}" hint-placeholder-val="{{true}}">\n    ' + sm.group(1) + '\n  </sc-if>', 1)
    # the header as SW_Main's (logo, motion, link in tokens)
    a = head.index('  <header'); b = head.index('</header>') + len('</header>')
    head = head[:a] + HEADER + head[b:]
    for old, new in MARKUP:
        head = head.replace(old, new)
    for old, new in SCRIPT:
        script = script.replace(old, new)
    script = script.replace("data-props='{\"$preview\"", "data-props='{\"theme\":{\"editor\":\"enum\",\"default\":\"dark\",\"options\":[\"dark\",\"light\"]},\"$preview\"", 1)
    i = script.index('  renderVals() {\n') + len('  renderVals() {\n'); i = script.index('\n', i) + 1   # after its first line
    script = script[:i] + PALETTE.replace('__DARKBG__', dark_bg) + script[i:]
    script = re.sub(r'(\n    return \{\n)', r'\1      theme: theme, c: c,\n', script, count=1)
    out = head + '<script type="text/x-dc"' + script
    p.write_text(out)
    # leftovers worth a look: hex colours still in the markup
    left = sorted(set(re.findall(r'#[0-9a-fA-F]{6}|rgba\(\d+,\d+,\d+,[.\d]+\)', out.split('<script type="text/x-dc"')[0])))
    print(name, 'markup colours left:', left)
    # the light wrapper
    lw = (proj / 'SW_Main_Light.dc.html').read_text()
    lw = lw.replace('SWINGBY main screen, daylight', 'SWINGBY ' + name[3:].lower() + ' screen, daylight').replace('name="SW_Main"', 'name="' + name + '"')
    (proj / (name + '_Light.dc.html')).write_text(lw)


for n in sys.argv[2:] or ['SW_Layer', 'SW_Arp', 'SW_Mod', 'SW_FX']:
    themeize(n)
