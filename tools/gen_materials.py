#!/usr/bin/env python3
"""Generate the 256x256 material atlas (4x4 cells of 64x64) for THE ASHEN KNIGHT.

Cell order must match the MAT_* enum in src/game.h:

   0 stone      1 cobble     2 flag     3 plank
   4 water0     5 water1     6 water2   7 water3
   8 slate      9 lime      10 iron    11 banner
  12 earth     13 moss      14 gold    15 dark

Everything wraps at the cell border so large surfaces can be tiled by mirroring.
Emits assets/_materials.png and src/materials_data.c.
"""
import os
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CELL = 64
GRID = 4
SIZE = CELL * GRID


def rnd(seed):
    return np.random.default_rng(seed)


def base(rng, rgb, amp):
    a = np.zeros((CELL, CELL, 4), dtype=np.int16)
    a[..., 0], a[..., 1], a[..., 2] = rgb
    a[..., 3] = 255
    a[..., :3] += rng.integers(-amp, amp + 1, size=(CELL, CELL, 1))
    return a


def blob(a, cx, cy, r, delta, soft=2.5):
    yy, xx = np.mgrid[0:CELL, 0:CELL]
    dx = np.abs(xx - cx)
    dy = np.abs(yy - cy)
    dx = np.minimum(dx, CELL - dx)
    dy = np.minimum(dy, CELL - dy)
    d = np.sqrt(dx * dx + dy * dy)
    k = np.clip(1.0 - (d - r) / soft, 0.0, 1.0)
    a[..., :3] += (k[..., None] * delta).astype(np.int16)


def row(a, y, x0, x1, rgb):
    a[y % CELL, x0:x1, :3] = rgb


def stone(rng):
    """Castle ashlar: large cool-grey blocks, dark joints, moss in the seams."""
    a = base(rng, (104, 103, 100), 5)
    for by in range(0, CELL, 16):
        a[by:by + 2, :, :3] = (52, 52, 52)                 # mortar
        off = (by // 16) % 2 * 16
        for bx in range(0, CELL, 32):
            x = (bx + off) % CELL
            a[by:by + 15, x:x + 2, :3] = (55, 55, 55)
            w = 14 if x + 14 <= CELL else CELL - x - 2
            a[by + 2:by + 15, x + 2:x + 2 + w, :3] += int(rng.integers(-7, 8))
            a[by + 2:by + 4, x + 2:x + 2 + w, :3] += 9      # lit top edge
    for _ in range(60):                                    # pitting
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] -= int(rng.integers(8, 20))
    for _ in range(5):                                     # moss in seams
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        blob(a, x, y - (y % 16), 3, (-18, -6, -23), 1.5)
    return a


def cobble(rng):
    """Courtyard cobbles: rounded stones, warm grey, deep shaded gaps."""
    a = base(rng, (70, 68, 64), 6)
    for by in range(0, CELL, 11):
        off = (by // 11) % 2 * 5
        for bx in range(0, CELL, 12):
            x = (bx + off) % CELL
            y = by
            c = int(rng.integers(18, 34))
            for k in range(7):                              # rounded stone
                yy = y + 1 + k
                inset = 1 if 2 <= k <= 5 else 2
                a[yy % CELL, (x + inset):(x + 10 - inset), :3] = np.clip(
                    a[yy % CELL, (x + inset):(x + 10 - inset), :3] + c, 0, 255)
            a[(y + 1) % CELL, (x + 1):(x + 10), :3] += 12   # crown highlight
            a[(y + 7) % CELL, (x + 1):(x + 10), :3] -= 16   # contact shadow
    for _ in range(90):                                     # grit
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(-10, 14))
    return a


