#!/usr/bin/env python3
# Writes src/xl/co_stamp_sites.h: the generator's stamp index as HellForge's level 11 leaves it, and the instructions
# the mod takes over so that the index is laid out per level (game build 25480438):
#   scripts/gen_stamp_sites.py <game.dll image dump> <HellForge native/mod-loader> [out]
# HellForge's five writes are read from its source; the game's bytes and what the mod relies on are checked on the dump.
import os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
dump = sys.argv[1]
game = open(dump, 'rb').read()
maps = open(os.path.join(sys.argv[2], 'difficulty_maps.c')).read()
out = sys.argv[3] if len(sys.argv) > 3 else HERE + '/../src/xl/co_stamp_sites.h'


def hexes(b): return ', '.join('0x%02X' % x for x in b)


def u32(b, at): return struct.unpack_from('<I', b, at)[0]


body = re.search(r'kStampIndex\[5\] = \{(.*?)\};', maps, re.S).group(1)
rows = re.findall(r'\{(0x[0-9A-F]+)u, (\d), \{([^}]*)\}, \{([^}]*)\}\}', body)
assert len(rows) == 5
stock = {}
for rva, length, pin, new in rows:
    rva, length = int(rva, 16), int(length)
    pin = bytes(int(x, 0) for x in pin.split(','))[:length]
    new = bytes(int(x, 0) for x in new.split(','))[:length]
    assert game[rva:rva + length] == pin, hex(rva)
    stock[rva] = new
assert sorted(stock) == [0xC0498B, 0xC04AEA, 0xC04C1A, 0xC04CB2, 0xC04CCC]
# HellForge writes them as they are, for a larger map (its JumpToMapStubs)
assert 'for (int i = 0; i < 5; i++)\n        if (ApiWrite(game + kStampIndex[i].rva, kStampIndex[i].bytes, kStampIndex[i].length))' in maps

# One count in all five: slots; (slots + 1) x 4 where the cells begin; slots x 4 + 0xB after the cells in the size.
slots = u32(stock[0xC04C1A], 2)
assert stock[0xC04C1A][:2] == b'\x41\xBD' and stock[0xC04AEA] == b'\xB8' + struct.pack('<I', slots)
assert stock[0xC04CB2][:3] == b'\x48\x8D\x81' and u32(stock[0xC04CB2], 3) == (slots + 1) * 4
assert stock[0xC04CCC][:3] == b'\xC7\x47\x1C' and u32(stock[0xC04CCC], 3) == slots + 1
assert stock[0xC0498B][:1] == b'\x05' and u32(stock[0xC0498B], 1) == slots * 4 + 0xB
assert game[0xC04C1A:0xC04C20] == b'\x41\xBD\x05\x2A\x00\x00' and u32(game, 0xC0498C) == 0x2A05 * 4 + 0xB

# The size (0xC048E0): cells x bytes a cell + the add. The game's 0x548 bytes are 0xA9 qwords, the cell's bitset.
cell_site, cell_pin = 0xC0497D, bytes([0x69, 0xC3, 0x48, 0x05, 0x00, 0x00])
assert game[cell_site:cell_site + 6] == cell_pin and game[0xC04CDA:0xC04CE1] == b'\xC7\x47\x28\xA9\x00\x00\x00' and 0xA9 * 8 == 0x548

# The constructor's layout part, 0xC04CB2..0xC04CE1, as HellForge leaves it: one stub replaces all of it.
layout = bytearray(game[0xC04CB2:0xC04CE1])
layout[0:7] = stock[0xC04CB2]
layout[0xC04CCC - 0xC04CB2:0xC04CCC - 0xC04CB2 + 7] = stock[0xC04CCC]
assert bytes(layout[7:26]) == bytes.fromhex('48894738' '428D0482' 'F3440F114708' 'BA01000000') and bytes(layout[33:40]) == bytes.fromhex('C7472404000000')

# No branch of the constructor (0xC04B80..0xC04EE9) lands inside what the two jumps replace.
dis = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel', '--start-address=0xc04b80',
                      '--stop-address=0xc04ee9', dump], capture_output=True, text=True, check=True).stdout
