#!/usr/bin/env python3
"""
StudioFros - Megaphone: builds the plugin artwork from the designer's panel.

Input : Design/panel_original.jpg  (1920 x 1280, the full panel as designed)
Output: Assets/panel.jpg           panel without the parts that move in the plugin
        Assets/*.png               one small sprite per switch position / LED state
        Source/PanelLayout.h       coordinates used by the editor (generated)

What is removed from the panel image (the plugin draws it live instead):
  * the pointer line of every knob   (drawn by code at the right angle)
  * the text on the LCD              (drawn by code)
  * the stray "BOTH" caption + LED next to BYPASS, and the "I/II" captions of MODE
    (fixed here: they become "I" and "II")

Usage:  python3 tools/make_assets.py [--preview preview.png]
Needs:  python3, numpy, opencv-python, pillow (preview only)
"""
import math
import os
import sys

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Design", "panel_original.jpg")
ASSETS = os.path.join(ROOT, "Assets")
HEADER = os.path.join(ROOT, "Source", "PanelLayout.h")

W, H = 1920, 1280

# ---------------------------------------------------------------------------
# Layout (all in panel pixels, 1920 x 1280)
# ---------------------------------------------------------------------------
# enum name in ParameterSpecs.h, label, centre x, centre y, radius of the rotating cap
KNOBS = [
    ("pLowCut",   "LOW CUT",  273.5, 266.0, 31.0),
    ("pHighCut",  "HIGH CUT", 413.0, 275.0, 29.0),
    ("pHonk",     "HONK",     568.0, 262.0, 46.0),
    ("pDrive",    "DRIVE",    753.0, 256.0, 46.0),
    ("pMix",      "MIX",     1415.0, 272.0, 45.0),
    ("pOutput",   "OUTPUT",  1597.0, 275.0, 48.0),
    ("pFeedback", "FEEDBACK", 1160.0, 480.0, 46.0),
    ("pNoise",    "NOISE",    1163.0, 714.0, 46.0),
]

LCD_GLASS = (327, 632, 548, 140)            # x, y, w, h  (the green area)
LCD_TEXT = (345, 650, 513, 105)             # area where text may go
BTN_PREV = (411, 827, 122, 65)
BTN_NEXT = (665, 825, 125, 67)
BTN_SAVE = (275, 962, 143, 140)
BTN_SAVEAS = (442, 962, 146, 140)
BTN_DELETE = (611, 962, 144, 140)
BTN_INIT = (779, 962, 143, 140)
TRIGGER = (1152.0, 995.0, 105.0)            # centre x, centre y, outer radius

# Sprites: name -> (centre x, centre y, half width, half height)
SPR_COMP = (927, 302, 48, 48)
SPR_MODE = (1153, 272, 48, 48)
SPR_NOISE = (1502, 639, 102, 88)
SPR_BYPASS = (1500, 1005, 102, 88)
SPR_LED = (1613, 872, 30, 30)


def rect_of(spr):
    cx, cy, hw, hh = spr
    return (cx - hw, cy - hh, 2 * hw, 2 * hh)


# ---------------------------------------------------------------------------
def load():
    im = cv2.imread(SRC, cv2.IMREAD_COLOR)
    if im is None or im.shape[1] != W or im.shape[0] != H:
        sys.exit("Design/panel_original.jpg must be %dx%d" % (W, H))
    return im


def inpaint(img, mask, radius=5):
    return cv2.inpaint(img, mask, radius, cv2.INPAINT_TELEA)


def dilate(mask, n):
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * n + 1, 2 * n + 1))
    return cv2.dilate(mask, k)


def disc_alpha(w, h, cx, cy, r, feather):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.hypot(xx - cx, yy - cy)
    return np.clip((r - d) / feather, 0.0, 1.0)


