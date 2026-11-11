#!/usr/bin/env python3
# The Pelican's eight passenger seats and all eight players' boosters (game build 25480438): caves
# scripts/gen_count_sites.py reads (groups 16 and 17, eight only). Static analysis only; the design's verify script ran
# every cave natively against the dump.
# Seats: the Pelican is seat type 40 (6 passenger seats, entrance 6, hub 7). Seats 8 (attach_passenger_left_4) and 9
# (attach_passenger_right_4) join them, reached through the hub and with left_3's / right_3's sit events (3, 6). C1,
# L1 and V1 ask the mod: seats 8 and 9 exist only once it has seen both nodes on a Pelican's unit (co_extract.c);
# until then C1 hands out the game's eight records, L1 the game's entrance list and V1 refuses seats 8 and 9, so the
# other caves, which act on seats 8 and 9 alone, change nothing. A peer's message can still name seat 8 or 9, so NL3
# and NL6 ask for each attach whether that unit has the node, and else attach to left_3 / right_3.
# Boosters: B1 replaces the per-frame booster update's only call of the collector 0x8568A0, which stops at four
# players; the callback does its work over eight (co_extract.c). BOOSTER_PINS are the game's bytes it rebuilds.
#   CAVES: (rva, replaced hex, asm, note, group)    GNU as intel code; game+0x.. and CALL(n) as in gen_count_sites.py
import struct

SEAT_DATA, SEAT_LIST, SEAT_VALID, BOOSTERS, SEAT_NODE = 40, 41, 42, 43, 44
G_SEATS, G_BOOSTERS = 16, 17
HASH_L4, HASH_R4 = 0xFC07BFB6, 0x27393B02   # thin murmur64a of attach_passenger_left_4 / attach_passenger_right_4
HASH_L3, HASH_R3 = 0xDD1461AE, 0x6AD680F5   # attach_passenger_left_3 / right_3: seats 2 and 5, on every gunship
RECORDS = 0x31AE690                          # type 40's eight seat records of 0x8C: {hash, index, n, links[32]}
RECORD = 0x8C
LISTS = 0x31AE590                            # type 40's seat lists, 0x1C each; the entrance (6) lists 0,3,1,4,2,5,-1
LIST = 0x1C
PAIRS = 0x31AEAF0                            # type 40's entrance pairs, 8 B per seat: (6, -1) for 0..5, (-1, 0) for 6, 7
NODES = 0x215C800                            # the attach node per seat of the event handler 0x1191520
HUB_HASH = 0xA524B781                        # interact_passenger, seats 6 and 7
LIST_AT = 0x578                              # the entrance list in the mod's seat block
BLOCK_SIZE = LIST_AT + 9 * 4

