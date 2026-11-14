# The mission summary screen for eight players (game build 25480438): the sites scripts/gen_count_sites.py reads (group
# 9, eight only). SS, the type-10 screen, is allocated at 0x8A540 bytes by the factory and freed with no size or
# destructor. Two row blocks R1 = SS+0x8 and R2 = SS+0x1148 (R2 a copy of R1): a qword, four rows of 0x24C by player
# index, a tail to +0x1140. The widget W = SS+0x2298 holds R1/R2 pointers at +0x88268/+0x88270, four cards of 0x125C8
# at +0x3B480 and four nav entries of 0x80 at +0x88068 (W+0x88008 = 4 is a layout mode, not a count).
# The allocation grows and eight-entry copies go in the new tail; the old space stays unused. A row block keeps its
# layout with eight rows, so its tail moves 0x930 inside the block. Formats as in map_sites.py; all static.
import struct


def le32(v): return struct.pack('<I', v & 0xFFFFFFFF).hex()

S_OLD = 0x8A540
S_RSZ = 0x8 + 8 * 0x24C + 0x808               # 0x1A70
S_NR1 = 0x8A548                               # = 8 mod 16 like SS+0x8
S_NR2 = S_NR1 + S_RSZ                         # 0x8BFB8, = 8 mod 16 like SS+0x1148
S_NC = S_NR2 + S_RSZ                          # 0x8DA28, = 8 mod 16 like SS+0x3D718
S_NNAV = (S_NC + 8 * 0x125C8 + 15) & ~15      # 0x120870, = 0 mod 16 like SS+0x8A300
S_NEW = S_NNAV + 8 * 0x80                     # 0x120C70
S_W = 0x2298
S_NCW = S_NC - S_W                            # new card 0, W-relative (old 0x3B480)
S_TAIL = 0x930                                # tail shift inside a row block
assert S_NR1 >= S_OLD and S_NC % 16 == 0x3D718 % 16


def _d(op, v, tail=''): return op + le32(v) + tail


