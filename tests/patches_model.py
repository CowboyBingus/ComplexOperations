#!/usr/bin/env python3
"""The patch outlines' breakup: reference model (src/xl/proto/difficulty_world_patches.c), for tests/patches_test.c.

Integer hash noise and double arithmetic with + - * / floor only, so C and Python agree to the last bit.
    python3 tests/patches_model.py    prints the table the test compares against, as C
"""
import math, random, struct

M32 = 0xFFFFFFFF
TEXEL = 2304.0 / 4096.0
WARP, WARP_WL, WARP_FINE, WARP_FINE_WL = 1.5, 9.0, 0.35, 3.0
SHARE, SHARE_WL, KEEP, RAMP = 0.3, 1.7, 0.95, 0.20
X0, Y0 = 2.0 - 1152.0 + TEXEL / 2, 2.0 + 1152.0 - TEXEL / 2

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

def clamp(v): return 0.0 if v < 0 else 1.0 if v > 1 else v

def synth(I, J, x0=X0, y0=Y0):
    """The test's painted ground: native texel (I, J) of a tile whose texel (0, 0) is at (x0, y0); slots 3, 9, 12."""
    x, y = x0 + (2 * J + 0.5) * TEXEL, y0 - (2 * I + 0.5) * TEXEL
    w9 = clamp(0.5 + 1.2 * noise(x, y, 40.0, 5))
    w12 = (1 - w9) * clamp(0.5 + 1.2 * noise(x, y, 15.0, 6))
    vb, vc = math.floor(w9 * 255 + 0.5), math.floor(w12 * 255 + 0.5)
    if vb + vc > 255: vc = 255 - vb
    parts = [(s, v) for s, v in ((3, 255 - vb - vc), (9, vb), (12, vc)) if v > 0]
    ids = [p[0] for p in parts] + [0, 0]; vs = [p[1] for p in parts] + [0, 0]
    return ids[0] << 26 | ids[1] << 21 | ids[2] << 16 | vs[1] << 8 | vs[2]

def mix(x, y):
    fc, fr = (x - X0) / TEXEL, (Y0 - y) / TEXEL
    tx, ty = (fc - 0.5) / 2, (fr - 0.5) / 2
    tx = 0 if tx < 0 else 2047 if tx > 2047 else tx; ty = 0 if ty < 0 else 2047 if ty > 2047 else ty
    J0, I0 = min(math.floor(tx), 2046), min(math.floor(ty), 2046)
    fx, fy = tx - J0, ty - I0
    ids, ws = [], []
    for k in range(4):
        dI, dJ = k >> 1, k & 1
        beta = (fx if dJ else 1 - fx) * (fy if dI else 1 - fy)
        v = synth(I0 + dI, J0 + dJ)
        wb, wc = ((v >> 8) & 255) / 255.0, (v & 255) / 255.0
        for s, w in (((v >> 26) & 31, 1 - wb - wc), ((v >> 21) & 31, wb), ((v >> 16) & 31, wc)):
            if s == 0: continue
            if s not in ids: ids.append(s); ws.append(0.0)
            ws[ids.index(s)] += beta * w
    return ids, ws

def field(x, y, strength):
    dx = WARP * strength * ((1 - WARP_FINE) * noise(x, y, WARP_WL, 41) + WARP_FINE * noise(x, y, WARP_FINE_WL, 42))
    dy = WARP * strength * ((1 - WARP_FINE) * noise(x, y, WARP_WL, 43) + WARP_FINE * noise(x, y, WARP_FINE_WL, 44))
    return dx, dy

def breakup(x, y, strength):
    ids, ws = mix(x, y)
    top = max(ws) if ws else 0.0
    if not strength > 0 or top >= KEEP: return ids, ws
    g = (KEEP - top) / RAMP
    g = 1.0 if g > 1 else g
    dx, dy = field(x, y, strength)
    ids, ws = mix(x + g * dx, y + g * dy)
    out = []
    for s, w in zip(ids, ws):
        v = w + g * (SHARE * strength) * noise(x, y, SHARE_WL, 64 + s) * (4.0 * w * (1.0 - w))
        out.append(0.0 if v < 0 else v)
    total = 0.0
    for v in out: total += v
    if total > 1e-9: out = [v / total for v in out]
    return ids, out

def pack(ids, ws):
    ids, ws = list(ids), list(ws)
    tid, tw, total = [0, 0, 0], [0.0, 0.0, 0.0], 0.0
    for r in range(3):
        best = -1
        for q in range(len(ids)):
            if ids[q] != 0 and (best < 0 or ws[q] * 64 - ids[q] * 1e-6 > ws[best] * 64 - ids[best] * 1e-6): best = q
        if best < 0: break
        if ws[best] > 0.5 / 255: tid[r], tw[r] = ids[best], ws[best]; total += ws[best]
        ids[best] = 0
    if total > 1e-9: tw = [v / total for v in tw]
    for a in range(2):
        for b in range(2 - a):
            if (tid[b] or 99) > (tid[b + 1] or 99): tid[b], tid[b + 1] = tid[b + 1], tid[b]; tw[b], tw[b + 1] = tw[b + 1], tw[b]
    vb, vc = math.floor(tw[1] * 255 + 0.5), math.floor(tw[2] * 255 + 0.5)
    if vb + vc > 255: vc = 255 - vb
    return tid[0] << 26 | tid[1] << 21 | tid[2] << 16 | vb << 8 | vc

def bits(v): return struct.unpack('<Q', struct.pack('<d', v))[0]

if __name__ == '__main__':
    random.seed(20261114)
    print('static const struct { double x, y; uint64_t dx, dy; int r, c; uint32_t plain, whole, half; } kModel[48] = {')
    for k in range(48):
        if k < 16: x, y = round(random.uniform(-8192, 8192), 3), round(random.uniform(-8192, 8192), 3)
        elif k < 32: x, y = round(random.uniform(-1e6, 1e6), 1), round(random.uniform(-1e6, 1e6), 1)
        else: x, y = round(random.uniform(-40, 40), 4), round(random.uniform(-40, 40), 4)
        r, c = random.randrange(600, 3500), random.randrange(600, 3500)
        tx, ty = X0 + TEXEL * c, Y0 - TEXEL * r
        dx, dy = field(x, y, 1.0)
        print('    {%r, %r, 0x%016Xull, 0x%016Xull, %d, %d, 0x%08Xu, 0x%08Xu, 0x%08Xu},' % (x, y, bits(dx), bits(dy), r, c,
            pack(*breakup(tx, ty, 0.0)), pack(*breakup(tx, ty, 1.0)), pack(*breakup(tx, ty, 0.5))))
    print('};')