def shape_alpha(w, h, rects, sigma=1.5):
    m = np.zeros((h, w), np.float32)
    for x0, y0, x1, y1 in rects:
        m[y0:y1, x0:x1] = 1.0
    return cv2.GaussianBlur(m, (0, 0), sigma)


# ---------------------------------------------------------------------------
# 1. clean the panel
# ---------------------------------------------------------------------------
def erase_knob_pointers(bg, gray):
    mask = np.zeros((H, W), np.uint8)
    for _, _, cx, cy, r in KNOBS:
        disc = np.zeros((H, W), np.uint8)
        cv2.circle(disc, (int(round(cx)), int(round(cy))), int(r * 1.12), 255, -1)
        bright = (gray > 120).astype(np.uint8) * 255
        mask |= cv2.bitwise_and(disc, bright)
    mask = dilate(mask, 2)
    return inpaint(bg, mask, 5)


def erase_lcd_text(bg, gray):
    x0, y0, x1, y1 = 360, 668, 840, 738
    sub = np.zeros((H, W), np.uint8)
    sub[y0:y1, x0:x1] = (gray[y0:y1, x0:x1] < 85).astype(np.uint8) * 255
    sub = dilate(sub, 3)
    return inpaint(bg, sub, 4)


def fix_bypass_decoration(bg, gray):
    # stray "BOTH" caption
    m = np.zeros((H, W), np.uint8)
    x0, y0, x1, y1 = 1318, 986, 1394, 1026
    m[y0:y1, x0:x1] = (gray[y0:y1, x0:x1] > 115).astype(np.uint8) * 255
    bg = inpaint(bg, dilate(m, 4), 5)

    # the left screw of the plate was drawn as a red LED: copy the right screw, mirrored
    src_cx, dst_cx, cy, r = 1578, 1422, 1005, 23
    patch = bg[cy - r - 2:cy + r + 2, src_cx - r - 2:src_cx + r + 2][:, ::-1].astype(np.float32)
    a = disc_alpha(patch.shape[1], patch.shape[0], patch.shape[1] / 2, patch.shape[0] / 2, r + 1, 5)[..., None]
    dst = bg[cy - r - 2:cy + r + 2, dst_cx - r - 2:dst_cx + r + 2].astype(np.float32)
    bg[cy - r - 2:cy + r + 2, dst_cx - r - 2:dst_cx + r + 2] = (patch * a + dst * (1 - a)).astype(np.uint8)
    return bg


def fix_mode_captions(bg, orig, gray):
    """The MODE captions both read 'I/II'. Make the top one 'I' and the bottom one 'II'."""
    top = (1128, 192, 1184, 230)
    bot = (1128, 318, 1184, 360)

    def comps(box):
        x0, y0, x1, y1 = box
        m = (gray[y0:y1, x0:x1] > 130).astype(np.uint8)
        n, lab, stats, _ = cv2.connectedComponentsWithStats(m, 8)
        items = [(stats[i], i) for i in range(1, n) if stats[i][4] >= 25]
        items.sort(key=lambda t: t[0][0])
        return lab, items

    lab, items = comps(top)
    if len(items) != 4:
        print("WARNING: expected 4 glyphs in the MODE caption, found %d" % len(items))
        return bg

    def glyph(lab, ids, box):
        x0, y0, _, _ = box
        keep = np.isin(lab, ids).astype(np.uint8)
        keep = dilate(keep, 2)
        ys, xs = np.nonzero(keep)
        gx0, gx1, gy0, gy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        g = gray[y0 + gy0:y0 + gy1, x0 + gx0:x0 + gx1].astype(np.float32)
        a = np.clip((g - 80.0) / 95.0, 0, 1) * keep[gy0:gy1, gx0:gx1]
        col = orig[y0 + gy0:y0 + gy1, x0 + gx0:x0 + gx1][a > 0.9]
        colour = np.median(col, axis=0) if len(col) else np.array([205, 215, 225])
        return a, colour, (x0 + gx0, y0 + gy0)

    g_I = glyph(lab, [items[0][1]], top)
    g_II = glyph(lab, [items[2][1], items[3][1]], top)

    # erase both captions
    m = np.zeros((H, W), np.uint8)
    for x0, y0, x1, y1 in (top, bot):
        m[y0:y1, x0:x1] = (gray[y0:y1, x0:x1] > 95).astype(np.uint8) * 255
    bg = inpaint(bg, dilate(m, 3), 4)

    def put(g, cx, cy):
        a, colour, _ = g
        h, w = a.shape
        x0, y0 = int(round(cx - w / 2)), int(round(cy - h / 2))
        reg = bg[y0:y0 + h, x0:x0 + w].astype(np.float32)
        reg = reg * (1 - a[..., None]) + colour.astype(np.float32) * a[..., None]
        bg[y0:y0 + h, x0:x0 + w] = reg.astype(np.uint8)

    top_cy = 211.0
    bot_cy = 339.0
    put(g_I, 1154.0, top_cy)
    put(g_II, 1154.0, bot_cy)
    return bg


