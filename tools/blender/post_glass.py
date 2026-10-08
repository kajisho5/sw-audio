#!/usr/bin/env python3
"""Turns tools/blender/vu_glass.py's render into a pure reflection overlay: the pane reflects a black studio, which would darken the meter face, so the
colour is made white (slightly warm where the warm box reflects) and the alpha the strength of the reflection. usage: post_glass.py <in.png> <out.webp> [gain=1.6] [soften_px=1.2]"""
import sys
import cv2
import numpy as np
im = cv2.imread(sys.argv[1], cv2.IMREAD_UNCHANGED).astype(np.float32) / 255.0
gain = float(sys.argv[3]) if len(sys.argv) > 3 else 1.6
b, g, r, a = im[..., 0], im[..., 1], im[..., 2], im[..., 3]
lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) * a                 # the reflected light (the colour is straight, the reflection is its share of the pixel)
lum = cv2.GaussianBlur(lum, (0, 0), float(sys.argv[4]) if len(sys.argv) > 4 else 1.2)    # the soft boxes' edges are blurred: a sheen, not a patch
alpha = np.clip(lum * gain, 0, 0.85)
warm = np.clip((r - b) * 0.5, 0, 0.3)
out = np.dstack([np.clip(1.0 - warm * 1.2, 0, 1), np.clip(0.99 - warm * 0.4, 0, 1), np.clip(0.97 - warm * 0.2 + 0.03, 0, 1), alpha])   # BGR + alpha
cv2.imwrite(sys.argv[2], (out * 255).astype(np.uint8), [cv2.IMWRITE_WEBP_QUALITY, 90])
