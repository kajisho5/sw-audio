#!/usr/bin/env python3
"""Cleanup of tools/blender/vu_bezel.py's render: the window shows only the shadow of the frame (smooth, so its alpha is blurred and the colour is black),
the frame itself is denoised. usage: post_frame.py <in.png> <out.webp> [frame_px_at_render=30] [shadow=0.75]"""
import sys
import cv2
import numpy as np
im = cv2.imread(sys.argv[1], cv2.IMREAD_UNCHANGED)
m = int(sys.argv[3]) if len(sys.argv) > 3 else 30
sh = float(sys.argv[4]) if len(sys.argv) > 4 else 0.75
h, w = im.shape[:2]
a = im[:, :, 3].astype(np.float32)
blur = cv2.GaussianBlur(a, (0, 0), 6)
mask = np.zeros((h, w), bool); mask[m:h - m, m:w - m] = True
a = np.where(mask, blur * sh, a).clip(0, 255).astype(np.uint8)
bgr = cv2.fastNlMeansDenoisingColored(im[:, :, :3], None, 3, 3, 5, 15)
bgr[mask] = 0
cv2.imwrite(sys.argv[2], np.dstack([bgr, a]), [cv2.IMWRITE_WEBP_QUALITY, 90])
