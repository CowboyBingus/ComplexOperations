#!/usr/bin/env python3
# Writes src/xl/co_status_sites.h: the status-effect volumes' unit registry, every use of its inline list of 4096 units
# in game.dll (game build 25480438):
#   scripts/gen_status_sites.py <game.dll image dump> <HellForge native/mod-loader> [out]
# The list is M + 0x129098 (u32 id, u32 setting a unit) with its count at M + 0x131098, M = [game + 0x347CF20]. The
# script disassembles the whole code section and requires that the instructions with one of those displacements are
# exactly the ones the seven sites below cover or the count's own, and that HellForge's source names no address in a site.
import os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
dump = sys.argv[1]
game = open(dump, 'rb').read()
loader = sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else HERE + '/../src/xl/co_status_sites.h'

MANAGER, WORLD_AT = 0x347CF20, 0x5FB24C0
INLINE, POINTER, PLACES, EMPTY, FACTOR, LIST, COUNT = 0x121080, 0x129080, 0x129088, 0x12908C, 0x129090, 0x129098, 0x131098
OWN = (COUNT - LIST) // 8
assert OWN == 4096 and POINTER - INLINE == 4096 * 8
pe = struct.unpack_from('<I', game, 0x3C)[0]
base = struct.unpack_from('<Q', game, pe + 0x30)[0]
sections = pe + 24 + struct.unpack_from('<H', game, pe + 20)[0]
code_end = sum(struct.unpack_from('<II', game, sections + 8))
assert struct.unpack_from('<I', game, sections + 12)[0] == 0x1000 and code_end == 0x2110A93


def hexes(b): return ', '.join('0x%02X' % x for x in b)


# the manager is a part of the world object (0x13F2CEF: lea rax, [rcx+0x5FB24C0]; mov [0x347CF20], rax), set up with
# its hash inline and 0x1000 places (0x13F4BB0) and the hash's factor 2
assert game[0x13F2CEF:0x13F2CFD] == bytes.fromhex('488D81C024FB05') + bytes.fromhex('48890523A20802')
assert 0x13F2CFD + struct.unpack_from('<i', game, 0x13F2CF9)[0] == MANAGER
assert game[0x13F4BB0:0x13F4BD0] == bytes.fromhex('488D8180101200' '48898180901200' '488D8120801100' '48C7818890120000100000')
assert game[0x13F4B99:0x13F4BA3] == bytes.fromhex('C7819090120002000000')

# site, length, the way back (0: the stub ends the span itself), what it is
sites = [
    (0xACF8F0, 11, 0xACF8FB, 'the count read; lea eax, [r15+1]', '458BB89810130041 8D4701'),
    (0xACF907, 16, 0xACF917, 'the append', '438984F89C901200 43899CF898901200'),
    (0xACF966, 6, 0xACF96C, "the insert's slot write", '418918488BCF'),
    (0x69C05A, 8, 0x69C062, "the hit path's read", '418B8CC19C901200'),
    (0x13CC56F, 18, 0x13CC581, "the removal's move", '4C8B94C698901200 33D2 4C8994FE98901200'),
    (0x13CC3F6, 51, 0x13CC429, 'the append of 0x13CC3E0, to behind its hash insert',
     '448B8298101300 4A8D0CC2 418D4001 898298101300 418B411C 89819C901200 899998901200 488D8A80901200 8BD3 E867333600'),
    (0x13CC03F, 8, 0x13CC047, 'the read of 0x13CBFB0', '418B84C39C901200')]
for rva, length, back, what, pin in sites:
    pin = bytes.fromhex(pin)
    assert len(pin) == length and game[rva:rva + length] == pin and back == rva + length, hex(rva)
