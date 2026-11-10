# Galactic map cursors for eight players (game build 25480438): the site lists scripts/gen_ui_sites.py and
# scripts/gen_count_sites.py read. The map object G = S + 0x4CD1F0 (S = [0x3326AA0], the system at component block +
# 0x1F7AE8, the next system right behind) cannot grow, so its three player-indexed arrays move to a block of the mod's
# within 2 GB of the component block:
#   CUR  cursors 4 x 0x298 at G+0x80E18 (S+0x54E008)   -> NEW+0x0000, 8 x 0x298
#   HID  hover target u32[4] at G+0x8D6C4 (S+0x55A8B4)  -> NEW+0x14C0, u32[8]
#   HKD  hover kind   u32[4] at G+0x8D6D4 (S+0x55A8C4)  -> NEW+0x14E0, u32[8]
# A relocated displacement becomes old + D + K[region], D = NEW - (block + OLD): the base register is G or S, both a
# fixed distance from the block, so one D serves both. Formats:
#   REL:     (rva, instruction length, offset of the 4-byte field, old value, region)
#   INPLACE: (rva, old hex, new hex, note)
#   CAVES:   (rva, replaced hex, asm, note, [(old value in the asm, region), ...]): those fields get D + K like REL
#   GUARD4:  (rva, replaced hex, asm): the guards while the cursors are four; GUARD8 the same entries with bound 8
OLD = 0x745AF0                              # block-relative address of the old cursor table (0x1F7AE8 + 0x4CD1F0 + 0x80E18)
SIZE = 0x1500
NEW = {'CUR': 0x0, 'HID': 0x14C0, 'HKD': 0x14E0}
OLD_G = {'CUR': 0x80E18, 'HID': 0x8D6C4, 'HKD': 0x8D6D4}
K = {r: NEW[r] - (OLD_G[r] - OLD_G['CUR']) for r in NEW}
G_RANGE = {'CUR': (0x80E18, 0x81878), 'HID': (0x8D6C4, 0x8D6D4), 'HKD': (0x8D6D4, 0x8D6E4)}
S_OFF = 0x4CD1F0                            # an S-relative value less S_OFF is the G-relative one


def region_of(v):
    for r, (lo, hi) in G_RANGE.items():
        if lo <= v < hi or lo <= v - S_OFF < hi:
            return r
    return None


