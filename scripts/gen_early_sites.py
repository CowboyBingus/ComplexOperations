#!/usr/bin/env python3
# Writes src/co_early_sites.h: the engine's pool sizes, broadphase and navigation grid as the game sets them up once,
# with the bytes HellForge leaves at the pool sizes (game build 25480438):
#   scripts/gen_early_sites.py <game.dll image dump> <helldivers2.exe image dump> <HellForge native/mod-loader> [out]
# HellForge's values are its kPhysicsPools table, read from its source; the game's bytes are checked against the dumps.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
game = open(sys.argv[1], 'rb').read()
exe = open(sys.argv[2], 'rb').read()
stock = sys.argv[3]
out = sys.argv[4] if len(sys.argv) > 4 else HERE + '/../src/co_early_sites.h'

maps = open(os.path.join(stock, 'difficulty_maps.c')).read()
table = re.search(r'kPhysicsPools\[9\] = \{(.*?)\};', maps, re.S)
assert table, 'kPhysicsPools'
rows = re.findall(r'\{(0x[0-9A-F]+)u, (\d+), (\d+), \{([^}]*)\}, (0x[0-9A-F]+)u\}', table.group(1))
assert len(rows) == 9, len(rows)
setup = int(re.search(r'#define DIF_PHYSICS_SETUP (0x[0-9A-Fa-f]+)u', maps).group(1), 16)
assert setup == 0x3326380
# the setup's first call stores its result there: mov [rip -> setup], rax at 0x4EDC62
assert game[0x4EDC62:0x4EDC65] == bytes([0x48, 0x89, 0x05]) and 0x4EDC69 + struct.unpack('<i', game[0x4EDC65:0x4EDC69])[0] == setup

# What an extra large world needs in place of HellForge's value; the tenth is a size HellForge leaves the game's.
# Navmesh cutters (config +0x4C): every forest tree's cutter holds one of the world's bodies (0x8153FB), so with the
# bodies' room no tree is left without one; 2^20 keeps one generation bit in the handle (allocator 0x75230).
OWN = {0xAB19EE: (0x100000, 'the Havok world\'s bodies'), 0x4EE324: (0x100000, 'static actors'),
       0x4EE371: (0x100000, 'navmesh cutters')}
CONNECTORS = (0x4EE340, 7, 3, bytes([0xC7, 0x45, 0x88, 0x00, 0x00, 0x04, 0x00]), 0x400000)
assert game[CONNECTORS[0]:CONNECTORS[0] + 7] == CONNECTORS[3]


# The cutter room's way into the engine (exe): the config's +0x44..+0x54 copied to 0x27C31D8 (0x78A2E2), so +0x4C is
# 0x27C31E0; each world's pool takes it as its room (0x7D7F4D -> [pool+0x24] at 0x7D7F60). The one block for the four
# worlds' pools is sized 64 B a slot (16 B an entry x 4, shl esi,6 at 0x78A2C1) and cleared at once (0x78A2DD).
assert exe[0x78A2B6:0x78A2BA] == bytes([0x41, 0x03, 0x77, 0x4C]) and exe[0x78A2C1:0x78A2C4] == bytes([0xC1, 0xE6, 0x06])
assert exe[0x78A2E2:0x78A2EB] == bytes([0x41, 0x0F, 0x10, 0x47, 0x44, 0x0F, 0x11, 0x05, 0xEA]) and 0x78A2EE + struct.unpack('<i', exe[0x78A2EA:0x78A2EE])[0] == 0x27C31D8
assert 0x7D7F53 + struct.unpack('<i', exe[0x7D7F4F:0x7D7F53])[0] == 0x27C31E0 and exe[0x7D7F60:0x7D7F64] == bytes([0x41, 0x89, 0x48, 0x24])
# A handle is index | live bit (the room's next power of two) | generation | kind and world (bits 22..31, 0x7D7F7B):
# a room up to 2^20 keeps at least one generation bit, so a stale handle is not taken for its slot's next one.
assert exe[0x7D7F7B:0x7D7F82] == bytes([0x41, 0xC7, 0x40, 0x30, 0x00, 0x00, 0xC0])
cutters = OWN[0x4EE371][0]
assert cutters & (cutters - 1) == 0 and 0x10000 <= cutters <= 1 << 20
# a forest tree's cutter is made with the tree's body (0x8153FB, description +0x20 at 0x8155EC): no more trees than bodies
assert exe[0x8153FB:0x8153FF] == bytes([0x45, 0x8B, 0x34, 0x86]) and exe[0x8155EC:0x8155F0] == bytes([0x44, 0x89, 0x75, 0xE0])
assert cutters >= OWN[0xAB19EE][0]

