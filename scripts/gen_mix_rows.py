#!/usr/bin/env python3
# Writes src/xl/co_mix_rows.h: the mission rows HellForge's level 11 mixes a partner mission into (game build 25480438):
#   scripts/gen_mix_rows.py <game.dll image dump> <HellForge native/mod-loader> [out]
# HellForge's MixInstall is followed on the dump's tables, after the widening steps its install runs before it. The mod
# works the rows out again from the game's memory (co_mix.c) and refuses when they are not these.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
g = bytearray(open(sys.argv[1], 'rb').read())
stock = sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else HERE + '/../src/xl/co_mix_rows.h'
difficulty = open(os.path.join(stock, 'difficulty.c')).read()
missions = open(os.path.join(stock, 'difficulty_missions.c')).read()
install = open(os.path.join(stock, 'difficulty_install.c')).read()


def define(text, name, suffix='u'): return int(re.search(r'#define ' + name + r' (0x[0-9A-Fa-f]+|\d+)' + suffix, text).group(1), 0)


M, MN, MR = define(difficulty, 'DIF_MISSIONS'), define(difficulty, 'DIF_MISSION_ROWS'), define(difficulty, 'DIF_MISSION_ROW')
O, ON, OR = define(difficulty, 'DIF_OPERATIONS'), define(difficulty, 'DIF_OPERATION_ROWS'), define(difficulty, 'DIF_OPERATION_ROW')
J, JN, JR = define(difficulty, 'DIF_OBJECTIVES'), define(difficulty, 'DIF_OBJECTIVE_ROWS'), define(difficulty, 'DIF_OBJECTIVE_ROW')
ERADICATE = define(difficulty, 'DIF_ERADICATE_DELTA', 'ull')
PARTNERS, LISTED = define(missions, 'DIF_MIX_PARTNERS'), define(missions, 'DIF_OPERATION_MISSIONS')
RADAR = define(missions, 'DIF_RADAR')
assert (M, MN, MR, O, ON, OR, J, JN, JR, PARTNERS, LISTED) == (0x3773420, 162, 0x380, 0x32FEF10, 25, 0x490, 0x32EF870, 153, 0xA0, 16, 32)

# The source this follows: the steps' order and the lines of the rules. A change to one stops the script.
order = re.search(r'kInstallSteps\[\]\)\(DifficultyInstall\* d\) = \{(.*?)\};', install, re.S).group(1)
steps = [s.strip() for s in re.sub(r'/\*.*?\*/', '', order, flags=re.S).split(',')]
assert steps.index('WidenObjectives') < steps.index('WidenMissionRanges') < steps.index('MixStep') < steps.index('InstallRadar')
for line in (
        'rows->standard[r] = kind == 1 && delta == 0 && enabled && range[0] <= DIF_VANILLA + 1 && DIF_VANILLA + 1 <= range[1];',
        'rows->eradicate[r] = delta == DIF_ERADICATE_DELTA && enabled && range[0] <= DIF_VANILLA && DIF_VANILLA <= range[1] &&',
        'if (id == 0 || !(weight > 0.0f) || cls > 1) return 0;',
        'if (in >= 0) return 1;',
        'if (def && !SafeRead(def + 0x24, levels, 8) && levels[0] <= DIF_VANILLA + 1 && DIF_VANILLA + 1 <= levels[1]) (*mains)++;',
        'return in >= 0 && in != 1;',
        'if (r == b || !(rows->standard[r] || rows->eradicate[r]) || rows->faction[r] != rows->faction[b]) return 0;',
        'return !conflict && mains > 0;',
        'if (!rows->standard[b]) return;',
        'for (uint32_t r = 0; r < DIF_MISSION_ROWS && g_mixPartnerCount[b] < DIF_MIX_PARTNERS; r++)',
        'g_mixBase[b] = g_mixPartnerCount[b] > 0;',
        'uint32_t entry[6] = {id, 0, 0, 1, 0, 0}; /* weight 0.0: never drawn */',
        'uint32_t entry[6] = {DIF_RADAR, 0, 0, 3, 0, 0xFFFFFFFFu};',
        'if (g_difficulty.flags & HF_DIFFICULTY_SECOND_PRIMARY) MixInstall(d->game);',
        'for (uint32_t d = 1; d < DIF_OBJECTIVE_ROWS; d++)'):
    assert line in missions, line
for line in ('if (!SafeRead(at, &highest, 4) && highest == DIF_VANILLA && ApiWrite(at, &level, 4))',
             'if (!SafeRead(at, &highest, 4) && highest > DIF_VANILLA && ApiWrite(at, &ten, 4))'):
    assert line in difficulty, line
assert 'memcpy(made + 0xC, &level, 4); memcpy(made + 0x10, &level, 4);' in missions  # the city ICBM rows: levels 11..11


def u32(a): return struct.unpack_from('<I', g, a)[0]
def entry(r, k): return struct.unpack_from('<IfIIII', g, M + r * MR + 0x40 + k * 0x18)


