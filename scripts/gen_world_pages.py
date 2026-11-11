#!/usr/bin/env python3
# Writes src/xl/co_page_sites.h: every instruction of the game that reads the world's size from a shared constant, and
# the value page each is pointed at (game build 25480438), from src/xl/proto/world_sites.h and a dump of game.dll:
#   scripts/gen_world_pages.py <game.dll image dump> [out]        writes the header
#   scripts/gen_world_pages.py <game.dll image dump> --check      fails when the header in src/ is not what it would write
#
# Two pages. The generation page follows every level init, a war-table preview's too; the live page follows a mission
# and is the game's own on the ship. A reader belongs to the page of the code it is in, and that is worked out here:
#
#   1. Functions are the heads of the image's exception directory (.pdata; chained entries lead to their head).
#   2. The call graph is the direct one: `call rel32` and `jmp rel32` to a head, and `lea reg, [rip + head]`, each
#      counted as "the function holding the instruction calls the head". Calls through a pointer are not in it.
#   3. From a reader's function the callers are walked upward. The walk stops at the generator: level init 0xF71220,
#      its step 0xF73F10 and 0xF74F40, the pass job 0xF6E420, the passes run on the game thread 0xF26140 and the pass
#      dispatcher 0xC14950. A function nothing calls directly is a root.
#   4. GEN: every chain ends in the generator. PLAY: none does. BOTH: some do.
#      GEN goes to the generation page, PLAY and BOTH to the live page, but for four BOTH functions the study
#      (xl-work co-port/per-lg/notes.md, table row 1) places with the generator; they are named below.
#   5. "Reachable from the game update" is: the walk of 3, which never passes through the generator, reaches the game
#      update 0x4EE6C0. No reader on the generation page may be; the script stops when one is.
#
# What the direct graph cannot say is printed and kept as flags: a PLAY function with no direct caller is called through
# a pointer, its thread is unknown, and it is on the live page by default; a generation-page function may also hang under
# such a root.
import bisect, mmap, os, pickle, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
END = 0x20A0000                      # the end of the code
GENERATOR = {0xC14950, 0xF71220, 0xF73F10, 0xF6E420, 0xF26140, 0xF74F40}
UPDATE = 0x4EE6C0
# BOTH functions go to the live page: there play code on the ship reads the game's own values whatever a preview does,
# and a preview's pass that calls such a helper reads them too (the design accepts a preview that differs in detail).
# Four go with the generator, by the study: their other roots are generator functions that nothing references.
GEN_BY_STUDY = {0xF4AE30, 0xF548D0, 0xF56AC0, 0xFCA400}
CONSTANTS = [(0x23C7D58, 1024.0), (0x23C7DD4, 2048.0), (0x23C65A4, 1.0 / 2048.0), (0x23C83C0, -1024.0), (0x23C83D0, -2048.0)]
WHICH = {'WORLD_HALF': 0, 'WORLD_FULL': 1, 'WORLD_INV': 2, 'WORLD_NEG_HALF': 3, 'WORLD_NEG_FULL': 4}
# One reader each: {rva, length, the constant, its value, the page's field, the page}. The displacement is the last 4 bytes.
SINGLE = [
    (0x953642, 8, 0x23C7DD0, 2000.0, 'GATE_HIGH', 'LIVE'), (0x953653, 8, 0x23C83CC, -2000.0, 'GATE_LOW', 'LIVE'),
    (0xA87BB2, 8, 0x23C83B8, -999.0, 'DROP_LOW', 'LIVE'), (0xA87BBD, 8, 0x23C7D38, 999.0, 'DROP_HIGH', 'LIVE'),
    (0x8275EE, 8, 0x23C83B8, -999.0, 'DROP_LOW', 'LIVE'), (0x8275FF, 8, 0x23C7D38, 999.0, 'DROP_HIGH', 'LIVE'),
    (0x6AA0CA, 9, 0x23C7E04, 4096.0, 'MASK_SPAN', 'LIVE'),
    (0x7ED57B, 8, 0x23C7E04, 4096.0, 'GOAL_HIGH', 'LIVE'), (0x7ED58C, 8, 0x23C83DC, -4096.0, 'GOAL_LOW', 'LIVE'),
    (0x5A01AF, 8, 0x23C7DC8, 1900.0, 'MOTION_HIGH', 'LIVE'), (0x5A0199, 8, 0x23C83C8, -1900.0, 'MOTION_LOW', 'LIVE'),
    (0xF5CEE0, 9, 0x23C7B88, 512.0, 'QUARTER', 'GEN'), (0xF42D1E, 8, 0x23C6538, 2.3283064365386963e-10, 'CLEARING', 'GEN'),
]
# The immediates the generator's code holds: {rva, where the value is, the value}. 0xF6E9D0 is in the generator's
# constructor, which runs once; the stamp index it sets up is rebuilt per level.
IMMEDIATES = [(0xF38EDA, 4, 2048.0), (0xF38EFE, 4, 2048.0), (0xF5DEF9, 4, 2048.0), (0xF5DF06, 4, 2048.0), (0xF5F3C1, 4, 2048.0),
              (0xF5F3CE, 4, 2048.0), (0xF6E9D0, 2, 2048.0), (0xF7DFEF, 3, 1024.0), (0xF7DFF6, 3, 1024.0), (0xF7EDDC, 3, 1024.0),
              (0xF7EDE3, 3, 1024.0), (0xF6CD2E, 6, 2046.0), (0xF6CD3C, 7, 2046.0)]
