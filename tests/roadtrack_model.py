#!/usr/bin/env python3
"""The worn track's reference model (src/xl/proto/difficulty_world_roadtrack.c), for tests/roadtrack_test.c.

Integer hash noise and double arithmetic with + - * / floor only, so C and Python agree to the last bit.
    python3 tests/roadtrack_model.py    prints the tables the test compares against, as C
"""
import math, random, struct

M32 = 0xFFFFFFFF
HALF, FEATHER, WANDER, WANDER_WL = 1.2, 1.5, 0.5, 14.0
GRAIN, GRAIN_WL, WEAR, WEAR_WL = 0.35, 1.7, 0.12, 37.0
REACH = HALF + WANDER + FEATHER
MEANDER, MEANDER_WL = 0.5, 25.0

def h(ix, iy, seed):
    v = (ix * 374761393 + iy * 668265263 + seed * 2246822519) & M32
    v = ((v ^ (v >> 13)) * 1274126177) & M32
    return ((v ^ (v >> 16)) & 0xFFFFFF) / 16777216.0

def noise(x, y, wl, seed):
    u, v = x / wl, y / wl
    i, j = math.floor(u), math.floor(v)
    fu, fv = u - i, v - j
    fu = fu * fu * (3 - 2 * fu); fv = fv * fv * (3 - 2 * fv)
    i, j = int(i) & M32, int(j) & M32
    a = h(i, j, seed) * (1 - fu) + h((i + 1) & M32, j, seed) * fu
    b = h(i, (j + 1) & M32, seed) * (1 - fu) + h((i + 1) & M32, (j + 1) & M32, seed) * fu
    return (a * (1 - fv) + b * fv) * 2 - 1

def weight(d, x, y):
    if d >= REACH: return 0.0
    half = HALF + WANDER * (0.7 * noise(x, y, WANDER_WL, 31) + 0.3 * noise(x, y, WANDER_WL / 3.1, 32))
    s = (d - half) / FEATHER
    s = 0.0 if s < 0 else 1.0 if s > 1 else s
    w = 1.0 - s * s * (3 - 2 * s)
    w *= 1.0 - WEAR * (0.5 + 0.5 * noise(x, y, WEAR_WL, 33))
    w += GRAIN * noise(x, y, GRAIN_WL, 34) * 4.0 * w * (1.0 - w)
    return 0.0 if w < 0 else 1.0 if w > 1 else w

def warp(x, y):
    return x + MEANDER * noise(x, y, MEANDER_WL, 36), y + MEANDER * noise(x, y, MEANDER_WL, 37)

def bits(v): return struct.unpack('<Q', struct.pack('<d', v))[0]

if __name__ == '__main__':
    random.seed(20261114)
    ds = [0.0, 0.3, 0.9, 1.2, 1.5, 1.7, 1.9, 2.0, 2.2, 2.4, 2.6, 2.8, 3.0, 3.1, 3.19, 3.2]
    rows = []
    for k in range(64):
        if k < 16: x, y = round(random.uniform(-8192, 8192), 3), round(random.uniform(-8192, 8192), 3)
        elif k < 32: x, y = -round(random.uniform(0, 8192), 2), -round(random.uniform(0, 8192), 2)
        elif k < 48: x, y = round(random.uniform(-1e6, 1e6), 1), round(random.uniform(-1e6, 1e6), 1)
        else: x, y = round(random.uniform(-40, 40), 4), round(random.uniform(-40, 40), 4)
        rows.append((ds[k % 16], x, y))
    print('static const struct { double d, x, y; uint64_t weight, wx, wy; } kModel[64] = {')
    for d, x, y in rows:
        wx, wy = warp(x, y)
        print('    {%r, %r, %r, 0x%016Xull, 0x%016Xull, 0x%016Xull},' % (d, x, y, bits(weight(d, x, y)), bits(wx), bits(wy)))
    print('};')
