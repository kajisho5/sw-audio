#!/usr/bin/env python3
"""SW AUDIO logo (C Rack Ear, docs/design/design-system/project/LOGO.md) as SVG files, plus PNG previews and the README banner.

    python3 tools/gen_logo.py        -> docs/design/design-system/project/assets/Logo/*.svg, *.png  and  docs/images/*.png

Geometry, as LOGO.md fixes it:
  symbol        rounded rectangle 100 x 236 (r 18) with two horizontal slots 52 x 24 (r 12), on a 256 grid; arcs only
  symbol-small  for 16-32 px: 140 x 240 (r 24), slots 72 x 36
  wordmark      "SW AUDIO", monoline (stroke 13 on a cap height of 100), wide capitals, the A's diagonals at 60 degrees, round ends
  lockups       horizontal (symbol 256, wordmark cap height 74, gap 44), stacked (cap height 56, gap 52, centred),
                line (horizontal + a bar 8 wide + STUDIO / LIVE at cap height 70)
  colour        one ink: sw-black #050505 on light, sw-white #F0F0F0 on dark; only the symbol may take a category colour
Design values (not in LOGO.md): the slots' centres at 46 from the ends of the ear (as in the 3D renders), letter gap 22, word gap 56.
The wordmark here is drawn from the rules (monoline, 60-degree A); if the original drawing of the wordmark turns up, it replaces this one.
Rendering: rsvg-convert.
"""
import math
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs/design/design-system/project/assets/Logo')
IMG = os.path.join(ROOT, 'docs/images')
BLACK, WHITE, TILE, GREY = '#050505', '#F0F0F0', '#101113', '#B4B4B4'
CATS = [('EQ', '#3b7fe6'), ('DYN', '#f0ad3d'), ('SAT', '#e0693f'), ('SPACE', '#7a6be0'), ('VOCAL', '#d4537e'),
        ('RESTORE', '#6aa832'), ('METER', '#a3a39f'), ('CREATIVE', '#22a87c'), ('BROADCAST', '#f2f2f2')]

STROKE = 13.0
H = STROKE / 2   # the centre line of a stroke is half a stroke inside the letter's box


def rrect(x, y, w, h, r):
    """a rounded rectangle as a closed path of lines and quarter arcs (clockwise)"""
    return (f'M{x + r:g} {y:g}H{x + w - r:g}A{r:g} {r:g} 0 0 1 {x + w:g} {y + r:g}V{y + h - r:g}'
            f'A{r:g} {r:g} 0 0 1 {x + w - r:g} {y + h:g}H{x + r:g}A{r:g} {r:g} 0 0 1 {x:g} {y + h - r:g}V{y + r:g}A{r:g} {r:g} 0 0 1 {x + r:g} {y:g}Z')


def ear(cx, cy, w, h, r, sw, sh, inset):
    """the ear: the plate and its two slots (even-odd: the slots are holes)"""
    x, y = cx - w / 2, cy - h / 2
    d = rrect(x, y, w, h, r)
    for sy in (y + inset, y + h - inset):
        d += rrect(cx - sw / 2, sy - sh / 2, sw, sh, sh / 2)
    return d


SYMBOL = ear(128, 128, 100, 236, 18, 52, 24, 46)
SYMBOL_SMALL = ear(128, 128, 140, 240, 24, 72, 36, 50)
EAR_BOX = (78, 10, 100, 236)   # the plate of the symbol on its 256 grid