def flag(rng):
    """Bridge deck: big smooth slabs with hairline cracks and wear."""
    a = base(rng, (96, 94, 90), 4)
    for by in range(0, CELL, 32):
        a[by:by + 1, :, :3] = (58, 56, 54)
        for bx in range(0, CELL, 32):
            a[:, bx:bx + 1, :3] = (58, 56, 54)
            a[by + 1:by + 31, bx + 1:bx + 31, :3] += int(rng.integers(-6, 7))
            a[by + 1:by + 4, bx + 1:bx + 31, :3] += 8
    for _ in range(7):                                      # cracks
        x, y = int(rng.integers(2, CELL - 2)), int(rng.integers(2, CELL - 2))
        for k in range(int(rng.integers(5, 14))):
            a[(y + k) % CELL, (x + k // 4) % CELL, :3] -= 30
    for _ in range(60):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(-9, 10))
    return a


def plank(rng):
    """Dock timber: weathered boards running down the cell."""
    a = base(rng, (78, 60, 44), 5)
    for bx in range(0, CELL, 16):
        a[:, bx, :3] = (38, 29, 22)                        # gap between boards
        a[:, (bx + 1) % CELL, :3] += 10
        a[:, (bx + 2):bx + 15, :3] += int(rng.integers(-7, 8))
        for _ in range(3):                                 # grain
            x = bx + int(rng.integers(3, 14))
            y = int(rng.integers(0, CELL))
            for k in range(int(rng.integers(8, 24))):
                a[(y + k) % CELL, x, :3] -= 10
    for _ in range(6):                                     # knots
        x, y = int(rng.integers(3, CELL - 3)), int(rng.integers(3, CELL - 3))
        a[y, x, :3] -= 22
        a[(y + 1) % CELL, x - 1:x + 2, :3] -= 10
    for _ in range(5):                                     # nail heads
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] = (120, 118, 112)
    return a


def water(rng, phase=0.0):
    a = base(rng, (16, 28, 40), 3)
    yy, xx = np.mgrid[0:CELL, 0:CELL]
    wave = np.sin(yy / 6.0 + phase * 1.9 + np.sin(xx / 15.0 + phase) * 1.1)
    a[..., 0] += (wave * 3).astype(np.int16)
    a[..., 1] += (wave * 6).astype(np.int16)
    a[..., 2] += (wave * 9).astype(np.int16)
    crest = wave > 0.9
    a[crest, 0] = np.clip(a[crest, 0] + 10, 0, 255)
    a[crest, 1] = np.clip(a[crest, 1] + 14, 0, 255)
    a[crest, 2] = np.clip(a[crest, 2] + 18, 0, 255)
    for _ in range(9):                                     # moon glints
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x:x + 2, :3] = (74, 92, 112)
    return a


def slate(rng):
    """Castle roof: small overlapping grey-blue shingles."""
    a = base(rng, (54, 58, 66), 4)
    for by in range(0, CELL, 10):
        for k in (8, 9):
            a[(by + k) % CELL, :, :3] -= 18
        a[by:by + 1, :, :3] += 10
        off = (by // 10) % 2 * 6
        for bx in range(0, CELL, 12):
            x = (bx + off) % CELL
            a[by + 1:by + 8, x, :3] -= 14
            a[by + 1:by + 8, (x + 1):x + 9, :3] += int(rng.integers(-6, 8))
    return a


def lime(rng):
    """City house wall: pale lime render, uneven wash, few cracks."""
    a = base(rng, (150, 143, 128), 4)
    for _ in range(10):
        blob(a, rng.integers(0, CELL), rng.integers(0, CELL),
             rng.integers(9, 22), rng.integers(-10, 6), 4.0)
    for _ in range(700):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(-6, 7))
    for _ in range(5):
        x, y = int(rng.integers(4, CELL - 4)), int(rng.integers(2, CELL - 10))
        for k in range(int(rng.integers(5, 12))):
            a[(y + k) % CELL, (x + k // 3) % CELL, :3] -= 26
    a[..., :3] -= 4
    return a


def iron(rng):
    """Dark steel for the portcullis, armour and blades: vertical brush."""
    a = base(rng, (72, 74, 80), 4)
    for x in range(CELL):
        a[:, x, :3] += int(rng.integers(-6, 7))
    for _ in range(40):                                    # brushed streaks
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        for k in range(int(rng.integers(6, 20))):
            a[(y + k) % CELL, x, :3] += 8
    for _ in range(6):                                     # rust flecks
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        blaze = int(rng.integers(2, 4))
        a[y:y + blaze, x:x + blaze, :3] = (88, 56, 38)
    a[..., :3] += 6
    return a


def banner(rng):
    """Crimson cloth with a woven weave and a darker fold every 16 px."""
    a = base(rng, (96, 26, 30), 3)
    yy, xx = np.mgrid[0:CELL, 0:CELL]
    fold = np.sin(xx / 16.0 * np.pi) * 0.5 + 0.5
    a[..., :3] += (fold[..., None] * 22).astype(np.int16)
    a[..., :3] -= (fold[..., None] * 6).astype(np.int16)
    for y in range(0, CELL, 2):                            # weave
        a[y, :, :3] += 4
    for x in range(0, CELL, 2):
        a[:, x, :3] += 3
    for _ in range(90):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] -= int(rng.integers(4, 12))
    return a


def earth(rng):
    a = base(rng, (62, 52, 42), 6)
    for _ in range(8):
        blob(a, rng.integers(0, CELL), rng.integers(0, CELL),
             rng.integers(8, 18), rng.integers(-9, 8))
    for _ in range(60):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(8, 22))
    return a


