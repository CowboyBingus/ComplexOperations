#!/usr/bin/env python3
# Writes src/xl/co_chain_sites.h: the generator steps of HellForge's level 11 that the mod chains in front of (game
# build 25480438):
#   scripts/gen_chain_sites.py <game.dll image dump> <HellForge native/mod-loader> <the prototype's difficulty_maps.c> [out]
# HellForge's stub at each site is taken from its source by name, and so is the prototype's where it has another form.
# Where each stub is filled (the level's disk, a value, an address) is listed here from the code that fills it; the
# source must hold zeros there. The game's bytes at each site are HellForge's own pin and the dump's.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
dump = open(sys.argv[1], 'rb').read()
stock_dir, fork_maps = sys.argv[2], sys.argv[3]
out = sys.argv[4] if len(sys.argv) > 4 else HERE + '/../src/xl/co_chain_sites.h'

MACROS = {'DIF_VANILLA': 10, 'DIF_UNREACHABLE_TRIES': 6}


def array(path, name):
    """The bytes of `static const uint8_t name[n] = {...};` in a C source."""
    text = open(path).read()
    found = re.search(r'static const uint8_t %s\[(\d+)\] = \{(.*?)\};' % re.escape(name), text, re.S)
    assert found, (path, name)
    body = re.sub(r'/\*.*?\*/', '', found.group(2), flags=re.S)
    values = []
    for item in body.split(','):
        item = item.strip()
        if not item: continue
        for macro, value in MACROS.items(): item = item.replace(macro, str(value))
        assert re.fullmatch(r'[0-9A-Fa-fx+ ]+', item), (name, item)
        values.append(eval(item))
    assert len(values) == int(found.group(1)) and all(0 <= v < 256 for v in values), name
    return bytes(values)


def stock(file, name): return array(os.path.join(stock_dir, file), name)


# Fill kinds: K the level's disk key (imm32); G a rel32 to game + arg; C a rel32 to a value of the stub's owner
# (arg: which of the mod's); F a float of the owner's in the stub (arg: which of the mod's); X an address of the owner's.
CELLS = ['SPACING', 'GRID', 'FACTOR', 'QUARTER', 'SQUARE', 'VISTA']
GO_ON = 0xF67FB7
# The mod's own FixUnreachableAreas step: the prototype's (and HellForge's) rule without the loader's counters.
OWN_UNREACHABLE = bytes([
    0x81, 0xB9, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,   # 0 cmp dword [rcx+0x2d7c], key (at 6)
    0x75, 0x0E,                                       # 10 jne 26
    0x83, 0xB9, 0x54, 0xF9, 0x00, 0x00, 0x06,         # 12 cmp dword [rcx+0xf954], 6
    0x72, 0x05,                                       # 19 jb 26
    0xE9, 0, 0, 0, 0,                                 # 21 jmp 0xF67FB7 (at 22)
    0xFF, 0x81, 0x54, 0xF9, 0x00, 0x00,               # 26 inc dword [rcx+0xf954]
    0xE9, 0, 0, 0, 0])                                # 32 jmp back (at 33)

