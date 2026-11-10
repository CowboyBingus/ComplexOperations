#!/usr/bin/env python3
# Writes src/co_manager_sites.h: the code sites of the per-player component managers (game build 25480438), from the
# reviewed lists in scripts/sites/ and a dump of the game's code:
#   scripts/gen_manager_sites.py <game.dll image dump> [out]
# A manager is a struct inside the game's component block with four slots and the next manager right behind it. The
# mod builds each one in its own memory with eight slots. Four kinds of site:
#   BLOCK  reaches the manager from the block (block + displacement): gets the distance to the new struct
#   ARRAY  reaches a per-slot array that eight slots of its neighbour would overrun: the array has a new offset
#   CAP    the capacity check `cmp eax,4`, or another four in a loop over a moved array
#   CODE   four bytes of code replaced
# Every row is checked against the dump here, and again by the mod against the running game before it writes.
import struct, sys, os

HERE = os.path.dirname(os.path.abspath(__file__)) + '/sites/'
# name: (global, offset in the block, size, new size, [(old start, old end, new start) of each array that moves])
MANAGERS = {
    'score':              (0x3326AE0, 0x7C7368, 0x37E38, 0x380A0, [(0x37D70, 0x37D90, 0x37EE0), (0xC440, 0xC4A0, 0x37F20), (0xC4A0, 0xC500, 0x37FE0)]),
    'customization':      (0x33264F8, 0xE9FC98, 0xB80,   0xEA8,   [(0x968, 0x96C, 0xC80), (0x96C, 0xA7C, 0xC88)]),
    'kill_streak':        (0x3326AF0, 0x2A988,  0x4A8,   0xCE8,   [(0x88, 0x4A8, 0x4A8)]),
    'ship_inventory':     (0x33266D0, 0xEB5178, 0x168,   0x318,   [(0x90, 0x160, 0x170), (0x160, 0x164, 0x310)]),
    'ship_customization': (0x33267B8, 0x3CA0,   0x2B8,   0x620,   [(0x90, 0x98, 0x398), (0x98, 0x1D8, 0x3A0)]),
    'ship_modules_sync':  (0x3326998, 0x4148,   0x267E8, 0x73548, [(0x88, 0x26528, 0x26948), (0x26528, 0x26688, 0x73288)]),
    'player':             (0x3326468, 0xEA0818, 0x428,   0x9E8,   [(0x108, 0x2C8, 0x4A8), (0x2C8, 0x3A8, 0x828)]),
}
# Sites the lists miss, found later: (manager, rva, offset of the disp32, old value)
EXTRA = [('player', 0xACC65B, 3, 0x2C8)]
# More site lists of a manager. Score's loot tables (four rows of 0x18 by score slot at +0xC440, four by player index at
# +0xC4A0) move behind the manager, so that eight rows of each fit.
LISTS = {'score': ['patch_sites_score.tsv', 'patch_sites_score_loot.tsv']}
LOOT_ROWS = 54
# The loops over the loot rows by score slot count to eight once the rows have moved: (manager, rva, instruction, offset
# of the four)
LOOPS = [('score', 0x63090B, '83 f9 04', 2), ('score', 0x63095B, '83 f8 04', 2), ('score', 0x630980, '44 8d 40 04', 3),
         ('score', 0x126EBFC, '83 f8 04', 2), ('score', 0x1347C1D, '83 f9 04', 2), ('score', 0x1380C68, '41 be 04 00 00 00', 2),
         ('score', 0x19940EB, '83 f9 04', 2)]
# r14d of the loop at 0x1380C68 is also the mission end report's outcome code, read at 0x1380F0B: mov eax,r14d becomes
# push 4; pop rax, the value it had. (manager, rva, four bytes, four new bytes)
CODE = [('score', 0x1380F0B, '41 8b c6 89', '6a 04 58 89')]
# classb_final.tsv lists every instruction with a fitting value. In these functions the base register is not the block
# (another object, or the manager itself, which patch_sites covers), except at the sites of BLOCK_ONLY.
NOT_BLOCK = {('ship_modules_sync', 'fn 0x569490'), ('ship_modules_sync', 'fn 0x571250'), ('ship_modules_sync', 'fn 0xbc2d30')}
BLOCK_ONLY = {0x57142E, 0xBD13E7}
# Block sites the list misses: the statistics reset at 0x56FFF0 reaches the block through a second global.
EXTRA_BLOCK = [('ship_customization', 0x57009B, 4), ('ship_modules_sync', 0x5700C5, 0), ('kill_streak', 0x5700D1, 0)]
only = None
args = [a for a in sys.argv[1:] if not a.startswith('--only=')]
for a in sys.argv[1:]:
    if a.startswith('--only='): only = a[7:].split(',')
dump = open(args[0], 'rb').read(); names = [m for m in MANAGERS if not only or m in only]; rows = []


