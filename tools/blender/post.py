#!/usr/bin/env python3
"""Denoise a Blender PNG (RGBA) with OpenCV and write WebP. This Blender build has no OpenImageDenoise (see RENDERING.md).
usage: post.py <in.png> <out.webp> [strength=4] [quality=92]"""
import sys
import cv2
import numpy as np

src, dst = sys.argv[1], sys.argv[2]
h = float(sys.argv[3]) if len(sys.argv) > 3 else 4
q = int(sys.argv[4]) if len(sys.argv) > 4 else 92
im = cv2.imread(src, cv2.IMREAD_UNCHANGED)
bgr, a = im[:, :, :3], im[:, :, 3]
# premultiply on a mid grey before denoising, so the transparent edge does not bleed black
f = a.astype(np.float32) / 255.0
bgr = (bgr.astype(np.float32) * f[..., None] + 128 * (1 - f[..., None])).astype(np.uint8)
den = cv2.fastNlMeansDenoisingColored(bgr, None, h, h, 5, 15)
den = ((den.astype(np.float32) - 128 * (1 - f[..., None])) / np.maximum(f[..., None], 1e-3)).clip(0, 255).astype(np.uint8)
a = cv2.GaussianBlur(a, (3, 3), 0.6)
cv2.imwrite(dst, np.dstack([den, a]), [cv2.IMWRITE_WEBP_QUALITY, q])
