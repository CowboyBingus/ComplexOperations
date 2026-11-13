#!/usr/bin/env python3
# Loadout screen (registry type 0xE5, "L") for eight players, game build 25480438. Static analysis only, nothing here
# has been run in the game. Way: the screen object is allocated bigger, and the four four-entry arrays that are
# indexed by player or card (records 4x0x9F0, map u32[4], reverse map u32[4], cards 4x0x1EE18) move to eight-entry
# arrays in the new tail. Everything else stays where it is. Sites follow the gen_count_sites.py style:
#   INPLACE: (rva, old hex, new hex, note)            same length, at most 15 bytes, one or more whole instructions
#   CAVES:   (rva, replaced hex, asm, note)           5..12 bytes become a jmp; asm uses game+0x.. and 1: labels
# Build the lists with build(dump); gen_count_sites.py checks them against a dump of the game's code and assembles the
# caves (groups 5 to 7).
import struct

OLD_SIZE = 0x2739D0           # alloc 0x1498ae1 (edx) and memset 0x1498b03 (r8d), ctor 0x14A17E0
NR   = 0x2739E0               # 8 records of 0x9F0          (old 0x10,   16-aligned like the old one)
NMAP = 0x278968               # u32[8] player -> card       (old 0x27D8, = 8 mod 16 like the old one)
NREV = 0x278988               # u32[8] card -> player       (old 0x27E8, directly behind the map, as before)
NC   = 0x2789A8               # 8 cards of 0x1EE18          (old 0x53A78, = 8 mod 16 like the old one)
NEW_SIZE = (NC + 8 * 0x1EE18 + 15) & ~15                     # 0x36FA70
OLD = {'REC': (0x10, 0x27D0), 'MAP': (0x27D8, 0x27E8), 'REV': (0x27E8, 0x27F8), 'CARD': (0x53A78, 0xCF2D8)}
NEW = {'REC': NR, 'MAP': NMAP, 'REV': NREV, 'CARD': NC}
assert NR >= OLD_SIZE and NR + 8 * 0x9F0 <= NMAP and NMAP + 32 == NREV and NREV + 32 <= NC

def moved(v, region):
    lo, hi = OLD[region]
    assert lo <= v < hi, (hex(v), region)
    return v - lo + NEW[region]

def le32(v): return struct.pack('<I', v & 0xFFFFFFFF).hex()

# 8 card centres (anchor x, a fraction of the 1920-wide container) for the init card loop, which reads one float a
# card from r12 (0x1466B17); the game's table at 0x21D8FB8 has {0.138, 0.38, 0.62, 0.862}. The cards are drawn at
# scale 0.5 (CARD_SCALE): 210-wide panels with the game's gap halved to 22, so a pitch of 232 and margins of 43.
CARD_X = [(960.0 + (i - 3.5) * 232.0) / 1920.0 for i in range(8)]

# --- 1. allocation size (factory 0x14984E0, screen type 11 case 0x1498AE1) ---------------------------------------
ALLOC = [
    (0x1498AE8, 'ba' + le32(OLD_SIZE)[:8], 'ba' + le32(NEW_SIZE), 'alloc size: mov edx,size'),
    (0x1498B03, '41b8' + le32(OLD_SIZE), '41b8' + le32(NEW_SIZE), 'memset size: mov r8d,size'),
]

