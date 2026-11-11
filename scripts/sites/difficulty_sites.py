#!/usr/bin/env python3
# The HiveMind's difficulty multipliers for five to eight players (game build 25480438): caves scripts/gen_count_sites.py
# reads (group 15, eight only). Static analysis only.
# HiveMindComponentData (0x438 bytes) holds 13 MissionDifficultyModifierSettings blocks with f32[4] by player count at
# +0x13C + 0x3C*k, read as `dec eax; mulss xmmN,[rec+rax*4+disp]` with no upper bound (count from [[0x3326468]+0x84]).
# Each cave keeps counts 1..4 on the game's own instruction; a count n of 5 or more multiplies by
#     v(n) = max(0.1, v4 + (n-4)*(v4-v3)),  v3 = [rec+disp+8], v4 = [rec+disp+12]
# from the record in use, so every faction and difficulty gets its own continuation. maxss gives 0.1 for a NaN too.
# What the caves rely on (the design's verify script checks it on the dump): eax = count-1 at the mulss, kept;
# the flags are written before any read after each mulss; xmm0/xmm1 are saved below a lowered rsp, never below rsp.
#   CAVES: (rva, replaced hex, asm, note)   the jump replaces one mulss
import struct

TABLE = {0x13C + 0x3C * k: k + 1 for k in range(13)}
GPR = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi'] + ['r%d' % i for i in range(8, 16)]
MIN = 0.1

# (rva of the mulss, function, note). Bytes, registers and displacement are decoded from the dump by build().
SITES = [
    (0x7F7B14, 0x7F7820, 'spawner_spawn_cooldown_rate'),
    (0x7F7BEA, 0x7F7820, 'spawner_production_cooldown_rate'),
    (0x7F8347, 0x7F7820, 'spawner_spawn_count'),
    (0x7F8F89, 0x7F8740, 'global_spawner_cooldown'),
    (0x7F9B10, 0x7F9A90, 'spawner_queue_limit (no reference)'),
    (0x7F9C08, 0x7F9B70, 'spawner_queue_limit'),
    (0x7F9D80, 0x7F9D00, 'spawner_spawn_count (no reference)'),
    (0x7F9E60, 0x7F9DE0, 'spawner_spawn_count (no reference)'),
    (0x7F9F50, 0x7F9ED0, 'spawner_spawn_count_minimum (no reference)'),
    (0x7FA03E, 0x7F9FB0, 'spawner_spawn_count_minimum'),
    (0x7FA129, 0x7FA0B0, 'spawner_production_cooldown_rate (no reference)'),
    (0x7FA209, 0x7FA190, 'spawner_spawn_cooldown_rate (no reference)'),
    (0x90BF86, 0x90BE70, 'guard_force_member_count_max'),
    (0x913610, 0x913590, 'guard_force_member_count_max (no reference)'),
    (0x9136F0, 0x913670, 'guard_force_member_count_max (no reference)'),
    (0x94E9AA, 0x94E900, 'travelers_waves_cooldown (eax is read again by 0x94E9B3)'),
    (0x94E9B3, 0x94E900, 'travelers_max_unit_count'),
    (0x9560E1, 0x956040, 'encounter_points'),
    (0x9590A0, 0x959020, 'encounter_points (no reference)'),
    (0x959220, 0x9591A0, 'encounter_points (no reference)'),
    (0x9593E5, 0x959350, 'encounter_cooldown_rate'),
    (0x9595F0, 0x959570, 'patrol_count_max (no reference)'),
    (0x9596D0, 0x959650, 'patrol_count_max'),
    (0x9597D5, 0x959740, 'patrol_spawn_cooldown_rate'),
    (0x95D5A3, 0x95D500, 'global_spawner_cooldown (no reference)'),
    (0x95D691, 0x95D600, 'global_spawner_cooldown (no reference)'),
]


def decode(dump, rva):
    """mulss xmmN, dword [base + rax*4 + disp32] at rva: (length, N, base, disp). Anything else fails."""
    b = dump[rva:rva + 10]; i = 1; rex = 0
    assert b[0] == 0xF3, hex(rva)
    if 0x40 <= b[1] <= 0x4F: rex = b[1]; i = 2
    assert b[i:i + 2] == b'\x0f\x59', hex(rva)
    modrm, sib = b[i + 2], b[i + 3]
    assert modrm >> 6 == 2 and modrm & 7 == 4, hex(rva)                         # [sib + disp32]
    assert sib >> 6 == 2 and ((sib >> 3) & 7) == 0 and not rex & 2, hex(rva)   # index rax, scale 4
    assert not rex & 0xC, hex(rva)                                              # no REX.W, xmm0..7
    n = (modrm >> 3) & 7; base = GPR[(sib & 7) | (8 if rex & 1 else 0)]
    disp = struct.unpack_from('<i', b, i + 4)[0]
    assert disp in TABLE and base not in ('rax', 'rsp') and n not in (0, 1), hex(rva)
    return i + 8, n, base, disp


def cave(dump, rva):
    n, x, base, disp = decode(dump, rva)
    back = rva + n
    asm = '\n'.join([
        'cmp eax, 3',
        'ja 1f',
        'mulss xmm%d, dword ptr [%s+rax*4+0x%x]' % (x, base, disp),    # counts 1..4: the game's own instruction
        'jmp game+0x%X' % back,
        '1: lea rsp, [rsp-0x20]',                                      # lea leaves the flags; nothing below rsp
        'movups xmmword ptr [rsp], xmm0',
        'movups xmmword ptr [rsp+0x10], xmm1',
        'movss xmm1, dword ptr [%s+0x%x]' % (base, disp + 12),
        'subss xmm1, dword ptr [%s+0x%x]' % (base, disp + 8),          # v4 - v3
        'lea eax, [rax-3]',
        'cvtsi2ss xmm0, eax',                                          # n - 4
        'lea eax, [rax+3]',                                            # count-1 again, upper half zero
        'mulss xmm0, xmm1',
        'addss xmm0, dword ptr [%s+0x%x]' % (base, disp + 12),
        'maxss xmm0, dword ptr [rip+2f]',                              # at least 0.1, and 0.1 for a NaN
        'mulss xmm%d, xmm0' % x,
        'movups xmm1, xmmword ptr [rsp+0x10]',
        'movups xmm0, xmmword ptr [rsp]',
        'lea rsp, [rsp+0x20]',
        'jmp game+0x%X' % back,
        '2: .float %r' % MIN,
    ])
    return dump[rva:back].hex(' '), asm


def build(dump):
    """([], caves) like loadout_sites.build: no in-place sites, one cave per mulss."""
    caves = []
    for rva, fn, note in SITES:
        rep, asm = cave(dump, rva)
        caves.append((rva, rep, asm, '%X block %d %s' % (fn, TABLE[decode(dump, rva)[3]], note)))
    return [], caves