# name, site, length, HellForge's pin, HellForge's stub, its fills, the mod's stub (None: the same form), its fills
CHAINS = [
    ('the route sites\' spacing', 0xF3614D, 5, stock('difficulty_maps.c', 'kSpacingPin'), stock('difficulty_maps.c', 'kSpacingStub'),
     [(15, 'K', 0), (25, 'C', 'SPACING'), (30, 'G', 0xF3614D + 5)], None, None),
    ('a cell\'s distance less the spacing', 0xF3732C, 5, stock('difficulty_maps.c', 'kSpacingPin2'), stock('difficulty_maps.c', 'kSpacingStub2'),
     [(16, 'K', 0), (27, 'C', 'SPACING'), (32, 'G', 0xF3732C + 5), (42, 'G', 0xF3732C + 5)], None, None),
    ('GenerateVoronoi\'s grid', 0xF3FB04, 9, stock('difficulty_maps.c', 'kGridPin'), stock('difficulty_maps.c', 'kGridStub'),
     [(19, 'K', 0), (29, 'C', 'GRID'), (34, 'G', 0xF3FB04 + 9)], None, None),
    ('the route cells\' spacing', 0xF9420F, 7, stock('difficulty_maps.c', 'kCellSpacingPin'), stock('difficulty_maps.c', 'kCellSpacingStub'),
     [(15, 'K', 0), (26, 'C', 'FACTOR'), (40, 'C', 'QUARTER'), (48, 'G', 0xF9420F + 7)],
     array(fork_maps, 'kCellSpacingStub'), [(15, 'K', 0), (43, 'C', 'FACTOR'), (53, 'C', 'SQUARE'), (67, 'C', 'QUARTER'), (75, 'G', 0xF9420F + 7)]),
    ('FinalizeVoronoi\'s spacing', 0xF68273, 9, stock('difficulty_maps.c', 'kFinalizeSpacingPin'), stock('difficulty_maps.c', 'kFinalizeSpacingStub'),
     [(24, 'K', 0), (34, 'C', 'SQUARE'), (48, 'C', 'QUARTER'), (53, 'G', 0xF68273 + 9)], None, None),
    ('revoronoi\'s cell size', 0xFD0EF1, 8, stock('difficulty_cities.c', 'kReVoronoiSizePin'), stock('difficulty_cities.c', 'kReVoronoiSizeStub'),
     [(15, 'K', 0), (25, 'C', 'FACTOR'), (39, 'C', 'QUARTER'), (44, 'G', 0xFD0EF1 + 8)], None, None),
    ('revoronoi\'s gap', 0xFD11A5, 11, stock('difficulty_cities.c', 'kReVoronoiGapPin'), stock('difficulty_cities.c', 'kReVoronoiGapStub'),
     [(15, 'K', 0), (25, 'C', 'FACTOR'), (39, 'C', 'QUARTER'), (44, 'G', 0xFD11A5 + 11)], None, None),
    ('the structured grid\'s spacing', 0xF361B4, 9, stock('difficulty_cities.c', 'kStructuredGridPin'), stock('difficulty_cities.c', 'kStructuredGridStub'),
     [(16, 'K', 0), (26, 'C', 'FACTOR'), (40, 'C', 'QUARTER'), (45, 'G', 0xF361B4 + 9)], None, None),
    ('the playable area\'s cull', 0xF25B13, 8, stock('difficulty_cities.c', 'kCullRadiusPin'), stock('difficulty_cities.c', 'kCullRadiusStub'),
     [(6, 'K', 0), (21, 'G', 0xF25B13 + 8), (34, 'G', 0xF25B13 + 8)], None, None),
    ('the vista level\'s pose', 0xC15A79, 7, stock('difficulty_cities.c', 'kVistaPosePin'), stock('difficulty_cities.c', 'kVistaPoseStub'),
     [(18, 'K', 0), (65, 'G', 0xC15A79 + 7), (80, 'F', 'VISTA'), (84, 'F', 'VISTA')], None, None),
    ('FixUnreachableAreas\' restart', 0xF67FDB, 6, stock('difficulty_diag.c', 'kRestartPin'), stock('difficulty_diag.c', 'kUnreachableStub'),
     [(3, 'X', 0), (10, 'X', 0), (20, 'K', 0), (38, 'X', 0), (43, 'G', GO_ON), (54, 'G', 0xF67FDB + 6)],
     OWN_UNREACHABLE, [(6, 'K', 0), (22, 'G', GO_ON), (33, 'G', 0xF67FDB + 6)]),
]

KIND = {'K': 'CO_FILL_KEY', 'G': 'CO_FILL_GAME', 'C': 'CO_FILL_CELL', 'F': 'CO_FILL_FLOAT', 'X': 'CO_FILL_OWNER'}


def check(name, stub, fills):
    spans = []
    for at, kind, arg in fills:
        assert stub[at:at + 4] == b'\0\0\0\0', (name, at)
        assert all(at + 4 <= a or b <= at for a, b in spans), (name, at)
        spans.append((at, at + 4))
        if kind == 'G': assert stub[at - 1] in (0xE9, 0xE8) or stub[at - 2:at] in (b'\x0F\x85', b'\x0F\x84'), (name, at)


def hexes(b): return ', '.join('0x%02X' % x for x in b)


def fills_c(fills):
    rows = []
    for at, kind, arg in fills:
        rows.append('{%d, %s, %s}' % (at, KIND[kind], ('CO_CELL_' + arg) if kind in 'CF' else '0x%Xu' % arg))
    return ', '.join(rows)


h = ['/* Generated by scripts/gen_chain_sites.py from HellForge\'s and the prototype\'s sources; do not edit. Game build 25480438. */\n',
     '#ifndef CO_CHAIN_SITES_H\n#define CO_CHAIN_SITES_H\n\n']
rows = []
for i, (name, site, length, pin, stub, fills, own, own_fills) in enumerate(CHAINS):
    assert len(pin) >= length and dump[site:site + len(pin)] == pin, name
    check(name, stub, fills)
    if own is None: own, own_fills = stub, fills
    else: check(name, own, own_fills)
    assert len(own) <= 96 and len(stub) <= 96, name
    h.append('static const uint8_t kChainGame%d[] = {%s};\n' % (i, hexes(pin[:length])))
    h.append('static const uint8_t kChainStock%d[] = {%s};\n' % (i, hexes(stub)))
    h.append('static const CoFill kChainStockFills%d[] = {%s};\n' % (i, fills_c(fills)))
    if own is not stub:
        h.append('static const uint8_t kChainOwn%d[] = {%s};\n' % (i, hexes(own)))
        h.append('static const CoFill kChainOwnFills%d[] = {%s};\n' % (i, fills_c(own_fills)))
    o = 'Own' if own is not stub else 'Stock'
    rows.append('    {"%s", 0x%Xu, %d, kChainGame%d, kChainStock%d, %d, kChainStockFills%d, %d, kChain%s%d, %d, kChain%sFills%d, %d},\n'
                % (name, site, length, i, i, len(stub), i, len(fills), o, i, len(own), o, i, len(own_fills)))
h.append('\n/* {what, site, length, the game\'s bytes, HellForge\'s stub, its size, its fills, the mod\'s stub, its size, its fills} */\n')
h.append('static const CoChain kCoChains[CO_CHAINS] = {\n')
h += rows
h.append('};\n\n#endif\n')
assert len(CHAINS) == 11
open(out, 'w').writelines(h)
print('%d chained sites' % len(CHAINS))
