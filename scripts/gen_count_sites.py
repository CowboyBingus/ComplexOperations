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
# An instruction of a moved array carries a distance the session step rewrites; those four bytes are not compared.
import os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
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
    sites.append((rva, old, new, skip))

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
# Callbacks, by number (the same order as kCountCallbacks in co_counts.c).
FIND_RSI, FIND_R11, GATE, VALUES, LAST_SLOT = 0, 1, 2, 3, 4
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
]
# Guards: tables of the game's with four entries, indexed by a player index or a player slot with no check. An index
# of four or more skips the access, so players 5 to 8 are missing from that table instead of overrunning it; for 0 to
# 3 nothing changes. Each cave runs the instructions its jump replaced.
GUARDS = [
    # Score manager, +0xC4A0 by player index and +0xC440 by score slot (both stay in the game's part of the manager).
    (0x6311D4, '3b c3 0f 84 2f 01 00 00', 'cmp eax, 4\n jae game+0x63130B\n jmp game+0x6311DC'),
    (0x631514, '8b c1 48 8d 0c 40', 'mov eax, ecx\n lea rcx, [rax+rax*2]\n cmp eax, 4\n jae game+0x63155B\n jmp game+0x63151A'),
    (0x631693, '41 3b ae 0c 7d 03 00', 'cmp ebp, 4\n jae game+0x6316A0\n cmp ebp, dword ptr [r14+0x37d0c]\n jmp game+0x63169A'),
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
    # HUD: the samples panel's four avatars, the in-world tags' four rows.
    (0x18437DD, '42 89 44 ad 40', 'cmp r13d, 4\n jae 1f\n mov dword ptr [rbp+r13*4+0x40], eax\n1: jmp game+0x18437E2'),
    (0x18AF049, '41 83 bd 84 00 00 00 00', 'cmp dword ptr [rsp+0x20], 4\n jae game+0x18AEDB9\n cmp dword ptr [r13+0x84], 0\n jmp game+0x18AF051'),
    # Mission summary: four rows of 0x24C by player index, and four "best" entries on the stack before the cookie.
    (0x1993F38, 'ff 44 24 24 33 d2', 'inc dword ptr [rsp+0x24]\n cmp eax, 4\n jae game+0x19941E3\n xor edx, edx\n jmp game+0x1993F3E'),
    (0x199422C, '48 69 d0 4c 02 00 00', 'cmp eax, 4\n jae game+0x19943B7\n imul rdx, rax, 0x24c\n jmp game+0x1994233'),
    (0x19942AE, '8b d0 48 69 c2 4c 02 00 00', 'cmp eax, 4\n jae game+0x19942D4\n mov edx, eax\n imul rax, rdx, 0x24c\n jmp game+0x19942B7'),
    (0x199430E, '8b c8 48 69 d1 4c 02 00 00', 'cmp eax, 4\n jae game+0x1994333\n cmp ebx, 4\n jae game+0x1994333\n mov ecx, eax\n imul rdx, rcx, 0x24c\n jmp game+0x1994317'),
    # Galactic map cursors, four of 0x298 by player index. Message handlers (two copies each) leave by their own
    # empty-list exit; the member versions return at once: the call they end in gives no value either.
    (0xB8CAE3, '45 39 be 20 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CC50\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xB8CAEA'),
    (0xBACED1, '45 39 be 20 5e 00 00', 'cmp ebp, 4\n jae game+0xBAD04D\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xBACED8'),
    (0xB8CC8A, '39 be 38 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CCF6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xB8CC90'),
    (0xBB494E, '39 be 38 5e 00 00', 'cmp ebp, 4\n jae game+0xBB49B6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xBB4954'),
    (0xB8CDDA, '39 be 68 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CE6E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xB8CDE0'),
    (0xBB1B9E, '39 be 68 5e 00 00', 'cmp ebp, 4\n jae game+0xBB1C2E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xBB1BA4'),
    (0x72F700, '40 53 48 83 ec 20', 'cmp edx, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F706\n1: ret'),
    (0x72F780, '40 53 48 83 ec 20', 'cmp edx, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F786\n1: ret'),
    (0x730190, '40 53 48 83 ec 20', 'cmp r8d, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x730196\n1: ret'),
    # The map's every-frame update numbers the other players into four dwords on the stack; the numbers index tables
    # of three, so a fourth other player gets none.
    (0x142EB70, '8b 02 41 3b c7 74 0a', 'mov eax, dword ptr [rdx]\n cmp eax, r15d\n je 1f\n cmp eax, 4\n jae 1f\n cmp r8d, 3\n jae 1f\n'
                                        ' jmp game+0x142EB77\n1: jmp game+0x142EB81'),
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
]
# Guards in place: the game tests the index against -1 ("none") only. cmp reg,-1; je becomes cmp reg,4; jae, which
# sends 4 to 7 the way "none" goes and leaves -1 going there. (rva, bytes, new bytes)
GUARD_SITES = [
    (0x142EA5C, '41 83 ff ff 0f 84 9f 00 00 00', '41 83 ff 04 0f 83 9f 00 00 00'),   # map update: the own cursor
    (0x142EF50, '83 f9 ff 0f 84 9f 01 00 00', '83 f9 04 0f 83 9f 01 00 00'),         # map camera: the own cursor
    (0x14670B8, '83 f9 ff 0f 84 dc fe ff ff', '83 f9 04 0f 83 dc fe ff ff'),         # loadout: the own index at setup
    (0x14697C1, '83 f8 ff 0f 84 9d 00 00 00', '83 f8 04 0f 83 9d 00 00 00'),         # and found later
    (0x146C46A, '41 83 fd ff 0f 84 38 02 00 00', '41 83 fd 04 0f 83 38 02 00 00'),   # a player's card
    (0x14716AD, '83 f8 ff 0f 84 b7 00 00 00', '83 f8 04 0f 83 b7 00 00 00'),         # the own card
    # Four bytes on the stack by manager slot: the game stops the process at slot 4; this goes on to the next slot.
    (0x146C3D5, '0f 83 30 0f 00 00', '0f 83 d9 02 00 00'),
]
for rva, old, new in GUARD_SITES:
    old = bytes.fromhex(old); new = bytes.fromhex(new)
    assert len(old) == len(new) <= 12 and dump[rva:rva + len(old)] == old and skip_of(rva, len(old)) == 0xFF, hex(rva)
    sites.append((rva, old, new, 0xFF))