# ---- the wordmark: centre lines of each letter on a cap height of 100 (y 0 = top), with its advance width
def glyphs():
    t, b, m = H, 100 - H, 50.0
    g = {}
    r = (m - t) / 2   # S: two bowls
    g['S'] = (88, f'M{88 - H:g} {t + r * 0.55:g}A{r:g} {r:g} 0 0 0 {88 - H - r:g} {t:g}H{H + r:g}A{r:g} {r:g} 0 0 0 {H + r:g} {m:g}'
                  f'H{88 - H - r:g}A{r:g} {r:g} 0 0 1 {88 - H - r:g} {b:g}H{H + r:g}A{r:g} {r:g} 0 0 1 {H:g} {b - r * 0.55:g}')
    g['W'] = (132, f'M{H:g} {t:g}L{H + 30:g} {b:g}L{66:g} {t + 22:g}L{132 - H - 30:g} {b:g}L{132 - H:g} {t:g}')
    half = (b - t) / math.tan(math.radians(60))
    wa = 2 * half + STROKE
    yc = t + (b - t) * 0.66
    xc = (yc - t) / math.tan(math.radians(60))
    g['A'] = (wa, f'M{H:g} {b:g}L{H + half:g} {t:g}L{H + 2 * half:g} {b:g}M{H + half - xc:g} {yc:g}H{H + half + xc:g}')
    ru = 35.5
    g['U'] = (2 * ru + STROKE, f'M{H:g} {t:g}V{b - ru:g}A{ru:g} {ru:g} 0 0 0 {H + 2 * ru:g} {b - ru:g}V{t:g}')
    rd = (b - t) / 2
    g['D'] = (92, f'M{H:g} {t:g}H{92 - H - rd:g}A{rd:g} {rd:g} 0 0 1 {92 - H - rd:g} {b:g}H{H:g}Z')
    g['I'] = (STROKE, f'M{H:g} {t:g}V{b:g}')
    g['O'] = (110, f'M{H + rd:g} {t:g}H{110 - H - rd:g}A{rd:g} {rd:g} 0 0 1 {110 - H - rd:g} {b:g}H{H + rd:g}A{rd:g} {rd:g} 0 0 1 {H + rd:g} {t:g}Z')
    g['T'] = (88, f'M{H:g} {t:g}H{88 - H:g}M44 {t:g}V{b:g}')
    g['L'] = (78, f'M{H:g} {t:g}V{b:g}H{78 - H:g}')
    g['V'] = (wa, f'M{H:g} {t:g}L{H + half:g} {b:g}L{H + 2 * half:g} {t:g}')
    g['E'] = (80, f'M{80 - H:g} {t:g}H{H:g}V{b:g}H{80 - H:g}M{H:g} {m:g}H{80 - H - 8:g}')
    return g


GLYPHS = glyphs()
GAP, SPACE = 22.0, 56.0


def word(text):
    """the centre-line path of a word on a cap height of 100 and its width"""
    x, parts = 0.0, []
    for i, ch in enumerate(text):
        if ch == ' ':
            x += SPACE - GAP; continue
        w, d = GLYPHS[ch]
        parts.append(f'<path transform="translate({x:g} 0)" d="{d}"/>')
        x += w + GAP
    return ''.join(parts), x - GAP


def word_group(text, x, y, cap, ink):
    """a word placed with its top left at (x, y) and a cap height of `cap`"""
    body, w = word(text)
    s = cap / 100
    return (f'<g transform="translate({x:g} {y:g}) scale({s:g})" fill="none" stroke="{ink}" stroke-width="{STROKE:g}" '
            f'stroke-linecap="round" stroke-linejoin="round">{body}</g>'), w * s


def svg(w, h, body, bg=None):
    rect = f'<rect width="{w:g}" height="{h:g}" fill="{bg}"/>' if bg else ''
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:g}" height="{h:g}" viewBox="0 0 {w:g} {h:g}">{rect}{body}</svg>\n'


def symbol_at(x, y, size, ink, small=False):
    s = size / 256
    return f'<path transform="translate({x:g} {y:g}) scale({s:g})" fill="{ink}" fill-rule="evenodd" d="{SYMBOL_SMALL if small else SYMBOL}"/>'


def horizontal(ink, sym_ink=None, line=None, pad=0.0):
    """symbol 256 high, the wordmark (cap 74) 44 after the ear, centred on the ear; with `line`: a bar 8 wide and the line name (cap 70)"""
    ex, ey, ew, eh = EAR_BOX
    x0 = pad - ex   # the ear's left edge at `pad`
    parts = [symbol_at(x0, pad, 256, sym_ink or ink)]
    wx = pad + ew + 44
    g, ww = word_group('SW AUDIO', wx, pad + 128 - 37, 74, ink)
    parts.append(g)
    right = wx + ww
    if line:
        bx = right + 44
        parts.append(f'<rect x="{bx:g}" y="{pad + 128 - 50:g}" width="8" height="100" fill="{ink}"/>')
        g2, w2 = word_group(line, bx + 8 + 44, pad + 128 - 35, 70, ink)
        parts.append(g2); right = bx + 8 + 44 + w2
    return ''.join(parts), right + pad, 256 + 2 * pad - 2 * ey + 0   # (width; the height keeps the 256 grid)


