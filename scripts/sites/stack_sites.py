# Stack and temporary arrays sized for four or eight avatars or players (game build 25480438): the sites
# scripts/gen_count_sites.py reads. With the avatar manager at sixteen slots, and with bodies in it beside the players,
# these functions see more avatars than their arrays hold. Formats:
#   INPLACE (rva, old, new, name, eight)       one instruction or a run of them, same length
#   CAVES   (rva, replaced, code, name, eight) a jump replaces `replaced`; `game+0x..` is a game address, CALL(n) the
#                                              register-saving call of callback n (gen_count_sites.py)
# eight: 1 for a build for eight only (group 11), 0 for every build (group 10). A fix goes in every build when four
# players can reach it: their bodies make five or more avatars in the respawn window and during the corpse delay. A fix
# for a room of eight goes in the build for eight only: the build for four keeps the avatar manager at eight slots.
# All static, from the dump; the C callbacks are in src/co_avatars.c.

# Callback numbers: the index in kCountCallbacks (co_counts.c).
AV_DIST_SUM, ENC_SPREAD, DORMANCY = 18, 19, 20
# The guard of gen_count_sites.GUARDS that SC2 replaces: it kept slots 4 and up out of the list, SC2 gives them room.
REPLACES = [0x18437DD]

INPLACE = []
CAVES = []


def inplace(name, rva, old, new, eight):
    INPLACE.append((rva, old, new, name, eight))


def cave(name, rva, replaced, code, eight):
    CAVES.append((rva, replaced, code, name, eight))


# 0x8C8DA0, expandable cover over capacity, and 0x92F0E0, hellpods over capacity: both cull the candidate farthest
# from all avatars. Phase 1 copies avatar positions into vec3[4] on the stack; EC1/HP1 skip it, and EC2/HP2 sum the
# distances in C from live positions. xmm6 holds the sum where the game's own loop leaves it.
inplace('EC1', 0x8C8DF8, '85 d2 0f 84 b3 00 00 00', 'e9 b6 00 00 00 0f 1f 00', 0)
cave('EC2', 0x8C9070, '83 f8 04 0f 82 ce 01 00 00', 'lea rdx, [rsp+0x50]\n CALL(%d)\n movd xmm6, eax\n jmp game+0x8C92C5' % AV_DIST_SUM, 0)
inplace('HP1', 0x92F155, '85 c9 0f 84 b2 00 00 00', 'e9 b5 00 00 00 0f 1f 00', 0)
cave('HP2', 0x92F637, '83 f8 04 0f 82 dd 01 00 00', 'lea rdx, [rsp+0x48]\n CALL(%d)\n movd xmm6, eax\n jmp game+0x92F89C' % AV_DIST_SUM, 0)

# 0xACB430, the reinforcement anchor: a score per avatar in float[4] at rbp-0x80, then the first strictly greatest.
# The scores go to one slot S = [rbp-0x80]; RA7 keeps the best B = [rbp-0x7C] and its index I = [rbp-0x78] at each
# avatar's end (the game's init zeroes all three), RA8 takes I, RA9 jumps over the old search.
for name, rva in (('RA1', 0xACB6A7), ('RA3', 0xACB814), ('RA5', 0xACB935)):
    inplace(name, rva, 'f3 42 0f 58 4c b5 80', 'f3 0f 58 4d 80 66 90', 0)   # addss xmm1,[rbp+r14*4-0x80] -> [rbp-0x80]
for name, rva in (('RA2', 0xACB6B5), ('RA4', 0xACB822), ('RA6', 0xACB93C)):
    inplace(name, rva, 'f3 42 0f 11 4c b5 80', 'f3 0f 11 4d 80 66 90', 0)   # movss [rbp+r14*4-0x80],xmm1 -> [rbp-0x80]
cave('RA7', 0xACB951, '41 ff c4 44 3b 67 6c', '''movss xmm0, dword ptr [rbp-0x80]
 test r12d, r12d
 je 1f
 comiss xmm0, dword ptr [rbp-0x7c]
 jbe 2f
1: movss dword ptr [rbp-0x7c], xmm0
 mov dword ptr [rbp-0x78], r12d
2: mov dword ptr [rbp-0x80], 0
 inc r12d
 cmp r12d, dword ptr [rdi+0x6c]
 jmp game+0xACB958''', 0)