# where the stubs lead besides: the unit loop's next item, the removal's and the two functions' ways on
SKIP, APPEND_SKIP = 0xACFA48, 0x13CC4C2
assert game[SKIP:SKIP + 4] == bytes.fromhex('4983C410')                              # add r12, 0x10
assert game[APPEND_SKIP:APPEND_SKIP + 11] == bytes.fromhex('488B5C24304883C4205FC3')   # mov rbx, [rsp+0x30]; add rsp, 0x20; pop rdi; ret
assert game[0x13CC3E0:0x13CC3F6] == bytes.fromhex('48895C2408 57 4883EC20 8BDA 4D8BC8 488B152A0B0B02')  # rsp is aligned, r9 the unit, rdx M
assert 0x13CC3F6 + struct.unpack_from('<i', game, 0x13CC3F2)[0] == MANAGER
assert 0x13CC429 + struct.unpack_from('<i', game, 0x13CC425)[0] == 0x172F790  # the call is the hash insert (rcx hash, edx key, r8d value)
assert game[0x13CC047:0x13CC049] == bytes.fromhex('EBD9')                             # jmp 0x13CC022
assert game[0xACF8FB:0xACF902] == bytes.fromhex('41898098101300') and game[0xACF96C:0xACF970] == bytes.fromhex('45897804')

# Every use in the image. The linear disassembly of the code section, memory operands only.
listing = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel', '--start-address=0x1000',
                          '--stop-address=%#x' % code_end, dump], capture_output=True, text=True, check=True).stdout
found = {}
operand = re.compile(r'\[([^\]]*)\+0x([0-9a-f]+)\]')
for line in listing.splitlines():
    parts = line.split('\t')
    if len(parts) < 3 or 'rip' in parts[2]: continue
    m = operand.search(parts[2].split('#')[0])
    if m is None: continue
    disp = int(m.group(2), 16)
    # the manager's own displacements, and the same places off the world object it is a part of
    if LIST <= disp <= COUNT or WORLD_AT + LIST <= disp <= WORLD_AT + COUNT:
        found[int(parts[0].strip().rstrip(':'), 16)] = disp
covered = {0xACF907: LIST + 4, 0xACF90F: LIST, 0x69C05A: LIST + 4, 0x13CC56F: LIST, 0x13CC579: LIST,
           0x13CC03F: LIST + 4, 0x13CC40F: LIST + 4, 0x13CC415: LIST,
           0xACF8F0: COUNT, 0x13CC3F6: COUNT, 0x13CC405: COUNT}
# the count alone, which stays the game's: its store behind the first site, the reset (0x13CBC80), the removal's two
count_only = {0xACF8FB: COUNT, 0x13CBC9B: COUNT, 0x13CC55C: COUNT, 0x13CC563: COUNT}
# other arrays of other objects that happen to lie in the range: none is reached from the manager's pointer
others = {a: d for a, d in found.items() if a not in covered and a not in count_only}
assert all(d not in (LIST, LIST + 4, COUNT) for d in others.values()), others
assert {a: d for a, d in found.items() if a in covered} == covered and {a: d for a, d in found.items() if a in count_only} == count_only
for a in covered: assert any(rva <= a < rva + length for rva, length, _, _, _ in sites), hex(a)
# a displacement in the list's span belongs to another object when no code of the manager's holds it: the manager's
# code is what is called with it (the world's lea [r+0x5FB24C0]: 0x13CBC80, 0x13CC050, and 0x13CC5F0 from those) or
# loads [0x347CF20] (29 loads; none of the other instructions lies in the 0x1000 bytes behind one)
loads = [int(l.split(':')[0], 16) for l in listing.splitlines() if l.rstrip().endswith('# %#x' % MANAGER) and '[rip' in l and 'mov    QWORD PTR [rip' not in l]
assert len(loads) == 28 and 0xACF8E0 in loads and 0x69BE9D in loads and 0x13CC3EF in loads and 0x13CBFC4 in loads and 0x13CC4E0 in loads, len(loads)
for a, d in others.items():
    assert not (0x13CBBE0 <= a < 0x13CF700) and not any(load <= a < load + 0x1000 for load in loads), (hex(a), hex(d))
# and the bytes themselves, wherever the disassembly may have lost its step: each dword is one of the above, or the
# tail of a 0x1310 / 0x1290 displacement (modrm 0x98, then 10 13 00 00 / 90 12 00 00)
for value in (LIST, LIST + 4, COUNT):
    at = game.find(struct.pack('<I', value), 0x1000, code_end)
    while at != -1:
        ins = [a for a in list(covered) + list(count_only) if a < at <= a + 4]
        assert ins or (value != LIST + 4 and game[at + 4] == 0 and game[at - 1] in (0x39, 0x3B, 0x89, 0x8B, 0x8D, 0x03, 0x2B, 0x01, 0x29)), hex(at)
        at = game.find(struct.pack('<I', value), at + 1, code_end)