# Every displacement into the three arrays in the image (an image-wide sweep of G, S, block and game-object bases).
REL = [
    (0x588EDA, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [r8+rcx*1+0x80e24]
    (0x72F697, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rbx+rdi*1+0x80e24]
    (0x72F69F, 8, 4, 0x80FA8, 'CUR'),  # mov    QWORD PTR [rbx+rdi*1+0x80fa8],rdx
    (0x72F6AC, 7, 3, 0x80FB8, 'CUR'),  # lea    rcx,[rdi+0x80fb8]
    (0x72F6B3, 9, 5, 0x80FB0, 'CUR'),  # movss  DWORD PTR [rbx+rdi*1+0x80fb0],xmm0
    (0x72F6CC, 7, 3, 0x81030, 'CUR'),  # lea    rcx,[rdi+0x81030]
    (0x72F719, 7, 3, 0x54E1A8, 'CUR'),  # lea    rcx,[rbx+0x54e1a8]
    (0x72F728, 7, 3, 0x54E220, 'CUR'),  # lea    rcx,[rbx+0x54e220]
    (0x72F796, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rcx+rbx*1+0x80e24]
    (0x72F840, 8, 3, 0x80E18, 'CUR'),  # cmp    BYTE PTR [rsi+rdi*1+0x80e18],0x0
    (0x72F873, 8, 3, 0x80E18, 'CUR'),  # mov    BYTE PTR [rsi+rdi*1+0x80e18],0x1
    (0x72F883, 9, 5, 0x80E24, 'CUR'),  # addss  xmm7,DWORD PTR [rsi+rdi*1+0x80e24]
    (0x72F88C, 9, 5, 0x80E28, 'CUR'),  # addss  xmm6,DWORD PTR [rsi+rdi*1+0x80e28]
    (0x72F89A, 9, 5, 0x80E24, 'CUR'),  # movss  DWORD PTR [rsi+rdi*1+0x80e24],xmm7
    (0x72F8A3, 9, 5, 0x80E28, 'CUR'),  # movss  DWORD PTR [rsi+rdi*1+0x80e28],xmm6
    (0x72F8E8, 8, 4, 0x55A8B4, 'HID'),  # mov    eax,DWORD PTR [rcx+r8*4+0x55a8b4]
    (0x72F8F7, 9, 4, 0x55A8C4, 'HKD'),  # cmp    DWORD PTR [rcx+r8*4+0x55a8c4],0x1   (leaf 0x72F8E0, second exit)
    (0x72F919, 8, 4, 0x55A8C4, 'HKD'),  # mov    eax,DWORD PTR [r10+rcx*4+0x55a8c4]
    (0x72F924, 8, 4, 0x55A8B4, 'HID'),  # mov    eax,DWORD PTR [r10+rcx*4+0x55a8b4]
    (0x72F950, 8, 4, 0x54E298, 'CUR'),  # movzx  eax,BYTE PTR [rcx+rax*1+0x54e298]
    (0x72F970, 8, 4, 0x54E299, 'CUR'),  # movzx  eax,BYTE PTR [rcx+rax*1+0x54e299]
    (0x72F991, 8, 4, 0x54E014, 'CUR'),  # mov    rcx,QWORD PTR [rcx+rax*1+0x54e014]
    (0x72F9E9, 9, 5, 0x54E02C, 'CUR'),  # movss  xmm6,DWORD PTR [rcx+rdx*1+0x54e02c]
    (0x72F9FA, 9, 5, 0x54E024, 'CUR'),  # movss  xmm4,DWORD PTR [rcx+rdx*1+0x54e024]
    (0x72FA03, 9, 5, 0x54E028, 'CUR'),  # movss  xmm5,DWORD PTR [rcx+rdx*1+0x54e028]
    (0x730126, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rbx+rdi*1+0x80e24]
    (0x73012E, 8, 4, 0x80FA8, 'CUR'),  # mov    QWORD PTR [rbx+rdi*1+0x80fa8],rdx
    (0x73013B, 7, 3, 0x80FB8, 'CUR'),  # lea    rcx,[rdi+0x80fb8]
    (0x730142, 9, 5, 0x80FB0, 'CUR'),  # movss  DWORD PTR [rbx+rdi*1+0x80fb0],xmm0
    (0x73015B, 7, 3, 0x81030, 'CUR'),  # lea    rcx,[rdi+0x81030]
    (0x7301AA, 7, 3, 0x54E1A8, 'CUR'),  # lea    rcx,[rbx+0x54e1a8]
    (0x7301B9, 7, 3, 0x54E220, 'CUR'),  # lea    rcx,[rbx+0x54e220]
    (0x7301DA, 7, 3, 0x54E220, 'CUR'),  # lea    rbx,[rcx+0x54e220]
    (0x730247, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rcx+rbx*1+0x80e24]
    (0xB8CB9E, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rbx+rdi*1+0x80e24]
    (0xB8CBA6, 8, 4, 0x80FA8, 'CUR'),  # mov    QWORD PTR [rbx+rdi*1+0x80fa8],rdx
    (0xB8CBB3, 7, 3, 0x80FB8, 'CUR'),  # lea    rcx,[rbx+0x80fb8]
    (0xB8CBBA, 9, 5, 0x80FB0, 'CUR'),  # movss  DWORD PTR [rbx+rdi*1+0x80fb0],xmm0
    (0xB8CBD3, 7, 3, 0x81030, 'CUR'),  # lea    rcx,[rbx+0x81030]
    (0xB8CCC8, 7, 3, 0x54E1A8, 'CUR'),  # lea    rcx,[rbx+0x54e1a8]
    (0xB8CCD7, 7, 3, 0x54E220, 'CUR'),  # lea    rcx,[rbx+0x54e220]
    (0xB8CD5F, 7, 3, 0x54E220, 'CUR'),  # add    rbx,0x54e220
    (0xB8CE18, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rcx+rbx*1+0x80e24]
    (0xBACF9B, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rbx+rdi*1+0x80e24]
    (0xBACFA3, 8, 4, 0x80FA8, 'CUR'),  # mov    QWORD PTR [rbx+rdi*1+0x80fa8],rdx
    (0xBACFB0, 7, 3, 0x80FB8, 'CUR'),  # lea    rcx,[rbx+0x80fb8]
    (0xBACFB7, 9, 5, 0x80FB0, 'CUR'),  # movss  DWORD PTR [rbx+rdi*1+0x80fb0],xmm0
    (0xBACFD0, 7, 3, 0x81030, 'CUR'),  # lea    rcx,[rbx+0x81030]
    (0xBB1BD8, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rcx+rbx*1+0x80e24]
    (0xBB268F, 7, 3, 0x54E220, 'CUR'),  # add    rbx,0x54e220
    (0xBB4988, 7, 3, 0x54E1A8, 'CUR'),  # lea    rcx,[rbx+0x54e1a8]
    (0xBB4997, 7, 3, 0x54E220, 'CUR'),  # lea    rcx,[rbx+0x54e220]
    (0x142BB8D, 7, 3, 0x80EB8, 'CUR'),  # lea    r14,[rbx+0x80eb8]
    (0x142C9CE, 7, 3, 0x80EB8, 'CUR'),  # lea    rbx,[rdi+0x80eb8]
    (0x142D7D2, 7, 3, 0x80E18, 'CUR'),  # lea    rbx,[rdi+0x80e18]
    (0x142DA78, 7, 3, 0x810A5, 'CUR'),  # lea    r14,[rsi+0x810a5]
    (0x142EA70, 9, 4, 0x80E18, 'CUR'),  # cmp    BYTE PTR [rbx+r14*1+0x80e18],0x0
    (0x142EA9C, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [rbx+r14*1+0x80e24]
    (0x142EBA1, 7, 3, 0x80E30, 'CUR'),  # add    r14,0x80e30
    (0x142EF8E, 9, 5, 0x80E24, 'CUR'),  # movss  xmm7,DWORD PTR [rcx+rdi*1+0x80e24]
    (0x142EF9E, 10, 6, 0x80E28, 'CUR'),  # movss  xmm8,DWORD PTR [rcx+rdi*1+0x80e28]
    (0x142F82F, 7, 3, 0x80E40, 'CUR'),  # lea    rcx,[rbx+0x80e40]
    (0x142F83B, 7, 3, 0x80EB8, 'CUR'),  # lea    rcx,[rbx+0x80eb8]
    (0x142F884, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rdi+rcx*1+0x80e24]
    (0x142F88C, 8, 4, 0x80FA8, 'CUR'),  # mov    QWORD PTR [rdi+rcx*1+0x80fa8],rdx
    (0x142F899, 9, 5, 0x80FB0, 'CUR'),  # movss  DWORD PTR [rdi+rbx*1+0x80fb0],xmm0
    (0x142F8A2, 7, 3, 0x80FB8, 'CUR'),  # lea    rcx,[rbx+0x80fb8]
    (0x142F8BA, 7, 3, 0x81030, 'CUR'),  # lea    rcx,[rbx+0x81030]
    (0x142F90F, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [rdx+rcx*1+0x80e24]
    (0x142F9D3, 8, 4, 0x8D6C4, 'HID'),  # mov    eax,DWORD PTR [rcx+r8*4+0x8d6c4]
    (0x142F9E0, 9, 4, 0x8D6D4, 'HKD'),  # cmp    DWORD PTR [rcx+r8*4+0x8d6d4],0x1
    (0x142FA03, 8, 4, 0x8D6D4, 'HKD'),  # mov    eax,DWORD PTR [rcx+r10*4+0x8d6d4]
    (0x142FA0E, 8, 4, 0x8D6C4, 'HID'),  # mov    eax,DWORD PTR [rcx+r10*4+0x8d6c4]
    (0x142FA29, 8, 4, 0x810A8, 'CUR'),  # movzx  eax,BYTE PTR [rdx+rcx*1+0x810a8]
    (0x142FA49, 8, 4, 0x810A9, 'CUR'),  # movzx  eax,BYTE PTR [rdx+rcx*1+0x810a9]
    (0x142FEE2, 8, 4, 0x8D6C4, 'HID'),  # mov    r10d,DWORD PTR [rcx+r9*4+0x8d6c4]
    (0x142FEF3, 7, 3, 0x8D6D4, 'HKD'),  # lea    rdx,[rcx+0x8d6d4]
    (0x142FEFA, 7, 3, 0x80E18, 'CUR'),  # add    rcx,0x80e18
    (0x142FF11, 8, 4, 0x8D6D4, 'HKD'),  # mov    eax,DWORD PTR [rbx+r9*4+0x8d6d4]
    (0x142FF31, 8, 4, 0x8D6D4, 'HKD'),  # mov    r8d,DWORD PTR [rbx+rdi*4+0x8d6d4]
    (0x142FF47, 11, 3, 0x8D6C4, 'HID'),  # mov    DWORD PTR [rbx+rdi*4+0x8d6c4],0xffffffff
    (0x142FF52, 11, 3, 0x8D6D4, 'HKD'),  # mov    DWORD PTR [rbx+rdi*4+0x8d6d4],0x0
    (0x142FF68, 12, 4, 0x8D6C4, 'HID'),  # mov    DWORD PTR [rbx+r9*4+0x8d6c4],0xffffffff
    (0x142FF74, 12, 4, 0x8D6D4, 'HKD'),  # mov    DWORD PTR [rbx+r8*4+0x8d6d4],0x0
    (0x143003C, 8, 4, 0x80E24, 'CUR'),  # mov    rdx,QWORD PTR [r12+rsi*1+0x80e24]
    (0x14300C1, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [rcx+rsi*1+0x80e24]
    (0x1430184, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [rcx+rsi*1+0x80e24]
    (0x1430245, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [rcx+rsi*1+0x80e24]
    (0x1430302, 8, 4, 0x80E24, 'CUR'),  # mov    rax,QWORD PTR [rcx+rsi*1+0x80e24]
    (0x1430394, 8, 4, 0x8D6C4, 'HID'),  # cmp    r14d,DWORD PTR [rsi+rdx*4+0x8d6c4]
    (0x143039E, 8, 4, 0x8D6D4, 'HKD'),  # cmp    r13d,DWORD PTR [rsi+rdx*4+0x8d6d4]
    (0x14303B3, 8, 4, 0x8D6C4, 'HID'),  # mov    DWORD PTR [rsi+rdi*4+0x8d6c4],r14d
    (0x14303BE, 8, 4, 0x8D6D4, 'HKD'),  # mov    DWORD PTR [rsi+rdi*4+0x8d6d4],r13d
    (0x14303F3, 7, 3, 0x80E40, 'CUR'),  # lea    rcx,[rdi+0x80e40]
    (0x1430402, 7, 3, 0x80EB8, 'CUR'),  # lea    rcx,[rdi+0x80eb8]
    (0x1430635, 7, 3, 0x80EB5, 'CUR'),  # lea    rbx,[rsi+0x80eb5]
    (0x143064F, 7, 3, 0x8D6D4, 'HKD'),  # lea    r15,[rsi+0x8d6d4]
    (0x143164D, 9, 5, 0x80E2C, 'CUR'),  # movss  xmm0,DWORD PTR [rcx+rsi*1+0x80e2c]
    (0x1431656, 9, 5, 0x80E30, 'CUR'),  # movss  xmm1,DWORD PTR [rcx+rsi*1+0x80e30]
    (0x1431A72, 9, 5, 0x80E2C, 'CUR'),  # movss  xmm0,DWORD PTR [rcx+rsi*1+0x80e2c]
    (0x1431A7B, 9, 5, 0x80E30, 'CUR'),  # movss  xmm1,DWORD PTR [rcx+rsi*1+0x80e30]
    (0x14329F1, 7, 3, 0x810BC, 'CUR'),  # lea    rbx,[rcx+0x810bc]
    (0x14330AB, 10, 6, 0x80E24, 'CUR'),  # movss  xmm8,DWORD PTR [rdi+rcx*1+0x80e24]
    (0x14330B5, 9, 5, 0x80E28, 'CUR'),  # movss  xmm7,DWORD PTR [rdi+rcx*1+0x80e28]
    (0x14331B8, 8, 4, 0x80E24, 'CUR'),  # mov    QWORD PTR [rdi+rbx*1+0x80e24],rax
    (0x14331C0, 9, 5, 0x80E28, 'CUR'),  # movss  xmm7,DWORD PTR [rdi+rbx*1+0x80e28]
    (0x14331C9, 10, 6, 0x80E24, 'CUR'),  # movss  xmm8,DWORD PTR [rdi+rbx*1+0x80e24]
    (0x143328C, 8, 4, 0x80E24, 'CUR'),  # mov    QWORD PTR [rdi+rbx*1+0x80e24],rax
    (0x14332F7, 7, 2, 0x80E18, 'CUR'),  # cmp    BYTE PTR [rbx+0x80e18],0x0
    (0x1433330, 7, 2, 0x80E18, 'CUR'),  # mov    BYTE PTR [rbx+0x80e18],0x1
    (0x1433341, 7, 2, 0x80E18, 'CUR'),  # mov    BYTE PTR [rbx+0x80e18],0x0
    (0x1433350, 7, 3, 0x80E40, 'CUR'),  # lea    rcx,[rbx+0x80e40]
    (0x1433363, 7, 3, 0x80EB8, 'CUR'),  # lea    rcx,[rbx+0x80eb8]
    (0x14333C3, 8, 3, 0x80E18, 'CUR'),  # cmp    BYTE PTR [rsi+rcx*1+0x80e18],0x0
    (0x14333F6, 8, 3, 0x80E18, 'CUR'),  # mov    BYTE PTR [rsi+rdi*1+0x80e18],0x1
    (0x1433406, 9, 5, 0x80E24, 'CUR'),  # addss  xmm7,DWORD PTR [rsi+rdi*1+0x80e24]
    (0x143340F, 9, 5, 0x80E28, 'CUR'),  # addss  xmm6,DWORD PTR [rsi+rdi*1+0x80e28]
    (0x143341D, 9, 5, 0x80E24, 'CUR'),  # movss  DWORD PTR [rsi+rdi*1+0x80e24],xmm7
    (0x1433426, 9, 5, 0x80E28, 'CUR'),  # movss  DWORD PTR [rsi+rdi*1+0x80e28],xmm6
    (0x143362F, 8, 4, 0x80E24, 'CUR'),  # mov    QWORD PTR [r12+rcx*1+0x80e24],rax
    (0x14336C9, 8, 4, 0x80E18, 'CUR'),  # cmp    BYTE PTR [r12+r14*1+0x80e18],bl
    (0x14336F0, 9, 4, 0x80E18, 'CUR'),  # mov    BYTE PTR [r12+r14*1+0x80e18],0x1
    (0x1433775, 9, 5, 0x80E34, 'CUR'),  # movss  xmm6,DWORD PTR [rcx+rdx*1+0x80e34]
    (0x143377E, 9, 5, 0x80E3C, 'CUR'),  # movss  xmm4,DWORD PTR [rcx+rdx*1+0x80e3c]
    (0x14337AC, 9, 5, 0x80E38, 'CUR'),  # movss  xmm5,DWORD PTR [rcx+rdx*1+0x80e38]
    (0x18BF5F9, 8, 4, 0x55A8B4, 'HID'),  # mov    r14d,DWORD PTR [rdx+rcx*4+0x55a8b4]
    (0x18BF609, 8, 4, 0x55A8C4, 'HKD'),  # mov    r15d,DWORD PTR [rdx+rcx*4+0x55a8c4]
    (0x18BF611, 9, 5, 0x54E298, 'CUR'),  # movzx  r12d,BYTE PTR [rax+rdx*1+0x54e298]
    (0x18BF61A, 9, 5, 0x54E299, 'CUR'),  # movzx  r13d,BYTE PTR [rax+rdx*1+0x54e299]
    (0x18C047F, 9, 5, 0x54E02C, 'CUR'),  # movss  xmm6,DWORD PTR [rcx+rdx*1+0x54e02c]
    (0x18C0490, 9, 5, 0x54E024, 'CUR'),  # movss  xmm4,DWORD PTR [rcx+rdx*1+0x54e024]
    (0x18C0499, 9, 5, 0x54E028, 'CUR'),  # movss  xmm5,DWORD PTR [rcx+rdx*1+0x54e028]
    (0x18C0C61, 8, 3, 0x54E298, 'CUR'),  # cmp    BYTE PTR [rax+rcx*1+0x54e298],0x0
    (0x18C0C8F, 8, 4, 0x54E014, 'CUR'),  # mov    rax,QWORD PTR [rax+rcx*1+0x54e014]
    (0x18C1143, 8, 4, 0x80E18, 'CUR'),  # cmp    BYTE PTR [r14+rbp*1+0x80e18],bl
    (0x18C1170, 9, 4, 0x80E18, 'CUR'),  # mov    BYTE PTR [r14+rbp*1+0x80e18],0x1
    (0x18C1181, 10, 6, 0x80E24, 'CUR'),  # addss  xmm7,DWORD PTR [r14+rbp*1+0x80e24]
    (0x18C118B, 10, 6, 0x80E28, 'CUR'),  # addss  xmm6,DWORD PTR [r14+rbp*1+0x80e28]
    (0x18C119A, 10, 6, 0x80E24, 'CUR'),  # movss  DWORD PTR [r14+rbp*1+0x80e24],xmm7
    (0x18C11A4, 10, 6, 0x80E28, 'CUR'),  # movss  DWORD PTR [r14+rbp*1+0x80e28],xmm6
]

INPLACE = [
    # counts of the loops over all cursors
    (0x142BB94, '41bc04000000', '41bc08000000', '142b990 ctor: cursors'),
    (0x142C9D5, 'be04000000', 'be08000000', '142c9b0 release: cursors'),
    (0x7301E1, 'bf04000000', 'bf08000000', '7301d0 (S): cursor tweens'),
    (0xB8CD5A, 'bf04000000', 'bf08000000', 'b8cd10 handler: cursor tweens'),
    (0xBB268A, 'bf04000000', 'bf08000000', 'bb2640 handler copy: cursor tweens'),
    (0x142DF8E, '4183fd04', '4183fd08', '142d870 loop 1 over cursors'),
    (0x142EDC6, '83f804', '83f808', '142d870 loop 2 over cursors (remote sounds)'),
    (0x142D7FA, '4183fc04', '4183fc08', '142cb90: active cursors'),
    (0x142FF2B, '4183f804', '4183f808', '142fed0: cursors and hover kinds'),
    (0x1430DE3, '4183fc04', '4183fc08', '1430450: cursor labels'),
    (0x1432A00, 'be02000000', 'be04000000', '14329b0: pairs of cursors (loop body does two)'),
    # bounds the game already has: 4 -> 8
    (0x72F9D0, '4183f804', '4183f808', '72f9a0 index bound'),
    (0x143375C, '4183f804', '4183f808', '1433730 index bound'),
    (0x18C0467, '83fb04', '83fb08', '18bf470 index bound'),
    # hover arrays addressed from each other: kind[i] - 0x10 = target[i] becomes - 0x20
    (0x142FF0B, '443952f0', '443952e0', '142fed0: [rdx-0x10] target of the kind at rdx'),
    (0x143072D, '418b57f0', '418b57e0', '1430450: [r15-0x10] target of the kind at r15'),
    # own-index tests: "-1 = none" becomes "8 or more = none" (memory safety for 0x606820's unchecked result)
    (0x142EA5C, '4183ffff0f849f000000', '4183ff080f839f000000', '142d870 own cursor'),
    (0x142EF50, '83f9ff0f849f010000', '83f9080f839f010000', '142ee70 camera'),
    (0x18C0F2F, '83ffff0f84a2030000', '83ff080f83a2030000', '18c0ec0 map input'),
    (0x18C0C1A, '83faff0f848c020000', '83fa080f838c020000', '18c0bc0 drop point'),
    (0x18BF5DF, '83f8ff0f8462010000', '83f8080f8362010000', '18bf470 UI state'),
    (0x143163D, '4183ffff747c', '4183ff08737c', '1430450 label fade'),
    (0x1431A62, '4183ffff747f', '4183ff08737f', '1430450 label fade'),
]

CAVES = [
    # 142bed0 (map reset): the hover arrays were cleared as two qwords each; eight entries need four.
    (0x142C867, '48c783c4d60800ffffffff', """
 lea rdx, [rbx+0x8d6c4]
 mov qword ptr [rdx], -1
 mov qword ptr [rdx+8], -1
 mov qword ptr [rdx+0x10], -1
 mov qword ptr [rdx+0x18], -1
 lea rdx, [rbx+0x8d6d4]
 mov qword ptr [rdx], rcx
 mov qword ptr [rdx+8], rcx
 mov qword ptr [rdx+0x10], rcx
 mov qword ptr [rdx+0x18], rcx
 jmp game+0x142C88B""", '142bed0 hover arrays x8 (skips 0x142C872..0x142C88A)', [(0x8D6C4, 'HID'), (0x8D6D4, 'HKD')]),
    # 142d870 stack: u32[8] player index -> number of the other player at rbp-0x60..-0x41 (the old u32[4] and the
    # 3-entry table at rbp-0x50, whose values move into the reader cave); the -0x70 and -0x80 tables stay.
    (0x142EB0E, '48c745a0ffffffff', """
 mov qword ptr [rbp-0x60], -1
 mov qword ptr [rbp-0x58], -1
 mov qword ptr [rbp-0x50], -1
 mov qword ptr [rbp-0x48], -1
 mov dword ptr [rbp-0x70], 0x2e1387c5
 mov dword ptr [rbp-0x6c], 0xed9e2bbd
 mov dword ptr [rbp-0x68], 0x30e087b2
 mov dword ptr [rbp-0x80], 0x25065dd3
 mov dword ptr [rbp-0x7c], 0x6e61441f
 mov dword ptr [rbp-0x78], 0x833e051c
 jmp game+0x142EB5D""", '142d870 stack map x8 (skips 0x142EB16..0x142EB5C)', []),
    # the numbering loop: index from the player manager slot; 8 or more gets no number (was the mod's G5 at 4 + clamp)
    (0x142EB70, '8b02413bc7740a', """
 mov eax, dword ptr [rdx]
 cmp eax, r15d
 je game+0x142EB81
 cmp eax, 8
 jae game+0x142EB81
 jmp game+0x142EB77""", '142d870 numbering, bound 8', []),
    # the three sound ids of the other player's number n (0..6): the game has three of each, so n mod 3
    (0x142ED37, '8b54859' + '08b7c85b08b5c8580', """
 lea edx, [rax-3]
 cmp eax, 3
 cmovae eax, edx
 lea edx, [rax-3]
 cmp eax, 3
 cmovae eax, edx
 mov edx, dword ptr [rbp+rax*4-0x70]
 mov ebx, dword ptr [rbp+rax*4-0x80]
 lea rdi, [rip+2f]
 mov edi, dword ptr [rdi+rax*4]
 jmp game+0x142ED43
2: .long 0x56d7e815, 0xce12b5a6, 0xd2d33c0c""", '142d870 remote cursor sounds by n mod 3', []),
]

# The guards while the cursors are four: the build for four has them for good, the build for eight until the arrays
# move. Message handlers (two copies each) leave by their own empty-list exit; the member versions return at once: the
# call they end in gives no value either. 0x72F5F0 and 0x730080 write a cursor by index with no bound as well.
GUARD4 = [
    (0xB8CAE3, '45 39 be 20 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CC50\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xB8CAEA'),
    (0xBACED1, '45 39 be 20 5e 00 00', 'cmp ebp, 4\n jae game+0xBAD04D\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xBACED8'),
    (0xB8CC8A, '39 be 38 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CCF6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xB8CC90'),
    (0xBB494E, '39 be 38 5e 00 00', 'cmp ebp, 4\n jae game+0xBB49B6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xBB4954'),
    (0xB8CDDA, '39 be 68 5e 00 00', 'cmp ebp, 4\n jae game+0xB8CE6E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xB8CDE0'),
    (0xBB1B9E, '39 be 68 5e 00 00', 'cmp ebp, 4\n jae game+0xBB1C2E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xBB1BA4'),
    (0x72F700, '40 53 48 83 ec 20', 'cmp edx, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F706\n1: ret'),
    (0x72F780, '40 53 48 83 ec 20', 'cmp edx, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F786\n1: ret'),
    (0x730190, '40 53 48 83 ec 20', 'cmp r8d, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x730196\n1: ret'),
    (0x72F5F0, '48 89 5c 24 08', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x72F5F5\n1: ret'),
    (0x730080, '48 89 5c 24 08', 'cmp r8d, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x730085\n1: ret'),
    # The every-frame update numbers the other players into four dwords on the stack; the numbers index tables of
    # three, so a fourth other player gets none.
    (0x142EB70, '8b 02 41 3b c7 74 0a', 'mov eax, dword ptr [rdx]\n cmp eax, r15d\n je 1f\n cmp eax, 4\n jae 1f\n cmp r8d, 3\n jae 1f\n'
                                        ' jmp game+0x142EB77\n1: jmp game+0x142EB81'),
    # The functions that write a cursor by the index in edx. None gives a value: no caller reads one, and four of the
    # eight have no caller in the game's code.
    (0x142FF90, '89 54 24 10 53', 'cmp edx, 4\n jae 1f\n mov dword ptr [rsp+0x10], edx\n push rbx\n jmp game+0x142FF95\n1: ret'),
    (0x1433020, '48 8b c4 48 89 58 10', 'cmp edx, 4\n jae 1f\n mov rax, rsp\n mov qword ptr [rax+0x10], rbx\n jmp game+0x1433027\n1: ret'),
    (0x14332E0, '40 53 48 83 ec 20', 'cmp edx, 4\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x14332E6\n1: ret'),
    (0x1433380, '48 89 5c 24 08', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x1433385\n1: ret'),
    (0x14335F0, '48 89 5c 24 10', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+0x10], rbx\n jmp game+0x14335F5\n1: ret'),
    (0x72F7F0, '48 8b c4 48 89 58 08', 'cmp edx, 4\n jae 1f\n mov rax, rsp\n mov qword ptr [rax+8], rbx\n jmp game+0x72F7F7\n1: ret'),
    (0x142F860, '48 89 5c 24 08', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x142F865\n1: ret'),
    (0x142F810, '48 89 5c 24 08', 'cmp edx, 4\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x142F815\n1: ret'),
]
# The game tests the own index against -1 ("none") only: cmp reg,4; jae sends 4 to 7 the way "none" goes and -1 too.
GUARD4_SITES = [
    (0x142EA5C, '41 83 ff ff 0f 84 9f 00 00 00', '41 83 ff 04 0f 83 9f 00 00 00'),   # map update: the own cursor
    (0x142EF50, '83 f9 ff 0f 84 9f 01 00 00', '83 f9 04 0f 83 9f 01 00 00'),         # map camera: the own cursor
    (0x18C0F2F, '83 ff ff 0f 84 a2 03 00 00', '83 ff 04 0f 83 a2 03 00 00'),         # map input: the own cursor moved
    (0x18C0C1A, '83 fa ff 0f 84 8c 02 00 00', '83 fa 04 0f 83 8c 02 00 00'),         # and a drop point chosen with it
]
# The same entry guards with bound 8, once the arrays have eight entries. Each still filters -1 (unsigned jae).
GUARD8 = [
    (0xB8CAE3, '4539be205e0000', 'cmp ebp, 8\n jae game+0xB8CC50\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xB8CAEA', 'b8cac0 handler'),
    (0xBACED1, '4539be205e0000', 'cmp ebp, 8\n jae game+0xBAD04D\n cmp dword ptr [r14+0x5e20], r15d\n jmp game+0xBACED8', 'baceb0 handler copy'),
    (0xB8CC8A, '39be385e0000', 'cmp ebp, 8\n jae game+0xB8CCF6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xB8CC90', 'b8cc70 handler'),
    (0xBB494E, '39be385e0000', 'cmp ebp, 8\n jae game+0xBB49B6\n cmp dword ptr [rsi+0x5e38], edi\n jmp game+0xBB4954', 'bb4930 handler copy'),
    (0xB8CDDA, '39be685e0000', 'cmp ebp, 8\n jae game+0xB8CE6E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xB8CDE0', 'b8cdc0 handler'),
    (0xBB1B9E, '39be685e0000', 'cmp ebp, 8\n jae game+0xBB1C2E\n cmp dword ptr [rsi+0x5e68], edi\n jmp game+0xBB1BA4', 'bb1b80 handler copy'),
    (0x72F700, '405348' + '83ec20', 'cmp edx, 8\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F706\n1: ret', '72f700'),
    (0x72F780, '405348' + '83ec20', 'cmp edx, 8\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x72F786\n1: ret', '72f780'),
    (0x730190, '405348' + '83ec20', 'cmp r8d, 8\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x730196\n1: ret', '730190'),
    (0x72F5F0, '48895c2408', 'cmp edx, 8\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x72F5F5\n1: ret', '72f5f0 (guards_A G2, not in the mod)'),
    (0x730080, '48895c2408', 'cmp r8d, 8\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x730085\n1: ret', '730080 (guards_A G3, not in the mod)'),
    (0x142FF90, '8954241053', 'cmp edx, 8\n jae 1f\n mov dword ptr [rsp+0x10], edx\n push rbx\n jmp game+0x142FF95\n1: ret', '142ff90'),
    (0x1433020, '488bc448895810', 'cmp edx, 8\n jae 1f\n mov rax, rsp\n mov qword ptr [rax+0x10], rbx\n jmp game+0x1433027\n1: ret', '1433020'),
    (0x14332E0, '405348' + '83ec20', 'cmp edx, 8\n jae 1f\n push rbx\n sub rsp, 0x20\n jmp game+0x14332E6\n1: ret', '14332e0'),
    (0x1433380, '48895c2408', 'cmp edx, 8\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x1433385\n1: ret', '1433380'),
    (0x14335F0, '48895c2410', 'cmp edx, 8\n jae 1f\n mov qword ptr [rsp+0x10], rbx\n jmp game+0x14335F5\n1: ret', '14335f0'),
    (0x72F7F0, '488bc448895808', 'cmp edx, 8\n jae 1f\n mov rax, rsp\n mov qword ptr [rax+8], rbx\n jmp game+0x72F7F7\n1: ret', '72f7f0'),
    (0x142F860, '48895c2408', 'cmp edx, 8\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x142F865\n1: ret', '142f860'),
    (0x142F810, '48895c2408', 'cmp edx, 8\n jae 1f\n mov qword ptr [rsp+8], rbx\n jmp game+0x142F815\n1: ret', '142f810'),
]