COUNT_CAVES = len(CAVES)
CAVES += GUARDS
# In place, with the cave above: the first loop's count is a copy of that constant.
sites.append((0x108D2DD, bytes.fromhex('8b ef 90'), bytes.fromhex('8d 6f 04'), 0xFF))
assert dump[0x108D2DD:0x108D2E0] == bytes.fromhex('8b ef 90')
# mov esi,4; lea rbx,[rbp+0x40]; mov r14d,esi: esi is also a value that stays four, r14d the slot loop's count.
# -> push 4; pop rsi; lea rbx,[rbp+0x40]; lea r14d,[rsi+4]; nop
sites.append((0x13507E6, bytes.fromhex('be 04 00 00 00 48 8d 5d 40 44 8b f6'), bytes.fromhex('6a 04 5e 48 8d 5d 40 44 8d 76 04 90'), 0xFF))
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
taken += [(rva + at, rva + at + 4) for rva, at in rows('co_manager_sites.h') + rows('co_pod_sites.h')]
assert len(taken) > 1400
own = [(rva, rva + len(old), skip != 0xFF) for rva, old, _, skip in sites] + [(rva, rva + 8, True) for rva, _, _ in CLEARS]
own += [(rva, rva + len(bytes.fromhex(replaced)), False) for rva, replaced, _ in CAVES]
own.sort()
for (a, b, _), (c, _, _) in zip(own, own[1:]): assert b <= c, (hex(a), hex(c))
for a, b, shared in own:
    if not shared: assert all(b <= c or d <= a for c, d in taken), hex(a)

