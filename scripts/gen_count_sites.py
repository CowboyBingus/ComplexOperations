#!/usr/bin/env python3
# Writes src/co_count_sites.h: the code sites that make the session lists count to eight (game build 25480438), from
# scripts/sites/count_sites.tsv, the caves below and a dump of the game's code:
#   scripts/gen_count_sites.py <game.dll image dump> [out]
# Three kinds of site:
#   - one instruction changed in place (the tsv);
#   - a flag clear that grows from four bytes to eight in the room of two instructions (CLEARS);
#   - a cave: a jump replaces some instructions and leads to code of the mod's, which may call into the mod's C code
#     and jumps back (CAVES). The caves are assembled here with GNU as; the header holds their bytes.
# The guards (GUARDS, GUARD_SITES) are caves and in-place sites of the same kind for the game's four-entry tables.
# The message guards (MESSAGES, NET_GUARDS) keep a list in a message inside the buffer its handler has for it.
# Every site and cave carries a group; the mod writes the groups its build has switched on:
#   0 counts, 1 guards, 2 message guards (always); 3 only in a build for eight players (EIGHT_SITES, EIGHT_CAVES);
#   4 only with the test option that hands out slots and indexes from a later number (TEST_CAVES);
#   5 only for eight players: the loadout screen with eight records and cards (sites/loadout_sites.py);
#   6 only for four players: the loadout screen's guards that group 5 replaces (loadout_sites.MOD_OFF);
#   7 only for eight players: the bounds of the per-peer loadout table, which the mod moves to room for eight first;
#   8 the score manager's loot rows (always): the bounds by index that follow the player managers' state (LOOT);
#   9 only for eight players: the mission summary with eight rows and cards (sites/summary_sites.py); its guards for
#     four rows are group 6;
#   10 the stack arrays that four players' avatars and bodies overrun (sites/stack_sites.py, always);
#   11 only for eight players: the stack and frame arrays with room for eight avatars (sites/stack_sites.py).
# It also writes tests/co_stack_code.h: the game's code the tests run beside the stack fixes.
# An instruction of a moved array carries a distance the session step rewrites; those four bytes are not compared.
import os, re, struct, sys

G_COUNT, G_GUARD, G_NET, G_EIGHT, G_TEST, G_LOADOUT, G_GUARD4, G_SYNCED, G_LOOT, G_SUMMARY, G_STACK, G_STACK8 = range(12)
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE + '/sites')
import cave_asm, loadout_sites, map_sites, stack_sites, summary_sites, tag_sites
dump = open(sys.argv[1], 'rb').read()
out = sys.argv[2] if len(sys.argv) > 2 else HERE + '/../src/co_count_sites.h'

# The session step's sites: rva + offset of the four bytes it rewrites.
moved = set()
for m in re.finditer(r'\{0x([0-9A-F]+)u, (\d+), 0x', open(HERE + '/../src/co_session_sites.h').read()):
    moved.add(int(m.group(1), 16) + int(m.group(2)))

def skip_of(rva, size):
    hits = [a - rva for a in moved if rva <= a and a + 4 <= rva + size]
    assert len(hits) <= 1
    return hits[0] if hits else 0xFF

sites = []
for line in open(HERE + '/sites/count_sites.tsv'):
    if line.startswith('#') or not line.strip(): continue
    f = line.rstrip('\n').split('\t')
    rva = int(f[0], 16); old = bytes.fromhex(f[1]); new = bytes.fromhex(f[2])
    assert len(old) == len(new) <= 12 and dump[rva:rva + len(old)] == old, f[0]
    skip = skip_of(rva, len(old))
    if skip != 0xFF: assert old[skip:skip + 4] == new[skip:skip + 4], f[0]
    sites.append((rva, old, new, skip, G_COUNT))

# xor eax,eax; mov [reg+A2],eax -> and qword [reg+A2],0. eax is loaded again before its next use, and so are the
# flags. (rva of the xor, ModRM of the mov, ModRM of the and)
CLEARS = [(0x1083C99, 0x83, 0xA3), (0x1088B8A, 0x87, 0xA7), (0x1088EA1, 0x83, 0xA3)]
for rva, mov, _ in CLEARS:
    assert dump[rva:rva + 4] == bytes([0x33, 0xC0, 0x89, mov]) and rva + 4 in moved, hex(rva)