inplace('RA8', 0xACB971, '41 8b d7', '8b 55 88', 0)                                       # mov edx,r15d -> [rbp-0x78]
inplace('RA9', 0xACB9B9, '45 3b d0 0f 86 90 00 00 00', 'e9 94 00 00 00 0f 1f 40 00', 0)  # -> jmp 0xACBA52

# 0x9FAA30, encounter stage change: shuffles avatar indexes in u32[4] and sends the stage's calls to them in turn.
cave('ER1', 0x9FAAB5, '48 8b 05 64 c2 92 02', 'CALL(%d)\n jmp game+0x9FAC21' % ENC_SPREAD, 0)

# 0xAD5360, the lobby's live payload: playerPositions[4] by a running count, the count right behind. Players past four
# are left out of the upload.
cave('LV1', 0xAD5466, '8b 85 b0 00 00 00', 'mov eax, dword ptr [rbp+0xb0]\n cmp eax, 4\n jae game+0xAD560A\n jmp game+0xAD546C', 0)

# 0x1843500, the HUD's sample counter: carried containers by player slot in u32[4] at rbp+0x40, the stack cookie behind.
# The list moves to rbp-0x70 with sixteen entries; that room is first written after both loops (0x1843E13).
cave('SC1', 0x1843624, '89 45 40 89 45 44 89 45 48 89 45 4c',
     '\n'.join(' mov dword ptr [rbp-0x%x], eax' % (0x70 - 4 * k) for k in range(16)) + '\n jmp game+0x1843630', 0)
cave('SC2', 0x18437DD, '42 89 44 ad 40', 'cmp r13d, 16\n jae 1f\n mov dword ptr [rbp+r13*4-0x70], eax\n1: jmp game+0x18437E2', 0)
inplace('SC3', 0x1843906, '48 8d 4d 40', '48 8d 4d 90', 0)   # lea rcx,[rbp+0x40] -> [rbp-0x70]
inplace('SC4', 0x184391E, '83 fa 04', '83 fa 10', 0)         # cmp edx,4 -> 16

# Rooms of eight: only a build for eight reaches them.
# 0x4C39C0 and 0x94EB90: a voice line by the nearest Helldiver; the distances are float[8]. Only avatars 0 to 7 speak.
cave('VO1', 0x4C3CC2, '41 8b 5e 6c 41 ff c7', 'mov ebx, dword ptr [r14+0x6c]\n cmp ebx, 8\n jbe 1f\n mov ebx, 8\n1: inc r15d\n jmp game+0x4C3CC9', 1)
cave('VO2', 0x94EEE9, 'ff c7 8b 5e 6c', 'inc edi\n mov ebx, dword ptr [rsi+0x6c]\n cmp ebx, 8\n jbe 1f\n mov ebx, 8\n1: jmp game+0x94EEEE', 1)
# Spawn-exclusion contexts {vec3[8], count +0x60, radius +0x64, flag +0x68}: entries past eight are dropped.
# 0x95B250, the director's spawn query, at rbp+0x10 (two fill loops).
cave('SQ1', 0x95B9A6, 'f2 0f 11 44 95 10 89 44 95 18', '''cmp ecx, 8
 jae 1f
 movsd qword ptr [rbp+rdx*4+0x10], xmm0
 mov dword ptr [rbp+rdx*4+0x18], eax
 jmp game+0x95B9B0
1: jmp game+0x95B9B8''', 1)
cave('SQ2', 0x95BB9E, 'f2 44 0f 11 6c 8d 10', 'cmp eax, 8\n jae 1f\n movsd qword ptr [rbp+rcx*4+0x10], xmm13\n1: jmp game+0x95BBA5', 1)
cave('SQ3', 0x95BBA9, '89 7c 8d 18 8b 75 70', '''cmp eax, 8
 jae 1f
 mov dword ptr [rbp+rcx*4+0x18], edi
 mov esi, dword ptr [rbp+0x70]
 jmp game+0x95BBB0
1: mov esi, dword ptr [rbp+0x70]
 dec esi
 jmp game+0x95BBB0''', 1)
