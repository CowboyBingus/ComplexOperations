#!/usr/bin/env python3
# Writes src/xl/co_spawner_sites.h: the enemy director's spawner list (HiveMind manager +0x82D98, 128 x 0x58, count
# +0x85998) as HellForge's level 11 leaves it, and the game's code the mod's own list needs (game build 25480438):
#   scripts/gen_spawner_sites.py <game.dll image dump> <HellForge native/mod-loader> [out]
# HellForge's site, pin and stub are read from its source; the game's bytes, the list's 22 users and the order of a
# mission's level init, the manager's clear and the first append are checked on the dump.
import glob, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
game = open(sys.argv[1], 'rb').read()
stock = sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else HERE + '/../src/xl/co_spawner_sites.h'
missions = open(os.path.join(stock, 'difficulty_missions.c')).read()
CODE_END = 0x20A0000


def array(text, name):
    m = re.search(r'static const uint8_t ' + name + r'\[(\d*)\] = \{(.*?)\};', text, re.S)
    assert m, name
    body = re.sub(r'/\*.*?\*/', '', m.group(2), flags=re.S)
    b = bytes(int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]{2}|\b0\b', body))
    assert not m.group(1) or len(b) == int(m.group(1)), name
    return b


def define(text, name): return int(re.search(r'#define ' + name + r' (0x[0-9A-Fa-f]+)u', text).group(1), 16)


def hexes(b, per=24):
    rows = [', '.join('0x%02X' % x for x in b[i:i + per]) for i in range(0, len(b), per)]
    return (',\n    '.join(rows)) if len(rows) > 1 else rows[0]


def rel(at): return at + 4 + struct.unpack_from('<i', game, at)[0]


def calls(target):
    found = []
    for op in (0xE8, 0xE9):
        i = game.find(bytes([op]), 0x1000, CODE_END)
        while i != -1:
            if rel(i + 1) == target: found.append(i)
            i = game.find(bytes([op]), i + 1, CODE_END)
    return sorted(found)


# ---- HellForge's guard: a jump of 6 bytes at the count's load, its stub with the list's 128 places.
site, add, done = define(missions, 'DIF_SPAWNER_SITE'), define(missions, 'DIF_SPAWNER_ADD'), define(missions, 'DIF_SPAWNER_DONE')
pin, guard = array(missions, 'kSpawnerPin'), array(missions, 'kSpawnerStub')
assert site == 0xAD3778 and add == site + 6 and done == 0xAD390B and game[site:site + 6] == pin
assert len(guard) == 30 and guard[:6] == pin and guard[6] == 0x3D and struct.unpack('<I', guard[7:11])[0] == 128
assert guard[11:14] == b'\x73\x05\xE9' and guard[18:21] == b'\xF0\xFF\x05' and guard[25] == 0xE9
for line in ('memcpy(spawnerStub + 14, &rel, 4);', 'memcpy(spawnerStub + 21, &rel, 4);', 'memcpy(spawnerStub + 26, &rel, 4);',
             'd->game + DIF_SPAWNER_ADD - (uintptr_t)(spawnerStub + 18)', 'd->game + DIF_SPAWNER_DONE - (uintptr_t)(spawnerStub + 30)',
             'uint8_t spawners[6] = {0xE9, 0, 0, 0, 0, 0x90};', 'ApiWrite(d->game + DIF_SPAWNER_SITE, spawners, sizeof spawners)'):
    assert line in missions, line
# and no step of its own in GuardForce's builder (0x94A270), whose loop the mod takes
for name in glob.glob(os.path.join(stock, '*.[ch]')):
    for value in re.findall(r'0x94[AB][0-9A-Fa-f]{3}\b', open(name, errors='replace').read()):
        assert not 0x94A270 <= int(value, 16) < 0x94B300, (name, value)

# ---- the game's append behind the count's load: stages key, pose, type and unit on the stack, copies 0x58 bytes to the
# entry, counts and leaves. No instruction in it is relative to rip but the last jump; rbx (the manager) is used by the
# six stores and the count alone, rcx is set by the imul at +9.
body = bytes.fromhex(
    '410F104E10' '48897DB8' '486BC858' '0F11442478' '410F104620' '0F114D88' '44897DC0' '410F104E30' '0F114598' '0F28442470'
    '0F118419982D0800' '0F284590' '0F114DA8' '0F284D80' '0F118C19A82D0800' '0F284DA0' '0F118419B82D0800' '0F2845B0'
    '0F118C19C82D0800' 'F20F104DC0' '0F118419D82D0800' 'F20F118C19E82D0800' 'FF8398590800')