CAVES = [
    # Seat table getter 0x1195460, case 40: the count and the records come from the callback.
    (0x11956A4, 'c7 02 08 00 00 00', 'mov dword ptr [rdx], 8\n CALL(%d)\n ret' % SEAT_DATA, 'C1 count and records', G_SEATS),
    # Seat roles 0x11957C0 and four inlined copies: seats 8 and 9 take the passengers' role 3. Every other seat leaves
    # by the game's own compare, so its flags are the game's.
    (0x119592C, '83 fa 05 0f 87 0d ff ff ff', 'cmp edx, 8\n jb 1f\n cmp edx, 9\n jbe game+0x1195944\n'
                                             '1: cmp edx, 5\n ja game+0x1195842\n jmp game+0x1195935', 'R1 role', G_SEATS),
    (0x634758, '83 ff 05 77 3e', 'cmp edi, 8\n jb 1f\n cmp edi, 9\n jbe game+0x6345FF\n'
                                 '1: cmp edi, 5\n ja game+0x63479B\n jmp game+0x63475D', 'R2 role', G_SEATS),
    (0x634ED2, '83 fd 05 77 45', 'cmp ebp, 8\n jb 1f\n cmp ebp, 9\n jbe game+0x634D54\n'
                                 '1: cmp ebp, 5\n ja game+0x634F1C\n jmp game+0x634ED7', 'R3 role', G_SEATS),
    (0x636C78, '83 fa 05 77 4c', 'cmp edx, 8\n jb 1f\n cmp edx, 9\n jbe game+0x636CC2\n'
                                 '1: cmp edx, 5\n ja game+0x636CC9\n jmp game+0x636C7D', 'R4 role', G_SEATS),
    (0x6370BC, '83 fa 05 77 4c', 'cmp edx, 8\n jb 1f\n cmp edx, 9\n jbe game+0x637106\n'
                                 '1: cmp edx, 5\n ja game+0x63710D\n jmp game+0x6370C1', 'R5 role', G_SEATS),
    # Valid seat 0x11962D0 (true at 0x119630E, false at 0x11962F7): 8 and 9 only with the seats on (the callback's CF).
    # The result is al alone; its callers test al first, so the flags at the way out are dead.
    (0x11963E3, '83 fa 07 0f 87 0b ff ff ff', 'cmp edx, 7\n jbe 1f\n cmp edx, 9\n ja 1f\n CALL(%d)\n jb game+0x119630E\n'
                                             '1: cmp edx, 7\n ja game+0x11962F7\n jmp game+0x11963EC' % SEAT_VALID, 'V1 valid', G_SEATS),
    # Seat lists 0x11966F0: the entrance's list from the callback; seats 8 and 9 get seat 0's empty list.
    (0x1196945, '48 6b c0 1c 48 03 c1 c3', 'cmp eax, 6\n je 2f\n cmp eax, 8\n jb 1f\n xor eax, eax\n1: imul rax, rax, 0x1c\n'
                                           ' add rax, rcx\n ret\n2: CALL(%d)\n ret' % SEAT_LIST, 'L1 lists', G_SEATS),
    # Entrance pairs 0x1196A60 and per-seat words 0x1199F40: seats 8 and 9 read seat 0's (all seats hold the same).
    (0x1196CB0, '8b c2 48 8d 04 c5 f0 ea 1a 03', 'mov eax, edx\n cmp eax, 8\n jb 1f\n xor eax, eax\n'
                                                '1: lea rax, [rax*8+0x31aeaf0]\n jmp game+0x1196CBA', 'E1 pairs', G_SEATS),
    (0x119A188, '8b c2 48 8d 04 85 70 e6 1a 03', 'mov eax, edx\n cmp eax, 8\n jb 1f\n xor eax, eax\n'
                                                '1: lea rax, [rax*4+0x31ae670]\n jmp game+0x119A192', 'O1 words', G_SEATS),
    # Seat steps 0x1196DC0 (tables of 8 x 8): a step from or to seat 8 or 9 goes through the hub 7, as every passenger
    # seat's does; hub -> 8 fires event 3 (left_3's), hub -> 9 event 6 (right_3's).
    (0x119756D, '8b 0a 48 c1 e1 03 41 8b c0', 'mov ecx, dword ptr [rdx]\n cmp ecx, 8\n jae 3f\n cmp r8d, 8\n jae 3f\n shl rcx, 3\n'
                                             ' mov eax, r8d\n jmp game+0x1197576\n'
                                             '3: cmp ecx, 9\n ja 9f\n cmp r8d, 9\n ja 9f\n cmp ecx, r8d\n je 9f\n cmp ecx, 7\n je 5f\n'
                                             ' mov eax, 7\n cmp ecx, 6\n jne 4f\n xor eax, eax\n4: mov r8d, 7\n jmp 6f\n'
                                             '5: mov eax, 3\n cmp r8d, 8\n je 6f\n mov eax, 6\n'
                                             '6: mov dword ptr [rdx], r8d\n shl rcx, 3\n ret\n'
                                             '9: mov eax, -1\n mov r8d, eax\n shl rcx, 3\n ret', 'T1 steps', G_SEATS),
    # Direct seat event 0x119A6B0: seat 8 -> event 3, seat 9 -> event 6.
    (0x119A857, '8b c2 8b 84 81 e8 51 26 02 c3', 'mov eax, 3\n cmp edx, 8\n je 1f\n mov eax, 6\n cmp edx, 9\n je 1f\n mov eax, edx\n'
                                                ' mov eax, dword ptr [rcx+rax*4+0x22651e8]\n1: ret', 'N1 events', G_SEATS),
    # 0x119AA50 gives -1 for all eight seats: the same for 8 and 9.
    (0x119AF2A, '8b c2 48 8d 04 85 00 00 00 00', 'cmp edx, 8\n jb 1f\n mov eax, -1\n ret\n1: mov eax, edx\n lea rax, [rax*4+0x0]\n'
                                                ' jmp game+0x119AF34', 'A1 words', G_SEATS),
    # The seat event handler 0x1191520, events 3 and 6: seats 8 and 9 attach to the node the callback names, their
    # own where that unit has it (rdi the vehicle's entity record from 0x119F910, its unit at +0xC). The flags are
    # dead: the next flag user is past the call behind.
    (0x1191702, '45 8b 84 b6 00 c8 15 02', 'mov r8d, dword ptr [r14+rsi*4+0x215c800]\n cmp esi, 8\n jb 1f\n cmp esi, 9\n ja 1f\n'
                                          ' CALL(%d)\n1: jmp game+0x119170A' % SEAT_NODE, 'NL3 node', G_SEATS),
    (0x11917E2, '45 8b 84 b6 00 c8 15 02', 'mov r8d, dword ptr [r14+rsi*4+0x215c800]\n cmp esi, 8\n jb 1f\n cmp esi, 9\n ja 1f\n'
                                          ' CALL(%d)\n1: jmp game+0x11917EA' % SEAT_NODE, 'NL6 node', G_SEATS),
    # The per-frame booster update: the collector's only call. rcx is B (world+0xEB5A50); every register is kept.
    (0x573690, 'e8 0b 32 2e 00', 'CALL(%d)\n jmp game+0x573695' % BOOSTERS, 'B1 boosters', G_BOOSTERS),
]
# The booster callback rebuilds 0x8568A0's body and calls its callees as it does: the bytes it depends on, (start, end).
# The body holds every field offset, stride and global; 0x12689C0 writes *rcx on every path; 0x66F190 saves rcx only to
# overwrite it (0x66F1B7). Of the other callees the head with the arguments' first use. Code_matches takes 256 at most.
BOOSTER_PINS = [(0x8568BA, 0x8569BA), (0x8569BA, 0x856ABA), (0x856ABA, 0x856BBA), (0x12689C0, 0x1268AA7),
                (0x12FE350, 0x12FE360), (0x12FE4E0, 0x12FE4F0), (0x670E30, 0x670E40), (0x66F190, 0x66F1BC),
                (0x66F4C0, 0x66F4D0)]