# ---------------------------------------------------------------------------
# 2. sprites
# ---------------------------------------------------------------------------
def crop(img, spr):
    cx, cy, hw, hh = spr
    return img[cy - hh:cy + hh, cx - hw:cx + hw].copy()


def rgba(bgr, alpha):
    a = np.clip(alpha * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return np.dstack([bgr, a])


def save_png(name, arr):
    path = os.path.join(ASSETS, name)
    cv2.imwrite(path, arr, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    return arr


# Light comes from the top-left, like in the designer's picture.
_LIGHT = np.array([-0.45, -0.55, 0.70], np.float32)
_LIGHT /= np.linalg.norm(_LIGHT)


def _chrome_ball(xs, ys, cx, cy, R):
    """Colour (h, w, 3) and coverage (h, w) of a chrome ball."""
    dx, dy = (xs - cx) / R, (ys - cy) / R
    d2 = dx * dx + dy * dy
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    diff = np.clip(dx * _LIGHT[0] + dy * _LIGHT[1] + nz * _LIGHT[2], 0, 1)
    spec = np.power(diff, 26)
    sky = np.clip(0.55 - dy * 0.45, 0, 1)
    val = 40 + 135 * np.power(diff, 0.85) + 60 * sky * nz + 170 * np.power(diff, 40)
    val *= (0.30 + 0.70 * np.power(nz, 0.45))                  # dark rim like the designer's balls
    col = np.dstack([val * 0.95, val * 1.0, val * 1.04])        # silver, slightly warm (BGR)
    cov = np.clip(R - np.sqrt(d2) * R + 0.5, 0, 1)
    return col, cov


def draw_lever(img, p0, p1, R, hw0, hw1):
    """
    Draws a chrome toggle lever on `img` (local pixel coordinates):
    a tapered shaft from p0 (the pivot) to p1 plus a ball of radius R at p1.
    The light always comes from the same side, so a lever pointing up or down
    gets the same kind of highlights and shadows as the rest of the panel.
    """
    h, w = img.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    xs += 0.5
    ys += 0.5
    out = img.astype(np.float32)
    p0 = np.array(p0, np.float32)
    p1 = np.array(p1, np.float32)
    d = p1 - p0
    L = float(np.linalg.norm(d))
    u = d / L if L > 1e-3 else np.array([0.0, 1.0], np.float32)
    n = np.array([-u[1], u[0]], np.float32)

    px, py = xs - p0[0], ys - p0[1]
    t = px * u[0] + py * u[1]
    s = px * n[0] + py * n[1]
    hw = hw0 + (hw1 - hw0) * np.clip(t / max(L, 1.0), 0, 1)

    shaft_cov = np.zeros((h, w), np.float32)
    if L > 1.0:
        shaft_cov = (np.clip(hw - np.abs(s) + 0.5, 0, 1)
                     * np.clip(t + 0.5, 0, 1) * np.clip(L - t + 1.5, 0, 1))
    ball_col, ball_cov = _chrome_ball(xs, ys, p1[0], p1[1], R)
    mask = np.maximum(shaft_cov, ball_cov)

    # dark well where the lever enters the nut
    well = np.clip((hw0 + 5.0 - np.hypot(xs - p0[0], ys - p0[1])) / 5.0, 0, 1)
    out *= (1 - 0.75 * well[..., None])

    # soft shadow thrown on the plate (towards the bottom-right)
    sh = cv2.GaussianBlur(mask, (0, 0), 2.6)
    M = np.float32([[1, 0, 2.5], [0, 1, 4.5]])
    sh = cv2.warpAffine(sh, M, (w, h))
    out *= (1 - 0.5 * sh[..., None])

    if L > 1.0:
        nx = np.clip(s / np.maximum(hw, 1e-3), -1, 1)
        nz = np.sqrt(np.clip(1 - nx * nx, 0, 1))
        diff = np.clip(nx * n[0] * _LIGHT[0] + nx * n[1] * _LIGHT[1] + nz * _LIGHT[2], 0, 1)
        val = 30 + 150 * np.power(diff, 1.0) + 150 * np.power(diff, 36)
        val *= (0.30 + 0.70 * np.power(nz, 0.6))
        val *= (0.5 + 0.5 * np.clip(t / (0.4 * L), 0, 1))
        shaft_col = np.dstack([val * 0.95, val * 1.0, val * 1.04])
        out = out * (1 - shaft_cov[..., None]) + shaft_col * shaft_cov[..., None]

    out = out * (1 - ball_cov[..., None]) + ball_col * ball_cov[..., None]
    return np.clip(out, 0, 255).astype(np.uint8)


# name -> pivot, lever of the designer's picture (tip, ball radius), sprite area,
#         lever half widths (pivot, ball), shape of the opaque area, sprites per position
SWITCHES = {
    "comp": dict(nut_r=38, spr=SPR_COMP, pivot=(927, 302), orig=((925, 282), 15), hw=(6.5, 7.5), shape="disc",
                 states=[("comp_off.png", (926, 326), 13), ("comp_on.png", (925, 279), 13)]),
    "mode": dict(nut_r=37, spr=SPR_MODE, pivot=(1153, 272), orig=((1166, 263), 17), hw=(7.0, 8.5), shape="disc",
                 states=[("mode_1.png", (1161, 249), 15), ("mode_2.png", (1161, 296), 15)]),
    "noise": dict(nut_r=38, spr=SPR_NOISE, pivot=(1501, 639), orig=((1513, 690), 19), hw=(7.5, 9.5), shape="noise",
                  states=[("noise_click.png", (1513, 588), 16), ("noise_both.png", (1501, 639), 16),
                          ("noise_burst.png", (1513, 690), 16)]),
    "bypass": dict(nut_r=38, spr=SPR_BYPASS, pivot=(1500, 1003), orig=((1510, 1058), 19), hw=(7.5, 9.5), shape="bypass",
                   states=[("bypass_active.png", (1510, 1058), 16), ("bypass_off.png", (1510, 950), 16)]),
}


def refill_nut(img, mask, pivot, nut_r):
    """
    Fills the masked pixels of the nut by interpolating along its rings (polar
    coordinates around the pivot), so concentric rings and the light falling on
    them continue across the gap instead of being smeared.
    """
    S = nut_r + 8
    px, py = pivot
    x0, y0 = px - S, py - S
    crop = img[y0:y0 + 2 * S, x0:x0 + 2 * S].copy()
    m = mask[y0:y0 + 2 * S, x0:x0 + 2 * S]
    A = 720
    flags = cv2.INTER_LINEAR | cv2.WARP_POLAR_LINEAR
    pol = cv2.warpPolar(crop, (S, A), (S, S), S, flags).astype(np.float32)
    pm = cv2.warpPolar(m, (S, A), (S, S), S, flags)
    pm = (pm > 10).astype(np.uint8)
    pm = cv2.dilate(pm, np.ones((9, 1), np.uint8))              # a little wider in angle
    rows = np.arange(A)
    for c in range(S):
        bad = pm[:, c] > 0
        if bad.all() or not bad.any():
            continue
        good = ~bad
        for ch in range(3):
            pol[bad, c, ch] = np.interp(rows[bad], rows[good], pol[good, c, ch], period=A)
    back = cv2.warpPolar(pol.astype(np.uint8), (2 * S, 2 * S), (S, S), S, flags | cv2.WARP_INVERSE_MAP)
    yy, xx = np.mgrid[0:2 * S, 0:2 * S].astype(np.float32)
    inside = np.clip((nut_r - np.hypot(xx - S, yy - S)) / 3.0, 0, 1)
    use = (m > 0).astype(np.float32) * inside
    use = cv2.GaussianBlur(use, (0, 0), 0.8)[..., None]
    out = img.copy()
    out[y0:y0 + 2 * S, x0:x0 + 2 * S] = (back.astype(np.float32) * use + crop.astype(np.float32) * (1 - use)).astype(np.uint8)
    # what is left of the mask (plate under the lever, outside the nut) is ordinary metal
    left = mask.copy()
    left[y0:y0 + 2 * S, x0:x0 + 2 * S] = (m > 0).astype(np.uint8) * 255 * (inside < 0.5).astype(np.uint8)
    res = inpaint(out, left, 7)
    return add_grain(res, left)


def add_grain(img, mask):
    """Inpainting leaves smooth patches; put back noise like the surrounding metal."""
    if not mask.any():
        return img
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    hp = gray - cv2.GaussianBlur(gray, (0, 0), 2.0)
    near = (dilate(mask, 14) > 0) & (dilate(mask, 3) == 0)
    vals = hp[near]
    sigma = 1.4826 * np.median(np.abs(vals - np.median(vals))) if len(vals) else 3.0
    rng = np.random.default_rng(7)
    noise = cv2.GaussianBlur(rng.normal(0, 1, gray.shape).astype(np.float32), (0, 0), 0.7) * sigma * 1.9
    w = cv2.GaussianBlur((dilate(mask, 2) > 0).astype(np.float32), (0, 0), 1.5)
    return np.clip(img.astype(np.float32) + (noise * w)[..., None], 0, 255).astype(np.uint8)


def build_sprites(bg):
    for name, cfg in SWITCHES.items():
        # 1. the picture without its lever: plate and nut stay exactly as designed
        mask = np.zeros((H, W), np.uint8)
        px, py = cfg["pivot"]
        (tx, ty), tr = cfg["orig"]
        cv2.line(mask, (px, py), (tx, ty), 255, int(2 * (cfg["hw"][1] + 4)))
        cv2.circle(mask, (tx, ty), tr, 255, -1)
        base_full = refill_nut(bg, mask, cfg["pivot"], cfg["nut_r"])

        cx, cy, hw_, hh_ = cfg["spr"]
        x0, y0 = cx - hw_, cy - hh_
        base = base_full[y0:y0 + 2 * hh_, x0:x0 + 2 * hw_].copy()
        h, w = base.shape[:2]

        # 2. opaque area of the sprite
        if cfg["shape"] == "disc":
            alpha = disc_alpha(w, h, w / 2, h / 2, w / 2 - 2, 9)
        elif cfg["shape"] == "noise":
            alpha = shape_alpha(w, h, [(38, 7, 163, 169), (5, 64, 197, 112)])
        else:
            alpha = shape_alpha(w, h, [(37, 3, 165, 173), (4, 64, 199, 112)])

        # 3. one sprite per lever position
        for fname, tip, R in cfg["states"]:
            p0 = (px - x0, py - y0)
            p1 = (tip[0] - x0, tip[1] - y0)
            img = draw_lever(base, p0, p1, R, cfg["hw"][0], cfg["hw"][1])
            save_png(fname, rgba(img, alpha))

    # -- LED ----------------------------------------------------------------------
    led = crop(bg, SPR_LED)
    h, w = led.shape[:2]
    a = disc_alpha(w, h, w / 2, h / 2, w / 2 - 1, 7)
    save_png("led_on.png", rgba(led, a))
    off = led.astype(np.float32)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    lc = (w / 2 - 0.3, h / 2 - 0.3)
    d = np.hypot(xs - lc[0], ys - lc[1])
    lens = np.clip((14.5 - d) / 2.0, 0, 1)[..., None]
    shade = 0.5 + 0.5 * np.clip(d / 14.5, 0, 1)
    dark = np.dstack([np.full_like(d, 14) * (1.2 - 0.4 * shade),
                      np.full_like(d, 18) * (1.2 - 0.4 * shade),
                      np.full_like(d, 66) * (1.2 - 0.3 * shade)])
    hl = np.exp(-(((xs - lc[0] + 4) ** 2 + (ys - lc[1] + 5) ** 2) / (2 * 2.6 ** 2)))[..., None]
    dark = dark + hl * np.array([40, 50, 90], np.float32)
    off = off * (1 - lens) + dark * lens
    save_png("led_off.png", rgba(np.clip(off, 0, 255).astype(np.uint8), a))


# ---------------------------------------------------------------------------
# 3. header with the coordinates
# ---------------------------------------------------------------------------
def write_header():
    L = []
    L.append("// StudioFros - Megaphone")
    L.append("// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).")
    L.append("//")
    L.append("// GENERATED by tools/make_assets.py - do not edit by hand.")
    L.append("// Coordinates of the controls on the 1920 x 1280 panel image.")
    L.append("#pragma once")
    L.append("")
    L.append('#include "ParameterSpecs.h"')
    L.append("")
    L.append("namespace panel")
    L.append("{")
    L.append("struct Rect { int x, y, w, h; };")
    L.append("struct KnobPos { int param; float cx, cy, capRadius; const char* label; };")
    L.append("struct Disc { float cx, cy, r; };")
    L.append("")
    L.append("inline constexpr int kWidth = %d;" % W)
    L.append("inline constexpr int kHeight = %d;" % H)
    L.append("")
    L.append("inline constexpr KnobPos kKnobs[] = {")
    for e, label, cx, cy, r in KNOBS:
        L.append('    { studiofros::%s, %.1ff, %.1ff, %.1ff, "%s" },' % (e, cx, cy, r, label))
    L.append("};")
    L.append("")

    def rc(name, r):
        L.append("inline constexpr Rect %s { %d, %d, %d, %d };" % (name, *r))

    rc("kLcdGlass", LCD_GLASS)
    rc("kLcdText", LCD_TEXT)
    rc("kBtnPrev", BTN_PREV)
    rc("kBtnNext", BTN_NEXT)
    rc("kBtnSave", BTN_SAVE)
    rc("kBtnSaveAs", BTN_SAVEAS)
    rc("kBtnDelete", BTN_DELETE)
    rc("kBtnInit", BTN_INIT)
    L.append("inline constexpr Disc kTrigger { %.1ff, %.1ff, %.1ff };" % TRIGGER)
    L.append("")
    rc("kSprComp", rect_of(SPR_COMP))
    rc("kSprMode", rect_of(SPR_MODE))
    rc("kSprNoise", rect_of(SPR_NOISE))
    rc("kSprBypass", rect_of(SPR_BYPASS))
    rc("kSprLed", rect_of(SPR_LED))
    L.append("} // namespace panel")
    L.append("")
    with open(HEADER, "w") as f:
        f.write("\n".join(L))


# ---------------------------------------------------------------------------
# 4. preview (what the plugin looks like with default settings)
# ---------------------------------------------------------------------------
def preview(path):
    from PIL import Image, ImageDraw, ImageFont

    img = cv2.imread(os.path.join(ASSETS, "panel.jpg")).astype(np.float32)

    def paste(name, rect):
        s = cv2.imread(os.path.join(ASSETS, name), cv2.IMREAD_UNCHANGED).astype(np.float32)
        x, y, w, h = rect
        a = s[..., 3:4] / 255.0
        img[y:y + h, x:x + w] = img[y:y + h, x:x + w] * (1 - a) + s[..., :3] * a

    paste("comp_on.png", rect_of(SPR_COMP))
    paste("mode_1.png", rect_of(SPR_MODE))
    paste("noise_both.png", rect_of(SPR_NOISE))
    paste("bypass_active.png", rect_of(SPR_BYPASS))
    paste("led_on.png", rect_of(SPR_LED))
    out = np.clip(img, 0, 255).astype(np.uint8)

    defaults = {"pLowCut": 0.5, "pHighCut": 0.5, "pHonk": 0.5, "pDrive": 0.4,
                "pMix": 1.0, "pOutput": 0.667, "pFeedback": 0.0, "pNoise": 0.2}
    for e, _, cx, cy, r in KNOBS:
        ang = math.radians(-150 + 300 * defaults[e])
        dx, dy = math.sin(ang), -math.cos(ang)
        p0 = (cx + dx * r * 0.06, cy + dy * r * 0.06)
        p1 = (cx + dx * r * 0.90, cy + dy * r * 0.90)
        S = 4
        sc = lambda p: (int(p[0] * S), int(p[1] * S))
        cv2.line(out, sc((p0[0] + 1, p0[1] + 1.5)), sc((p1[0] + 1, p1[1] + 1.5)), (10, 10, 10), max(1, int(r * 0.17)), cv2.LINE_AA, 2)
        cv2.line(out, sc(p0), sc(p1), (205, 226, 232), max(1, int(r * 0.12)), cv2.LINE_AA, 2)

    # LCD text as dots
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 13)
    txt = Image.new("L", (160, 16), 0)
    ImageDraw.Draw(txt).text((0, 1), "Classic Bullhorn", font=font, fill=255)
    arr = np.array(txt)
    pitch = 4.3
    ox = LCD_GLASS[0] + LCD_GLASS[2] / 2 - 16 * 7.8 * pitch / 7.8 / 2 * 1.0
    ox = 372
    oy = 700 - 8 * pitch
    for yy in range(arr.shape[0]):
        for xx in range(arr.shape[1]):
            v = arr[yy, xx] / 255.0
            if v > 0.3:
                cv2.circle(out, (int((ox + xx * pitch) * 4), int((oy + yy * pitch) * 4)), int(pitch * 0.42 * 4),
                           (12, 28, 20), -1, cv2.LINE_AA, 2)
    cv2.imwrite(path, out)
    print("preview ->", path)


# ---------------------------------------------------------------------------
def main():
    os.makedirs(ASSETS, exist_ok=True)
    orig = load()
    gray = cv2.cvtColor(orig, cv2.COLOR_BGR2GRAY)

    bg = orig.copy()
    bg = fix_mode_captions(bg, orig, gray)
    bg = fix_bypass_decoration(bg, gray)
    gray2 = cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY)
    build_sprites(bg)                      # sprites are cut from the panel before anything dynamic is removed
    bg = erase_knob_pointers(bg, gray2)
    bg = erase_lcd_text(bg, gray2)

    cv2.imwrite(os.path.join(ASSETS, "panel.jpg"), bg, [cv2.IMWRITE_JPEG_QUALITY, 93])
    write_header()
    print("assets written to", ASSETS)

    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