ONCE = {0xF6E710}

dump_path = sys.argv[1]
check = len(sys.argv) > 2 and sys.argv[2] == '--check'
out = HERE + '/../src/xl/co_page_sites.h' if check or len(sys.argv) < 3 else sys.argv[2]
f = open(dump_path, 'rb')
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

pe = struct.unpack_from('<I', m, 0x3C)[0]
rva, size = struct.unpack_from('<II', m, pe + 24 + 112 + 3 * 8)
entries = sorted(e for e in (struct.unpack_from('<III', m, rva + i * 12) for i in range(size // 12)) if e[0])
starts = [e[0] for e in entries]
chained = lambda unwind: (m[unwind] >> 3) & 4
heads = {e[0] for e in entries if not chained(e[2])}


def function(at):
    i = bisect.bisect_right(starts, at) - 1
    if i < 0 or at >= entries[i][1]: return None
    start, _, unwind = entries[i]
    while chained(unwind):
        start, _, unwind = struct.unpack_from('<III', m, unwind + 4 + ((m[unwind + 2] + 1) & ~1) * 2)
    return start


def graph():
    cache = HERE + '/../build/world_pages_graph.pkl'
    stamp = (os.path.getsize(dump_path), int(os.path.getmtime(dump_path)))
    if os.path.exists(cache):
        saved = pickle.load(open(cache, 'rb'))
        if saved[0] == stamp: return saved[1]
    callers = {}
    for op in (0xE8, 0xE9):
        i = m.find(bytes([op]), 0x1000, END)
        while i != -1:
            target = i + 5 + struct.unpack_from('<i', m, i + 1)[0]
            if target in heads:
                holder = function(i)
                if holder is not None and holder != target: callers.setdefault(target, set()).add(holder)
            i = m.find(bytes([op]), i + 1, END)
    i = m.find(b'\x8d', 0x1000, END)
    while i != -1:
        if (m[i + 1] & 0xC7) == 0x05 and (m[i - 1] & 0xF0) == 0x40:
            target = i + 6 + struct.unpack_from('<i', m, i + 2)[0]
            if target in heads:
                holder = function(i)
                if holder is not None and holder != target: callers.setdefault(target, set()).add(holder)
        i = m.find(b'\x8d', i + 1, END)
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    pickle.dump((stamp, callers), open(cache, 'wb'))
    return callers


callers = graph()


def walk(start):
    """(some chain ends in the generator, the roots outside it) of one function; the whole graph above it, no limit."""
    seen, todo, generator, roots = {start}, [start], False, set()
    while todo:
        x = todo.pop()
        if x in GENERATOR: generator = True; continue
        above = callers.get(x)
        if not above: roots.add(x); continue
        for c in above:
            if c not in seen: seen.add(c); todo.append(c)
    return generator, roots


classes = {}


def classify(at):
    start = function(at)
    if start is None: return None, 'PLAY', {at}      # no function record: nothing leads to it
    if start not in classes:
        generator, roots = walk(start)
        classes[start] = ('GEN' if generator and not roots else 'BOTH' if generator else 'PLAY', roots)
    return (start,) + classes[start]


for constant, value in CONSTANTS + [(s[2], s[3]) for s in SINGLE]:
    assert struct.unpack_from('<f', m, constant)[0] == struct.unpack('<f', struct.pack('<f', value))[0], hex(constant)

text = open(HERE + '/../src/xl/proto/world_sites.h').read()
listed = [(int(a, 16), int(n), WHICH[w]) for a, n, w in re.findall(r'\{0x([0-9A-Fa-f]+)u,\s*(\d+),\s*(WORLD_\w+)\}', text)]
assert len(listed) == 215 and len({s[0] for s in listed}) == 215, len(listed)

F_POINTER, F_OTHER_ROOTS, F_SHARED = 1, 2, 4
rows, by_function = [], {}
for at, length, which in sorted(listed):
    assert at + length + struct.unpack_from('<i', m, at + length - 4)[0] == CONSTANTS[which][0], hex(at)
    start, cls, roots = classify(at)
    flags = 0
    if cls == 'GEN': page = 'GEN'
    elif cls == 'PLAY':
        page = 'LIVE'
        if start is None or roots == {start}: flags |= F_POINTER
    else:
        page = 'GEN' if start in GEN_BY_STUDY else 'LIVE'
        flags |= F_OTHER_ROOTS if page == 'GEN' else F_SHARED
    if page == 'GEN': assert UPDATE not in roots, 'a generation page reader under the game update: 0x%X in 0x%X' % (at, start)
    rows.append((at, length, which, page, flags, start or 0))
    by_function.setdefault(start if start is not None else at, (cls, page, flags, roots, []))[4].append(at)
assert all(by_function[s][0] == 'BOTH' for s in GEN_BY_STUDY), 'the study places a function the dump does not class as both'

singles = []
for at, length, constant, value, field, page in SINGLE:
    assert at + length + struct.unpack_from('<i', m, at + length - 4)[0] == constant, hex(at)
    start, cls, roots = classify(at)
    assert (cls == 'GEN') == (page == 'GEN'), hex(at)
    singles.append((at, length, field, page))
immediates = []
for at, where, value in IMMEDIATES:
    assert struct.unpack_from('<f', m, at + where)[0] == value, hex(at)
    start, cls, roots = classify(at)
    assert cls == 'GEN' or start in ONCE, hex(at)
    immediates.append((at, where, struct.unpack_from('<I', m, at + where)[0]))

hexes = lambda at, n: ', '.join('0x%02X' % b for b in m[at:at + n])
count = lambda page: sum(1 for r in rows if r[3] == page)
pointer = sorted(s for s, v in by_function.items() if v[2] & F_POINTER)
h = ['/* Generated by scripts/gen_world_pages.py from src/xl/proto/world_sites.h and the game\'s code; do not edit. Game build\n'
     ' * 25480438. The script says how each reader\'s page is worked out. */\n',
     '#ifndef CO_PAGE_SITES_H\n#define CO_PAGE_SITES_H\n\n',
     '#define CO_PAGE_GEN 0u  /* follows every level init */\n#define CO_PAGE_LIVE 1u /* follows a mission; the game\'s own on the ship */\n',
     '#define CO_SITE_POINTER 1u     /* live by default: nothing calls its function directly, so its thread is not known */\n',
     '#define CO_SITE_OTHER_ROOTS 2u /* generation page; its function also hangs under functions called through a pointer */\n',
     '#define CO_SITE_SHARED 4u      /* live page; the generator reaches its function too, through helpers all of play uses */\n\n',
     '#define CO_WORLD_READERS %du\n#define CO_WORLD_READERS_GEN %du\n#define CO_WORLD_READERS_LIVE %du\n#define CO_WORLD_POINTER_FUNCTIONS %du\n\n'
     % (len(rows), count('GEN'), count('LIVE'), len(pointer)),
     '/* The constants the game holds: half the world, the world, 1 / world, and the first two negated. */\n',
     'static const uint32_t kCoWorldConstants[5] = {%s};\n\n' % ', '.join('0x%Xu' % c[0] for c in CONSTANTS),
     '/* {rva, length, which constant, page, flags, its function, the game\'s bytes}: the displacement is the last 4 bytes */\n',
     'static const CoWorldReader kCoWorldReaders[CO_WORLD_READERS] = {\n']
for at, length, which, page, flags, start in rows:
    h.append('    {0x%Xu, %d, %d, CO_PAGE_%s, %d, 0x%Xu, {%s}},\n' % (at, length, which, page, flags, start, hexes(at, length)))
h.append('};\n\n/* One reader each: {rva, length, the page\'s field, page, the game\'s bytes} */\n')
h.append('static const CoPageReader kCoPageReaders[%d] = {\n' % len(singles))
for at, length, field, page in singles:
    h.append('    {0x%Xu, %d, CO_FIELD_%s, CO_PAGE_%s, {%s}},\n' % (at, length, field, page, hexes(at, length)))
h.append('};\n\n/* The generator\'s immediates: {rva, where the value is, the game\'s value, the game\'s bytes up to its end} */\n')
h.append('static const CoPageImmediate kCoPageImmediates[%d] = {\n' % len(immediates))
for at, where, value in immediates:
    h.append('    {0x%Xu, %d, 0x%08Xu, {%s}},\n' % (at, where, value, hexes(at, where + 4)))
h.append('};\n\n#endif\n')
header = ''.join(h)

print('%d readers: %d on the generation page, %d on the live page' % (len(rows), count('GEN'), count('LIVE')))
print('classes: ' + ', '.join('%s %d' % (c, sum(len(v[4]) for v in by_function.values() if v[0] == c)) for c in ('GEN', 'PLAY', 'BOTH')))
print('%d functions with live readers and no direct caller (live by default): %s' % (len(pointer), ' '.join('%X' % s for s in pointer)))
print('%d live page readers are in functions under the game update' % sum(len(v[4]) for v in by_function.values() if v[1] == 'LIVE' and UPDATE in v[3]))
for s, (cls, page, flags, roots, sites) in sorted(by_function.items()):
    if cls == 'BOTH':
        named = sorted(roots)[:8]
        print('both kinds: %X on the %s page, %d readers, %d other roots: %s%s' % (s, 'generation' if page == 'GEN' else 'live', len(sites),
              len(roots), ' '.join('%X' % r for r in named), ' ...' if len(roots) > 8 else ''))
        if page == 'GEN':
            for r in sorted(roots):
                if r in by_function: print('  its root %X holds a live page reader itself (%s)' % (r, ' '.join('%X' % a for a in by_function[r][4])))
if check:
    if open(out).read() != header: sys.exit('src/xl/co_page_sites.h is not what this dump gives')
    print('the header in src/ matches')
else:
    open(out, 'w').write(header)
