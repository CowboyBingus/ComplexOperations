#!/usr/bin/env python3
# Writes src/co_session_sites.h: the code sites of the session arrays that move into the mod's memory (game build
# 25480438), from scripts/sites/session_sites_by_array.txt and a dump of the game's code:
#   scripts/gen_session_sites.py <game.dll image dump> [out]
# The host and client synchronizers lie inside the session context, packed. Arrays that grow to eight entries stay;
# the neighbours they would overrun move to an extension block, at these offsets in it.
# A site that reaches a moved array through an 8-bit offset cannot hold the new distance: a jump takes its place and
# leads to a stub with the same instruction in its 32-bit form. Some sites fold the array's offset into the index
# ((i + K) * 16); their K changes by the distance / 16, so B5 and B6 keep their offset's remainder of 8.
# The lobby's member lists move the same way (MIRROR, COPIES); their stubs are assembled here with GNU as.
import re, struct, subprocess, sys, os, tempfile

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
    # The lobby's member lists: four ids at +0x1B58 of each of three lobby objects in the context (previous lobby
    # +0x17210, joiner +0x18DC8, main +0x1D470). Room for eight each, placed so that all three move by one distance:
    # code that reaches a list from its object (+0x1B58) is right for any of the three.
    'L0': ('previous lobby members', 0x18D68, 0x1000),
    'L1': ('joiner lobby members', 0x1A920, 0x2BB8),
    'L2': ('main lobby members', 0x1EFC8, 0x7260),
}
assert len({ARRAYS[t][2] - ARRAYS[t][1] for t in ('L0', 'L1', 'L2')}) == 1
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
# Sites the earlier list lacks, from an independent tracker's list of every access (client members without a caller in
# the image, the host migration's index flags). (rva, offset of the value, value, array)
MORE = [(0x1082F86, 3, 0x150, 'B5'), (0x1082F8D, 3, 0x160, 'B5'), (0x1082F94, 3, 0x170, 'B5'), (0x1082F9B, 3, 0x180, 'B5'),
        (0x1084DC1, 3, 0x150, 'B5'), (0x1084DE7, 4, 0x158, 'B5'), (0x1084DF9, 4, 0x15C, 'B5'),
        (0x1084EFA, 3, 0x160, 'B5'), (0x1084F04, 3, 0x170, 'B5'), (0x1084F0F, 3, 0x180, 'B5'),
        (0x108823B, 3, 0x190, 'B6'), (0x1088246, 2, 0x198, 'B6'), (0x1088255, 3, 0x1A0, 'B6'), (0x1088260, 2, 0x1A8, 'B6'),
        (0x108826F, 3, 0x1B0, 'B6'), (0x108827A, 2, 0x1B8, 'B6'), (0x1088289, 3, 0x1C0, 'B6'), (0x1088294, 2, 0x1C8, 'B6'),
        (0x108829E, 3, 0x190, 'B6'), (0x10882A8, 3, 0x1A0, 'B6'), (0x10882B2, 3, 0x1B0, 'B6'), (0x10882BC, 3, 0x1C0, 'B6'),
        (0x1089EAB, 4, 0x100, 'A2'), (0x1089FD7, 3, 0x100, 'A2'), (0x1089FF4, 4, 0x100, 'A2')]