targets = [int(t, 16) for t in re.findall(r'\tj[a-z]+ +0x([0-9a-f]+)', dis)]
assert targets and not [t for t in targets if 0xC04C1A < t < 0xC04C20 or 0xC04CB2 < t < 0xC04CE1], targets
# r13d is the slot count from 0xC04C1A to the free list's loop (cmp r9d, r13d at 0xC04D0A): nothing writes it between.
between = dis[dis.index('c04c20:'):dis.index('c04d0a:')]
assert not re.search(r'\t(mov|lea|xor|add|sub|pop|movzx|cmov\w+) +r13', between)
# Every index has 64 + 64 cells: the generator's (2048 m in cells of 32 m, 0xF6E96B / 0xF6E973) and each rebuild
# (the world x 1/64: 0xF70AB3, 0xFCAC1E, 0xFCACD3); the four callers of the size are these.
assert struct.unpack_from('<f', game, 0x23C7DD4)[0] == 2048.0 and struct.unpack_from('<f', game, 0x23C766C)[0] == 32.0
assert struct.unpack_from('<f', game, 0x23C6648)[0] == 1.0 / 64.0
callers = [0xF6E9E4, 0xF70ABB, 0xFCAC58, 0xFCACDB]
for a in callers: assert game[a] == 0xE8 and a + 5 + struct.unpack_from('<i', game, a + 1)[0] == 0xC048E0, hex(a)
at, n = game.find(b'\xE8', 0x1000), 0
while at != -1 and at < 0x20A0000:
    if at + 5 + struct.unpack_from('<i', game, at + 1)[0] == 0xC048E0: n += 1
    at = game.find(b'\xE8', at + 1)
assert n == 4, n
# The layout check's answer (0xC04AB0, HellForge's slot count at 0xC04AEA) is read by none of its four callers.
for a, after in ((0xC04909, 'F3410F5EF00F28C6'), (0xC04C49, '413BDE6644897702'), (0xF6E9DB, '0F28D60F28CF0F28'), (0xFCAC4F, '0F28D60F28CF0F28')):
    assert game[a - 5] == 0xE8 and a + struct.unpack_from('<i', game, a - 4)[0] == 0xC04AB0 and game[a:a + 8] == bytes.fromhex(after), hex(a)
query = game[0xC05360:0xC05370]
assert query[:9] == bytes.fromhex('4C894C242048894C24')

CELLS, OWN_SLOTS, OWN_QWORDS, OWN_CELL = 128, 131071, 0x800, 0x5000
assert OWN_QWORDS * 64 > OWN_SLOTS and (OWN_SLOTS + 1) * 4 + CELLS * OWN_QWORDS * 8 <= CELLS * OWN_CELL
assert (slots + 1) * 4 + CELLS * 0xA9 * 8 <= CELLS * OWN_CELL

o = ['/* Generated by scripts/gen_stamp_sites.py from HellForge\'s source and the game\'s image; do not edit. Game build 25480438. */\n',
     '#ifndef CO_STAMP_SITES_H\n#define CO_STAMP_SITES_H\n\n#include <stdint.h>\n\n',
     '/* The index\'s size, 0xC048E0: cells x bytes a cell + an add. imul eax, ebx, 0x548 (the game\'s: 0xA9 qwords). */\n',
     '#define CO_STAMP_CELL_SITE 0x%Xu\nstatic const uint8_t kCoStampCellGame[6] = {%s};\n' % (cell_site, hexes(cell_pin)),
     '#define CO_STAMP_CELLS %du        /* 64 + 64: every index the game or the mod builds */\n' % CELLS,
     '#define CO_STAMP_CELL_BYTES 0x%Xu  /* x 128 cells holds 131071 slots and their cells\' bitsets, whatever the add */\n\n' % OWN_CELL,
     '/* HellForge\'s level 11 and the game\'s own cell: slots, qwords a cell. */\n',
     '#define CO_STAMP_STOCK_SLOTS %du\n#define CO_STAMP_STOCK_QWORDS 0xA9u\n#define CO_STAMP_OWN_QWORDS 0x%Xu\n\n' % (slots, OWN_QWORDS),
     '/* HellForge\'s two writes the mod leaves: the size\'s add and the layout check\'s count (its answer is read by no caller). */\n',
     '#define CO_STAMP_ADD_SITE 0xC0498Bu\nstatic const uint8_t kCoStampAddStock[5] = {%s};\n' % hexes(stock[0xC0498B]),
     '#define CO_STAMP_CHECK_SITE 0xC04AEAu\nstatic const uint8_t kCoStampCheckStock[5] = {%s};\n\n' % hexes(stock[0xC04AEA]),
     '/* The constructor, 0xC04B80, as HellForge leaves it: mov r13d, slots; and the layout part up to 0xC04CE1. */\n',
     '#define CO_STAMP_SLOTS_SITE 0xC04C1Au\n#define CO_STAMP_SLOTS_BACK 0xC04C20u\nstatic const uint8_t kCoStampSlotsStock[6] = {%s};\n' % hexes(stock[0xC04C1A]),
     '#define CO_STAMP_LAYOUT_SITE 0xC04CB2u\n#define CO_STAMP_LAYOUT_BACK 0xC04CE1u\nstatic const uint8_t kCoStampLayoutStock[47] = {\n    %s,\n    %s};\n\n'
     % (hexes(layout[:24]), hexes(layout[24:])),
     '/* The index query, 0xC05360: its first bytes. */\n',
     '#define CO_STAMP_QUERY 0xC05360u\n#define CO_STAMP_QUERY_PIN "4C 89 4C 24 20 48 89 4C 24"\n\n#endif\n']
open(out, 'w').write(''.join(o))