assert game[add:add + len(body)] == body and add + len(body) == 0xAD37FB and body[9:13] == bytes.fromhex('486BC858')
assert game[0xAD37FB] == 0xE9 and rel(0xAD37FC) == done
assert game[0xAD373F:0xAD3744] == bytes.fromhex('4C896C2470')  # mov [rsp+0x70], r13: the key, staged before the site
SPLIT = 13

# ---- GuardForce's builder: the loop over the list, r13 the manager (the world + 0x1FA08, kept at [rsp+0x48]).
base_load, base_add = bytes.fromhex('4C8B2D93C09D02'), bytes.fromhex('4981C508FA0100')
assert game[0x94A2A6:0x94A2AD] == base_load and rel(0x94A2A9) == 0x3326340 and game[0x94A2B2:0x94A2B9] == base_add
assert game[0x94A2BD:0x94A2C2] == bytes.fromhex('4C896C2448') and game[0x94A511:0x94A516] == bytes.fromhex('4C8B6C2448')
first, nxt = bytes.fromhex('4539A5985908000F86AD060000'), bytes.fromhex('453B95985908000F8263F9FFFF')
FIRST, NEXT, LOOP, EMPTY, LOOP_DONE, PLACES = 0x94A52D, 0x94ABD0, 0x94A540, 0x94ABE7, 0x94ABDD, 0x94ABAF
assert game[FIRST:FIRST + 13] == first and rel(FIRST + 9) == EMPTY and game[NEXT:NEXT + 13] == nxt and rel(NEXT + 9) == LOOP and NEXT + 13 == LOOP_DONE
# a spawner's position into its group's places: 12 bytes each from group + 0x18C, their count at +0x24C, so 16 places
places = bytes.fromhex('418B804C020000' '4883C021' '488D0C40' 'F2410F112488' '4189548808' '41FF804C020000')
assert game[PLACES:PLACES + 33] == places and PLACES + 33 == NEXT and (0x24C - 0x18C) // 12 == 16 and 0x21 * 12 == 0x18C

# ---- every user of the list: a 32-bit displacement of an entry's field or the count. The same four bytes are also a
# call's or jump's distance (E8 / E9 before them) or begin a call or jump over 0x82D bytes (E8 / E9, 2D 08 00 00);
# those are none. 8 in the append (0xAD2010), 14 in the builder, each covered by a site above.
users = []
for i in range(0x1000, CODE_END - 4):
    if game[i + 2] != 0x08 or game[i + 3] != 0x00: continue
    v = struct.unpack_from('<I', game, i)[0]
    if not (0x82D98 <= v < 0x82D98 + 0x58 or v == 0x85998): continue
    if game[i - 1] in (0xE8, 0xE9) or (game[i] in (0xE8, 0xE9) and game[i + 4] == 0x00 and game[i - 1] != 0x2B): continue
    users.append(i)
appends = [u for u in users if 0xAD2010 <= u < 0xAD3E20]
loop = [u for u in users if 0x94A270 <= u < 0x94B300]
assert len(users) == 22 and len(appends) == 8 and len(loop) == 14, [hex(u) for u in users]
assert all(site <= u < 0xAD37FB for u in appends) and all(FIRST <= u < LOOP_DONE for u in loop)
# r13 in the loop: the twelve entry reads and the count, and no branch leaves the loop but its end
r13 = [u for u in loop if FIRST + 13 <= u < NEXT]
assert len(r13) == 12 and all(game[u - 1] == 0x2B for u in r13)  # sib: [rbx + r13 + disp]

# ---- the order at mission load. The state change (0xAB2FA0) enters state 4, the mission, by clearing the manager
# (0x8C810 bytes, the count with them) and calling its enter, which appends the map's spawners; state 4 is entered
# from state 6's update alone, whose enter set the level up (0xF71220 with no preview record) on the same generator.
assert struct.unpack_from('<I', game, 0xAB332C + 4 * 3)[0] == 0xAB3121 and struct.unpack_from('<I', game, 0xAB332C + 4 * 5)[0] == 0xAB315A
assert game[0xAB3121:0xAB3147] == bytes.fromhex('33D2' '488D8F08FA0100' '41B810C80800' 'E86B795D01' '4C8BC6' '488D8F08FA0100' '488BD7' 'E8F92B0100')
assert rel(0xAB3131) == 0x208AAA0 and rel(0xAB3143) == 0xAC5D40 and 0x85998 < 0x8C810
assert calls(0xAC5D40) == [0xAB3142] and calls(0xACF710) == [0xAC637A] and calls(0xAD2010) == [0xACF7B6] and 0xAC5D40 < 0xAC637A < 0xACF710
assert rel(0xAB3165) == 0xAC4C50 and calls(0xAC4C50) == [0xAB3164] and calls(0xAB8900) == [0xAC4DE7, 0xAD922F]
assert game[0xAB8975:0xAB8986] == bytes.fromhex('4533C9' '4533C0' '660F7F442420' 'E89A884B00') and rel(0xAB8982) == 0xF71220
# state 4 is asked for at five places; four lie in functions nothing in the image calls or points at
four = [c for c in calls(0xAB2FA0) if game[c - 4:c] == bytes.fromhex('418D5004') or game[c - 10:c - 5] == bytes.fromhex('BA04000000')
        or game[c - 14:c - 10] == bytes.fromhex('418D5004')]