# The member lists' sites, all with a 32-bit distance: (rva, instruction bytes, offset of the distance, list). `any`
# is the distance from a lobby object, the same for all three.
MIRROR = [
    (0x108A650, '4d 39 b4 cd 58 1b 00 00', 4, 'any'), (0x1093630, '49 8d 9e 58 1b 00 00', 3, 'any'),
    (0x1093CA9, '48 8d 97 58 1b 00 00', 3, 'any'), (0x1093CFE, '48 89 84 cf 58 1b 00 00', 4, 'any'),
    (0x1093D17, '48 8d 97 58 1b 00 00', 3, 'any'), (0x1093DBA, '4c 8d b7 58 1b 00 00', 3, 'any'),
    (0x10943A4, '48 8b 94 df 58 1b 00 00', 4, 'any'), (0x10943C9, '48 39 94 df 58 1b 00 00', 4, 'any'),
    (0x1092E8C, '48 8d 93 58 1b 00 00', 3, 'any'), (0x1092F0A, '48 89 84 fb 58 1b 00 00', 4, 'any'),
    (0x1092F2A, '48 8d 93 58 1b 00 00', 3, 'any'), (0x1092F9E, '48 8b 84 c1 58 1b 00 00', 4, 'any'),
    (0x1092FD0, '48 39 94 c1 58 1b 00 00', 4, 'any'),
    (0xB92032, '49 39 9c c8 c8 ef 01 00', 4, 'L2'), (0xB921D2, '49 39 9c c1 c8 ef 01 00', 4, 'L2'),
    (0xBB24E2, '49 39 9c c8 c8 ef 01 00', 4, 'L2'), (0xFEDB73, '4d 39 9c d1 c8 ef 01 00', 4, 'L2'),
    (0xFEF7C3, '49 39 8c c3 c8 ef 01 00', 4, 'L2'), (0xFF1942, '4d 39 9c ca c8 ef 01 00', 4, 'L2'),
    (0x1086CB0, '49 39 9c c0 c8 ef 01 00', 4, 'L2'), (0x1086DF0, '49 39 9c c9 c8 ef 01 00', 4, 'L2'),
    (0x108FB62, '49 39 94 c7 c8 ef 01 00', 4, 'L2'), (0x12DBF90, '4d 39 84 c1 c8 ef 01 00', 4, 'L2'),
    (0x134F003, '48 39 94 c1 c8 ef 01 00', 4, 'L2'),
    (0xB92243, '49 39 9c c9 20 a9 01 00', 4, 'L1'), (0x1086E55, '49 39 9c c9 20 a9 01 00', 4, 'L1'),
    (0x134F070, '4b 39 94 c3 20 a9 01 00', 4, 'L1'),
    (0xB92212, '49 39 9c c1 68 8d 01 00', 4, 'L0'), (0x1086E26, '49 39 9c c9 68 8d 01 00', 4, 'L0'),
    (0x134F042, '49 39 94 c3 68 8d 01 00', 4, 'L0'),
    (0x108FB92, '48 39 94 c7 a0 1b 00 00', 4, 'L0'),   # from the join component, context +0x171C8
]
# The game copies a whole lobby object (0x1BB8 bytes) with the CRT's memcpy: main <- rdx, and previous <- main three
# times. The copy no longer holds the members, so a stub copies the moved list first. rax, r9 and xmm0 to xmm3 are
# free here: memcpy, the next call, takes rcx, rdx and r8 only. SLOT is the list's distance from its object.
COPY = '''
 lea rax,[rdx+SLOT]
 lea r9,[rcx+SLOT]
 movups xmm0,xmmword ptr [rax]
 movups xmm1,xmmword ptr [rax+0x10]
 movups xmm2,xmmword ptr [rax+0x20]
 movups xmm3,xmmword ptr [rax+0x30]
 movups xmmword ptr [r9],xmm0
 movups xmmword ptr [r9+0x10],xmm1
 movups xmmword ptr [r9+0x20],xmm2
 movups xmmword ptr [r9+0x30],xmm3
'''
COPIES = [
    (0x134F79C, '48 8d 8b 70 d4 01 00', ' lea rcx,[rbx+0x1d470]' + COPY),
    (0xB94E97, '48 8d 4f 48 48 81 c2 70 d4 01 00', ' lea rcx,[rdi+0x48]\n add rdx,0x1d470' + COPY),
    (0xBAE2F2, '48 8d 4f 48 48 81 c2 70 d4 01 00', ' lea rcx,[rdi+0x48]\n add rdx,0x1d470' + COPY),
    (0x1090A93, '48 8d 4b 48 48 81 c2 70 d4 01 00', ' lea rcx,[rbx+0x48]\n add rdx,0x1d470' + COPY),
]
SLOT = 0x40001B58   # a stand-in for the distance while assembling
STUB_CODE = 64      # CoSessionStub.code; a stub's room (CO_STUB_ROOM) also holds the jump back

def assemble(text):
    with tempfile.TemporaryDirectory() as tmp:
        open(tmp + '/c.s', 'w').write('.intel_syntax noprefix\n.set SLOT, %#x\n.text\n%s\n' % (SLOT, text))
        subprocess.run(['as', '--64', '-o', tmp + '/c.o', tmp + '/c.s'], check=True)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', tmp + '/c.o', tmp + '/c.bin'], check=True)
        return open(tmp + '/c.bin', 'rb').read()