# the list's base is taken once without a displacement (0x13CC3FD: lea rcx, [rdx+r8*8]), inside the sixth site
assert game[0x13CC3FD:0x13CC401] == bytes.fromhex('4A8D0CC2')


# The two functions no call reaches: no call or jump, no rip-relative operand, no pointer in the image leads to them.
def referenced(rva):
    refs = []
    for op in (b'\xE8', b'\xE9'):
        at = game.find(op, 0x1000, code_end)
        while at != -1:
            if at + 5 + struct.unpack_from('<i', game, at + 1)[0] == rva: refs.append(at)
            at = game.find(op, at + 1, code_end)
    for line in listing.splitlines():
        if line.rstrip().endswith('# %#x' % rva): refs.append(int(line.split(':')[0], 16))
    if game.find(struct.pack('<Q', base + rva)) != -1: refs.append(-1)
    return refs


assert referenced(0x13CBFB0) == [] and referenced(0x13CC3E0) == [], 'a function thought unreached has a reference'
assert sorted(referenced(0x13CC4D0)) == [0xACFA33, 0x13CC4AD]  # the removal: the callback both appends register

# HellForge's own steps: none at a site, or the mod would have to stand beside it.
named = set()
for name in sorted(os.listdir(loader)):
    if name.endswith(('.c', '.h')):
        for lit in re.findall(r'0x[0-9A-Fa-f]{5,8}', open(os.path.join(loader, name), errors='replace').read()): named.add(int(lit, 16))
for rva, length, _, _, _ in sites: assert not any(rva <= v < rva + length for v in named), hex(rva)
assert MANAGER not in named

o = ['/* Generated by scripts/gen_status_sites.py from the game\'s image; do not edit. Game build 25480438. */\n',
     '#ifndef CO_STATUS_SITES_H\n#define CO_STATUS_SITES_H\n\n#include <stdint.h>\n\n',
     '/* The registry M = [game + 0x347CF20]: an open hash of unit ids (pointer, places, the empty key, the factor) over\n',
     ' * its inline table, then the list (u32 id, u32 setting a unit) and the list\'s count. */\n',
     '#define CO_STATUS_MANAGER 0x%Xu\n#define CO_STATUS_INLINE 0x%Xu\n#define CO_STATUS_POINTER 0x%Xu\n#define CO_STATUS_PLACES 0x%Xu\n' % (MANAGER, INLINE, POINTER, PLACES),
     '#define CO_STATUS_EMPTY 0x%Xu\n#define CO_STATUS_FACTOR 0x%Xu\n#define CO_STATUS_GAME_LIST 0x%Xu\n#define CO_STATUS_COUNT 0x%Xu\n' % (EMPTY, FACTOR, LIST, COUNT),
     '#define CO_STATUS_OWN %du /* the game\'s places, list and hash alike */\n\n' % OWN,
     '/* Every instruction of the image that uses the list, by site: the bytes, and where the game goes on behind them. */\n',
     'typedef struct CoStatusSite { uint32_t rva; uint8_t length; uint8_t pin[%d]; uint32_t back; } CoStatusSite;\n' % max(s[1] for s in sites),
     '#define CO_STATUS_SITES %d\n' % len(sites),
     'static const CoStatusSite kCoStatusSites[CO_STATUS_SITES] = {\n']
for rva, length, back, what, pin in sites:
    o.append('    {0x%Xu, %d, {%s}, 0x%Xu}, /* %s */\n' % (rva, length, hexes(bytes.fromhex(pin)), back, what))
o.append('};\n\n/* A unit left out: the unit loop\'s next item (0xACF710\'s), and the end of 0x13CC3E0. */\n')
o.append('#define CO_STATUS_SKIP 0x%Xu\n#define CO_STATUS_APPEND_SKIP 0x%Xu\n\n#endif\n' % (SKIP, APPEND_SKIP))
open(out, 'w').write(''.join(o))
