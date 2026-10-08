#!/usr/bin/env python3
"""Pack the frames of a spinning body (tools/blender/in07_orbital.py body ...) into one sprite sheet (a grid of frames).
usage: pack_sheet.py <frame-prefix> <out.webp> [strength=4] [quality=90] [columns=12]
Reads <prefix>_000.png, _001.png, ... (RGBA), denoises each with denoise.py (this Blender build has no OpenImageDenoise) and lays the
frames out in a grid, row by row from the top left (WebP is limited to 16383 px a side, so 96 frames do not fit in one row).
The screen shows frame k at column k % columns, row k // columns."""
import glob
import sys

import cv2
import numpy as np

from denoise import denoise_rgba


def main():
    prefix, dst = sys.argv[1], sys.argv[2]
    h = float(sys.argv[3]) if len(sys.argv) > 3 else 4
    q = int(sys.argv[4]) if len(sys.argv) > 4 else 90
    cols = int(sys.argv[5]) if len(sys.argv) > 5 else 12
    files = sorted(glob.glob(prefix + '_[0-9][0-9][0-9].png'))
    if not files:
        sys.exit(f'no frames for {prefix}')
    frames = [denoise_rgba(cv2.imread(f, cv2.IMREAD_UNCHANGED), h) for f in files]
    cols = min(cols, len(frames))
    rows = -(-len(frames) // cols)
    blank = np.zeros_like(frames[0])
    grid = [np.hstack([frames[r * cols + c] if r * cols + c < len(frames) else blank for c in range(cols)]) for r in range(rows)]
    cv2.imwrite(dst, np.vstack(grid), [cv2.IMWRITE_WEBP_QUALITY, q])
    print(f'{dst}: {len(frames)} frames of {frames[0].shape[1]} x {frames[0].shape[0]}, {cols} x {rows}')


if __name__ == '__main__':
    main()
