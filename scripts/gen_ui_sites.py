#!/usr/bin/env python3
# Writes src/co_ui_sites.h: the code sites of the tables a build for eight moves to blocks of the mod's at run time
# (game build 25480438), from scripts/sites/ and a dump of the game's code:
#   scripts/gen_ui_sites.py <game.dll image dump> [out]
# A block's sites have three layouts: the game's (OFF), four entries with the guards (KEPT) and eight entries in the
# mod's block (MOVED). A layout writes, at each site, one of:
#   GAME   the game's bytes
#   BYTES  new bytes of the same length
#   REL    the game's instruction with its 4-byte displacement moved: old + D + K, D known only when the block moves
#   JUMP   a jump to a cave and nops; a cave's fields of the block's table get old + D + K the same way
# Every row is checked against the dump here, and again by the mod against the running game before it writes. No byte
# may be shared with another site here or with any site, cave or stub of the mod's other steps in a build for eight.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE + '/sites')
import cave_asm, hud_sites, map_sites, tag_sites

# (C name, module, owner): the map's owner is the component block, the others' the HUD's second part H2.
BLOCKS = [('CO_UI_MAP', map_sites, 'block'), ('CO_UI_HUD', hud_sites, 'H2'), ('CO_UI_TAG', tag_sites, 'H2')]
GAME, BYTES, REL, JUMP = 'CO_UI_GAME', 'CO_UI_BYTES', 'CO_UI_REL', 'CO_UI_JUMP'
dump = open(sys.argv[1], 'rb').read()
out = sys.argv[2] if len(sys.argv) > 2 else HERE + '/../src/co_ui_sites.h'

slots = {}          # rva -> slot
caves = []          # (block, text, fields, where)


def slot(rva, size, block):
    s = slots.setdefault(rva, {'size': size, 'block': block, 'kept': (GAME,), 'moved': None})
    assert s['size'] == size and s['block'] == block and size <= 12, hex(rva)
    return s


def cave(block, rva, replaced, text, fields=()):
    replaced = bytes.fromhex(replaced)
    assert dump[rva:rva + len(replaced)] == replaced and 5 <= len(replaced) <= 12, hex(rva)
    caves.append((block, text, list(fields), hex(rva)))
    return slot(rva, len(replaced), block), len(caves) - 1


for b, (_, m, _) in enumerate(BLOCKS):
    for rva, length, at, old, region in m.REL:
        assert m.region_of(old) == region and 1 <= at <= length - 4 and length <= 12, hex(rva)
        assert struct.unpack_from('<I', dump, rva + at)[0] == old, hex(rva)
        s = slot(rva, length, b); assert s['moved'] is None, hex(rva)
        s['moved'] = (REL, at, m.K[region])
    for rva, old, new, _ in m.INPLACE:
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        assert len(old) == len(new) <= 12 and dump[rva:rva + len(old)] == old and old != new, hex(rva)
        s = slot(rva, len(old), b); assert s['moved'] is None, hex(rva)
        s['moved'] = (BYTES, new)
    for rva, replaced, text, _, fields in m.CAVES:
        s, c = cave(b, rva, replaced, text, [(old, m.K[region]) for old, region in fields])
        assert s['moved'] is None, hex(rva); s['moved'] = (JUMP, c)
    for rva, replaced, text, _ in m.GUARD8:
        s, c = cave(b, rva, replaced, text)
        assert s['moved'] is None, hex(rva); s['moved'] = (JUMP, c)
    # The guards for four stand at sites the eight-entry layout changes as well; each is KEPT's bytes of that site.
    for rva, replaced, text in m.GUARD4:
        assert rva in slots, hex(rva)
        s, c = cave(b, rva, replaced, text); s['kept'] = (JUMP, c)
    for rva, old, new in m.GUARD4_SITES:
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        assert rva in slots and slots[rva]['size'] == len(old) and dump[rva:rva + len(old)] == old, hex(rva)
        slots[rva]['kept'] = (BYTES, new)
assert all(s['moved'] is not None for s in slots.values())

# Assemble the caves; the fields of the block's table are found in the code by their old value.
blob = bytearray(); cave_rows = []; field_rows = []
for block, text, fields, where in caves:
    code, jumps = cave_asm.relocatable(lambda game: cave_asm.at_game(text, game), where)
    assert len(jumps) <= 3, where
    first = len(field_rows)
    for old, add in fields:
        hits = [i for i in range(len(code) - 3) if code[i:i + 4] == struct.pack('<I', old)]
        assert len(hits) == 1, (where, hex(old))
        field_rows.append((len(blob) + hits[0], old, add))
    cave_rows.append((len(blob), len(code), jumps, first, len(fields)))
    blob += code

# Overlaps: none between these sites, none with what the other steps write in a build for eight (counts group 6 is
# the build for four's only).
def rows(name, part, end=None):
    """The leading numbers of each row of a table in a generated header."""
    text = open(HERE + '/../src/' + name).read()
    text = text[text.index(part):]
    if end: text = text[:text.index(end)]
    found = []
    for line in re.findall(r'^    \{(0x[0-9A-F]+u[^{]*)', text, re.M):
        found.append(tuple(int(x.strip().rstrip('u'), 0) for x in line.split(',') if re.fullmatch(r'\s*(0x[0-9A-F]+u?|\d+)\s*', x)))
    return found