def moss(rng):
    a = base(rng, (34, 48, 30), 5)
    for _ in range(24):
        blob(a, rng.integers(0, CELL), rng.integers(0, CELL),
             rng.integers(4, 12), rng.integers(-12, 14))
    for _ in range(140):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(-12, 16))
    return a


def gold(rng):
    a = base(rng, (128, 96, 38), 5)
    band = np.maximum(0.0, np.sin(np.mgrid[0:CELL, 0:CELL][1] / 5.0)) * 26
    a[..., :3] += band[..., None].astype(np.int16)
    for _ in range(40):                                    # hammered dents
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += 26
    for _ in range(3):                                     # tarnish
        blob(a, rng.integers(0, CELL), rng.integers(0, CELL), 8, (-30, -24, -14), 4.0)
    return a


def dark(rng):
    a = base(rng, (42, 42, 44), 5)
    for _ in range(12):
        blob(a, rng.integers(0, CELL), rng.integers(0, CELL),
             rng.integers(6, 16), rng.integers(-10, 8))
    for _ in range(50):
        x, y = int(rng.integers(0, CELL)), int(rng.integers(0, CELL))
        a[y, x, :3] += int(rng.integers(-8, 10))
    return a


TILES = [
    stone, cobble, flag, plank,
    lambda r: water(r, 0.0), lambda r: water(r, 1.2), lambda r: water(r, 2.4), lambda r: water(r, 3.6),
    slate, lime, iron, banner,
    earth, moss, gold, dark,
]


def build():
    atlas = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    for i, fn in enumerate(TILES):
        cell = fn(rnd(4000 + i * 37))
        cx, cy = (i % GRID) * CELL, (i // GRID) * CELL
        atlas[cy:cy + CELL, cx:cx + CELL] = np.clip(cell, 0, 255).astype(np.uint8)
    return atlas


def emit_c(atlas, path):
    data = atlas.tobytes()
    with open(path, 'w') as f:
        f.write('/* Generated by tools/gen_materials.py — do not edit. */\n')
        f.write('#define MAT_ATLAS_W %d\n#define MAT_ATLAS_H %d\n' % (SIZE, SIZE))
        f.write('const unsigned char materials_atlas[%d] __attribute__((aligned(128))) = {\n'
                % len(data))
        for i in range(0, len(data), 32):
            f.write('  ' + ','.join(str(b) for b in data[i:i + 32]) + ',\n')
        f.write('};\n')


def main():
    atlas = build()
    Image.fromarray(atlas, 'RGBA').save(os.path.join(ROOT, 'assets', '_materials.png'))
    emit_c(atlas, os.path.join(ROOT, 'src', 'materials_data.c'))
    print('materials ok: %dx%d' % (SIZE, SIZE))


if __name__ == '__main__':
    main()