def assemble(text, game):
    src = '.intel_syntax noprefix\n.text\n'
    used = sorted({int(n) for n in re.findall(r'CALL\((\d+)\)', text)})
    text = re.sub(r'CALL\((\d+)\)', lambda m: CALL % int(m.group(1)), text)
    text = re.sub(r'game\+0x([0-9A-Fa-f]+)', lambda m: '0x%x' % (game + int(m.group(1), 16)), text)
    src += text + '\n'
    for n in used: src += '.balign 8\ncallback%d: .quad 0x%x\n' % (n, 0xC0DE00000000 + n)
    with tempfile.TemporaryDirectory() as tmp:
        open(tmp + '/c.s', 'w').write(src)
        subprocess.run(['as', '--64', '-o', tmp + '/c.o', tmp + '/c.s'], check=True)
        subprocess.run(['ld', '-Ttext=0', '-o', tmp + '/c.elf', tmp + '/c.o'], check=True, stderr=subprocess.DEVNULL)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', tmp + '/c.elf', tmp + '/c.bin'], check=True)
        return open(tmp + '/c.bin', 'rb').read()

caves = []
for rva, replaced, text in CAVES:
    replaced = bytes.fromhex(replaced)
    assert dump[rva:rva + len(replaced)] == replaced and 5 <= len(replaced) <= 12, hex(rva)
    assert skip_of(rva, len(replaced)) == 0xFF, hex(rva)
    # Assembled twice with the game at two addresses: the bytes that differ are the jumps into the game.
    a = assemble(text, 0x10000000); b = assemble(text, 0x20000000)
    assert len(a) == len(b)
    jumps = []; i = 0
    while i < len(a):
        if a[i] != b[i]:
            at = i - 3 if a[i - 3:i] == b[i - 3:i] and i >= 3 and struct.unpack_from('<i', b, i - 3)[0] - struct.unpack_from('<i', a, i - 3)[0] == 0x10000000 else None
            assert at is not None, (hex(rva), i)
            jumps.append(at); i = at + 4
        else: i += 1
    code = bytearray(a)
    for at in jumps: struct.pack_into('<i', code, at, struct.unpack_from('<i', a, at)[0] - 0x10000000)
    calls = []
    for m in re.finditer(rb'(.)\x00\x00\x00\xde\xc0\x00\x00', bytes(code)):
        if m.start() % 8 == 0: calls.append((m.start(), m.group(1)[0]))
    assert len(calls) == len(re.findall(r'CALL\(', text)), hex(rva)
    caves.append((rva, replaced, bytes(code), jumps, calls))

def c_bytes(b): return ', '.join('0x%02X' % x for x in b)
h = ['/* Generated by scripts/gen_count_sites.py from scripts/sites/count_sites.tsv; do not edit. */\n']
h.append('static const CoCountSite kCoCountSites[] = {\n')
for rva, old, new, skip in sites:
    h.append('    {0x%Xu, %d, %d, {%s}, {%s}},\n' % (rva, len(old), skip, c_bytes(old), c_bytes(new)))
h.append('};\nstatic const CoCountClear kCoCountClears[] = {\n')
for rva, mov, land in CLEARS: h.append('    {0x%Xu, 0x%02X, 0x%02X},\n' % (rva, mov, land))
h.append('};\n')
blob = bytearray(); rows = []
for rva, replaced, code, jumps, calls in caves:
    assert len(jumps) <= 3 and len(calls) <= 1
    rows.append('    {0x%Xu, %d, {%s}, %d, %d, {%s}, %d, %d},\n' % (rva, len(replaced), c_bytes(replaced), len(blob), len(code),
                ', '.join(str(j) for j in jumps + [0] * (3 - len(jumps))), calls[0][0] if calls else 0, calls[0][1] if calls else 0xFF))
    blob += code
h.append('static const uint8_t kCoCountCode[] = {\n')
for i in range(0, len(blob), 24): h.append('    ' + c_bytes(blob[i:i + 24]) + ',\n')
h.append('};\nstatic const CoCountCave kCoCountCaves[] = {\n'); h += rows; h.append('};\n#define CO_COUNT_GUARDS %du\n#define CO_COUNT_GUARD_SITES %du\n' % (len(GUARDS), len(GUARD_SITES)))
open(out, 'w').writelines(h)
print('%d sites, %d clears, %d caves (%d bytes); of them %d guard caves, %d guard sites' % (len(sites), len(CLEARS), len(caves), len(blob), len(GUARDS), len(GUARD_SITES)))