assert four == [0xAC534B, 0xAC53D7, 0xAD9581, 0xAD975E], [hex(c) for c in four]
assert calls(0xAC50E0) == [0xAB6115] and calls(0xAC5360) == [] and calls(0xAD9550) == [] and calls(0xAD96B0) == [0xAD9540] and calls(0xAD9540) == []
image = struct.unpack_from('<Q', game, struct.unpack_from('<I', game, 0x3C)[0] + 24 + 24)[0]
for dead in (0xAC5360, 0xAD9550, 0xAD9540, 0xAD91C0):
    assert game.find(struct.pack('<Q', image + dead)) == -1, hex(dead)
# the size's store, where the mod's level init runs: 0xF71220 has no return and no branch past it before it (listing)
assert game[0xF714E8:0xF714F1] == bytes.fromhex('F3410F1185E0010000')

o = ['/* Generated by scripts/gen_spawner_sites.py from HellForge\'s source and the game\'s image; do not edit. Game build 25480438. */\n',
     '#ifndef CO_SPAWNER_SITES_H\n#define CO_SPAWNER_SITES_H\n\n#include <stdint.h>\n\n',
     '/* The list in the HiveMind manager (the world + 0x1FA08): 128 entries of 0x58, their count behind them. */\n',
     '#define CO_SPAWNER_ENTRIES 0x82D98u\n#define CO_SPAWNER_ENTRY 0x58u\n#define CO_SPAWNER_COUNT 0x85998u\n',
     '#define CO_SPAWNER_WORLD 0x3326340u\n#define CO_SPAWNER_MANAGER 0x1FA08u\n\n',
     '/* The append\'s load of the count: HellForge\'s jump of 6 bytes, and its stub (128 places; its jumps at 14 and 26,\n'
     ' * its counter at 21). */\n',
     '#define CO_SPAWNER_SITE 0x%Xu\n#define CO_SPAWNER_ADD 0x%Xu\n#define CO_SPAWNER_DONE 0x%Xu\n' % (site, add, done),
     'static const uint8_t kCoSpawnerPin[6] = {%s};\n' % hexes(pin),
     'static const uint8_t kCoSpawnerGuard[30] = {\n    %s};\n' % hexes(guard),
     '/* The game\'s append from 0x%X to its jump at 0x%X: the entry\'s offset is in rcx after the first %d bytes. */\n' % (add, add + len(body), SPLIT),
     '#define CO_SPAWNER_BODY_SPLIT %du\n' % SPLIT,
     'static const uint8_t kCoSpawnerBody[%d] = {\n    %s};\n\n' % (len(body), hexes(body)),
     '/* GuardForce\'s builder: where r13 becomes the manager, and its loop\'s two tests of the count. */\n',
     '#define CO_SPAWNER_BASE_LOAD 0x94A2A6u\n#define CO_SPAWNER_BASE_ADD 0x94A2B2u\n',
     'static const uint8_t kCoSpawnerBaseLoad[7] = {%s};\nstatic const uint8_t kCoSpawnerBaseAdd[7] = {%s};\n' % (hexes(base_load), hexes(base_add)),
     '#define CO_SPAWNER_FIRST 0x%Xu\n#define CO_SPAWNER_NEXT 0x%Xu\n#define CO_SPAWNER_LOOP 0x%Xu\n#define CO_SPAWNER_EMPTY 0x%Xu\n#define CO_SPAWNER_LOOP_DONE 0x%Xu\n'
     % (FIRST, NEXT, LOOP, EMPTY, LOOP_DONE),
     'static const uint8_t kCoSpawnerFirst[13] = {%s};\nstatic const uint8_t kCoSpawnerNext[13] = {%s};\n' % (hexes(first), hexes(nxt)),
     '/* A spawner\'s position into its group\'s 16 places; the count\'s load is the first 7 bytes. */\n',
     '#define CO_SPAWNER_PLACES 0x%Xu\n#define CO_SPAWNER_GROUP_PLACES 16u\n' % PLACES,
     'static const uint8_t kCoSpawnerPlaces[33] = {\n    %s};\n\n#endif\n' % hexes(places)]
open(out, 'w').write(''.join(o))