# --- 2. L-relative displacements/immediates in a moved range, rewritten in place ---------------------------------
# (rva, instruction length, offset of the 4-byte field, old value, region). From the L data-flow tracker
# (lflow/lclass): accesses whose own displacement lies in a moved range with the L base itself (no card/record base).
DISP = [
    (0x87332A, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [r9+rax*4+0x27d8]
    (0x87333E, 7, 3, 0x620E8, 'CARD'),  # add    r9,0x620e8
    (0x875B53, 8, 4, 0x27D8, 'MAP'),  # mov    r9d,DWORD PTR [r8+rcx*4+0x27d8]
    (0x875B64, 7, 3, 0x620E8, 'CARD'),  # add    r8,0x620e8
    (0xB72AC8, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [r8+rax*4+0x27d8]
    (0xB72ADC, 7, 3, 0x620E8, 'CARD'),  # add    r8,0x620e8
    (0xB90143, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [r10+rax*4+0x27d8]
    (0xB90157, 7, 3, 0x620E8, 'CARD'),  # add    r10,0x620e8
    (0xB901EB, 7, 3, 0x27D8, 'MAP'),  # mov    eax,DWORD PTR [r9+0x27d8]
    (0xB901FC, 7, 3, 0x27EC, 'REV'),  # lea    rcx,[rdx+0x27ec]
    (0xB90217, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [rdx+rcx*4+0x27e8],r8d
    (0xB9021F, 7, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [r9+0x27d8],eax
    (0xB9022F, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdx+0x53a78]
    (0xB90373, 8, 4, 0x27D8, 'MAP'),  # mov    edi,DWORD PTR [r14+rax*4+0x27d8]
    (0xB90513, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [r14+rax*4+0x27d8]
    (0xB9056D, 9, 4, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rsi+r14*1+0x6f290],0x1
    (0xBA5953, 8, 4, 0x27D8, 'MAP'),  # mov    edi,DWORD PTR [r14+rax*4+0x27d8]
    (0xBAD843, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [r10+rax*4+0x27d8]
    (0xBAD857, 7, 3, 0x620E8, 'CARD'),  # add    r10,0x620e8
    (0xBB8B3B, 7, 3, 0x27D8, 'MAP'),  # mov    eax,DWORD PTR [r9+0x27d8]
    (0xBB8B4C, 7, 3, 0x27EC, 'REV'),  # lea    rcx,[rdx+0x27ec]
    (0xBB8B67, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [rdx+rcx*4+0x27e8],r8d
    (0xBB8B6F, 7, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [r9+0x27d8],eax
    (0xBB8B7F, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdx+0x53a78]
    (0x1466ADB, 7, 3, 0x562F9, 'CARD'),  # lea    rbx,[rdi+0x562f9]
    (0x1466AE2, 7, 3, 0x53A78, 'CARD'),  # lea    rsi,[rdi+0x53a78]
    (0x1466F26, 11, 3, 0x27D8, 'MAP'),  # mov    QWORD PTR [rdi+0x27d8],0xffffffffffffffff
    (0x1466F31, 11, 3, 0x27E8, 'REV'),  # mov    QWORD PTR [rdi+0x27e8],0xffffffffffffffff
    (0x1466F3C, 11, 3, 0x27E0, 'MAP'),  # mov    QWORD PTR [rdi+0x27e0],0xffffffffffffffff
    (0x1466F47, 11, 3, 0x27F0, 'REV'),  # mov    QWORD PTR [rdi+0x27f0],0xffffffffffffffff
    (0x14670C7, 8, 4, 0x27D8, 'MAP'),  # mov    DWORD PTR [rdi+rcx*4+0x27d8],r14d
    (0x14670CF, 6, 2, 0x27E8, 'REV'),  # mov    DWORD PTR [rdi+0x27e8],ecx
    (0x14674B4, 7, 3, 0x57FC0, 'CARD'),  # lea    rbx,[r14+0x57fc0]
    (0x146797B, 7, 3, 0x72868, 'CARD'),  # mov    rdx,QWORD PTR [rsi+0x72868]
    (0x1467A02, 7, 3, 0x72868, 'CARD'),  # mov    rax,QWORD PTR [rsi+0x72868]
    (0x1467A1C, 7, 3, 0x72868, 'CARD'),  # mov    rdx,QWORD PTR [rsi+0x72868]
    (0x1467A37, 7, 3, 0x198, 'REC'),  # lea    r8,[rsi+0x198]
    (0x1467A4A, 8, 4, 0x798, 'REC'),  # mov    r9d,DWORD PTR [rcx+rsi*1+0x798]
    (0x1467CDE, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdi+0x53a78]
    (0x1468601, 7, 3, 0x66F58, 'CARD'),  # lea    rcx,[rsi+0x66f58]
    (0x146867E, 7, 3, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rcx+rsi*1+0x6f290]
    (0x146868E, 7, 3, 0x6F284, 'CARD'),  # mov    ecx,DWORD PTR [rcx+rsi*1+0x6f284]
    (0x1468748, 7, 3, 0x66F5C, 'CARD'),  # mov    edx,DWORD PTR [rcx+rsi*1+0x66f5c]
    (0x146884E, 8, 3, 0x66F58, 'CARD'),  # cmp    DWORD PTR [rcx+rsi*1+0x66f58],0xffffffff
    (0x1468861, 8, 3, 0x66F58, 'CARD'),  # cmp    DWORD PTR [rcx+rsi*1+0x66f58],0x4
    (0x146887A, 8, 3, 0x66F5C, 'CARD'),  # cmp    DWORD PTR [rcx+rsi*1+0x66f5c],0x4
    (0x1468887, 7, 3, 0x624AC, 'CARD'),  # mov    eax,DWORD PTR [rcx+rsi*1+0x624ac]
    (0x14688F9, 8, 4, 0x72878, 'CARD'),  # mov    r15,QWORD PTR [rcx+rsi*1+0x72878]
    (0x1468E82, 6, 2, 0x6373C, 'CARD'),  # mov    eax,DWORD PTR [rcx+0x6373c]
    (0x1468EA6, 6, 2, 0x649E4, 'CARD'),  # mov    eax,DWORD PTR [rcx+0x649e4]
    (0x1468ECF, 6, 2, 0x65C8C, 'CARD'),  # mov    eax,DWORD PTR [rcx+0x65c8c]
    (0x1468EF1, 6, 2, 0x66F34, 'CARD'),  # mov    eax,DWORD PTR [rcx+0x66f34]
    (0x14697D2, 8, 4, 0x27D8, 'MAP'),  # mov    DWORD PTR [rsi+rcx*4+0x27d8],r14d
    (0x14697DA, 6, 2, 0x27E8, 'REV'),  # mov    DWORD PTR [rsi+0x27e8],eax
    (0x1469C96, 7, 3, 0x72878, 'CARD'),  # lea    rdi,[rsi+0x72878]
    (0x1469D4E, 7, 3, 0x798, 'REC'),  # lea    r14,[rsi+0x798]
    (0x1469D81, 8, 3, 0x198, 'REC'),  # cmp    DWORD PTR [rsi+rcx*8+0x198],0x21
    (0x1469DA0, 7, 3, 0x198, 'REC'),  # mov    edx,DWORD PTR [rsi+rcx*8+0x198]
    (0x146A382, 7, 3, 0x66F58, 'CARD'),  # lea    r8,[rcx+0x66f58]
    (0x146A400, 8, 4, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rcx+r10*1+0x6f290]
    (0x146A40D, 8, 4, 0x6F284, 'CARD'),  # mov    eax,DWORD PTR [rcx+r10*1+0x6f284]
    (0x146A43C, 8, 4, 0x66F5C, 'CARD'),  # mov    eax,DWORD PTR [rcx+r10*1+0x66f5c]
    (0x146A48D, 9, 4, 0x66F58, 'CARD'),  # cmp    DWORD PTR [r8+r9*1+0x66f58],0xffffffff
    (0x146A4A7, 9, 4, 0x66F58, 'CARD'),  # cmp    DWORD PTR [r8+r9*1+0x66f58],0x4
    (0x146A4B6, 9, 4, 0x66F5C, 'CARD'),  # cmp    DWORD PTR [r8+r9*1+0x66f5c],0x4
    (0x146A4C5, 8, 4, 0x624AC, 'CARD'),  # mov    eax,DWORD PTR [r8+r9*1+0x624ac]
    (0x146A4E1, 7, 2, 0x66F58, 'CARD'),  # cmp    DWORD PTR [rdx+0x66f58],0xffffffff
    (0x146A4F9, 7, 2, 0x66F58, 'CARD'),  # cmp    DWORD PTR [rdx+0x66f58],0x4
    (0x146A502, 7, 2, 0x66F5C, 'CARD'),  # cmp    DWORD PTR [rdx+0x66f5c],0x4
    (0x146A50D, 6, 2, 0x624AC, 'CARD'),  # mov    eax,DWORD PTR [rdx+0x624ac]
    (0x146A53F, 7, 3, 0x53A78, 'CARD'),  # add    r9,0x53a78
    (0x146A585, 7, 3, 0x53A78, 'CARD'),  # add    rdx,0x53a78
    (0x146AE35, 7, 3, 0x53A78, 'CARD'),  # lea    rbx,[rdi+0x53a78]
    (0x146B1F4, 7, 3, 0x55A20, 'CARD'),  # lea    rbx,[rdi+0x55a20]
    (0x146C110, 7, 3, 0x53A78, 'CARD'),  # lea    rdi,[rsi+0x53a78]
    (0x146C161, 7, 2, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rsi+0x592ac],0x1
    (0x146C16A, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x146C171, 10, 2, 0x592AC, 'CARD'),  # mov    DWORD PTR [rsi+0x592ac],0x1
    (0x146C18B, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x146C213, 7, 2, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rsi+0x592ac],0x0
    (0x146C224, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x146C22B, 10, 2, 0x592AC, 'CARD'),  # mov    DWORD PTR [rsi+0x592ac],0x0
    (0x146C23A, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x146C260, 7, 2, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rsi+0x592ac],0x1
    (0x146C26C, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x146C273, 10, 2, 0x592AC, 'CARD'),  # mov    DWORD PTR [rsi+0x592ac],0x1
    (0x146C28D, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x146C2A6, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rsi+0x56328]
    (0x146C474, 8, 4, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [rsi+r13*4+0x27d8]
    (0x146C4A3, 7, 3, 0x27EC, 'REV'),  # lea    rax,[rsi+0x27ec]
    (0x146C4C9, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [rsi+rax*4+0x27e8],r13d
    (0x146C4D1, 8, 4, 0x27D8, 'MAP'),  # mov    DWORD PTR [rsi+r13*4+0x27d8],ecx
    (0x146C510, 8, 3, 0x72880, 'CARD'),  # cmp    DWORD PTR [rdi+rsi*1+0x72880],0x5
    (0x146C51A, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rsi+0x595f0]
    (0x146C521, 12, 4, 0x72868, 'CARD'),  # mov    QWORD PTR [rdi+rsi*1+0x72868],0x0
    (0x146C530, 12, 4, 0x72878, 'CARD'),  # mov    QWORD PTR [rdi+rsi*1+0x72878],0x0
    (0x146C549, 9, 4, 0x6F278, 'CARD'),  # cmp    QWORD PTR [rdi+rsi*1+0x6f278],0x0
    (0x146C554, 8, 3, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rdi+rsi*1+0x6f28c],0x0
    (0x146C55E, 12, 4, 0x6F278, 'CARD'),  # mov    QWORD PTR [rdi+rsi*1+0x6f278],0x0
    (0x146C56A, 8, 3, 0x6F28C, 'CARD'),  # mov    BYTE PTR [rdi+rsi*1+0x6f28c],0x0
    (0x146C572, 8, 3, 0x72880, 'CARD'),  # cmp    DWORD PTR [rdi+rsi*1+0x72880],0x0
    (0x146C57C, 11, 3, 0x72880, 'CARD'),  # mov    DWORD PTR [rdi+rsi*1+0x72880],0x0
    (0x146C62B, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdi+0x53a78]
    (0x146C639, 11, 3, 0x72868, 'CARD'),  # mov    QWORD PTR [rdi+0x72868],0x0
    (0x146C644, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rdi+0x595f0]
    (0x146C64E, 11, 3, 0x72878, 'CARD'),  # mov    QWORD PTR [rdi+0x72878],0x0
    (0x146C663, 8, 3, 0x6F278, 'CARD'),  # cmp    QWORD PTR [rdi+0x6f278],0x0
    (0x146C66D, 7, 2, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rdi+0x6f28c],0x0
    (0x146C676, 11, 3, 0x6F278, 'CARD'),  # mov    QWORD PTR [rdi+0x6f278],0x0
    (0x146C681, 7, 2, 0x6F28C, 'CARD'),  # mov    BYTE PTR [rdi+0x6f28c],0x0
    (0x146C690, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdi+0x53a78]
    (0x146C81E, 8, 3, 0x72868, 'CARD'),  # cmp    QWORD PTR [rdi+0x72868],0x0
    (0x146C849, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rdi+0x595f0]
    (0x146C85D, 7, 3, 0x72868, 'CARD'),  # mov    QWORD PTR [rdi+0x72868],rbp
    (0x146C866, 7, 3, 0x72878, 'CARD'),  # mov    QWORD PTR [rdi+0x72878],rbp
    (0x146C872, 7, 3, 0x6F278, 'CARD'),  # cmp    QWORD PTR [rdi+0x6f278],rbp
    (0x146C87B, 7, 3, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rdi+0x6f28c],bpl
    (0x146C884, 7, 3, 0x6F278, 'CARD'),  # mov    QWORD PTR [rdi+0x6f278],rbp
    (0x146C88B, 7, 3, 0x6F28C, 'CARD'),  # mov    BYTE PTR [rdi+0x6f28c],bpl
    (0x146C892, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rsi+0x72889],0x0
    (0x146C89F, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rsi+0x6f290],0x1
    (0x146C8AC, 7, 2, 0x7288A, 'CARD'),  # cmp    BYTE PTR [rdi+0x7288a],0x1
    (0x146C8B9, 7, 3, 0x55A20, 'CARD'),  # lea    rbp,[rdi+0x55a20]
    (0x146C8C0, 7, 2, 0x7288A, 'CARD'),  # mov    BYTE PTR [rdi+0x7288a],0x1
    (0x146C913, 7, 3, 0x558C8, 'CARD'),  # lea    rcx,[rdi+0x558c8]
    (0x146C922, 7, 3, 0x593D0, 'CARD'),  # lea    rcx,[rdi+0x593d0]
    (0x146C92E, 7, 2, 0x556A2, 'CARD'),  # cmp    BYTE PTR [rdi+0x556a2],0x1
    (0x146C935, 7, 3, 0x53B88, 'CARD'),  # lea    rcx,[rdi+0x53b88]
    (0x146C94A, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rdi+0x66f70]
    (0x146C956, 8, 3, 0x72878, 'CARD'),  # cmp    QWORD PTR [rdi+0x72878],0x0
    (0x146C960, 7, 2, 0x72885, 'CARD'),  # cmp    BYTE PTR [rdi+0x72885],0x0
    (0x146C969, 7, 2, 0x72888, 'CARD'),  # mov    BYTE PTR [rdi+0x72888],0x1
    (0x146C975, 7, 3, 0x72887, 'CARD'),  # movzx  ebp,BYTE PTR [rdi+0x72887]
    (0x146C97C, 7, 3, 0x53B88, 'CARD'),  # lea    rcx,[rdi+0x53b88]
    (0x146C983, 7, 2, 0x72887, 'CARD'),  # mov    BYTE PTR [rdi+0x72887],0x1
    (0x146C9C7, 7, 3, 0x556A8, 'CARD'),  # lea    rcx,[rdi+0x556a8]
    (0x146C9E8, 7, 3, 0x557B8, 'CARD'),  # lea    rcx,[rdi+0x557b8]
    (0x146CA0B, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rdi+0x72889],0x0
    (0x146CA18, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rdi+0x6f290],0x1
    (0x146CA25, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rdi+0x66f70]
    (0x146CA36, 7, 2, 0x7288A, 'CARD'),  # cmp    BYTE PTR [rdi+0x7288a],0x0
    (0x146CA3D, 7, 3, 0x53A78, 'CARD'),  # lea    r15,[rdi+0x53a78]
    (0x146CC08, 7, 3, 0x27D8, 'MAP'),  # mov    edi,DWORD PTR [rsi+rcx*4+0x27d8]
    (0x146CC17, 7, 3, 0x27EC, 'REV'),  # lea    rax,[rsi+0x27ec]
    (0x146CC49, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [rsi+rax*4+0x27e8],r13d
    (0x146CC51, 7, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [rsi+rcx*4+0x27d8],edi
    (0x146CC69, 9, 4, 0x72878, 'CARD'),  # cmp    QWORD PTR [rbx+rsi*1+0x72878],0x0
    (0x146CC78, 8, 3, 0x7288A, 'CARD'),  # cmp    BYTE PTR [rbx+rsi*1+0x7288a],0x1
    (0x146CC86, 7, 3, 0x55A20, 'CARD'),  # lea    rbp,[rsi+0x55a20]
    (0x146CC8D, 8, 3, 0x7288A, 'CARD'),  # mov    BYTE PTR [rbx+rsi*1+0x7288a],0x1
    (0x146CCE4, 7, 3, 0x558C8, 'CARD'),  # lea    rcx,[rsi+0x558c8]
    (0x146CCF6, 7, 3, 0x593D0, 'CARD'),  # lea    rcx,[rsi+0x593d0]
    (0x146CD05, 7, 3, 0x53B88, 'CARD'),  # lea    rcx,[rsi+0x53b88]
    (0x146CD24, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rsi+0x66f70]
    (0x146CD3B, 7, 3, 0x53A78, 'CARD'),  # lea    r12,[rsi+0x53a78]
    (0x146CD4F, 8, 3, 0x72880, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x72880],0x0
    (0x146CD5D, 11, 3, 0x72880, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x72880],0x0
    (0x146CD6C, 8, 3, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x146CD76, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x146CD7D, 11, 3, 0x592AC, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x146CD93, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x146CDA7, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rbp+0x56328]
    (0x146CDB8, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rbp+0x56328]
    (0x146CDC1, 8, 3, 0x72880, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x72880],0x4
    (0x146CDCB, 11, 3, 0x72880, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x72880],0x4
    (0x146CDD6, 7, 3, 0x56328, 'CARD'),  # lea    rbp,[rsi+0x56328]
    (0x146CDDD, 8, 3, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x146CDE7, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x146CDEE, 11, 3, 0x592AC, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x146CE04, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x146CE5B, 9, 4, 0x72878, 'CARD'),  # cmp    QWORD PTR [rbx+rsi*1+0x72878],0x0
    (0x146CE66, 8, 3, 0x72885, 'CARD'),  # cmp    BYTE PTR [rbx+rsi*1+0x72885],0x0
    (0x146CE70, 8, 3, 0x72888, 'CARD'),  # mov    BYTE PTR [rbx+rsi*1+0x72888],0x1
    (0x146CE7D, 8, 4, 0x72887, 'CARD'),  # movzx  ebp,BYTE PTR [rbx+rsi*1+0x72887]
    (0x146CE85, 7, 3, 0x53B88, 'CARD'),  # lea    rcx,[rsi+0x53b88]
    (0x146CE8F, 8, 3, 0x72887, 'CARD'),  # mov    BYTE PTR [rbx+rsi*1+0x72887],0x1
    (0x146CED4, 7, 3, 0x556A8, 'CARD'),  # lea    rcx,[rsi+0x556a8]
    (0x146CEF0, 7, 3, 0x557B8, 'CARD'),  # lea    rcx,[rsi+0x557b8]
    (0x146CF1A, 8, 3, 0x72889, 'CARD'),  # cmp    BYTE PTR [rbx+rsi*1+0x72889],0x0
    (0x146CF24, 8, 3, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x6f290],0x1
    (0x146CF2E, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rsi+0x66f70]
    (0x146CF61, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rsi+0x72889],0x0
    (0x146CF6A, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rsi+0x6f290],0x1
    (0x146CFD7, 7, 3, 0x27E8, 'REV'),  # lea    r14,[rsi+0x27e8]
    (0x146CFE3, 7, 3, 0x562F9, 'CARD'),  # lea    rbx,[rsi+0x562f9]
    (0x146D0ED, 6, 2, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rsi+0x6f290]
    (0x146D0F8, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rsi+0x66f70]
    (0x146D113, 6, 2, 0x66F5C, 'CARD'),  # mov    eax,DWORD PTR [rsi+0x66f5c]
    (0x146D121, 7, 3, 0x66F5C, 'CARD'),  # mov    DWORD PTR [rsi+0x66f5c],r8d
    (0x146D12D, 6, 2, 0x66F60, 'CARD'),  # mov    DWORD PTR [rsi+0x66f60],eax
    (0x146D135, 6, 2, 0x66F60, 'CARD'),  # mov    eax,DWORD PTR [rsi+0x66f60]
    (0x146D145, 7, 3, 0x624A8, 'CARD'),  # movzx  edx,WORD PTR [rsi+0x624a8]
    (0x146D14C, 7, 3, 0x620E8, 'CARD'),  # lea    rcx,[rsi+0x620e8]
    (0x146D17B, 7, 3, 0x59E48, 'CARD'),  # lea    rcx,[rsi+0x59e48]
    (0x146D1B9, 7, 3, 0x63738, 'CARD'),  # movzx  edx,WORD PTR [rsi+0x63738]
    (0x146D1C0, 7, 3, 0x624B0, 'CARD'),  # lea    rcx,[rsi+0x624b0]
    (0x146D22B, 7, 3, 0x53A78, 'CARD'),  # lea    rdi,[rsi+0x53a78]
    (0x146D29B, 11, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [rsi+rax*4+0x27d8],0xffffffff
    (0x146D329, 7, 3, 0x27D8, 'MAP'),  # mov    eax,DWORD PTR [r9+0x27d8]
    (0x146D33A, 7, 3, 0x27EC, 'REV'),  # add    rcx,0x27ec
    (0x146D359, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [r8+rcx*4+0x27e8],edx
    (0x146D361, 7, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [r9+0x27d8],eax
    (0x146D9D6, 8, 3, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rcx+rbx*1+0x6f290],0x1
    (0x146D9E0, 7, 3, 0x6F284, 'CARD'),  # mov    ecx,DWORD PTR [rcx+rbx*1+0x6f284]
    (0x146DA08, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rbx+0x53a78]
    (0x146DA49, 8, 4, 0x72886, 'CARD'),  # cmp    BYTE PTR [rsi+rbx*1+0x72886],dil
    (0x146DA53, 8, 4, 0x72884, 'CARD'),  # cmp    BYTE PTR [rsi+rbx*1+0x72884],dil
    (0x146DA5D, 9, 5, 0x7288C, 'CARD'),  # movss  xmm0,DWORD PTR [rsi+rbx*1+0x7288c]
    (0x146DA88, 7, 3, 0x72886, 'CARD'),  # cmp    BYTE PTR [rbx+0x72886],dil
    (0x146DA95, 7, 3, 0x72884, 'CARD'),  # cmp    BYTE PTR [rbx+0x72884],dil
    (0x146DAA2, 8, 4, 0x7288C, 'CARD'),  # movss  xmm0,DWORD PTR [rbx+0x7288c]
    (0x146DAED, 7, 3, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rsi+rbx*1+0x6f290]
    (0x146DAF9, 9, 5, 0x6F296, 'CARD'),  # movzx  r9d,BYTE PTR [rsi+rbx*1+0x6f296]
    (0x146DB02, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rbx+0x66f70]
    (0x146DB1E, 9, 5, 0x6F296, 'CARD'),  # movzx  r9d,BYTE PTR [rsi+rbx*1+0x6f296]
    (0x146DB27, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x146DB68, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rbx+0x56328]
    (0x146DB79, 7, 3, 0x6F298, 'CARD'),  # lea    rcx,[rbx+0x6f298]
    (0x146DB8A, 8, 3, 0x6F296, 'CARD'),  # mov    BYTE PTR [rsi+rbx*1+0x6f296],0x1
    (0x146DB97, 6, 2, 0x72870, 'CARD'),  # mov    edx,DWORD PTR [rbx+0x72870]
    (0x146DBAA, 7, 2, 0x6F296, 'CARD'),  # cmp    BYTE PTR [rbx+0x6f296],0x0
    (0x146DBB7, 6, 2, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rbx+0x6f290]
    (0x146DBC2, 7, 2, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rbx+0x6f28c],0x0
    (0x146DBF4, 7, 2, 0x6F280, 'CARD'),  # cmp    DWORD PTR [rbx+0x6f280],0xffffffff
    (0x146DC1F, 6, 2, 0x6F280, 'CARD'),  # mov    edx,DWORD PTR [rbx+0x6f280]
    (0x146DC25, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rbx+0x66f70]
    (0x146DC3B, 6, 2, 0x66F68, 'CARD'),  # cmp    BYTE PTR [rbx+0x66f68],al
    (0x146DC6C, 6, 2, 0x66F58, 'CARD'),  # mov    edx,DWORD PTR [rbx+0x66f58]
    (0x146DC9E, 6, 2, 0x66F58, 'CARD'),  # mov    edx,DWORD PTR [rbx+0x66f58]
    (0x146DCA4, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x146DCBD, 7, 3, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rcx+rbx*1+0x6f290]
    (0x146DD18, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rbx+0x53a78]
    (0x146DD69, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rbx+0x6f290],0x1
    (0x146DD72, 7, 3, 0x593D0, 'CARD'),  # lea    rcx,[rbx+0x593d0]
    (0x146DD85, 6, 2, 0x6F290, 'CARD'),  # mov    ebp,DWORD PTR [rbx+0x6f290]
    (0x146DD92, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rbx+0x66f70]
    (0x146DDA9, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x146DDBA, 7, 3, 0x781E8, 'CARD'),  # lea    rsi,[rbx+0x781e8]
    (0x146DF0B, 6, 2, 0x6F290, 'CARD'),  # mov    esi,DWORD PTR [rbx+0x6f290]
    (0x146DF18, 7, 3, 0x66F70, 'CARD'),  # lea    rcx,[rbx+0x66f70]
    (0x146DF2F, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x146DF40, 7, 3, 0x781E8, 'CARD'),  # add    rbx,0x781e8
    (0x146DF9D, 8, 3, 0x7288A, 'CARD'),  # cmp    BYTE PTR [rcx+rbx*1+0x7288a],0x0
    (0x146DFC4, 7, 3, 0x6F290, 'CARD'),  # mov    eax,DWORD PTR [rcx+rbx*1+0x6f290]
    (0x146E027, 8, 3, 0x7288A, 'CARD'),  # cmp    BYTE PTR [rcx+rbx*1+0x7288a],0x0
    (0x146E31D, 7, 3, 0x6373C, 'CARD'),  # lea    rcx,[rdi+0x6373c]
    (0x146E33A, 6, 2, 0x649E4, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x649e4]
    (0x146E363, 6, 2, 0x65C8C, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x65c8c]
    (0x146E388, 6, 2, 0x66F34, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x66f34]
    (0x146E425, 7, 3, 0x6373C, 'CARD'),  # lea    rcx,[rdi+0x6373c]
    (0x146E442, 6, 2, 0x649E4, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x649e4]
    (0x146E46B, 6, 2, 0x65C8C, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x65c8c]
    (0x146E490, 6, 2, 0x66F34, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x66f34]
    (0x146E511, 7, 3, 0x6373C, 'CARD'),  # lea    rcx,[rdi+0x6373c]
    (0x146E52E, 6, 2, 0x649E4, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x649e4]
    (0x146E557, 6, 2, 0x65C8C, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x65c8c]
    (0x146E57C, 6, 2, 0x66F34, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x66f34]
    (0x146E5EA, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdi+0x53a78]
    (0x146E5F6, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rdi+0x53a78]
    (0x146E609, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rdi+0x6f290],0x1
    (0x146E619, 7, 3, 0x6373C, 'CARD'),  # lea    r11,[rdi+0x6373c]
    (0x146E6C6, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rdi+0x595f0]
    (0x146E76A, 7, 3, 0x620E8, 'CARD'),  # lea    rcx,[rdi+0x620e8]
    (0x146EA28, 7, 3, 0x53A78, 'CARD'),  # lea    rbx,[rcx+0x53a78]
    (0x146EA7B, 7, 3, 0x6F290, 'CARD'),  # mov    r8d,DWORD PTR [rdi+0x6f290]
    (0x146EA88, 6, 2, 0x6F284, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x6f284]
    (0x146EA9E, 7, 3, 0x685D0, 'CARD'),  # mov    ebx,DWORD PTR [rcx+rdi*1+0x685d0]
    (0x146EAB0, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rdi+0x595f0]
    (0x146EAC1, 7, 3, 0x6F290, 'CARD'),  # mov    r8d,DWORD PTR [rdi+0x6f290]
    (0x146EAD2, 6, 2, 0x6F284, 'CARD'),  # mov    ecx,DWORD PTR [rdi+0x6f284]
    (0x146EB07, 6, 2, 0x6F284, 'CARD'),  # mov    eax,DWORD PTR [rdi+0x6f284]
    (0x146EB49, 6, 2, 0x66F5C, 'CARD'),  # mov    ecx,DWORD PTR [rdi+0x66f5c]
    (0x146EB8F, 6, 2, 0x66F5C, 'CARD'),  # mov    edx,DWORD PTR [rdi+0x66f5c]
    (0x146EC86, 7, 3, 0x63738, 'CARD'),  # lea    rsi,[rdi+0x63738]
    (0x146ECBF, 7, 3, 0x72878, 'CARD'),  # lea    rbp,[rdi+0x72878]
    (0x146ECE0, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rdi+0x72889],0x0
    (0x146ECE9, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rdi+0x6f290],0x1
    (0x146ED86, 7, 3, 0x72878, 'CARD'),  # lea    rbp,[rdi+0x72878]
    (0x146EDAA, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rdi+0x72889],0x0
    (0x146EDB3, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rdi+0x6f290],0x1
    (0x146EF7F, 7, 3, 0x562F9, 'CARD'),  # lea    rsi,[rdi+0x562f9]
    (0x146F0BB, 7, 3, 0x562F9, 'CARD'),  # lea    rsi,[rdi+0x562f9]
    (0x146F43A, 7, 3, 0x63738, 'CARD'),  # lea    rbx,[rsi+0x63738]
    (0x146F47D, 7, 3, 0x6F290, 'CARD'),  # lea    rbx,[rsi+0x6f290]
    (0x146F484, 7, 3, 0x53A78, 'CARD'),  # lea    rdi,[rsi+0x53a78]
    (0x146F4A4, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rsi+0x72889],0x0
    (0x146F4AD, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rsi+0x6f290],0x1
    (0x146FBE2, 7, 3, 0x27D8, 'MAP'),  # mov    r8d,DWORD PTR [r10+0x27d8]
    (0x146FBF5, 7, 3, 0x27EC, 'REV'),  # lea    rax,[r9+0x27ec]
    (0x146FC16, 8, 4, 0x27E8, 'REV'),  # mov    DWORD PTR [r9+rax*4+0x27e8],ecx
    (0x146FC1E, 7, 3, 0x27D8, 'MAP'),  # mov    DWORD PTR [r10+0x27d8],r8d
    (0x146FC28, 7, 3, 0x53A78, 'CARD'),  # add    r9,0x53a78
    (0x146FDF9, 8, 4, 0x27D8, 'MAP'),  # mov    esi,DWORD PTR [r13+rbx*4+0x27d8]
    (0x146FEEB, 6, 2, 0x140, 'REC'),  # mov    eax,DWORD PTR [rsi+0x140]
    (0x146FF42, 7, 3, 0x198, 'REC'),  # lea    rcx,[rsi+0x198]
    (0x146FF56, 10, 2, 0x798, 'REC'),  # mov    DWORD PTR [rsi+0x798],0x0
    (0x146FF6C, 8, 4, 0x9F8, 'REC'),  # cmp    QWORD PTR [rbx+r13*1+0x9f8],r15
    (0x146FFBB, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x146FFC2, 11, 3, 0x72868, 'CARD'),  # mov    QWORD PTR [rbx+0x72868],0x0
    (0x146FFCD, 11, 3, 0x72878, 'CARD'),  # mov    QWORD PTR [rbx+0x72878],0x0
    (0x146FFDD, 8, 3, 0x6F278, 'CARD'),  # cmp    QWORD PTR [rbx+0x6f278],0x0
    (0x146FFE7, 7, 2, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rbx+0x6f28c],0x0
    (0x146FFF0, 11, 3, 0x6F278, 'CARD'),  # mov    QWORD PTR [rbx+0x6f278],0x0
    (0x146FFFB, 7, 2, 0x6F28C, 'CARD'),  # mov    BYTE PTR [rbx+0x6f28c],0x0
    (0x1470013, 7, 3, 0x9F8, 'REC'),  # mov    QWORD PTR [r14+0x9f8],r15
    (0x14700E2, 7, 3, 0x130, 'REC'),  # cmp    DWORD PTR [r14+0x130],eax
    (0x14700EB, 7, 3, 0x130, 'REC'),  # mov    ecx,DWORD PTR [r14+0x130]
    (0x1470133, 7, 3, 0x134, 'REC'),  # cmp    DWORD PTR [r14+0x134],eax
    (0x147013C, 7, 3, 0x134, 'REC'),  # mov    r8d,DWORD PTR [r14+0x134]
    (0x147015E, 7, 3, 0x138, 'REC'),  # cmp    DWORD PTR [r14+0x138],eax
    (0x1470167, 7, 3, 0x138, 'REC'),  # mov    r8d,DWORD PTR [r14+0x138]
    (0x1470189, 7, 3, 0x13C, 'REC'),  # cmp    DWORD PTR [r14+0x13c],eax
    (0x1470192, 7, 3, 0x13C, 'REC'),  # mov    r8d,DWORD PTR [r14+0x13c]
    (0x14701CF, 7, 3, 0x140, 'REC'),  # mov    eax,DWORD PTR [r14+0x140]
    (0x1470239, 7, 3, 0x130, 'REC'),  # mov    DWORD PTR [r14+0x130],eax
    (0x1470245, 7, 3, 0x134, 'REC'),  # mov    DWORD PTR [r14+0x134],eax
    (0x1470251, 7, 3, 0x138, 'REC'),  # mov    DWORD PTR [r14+0x138],eax
    (0x147025D, 7, 3, 0x13C, 'REC'),  # mov    DWORD PTR [r14+0x13c],eax
    (0x147028E, 7, 3, 0x140, 'REC'),  # mov    DWORD PTR [r14+0x140],eax
    (0x14702A1, 7, 3, 0x144, 'REC'),  # mov    DWORD PTR [r14+0x144],eax
    (0x1470313, 7, 3, 0x158, 'REC'),  # mov    BYTE PTR [r14+0x158],cl
    (0x147032D, 7, 3, 0x14C, 'REC'),  # mov    DWORD PTR [r14+0x14c],eax
    (0x1470339, 7, 3, 0x148, 'REC'),  # mov    DWORD PTR [r14+0x148],eax
    (0x147036A, 7, 3, 0x154, 'REC'),  # mov    DWORD PTR [r14+0x154],eax
    (0x147037F, 7, 3, 0x198, 'REC'),  # lea    rcx,[r14+0x198]
    (0x1470398, 11, 3, 0x798, 'REC'),  # mov    DWORD PTR [r14+0x798],0x0
    (0x14703BB, 7, 3, 0x595F0, 'CARD'),  # lea    rcx,[rbx+0x595f0]
    (0x14703C2, 11, 3, 0x72868, 'CARD'),  # mov    QWORD PTR [rbx+0x72868],0x0
    (0x14703CD, 11, 3, 0x72878, 'CARD'),  # mov    QWORD PTR [rbx+0x72878],0x0
    (0x14703DD, 8, 3, 0x6F278, 'CARD'),  # cmp    QWORD PTR [rbx+0x6f278],0x0
    (0x14703E7, 7, 2, 0x6F28C, 'CARD'),  # cmp    BYTE PTR [rbx+0x6f28c],0x0
    (0x14703F0, 11, 3, 0x6F278, 'CARD'),  # mov    QWORD PTR [rbx+0x6f278],0x0
    (0x14703FB, 7, 2, 0x6F28C, 'CARD'),  # mov    BYTE PTR [rbx+0x6f28c],0x0
    (0x1470451, 7, 3, 0x6F278, 'CARD'),  # lea    rbx,[r13+0x6f278]
    (0x14704D8, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[r13+0x53a78]
    (0x1470550, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[r13+0x53a78]
    (0x14705DE, 11, 3, 0x9F8, 'REC'),  # mov    QWORD PTR [r13+0x9f8],0x0
    (0x14705F0, 11, 3, 0x13E8, 'REC'),  # mov    QWORD PTR [r13+0x13e8],0x0
    (0x1470602, 11, 3, 0x1DD8, 'REC'),  # mov    QWORD PTR [r13+0x1dd8],0x0
    (0x1470614, 11, 3, 0x27C8, 'REC'),  # mov    QWORD PTR [r13+0x27c8],0x0
    (0x14706AF, 7, 3, 0x9F8, 'REC'),  # lea    rcx,[rsi+0x9f8]
    (0x1470704, 6, 2, 0x72874, 'CARD'),  # mov    edx,DWORD PTR [rcx+0x72874]
    (0x147070A, 10, 2, 0x7288C, 'CARD'),  # mov    DWORD PTR [rcx+0x7288c],0xbf800000
    (0x147088E, 8, 4, 0xD0, 'REC'),  # mov    r9d,DWORD PTR [rcx+rbx*1+0xd0]
    (0x1470B48, 7, 3, 0x53A78, 'CARD'),  # lea    rbx,[rsi+0x53a78]
    (0x1470B48, 7, 3, 0x53A78, 'CARD'),  # lea    rbx,[rsi+0x53a78]
    (0x14710B4, 7, 3, 0x72870, 'CARD'),  # lea    r13,[rsi+0x72870]
    (0x14711B4, 7, 3, 0x72870, 'CARD'),  # mov    edx,DWORD PTR [rcx+rsi*1+0x72870]
    (0x1471205, 7, 3, 0x556A2, 'CARD'),  # lea    rbx,[rsi+0x556a2]
    (0x1471221, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rsi+0x53a78]
    (0x147123B, 7, 2, 0x72889, 'CARD'),  # cmp    BYTE PTR [rsi+0x72889],0x0
    (0x1471248, 7, 2, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rsi+0x6f290],0x1
    (0x147149F, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rsi+0x53a78]
    (0x14716B6, 7, 3, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [rsi+rax*4+0x27d8]
    (0x14716CD, 7, 3, 0x53A78, 'CARD'),  # lea    rcx,[rsi+0x53a78]
    (0x14716F1, 8, 3, 0x72880, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x72880],0x1
    (0x14716FB, 11, 3, 0x72880, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x72880],0x1
    (0x147170A, 8, 3, 0x592AC, 'CARD'),  # cmp    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x1471714, 7, 3, 0x566A0, 'CARD'),  # lea    rcx,[rsi+0x566a0]
    (0x147171B, 11, 3, 0x592AC, 'CARD'),  # mov    DWORD PTR [rbx+rsi*1+0x592ac],0x1
    (0x1471731, 7, 3, 0x58420, 'CARD'),  # lea    rcx,[rsi+0x58420]
    (0x1471746, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rdi+0x56328]
    (0x1471757, 7, 3, 0x56328, 'CARD'),  # lea    rcx,[rdi+0x56328]
    (0x1471848, 7, 3, 0x53A78, 'CARD'),  # lea    rbx,[rsi+0x53a78]
    (0x1471AC3, 7, 3, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [rdi+rax*4+0x27d8]
    (0x1471BDA, 7, 3, 0x27D8, 'MAP'),  # mov    ebx,DWORD PTR [rdi+rax*4+0x27d8]
    (0x1471D33, 7, 3, 0x27D8, 'MAP'),  # mov    ecx,DWORD PTR [rsi+rax*4+0x27d8]
    (0x1471D44, 7, 3, 0x66F70, 'CARD'),  # lea    rbx,[rsi+0x66f70]
    (0x1471D95, 8, 3, 0x6F290, 'CARD'),  # cmp    DWORD PTR [rdi+rsi*1+0x6f290],0x1
    (0x1471E6B, 7, 3, 0x27D8, 'MAP'),  # mov    edx,DWORD PTR [rcx+rax*4+0x27d8]
    (0x1471E7E, 7, 3, 0x620E8, 'CARD'),  # add    rcx,0x620e8
    (0x14725E2, 7, 3, 0x6F290, 'CARD'),  # lea    rbx,[rcx+0x6f290]
    (0x14A1943, 7, 3, 0x53A78, 'CARD'),  # lea    rsi,[rbx+0x53a78]
]
# Sites of DISP that a cave or a combined in-place site below rewrites itself.
DISP_SKIP = {0x1466F26, 0x1466F31, 0x1466F3C, 0x1466F47,   # map clear: cave at 0x1466F26
             0x146F43A,                                    # combined site 0x146F435
             0x1471848,                                    # cave 0x1471848
             0x146EF7F, 0x146F0BB,                         # card loop count caves of 146e9d0
             # map reads inside the eight-bounded guards (GUARDS8)
             0x146CC08, 0x875B53, 0xB72AC8, 0xB90143, 0xB90373, 0xB90513, 0xBA5953, 0xBAD843, 0x87332A, 0x1471E6B,
             0x1471AC3, 0x1471BDA, 0x1471D33}

# --- 3. card-base immediates the tracker did not follow (the L pointer comes through a path it lost) -------------
# (rva, old instruction hex); the imm32/disp32 is the last 4 bytes, card region.
CARD_IMM = [
    (0xB90397, '4881c1783a0500'),   # b90260 handler: add rcx,0x53a78 (rcx = card index*0x1EE18, then + L)
    (0xB90527, '488dbe706f0600'),   # b903f0 handler: lea rdi,[rsi+0x66f70] (rsi = L + card*0x1EE18)
    (0xBA5977, '4881c1783a0500'),   # ba5840 rpc_loadout_set_stratagem_in_slot
    (0x1467743, '4881c3205a0500'),  # 1467720: add rbx,0x55a20 (rbx = L), card loop edi=4 at 0x146774A
    (0x14688CE, '4881c1783a0500'),
    (0x1469074, '4881c1783a0500'),
    (0x1469CB0, '4881c1783a0500'),
    (0x146C587, '4881c728630500'),
    (0x146D190, '4881c1b0240600'),
    (0x146DCC9, '4881c1706f0600'),
    (0x146DCE3, '4881c1f0950500'),
    (0x146DFD7, '4881c1706f0600'),
    (0x146DFEE, '4881c1f0950500'),
    (0x146E050, '4881c1d0930500'),
    (0x146ECD6, '4881c1783a0500'),
    (0x146EDA0, '4881c1783a0500'),
    (0x1470530, '4881c1783a0500'),
    (0x1471AD9, '4881c1e8200600'),
    (0x1471BFE, '4881c1783a0500'),
    (0x146CB2C, '4881c7783a0500'),  # 146c2f0: add rdi,0x53a78 (rdi = L + card*0x1EE18, as at 0x146CA18)
    (0xB58CB8, '4881c390f20600'),   # b58a90 registry walker, type 0xE5 case: add rbx,0x6f290 (rbx = L)
]

# --- 4. counts and bounds 4 -> 8 (7 for the "other cards" loops) -----------------------------------------------
# (rva, old hex, new hex, note). R = read in the listing, see NOTES.md.
BOUNDS = [
    # card loops (stride 0x1EE18)
    (0x14674BB, 'bf04000000', 'bf08000000', 'init 2: cards, sub rdi,1 loop'),
    (0x146774A, 'bf04000000', 'bf08000000', '1467720: cards'),
    (0x146866B, '4183f804', '4183f808', '1468320: cards, +0x6f290 field'),
    (0x1469CC8, '83fb04', '83fb08', '1468320: cards'),
    (0x146AE3C, 'be04000000', 'be08000000', 'phase 2: hide cards'),
    (0x146B1FB, 'be04000000', 'be08000000', '146abc0: cards'),
    (0x146D2C0, '83fd04', '83fd08', '146c2f0: cards with rev pointer r14 and bit r12d'),
    (0x146ED06, '83fe04', '83fe08', '146e9d0: cards'),
    (0x146EDD0, '83fe04', '83fe08', '146e9d0: cards'),
    (0x14704AB, '83fe04', '83fe08', '146fc40: cards'),
    (0x147056D, '83fe04', '83fe08', '146fc40: outer card loop (inner 0x1470566 = 4 slots, stays)'),
    (0x1470B4F, 'bf04000000', 'bf08000000', '1470a40: cards'),
    (0x14711F1, '4183fc04', '4183fc08', '14710xx: cards, r13 += 0x1EE18'),
    (0x14714BC, '4183fe04', '4183fe08', '1471210 loop: cards'),
    (0x14725EE, 'bf04000000', 'bf08000000', '14725c0: cards'),
    (0x14A17F4, 'bf04000000', 'bf08000000', 'ctor: records loop (r8=edi) and cards loop (rdi)'),
    (0xB58CBF, 'bf04000000', 'bf08000000', 'b58a90 registry walker: cards'),
    (0x146DDC1, 'bf03000000', 'bf07000000', '146d9b0: cards 1..3 (from +0x781E8 = card 1)'),
    (0x146DF47, 'bf03000000', 'bf07000000', '146d9b0: cards 1..3'),
    # record loops (stride 0x9F0)
    (0x1469E06, '4183ff04', '4183ff08', '1468320: records (r11 += 0x35 = record stride / 0x30 items)'),
    (0x14706C4, '83fb04', '83fb08', '1470650: record search by peer id'),
    # free-card searches over the reverse map (start at card 1)
    (0x146C4BB, '83f904', '83f908', '146c2f0'),
    (0x146CC2B, '83ff04', '83ff08', '146c2f0 (mod guard 0x146CC30 sits behind it)'),
    (0x146D34C, '83f804', '83f808', '146d320'),
    (0x146FC0C, '4183f804', '4183f808', '146fbd0'),
    (0xB9020E, '83f804', '83f808', 'b90190 (handler)'),
    (0xBB8B5E, '83f804', '83f808', 'bb8ae0 rpc_sync_stratagem_loadout_to_hotjoiner'),
    # player index / card index / record index bounds
    (0xB901E1, '4183f804', '4183f808', 'b90190: own index L+0x27D4'),
    (0xBB8B31, '4183f804', '4183f808', 'bb8ae0: own index L+0x27D4'),
    (0x146FDE6, '83fb04', '83fb08', '146fc40: peer player index (ctx+0x163AC)'),
    (0x1471134, '4183ff04', '4183ff08', '14710c0 card loop: peer player index'),
    (0x146E1B3, '4183fa04', '4183fa08', '146e0a0: record index'),
    (0x146E867, '83f804', '83f808', '146e0a0: record index'),
    (0x1471ACA, '83f904', '83f908', '14719f0: card from map'),
    (0x1471BE1, '83fb04', '83fb08', '1471b00: card from map'),
    (0x1471D3A, '83f904', '83f908', '1471c50: card from map'),
    # card index from the map, read by peer: the handlers reject cards 4..7 here
    (0xB9014B, '83f904', '83f908', 'b90040 (handler): card from map'),
    (0xB9037B, '83ff04', '83ff08', 'b90260 (handler): card from map'),
    (0xB9051B, '83f904', '83f908', 'b903f0 (handler): card from map'),
    (0xBA595B, '83ff04', '83ff08', 'ba5840 rpc_loadout_set_stratagem_in_slot: card from map'),
    (0xBAD84B, '83f904', '83f908', 'bad740 booster: card from map'),
    # no caller found (no call, no pointer, no pdata row): same shape as the live functions, raised with them
    (0x146A3EC, '4183f904', '4183f908', '146a360: cards (r8 += 0x1EE18)'),
    (0x146FBD9, '83f904', '83f908', '146fbd0: own index L+0x27D4 before the map'),
    # 146c2f0: manager slots; the mod's guard behind it (0x146C3D5) then skips only slots of 8 and up
    (0x146C3D1, '4983ff04', '4983ff08', '146c2f0: manager slot bound (stack flags, see STACK)'),
    # memset of the records (0x1465840 reset): 4*0x9F0 -> 8*0x9F0 (the lea is cave 0x1465C06)
    (0x1465C12, '41b8c0270000', '41b8' + le32(8 * 0x9F0), '1465840: records memset size'),
]

# --- 4b. four flag bytes on the stack by player index move to [rsp+0x38] (8 bytes no instruction of the function and
# no callee of its uses: calls pass at most 7 arguments, so nobody owns +0x38) ------------------------------------
STACK = [
    # 146c2f0: [rsp+0x40..0x43] by manager slot, read by card; [rsp+0x44] is the peer loop's counter
    (0x146C319, 'c744244000000000', '4c896c2438' '0f1f00', '146c2f0: mov [rsp+0x38],r13 (r13 = 0 at 0x146C303); nop'),
    (0x146C3DE, '42c6443c4000', '42c6443c3800', '146c2f0: flag by slot -> [rsp+r15+0x38]'),
    (0x146C5D9, '42c6443c4001', '42c6443c3801', '146c2f0: flag by slot -> [rsp+r15+0x38]'),
    (0x146CFD2, '4c8d7c2440', '4c8d7c2438', '146c2f0: lea r15,[rsp+0x38] (read by card)'),
    # 146fc40: [rsp+0x4c..0x4f] by player index; [rsp+0x50] is the card temp. Init is cave 0x146FC7E.
    (0x146FE05, 'c6441c4c01', 'c6441c3801', '146fc40: flag by player -> [rsp+rbx+0x38]'),
    (0x147059E, '8b44244c', '8b442438', '146fc40: flag 0 (al) from [rsp+0x38]'),
    (0x14705E9, '807c244d00', '807c243900', '146fc40: flag 1'),
    (0x14705FB, '807c244e00', '807c243a00', '146fc40: flag 2'),
    (0x147060D, '807c244f00', '807c243b00', '146fc40: flag 3 (flags 4..7: cave 0x147061F)'),
]

def _combined():
    lea_old = 0x63738
    lea_new = moved(lea_old, 'CARD')
    return [
        # mov ebp,4; mov [rsp+0xc0],rsi; mov esi,ebp -> mov [rsp+0xc0],rsi; push 8; pop rsi; push 4; pop rbp; nop
        # rbp stays the function's constant 4 (enum, stride, bit, slot count); rsi is the card loop's count.
        (0x146EA37, 'bd04000000' '4889b424c0000000' '8bf5', '4889b424c0000000' '6a085e' '6a045d' '90',
         '146e9d0: card loop count esi 4 -> 8, ebp stays 4'),
        # mov ebp,4; lea rbx,[rsi+0x63738]; mov edi,ebp -> lea rbx,[rsi+new]; push 4; pop rdi; push 8; pop rbp; nop
        # rdi counts card 0's four slots (stride 0x12A8), rbp the cards (stride 0x1EE18).
        (0x146F435, 'bd04000000' '488d9e' + le32(lea_old) + '8bfd', '488d9e' + le32(lea_new) + '6a045f' '6a085d' '90',
         '146f3b0: card loop count rbp 4 -> 8, slot count rdi stays 4'),
    ]

def _caves():
    R = lambda v: '0x%x' % moved(v, 'REC')
    C = lambda v: '0x%x' % moved(v, 'CARD')
    table = '\n'.join(' .float %r' % x for x in CARD_X)
    clear = ''.join(' mov qword ptr [rdi+0x%x], -1\n' % (NMAP + 8 * i) for i in range(8))
    return [
        # records base with a disp8 (0x10): no room for disp32, so a jump to the instruction(s) with the new base
        (0x1465C06, '488d4f10' '88870d280000', 'lea rcx, [rdi+%s]\n mov byte ptr [rdi+0x280d], al\n jmp game+0x1465C10' % R(0x10), '1465840 records memset'),
        (0x1467A87, '4c8d4610' '4869c8f0090000', 'lea r8, [rsi+%s]\n imul rcx, rax, 0x9f0\n jmp game+0x1467A92' % R(0x10), '1467a50 own record'),
        (0x1467AB4, '8b86d0270000' '4c8d4610', 'mov eax, dword ptr [rsi+0x27d0]\n lea r8, [rsi+%s]\n jmp game+0x1467ABE' % R(0x10), '1467a50 own record'),
        (0x146E1C0, '4c8d5710' '4c03d1', 'lea r10, [rdi+%s]\n add r10, rcx\n jmp game+0x146E1C7' % R(0x10), '146e0a0 record (je uses the add)'),
        (0x146E877, '488d7710' '4803f1', 'lea rsi, [rdi+%s]\n add rsi, rcx\n jmp game+0x146E87E' % R(0x10), '146e0a0 record (je uses the add)'),
        (0x146FE43, '4903f5' '488d4e10', 'add rsi, r13\n lea rcx, [rsi+%s]\n jmp game+0x146FE4A' % R(0x10), '146fc40 record copy'),
        (0x146FF8A, '498d4d10' '41b8f0090000', 'lea rcx, [r13+%s]\n mov r8d, 0x9f0\n jmp game+0x146FF94' % R(0x10), '146fc40 record copy'),
        (0x147041B, '0fb65c2440' '498d6e10', 'movzx ebx, byte ptr [rsp+0x40]\n lea rbp, [r14+%s]\n jmp game+0x1470424' % R(0x10), '146fc40 record'),
        (0x14706E4, '488d4610' '4803c1', 'lea rax, [rsi+%s]\n add rax, rcx\n jmp game+0x14706EB' % R(0x10), '1470650 record by peer id'),
        (0x1470837, '448b4c1910', 'mov r9d, dword ptr [rcx+rbx+%s]\n jmp game+0x147083C' % R(0x10), '1470740 exit event: own record field +0'),
        (0x1470863, '448b4c1970', 'mov r9d, dword ptr [rcx+rbx+%s]\n jmp game+0x1470868' % R(0x70), '1470740 exit event: own record field +0x60'),
        (0x14A17F9, '488d5110' '488bd9', 'lea rdx, [rcx+%s]\n mov rbx, rcx\n jmp game+0x14A1800' % R(0x10), 'ctor records loop'),
        # init: the map and reverse map cleared to -1: 4 qword stores for 4+4 entries -> 8 for 8+8; jumps over the
        # three old stores behind it
        (0x1466F26, '48c787d8270000ffffffff', clear + ' jmp game+0x1466F52', '1465840 map/rev clear'),
        # init card loop: r13d is the function's 4 (enum value at 0x1466944, stride at 0x14669EA) and the card loop
        # count from here (sub r13,1 at 0x1466C4D); r12 walks the card x table, which gets eight entries
        (0x1466AD4, '4c8d25dd24d700', 'mov r13d, 8\n lea r12, [rip+1f]\n jmp game+0x1466ADB\n .balign 4\n1:\n' + table, '1465840 card positions'),
        # card loop of 1471740: edi=4 at 0x147176D is also a message value ([rsp+0x54] of event 0xFBC8E61E), so the
        # count is set where the loop starts
        (0x1471848, '488d9e783a0500', 'mov edi, 8\n lea rbx, [rsi+%s]\n jmp game+0x147184F' % C(0x53A78), '1471740 cards'),
        # record base as index*0x9F0, then add 0x10 (disp8), then add L: both adds, the call behind takes no flags
        (0x1467801, '4883c110' '4803ce', 'add rcx, %s\n add rcx, rsi\n jmp game+0x1467808' % R(0x10), '1467790 own record'),
        (0x1467843, '4883c110' '4803ce', 'add rcx, %s\n add rcx, rsi\n jmp game+0x146784A' % R(0x10), '1467790 own record'),
        (0x1467CD2, '4883c110' '4803cf', 'add rcx, %s\n add rcx, rdi\n jmp game+0x1467CD9' % R(0x10), '1467b90 own record'),
        (0x146E68B, '4883c210' '4803d7', 'add rdx, %s\n add rdx, rdi\n jmp game+0x146E692' % R(0x10), '146e0a5 own record'),
        (0x146E6B0, '4883c110' '4803cf', 'add rcx, %s\n add rcx, rdi\n jmp game+0x146E6B7' % R(0x10), '146e0a5 own record'),
        (0x146E9A3, '4883c110' '4803cf', 'add rcx, %s\n add rcx, rdi\n jmp game+0x146E9AA' % R(0x10), '146e0a5 record by own index'),
        (0x146ED5E, '4983c010' '4c03c7', 'add r8, %s\n add r8, rdi\n jmp game+0x146ED65' % R(0x10), '146e9d0 own record'),
        # 146e9d0 states 3 and 1: rbp is the function's 4 (enum, stride, bit) and these two card loops' count
        (0x146EF7F, '488db7f9620500', 'mov ebp, 8\n lea rsi, [rdi+%s]\n jmp game+0x146EF86' % C(0x562F9), '146e9d0 state 3 cards'),
        (0x146F0BB, '488db7f9620500', 'mov ebp, 8\n lea rsi, [rdi+%s]\n jmp game+0x146F0C2' % C(0x562F9), '146e9d0 state 1 cards'),
        # 146fc40: the flags by player index are cleared as 8 bytes at [rsp+0x38] (rax = 0 from 0x146FC61), before
        # the early exit at 0x146FC8C that reads them too
        (0x146FC7E, '8944244c' '448b8e84000000', 'mov qword ptr [rsp+0x38], rax\n mov r9d, dword ptr [rsi+0x84]\n'
                                                ' jmp game+0x146FC89', '146fc40 flags init'),
        # 146fc40 exit: records whose player was not seen lose their peer id (+0x9E8); the game unrolls 0..3
        (0x147061F, '41c6859539270000', ''.join(' cmp byte ptr [rsp+0x%x], 0\n jne %d1f\n mov qword ptr [r13+0x%x], 0\n%d1:\n'
                                                % (0x38 + k, k, NR + k * 0x9F0 + 0x9E8, k) for k in range(4, 8)) +
         ' mov byte ptr [r13+0x273995], 0\n jmp game+0x1470627', '146fc40 records 4..7 peer id clear'),
    ] + CARD_SCALE + FIGURE_SLOT + GUARDS8 + HEIGHTS + LAYOUT

# Eight cards at the game's size overlap: the init card loop sets each card root's scale to 0.5 before its pivot, so
# the panel, its text and its figure image draw at half size. rsi is the card for the whole loop and rcx, rdx are
# loaded again behind the replaced store; the setter returns early on an equal value and tests the parent for null.
CARD_SCALE = [
    (0x1466B38, '48c74424400000003f', 'mov rcx, rsi\n mov rdx, 0x3f0000003f000000\n call game+0x1447ED0\n'
     ' mov qword ptr [rsp+0x40], 0x3f000000\n jmp game+0x1466B41', '1465840 init card loop: card scale 0.5'),
]

# The card's height, 780 in the game, is a store of an immediate at each place that sizes or tweens the card: the
# constructor's root, +0x1C30, +0x1D40 and +0x5848, L's and the card's resizes of +0x1C30 and +0x1D40, and the expand
# and collapse tweens (0x1451BB0, from and to both ways in 0x189E2F0). That is every 780.0 immediate of L and the card
# code but the figure's own 780 x 780 (0x18A0DBE..0x18A0F00). Each store reads the height from the mod's slot instead
# (DATA(CARD_HEIGHT), co_cards.c), so one build can draw the game's cards or shorter ones. rax is kept with a push; no
# flag is written.
# (rva, [rsp+d] or [rbp+d] of the store, its length, note)
CARD_HEIGHT = 0
_HEIGHT_STORES = [
    (0x146C9CE, 'rsp', 0x5C, 8, '146c2f0 +0x1C30 size'),
    (0x146C9EF, 'rsp', 0x64, 8, '146c2f0 +0x1D40 size'),
    (0x146CEDB, 'rsp', 0x64, 8, '146c2f0 +0x1C30 size'),
    (0x146CEFF, 'rsp', 0x5C, 8, '146c2f0 +0x1D40 size'),
    (0x1471363, 'rsp', 0x64, 8, '1470d90 +0x1C30 size'),
    (0x1471384, 'rsp', 0x54, 8, '1470d90 +0x1D40 size'),
    (0x189AE82, 'rbp', -0x2C, 7, '189ae00 card root size'),
    (0x189AEE2, 'rbp', -0x2C, 7, '189ae00 +0x1C30 size (the init end makes it 42)'),
    (0x189AF49, 'rbp', -0x2C, 7, '189ae00 +0x1D40 size (the init end makes it 42)'),
    (0x189B2F5, 'rbp', -0x3C, 7, '189ae00 +0x5848 size'),
    (0x189E149, 'rsp', 0x94, 11, '189e080 +0x1C30 tween to'),
    (0x189E187, 'rsp', 0x94, 11, '189e080 +0x1D40 size'),
    (0x189E23C, 'rsp', 0x94, 11, '189e080 +0x1D40 tween to'),
    (0x189E38D, 'rbp', -0x35, 7, '189e2f0 +0x5848 tween 780 <-> 128'),
    (0x189E3EA, 'rbp', -0x45, 7, '189e2f0 +0x1D40 tween 780 <-> 206'),
]

def _height_bytes(base, disp, size):
    if size == 7: return 'c745' + struct.pack('<b', disp).hex() + '00004344'
    if size == 8: return 'c74424' + struct.pack('<B', disp).hex() + '00004344'
    return 'c78424' + le32(disp) + '00004344'

def _height_cave(rva, base, disp, size, note):
    at = disp + 8 if base == 'rsp' else disp       # the push moves rsp
    return (rva, _height_bytes(base, disp, size),
            'push rax\n mov rax, DATA(%d)\n mov eax, dword ptr [rax]\n mov dword ptr [%s%+#x], eax\n pop rax\n jmp game+0x%X'
            % (CARD_HEIGHT, base, at, rva + size), 'card height from the slot: ' + note)

HEIGHTS = [_height_cave(*h) for h in _HEIGHT_STORES]

# The screen-set update hands L its update with a tail jump (0x1497C45, the case for L of 0x1497920; the only reference
# to 0x1468320), after the canvas refit of the same update. The layout callback (co_cards.c) runs there every frame L is
# updated, with rcx = L. The jump leaves the stack as at a function's entry; CALL aligns it and keeps rcx and xmm0..5.
CARDS_LAYOUT = 11
LAYOUT = [
    (0x1497C45, 'e9d606fdff', 'CALL(%d)\n jmp game+0x1468320' % CARDS_LAYOUT, '1497920 L update: the cards\' layout first'),
]

# The figure preview has four slots (a z table of four, a viewport switch with cases 0 to 3); slot 4 and up take the
# default case, which draws over card 1. They get -1, the "no figure" the game passes for a peer it does not find.
# Every reader that indexes by the slot tests -1 or the spawned flag first. Only cards of a build for eight reach 4.
FIGURE_SLOT = [
    (0x18A1A60, '40534883ec30', 'cmp edx, 4\n jb 1f\n mov edx, -1\n1: push rbx\n sub rsp, 0x30\n jmp game+0x18A1A66',
     '18a1a60 figure slot: 4 and up get none'),
]

# The mod's loadout guards (gen_count_sites.py GUARDS) bound an index by 4 before a map read or a record; they also
# stop -1 ("no peer", "no index"), which the game reads unchecked. In this build each is replaced by the same cave
# bounded by 8 and reading the moved map (the four-player one goes in MOD_OFF).
_M = '0x%x' % (NMAP)
GUARDS8 = [
    (0x146CC05, '418bcd8bbc8ed8270000', 'mov ecx, r13d\n cmp r13d, 8\n jae 1f\n mov edi, dword ptr [rsi+rcx*4+%s]\n'
     ' jmp game+0x146CC0F\n1: mov r13d, dword ptr [rsp+0xe8]\n xor eax, eax\n jmp game+0x146CF99' % _M, 'guard8 146c2f0 peer -> card'),
    (0x146E996, '8b87d4270000', 'mov eax, dword ptr [rdi+0x27d4]\n cmp eax, 8\n jae game+0x146E9BE\n jmp game+0x146E99C',
     'guard8 146e0a5 own index -> record'),
    (0x146D320, '8bc24c8bc1', 'cmp edx, 8\n jae 1f\n mov eax, edx\n mov r8, rcx\n jmp game+0x146D325\n1: mov eax, 0xffffffff\n ret',
     'guard8 146d320 card for an index'),
    (0x875B50, '418bc9458b8c88d8270000', 'mov ecx, r9d\n cmp r9d, 8\n jae game+0x875BA8\n mov r9d, dword ptr [r8+rcx*4+%s]\n'
     ' jmp game+0x875B5B' % _M, 'guard8 8758d0 map read'),
    (0xB72AC6, '8bc1418b8c80d8270000', 'mov eax, ecx\n cmp ecx, 8\n jae game+0xB72B2A\n mov ecx, dword ptr [r8+rax*4+%s]\n'
     ' jmp game+0xB72AD0' % _M, 'guard8 b727d0 map read'),
    (0xB90143, '418b8c82d8270000', 'cmp eax, 8\n jae game+0xB90168\n mov ecx, dword ptr [r10+rax*4+%s]\n jmp game+0xB9014B' % _M,
     'guard8 b90040 map read'),
    (0xB90373, '418bbc86d8270000', 'cmp eax, 8\n jae game+0xB903A6\n mov edi, dword ptr [r14+rax*4+%s]\n jmp game+0xB9037B' % _M,
     'guard8 b90260 map read'),
    (0xB90513, '418b8c86d8270000', 'cmp eax, 8\n jae game+0xB90580\n mov ecx, dword ptr [r14+rax*4+%s]\n jmp game+0xB9051B' % _M,
     'guard8 b903f0 map read'),
    (0xBA5953, '418bbc86d8270000', 'cmp eax, 8\n jae game+0xBA5986\n mov edi, dword ptr [r14+rax*4+%s]\n jmp game+0xBA595B' % _M,
     'guard8 ba5840 map read'),
    (0xBAD843, '418b8c82d8270000', 'cmp eax, 8\n jae game+0xBAD868\n mov ecx, dword ptr [r10+rax*4+%s]\n jmp game+0xBAD84B' % _M,
     'guard8 bad740 map read'),
    (0x873328, '8bc2418b8c81d8270000', 'mov eax, edx\n cmp edx, 8\n jae game+0x873350\n mov ecx, dword ptr [r9+rax*4+%s]\n'
     ' jmp game+0x873332' % _M, 'guard8 873310 map read'),
    (0x1471E69, '8bc28b9481d8270000', 'mov eax, edx\n cmp edx, 8\n jae game+0x1471E90\n mov edx, dword ptr [rcx+rax*4+%s]\n'
     ' jmp game+0x1471E72' % _M, 'guard8 1471e60 map read'),
    (0x1471AC0, '418bc08b8c87d8270000', 'mov eax, r8d\n cmp r8d, 8\n jae game+0x1471AE8\n mov ecx, dword ptr [rdi+rax*4+%s]\n'
     ' jmp game+0x1471ACA' % _M, 'guard8 14719f0 map read'),
    (0x1471BD7, '418bc08b9c87d8270000', 'mov eax, r8d\n cmp r8d, 8\n jae game+0x1471C0D\n mov ebx, dword ptr [rdi+rax*4+%s]\n'
     ' jmp game+0x1471BE1' % _M, 'guard8 1471b00 map read'),
    (0x1471D30, '418bc08b8c86d8270000', 'mov eax, r8d\n cmp r8d, 8\n jae game+0x1471DB1\n mov ecx, dword ptr [rsi+rax*4+%s]\n'
     ' jmp game+0x1471D3A' % _M, 'guard8 1471c50 map read'),
]

# Mod sites (ComplexOperations gen_count_sites.py) that this build replaces: their bytes are rewritten here, or
# they stop players 4..7 on purpose. Every other mod site stays. (verify.py lists the byte overlaps.)
# Stay on in both builds: 0x146CC30 (no free card: skip; no count in it) and 0x146C3D5 (slot of 8 and up: skip).
MOD_OFF = [0x146CC05, 0x146E996, 0x146D320, 0x875B50, 0xB72AC6, 0xB90143, 0xB90373, 0xB90513, 0xBA5953,
           0xBAD843, 0x873328, 0x1471E69, 0x1471AC0, 0x1471BD7, 0x1471D30,            # GUARDS: GUARDS8 replaces them
           0x14670B8, 0x14697C1, 0x146C46A, 0x14716AD]                                 # GUARD_SITES: the game tests -1

# --- F8. SyncedLoadoutSystem: 4 x 0x9F8 {record 0x9F0, peer id} keyed by peer id, the 0x27E0-byte member at
# systems + 0x60E7208 (systems = [0x3326340] + 0x1024220; next member at +0x60E99E8, so it cannot grow). Reached only
# by pointer: [0x346D5F0] (0x13F2D12) and the three registry entries {ptr, 0x11D} that 0x13F4CAC adds (lists at
# registry +0x2080, +0x7238, +0x7250; registry = [0x3326E68]). Both are set in engine init (0xAB1D55) before the first
# session init or state constructor (0xAB28F5), where the mod applies its sites. The generator puts SYNCED_SITES in
# their own group of the build for eight; the mod's C code moves the table (SYNCED_MOVE) before it writes them and
# moves it back after it has put them back.
# The three lists' rooms: entries from align8(count + 4) up to the next list's count (+0x20C0, +0x7248, +0x7260).
SYNCED_MOVE = {'pointer': 0x346D5F0, 'registry_type': 0x11D, 'registry_lists': (0x2078, 0x7230, 0x7248), 'registry_rooms': (4, 1, 1),
               'old_size': 0x27E0, 'new_size': 8 * 0x9F8, 'entry': 0x9F8, 'key': 0x9F0,
               'engine': 0x3326340, 'member': 0x1024220 + 0x60E7208, 'registry': 0x3326E68}
SYNCED_SITES = [
    (0x1381254, '41b8e0270000', '41b8' + le32(8 * 0x9F8), '1381220 (loadout init): memset of the table'),
    (0x146C7C8, '83f904', '83f908', '146c2f0 hot-join pass: peer search'),
    (0x13951A9, '83fd04', '83fd08', '1394fd0: player index 4..7 look up their own peer, not the local one'),
    (0x1395221, '83f904', '83f908', '1394fd0: peer search'),
    (0xB9E790, '83f904', '83f908', 'b9e670 store: peer search'),
    (0xB9E7F4, '4183fa04', '4183fa08', 'b9e670 store: free entry search'),
    (0x1381214, '83f804', '83f808', '1381200 (no caller found): peer search'),
    (0x1381632, '83f804', '83f808', '13815f0 (no caller found): peer search'),
    (0x1381A0E, '4183f804', '4183f808', '13819b0 (no caller found): peer search'),
    (0x1381A73, '4183fa04', '4183fa08', '13819b0 (no caller found): free entry search'),
    # registry walkers, case 0x11D: a peer that left loses its entry (callers 0xBA55CD; 0xB90A9E, 0xB90FAD, 0x10849DE,
    # 0x108760D)
    (0x1087122, '83f804', '83f808', '1087010 peers removed: peer search'),
    (0xB5F1F2, '83f804', '83f808', 'b5f140 peers removed: peer search'),
]

def build(dump=None):
    inplace = list((r, o, n, note) for r, o, n, note in ALLOC)
    for rva, n, at, ov, rg in sorted(set(DISP)):
        if rva in DISP_SKIP: continue
        old = dump[rva:rva + n].hex() if dump else None
        nv = moved(ov, rg)
        if dump:
            b = bytearray(dump[rva:rva + n]); assert struct.unpack_from('<I', b, at)[0] == ov, hex(rva)
            struct.pack_into('<I', b, at, nv); inplace.append((rva, old, b.hex(), '%s 0x%x -> 0x%x' % (rg, ov, nv)))
        else: inplace.append((rva, None, None, '%s 0x%x -> 0x%x' % (rg, ov, nv)))
    for rva, old in CARD_IMM:
        ov = struct.unpack('<I', bytes.fromhex(old)[-4:])[0]
        inplace.append((rva, old, old[:-8] + le32(moved(ov, 'CARD')), 'CARD imm 0x%x' % ov))
    inplace += BOUNDS + STACK + _combined()
    return sorted(inplace), sorted(_caves())

if __name__ == '__main__':
    import sys
    dump = open(sys.argv[1], 'rb').read()
    ip, cv = build(dump)
    print('new size 0x%x; %d in-place sites, %d caves' % (NEW_SIZE, len(ip), len(cv)))
    for r, o, n, note in ip: print('  %08X  %-30s %-30s %s' % (r, o, n, note))
    for r, rep, asm, note in cv: print('  cave %08X  %-24s %s' % (r, rep, note))