# What the mod tells HellForge's later additions by must not be in the game's own table: a step of weight 0, the radar
# station's entry, a row offered from level 11 on.
for r in range(MN):
    assert u32(M + r * MR + 0xC) <= 10, r
    for k in range(32):
        e = entry(r, k)
        assert not (e[0] != 0 and e[1] == 0.0 and e[3] == 1), (r, k)
        assert not (e[0] == RADAR and e[1] == 0.0 and e[3] == 3 and e[5] == 0xFFFFFFFF), (r, k)


def bases(no_eradicate):
    t = bytearray(g)
    def r32(a): return struct.unpack_from('<I', t, a)[0]
    def w32(a, v): struct.pack_into('<I', t, a, v)
    for i in range(JN):  # WidenObjectives
        if r32(J + i * JR + 0x28) == 10: w32(J + i * JR + 0x28, 11)
    for i in range(ON):  # WidenMissionRanges
        if r32(O + i * OR + 0x10) == 10: w32(O + i * OR + 0x10, 11)
    for r in range(MN):
        row = M + r * MR
        if no_eradicate and struct.unpack_from('<Q', t, row + 0x360)[0] == ERADICATE:
            if r32(row + 0x10) > 10: w32(row + 0x10, 10)
        elif r32(row + 0x10) == 10: w32(row + 0x10, 11)
    ids, faction, standard, eradicate, entries = [], [], [], [], []
    for r in range(MN):
        row = M + r * MR
        low, high, delta = r32(row + 0xC), r32(row + 0x10), struct.unpack_from('<Q', t, row + 0x360)[0]
        ids.append(r32(row)); faction.append(t[row + 8])
        standard.append(t[row + 0x34] == 1 and delta == 0 and t[row + 4] != 0 and low <= 11 <= high)
        eradicate.append(delta == ERADICATE and t[row + 4] != 0 and low <= 10 <= high and not no_eradicate)
        entries.append([struct.unpack_from('<IfIIII', t, row + 0x40 + k * 0x18) for k in range(32)])
    listed = [[False] * MN for _ in range(ON)]
    for o in range(ON):
        for k in range(LISTED):
            i = r32(O + o * OR + 0x38 + 8 * k)
            for r in range(MN):
                if i != 0 and ids[r] == i: listed[o][r] = True

    def levels(i):
        for d in range(1, JN):
            if r32(J + d * JR) == i: return r32(J + d * JR + 0x24), r32(J + d * JR + 0x28)
        return None

    def cls(b, i): return next((e[3] for e in entries[b] if e[0] == i), -1)

    def fits(b, r):
        if r == b or not (standard[r] or eradicate[r]) or faction[r] != faction[b]: return False
        if not any(listed[o][b] and listed[o][r] for o in range(ON)): return False
        mains = 0
        for e in entries[r]:
            if e[0] == 0 or not e[1] > 0.0 or e[3] > 1: continue
            there = cls(b, e[0])
            if e[3] == 0:
                if there >= 0: return False
                lv = levels(e[0])
                if lv and lv[0] <= 11 <= lv[1]: mains += 1
            elif there >= 0 and there != 1: return False
        return mains > 0
    partners = [[r for r in range(MN) if fits(b, r)][:PARTNERS] if standard[b] else [] for b in range(MN)]
    registered = 0  # RegisterPartnerSteps: what tells a HellForge that mixes from one that does not
    for b in range(MN):
        for r in partners[b]:
            for e in entries[r]:
                if e[0] == 0 or not e[1] > 0.0 or e[3] != 1 or any(x[0] == e[0] for x in entries[b]): continue
                free = next((k for k, x in enumerate(entries[b]) if x == (0, 0.0, 0, 0, 0, 0)), -1)
                if free >= 0: entries[b][free] = (e[0], 0.0, 0, 1, 0, 0); registered += 1
    return [1 if p else 0 for p in partners], registered


with_eradicate, n1 = bases(False)
without, n2 = bases(True)
# a row is a base with or without Eradicate partners, and a HellForge that mixes always leaves steps behind
assert with_eradicate == without and n1 > 0 and n2 > 0, (n1, n2)
rows = [i for i, b in enumerate(without) if b]
o = ['/* Generated by scripts/gen_mix_rows.py from HellForge\'s source and the game\'s image; do not edit. Game build 25480438. */\n',
     '#ifndef CO_MIX_ROWS_H\n#define CO_MIX_ROWS_H\n\n#include <stdint.h>\n\n',
     '/* The %d mission rows HellForge\'s level 11 mixes a partner into, of %d. */\n' % (len(rows), MN),
     '#define CO_MIX_BASES %du\n' % len(rows),
     'static const uint8_t kCoMixBases[CO_MIX_BASES] = {\n    ', ', '.join(str(r) for r in rows), '};\n\n#endif\n']
open(out, 'w').write(''.join(o))