def delta(m, field):
    for lo, hi, new in MANAGERS[m][4]:
        if lo <= field < hi: return new - lo
    return None


for mi, m in enumerate(names):
    g, off, size, new_size, moved = MANAGERS[m]
    for line in [l for f in LISTS.get(m, [f'patch_sites_{m}.tsv']) for l in open(HERE + f).read().splitlines()[1:]]:
        c = line.split('\t'); rva, at, raw = int(c[0], 16), int(c[5]), bytes.fromhex(c[6])
        old = struct.unpack_from('<I', raw, at)[0]; assert dump[rva:rva + len(raw)] == raw, (m, hex(rva))
        assert old == int(c[4], 16) and delta(m, old) is not None, (m, line)
        rows.append((rva, at, old, mi, delta(m, old), 'CO_MGR_ARRAY'))
    for em, rva, at, old in EXTRA:
        if em == m:
            assert struct.unpack_from('<I', dump, rva + at)[0] == old and delta(m, old) is not None; rows.append((rva, at, old, mi, delta(m, old), 'CO_MGR_ARRAY'))
    for line in open(HERE + 'classb_final.tsv').read().splitlines():
        c = line.split('\t')
        if c[0] != m or (m, c[3]) in NOT_BLOCK and int(c[2], 16) not in BLOCK_ONLY: continue
        rva, field = int(c[2], 16), int(c[1], 16); want = struct.pack('<I', off + field)
        at = [a for a in range(1, 9) if dump[rva + a:rva + a + 4] == want]; assert len(at) == 1, (m, hex(rva), at)
        assert 0 <= field < size, line
        rows.append((rva, at[0], off + field, mi, delta(m, field) or 0, 'CO_MGR_BLOCK'))
    for em, rva, field in EXTRA_BLOCK:
        if em == m:
            at = dump.find(struct.pack('<I', off + field), rva, rva + 12) - rva; assert 1 <= at <= 8, hex(rva)
            rows.append((rva, at, off + field, mi, delta(m, field) or 0, 'CO_MGR_BLOCK'))
    caps = 0
    for line in open(HERE + 'cap_sites.tsv').read().splitlines()[1:]:
        c = line.split('\t')
        if c[0] != m: continue
        rva = int(c[3], 16); assert dump[rva:rva + 3] == bytes([0x83, 0xF8, 0x04]), hex(rva); rows.append((rva, 2, 4, mi, 0, 'CO_MGR_CAP')); caps += 1
    assert caps == 4, (m, caps)
    for em, rva, raw, at in LOOPS:
        raw = bytes.fromhex(raw)
        if em == m:
            assert dump[rva:rva + len(raw)] == raw and raw[at] == 4, hex(rva); rows.append((rva, at, 4, mi, 0, 'CO_MGR_CAP'))
    for em, rva, raw, new in CODE:
        if em == m:
            assert dump[rva:rva + 4] == bytes.fromhex(raw)
            rows.append((rva, 0, struct.unpack('<I', bytes.fromhex(raw))[0], mi, struct.unpack('<i', bytes.fromhex(new))[0], 'CO_MGR_CODE'))
if only is None:
    loot = sum(1 for r in rows if r[5] == 'CO_MGR_ARRAY' and names[r[3]] == 'score' and 0xC440 <= r[2] < 0xC500)
    assert loot == LOOT_ROWS, loot
from collections import Counter
dup = [r for r in sorted(rows) if Counter(x[0] for x in rows)[r[0]] > 1]
for r in dup: print('DUP %x at %d old %x %s add %x %s' % (r[0], r[1], r[2], names[r[3]], r[4], r[5]))
assert not dup, 'a site is listed twice'
out = ['/* Generated by scripts/gen_manager_sites.py from scripts/sites/; do not edit. Game build 25480438. */',
       '#ifndef CO_MANAGER_SITES_H', '#define CO_MANAGER_SITES_H', '',
       '/* {name, the global that holds it, offset in the block, size, new size} */',
       'static const CoManager kCoManagers[] = {']
out += ['    {"%s", 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu},' % (m, *MANAGERS[m][:4]) for m in names]
out += ['};', '', '/* {rva, offset of the value in the instruction, its vanilla value, manager, what the new array offset adds,',
        ' * kind, the instruction bytes before the value} */', 'static const CoManagerSite kCoManagerSites[] = {']
for rva, at, old, mi, add, kind in sorted(rows):
    pin = ', '.join('0x%02X' % b for b in dump[rva:rva + at]) or '0'
    out.append('    {0x%Xu, %d, 0x%Xu, %d, %d, %s, {%s}},' % (rva, at, old, mi, add, kind, pin))
out += ['};', '', '#endif', '']
open(args[1] if len(args) > 1 else 'src/co_manager_sites.h', 'w').write('\n'.join(out))
print(len(rows), 'sites;', ', '.join('%s %d' % (m, sum(1 for r in rows if r[3] == i)) for i, m in enumerate(names)))