# The call into C: every register and the flags are kept; the callback gets a pointer to them (CoRegs in the source).
CALL = '''
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rcx, rsp
    mov rbx, rsp
    and rsp, -16
    sub rsp, 0x80
    movups [rsp+0x20], xmm0
    movups [rsp+0x30], xmm1
    movups [rsp+0x40], xmm2
    movups [rsp+0x50], xmm3
    movups [rsp+0x60], xmm4
    movups [rsp+0x70], xmm5
    call qword ptr [rip + callback%d]
    movups xmm0, [rsp+0x20]
    movups xmm1, [rsp+0x30]
    movups xmm2, [rsp+0x40]
    movups xmm3, [rsp+0x50]
    movups xmm4, [rsp+0x60]
    movups xmm5, [rsp+0x70]
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
'''
# Callbacks, by number (the index in kCountCallbacks in co_counts.c). 8 to 13 are kept for the wire and extraction caves;
# 18 and 19 are the stack fixes' (stack_sites.py).
FIND_RSI, FIND_R11, GATE, VALUES, LAST_SLOT, MESSAGE, TEST_SLOT, TEST_INDEX = range(8)
SCORE_INDEX, SCORE_ROW, PARTICIPANT, RECORD = range(14, 18)
# (rva, bytes the jump replaces, code; `game+0x..` is an address in the game, CALL(n) the call of callback n)
def HIGH(r): return ''.join(' movups [%s+0x%x], xmm0\n' % (r, o) for o in range(0x150, 0x1D0, 0x10))
CAVES = [
    # Party records: the game's chain of four compares has run; a miss looks at records 4 to 7.
    (0x108A3F8, '48 85 c9 75 29', 'CALL(%d)\n test rcx, rcx\n jne game+0x108A426\n jmp game+0x108A3FD' % FIND_RSI),
    (0xB94116, '48 85 c9 75 2b', 'CALL(%d)\n test rcx, rcx\n jne game+0xB94146\n jmp game+0xB9411B' % FIND_R11),
    (0xBBEA44, '48 85 c9 75 2b', 'CALL(%d)\n test rcx, rcx\n jne game+0xBBEA74\n jmp game+0xBBEA49' % FIND_R11),
    # The join gate counts the used slots 1 to 3 one by one; the callback leaves the free slots in ecx.
    (0x108C18F, '41 8b c6 49 39 41 58', 'CALL(%d)\n jmp game+0x108C1BC' % GATE),
    # Peer values: four resets write the keys 1 to 3 one by one; the callback clears the keys 4 to 7.
    (0xB92D9B, '8b cd 48 8d 46 20', 'mov ecx, ebp\n lea rax, [rsi+0x20]\n CALL(%d)\n jmp game+0xB92DA1' % VALUES),
    (0xBB5ACB, '8b cd 48 8d 46 20', 'mov ecx, ebp\n lea rax, [rsi+0x20]\n CALL(%d)\n jmp game+0xBB5AD1' % VALUES),
    (0x1084F16, '66 66 0f 1f 84 00 00 00 00 00', 'CALL(%d)\n jmp game+0x1084F20' % VALUES),
    (0x1082FA2, '48 83 c4 28 c3', 'CALL(%d)\n add rsp, 0x28\n ret' % VALUES),
    # Session peers: the reset zeroes four entries with eight stores; the cave adds the other four entries.
    (0xB90AE8, '0f 11 83 40 01 00 00', 'movups [rbx+0x140], xmm0\n' + HIGH('rbx') + ' jmp game+0xB90AEF'),
    (0x1084A28, '0f 11 87 40 01 00 00', 'movups [rdi+0x140], xmm0\n' + HIGH('rdi') + ' jmp game+0x1084A2F'),
    (0x1087298, '41 0f 11 87 40 01 00 00', 'movups [r15+0x140], xmm0\n' + HIGH('r15') + ' jmp game+0x10872A0'),
    # The mission-flag test names the four slots one by one: the callback gives al for eight (the occupied slot with
    # the highest number is in state 6 to 8), and the game's two ways on follow.
    (0xACCA82, '48 39 bb d0 59 01 00', 'CALL(%d)\n test al, al\n jne game+0xACCAE9\n jmp game+0xACCBC9' % LAST_SLOT),
    # A reset of the slots' dword at +0x24, written out for slots 0 to 3 (rbp the host synchronizer, edi zero).
    (0x13506D7, '89 7d 44 8b d3', 'mov [rbp+0x44], edi\n mov edx, ebx\n mov [rbp+0x124], edi\n mov [rbp+0x15c], edi\n'
                                  ' mov [rbp+0x194], edi\n mov [rbp+0x1cc], edi\n jmp game+0x13506DC'),
    # The second slot loop of the peer-left broadcast counts rdi down from a constant that must stay four above.
    (0x108D368, '48 8b ac 24 98 00 00 00', 'mov rbp, [rsp+0x98]\n mov edi, 8\n jmp game+0x108D370'),
    # The own lobby's record (0x678 bytes) holds four members at +0x350; key 4 writes the session's count to +0x348
    # (members) and +0x5D0 (entries, the bound of the record's loops). The entries stop at four, as a friend's record has.
    (0x133EF93, '89 b3 d0 05 00 00', 'mov dword ptr [rbx+0x5d0], esi\n cmp esi, 4\n jbe game+0x133EF99\n'
                                     ' mov dword ptr [rbx+0x5d0], 4\n jmp game+0x133EF99'),
]
# Guards: tables of the game's with four entries, indexed by a player index or a player slot with no check. An index
# of four or more skips the access, so players 5 to 8 are missing from that table instead of overrunning it; for 0 to
# 3 nothing changes. Each cave runs the instructions its jump replaced.
GUARDS = [
    # Customization, every frame: four pairs on the stack, the stack cookie right behind them.
    (0x874E26, '8d 14 00 8b 43 c0', 'cmp eax, 4\n jae game+0x874E3A\n lea edx, [rax+rax*1]\n mov eax, dword ptr [rbx-0x40]\n jmp game+0x874E2C'),
    # Clan tags and names (two copies of each message handler).
    (0xB95BAB, '39 ae 08 6a 00 00', 'cmp r15d, 4\n jae game+0xB95CD5\n cmp dword ptr [rsi+0x6a08], ebp\n jmp game+0xB95BB1'),
    (0xBADDC3, '39 ae 08 6a 00 00', 'cmp r15d, 4\n jae game+0xBADEE5\n cmp dword ptr [rsi+0x6a08], ebp\n jmp game+0xBADDC9'),
    (0xB95D15, '41 39 be 20 6a 00 00', 'cmp r15d, 4\n jae game+0xB95E1A\n cmp dword ptr [r14+0x6a20], edi\n jmp game+0xB95D1C'),
    (0xBA518B, '41 39 be 20 6a 00 00', 'cmp r15d, 4\n jae game+0xBA529A\n cmp dword ptr [r14+0x6a20], edi\n jmp game+0xBA5192'),
    # Four rows of 0x28 filled by index, behind a scan of the four.
    (0xFEBB87, '41 83 f9 04 72 e3', 'cmp r9d, 4\n jb game+0xFEBB70\n cmp r8d, 4\n jae game+0xFEC0B4\n jmp game+0xFEBB8D'),
    # Other players' ships: an append to four entries of 0x118 with no check of the count.
    (0x65FFE2, '41 8b 85 60 04 00 00', 'mov eax, dword ptr [r13+0x460]\n cmp eax, 4\n jae game+0x66006A\n jmp game+0x65FFE9'),
    # A vehicle's owner id from four: skipping would leave the vehicle's field unset, so 4 to 7 share an id. An owner
    # without an index (-1) stays as it is.
    (0x7171EC, 'e9 48 ff ff ff', 'cmp ecx, 4\n jb 1f\n cmp ecx, -1\n je 1f\n and ecx, 3\n1: jmp game+0x717139'),
    # HUD: the samples panel's four avatars.
    (0x18437DD, '42 89 44 ad 40', 'cmp r13d, 4\n jae 1f\n mov dword ptr [rbp+r13*4+0x40], eax\n1: jmp game+0x18437E2'),
    # The game state's four rows of 0x90 by player index. Each skip target loads again what the loop keeps in a
    # register the index lookup overwrites.
    (0xB8F681, 'f2 41 0f 10 45 00', 'cmp eax, 4\n jae game+0xB8F723\n movsd xmm0, qword ptr [r13+0]\n jmp game+0xB8F687'),
    (0xB8F830, 'f2 41 0f 10 06', 'cmp eax, 4\n jae game+0xB8F885\n movsd xmm0, qword ptr [r14]\n jmp game+0xB8F835'),
    (0xBB3BBC, '8b c8 41 b8 d5 d6 bc 96', 'cmp eax, 4\n jae game+0xBB3C09\n mov ecx, eax\n mov r8d, 0x96bcd6d5\n jmp game+0xBB3BC4'),
    (0xBB535D, '8b d0 48 8d 04 d2', 'cmp eax, 4\n jae game+0xBB53F1\n mov edx, eax\n lea rax, [rdx+rdx*8]\n jmp game+0xBB5363'),
    (0xFFE407, '48 8d 14 c9 48 03 d2', 'cmp ecx, 4\n jae game+0xFFE43A\n lea rdx, [rcx+rcx*8]\n add rdx, rdx\n jmp game+0xFFE40E'),
    (0x100317E, 'f2 0f 10 03 8b d0', 'cmp eax, 4\n jae game+0x100320E\n movsd xmm0, qword ptr [rbx]\n mov edx, eax\n jmp game+0x1003184'),
    (0x100329A, 'f2 0f 10 07 41 b8 d5 d6 bc 96', 'cmp eax, 4\n jae game+0x10032FB\n movsd xmm0, qword ptr [rdi]\n mov r8d, 0x96bcd6d5\n jmp game+0x10032A4'),
    # Loadout screen: two maps of four dwords (player index to card and back) and four records by index. The writers:
    # a peer above 3, or one for which no card is free, gets no card. The skip enters the loop's tail with an empty
    # bit in eax, because the tail loads r8 for the loop head and r13d holds the index here.
    (0x146CC05, '41 8b cd 8b bc 8e d8 27 00 00', 'mov ecx, r13d\n cmp r13d, 4\n jae 1f\n mov edi, dword ptr [rsi+rcx*4+0x27d8]\n jmp game+0x146CC0F\n'
                                                 '1: mov r13d, dword ptr [rsp+0xe8]\n xor eax, eax\n jmp game+0x146CF99'),
    (0x146CC30, 'bf ff ff ff ff eb 21', 'mov r13d, dword ptr [rsp+0xe8]\n xor eax, eax\n jmp game+0x146CF99'),
    (0x146E996, '8b 87 d4 27 00 00', 'mov eax, dword ptr [rdi+0x27d4]\n cmp eax, 4\n jae game+0x146E9BE\n jmp game+0x146E99C'),
    # -1 is the function's own answer for "no card".
    (0x146D320, '8b c2 4c 8b c1', 'cmp edx, 4\n jae 1f\n mov eax, edx\n mov r8, rcx\n jmp game+0x146D325\n1: mov eax, 0xffffffff\n ret'),
    # The readers: without these an index above 3 reads the second map as the first and lands on another player's card.
    (0x875B50, '41 8b c9 45 8b 8c 88 d8 27 00 00', 'mov ecx, r9d\n cmp r9d, 4\n jae game+0x875BA8\n mov r9d, dword ptr [r8+rcx*4+0x27d8]\n jmp game+0x875B5B'),
    (0xB72AC6, '8b c1 41 8b 8c 80 d8 27 00 00', 'mov eax, ecx\n cmp ecx, 4\n jae game+0xB72B2A\n mov ecx, dword ptr [r8+rax*4+0x27d8]\n jmp game+0xB72AD0'),
    (0xB90143, '41 8b 8c 82 d8 27 00 00', 'cmp eax, 4\n jae game+0xB90168\n mov ecx, dword ptr [r10+rax*4+0x27d8]\n jmp game+0xB9014B'),
    (0xB90373, '41 8b bc 86 d8 27 00 00', 'cmp eax, 4\n jae game+0xB903A6\n mov edi, dword ptr [r14+rax*4+0x27d8]\n jmp game+0xB9037B'),
    (0xB90513, '41 8b 8c 86 d8 27 00 00', 'cmp eax, 4\n jae game+0xB90580\n mov ecx, dword ptr [r14+rax*4+0x27d8]\n jmp game+0xB9051B'),
    (0xBA5953, '41 8b bc 86 d8 27 00 00', 'cmp eax, 4\n jae game+0xBA5986\n mov edi, dword ptr [r14+rax*4+0x27d8]\n jmp game+0xBA595B'),
    (0xBAD843, '41 8b 8c 82 d8 27 00 00', 'cmp eax, 4\n jae game+0xBAD868\n mov ecx, dword ptr [r10+rax*4+0x27d8]\n jmp game+0xBAD84B'),
    (0x873328, '8b c2 41 8b 8c 81 d8 27 00 00', 'mov eax, edx\n cmp edx, 4\n jae game+0x873350\n mov ecx, dword ptr [r9+rax*4+0x27d8]\n jmp game+0x873332'),
    (0x1471E69, '8b c2 8b 94 81 d8 27 00 00', 'mov eax, edx\n cmp edx, 4\n jae game+0x1471E90\n mov edx, dword ptr [rcx+rax*4+0x27d8]\n jmp game+0x1471E72'),
    (0x1471AC0, '41 8b c0 8b 8c 87 d8 27 00 00', 'mov eax, r8d\n cmp r8d, 4\n jae game+0x1471AE8\n mov ecx, dword ptr [rdi+rax*4+0x27d8]\n jmp game+0x1471ACA'),
    (0x1471BD7, '41 8b c0 8b 9c 87 d8 27 00 00', 'mov eax, r8d\n cmp r8d, 4\n jae game+0x1471C0D\n mov ebx, dword ptr [rdi+rax*4+0x27d8]\n jmp game+0x1471BE1'),
    (0x1471D30, '41 8b c0 8b 8c 86 d8 27 00 00', 'mov eax, r8d\n cmp r8d, 4\n jae game+0x1471DB1\n mov ecx, dword ptr [rsi+rax*4+0x27d8]\n jmp game+0x1471D3A'),
    # Four entries of 0x1320 by player index: entry 4 would be the object's own members, a called pointer among
    # them. The function gives no value (its four callers read none), so 4 and more return at once.
    (0x139D300, '48 89 5c 24 10', 'cmp r8d, 4\n jae 1f\n mov qword ptr [rsp+0x10], rbx\n jmp game+0x139D305\n1: ret'),
    # Mission result: it copies the loot rows by score slot into room for four rows of 0x18, a table of objectives
    # right behind. Slot 4 and up skips the copy (the guard stands before the load, whose offset the managers move); a
    # flagged slot (dl) still adds its row's three counts to the loot total at [rsp+0x34], which edx carries on, as the
    # game's own add does. The callback gives that sum, 0 for a slot without a row.
    (0x62F2DB, '4d 03 cd 89 44 24 3c 44 8b d0', 'add r9, r13\n mov dword ptr [rsp+0x3c], eax\n mov r10d, eax\n cmp r13d, 4\n jb game+0x62F2E5\n'
                                                ' test dl, dl\n je game+0x62F358\n CALL(%d)\n add dword ptr [rsp+0x34], eax\n mov edx, dword ptr [rsp+0x34]\n'
                                                ' jmp game+0x62F35C' % SCORE_ROW),
    # Activity record: the participants (four, the count right behind) and the peers whose answer it waits for (three)
    # are filled from the ready list with no bound. A peer beyond them stays out of the record; the callback keeps
    # participants 5 to 8 for the record that is being made, and the record's post lists them (co_counts.c).
    (0xA56D88, '48 89 54 c5 98', 'CALL(%d)\n cmp eax, 4\n jae game+0xA56D95\n mov qword ptr [rbp+rax*8-0x68], rdx\n jmp game+0xA56D8D' % PARTICIPANT),
    # The record is in the table now (rsi the table, rcx its offset): the kept participants go with its key.
    (0xA56F0E, 'ff 86 00 11 00 00', 'inc dword ptr [rsi+0x1100]\n CALL(%d)\n jmp game+0xA56F14' % RECORD),
    (0xA56E91, '48 89 4c c5 e8', 'cmp eax, 3\n jae game+0xA56E9E\n mov qword ptr [rbp+rax*8-0x18], rcx\n jmp game+0xA56E96'),
    (0x12E67B1, '48 89 4c c5 bf', 'cmp eax, 3\n jae game+0x12E67BE\n mov qword ptr [rbp+rax*8-0x41], rcx\n jmp game+0x12E67B6'),
    # A record's bits 0 to 3 and 4 to 7 by player index: index 4 would set a bit of the other half or a flag above
    # them. An index of -1 goes on as the game has it.
    (0x13D1119, '44 8b 4d 6f 41 b8 04 00 00 00', 'cmp eax, 4\n jb 1f\n cmp eax, -1\n jne game+0x13D173E\n1: mov r9d, dword ptr [rbp+0x6f]\n mov r8d, 4\n'
                                                 ' jmp game+0x13D1123'),
    # Clan records ([game+0x347CDC8]: four pairs of 8 bytes, then four records of 0x4A, then the object's own fields) and
    # the clan station's four rows of 0x28 by player index: the copies of the handlers above. The pair and record
    # handlers and the rows' refresh give no value, so 4 and more return at once; the second refresh leaves by its own
    # pop rbx; ret.
    (0x12919A0, '40 55 57 41 56', 'cmp r8d, 4\n jae 1f\n push rbp\n push rdi\n push r14\n jmp game+0x12919A5\n1: ret'),
    (0x1291AB0, '48 89 5c 24 08', 'cmp r8d, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x1291AB5\n1: ret'),
    (0xB65880, '48 89 5c 24 08', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0xB65885\n1: ret'),
    (0xFEC3E8, '41 8b c0 4c 8d 0c 80', 'cmp r8d, 4\n jae game+0xFEC437\n mov eax, r8d\n lea r9, [rax+rax*4]\n jmp game+0xFEC3EF'),
]
# The score manager's loot rows: B (+0xC4A0) by player index, L (+0xC440) by score slot, four of 0x18 each. Once the
# managers have moved (gen_manager_sites.py) there are eight of each behind the manager, else four in the game's place.
# These guards ask the mod which; they go in and out with the counts, the rows' own sites and caves with the managers.
LOOT = [
    # An index without a row skips the access (the callback sets CF for an index with one).
    (0x6311D4, '3b c3 0f 84 2f 01 00 00', 'CALL(%d)\n jae game+0x63130B\n jmp game+0x6311DC' % SCORE_INDEX),
    (0x631514, '8b c1 48 8d 0c 40', 'mov eax, ecx\n lea rcx, [rax+rax*2]\n CALL(%d)\n jae game+0x63155B\n jmp game+0x63151A' % SCORE_INDEX),
    (0x631693, '41 3b ae 0c 7d 03 00', 'push rax\n mov eax, ebp\n CALL(%d)\n pop rax\n jae game+0x6316A0\n cmp ebp, dword ptr [r14+0x37d0c]\n'
                                        ' jmp game+0x63169A' % SCORE_INDEX),
]
# Guards in place: the game tests the index against -1 ("none") only. cmp reg,-1; je becomes cmp reg,4; jae, which
# sends 4 to 7 the way "none" goes and leaves -1 going there. (rva, bytes, new bytes)
GUARD_SITES = [
    (0x14670B8, '83 f9 ff 0f 84 dc fe ff ff', '83 f9 04 0f 83 dc fe ff ff'),         # loadout: the own index at setup
    (0x14697C1, '83 f8 ff 0f 84 9d 00 00 00', '83 f8 04 0f 83 9d 00 00 00'),         # and found later
    (0x146C46A, '41 83 fd ff 0f 84 38 02 00 00', '41 83 fd 04 0f 83 38 02 00 00'),   # a player's card
    (0x14716AD, '83 f8 ff 0f 84 b7 00 00 00', '83 f8 04 0f 83 b7 00 00 00'),         # the own card
    # Four bytes on the stack by manager slot: the game stops the process at slot 4; this goes on to the next slot.
    (0x146C3D5, '0f 83 30 0f 00 00', '0f 83 d9 02 00 00'),
    (0x1291BEF, '83 f9 ff 74 11', '83 f9 04 73 11'),                                 # the own clan pair: none (NULL) for 4 to 7
    (0x1291C7F, '83 f9 ff 74 ec', '83 f9 04 73 ec'),                                 # the own clan record's second string
]
# Only in a build for eight players: what the game tests against its four. The client's own player index (below 4:
# else it stays in "joining" and never spawns), the SOS beacon's "the squad is full" and the lobby's size. A build
# for four keeps a lobby of four. (rva, bytes, new bytes)
EIGHT_SITES = [
    (0x134EF11, '83 f8 04 0f 93 c0', '83 f8 08 0f 93 c0'),                           # a client: my index is valid
    (0x1086460, '42 83 bc 02 ac 63 01 00 04', '42 83 bc 02 ac 63 01 00 08'),         # a client: my index may spawn
    (0x517FEA, '83 b8 90 63 01 00 04', '83 b8 90 63 01 00 08'),                      # SOS starts below this many peers
    (0x67A6F5, '83 b8 90 63 01 00 04', '83 b8 90 63 01 00 08'),                      # the same, second entry
    (0x67AB34, '83 b8 90 63 01 00 04', '83 b8 90 63 01 00 08'),                      # SOS ends at this many peers
    (0xB5EBC2, '83 b8 90 63 01 00 04', '83 b8 90 63 01 00 08'),                      # the same when a peer is added
    (0x66CA22, '83 f8 04', '83 f8 08'),                                              # SOS can be called below this many
    # The lobby closes (Steam: not joinable, PlayFab: private) once these many host slots are taken.
    (0x108AA56, '83 f9 04', '83 f9 08'),
    # The lobby takes eight: the Steam lobby's size at create, the joiner's reset of it, a third copy without a caller.
    (0x10919D1, 'b9 04 00 00 00', 'b9 08 00 00 00'),
    (0x108F108, 'c7 86 90 1b 00 00 04 00 00 00', 'c7 86 90 1b 00 00 08 00 00 00'),
    (0x1091D68, 'c7 83 90 1b 00 00 04 00 00 00', 'c7 83 90 1b 00 00 08 00 00 00'),
    (0x133B272, '41 b8 04 00 00 00', '41 b8 08 00 00 00'),                           # lobby search: members up to 8 less the party
    (0x12CB9BA, '74 22', 'eb 22'),       # the host's friends-list post keeps the session's count, no re-post capped at 4
    (0x1338699, 'c7 84 24 b0 00 00 00 04 00 00 00', 'c7 84 24 b0 00 00 00 08 00 00 00'),   # the lobby's size the UI shows
    (0x1347AC0, '83 f8 03', '83 f8 07'),         # the mission result for player counts 1 to 8 (lea eax,[rbx-1]; cmp; ja)
]
# The lobby's member lists (moved by the session step) take eight ids. Without the move eight would overrun the lobby
# object, so these are counts: the session step's sites are all in before any of them. (rva, bytes, new bytes)
MIRROR_CAPS = [
    (0x1093CB0, '41 b8 04 00 00 00', '41 b8 08 00 00 00'),                           # PlayFab members copy
    (0x1093D1E, '41 b8 04 00 00 00', '41 b8 08 00 00 00'),                           # Steam members copy
    (0x1093C91, '83 f8 04', '83 f8 08'),                                              # LAN count clamp
    (0x1093C96, 'c7 87 b0 1b 00 00 04 00 00 00', 'c7 87 b0 1b 00 00 08 00 00 00'),
    (0x1092E78, '41 b8 04 00 00 00', '41 b8 08 00 00 00'),                           # a copy of the fill without a caller
    (0x1092ECB, '83 f8 04', '83 f8 08'),
    (0x1092ED0, 'c7 83 b0 1b 00 00 04 00 00 00', 'c7 83 b0 1b 00 00 08 00 00 00'),
    (0x1092F31, '41 b8 04 00 00 00', '41 b8 08 00 00 00'),
]
# With the cave above: the friends post takes numLobbyMembers from the members, no longer from the entries.
RECORD_SITES = [(0x12CB379, '44 8b 87 d0 05 00 00', '44 8b 87 48 03 00 00')]
# The loadout screen for eight: its sites and caves, and the guards of the build for four they replace.
LOADOUT_SITES, LOADOUT_CAVES = loadout_sites.build(dump)
# The guards of tables that a build for eight gives eight entries: the mission summary's rows here (group 9), the map's
# cursors and the name tags in co_ui.c, which keeps them at four until the tables move. Here they are the build for
# four's.
GUARDS += summary_sites.GUARD4 + map_sites.GUARD4 + tag_sites.GUARD4
# A stack fix gives room where a guard dropped the entry: the guard goes.
assert set(stack_sites.REPLACES) <= {rva for rva, _, _ in GUARDS}
GUARDS = [g for g in GUARDS if g[0] not in stack_sites.REPLACES]
GUARD_SITES += map_sites.GUARD4_SITES
OFF = set(loadout_sites.MOD_OFF) | {rva for rva, _, _ in summary_sites.GUARD4 + map_sites.GUARD4 + map_sites.GUARD4_SITES + tag_sites.GUARD4}
assert OFF <= {rva for rva, _, _ in GUARD_SITES} | {rva for rva, _, _ in GUARDS}
rows_by_group = ((G_GUARD, GUARD_SITES), (G_EIGHT, EIGHT_SITES), (G_COUNT, MIRROR_CAPS), (G_COUNT, RECORD_SITES),
                 (G_LOADOUT, [(r, o, n) for r, o, n, _ in LOADOUT_SITES]), (G_SYNCED, [(r, o, n) for r, o, n, _ in loadout_sites.SYNCED_SITES]),
                 (G_SUMMARY, [(r, o, n) for r, o, n, _ in summary_sites.INPLACE]),
                 (G_STACK, [(r, o, n) for r, o, n, _, eight in stack_sites.INPLACE if not eight]),
                 (G_STACK8, [(r, o, n) for r, o, n, _, eight in stack_sites.INPLACE if eight]))
for group, rows in rows_by_group:
    for rva, old, new in rows:
        old = bytes.fromhex(old); new = bytes.fromhex(new)
        assert len(old) == len(new) <= 16 and dump[rva:rva + len(old)] == old and skip_of(rva, len(old)) == 0xFF, hex(rva)
        sites.append((rva, old, new, 0xFF, G_GUARD4 if group == G_GUARD and rva in OFF else group))

# Message guards. A handler copies each list of a message into a fixed buffer on its stack and trusts the count
# from the wire, which can be higher than the buffer (and is, once a session has more than four peers). A cave at
# the handler's entry asks the mod's C code whether every list fits; a message with a longer list is ignored.
# The handler gets its arguments as 16-byte entries with the value or its address at +8; a list is one entry with
# its count and one entry for each element.
# (handler, bytes the jump replaces, the same as code, where it goes on, shape: 0 one entry, n a list of at most n)
MESSAGES = [
    # rpc_sync_update_or_add_lobby: a lobby's members, four each; one list of eight.
    (0xBBD6F0, '40 55 56 41 56', 'push rbp\n push rsi\n push r14', 0xBBD6F5, [0] * 12 + [4] * 7 + [0, 0, 8, 4]),
    # rpc_mission_progression_result_received: five lists of eight, four of fifteen, four of four.
    (0xBA9440, '48 8b c4 55 56', 'mov rax, rsp\n push rbp\n push rsi', 0xBA9445, [0] + [8] * 5 + [15] * 4 + [0] * 4 + [4, 0, 4, 4, 4]),
    # rpc_sync_droppable_infinite_map_items
    (0xBBE230, '48 89 5c 24 08', 'mov qword ptr [rsp+8], rbx', 0xBBE235, [128, 4]),
    # The party list (five peers) of rpc_from_partyleader_join_lobby and rpc_from_partyleader_join_request.
    (0xBA2FC0, '48 89 5c 24 18', 'mov qword ptr [rsp+0x18], rbx', 0xBA2FC5, [0, 5]),
    (0xBA8CC0, '4c 8b dc 49 89 5b 18', 'mov r11, rsp\n mov qword ptr [r11+0x18], rbx', 0xBA8CC7, [0, 5]),
    # rpc_hostability_scores: five peers and five scores.
    (0xBAFF70, '40 53 48 83 ec 50', 'push rbx\n sub rsp, 0x50', 0xBAFF76, [5, 5]),
]
SHAPE_ROOM = 24
NET_GUARDS = [(rva, replaced, 'mov eax, %d\n CALL(%d)\n test eax, eax\n jne 1f\n %s\n jmp game+0x%X\n1: ret' % (i, MESSAGE, code, back))
              for i, (rva, replaced, code, back, _) in enumerate(MESSAGES)]
NET_GUARDS += [
    # rpc_from_host_party_join_accepted: its entry has no room for a jump, so the count is tested where it is
    # read; a longer list leaves by the handler's own end.
    (0xBAC74D, '8b 4a 18 0f 11 44 24 20', 'mov ecx, dword ptr [rdx+0x18]\n cmp ecx, 5\n ja game+0xBAC79C\n movups xmmword ptr [rsp+0x20], xmm0\n'
                                          ' jmp game+0xBAC755'),
    # The sender of a lobby's entry loops over the lobby's member count; its lists and the receiver's hold four.
    (0x1339313, '41 8b 8f d0 05 00 00', 'mov ecx, dword ptr [r15+0x5d0]\n cmp ecx, 4\n jbe 1f\n mov ecx, 4\n1: jmp game+0x133931A'),
    # The sender of the hostability scores appends a peer and a score for each ready peer it finds in slots 0 to 3;
    # both lists hold five.
    (0x108ABF2, '8b c7 8b ce 48 89 9c c5 b0 01 00 00', 'cmp edi, 5\n jae game+0x108AC24\n cmp esi, 5\n jae game+0x108AC24\n mov eax, edi\n mov ecx, esi\n'
                                                       ' mov qword ptr [rbp+rax*8+0x1b0], rbx\n jmp game+0x108ABFE'),
]
# The test option: the host's join request handler looks for the first free slot and the first free player index.
# The mod's C code does both searches from the option's number on, so one joining peer gets a slot and an index
# above 3. (r9 is loaded as the game's own search leaves it; the index search ends where the game stores the index.)
TEST_CAVES = [
    (0x108C382, '41 8b c6 48 8b ce', 'CALL(%d)\n mov r9, qword ptr [rbp-0x59]\n jmp game+0x108C3B0' % TEST_SLOT),
    (0x108C3CA, '49 8b c5 44 89 72 24', 'mov dword ptr [rdx+0x24], r14d\n CALL(%d)\n jmp game+0x108C3F7' % TEST_INDEX),
]
# Only in a build for eight players: a friend's lobby shows eight places. edx stays four, the record's inline members.
EIGHT_CAVES = [(0x103D11D, 'ba 04 00 00 00 41 89 96 4c 03 00 00', 'mov edx, 4\n mov dword ptr [r14+0x34c], 8\n jmp game+0x103D129')]
LOADOUT_CAVES = [(rva, replaced, text) for rva, replaced, text, _ in LOADOUT_CAVES]
SUMMARY_CAVES = [(rva, replaced, text) for rva, replaced, text, _, _ in summary_sites.CAVES]
SUMMARY_CAVES += [(rva, replaced, text) for rva, replaced, text, _ in summary_sites.GUARD8]
STACK_CAVES = [(rva, replaced, text) for rva, replaced, text, _, _ in stack_sites.CAVES]
cave_groups = ([G_COUNT] * len(CAVES) + [G_GUARD4 if rva in OFF else G_GUARD for rva, _, _ in GUARDS] + [G_NET] * len(NET_GUARDS) +
               [G_EIGHT] * len(EIGHT_CAVES) + [G_TEST] * len(TEST_CAVES) + [G_LOADOUT] * len(LOADOUT_CAVES) + [G_LOOT] * len(LOOT) +
               [G_SUMMARY] * len(SUMMARY_CAVES) + [G_STACK8 if eight else G_STACK for _, _, _, _, eight in stack_sites.CAVES])
CAVES += GUARDS + NET_GUARDS + EIGHT_CAVES + TEST_CAVES + LOADOUT_CAVES + LOOT + SUMMARY_CAVES + STACK_CAVES
# In place, with the cave above: the first loop's count is a copy of that constant.
sites.append((0x108D2DD, bytes.fromhex('8b ef 90'), bytes.fromhex('8d 6f 04'), 0xFF, G_COUNT))
assert dump[0x108D2DD:0x108D2E0] == bytes.fromhex('8b ef 90')
# mov esi,4; lea rbx,[rbp+0x40]; mov r14d,esi: esi is also a value that stays four, r14d the slot loop's count.
# -> push 4; pop rsi; lea rbx,[rbp+0x40]; lea r14d,[rsi+4]; nop
sites.append((0x13507E6, bytes.fromhex('be 04 00 00 00 48 8d 5d 40 44 8b f6'), bytes.fromhex('6a 04 5e 48 8d 5d 40 44 8d 76 04 90'), 0xFF, G_COUNT))
assert dump[0x13507E6:0x13507F2] == sites[-1][1]
sites.sort()

# What the mod's other steps write: no cave and no guard may share a byte with it, and no two sites here with each other.
def rows(name, part=None):
    text = open(HERE + '/../src/' + name).read()
    if part: text = text[text.index(part):]
    return [(int(a, 16), int(b)) for a, b in re.findall(r'^    \{0x([0-9A-F]+)u, (\d+),', text, re.M)]
stubs = rows('co_session_sites.h', 'kCoSessionStubs')
taken = [(rva, rva + size) for rva, size in stubs]
taken += [(rva + at, rva + at + 4) for rva, at in rows('co_session_sites.h') if (rva, at) not in stubs]
manager_caves = rows('co_manager_sites.h', 'kCoManagerCaves')
taken += [(rva, rva + size) for rva, size in manager_caves]
taken += [(rva + at, rva + at + 4) for rva, at in rows('co_manager_sites.h') + rows('co_pod_sites.h') if (rva, at) not in manager_caves]
assert len(taken) > 1400
# The build for four leaves out groups 5, 7, 9 and 11, the build for eight group 6: they may share bytes, a build may not.
own = [(rva, rva + len(old), skip != 0xFF, group) for rva, old, _, skip, group in sites] + [(rva, rva + 8, True, G_COUNT) for rva, _, _ in CLEARS]
own += [(rva, rva + len(bytes.fromhex(replaced)), False, group) for (rva, replaced, _), group in zip(CAVES, cave_groups)]
own.sort()
for left_out in ((G_LOADOUT, G_SYNCED, G_SUMMARY, G_STACK8), (G_GUARD4,)):
    build = [o for o in own if o[3] not in left_out]
    for (a, b, _, _), (c, _, _, _) in zip(build, build[1:]): assert b <= c, (hex(a), hex(c))
for a, b, shared, _ in own:
    if not shared: assert all(b <= c or d <= a for c, d in taken), hex(a)

def source(text, game):
    used = sorted({int(n) for n in re.findall(r'CALL\((\d+)\)', text)})
    src = cave_asm.at_game(re.sub(r'CALL\((\d+)\)', lambda m: CALL % int(m.group(1)), text), game) + '\n'
    for n in used: src += '.balign 8\ncallback%d: .quad 0x%x\n' % (n, 0xC0DE00000000 + n)
    return src

caves = []
for rva, replaced, text in CAVES:
    replaced = bytes.fromhex(replaced)
    assert dump[rva:rva + len(replaced)] == replaced and 5 <= len(replaced) <= 12, hex(rva)
    assert skip_of(rva, len(replaced)) == 0xFF, hex(rva)
    code, jumps = cave_asm.relocatable(lambda game: source(text, game), hex(rva))
    calls = []
    for m in re.finditer(rb'(.)\x00\x00\x00\xde\xc0\x00\x00', code, re.S):
        if m.start() % 8 == 0: calls.append((m.start(), m.group(1)[0]))
    assert len(calls) == len(re.findall(r'CALL\(', text)), hex(rva)
    caves.append((rva, replaced, code, jumps, calls))

def c_bytes(b): return ', '.join('0x%02X' % x for x in b)
h = ['/* Generated by scripts/gen_count_sites.py from scripts/sites/count_sites.tsv; do not edit. */\n']
h.append('static const CoCountSite kCoCountSites[] = {\n')
for rva, old, new, skip, group in sites:
    h.append('    {0x%Xu, %d, %d, %d, {%s}, {%s}},\n' % (rva, len(old), skip, group, c_bytes(old), c_bytes(new)))
h.append('};\nstatic const CoCountClear kCoCountClears[] = {\n')
for rva, mov, land in CLEARS: h.append('    {0x%Xu, 0x%02X, 0x%02X},\n' % (rva, mov, land))
h.append('};\n')
blob = bytearray(); rows = []
for (rva, replaced, code, jumps, calls), group in zip(caves, cave_groups):
    assert len(jumps) <= 3 and len(calls) <= 1
    rows.append('    {0x%Xu, %d, %d, {%s}, %d, %d, {%s}, %d, %d},\n' % (rva, len(replaced), group, c_bytes(replaced), len(blob), len(code),
                ', '.join(str(j) for j in jumps + [0] * (3 - len(jumps))), calls[0][0] if calls else 0, calls[0][1] if calls else 0xFF))
    blob += code
h.append('static const uint8_t kCoCountCode[] = {\n')
for i in range(0, len(blob), 24): h.append('    ' + c_bytes(blob[i:i + 24]) + ',\n')
h.append('};\nstatic const CoCountCave kCoCountCaves[] = {\n'); h += rows
h.append('};\n/* A message handler\'s arguments in order: 0 one entry, n a list of at most n entries, 0xFF the end. */\n')
h.append('static const uint8_t kCoMessageShapes[][%d] = {\n' % SHAPE_ROOM)
for _, _, _, _, shape in MESSAGES:
    assert len(shape) < SHAPE_ROOM and all(0 <= n < 0xFF for n in shape)
    h.append('    {%s},\n' % ', '.join(str(n) for n in shape + [0xFF]))
h.append('};\nstatic const uint32_t kCoMessageHandlers[] = {%s};\n' % ', '.join('0x%Xu' % m[0] for m in MESSAGES))
# The per-peer loadout table the mod moves before group 7 is written (loadout_sites.SYNCED_MOVE).
m = loadout_sites.SYNCED_MOVE
assert m['new_size'] == 2 * m['old_size'] == 8 * m['entry'] and len(m['registry_lists']) == len(m['registry_rooms']) == 3
h.append('#define CO_SYNCED_POINTER 0x%Xu\n#define CO_SYNCED_ENGINE 0x%Xu\n#define CO_SYNCED_MEMBER 0x%Xu\n#define CO_SYNCED_REGISTRY 0x%Xu\n'
         % (m['pointer'], m['engine'], m['member'], m['registry']))
h.append('#define CO_SYNCED_TYPE 0x%Xu\n#define CO_SYNCED_OLD_SIZE 0x%Xu\n#define CO_SYNCED_NEW_SIZE 0x%Xu\n' % (m['registry_type'], m['old_size'], m['new_size']))
h.append('static const uint32_t kCoSyncedLists[] = {%s};\n' % ', '.join('0x%Xu' % x for x in m['registry_lists']))
h.append('static const uint32_t kCoSyncedRooms[] = {%s};\n' % ', '.join('%du' % x for x in m['registry_rooms']))
assert len(cave_groups) == len(caves) == len(CAVES)
open(out, 'w').writelines(h)
open(HERE + '/../tests/co_stack_code.h', 'w').writelines(stack_sites.test_header(dump))
print('%d sites, %d clears, %d caves (%d bytes)' % (len(sites), len(CLEARS), len(caves), len(blob)))
for group, name in enumerate(('counts', 'guards', 'message guards', 'eight players only', 'test option only', 'loadout (eight only)',
                               'guards (four only)', 'loadout table (eight)', 'loot rows', 'summary (eight only)', 'stack arrays',
                               'stack arrays (eight)')):
    print('  %-20s %3d sites, %2d caves' % (name, sum(s[4] == group for s in sites), cave_groups.count(group)))
