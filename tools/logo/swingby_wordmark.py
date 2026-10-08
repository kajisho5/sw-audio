#!/usr/bin/env python3
"""SWINGBY wordmark (SW IN07) built from the SW AUDIO wordmark's glyph system.

usage: swingby_wordmark.py <sw-audio-wordmark.svg> <out-dir>

S, W and I are taken unchanged from the SW AUDIO wordmark (the distributed path data: one outlined path, cap height 56,
monoline 6.44, round ends). N, G, B and Y are new, drawn on the same centre lines with the same stroke, round caps and
round joins, the same corner radius as the O and D (15) and the same gap between letters (11.4), then outlined so the
result is plain filled paths like the brand data. Needs skia-pathops (pip install skia-pathops).
Writes swingby-dark.svg (SW in cat-creative, rest sw-white), swingby-light.svg (SW in cat-creative, rest sw-black),
swingby-white.svg, swingby-black.svg (one colour) and swingby-mono.svg (currentColor)."""
import os
import re
import sys

import pathops

CAT = '#22a87c'      # cat-creative (02_design_tokens.json)
WHITE = '#F0F0F0'    # sw-white
BLACK = '#050505'    # sw-black
W = 6.44             # stroke (outer - inner of the D and O)
Y0, Y1 = 311.22, 360.78   # centre lines of the top and bottom strokes (cap 308..364)
YM = (Y0 + Y1) / 2
GAP = 11.4           # gap between letter outlines in the SW AUDIO wordmark
KAPPA = 0.5522847    # cubic Bezier handle of a quarter circle


def subpaths(svg_path):
    d = re.search(r' d="([^"]+)"', open(svg_path).read()).group(1)
    return [s for s in re.split(r'(?=M)', d) if s.strip()]


def from_svg(sub, dx):
    """an outlined glyph of the brand data (M / L / Z only), moved by dx"""
    p = pathops.Path()
    for cmd, xs in re.findall(r'([MLZ])([^MLZ]*)', sub):
        n = list(map(float, re.findall(r'-?\d+\.?\d*', xs)))
        if cmd == 'M':
            p.moveTo(n[0] + dx, n[1])
        elif cmd == 'L':
            for i in range(0, len(n), 2):
                p.lineTo(n[i] + dx, n[i + 1])
        else:
            p.close()
    return p


class Pen:
    """a centre line: straight lines and quarter-circle corners (cubic Beziers; the conic input of skia-pathops 0.8
    came out as the straight chord)"""
    def __init__(self):
        self.p = pathops.Path()
        self.at = (0.0, 0.0)

    def m(self, x, y):
        self.p.moveTo(x, y); self.at = (x, y); return self

    def l(self, x, y):
        self.p.lineTo(x, y); self.at = (x, y); return self

    def corner(self, cx, cy, x, y):
        """a quarter circle from the current point to (x, y) around the square corner (cx, cy)"""
        x0, y0 = self.at
        self.p.cubicTo(x0 + KAPPA * (cx - x0), y0 + KAPPA * (cy - y0), x + KAPPA * (cx - x), y + KAPPA * (cy - y), x, y)
        self.at = (x, y); return self

    def outline(self):
        self.p.stroke(W, pathops.LineCap.ROUND_CAP, pathops.LineJoin.ROUND_JOIN, 4)
        self.p.convertConicsToQuads(0.002)
        return self.p


def union(paths):
    out = pathops.Path()
    for p in paths:
        out.addPath(p)
    out.simplify()
    return out


def d_of(path, dy):
    parts = []
    f = lambda x, y: f'{x:.2f} {y + dy:.2f}'
    for verb, pts in path.segments:
        if verb == 'moveTo':
            parts.append('M' + f(*pts[0]))
        elif verb == 'lineTo':
            parts.append('L' + f(*pts[0]))
        elif verb == 'qCurveTo':
            # pathops gives TrueType-style quadratic splines: implied on-curve points between off-curve points
            off, end = pts[:-1], pts[-1]
            for i, c in enumerate(off):
                nxt = end if i == len(off) - 1 else ((c[0] + off[i + 1][0]) / 2, (c[1] + off[i + 1][1]) / 2)
                parts.append('Q' + f(*c) + ' ' + f(*nxt))
        elif verb == 'curveTo':
            parts.append('C' + ' '.join(f(*q) for q in pts))
        elif verb == 'closePath':
            parts.append('Z')
    return ''.join(parts)


def build(src):
    subs = subpaths(src)
    S, Wg, I = subs[1], subs[0], subs[7]          # S 0..57.98, W 69.38..150.14, I 401.15..407.59 in the brand data
    h = W / 2
    x = 150.14 + GAP                               # right edge of W + gap
    i_dx = x - 401.15
    glyph_I = from_svg(I, i_dx)
    x += 6.44 + GAP
    # N: outer width 58 (like U)
    a, b = x + h, x + 58 - h
    N = Pen().m(a, Y1).l(a, Y0).l(b, Y1).l(b, Y0).outline()
    x += 58 + GAP
    # G: outer width 62, corners R 15 like the O; the top stroke runs to the right edge (open on the right down to the bar)
    g0, g1, R = x + h, x + 62 - h, 15.0
    G = (Pen().m(g1, Y0).l(g0 + R, Y0).corner(g0, Y0, g0, Y0 + R).l(g0, Y1 - R).corner(g0, Y1, g0 + R, Y1)
         .l(g1 - R, Y1).corner(g1, Y1, g1, Y1 - R).l(g1, YM).l(g1 - 24, YM).outline())
    x += 62 + GAP
    # B: outer width 56, top bowl a little narrower, bowls R 12 (half the bowl height, like the S loops)
    b0, bt, bb, r = x + h, x + 51 - h, x + 56 - h, 12.0
    B_stem = Pen().m(b0, Y1).l(b0, Y0).outline()
    B_top = (Pen().m(b0, Y0).l(bt - r, Y0).corner(bt, Y0, bt, Y0 + r).l(bt, YM - r).corner(bt, YM, bt - r, YM)
             .l(b0, YM).outline())
    B_bot = (Pen().m(b0, YM).l(bb - r, YM).corner(bb, YM, bb, YM + r).l(bb, Y1 - r).corner(bb, Y1, bb - r, Y1)
             .l(b0, Y1).outline())
    x += 56 + GAP
    # Y: outer width 52, arms meet at mid height
    y0, y1, yc = x + h, x + 52 - h, x + 26
    Y_arms = Pen().m(y0, Y0).l(yc, YM).l(y1, Y0).outline()
    Y_stem = Pen().m(yc, YM).l(yc, Y1).outline()
    width = x + 52
    sw = union([from_svg(S, 0), from_svg(Wg, 0)])
    rest = union([glyph_I, N, G, B_stem, B_top, B_bot, Y_arms, Y_stem])
    return d_of(sw, -308), d_of(rest, -308), width


def svg(d_sw, d_rest, width, c_sw, c_rest):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:.2f} 56"><title>SWINGBY</title>'
            f'<path fill="{c_sw}" d="{d_sw}"/><path fill="{c_rest}" d="{d_rest}"/></svg>\n')


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    d_sw, d_rest, width = build(src)
    for name, a, b in (('dark', CAT, WHITE), ('light', CAT, BLACK), ('white', WHITE, WHITE), ('black', BLACK, BLACK),
                       ('mono', 'currentColor', 'currentColor')):
        open(os.path.join(out, f'swingby-{name}.svg'), 'w').write(svg(d_sw, d_rest, width, a, b))
    print(f'width {width:.2f} x 56 -> {out}')


if __name__ == '__main__':
    main()