# 0x95D730, the friendly SEAF spawner, at rbp-0x70.
cave('SQ4', 0x95DBAF, 'f2 44 0f 11 6c 8d 90', 'cmp eax, 8\n jae 1f\n movsd qword ptr [rbp+rcx*4-0x70], xmm13\n1: jmp game+0x95DBB6', 1)
cave('SQ5', 0x95DBBB, '89 7c 8d 98 44 8b 6d f0', '''cmp eax, 8
 jae 1f
 mov dword ptr [rbp+rcx*4-0x68], edi
 mov r13d, dword ptr [rbp-0x10]
 jmp game+0x95DBC3
1: mov r13d, dword ptr [rbp-0x10]
 dec r13d
 jmp game+0x95DBC3''', 1)
# 0x8F95F0, an enemy reinforcement request: the context in the request at +0x13C, its count at +0x19C.
cave('SQ6', 0x8F9770, '8b 83 9c 01 00 00', 'mov eax, dword ptr [rbx+0x19c]\n cmp eax, 8\n jae game+0x8F9799\n jmp game+0x8F9776', 1)
# 0x94EB90 and 0xA139E0: avatar lists of eight from the frame allocator 0x173B8D0(alloc, size, align); sixteen now.
inplace('DL1', 0x950B66, 'ba 60 00 00 00 44 8d 42 a4', 'ba c0 00 00 00 6a 04 41 58', 1)   # size 0xC0; push 4; pop r8
inplace('DL2', 0x950B7E, 'ba 20 00 00 00', 'ba 40 00 00 00', 1)
inplace('DL3', 0x950B99, '44 8d 42 e4', '44 8d 42 c4', 1)                                 # align stays 4 for size 0x40
cave('AP1', 0xA139FD, '48 8b 49 10 8d 50 60', 'mov rcx, qword ptr [rcx+0x10]\n mov edx, 0xc0\n jmp game+0xA13A04', 1)

# 0xA04910, the dormancy pass: player positions in vec3[4] at rsp+0x48, the stack cookie right behind; the fill has no
# bound and the pass then takes each entity's nearest player from the array. The positions go to the mod's vec3[8]
# (DORMANCY fills it with the game's default and gives it in r14). The old room holds two pointers instead: the array
# at rsp+0x50 for the fill, its first z at rsp+0x48 for the two search loops. Nothing else reads that room, and the
# pass has one caller (0x57175F, the world's update), so one array serves.
cave('DP1', 0xA04982, 'f2 0f 11 44 24 48', '''CALL(%d)
 mov qword ptr [rsp+0x50], r14
 lea r14, [r14+8]
 mov qword ptr [rsp+0x48], r14
 jmp game+0xA049AA''' % DORMANCY, 1)
inplace('DP2', 0xA049B9, '4c 8d 74 24 48', '4c 8b 74 24 50', 1)   # lea r14,[rsp+0x48] -> mov r14,[rsp+0x50]
inplace('DP3', 0xA04D46, '48 8d 4c 24 50', '48 8b 4c 24 48', 1)   # lea rcx,[rsp+0x50] -> mov rcx,[rsp+0x48]
inplace('DP4', 0xA04EAB, '48 8d 4c 24 50', '48 8b 4c 24 48', 1)

