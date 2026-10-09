#!/usr/bin/env python3
"""Checks the generated screens (ui/skins.json, from tools/gen_skins.py) against the parameter tables (ui/specs.json) for binding mistakes that no other test sees:
  1. a knob whose label shares no word with the parameter it drives (LV03 "Thresh" was bound to Phase);
  2. a knob bound to a common parameter (Auto gain, Delta, Bypass) instead of a product parameter (DY02 "Gain");
  3. two knobs on one parameter (outside the per-band controls);
  4. a switch whose text is also an option of another parameter (VO03 "Scale" = Source option, not the Scale switch);
  5. a chip for one option of a parameter whose text names a different option;
  6. a dial (.rk / .dk / .knob) with no binding that is not marked as a read-out or dimmed.
Run: python3 tools/gen_skins.py && python3 tools/check_skins.py        (exit code 1 when anything is found)"""
import json, os, re, sys
from bs4 import BeautifulSoup

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
skins = json.load(open(os.path.join(ROOT, 'ui/skins.json'), encoding='utf-8'))
specs = json.load(open(os.path.join(ROOT, 'ui/specs.json'), encoding='utf-8'))['specs']
# deliberate differences between the design's label and the parameter (decisions in the README)
KNOB_OK = {('DY01', 'Input', 'Drive'), ('EQ01', 'Level', 'Output'), ('RV06', 'Pitch', 'Interval'), ('DY02', 'Peak reduction', 'Level'), ('DY02', 'Gain', 'Output')}
DIAL_OK = set()


def words(t):
    return set(re.findall(r'[a-z0-9]+', t.lower()))


def own_text(e):
    return ''.join(c for c in e.children if isinstance(c, str)).strip()


problems = []
for code, sk in skins.items():
    if not isinstance(sk, dict) or 'html' not in sk:
        continue
    ps = specs.get(code.lower())
    if not ps:
        continue
    soup = BeautifulSoup(sk['html'], 'html.parser')
    seen = {}
    for c in soup.select('.ctl[data-p], .rc[data-p]'):
        i = int(c['data-p'])
        lab = (c.select_one('.lbl, .rl') or c).get_text().strip()
        if i >= len(ps):
            problems.append('%s: knob "%s" is bound to a common parameter (%d)' % (code, lab, i)); continue
        seen.setdefault(i, []).append(lab)
        a, b = words(lab), words(ps[i]['name'])
        if a and b and not (a & b) and (code, lab, ps[i]['name']) not in KNOB_OK:
            problems.append('%s: knob "%s" drives "%s"' % (code, lab, ps[i]['name']))
    for i, labs in seen.items():
        if len(labs) > 1:
            problems.append('%s: parameter "%s" is on several knobs %s' % (code, ps[i]['name'], labs))
    for b in soup.select('button[data-p][data-toggle], .chip[data-p][data-toggle], .dbtn[data-p][data-toggle]'):
        i = int(b['data-p'])
        if i >= len(ps):
            continue
        t = words(own_text(b) or b.get_text())
        for j, p in enumerate(ps):
            if j != i and p.get('labels') and t and t in [words(l) for l in p['labels']] and words(p['name']) != t:
                problems.append('%s: switch "%s" toggles "%s" but is also an option of "%s"' % (code, b.get_text().strip(), ps[i]['name'], p['name']))
    for b in soup.select('button[data-p][data-v], .chip[data-p][data-v], .dbtn[data-p][data-v]'):
        i = int(b['data-p'])
        if i >= len(ps):
            continue
        p = ps[i]; labs, st = p.get('labels'), p.get('steps')
        try:
            j = st.index(float(b['data-v'])) if st else None
        except ValueError:
            j = None
        if labs and j is not None and j < len(labs):
            lt, bt = words(labs[j]), words(own_text(b) or b.get_text())
            if lt and bt and not (lt & bt) and not (bt & words(p['name'])):
                problems.append('%s: chip "%s" sets "%s" = %s' % (code, b.get_text().strip(), p['name'], labs[j]))
    for e in soup.select('.rk:has(.kn), .dk, .knob'):   # (MS01's .rk without .kn is a ruler)
        if e.get('data-dial') or e.find_parent(attrs={'data-p': True}) or e.find_parent(attrs={'data-pb': True}) or e.find_parent(attrs={'data-static': True}) or e.find_parent(attrs={'data-inert': True}):
            continue
        lab = e.parent.select_one('.rl, .lbl')
        if (code, lab.get_text().strip() if lab else '?') not in DIAL_OK:
            problems.append('%s: dial "%s" has no binding' % (code, lab.get_text().strip() if lab else '?'))

for p in problems:
    print(p)
print(len(problems), 'problems' if problems else 'screens are consistent with the parameter tables')
sys.exit(1 if problems else 0)
