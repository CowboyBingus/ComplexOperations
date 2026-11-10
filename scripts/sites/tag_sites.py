# In-world name tags for eight players (game build 25480438): the sites scripts/gen_ui_sites.py reads. T, the tag
# widget at H2 + 0x1A0E28 (HUD = [0x346D538], H2 = HUD + 0x24E340), has four rows in seven arrays with T's own fields
# right behind, and T is packed inside H2, so the arrays move to a block of the mod's with eight rows each:
#   A  widget 0x110  T+0x5B460  -> NEW+0x0000   (child of T+0x59270)
#   B  widget 0x110  T+0x5B8A0  -> NEW+0x0880   (child of A_i; B_i+0x54 is the row alpha)
#   D  widget 0x158  T+0x5BCE0  -> NEW+0x1100   (child of B_i)
#   E  widget 0x158  T+0x5C240  -> NEW+0x1BC0   (child of B_i)
#   C  widget 0x480  T+0x5C7A0  -> NEW+0x2680   (child of A_i; the name text)
#   F  u8            T+0x5D9A0  -> NEW+0x4A80   (visible last frame)
#   P  12 bytes      T+0x5D9A4  -> NEW+0x4A94   (screen position and a word)
# A relocated site becomes old + D + K[region], D = NEW - (H2 + OLD). Formats as in map_sites.py.
import struct

T = 0x1A0E28                                # H2-relative address of T
OLD = T + 0x5B460                           # H2-relative address of the first row of A
SIZE = 0x4B00
ROWS = {'A': 0x5B460, 'B': 0x5B8A0, 'D': 0x5BCE0, 'E': 0x5C240, 'C': 0x5C7A0, 'F': 0x5D9A0, 'P': 0x5D9A4}
ENTRY = {'A': 0x110, 'B': 0x110, 'D': 0x158, 'E': 0x158, 'C': 0x480, 'F': 1, 'P': 0xC}
NEW = {'A': 0x0, 'B': 0x880, 'D': 0x1100, 'E': 0x1BC0, 'C': 0x2680, 'F': 0x4A80, 'P': 0x4A94}
K = {r: NEW[r] - (ROWS[r] - ROWS['A']) for r in ROWS}


def region_of(v):
    for r in ROWS:
        if ROWS[r] <= v < ROWS[r] + 4 * ENTRY[r]:
            return r
    return None


def le32(v): return struct.pack('<I', v & 0xFFFFFFFF).hex()


# Every T-relative tag offset in the image; all are in the T init 0x18A5580 and the tag update 0x18AEBC0.
REL = [
    # 18a5580: T init (H2 init 0x12E96D0 calls it once, at 0x12EA155): row loop bases
    (0x18A7015, 7, 3, 0x5D9A0, 'F'),   # lea rax,[r14+0x5d9a0]        F_0 -> [rbp-0x48]
    (0x18A7028, 7, 3, 0x5D9A4, 'P'),   # lea rax,[r14+0x5d9a4]        P_0 -> [rbp-0x50]
    (0x18A7033, 7, 3, 0x5C7A0, 'C'),   # lea rax,[r14+0x5c7a0]        C_0 -> [rsp+0x50]
    (0x18A7126, 7, 3, 0x5C240, 'E'),   # lea rax,[r14+0x5c240]        E_0 -> [rsp+0x70] (D_i = E_i - 0x560)
    (0x18A7132, 7, 3, 0x5B8A0, 'B'),   # lea rcx,[r14+0x5b8a0]        B_0 -> [rbp-0x60] (A_i = B_i - 0x440)
    # 18aebc0: tag update (only caller 0x18A86F5 in 0x18A7C60, called from the HUD update 0x12EC155)
    (0x18AEC08, 7, 3, 0x5B8A0, 'B'),   # lea rbx,[rcx+0x5b8a0]        one avatar or none: hide all B
    (0x18AEE3D, 7, 3, 0x5B460, 'A'),   # add rcx,0x5b460              after imul rcx,rax,0x110; then add rcx,r14
    (0x18AEE62, 7, 3, 0x5B8F4, 'B'),   # lea rsi,[r14+0x5b8f4]        B_0 + 0x54 (row alpha)
    (0x18AEE69, 7, 3, 0x5BCE0, 'D'),   # lea rbx,[r14+0x5bce0]        D_0 (E_i = D_i + 0x560)
    (0x18AF070, 7, 3, 0x5B8A0, 'B'),   # add rcx,0x5b8a0              after imul rcx,r12,0x110; own avatar
    (0x18AF091, 7, 3, 0x5B460, 'A'),   # lea rax,[r14+0x5b460]        + rbx (= i * 0x110)
    (0x18AF0B0, 7, 3, 0x5C7A0, 'C'),   # lea r13,[r14+0x5c7a0]        + i * 0x480 (lea r12*9, shl 7)
    (0x18AF143, 9, 5, 0x5D9A0, 'F'),   # movzx r15d,BYTE PTR [r12+r14*1+0x5d9a0]
    (0x18AF156, 7, 3, 0x5B8A0, 'B'),   # lea rsi,[r14+0x5b8a0]        + i * 0x110
    (0x18AF15D, 8, 4, 0x5D9A0, 'F'),   # mov BYTE PTR [r12+r14*1+0x5d9a0],al
    (0x18AF212, 7, 3, 0x5BCE0, 'D'),   # lea rbx,[r14+0x5bce0]        + i * 0x158
    (0x18AF4A5, 7, 3, 0x5C240, 'E'),   # lea rdi,[r14+0x5c240]        + i * 0x158
    (0x18AF4CE, 7, 3, 0x5BCE0, 'D'),   # add rbx,0x5bce0              i * 0x158 + D, then add rbx,r14
    (0x18AF5AF, 9, 4, 0x5D9A0, 'F'),   # cmp BYTE PTR [r12+r14*1+0x5d9a0],0x0
    (0x18AF5C7, 10, 6, 0x5D9A4, 'P'),  # movsd QWORD PTR [r14+rcx*4+0x5d9a4],xmm12   (rcx = 3i)
    (0x18AF5D3, 10, 6, 0x5D9A4, 'P'),  # movsd xmm0,QWORD PTR [r14+rcx*4+0x5d9a4]
    (0x18AF5F7, 10, 6, 0x5D9A4, 'P'),  # movsd QWORD PTR [r14+rcx*4+0x5d9a4],xmm0
    (0x18AF601, 8, 4, 0x5D9AC, 'P'),   # mov DWORD PTR [r14+rcx*4+0x5d9ac],eax
]