# HellForge moves the stratagem list at game+0x37CB600 when a mod adds a stratagem, and rewrites this lea r13's disp32
# with it (its stratagem_sites.h). The pin leaves those four bytes open; the callback reads the list through the lea.
EFFECTS_LEA = 0x856993
BOOSTER_OPEN = range(EFFECTS_LEA + 3, EFFECTS_LEA + 7)


def seat_block(dump):
    """The mod's seat block for type 40: records 0..6 as the game's, the hub (7) linking 0..6, 8 and 9, records 8 and 9
    linking the hub, and the entrance list 0,3,1,4,2,5,8,9,-1."""
    b = bytearray(dump[RECORDS:RECORDS + 8 * RECORD]) + bytearray(2 * RECORD)
    struct.pack_into('<IIi9I', b, 7 * RECORD, HUB_HASH, 7, 9, 0, 1, 2, 3, 4, 5, 6, 8, 9)
    struct.pack_into('<IIiI', b, 8 * RECORD, HASH_L4, 8, 1, 7)
    struct.pack_into('<IIiI', b, 9 * RECORD, HASH_R4, 9, 1, 7)
    b += struct.pack('<9i', 0, 3, 1, 4, 2, 5, 8, 9, -1)
    assert len(b) == BLOCK_SIZE
    return bytes(b)


def check(dump):
    """The game's data the block and the caves build on, as the study read it."""
    u32 = lambda a: struct.unpack_from('<I', dump, a)[0]
    i32 = lambda a: struct.unpack_from('<i', dump, a)[0]
    recs = [(u32(RECORDS + r * RECORD), u32(RECORDS + r * RECORD + 4), i32(RECORDS + r * RECORD + 8)) for r in range(8)]
    assert [r[1:] for r in recs] == [(r, 1) for r in range(7)] + [(7, 7)] and recs[6][0] == recs[7][0] == HUB_HASH, recs
    assert [i32(LISTS + 6 * LIST + 4 * j) for j in range(7)] == [0, 3, 1, 4, 2, 5, -1]
    assert [u32(NODES + 4 * i) for i in range(6, 10)] == [HUB_HASH, 0, 0, 0]   # the attach nodes; 8, 9 hold none
    assert u32(NODES + 8) == HASH_L3 and u32(NODES + 0x14) == HASH_R3        # the fallback is seats 2 and 5's node
    # 0x635270 resolves records by the pairs' seats (its callers 0x6367E8, 0x637758), never seat 8 or 9: E1 gives
    # those seat 0's pair.
    assert [(i32(PAIRS + 8 * r), i32(PAIRS + 8 * r + 4)) for r in range(8)] == [(6, -1)] * 6 + [(-1, 0)] * 2
    assert all(ok - lo <= 256 and dump[lo:ok] for lo, ok in BOOSTER_PINS)
    assert dump[EFFECTS_LEA:EFFECTS_LEA + 3] == bytes.fromhex('4c8d2d') and \
        EFFECTS_LEA + 7 + struct.unpack_from('<i', dump, EFFECTS_LEA + 3)[0] == 0x37CB600
    assert HASH_L4 not in [r[0] for r in recs] and HASH_R4 not in [r[0] for r in recs]


def build(dump):
    check(dump)
    return [], CAVES
