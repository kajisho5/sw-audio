"""Shared denoiser for the Blender renders (this Blender build has no OpenImageDenoise, see RENDERING.md)."""
import cv2
import numpy as np


def denoise_rgba(im, h=4.0):
    """im: 8-bit BGRA as read by cv2 (straight alpha, as Blender writes PNG). Returns BGRA.

    The colour is premultiplied on a mid grey before denoising, so the transparent edge does not bleed black. Near the silhouette,
    dividing back by a small alpha blows up the denoiser's error (it turned teal rims into the state green, 2026-10-08): there the
    renderer's own straight colour is kept, moving to the denoised one as the alpha rises. The alpha is softened a little; the pixels
    that this makes visible (left empty by the renderer, colour 0) get the colour of the covered pixels next to them."""
    bgr, a = im[:, :, :3], im[:, :, 3]
    f = a.astype(np.float32) / 255.0
    pm = (bgr.astype(np.float32) * f[..., None] + 128 * (1 - f[..., None])).astype(np.uint8)
    den = cv2.fastNlMeansDenoisingColored(pm, None, h, h, 5, 15)
    un = (den.astype(np.float32) - 128 * (1 - f[..., None])) / np.maximum(f[..., None], 1e-3)
    w = np.clip((f - 0.15) / 0.45, 0, 1)[..., None]
    col = (un * w + bgr.astype(np.float32) * (1 - w)).clip(0, 255).astype(np.uint8)
    a2 = cv2.GaussianBlur(a, (3, 3), 0.6)
    empty = ((a == 0) & (a2 > 0)).astype(np.uint8)
    if empty.any():
        col = cv2.inpaint(col, empty, 2, cv2.INPAINT_TELEA)
    return np.dstack([col, a2])