A_FROM_B = NEW['A'] - NEW['B']             # -0x880 (was -0x440)
D_FROM_E = NEW['D'] - NEW['E']             # -0xAC0 (was -0x560)
E_FROM_D = NEW['E'] - NEW['D']             # +0xAC0 (was +0x560)
INPLACE = [
    # one array reached from another by a fixed distance (the F1 pattern): the distance is 8 rows now
    (0x18A7160, '488d81c0fbffff', '488d81' + le32(A_FROM_B), '18a5580: A_i = B_i - 0x440'),
    (0x18A720E, '4805a0faffff', '4805' + le32(D_FROM_E), '18a5580: D_i = E_i - 0x560'),
    (0x18AEF40, '488dbb60050000', '488dbb' + le32(E_FROM_D), '18aebc0 final loop: E_i = D_i + 0x560'),
    (0x18AEF76, '488b8b58060000', '488b8b' + le32(E_FROM_D + 0xF8), '18aebc0 final loop: [D_i+0x658] = E_i+0xF8'),
    (0x18AEFCB, '488b8b50060000', '488b8b' + le32(E_FROM_D + 0xF0), '18aebc0 final loop: [D_i+0x650] = E_i+0xF0 (parent)'),
    # counts and bounds 4 -> 8
    (0x18AEC02, '41bc04000000', '41bc08000000', '18aebc0: one avatar or none, hide B x4 (r12 is also the final loop count)'),
    (0x18AEE5C, '41bc04000000', '41bc08000000', '18aebc0: final loop count (D/E alpha from B)'),
    (0x18AEE29, '83ff04', '83ff08', '18aebc0: unused rows edi..3 hidden (entry test)'),
    (0x18AEE4E, '83ff04', '83ff08', '18aebc0: unused rows edi..3 hidden (loop bound)'),
]

CAVES = [
    # The row loop's count [rbp-0x70] is the 4 that drove the loop over T+0x595A0 (r14d = 4 at 0x18A682E, stored at
    # 0x18A6834, that loop counts r14 itself). That 4 stays; the tag loop gets its own 8 here.
    (0x18A7118, 'c745a8f2010000', """
 mov qword ptr [rbp-0x70], 8
 mov dword ptr [rbp-0x58], 0x1f2
 jmp game+0x18A711F""", '18a5580: tag row loop count 8', []),
]

# The guard while the rows are four: a tag index of 4 or more skips the row. The index counts matches from 0, so it is
# never -1.
GUARD4 = [
    (0x18AF049, '41 83 bd 84 00 00 00 00', 'cmp dword ptr [rsp+0x20], 4\n jae game+0x18AEDB9\n cmp dword ptr [r13+0x84], 0\n jmp game+0x18AF051'),
]
GUARD4_SITES = []
GUARD8 = [
    (0x18AF049, '4183bd8400000000',
     'cmp dword ptr [rsp+0x20], 8\n jae game+0x18AEDB9\n cmp dword ptr [r13+0x84], 0\n jmp game+0x18AF051',
     '18aebc0 tag row by tag index'),
]
