#!/usr/bin/env python3
# Writes src/co_session_sites.h: the code sites of the session arrays that move into the mod's memory (game build
# 25480438), from scripts/sites/session_sites_by_array.txt and a dump of the game's code:
#   scripts/gen_session_sites.py <game.dll image dump> [out]
# The host and client synchronizers lie inside the session context, packed. Arrays that grow to eight entries stay;
# the neighbours they would overrun move to an extension block, at these offsets in it.
# A site that reaches a moved array through an 8-bit offset cannot hold the new distance: a jump takes its place and
# leads to a stub with the same instruction in its 32-bit form. Some sites fold the array's offset into the index
# ((i + K) * 16); their K changes by the distance / 16, so B5 and B6 keep their offset's remainder of 8.
import re, struct, sys, os

HERE = os.path.dirname(os.path.abspath(__file__)) + '/sites/'
# section tag: (name, old offset in the context, offset in the extension)
ARRAYS = {
    'A2': ('index flags', 0x15AB0, 0x0),
    'B3c': ('list 3 count', 0x16348, 0x8),
    'B1': ('ready peers', 0x162E0, 0x10),
    'B3': ('list 3', 0x16350, 0x50),
    'B5': ('peer values', 0x16418, 0xD8),
    'B6': ('hostability', 0x16458, 0x158),
    'A3': ('party records', 0x15AB8, 0x200),
}
# Index forms with a 32-bit K, changed in place: (rva, offset of K, K, array)
INDEX = [(0x1346F35, 3, 0x1635, 'B3'), (0x134EE33, 2, 0x1635, 'B3'), (0x10898A2, 2, 0x1646, 'B6'), (0x1089D84, 2, 0x1646, 'B6')]
# Sites the list misses: the session init fills the peer values through the client synchronizer's pointer, which it
# keeps in r8 across a call. (rva, value, array)
EXTRA = [(0x134DD65, 0x150, 'B5'), (0x134DD73, 0x160, 'B5'), (0x134DD7A, 0x170, 'B5'), (0x134DD81, 0x180, 'B5')]
# The client's own-values update is inlined in five more functions: two message handlers that find the synchronizer
# by type in the event registry, three that take it from its fixed registry slot (+0x2B98).
for start in (0xB67177, 0xBA0747): EXTRA += [(start, 0x158, 'B5'), (start + 0xD, 0x158, 'B5'), (start + 0x1A, 0x15C, 'B5'), (start + 0x25, 0x15C, 'B5')]
EXTRA += [(0xB64A1A, 0x158, 'B5'), (0xB64A26, 0x15C, 'B5'), (0xB64A31, 0x158, 'B5'), (0xB64A3E, 0x15C, 'B5'),
          (0xB6B57A, 0x158, 'B5'), (0xB6B586, 0x15C, 'B5'), (0xB6B591, 0x158, 'B5'), (0xB6B5A0, 0x15C, 'B5'),
          (0x13713CA, 0x158, 'B5'), (0x13713D6, 0x15C, 'B5'), (0x13713E1, 0x158, 'B5'), (0x13713EE, 0x15C, 'B5')]