# The SpeedTree forest's instance grid (exe): every tree a level's layers spawn takes one slot (insert 0x7AD5E0; slot 0
# is never handed out), and a full grid crashes the spawn (0x20CF57). Each forest is made with its world (0x1A6B74 ->
# 0x20AA90), which reads the room twice: for the block's size (0x7ACE50, mov r9d at 0x20ADA2) and for its layout
# (0x7AD1D0, [rsp+0x30] at 0x20AE1E). Both go up together; the size site is written first and put back last.
FOREST = 0x80000
forest_sites = ((0x20ADA2, 6, 2), (0x20AE1E, 8, 4))
assert exe[0x20ADA2:0x20ADA8] == bytes([0x41, 0xB9, 0x00, 0x80, 0x01, 0x00])
assert exe[0x20AE1E:0x20AE26] == bytes([0xC7, 0x44, 0x24, 0x30, 0x00, 0x80, 0x01, 0x00])
def call_to(at): return exe[at] == 0xE8 and at + 5 + struct.unpack('<i', exe[at + 1:at + 5])[0]
forest_calls = (0x20ADC5, 0x20AE39)
assert call_to(0x20ADC5) == 0x7ACE50 and call_to(0x20AE39) == 0x7AD1D0
def rip_float(at): return struct.unpack('<f', exe[at + 8 + struct.unpack('<i', exe[at + 4:at + 8])[0]:][:4])[0]
# The size: words = ceil(room/64) (0x7ACEF5, 0x7ACF11), 8 B a word for each row and column of 2048 m / 8 m (0x7ACF4C,
# 0x7ACF66), 4 B a slot (0x7ACF57), + 7. The layout: the same words (0x7AD35F, 0x7AD363 -> [grid+0x28]) over the rows
# and columns of -1024..1024 m / 16 m, 4 B a slot; the room is [grid+0x1C] (0x7AD36D).
assert exe[0x7ACEF5:0x7ACEF9] == bytes([0x49, 0x8D, 0x77, 0x3F]) and exe[0x7ACF11:0x7ACF15] == bytes([0x48, 0xC1, 0xEE, 0x06])
assert exe[0x7ACF4C:0x7ACF4F] == bytes([0x0F, 0xAF, 0xC6]) and exe[0x7ACF57:0x7ACF5B] == bytes([0x44, 0x0F, 0xAF, 0xFF])
assert exe[0x7ACF66:0x7ACF6D] == bytes([0x8D, 0x0C, 0xC5, 0, 0, 0, 0])
assert exe[0x7AD35F:0x7AD367] == bytes([0x49, 0x8D, 0x45, 0x3F, 0x48, 0xC1, 0xE8, 0x06]) and exe[0x7AD36D:0x7AD371] == bytes([0x44, 0x89, 0x6F, 0x1C])
assert rip_float(0x20AD9A) == 2048.0 and rip_float(0x20ADA8) == 8.0
assert rip_float(0x20ADE2) == 16.0 and rip_float(0x20ADF1) == -1024.0 and rip_float(0x20ADF9) == 1024.0
size_cells, grid_cells = int(2048.0 / 8.0), int(2048.0 / 16.0)
def block_bytes(room): return 8 * ((room + 63) // 64) * 2 * size_cells + 4 * room + 7
def laid_bytes(room): return 4 * room + 8 * ((room + 63) // 64) * 2 * grid_cells
# The key the insert hands back (0x7AD778..0x7AD792): the cell box, four fields of the cells' bits, above the slot.
assert exe[0x7AD778:0x7AD780] == bytes([0x42, 0x8D, 0x14, 0xBD, 0, 0, 0, 0]) and exe[0x7AD780:0x7AD783] == bytes([0x44, 0x2B, 0xCA])
assert exe[0x7AD3B4:0x7AD3B7] == bytes([0x8D, 0x14, 0x88]) and exe[0x7AD3BC:0x7AD3BF] == bytes([0x83, 0xFA, 0x20])
assert FOREST & (FOREST - 1) == 0 and 0x18000 < FOREST <= 1 << 20
assert (FOREST - 1).bit_length() + 4 * (grid_cells - 1).bit_length() <= 64
assert laid_bytes(FOREST) <= block_bytes(FOREST) < 1 << 31


def hexes(b): return ', '.join('0x%02X' % x for x in b)


pools = []
for rva, length, at, pin, value in rows:
    rva, length, at, value = int(rva, 16), int(length), int(at), int(value, 16)
    pin = bytes(int(i, 0) for i in pin.split(','))
    assert len(pin) == length and at + 4 == length and game[rva:rva + length] == pin, hex(rva)
    own = OWN.get(rva, (value, None))[0]
    assert own >= value
    pools.append((rva, length, at, pin[:at] + struct.pack('<I', value), own))
assert sum(1 for p in pools if p[0] in OWN) == len(OWN)
pools.append((CONNECTORS[0], CONNECTORS[1], CONNECTORS[2], CONNECTORS[3], CONNECTORS[4]))

# the broadphase: one float, one reader
load = 0xAB1975
assert game[load:load + 4] == bytes([0xF3, 0x0F, 0x10, 0x05])
size_at = load + 8 + struct.unpack('<i', game[load + 4:load + 8])[0]
assert size_at == 0x23C7DF0 and struct.unpack('<f', game[size_at:size_at + 4])[0] == 3072.0
# the navigation grid: four extents of 1024.0 and the cell size's read (128.0)
extents = [0x4EE3BE, 0x4EE3C5, 0x4EE101, 0x4EE108]
for at in extents: assert game[at] == 0xC7 and game[at + 1] == 0x45 and struct.unpack('<f', game[at + 3:at + 7])[0] == 1024.0, hex(at)
cell = 0xAB1AD6
assert game[cell:cell + 4] == bytes([0xF3, 0x0F, 0x10, 0x05])
cell_at = cell + 8 + struct.unpack('<i', game[cell + 4:cell + 8])[0]
assert struct.unpack('<f', game[cell_at:cell_at + 4])[0] == 128.0
tile = 0x89A85E
assert exe[tile:tile + 4] == bytes([0xF3, 0x0F, 0x10, 0x05])

lines = ['/* Generated by scripts/gen_early_sites.py from HellForge\'s source and the game\'s images; do not edit. Game build 25480438. */\n',
         '#ifndef CO_EARLY_SITES_H\n#define CO_EARLY_SITES_H\n\n',
         '#define CO_EARLY_SETUP 0x%Xu      /* 0 until the game\'s plugin setup begins (stored at 0x4EDC62) */\n' % setup,
         '#define CO_EARLY_BROADPHASE_LOAD 0x%Xu\n' % load,
         '#define CO_EARLY_BROADPHASE 0x%Xu /* 3072.0 */\n' % size_at,
         '#define CO_EARLY_CELL_READ 0x%Xu  /* movss xmm0, [rip -> 128.0] */\n' % cell,
         '#define CO_EARLY_TILE_READ 0x%Xu  /* exe: the terrain build\'s tile size */\n' % tile,
         '/* The pool sizes: the bytes HellForge leaves (the tenth: the game\'s) and the size an extra large world needs. */\n',
         'typedef struct CoEarlyPool { uint32_t rva; uint8_t length, at; uint8_t stock[10]; uint32_t own; } CoEarlyPool;\n',
         'static const CoEarlyPool kCoEarlyPools[%d] = {\n' % len(pools)]
for rva, length, at, pin, own in pools: lines.append('    {0x%Xu, %d, %d, {%s}, 0x%Xu},\n' % (rva, length, at, hexes(pin), own))
lines += ['};\n',
          'static const uint8_t kCoEarlyBroadphaseLoad[8] = {%s};\n' % hexes(game[load:load + 8]),
          'static const struct { uint32_t rva; uint8_t bytes[7]; } kCoEarlyExtents[4] = {\n']
for at in extents: lines.append('    {0x%Xu, {%s}},\n' % (at, hexes(game[at:at + 7])))
lines += ['};\n',
          'static const uint8_t kCoEarlyCellRead[8] = {%s};\n' % hexes(game[cell:cell + 8]),
          'static const uint8_t kCoEarlyTileRead[8] = {%s};\n' % hexes(exe[tile:tile + 8]),
          '/* exe: the SpeedTree forest grid\'s room at its size call and its layout (the game\'s bytes), then those calls. */\n',
          '#define CO_EARLY_FOREST_ROOM 0x%Xu /* the game\'s: 0x18000 */\n' % FOREST,
          'static const struct { uint32_t rva; uint8_t length, at; uint8_t stock[8]; } kCoEarlyForest[2] = {\n']
for rva, length, at in forest_sites: lines.append('    {0x%Xu, %d, %d, {%s}},\n' % (rva, length, at, hexes(exe[rva:rva + length])))
lines += ['};\n',
          'static const struct { uint32_t rva; uint8_t bytes[5]; } kCoEarlyForestCalls[2] = {\n']
for at in forest_calls: lines.append('    {0x%Xu, {%s}},\n' % (at, hexes(exe[at:at + 5])))
lines += ['};\n',
          '\n#endif\n']
open(out, 'w').writelines(lines)
print('early sites written')