taken = []
stubs = [(r[0], r[1]) for r in rows('co_session_sites.h', 'kCoSessionStubs')]
taken += [(rva, rva + size, 'session stub') for rva, size in stubs]
taken += [(r[0] + r[1], r[0] + r[1] + 4, 'session') for r in rows('co_session_sites.h', 'kCoSessionSites', 'kCoSessionStubs') if r[:2] not in stubs]
taken += [(r[0] + r[1], r[0] + r[1] + 4, 'managers') for r in rows('co_manager_sites.h', 'kCoManagerSites', 'kCoManagerCaves')]
# A managers' cave {rva, size, back, ...} replaces `size` bytes; the bytes up to where it goes back are dead too.
taken += [(r[0], max(r[0] + r[1], r[2]), 'managers cave') for r in rows('co_manager_sites.h', 'kCoManagerCaves')]
taken += [(r[0] + r[1], r[0] + r[1] + 4, 'pods') for r in rows('co_pod_sites.h', 'kCoPodSites')]
taken += [(r[0], r[0] + r[1], 'counts') for r in rows('co_count_sites.h', 'kCoCountSites', 'kCoCountClears') if r[3] != 6]
taken += [(r[0], r[0] + 8, 'counts clear') for r in rows('co_count_sites.h', 'kCoCountClears', 'kCoCountCode')]
taken += [(r[0], r[0] + r[1], 'counts cave') for r in rows('co_count_sites.h', 'kCoCountCaves', 'kCoMessageShapes') if r[2] != 6]
# The hook of H2's init (co_ui.c) replaces the function's first instructions.
taken += [(0x12E96D0, 0x12E96D0 + 31, 'hook of 0x12E96D0')]
assert len(taken) > 2000, len(taken)
mine = sorted((rva, rva + s['size']) for rva, s in slots.items())
for (a, b), (c, _) in zip(mine, mine[1:]): assert b <= c, (hex(a), hex(c))
for a, b in mine:
    for c, d, what in taken: assert b <= c or d <= a, (hex(a), what, hex(c))


def c_bytes(b): return ', '.join('0x%02X' % x for x in b) or '0'


h = ['/* Generated by scripts/gen_ui_sites.py from scripts/sites/; do not edit. Game build 25480438. */\n',
     '#ifndef CO_UI_SITES_H\n#define CO_UI_SITES_H\n\n',
     'enum { %s };\n' % ', '.join(name for name, _, _ in BLOCKS),
     '/* {name, the old table\'s address from its owner, the new table\'s size} */\n',
     'static const CoUiBlockInfo kCoUiBlocks[] = {\n']
for name, m, owner in BLOCKS:
    h.append('    {"%s", 0x%Xu, 0x%Xu},  /* owner: %s */\n' % (name[6:].lower(), m.OLD, m.SIZE, owner))
h.append('};\n\n/* {rva, size, block, KEPT, MOVED, offset of the moved displacement, its K, KEPT\'s cave, MOVED\'s cave, game bytes,\n'
         ' * KEPT\'s bytes, MOVED\'s bytes} */\nstatic const CoUiSite kCoUiSites[] = {\n')
for rva, s in sorted(slots.items(), key=lambda x: (x[1]['block'], x[0])):
    game = dump[rva:rva + s['size']]
    kept, moved = s['kept'], s['moved']
    at, add = (moved[1], moved[2]) if moved[0] == REL else (0, 0)
    h.append('    {0x%Xu, %d, %d, %s, %s, %d, %d, %d, %d, {%s}, {%s}, {%s}},\n' % (
        rva, s['size'], s['block'], kept[0], moved[0], at, add, kept[1] if kept[0] == JUMP else 0, moved[1] if moved[0] == JUMP else 0,
        c_bytes(game), c_bytes(kept[1] if kept[0] == BYTES else b''), c_bytes(moved[1] if moved[0] == BYTES else b'')))
h.append('};\n\nstatic const uint8_t kCoUiCode[] = {\n')
for i in range(0, len(blob), 24): h.append('    ' + c_bytes(blob[i:i + 24]) + ',\n')
h.append('};\n/* {where in kCoUiCode, length, its jumps into the game, first field, fields} */\nstatic const CoUiCave kCoUiCaves[] = {\n')
for code, length, jumps, first, n in cave_rows:
    h.append('    {%d, %d, {%s}, %d, %d},\n' % (code, length, ', '.join(str(j) for j in jumps + [0] * (3 - len(jumps))), first, n))
h.append('};\n/* {where in kCoUiCode, the old value, its K} */\nstatic const CoUiField kCoUiFields[] = {\n')
for at, old, add in field_rows or [(0, 0, 0)]: h.append('    {%d, 0x%Xu, %d},\n' % (at, old, add))
h.append('};\n\n#endif\n')
open(out, 'w').writelines(h)
print('%d sites, %d caves (%d bytes), %d fields; no overlap with %d sites of the other steps (%s)' % (
    len(slots), len(caves), len(blob), len(field_rows), len(taken),
    ', '.join('%s %d' % (w, sum(t[2] == w for t in taken)) for w in sorted({t[2] for t in taken}))))
for b, (name, m, _) in enumerate(BLOCKS):
    mine = [s for s in slots.values() if s['block'] == b]
    print('  %-10s %3d sites: %3d REL, %3d in place, %3d jumps; KEPT writes %d' % (
        name, len(mine), sum(s['moved'][0] == REL for s in mine), sum(s['moved'][0] == BYTES for s in mine),
        sum(s['moved'][0] == JUMP for s in mine), sum(s['kept'][0] != GAME for s in mine)))