# Stubs: (rva, the bytes a jump replaces, the stub's code with zeros for the value, offset of the value, old value,
# array, shift). No branch leads into the replaced bytes behind their first (checked against a full disassembly).
STUBS = [
    (0x1085E7E, '41 c7 47 10 01 00 00 00 4d 89 67 18', '41 c7 47 10 01 00 00 00 4d 89 a7 00 00 00 00', 11, 0x18, 'B1', 0),
    (0xB910D6, '48 83 c0 15 8b cd', '48 05 00 00 00 00 8b cd', 2, 0x15, 'B5', 4),
    (0x108773E, '48 83 c0 15 48 03 c0', '48 05 00 00 00 00 48 03 c0', 2, 0x15, 'B5', 4),
    (0xB92C14, '48 83 c1 09 48 03 c9', '48 81 c1 00 00 00 00 48 03 c9', 3, 0x9, 'B3', 4),
    (0x1088174, '48 83 c1 09 48 03 c9', '48 81 c1 00 00 00 00 48 03 c9', 3, 0x9, 'B3', 4),
    (0xB92C67, '48 8d 4f 09 48 03 c9', '48 8d 8f 00 00 00 00 48 03 c9', 3, 0x9, 'B3', 4),
    (0x10881A9, '48 8d 4f 09 48 8b 7c 24 30', '48 8d 8f 00 00 00 00 48 8b 7c 24 30', 3, 0x9, 'B3', 4),
]
dump = open(sys.argv[1], 'rb').read(); rows = []; short = []; tag = None; order = list(ARRAYS)
for line in open(HERE + 'session_sites_by_array.txt'):
    m = re.match(r'### (\w+) ', line)
    if m: tag = m.group(1); continue
    m = re.match(r'\s*([0-9a-f]+) fn\s+([0-9a-f]+)U?\s+((?:[0-9a-f]{2} )+)\s+(.*?)\s+(hs|cs)\+0x([0-9a-f]+)\s+(\S+)\s*$', line)
    if not m or tag not in ARRAYS: continue
    rva, raw, text, form = int(m.group(1), 16), bytes.fromhex(m.group(3)), m.group(4), m.group(7)
    assert dump[rva:rva + len(raw)] == raw, hex(rva)
    if form.startswith('derived'): continue
    inside = re.search(r'\[[^\]]*?(0x[0-9a-f]+)\]', text)
    value = int(inside.group(1) if inside else re.findall(r'0x[0-9a-f]+', text)[-1], 16)
    if 'disp8' in form or 'addimm8' in form:
        if rva == 0x1085E86: continue  # four bytes: in STUBS with the instruction before it
        # rex, opcode, modrm, sib, disp8: the same with a 32-bit displacement
        assert len(raw) == 5 and raw[2] & 0xC7 == 0x44 and raw[4] == value, (hex(rva), text)
        short.append((rva, raw, bytes([raw[0], raw[1], raw[2] ^ 0xC0, raw[3], 0, 0, 0, 0]), 4, value, tag, 0)); continue
    at = [a for a in range(1, len(raw) - 3) if raw[a:a + 4] == struct.pack('<I', value)]
    assert len(at) == 1, (hex(rva), text)
    rows.append((rva, at[0], value, order.index(tag), 0, raw[:at[0]]))
for rva, at, k, t in INDEX:
    assert struct.unpack_from('<I', dump, rva + at)[0] == k and k * 16 - 8 <= ARRAYS[t][1] <= k * 16; rows.append((rva, at, k, order.index(t), 4, dump[rva:rva + at]))
for rva, value, t in EXTRA:
    at = [a for a in (2, 3) if struct.unpack_from('<I', dump, rva + a)[0] == value]; assert len(at) == 1 and dump[rva + at[0] - 2] in (0x89, 0x8B), hex(rva)
    rows.append((rva, at[0], value, order.index(t), 0, dump[rva:rva + at[0]]))
for rva, old, code, at, value, t, shift in STUBS:
    old = bytes.fromhex(old); assert dump[rva:rva + len(old)] == old, hex(rva); short.append((rva, old, bytes.fromhex(code), at, value, t, shift))
assert len({r[0] for r in rows} | {c[0] for c in short}) == len(rows) + len(short)
out = ['/* Generated by scripts/gen_session_sites.py from scripts/sites/; do not edit. Game build 25480438. */',
       '#ifndef CO_SESSION_SITES_H', '#define CO_SESSION_SITES_H', '',
       '/* {old offset in the session context, offset in the extension} */', 'static const CoSessionArray kCoSessionArrays[] = {']
out += ['    {0x%Xu, 0x%Xu}, /* %s */' % (ARRAYS[t][1], ARRAYS[t][2], ARRAYS[t][0]) for t in order]
out += ['};', '', '/* {rva, offset of the value in the instruction, its vanilla value, array, the instruction bytes before it} */',
        'static const CoSessionSite kCoSessionSites[] = {']
out += ['    {0x%Xu, %d, 0x%Xu, %d, %d, {%s}},' % (r, a, v, i, sh, ', '.join('0x%02X' % b for b in p)) for r, a, v, i, sh, p in sorted(rows)]
hexes = lambda b: ', '.join('0x%02X' % x for x in b)
out += ['};', '', '/* {rva, bytes replaced, stub length, offset of the value in the stub, its vanilla value, array, shift, the', ' * replaced bytes, the stub} */',
        'static const CoSessionStub kCoSessionStubs[] = {']
out += ['    {0x%Xu, %d, %d, %d, 0x%Xu, %d, %d, {%s}, {%s}},' % (r, len(o), len(c), a, v, order.index(t), sh, hexes(o), hexes(c)) for r, o, c, a, v, t, sh in sorted(short)]
out += ['};', '', '#endif', '']
open(sys.argv[2] if len(sys.argv) > 2 else 'src/co_session_sites.h', 'w').write('\n'.join(out))
print(len(rows), 'sites;', ', '.join('%s %d' % (t, sum(1 for r in rows if r[3] == i)) for i, t in enumerate(order)))
print(len(short), 'stubs')
