#!/usr/bin/env python3
"""Denoise a Blender PNG (RGBA) with OpenCV and write WebP. This Blender build has no OpenImageDenoise (see RENDERING.md).
usage: post.py <in.png> <out.webp> [strength=4] [quality=92]"""
import sys
import cv2

from denoise import denoise_rgba

src, dst = sys.argv[1], sys.argv[2]
h = float(sys.argv[3]) if len(sys.argv) > 3 else 4
q = int(sys.argv[4]) if len(sys.argv) > 4 else 92
im = cv2.imread(src, cv2.IMREAD_UNCHANGED)
cv2.imwrite(dst, denoise_rgba(im, h), [cv2.IMWRITE_WEBP_QUALITY, q])