INPLACE = [
    (0x14989FB, 'ba40a50800', _d('ba', S_NEW), 'factory: alloc size'),
    (0x1498A30, '41b8b0820800', _d('41b8', S_NEW - 0x2290), 'factory: memset size of SS+0x2290..end'),
    (0x1498A3B, 'bd04000000', 'bd08000000', 'factory: rows in R1 and R2 (both loops)'),
    (0x1498A7B, '488d9350110000', _d('488d93', S_NR2 + 8), 'factory: R2 rows'),
    (0x14A12D6, '41ba04000000', '41ba08000000', 'ctor 14a12d0 (inlined in the factory): rows'),
    (0x14A131D, '488d9150110000', _d('488d91', S_NR2 + 8), 'ctor 14a12d0: R2 rows'),
    (0x14A1380, '41b904000000', '41b908000000', 'row-block ctor 14a1380 (no caller in code): rows'),
    # R2 pointers
    (0x14648BE, '488d8e48110000', _d('488d8e', S_NR2), '1464850: R2 (memcpy dst)'),
    (0x14648E4, '4c8d8648110000', _d('4c8d86', S_NR2), '1464850: R2 to W init'),
    (0x1464B89, '4d8d8648110000', _d('4d8d86', S_NR2), '1464b50: R2 to W init'),
    (0x1464DFB, '488d8f48110000', _d('488d8f', S_NR2), '1464dc0: R2 (memcpy dst)'),
    (0x1497AD2, '488d8f48110000', _d('488d8f', S_NR2), '1497920: R2 (memcpy dst)'),
    (0x14648B1, '41b840110000', _d('41b8', S_RSZ), '1464850: memcpy R1 -> R2 size'),
    # R1 + 0x1138 from SS (SS+0x1140)
    (0x1464965, 'c6864011000001', _d('c686', S_NR1 + 0x1138 + S_TAIL, '01'), '1464850: R1 flag +0x1138'),
    (0x1464C0B, '41c6864011000001', _d('41c686', S_NR1 + 0x1138 + S_TAIL, '01'), '1464b50: R1 flag +0x1138'),
    (0x1464EFC, 'c6874011000001', _d('c687', S_NR1 + 0x1138 + S_TAIL, '01'), '1464dc0: R1 flag +0x1138'),
    (0x1464FD9, 'c6834011000001', _d('c683', S_NR1 + 0x1138 + S_TAIL, '01'), '1464fc0: R1 flag +0x1138'),
    (0x1497BC9, 'c6874011000001', _d('c687', S_NR1 + 0x1138 + S_TAIL, '01'), '1497920: R1 flag +0x1138'),
    # row-block tail through a block pointer (+0x930)
    (0x1464E0E, '80b83911000000', _d('80b8', 0x1139 + S_TAIL, '00'), '1464dc0: [R1+0x1139]'),
    (0x1464E38, 'c6803911000000', _d('c680', 0x1139 + S_TAIL, '00'), '1464dc0: [R1+0x1139]'),
    (0x1497AE5, '80b83911000000', _d('80b8', 0x1139 + S_TAIL, '00'), '1497920: [R1+0x1139]'),
    (0x1497B03, 'c6803911000000', _d('c680', 0x1139 + S_TAIL, '00'), '1497920: [R1+0x1139]'),
    (0x199373B, 'c6803911000001', _d('c680', 0x1139 + S_TAIL, '01'), '1993620: [[W+0x88268]+0x1139]'),
    (0x1993206, '0fb68838110000', _d('0fb688', 0x1138 + S_TAIL), '19931e0: [R1+0x1138]'),
    (0x1993214, '3a8838110000', _d('3a88', 0x1138 + S_TAIL), '19931e0: [R2+0x1138]'),
    (0x19937E7, '0fb69038110000', _d('0fb690', 0x1138 + S_TAIL), '19937e0: [R1+0x1138]'),
    (0x19937F5, '3a9038110000', _d('3a90', 0x1138 + S_TAIL), '19937e0: [R2+0x1138]'),
    (0x1993819, '389038110000', _d('3890', 0x1138 + S_TAIL), '1993810: [R1+0x1138]'),
    (0x19941D2, '44889038090000', _d('448890', 0x938 + S_TAIL), '1993e30: [R1+0x938]'),
    (0x1992A5F, '4d8d8538090000', _d('4d8d85', 0x938 + S_TAIL), '1992660: R1 tail to W+0x84BA0'),
    (0x1992A66, '4981c138090000', _d('4981c1', 0x938 + S_TAIL), '1992660: R2 tail to W+0x84BA0'),
    # The survey link's text (0x800 bytes at the tail's start): at its old place it would be rows 4 to 7, row 4's count
    # four of its letters, and that row's card builds a list of that length past its 24 widgets.
    (0x199443A, '4881c638090000', _d('4881c6', 0x938 + S_TAIL), '1994400: R1 tail, the link text written'),
    (0x1993DD8, '80b83911000000', _d('80b8', 0x1139 + S_TAIL, '00'), '1993dd0 (by vtable): [R1+0x1139]'),
    (0x1993E02, 'c6803911000000', _d('c680', 0x1139 + S_TAIL, '00'), '1993dd0 (by vtable): [R1+0x1139]'),
    # cards
    (0x1464983, '418984315cfc0400', _d('41898431', S_NC + 0x12544), '1464850: card i field +0x12544'),
    (0x1464A49, '4981f920970400', _d('4981f9', 8 * 0x125C8), '1464850: rows/cards loop end'),
    (0x1464C33, '4389840e5cfc0400', _d('4389840e', S_NC + 0x12544), '1464b50: card i field +0x12544'),
    (0x1464CF9, '4981f920970400', _d('4981f9', 8 * 0x125C8), '1464b50: rows/cards loop end'),
    (0x19929C6, '4c8dbe80b40300', _d('4c8dbe', S_NCW), '1992660 W init: cards'),
    (0x19929D7, 'bd04000000', 'bd08000000', '1992660: rows/cards loop'),
    (0x1992EA9, '488d9e8c800800', _d('488d9e', S_NNAV - S_W + 0x24), '1992660: nav entries'),
    (0x1992FA7, '4883fd04', '4883fd08', '1992660: nav entries loop'),
    (0x199306E, '498dbf62030400', _d('498dbf', S_NCW + 0x4EE2), '1993010: cards'),
    (0x199307A, '41be04000000', '41be08000000', '1993010: cards loop'),
    (0x199345A, '488dbb80b40300', _d('488dbb', S_NCW), '19931e0: cards'),
    (0x1993461, 'be04000000', 'be08000000', '19931e0: cards loop'),
    (0x19936A0, '488d99a0fb0300', _d('488d99', S_NCW + 0x4720), '1993620: cards'),
    (0x1993774, '4883fe04', '4883fe08', '1993620: cards loop'),
    (0x199379F, '83bfb8d9040003', _d('83bf', S_NCW + 0x12538, '03'), '1993620: card 0 field +0x12538'),
    (0x199351A, '4489840ac4d90400', _d('4489840a', S_NCW + 0x12544), '1993510 (no caller in code): card i'),
    (0x14A145A, 'bd04000000', 'bd08000000', 'W ctor 14a13e0: cards loop'),
    (0x14A145F, '488d9e00fe0300', _d('488d9e', S_NCW + 0x4980), 'W ctor 14a13e0: cards'),
]
CAVES = [
    (0x14648DD, '4c8d760849' + '8bd6', 'lea r14, [rsi+0x%x]\n mov rdx, r14\n jmp game+0x14648E4' % S_NR1, '1464850: R1', []),
    (0x1464B82, '498d7608' + '488bd6', 'lea rsi, [r14+0x%x]\n mov rdx, rsi\n jmp game+0x1464B89' % S_NR1, '1464b50: R1', []),
    (0x1464DF1, '488d5708' + '41b840110000', 'lea rdx, [rdi+0x%x]\n mov r8d, 0x%x\n jmp game+0x1464DFB' % (S_NR1, S_RSZ),
     '1464dc0: R1 and memcpy size', []),
    (0x1497AC8, '488d5708' + '41b840110000', 'lea rdx, [rdi+0x%x]\n mov r8d, 0x%x\n jmp game+0x1497AD2' % (S_NR1, S_RSZ),
     '1497920: R1 and memcpy size', []),
    (0x1464934, '4c8d4614' + '4533d2', 'lea r8, [rsi+0x%x]\n xor r10d, r10d\n jmp game+0x146493B' % (S_NR1 + 0xC),
     '1464850: R1 row 0 + 0xC', []),
    (0x1464BDA, '4d8d4614' + '4533d2', 'lea r8, [r14+0x%x]\n xor r10d, r10d\n jmp game+0x1464BE1' % (S_NR1 + 0xC),
     '1464b50: R1 row 0 + 0xC', []),
    (0x1498A40, '488d5310' + '448bc5', 'lea rdx, [rbx+0x%x]\n mov r8d, ebp\n jmp game+0x1498A47' % (S_NR1 + 8),
     'factory: R1 rows', []),
    (0x14A12DC, '4c8d4110' + '458bca', 'lea r8, [rcx+0x%x]\n mov r9d, r10d\n jmp game+0x14A12E3' % (S_NR1 + 8),
     'ctor 14a12d0: R1 rows', []),
    (0x1993578, '83beb8d9040001',
     'lea r10, [rsi+0x%x]\n mov r11d, 8\n1: cmp dword ptr [r10], 1\n je 2f\n mov dword ptr [r10+0xc], ebp\n'
     '2: add r10, 0x125c8\n dec r11d\n jne 1b\n jmp game+0x19935B4' % (S_NCW + 0x12538),
     '1993530: the four unrolled card stores as a loop of 8 (skips 0x199357F..0x19935B3)', []),
    (0x1993901, '83bbb8d9040001',
     'lea r10, [rbx+0x%x]\n mov r11d, 8\n1: cmp dword ptr [r10], 1\n je 2f\n mov dword ptr [r10+0xc], 3\n'
     '2: add r10, 0x125c8\n dec r11d\n jne 1b\n jmp game+0x19938B7' % (S_NCW + 0x12538),
     '1993840: the four unrolled card stores as a loop of 8 (skips 0x1993908..0x1993955)', []),
]
# The guards while the rows are four (the build for four): a player index of 4 or more skips the row. The "best" list
# on the stack holds four entries, its cookie right behind.
GUARD4 = [
    (0x1993F38, 'ff 44 24 24 33 d2', 'inc dword ptr [rsp+0x24]\n cmp eax, 4\n jae game+0x19941E3\n xor edx, edx\n jmp game+0x1993F3E'),
    (0x199422C, '48 69 d0 4c 02 00 00', 'cmp eax, 4\n jae game+0x19943B7\n imul rdx, rax, 0x24c\n jmp game+0x1994233'),
    (0x19942AE, '8b d0 48 69 c2 4c 02 00 00', 'cmp eax, 4\n jae game+0x19942D4\n mov edx, eax\n imul rax, rdx, 0x24c\n jmp game+0x19942B7'),
    (0x199430E, '8b c8 48 69 d1 4c 02 00 00', 'cmp eax, 4\n jae game+0x1994333\n cmp ebx, 4\n jae game+0x1994333\n mov ecx, eax\n imul rdx, rcx, 0x24c\n jmp game+0x1994317'),
]
# With eight rows: bound 8, and the stack list keeps its bound 4. All four still filter -1 from 0x606820 (unsigned jae).
GUARD8 = [
    (0x1993F38, 'ff442424' + '33d2', 'inc dword ptr [rsp+0x24]\n cmp eax, 8\n jae game+0x19941E3\n xor edx, edx\n jmp game+0x1993F3E', '1993e30 row write'),
    (0x199422C, '4869d04c020000', 'cmp eax, 8\n jae game+0x19943B7\n imul rdx, rax, 0x24c\n jmp game+0x1994233', '1993e30 own row'),
    (0x19942AE, '8bd0' + '4869c24c020000', 'cmp eax, 8\n jae game+0x19942D4\n mov edx, eax\n imul rax, rdx, 0x24c\n jmp game+0x19942B7', '1993e30 best scan'),
    (0x199430E, '8bc8' + '4869d14c020000', 'cmp eax, 8\n jae game+0x1994333\n cmp ebx, 4\n jae game+0x1994333\n mov ecx, eax\n imul rdx, rcx, 0x24c\n jmp game+0x1994317', '1993e30 best list (stack list stays 4)'),
]


# A row block's tail by a displacement from the block: every one in the screen's code is a site above, or the text
# lands in rows 4 to 7. 0x199070F reads another object (the global at game+0x33264F8).
TAIL_CODE = ((0x1464850, 0x1465000), (0x1497920, 0x1498400), (0x198F060, 0x1995000))
TAIL_OTHER = {0x199070F}


def check(dump):
    done = {rva + k for rva, old, _, _ in INPLACE for k in range(len(old) // 2)}
    for lo, hi in TAIL_CODE:
        for v in (0x938, 0x1138, 0x1139):
            at = dump.find(struct.pack('<I', v), lo, hi)
            while at >= 0:
                assert at in done or at in TAIL_OTHER, 'summary: the tail at +0x%X is not moved at game+0x%X' % (v, at)
                at = dump.find(struct.pack('<I', v), at + 1, hi)
