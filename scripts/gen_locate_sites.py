#!/usr/bin/env python3
# Writes src/xl/co_locate_sites.h: the one instruction that addresses the level's location array where the game has it
# and that HellForge's move of the generation block does not list (game build 25480438):
#   scripts/gen_locate_sites.py <game.dll image dump> <HellForge native/mod-loader> [out]
# The leaf and its callers are checked on the dump. Every array HellForge moves is then searched for in the code: an
# instruction with a displacement or an immediate in an array's first element that is in none of HellForge's tables
# must be this one. Needs objdump.
import glob, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
game = open(sys.argv[1], 'rb').read()
stock = sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else HERE + '/../src/xl/co_locate_sites.h'
CODE_END = 0x20A0000


def hexes(b): return ', '.join('0x%02X' % x for x in b)


# The leaf 0xF73E50 (rcx the level generator, rdx a unit): the index of the location whose unit [+0xA8] is rdx, or -1.
LEAF, SITE, BACK = 0xF73E50, 0xF73E79, 0xF73E80
leaf = bytes.fromhex(
    '4C8B91C8020000'      # mov r10, [rcx+0x2C8]       the generation block G
    '33C0'                # xor eax, eax
    '458B8A08711A01'      # mov r9d, [r10+0x11A7108]   the locations' count
    '4585C9' '7430'       # test r9d, r9d; je -1
    '6666660F1F840000000000'
    '8BC8'                # mov ecx, eax
    '4C69C1E0000000'      # imul r8, rcx, 0xE0
    '498D8A208E7500'      # lea rcx, [r10+0x758E20]    the site: array A where the game has it
    '4903C8' '7409'       # add rcx, r8; je next
    '483991A8000000'      # cmp [rcx+0xA8], rdx
    '740C'                # je found
    'FFC0' '413BC1' '72DB'
    'B8FFFFFFFF' 'C3')
assert game[LEAF:LEAF + len(leaf)] == leaf and SITE - LEAF == 0x29 and leaf[0x29:0x30] == bytes.fromhex('498D8A208E7500')
site = game[SITE:BACK]
old = struct.unpack_from('<I', site, 3)[0]
# its callers: the tail of 0x9D1FF0 (which nothing in the image refers to) and the loot component's set-up 0x9D2B70
callers = []
for op in (0xE8, 0xE9):
    i = game.find(bytes([op]), 0x1000, CODE_END)
    while i != -1:
        if i + 5 + struct.unpack_from('<i', game, i + 1)[0] == LEAF: callers.append((op, i))
        i = game.find(bytes([op]), i + 1, CODE_END)
assert sorted(callers) == [(0xE8, 0x9D2CFE), (0xE9, 0x9D2013)], callers
# the set-up stores the answer in the component's record: mov ebp, eax; ...; mov [rax+r12*4+4], ebp
assert game[0x9D2D03:0x9D2D05] == bytes.fromhex('8BE8') and game[0x9D2D09:0x9D2D0E] == bytes.fromhex('42896CA004')

# HellForge's side: where A is in the game, and every site its tables move.
block = open(os.path.join(stock, 'difficulty_block.c')).read()


def define(name): return int(re.search(r'#define ' + name + r' (0x[0-9A-Fa-f]+)u', block).group(1), 16)


assert define('DIF_LOCATIONS_A') == old == 0x758E20
covered = set()
for path in glob.glob(os.path.join(stock, '*.h')) + glob.glob(os.path.join(stock, '*.c')):
    text = open(path).read()
    for rva, at in re.findall(r'\{0x([0-9A-Fa-f]+)u, *(\d+), *0x[0-9A-Fa-f]+u\}', text): covered.add(int(rva, 16) + int(at))
    for rva, at in re.findall(r'\{0x([0-9A-Fa-f]+)u, *\d+, *(\d+), *\d+, *0x', text): covered.add(int(rva, 16) + int(at))
