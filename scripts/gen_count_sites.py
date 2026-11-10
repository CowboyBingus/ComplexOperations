#!/usr/bin/env python3
# Writes src/co_count_sites.h: the code sites that make the session lists count to eight (game build 25480438), from
# scripts/sites/count_sites.tsv, the caves below and a dump of the game's code:
#   scripts/gen_count_sites.py <game.dll image dump> [out]
# Three kinds of site:
#   - one instruction changed in place (the tsv);
#   - a flag clear that grows from four bytes to eight in the room of two instructions (CLEARS);
#   - a cave: a jump replaces some instructions and leads to code of the mod's, which may call into the mod's C code
#     and jumps back (CAVES). The caves are assembled here with GNU as; the header holds their bytes.
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
# In place, with the cave above: the first loop's count is a copy of that constant.
sites.append((0x108D2DD, bytes.fromhex('8b ef 90'), bytes.fromhex('8d 6f 04'), 0xFF))
assert dump[0x108D2DD:0x108D2E0] == bytes.fromhex('8b ef 90')
# mov esi,4; lea rbx,[rbp+0x40]; mov r14d,esi: esi is also a value that stays four, r14d the slot loop's count.
# -> push 4; pop rsi; lea rbx,[rbp+0x40]; lea r14d,[rsi+4]; nop
sites.append((0x13507E6, bytes.fromhex('be 04 00 00 00 48 8d 5d 40 44 8b f6'), bytes.fromhex('6a 04 5e 48 8d 5d 40 44 8d 76 04 90'), 0xFF))
assert dump[0x13507E6:0x13507F2] == sites[-1][1]
sites.sort()

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
h.append('};\nstatic const CoCountCave kCoCountCaves[] = {\n'); h += rows; h.append('};\n')
open(out, 'w').writelines(h)
print('%d sites, %d clears, %d caves (%d bytes)' % (len(sites), len(CLEARS), len(caves), len(blob)))