# A stub's own jump: stub rva -> (offset of its rel32 in the stub, target rva)
JUMPS = {0x108506A: (2, 0x1085081)}
# Stubs: (rva, the bytes a jump replaces, the stub's code with zeros for the value, offset of the value, old value,
# array, shift). No branch leads into the replaced bytes behind their first (checked against a full disassembly).
STUBS = [
    (0x1085E7E, '41 c7 47 10 01 00 00 00 4d 89 67 18', '41 c7 47 10 01 00 00 00 4d 89 a7 00 00 00 00', 11, 0x18, 'B1', 0),
    # je +0x15; lea rax,[rcx+0x18]: the lea's successor is a loop head, so the jump before it goes into the stub
    (0x108506A, '74 15 48 8d 41 18', '0f 84 00 00 00 00 48 8d 81 00 00 00 00', 9, 0x18, 'B1', 0),
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
for rva, at, value, t in MORE:
    assert struct.unpack_from('<I', dump, rva + at)[0] == value, hex(rva); rows.append((rva, at, value, order.index(t), 0, dump[rva:rva + at]))
for rva, raw in ((0x1086FD0, '4c 39 44 d1 18'), (0x1086FDE, '4e 89 44 c9 18')):
    raw = bytes.fromhex(raw); assert dump[rva:rva + 5] == raw; short.append((rva, raw, bytes([raw[0], raw[1], raw[2] ^ 0xC0, raw[3], 0, 0, 0, 0]), 4, 0x18, 'B1', 0))
for rva, old, code, at, value, t, shift in STUBS:
    old = bytes.fromhex(old); assert dump[rva:rva + len(old)] == old, hex(rva); short.append((rva, old, bytes.fromhex(code), at, value, t, shift))
for rva, raw, at, t in MIRROR:
    raw = bytes.fromhex(raw); value = struct.unpack_from('<I', raw, at)[0]
    base = 0x1D470 if t == 'any' else 0x171C8 if value == 0x1BA0 else 0
    t = 'L2' if t == 'any' else t
    assert dump[rva:rva + len(raw)] == raw and at + 4 == len(raw) and base + value == ARRAYS[t][1], hex(rva)
    rows.append((rva, at, value, order.index(t), 0, raw[:at]))
again = {}
for rva, old, text in COPIES:
    old = bytes.fromhex(old); assert dump[rva:rva + len(old)] == old, hex(rva)
    code = bytearray(assemble(text))
    at = [a for a in range(len(code) - 3) if struct.unpack_from('<I', code, a)[0] == SLOT]
    assert len(at) == 2 and len(code) <= STUB_CODE, hex(rva)
    for a in at: code[a:a + 4] = bytes(4)
    short.append((rva, old, bytes(code), at[0], 0x1B58, 'L2', 0)); again[rva] = at[1]
assert len({r[0] for r in rows} | {c[0] for c in short}) == len(rows) + len(short)
out = ['/* Generated by scripts/gen_session_sites.py from scripts/sites/; do not edit. Game build 25480438. */',
       '#ifndef CO_SESSION_SITES_H', '#define CO_SESSION_SITES_H', '',
       '/* {old offset in the session context, offset in the extension} */', 'static const CoSessionArray kCoSessionArrays[] = {']
out += ['    {0x%Xu, 0x%Xu}, /* %s */' % (ARRAYS[t][1], ARRAYS[t][2], ARRAYS[t][0]) for t in order]
out += ['};', '', '/* {rva, offset of the value in the instruction, its vanilla value, array, the instruction bytes before it} */',
        'static const CoSessionSite kCoSessionSites[] = {']
out += ['    {0x%Xu, %d, 0x%Xu, %d, %d, {%s}},' % (r, a, v, i, sh, ', '.join('0x%02X' % b for b in p)) for r, a, v, i, sh, p in sorted(rows)]
hexes = lambda b: ', '.join('0x%02X' % x for x in b)
out += ['};', '', '/* {rva, bytes replaced, stub length, offset of the value in the stub, a second place for it (0: none), its vanilla',
        ' * value, array, shift, offset of a jump\'s rel32 in the stub (0: none) and its target, the replaced bytes, the stub} */',
        'static const CoSessionStub kCoSessionStubs[] = {']
out += ['    {0x%Xu, %d, %d, %d, %d, 0x%Xu, %d, %d, %d, 0x%Xu, {%s}, {%s}},' % ((r, len(o), len(c), a, again.get(r, 0), v, order.index(t), sh) + JUMPS.get(r, (0, 0)) + (hexes(o), hexes(c))) for r, o, c, a, v, t, sh in sorted(short)]
out += ['};', '', '#endif', '']
open(sys.argv[2] if len(sys.argv) > 2 else 'src/co_session_sites.h', 'w').write('\n'.join(out))
print(len(rows), 'sites;', ', '.join('%s %d' % (t, sum(1 for r in rows if r[3] == i)) for i, t in enumerate(order)))
print(len(short), 'stubs')