def stacked(ink, sym_ink=None, pad=0.0):
    _, ww = word('SW AUDIO'); ww *= 0.56
    width = max(256, ww) + 2 * pad
    parts = [symbol_at(width / 2 - 128, pad, 256, sym_ink or ink)]
    g, _ = word_group('SW AUDIO', width / 2 - ww / 2, pad + 246 + 52, 56, ink)
    parts.append(g)
    return ''.join(parts), width, pad + 246 + 52 + 56 + pad


def write(name, text):
    p = os.path.join(OUT, name)
    with open(p, 'w', encoding='utf-8') as f:
        f.write(text)
    return p


def png(src, dst, width):
    subprocess.run(['rsvg-convert', '-w', str(width), '-o', dst, src], check=True)


def main():
    os.makedirs(OUT, exist_ok=True); os.makedirs(IMG, exist_ok=True)
    files = {}
    files['sw-audio-symbol.svg'] = svg(256, 256, symbol_at(0, 0, 256, BLACK))
    files['sw-audio-symbol-small.svg'] = svg(256, 256, symbol_at(0, 0, 256, BLACK, small=True))
    body, w = word('SW AUDIO')
    files['sw-audio-wordmark.svg'] = svg(w, 100, f'<g fill="none" stroke="{BLACK}" stroke-width="{STROKE:g}" stroke-linecap="round" stroke-linejoin="round">{body}</g>')
    for name, line in (('sw-audio-horizontal.svg', None), ('sw-audio-studio.svg', 'STUDIO'), ('sw-audio-live.svg', 'LIVE')):
        b, w, h = horizontal(BLACK, line=line, pad=0)
        files[name] = svg(w, 256, b)
    b, w, h = stacked(BLACK)
    files['sw-audio-stacked.svg'] = svg(w, h, b)
    files['sw-audio-app-icon.svg'] = svg(1024, 1024, f'<rect width="1024" height="1024" rx="224" fill="{TILE}"/>' + symbol_at(512 - 340, 512 - 340, 680, WHITE, small=True))
    for name, text in files.items():
        p = write(name, text)
        png(p, p[:-4] + '.png', 512 if 'symbol' in name or 'icon' in name else 1200)

    # README banner: dark, the horizontal lockup in white, the nine category ears underneath
    pad = 96
    b, w, _ = horizontal(WHITE, pad=0)
    bw = 1600; scale = 0.62
    lock_w = w * scale
    ears = ''
    ew, eh, gap = 34, 80, 26
    total = len(CATS) * ew + (len(CATS) - 1) * gap
    for i, (_, col) in enumerate(CATS):
        ex = bw / 2 - total / 2 + i * (ew + gap)
        ears += f'<path fill="{col}" fill-rule="evenodd" d="{ear(ex + ew / 2, 0, ew, eh, 6, 18, 8, 16)}" transform="translate(0 {pad + 256 * scale + 70 + eh / 2:g})"/>'
    banner_h = pad + 256 * scale + 70 + eh + pad
    banner = svg(bw, banner_h, f'<g transform="translate({bw / 2 - lock_w / 2:g} {pad:g}) scale({scale:g})">{b}</g>' + ears, bg='#0c0c0d')
    bp = os.path.join(IMG, 'sw-audio-banner.svg')
    with open(bp, 'w', encoding='utf-8') as f: f.write(banner)
    png(bp, os.path.join(IMG, 'sw-audio-banner.png'), 1600)
    # the light version (white background, black ink) for places with a light theme
    b2, _, _ = horizontal(BLACK, pad=0)
    lp = os.path.join(IMG, 'sw-audio-banner-light.svg')
    with open(lp, 'w', encoding='utf-8') as f:
        f.write(svg(bw, banner_h, f'<g transform="translate({bw / 2 - lock_w / 2:g} {pad:g}) scale({scale:g})">{b2}</g>' + ears.replace('#f2f2f2', '#d8d8d8'), bg='#f7f7f5'))
    png(lp, os.path.join(IMG, 'sw-audio-banner-light.png'), 1600)
    png(os.path.join(OUT, 'sw-audio-app-icon.svg'), os.path.join(IMG, 'sw-audio-icon.png'), 256)
    print('\n'.join(sorted(files)) + '\nbanner: docs/images/sw-audio-banner.png')


if __name__ == '__main__':
    main()