theirs = SITE + 3 in covered
covered.discard(SITE + 3)
# The arrays its move takes away, as the game has them: {offset in G, an element's size}.
extras = re.search(r'kGenerationExtras\[[^\]]*\] = \{(.*?)\};', block, re.S).group(1)
regions = {'A': (old, 0xE0), 'B': (define('DIF_LOCATIONS_B'), 0x40), 'types': (define('DIF_LOCATION_TYPES_C'), 0x10),
           'minimap P': (define('DIF_MINIMAP_P'), 0x30), 'minimap S': (define('DIF_MINIMAP_S'), 0x58), 'minimap U': (define('DIF_MINIMAP_U'), 4),
           'minimap V': (define('DIF_MINIMAP_V'), 4), 'stamps': (define('DIF_STAMPS_OLD'), 0x10), 'objective list': (define('DIF_OBJLIST_OLD'), 0x100),
           'places': (0x8ABCC0, 8), 'place set': (0x8C4A38, 0x10), 'clusters': (0x896FB0, 0xE0)}
for name, value in (('places', 'G\\+0x8ABCC0: 16 pointers'), ('place set', 'G\\+0x8C4A38: 64 x 0x10'), ('clusters', 'G\\+0x896FB0, 8 x 0xE0')):
    assert re.search(value, block), name
rows = re.findall(r'\{(GEN_\w+), GEN_FIELD_\w+, (0x[0-9A-F]+)u, (0x[0-9A-F]+|\d+)u,', extras)
assert len(rows) == 6
for name, at, element in rows: regions[name] = (int(at, 16), int(element, 0))


def decodes(at, value, negative):
    """The instruction that holds `value` at `at` as a displacement or an immediate, or None. The longest reading whose
    first instruction spans the four bytes decides: a shorter one is that instruction without its prefix."""
    want = re.compile(r'(?<![0-9a-fx])%s0x%x(?![0-9a-f])' % ('-' if negative else '', value))
    with tempfile.TemporaryDirectory() as tmp:
        for back in range(7, 0, -1):
            path = os.path.join(tmp, 'i')
            open(path, 'wb').write(game[at - back:at + 12])
            text = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel', path], capture_output=True, text=True, check=True).stdout
            starts = [(int(m.group(1), 16), m.group(2)) for m in re.finditer(r'^\s+([0-9a-f]+):\t[0-9a-f ]+\t(.*)$', text, re.M)]
            if len(starts) < 2 or starts[0][0] != 0 or starts[1][0] < back + 4 or '(bad)' in starts[0][1]: continue
            return starts[0][1].strip() if 'rip' not in starts[0][1] and want.search(starts[0][1]) else None
    return None


found = []
for name, (at, element) in sorted(regions.items()):
    for negative in (0, 1):
        values = [(-(at + k)) & 0xFFFFFFFF if negative else at + k for k in range(element)]
        for high in sorted(set(v >> 8 for v in values)):
            low = set(v & 0xFF for v in values if v >> 8 == high)
            i = game.find(high.to_bytes(3, 'little'), 0x1001, CODE_END)
            while i != -1:
                if game[i - 1] in low and i - 1 not in covered:
                    value = struct.unpack_from('<I', game, i - 1)[0]
                    text = decodes(i - 1, (-value) & 0xFFFFFFFF if negative else value, negative)
                    if text: found.append((i - 1, name, text))
                i = game.find(high.to_bytes(3, 'little'), i + 1, CODE_END)
assert [(at, name) for at, name, text in found] == [(SITE + 3, 'A')], found
if theirs: sys.stderr.write('HellForge lists 0x%X itself now: the mod finds the site moved and leaves it.\n' % SITE)

o = ['/* Generated by scripts/gen_locate_sites.py from the game\'s image and HellForge\'s source; do not edit. Game build 25480438. */\n',
     '#ifndef CO_LOCATE_SITES_H\n#define CO_LOCATE_SITES_H\n\n#include <stdint.h>\n\n',
     '/* lea rcx, [r10+0x758E20] in the leaf 0xF73E50 (r10 the generation block): the location array where the game has\n',
     ' * it. The displacement is at +3; the leaf goes on at CO_LOCATE_BACK (add rcx, r8). */\n',
     '#define CO_LOCATE_SITE 0x%Xu\n#define CO_LOCATE_BACK 0x%Xu\n#define CO_LOCATE_GAME_AT 0x%Xu\n' % (SITE, BACK, old),
     'static const uint8_t kCoLocateGame[%d] = {%s};\n\n#endif\n' % (len(site), hexes(site))]
open(out, 'w').write(''.join(o))