# 0x874D80, the customization manager's frame: each player index holds the packages of its armor's and helmet's passive
# (applied record +0x3C, +0x38) as a u32 pair. This frame's pairs go to the stack (four, the cookie behind), last
# frame's are at game+0x37CED80 (four, a byte flag behind); a changed entry releases the old package and takes the new.
# Both lists move to the mod's u32[16] each (DATA(PASSIVES): last frame's, then this frame's at +0x40), and the guard
# of the build for four makes way (REPLACES8). From CP3 on r14 is the mod's list. co_counts.c carries the game's four
# pairs over when the counts go in, and back when they go out.
PASSIVES = 2   # the data slot (kCountData in co_counts.c)
REPLACES8 = [0x874E26]
cave('CP1', 0x874DE7, '0f 11 74 24 30 0f 11 7c 24 40', '''movups xmmword ptr [rsp+0x30], xmm6
 movups xmmword ptr [rsp+0x40], xmm7
 mov rcx, DATA(%d)
 movups xmmword ptr [rcx+0x40], xmm6
 movups xmmword ptr [rcx+0x50], xmm6
 movups xmmword ptr [rcx+0x60], xmm6
 movups xmmword ptr [rcx+0x70], xmm6
 jmp game+0x874DF1''' % PASSIVES, 1)
cave('CP2', 0x874E26, '8d 14 00 8b 43 c0', '''cmp eax, 8
 jae game+0x874E3A
 lea edx, [rax+rax*1]
 mov rcx, DATA(%d)
 mov eax, dword ptr [rbx-0x40]
 mov dword ptr [rcx+rdx*4+0x40], eax
 mov eax, dword ptr [rbx-0x44]
 mov dword ptr [rcx+rdx*4+0x44], eax
 jmp game+0x874E3A''' % PASSIVES, 1)
cave('CP3', 0x874E4A, '4c 8d 35 2f 9f f5 02', 'mov r14, DATA(%d)\n jmp game+0x874E51' % PASSIVES, 1)
inplace('CP4', 0x874E51, '41 b8 20 00 00 00', '41 b8 40 00 00 00', 1)   # mov r8d,0x20 -> 0x40: the compare's size
inplace('CP5', 0x874E5A, '48 8d 4c 24 30', '49 8d 4e 40 90', 1)         # lea rcx,[rsp+0x30] -> [r14+0x40]
inplace('CP6', 0x874E73, 'bd 08 00 00 00', 'bd 10 00 00 00', 1)         # mov ebp,8 -> 16 entries
cave('CP7', 0x874E80, '8b 7c 34 30 42 8b 04 36 3b f8', 'mov edi, dword ptr [r14+rsi+0x40]\n mov eax, dword ptr [r14+rsi]\n cmp edi, eax\n'
                                                       ' jmp game+0x874E8A', 1)
# The game stores its two registers of pairs here and at 0x874F37; this frame's list becomes last frame's instead.
cave('CP8', 0x874F30, '0f 11 35 49 9e f5 02', ''.join(' movups xmm0, xmmword ptr [r14+0x%x]\n movups xmmword ptr [r14+0x%x], xmm0\n' % (0x40 + o, o)
                                                     for o in range(0, 0x40, 0x10)) + ' jmp game+0x874F3E', 1)

# The game's code the tests run as it is, beside the patched code: the cull sums (phase 1 and the sum loop), the
# reinforcement anchor's search, the whole encounter stage change, the whole dormancy pass and the customization frame.
VANILLA = [(0x8C8DCB, 0x8C8EB3), (0x8C904B, 0x8C92CA), (0x92F138, 0x92F20F), (0x92F24F, 0x92F25D), (0x92F614, 0x92F89C),
           (0xACB951, 0xACBA52), (0x9FAA30, 0x9FAC36), (0xA04910, 0xA04FB1), (0x874D80, 0x874F71)]


def test_header(dump):
    """tests/co_stack_code.h: the VANILLA ranges' bytes for the tests to run."""
    h = ['/* Generated by scripts/gen_count_sites.py from scripts/sites/stack_sites.py; do not edit. */\n']
    for i, (start, end) in enumerate(VANILLA):
        b = dump[start:end]
        h.append('static const uint8_t kStackCode%d[] = {\n' % i)
        for j in range(0, len(b), 24): h.append('    ' + ', '.join('0x%02X' % x for x in b[j:j + 24]) + ',\n')
        h.append('};\n')
    h.append('static const struct { uint32_t rva, size; const uint8_t* code; } kStackCode[] = {\n')
    for i, (start, end) in enumerate(VANILLA): h.append('    {0x%Xu, 0x%Xu, kStackCode%d},\n' % (start, end - start, i))
    h.append('};\n')
    return h
