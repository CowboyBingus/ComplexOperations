#!/usr/bin/env python3
# Loadout screen for eight players: a 3D figure on every card, game build 25480438. Static analysis only, nothing here
# has been run in the game. The figures' unit records live in the placement system UPS = [game+0x347CE60]: 8 records of
# 0xA0 at +0xC008, with its count, mask and ring right behind them at +0xC508. The weapon thumbnail generator on the
# same screen owns records and viewports 4 to 7, so card positions 4 to 7 get records and viewports 8 to 11:
#   card position   record   viewport   session slot looked up for its customization
#   0..3            0..3     0..3       0..3
#   4..7            8..11    8..11      record - 4
#   thumbnails      4..7     4..7       record (as the mod's row for the per-peer table)
# Twelve records do not fit where the eight are, so the mod moves them (MOVE): a block of 12 within reach of every
# field below, records 0 to 7 copied, and each REL field and CAVE_REL field gets DELTA = new place - (UPS + 0xC008).
# Rows in the gen_count_sites.py style:
#   INPLACE: (rva, old hex, new hex, note)        same length, one or more whole instructions
#   CAVES:   (rva, replaced hex, asm, note)       5..12 bytes become a jmp; asm uses game+0x.. and local labels
#   REL:     (rva, instruction hex, at, old)      the 32-bit field at +at holds old, a record field of UPS
# gen_count_sites.py writes them as groups 18 (the figures) and 19 (the rows of the build for eight they replace).
import struct

def le32(v): return struct.pack('<I', v & 0xFFFFFFFF).hex()
def f32(x): return struct.unpack('<I', struct.pack('<f', x))[0]

# UPS is set once in engine init (0x13F2930, called from 0x13F2E40 inside the call at 0xAB1D55): arena + 0x65DA300,
# with the arena = [game+0x3326340] + 0x1024220, the same arena whose +0x60E7208 the per-peer table's pointer gets
# there. The mod moves the records where it writes its count sites, after that call, as for the per-peer table.
MOVE = {'pointer': 0x347CE60, 'engine': 0x3326340, 'member': 0x1024220 + 0x65DA300, 'records': 0xC008, 'stride': 0xA0,
        'old_count': 8, 'new_count': 12}
NEW = MOVE['new_count']
OLD_END = MOVE['records'] + MOVE['old_count'] * MOVE['stride']        # 0xC508: the count, the mask and the ring

# Every displacement or immediate in [0xC008, 0xC508) of the 37 functions that reach UPS, each addressed from UPS on
# every path (112 as the base, 115 as the index, the immediate at 0x1398D5D: rdi = UPS + slot * 0xA0). No other code
# loads UPS or receives it in an argument register. (rva, instruction hex, field offset, old value)  # function: text
REL = [
    (0x130414A, '8b9c0210c00000', 3, 0xC010),  # 13040f0: mov ebx,DWORD PTR [rdx+rax*1+0xc010]
    (0x1305395, '8b8010c00000', 2, 0xC010),  # 13050b0: mov eax,DWORD PTR [rax+0xc010]
    (0x130636B, '418b9c0110c00000', 4, 0xC010),  # 1306330: mov ebx,DWORD PTR [r9+rax*1+0xc010]
    (0x138FF69, '4c398908c00000', 3, 0xC008),  # 138fbb0: cmp QWORD PTR [rcx+0xc008],r9
    (0x138FF72, '48899908c00000', 3, 0xC008),  # 138fbb0: mov QWORD PTR [rcx+0xc008],rbx
    (0x138FF79, '4c3989a8c00000', 3, 0xC0A8),  # 138fbb0: cmp QWORD PTR [rcx+0xc0a8],r9
    (0x138FF82, '488999a8c00000', 3, 0xC0A8),  # 138fbb0: mov QWORD PTR [rcx+0xc0a8],rbx
    (0x138FF89, '4c398948c10000', 3, 0xC148),  # 138fbb0: cmp QWORD PTR [rcx+0xc148],r9
    (0x138FF92, '48899948c10000', 3, 0xC148),  # 138fbb0: mov QWORD PTR [rcx+0xc148],rbx
    (0x138FF99, '4c3989e8c10000', 3, 0xC1E8),  # 138fbb0: cmp QWORD PTR [rcx+0xc1e8],r9
    (0x138FFA2, '488999e8c10000', 3, 0xC1E8),  # 138fbb0: mov QWORD PTR [rcx+0xc1e8],rbx
    (0x138FFA9, '4c398988c20000', 3, 0xC288),  # 138fbb0: cmp QWORD PTR [rcx+0xc288],r9
    (0x138FFB2, '48899988c20000', 3, 0xC288),  # 138fbb0: mov QWORD PTR [rcx+0xc288],rbx
    (0x138FFB9, '4c398928c30000', 3, 0xC328),  # 138fbb0: cmp QWORD PTR [rcx+0xc328],r9
    (0x138FFC2, '48899928c30000', 3, 0xC328),  # 138fbb0: mov QWORD PTR [rcx+0xc328],rbx
    (0x138FFC9, '4c3989c8c30000', 3, 0xC3C8),  # 138fbb0: cmp QWORD PTR [rcx+0xc3c8],r9
    (0x138FFD2, '488999c8c30000', 3, 0xC3C8),  # 138fbb0: mov QWORD PTR [rcx+0xc3c8],rbx
    (0x138FFD9, '4c398968c40000', 3, 0xC468),  # 138fbb0: cmp QWORD PTR [rcx+0xc468],r9
    (0x138FFE2, '48899968c40000', 3, 0xC468),  # 138fbb0: mov QWORD PTR [rcx+0xc468],rbx
    (0x139090F, '8b8cb814c00000', 3, 0xC014),  # 13908d0: mov ecx,DWORD PTR [rax+rdi*4+0xc014]
    (0x139094A, '8b8cb814c00000', 3, 0xC014),  # 13908d0: mov ecx,DWORD PTR [rax+rdi*4+0xc014]
    (0x139095F, '8b8cb83cc00000', 3, 0xC03C),  # 13908d0: mov ecx,DWORD PTR [rax+rdi*4+0xc03c]
    (0x139099A, '8b8cb83cc00000', 3, 0xC03C),  # 13908d0: mov ecx,DWORD PTR [rax+rdi*4+0xc03c]
    (0x13909AF, '8b8cb864c00000', 3, 0xC064),  # 13908d0: mov ecx,DWORD PTR [rax+rdi*4+0xc064]
    (0x13909F2, '8b8c8864c00000', 3, 0xC064),  # 13908d0: mov ecx,DWORD PTR [rax+rcx*4+0xc064]
    (0x1391053, '488d9e14c00000', 3, 0xC014),  # 1390fc0: lea rbx,[rsi+0xc014]
    (0x1391093, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x13910AF, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x13910CC, '488d9e3cc00000', 3, 0xC03C),  # 1390fc0: lea rbx,[rsi+0xc03c]
    (0x1391111, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x139112D, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x139114A, '488d9e64c00000', 3, 0xC064),  # 1390fc0: lea rbx,[rsi+0xc064]
    (0x1391191, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x13911AD, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x13911CA, '418b8c3710c00000', 4, 0xC010),  # 1390fc0: mov ecx,DWORD PTR [r15+rsi*1+0xc010]
    (0x1391238, '418b943710c00000', 4, 0xC010),  # 1390fc0: mov edx,DWORD PTR [r15+rsi*1+0xc010]
    (0x139124F, '418b943710c00000', 4, 0xC010),  # 1390fc0: mov edx,DWORD PTR [r15+rsi*1+0xc010]
    (0x1391257, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x1391267, '4589a43710c00000', 4, 0xC010),  # 1390fc0: mov DWORD PTR [r15+rsi*1+0xc010],r12d
    (0x1391276, '488d9e94c00000', 3, 0xC094),  # 1390fc0: lea rbx,[rsi+0xc094]
    (0x13912DC, '498b8c3708c00000', 4, 0xC008),  # 1390fc0: mov rcx,QWORD PTR [r15+rsi*1+0xc008]
    (0x13912E4, '41c7843790c0000002000000', 4, 0xC090),  # 1390fc0: mov DWORD PTR [r15+rsi*1+0xc090],0x2
    (0x13912F0, '4589a4378cc00000', 4, 0xC08C),  # 1390fc0: mov DWORD PTR [r15+rsi*1+0xc08c],r12d
    (0x1391A20, '428b8c0910c00000', 4, 0xC010),  # 13918e0: mov ecx,DWORD PTR [rcx+r9*1+0xc010]
    (0x139291D, '8b9c9014c00000', 3, 0xC014),  # 1392830: mov ebx,DWORD PTR [rax+rdx*4+0xc014]
    (0x1392AEA, '8b9c903cc00000', 3, 0xC03C),  # 1392830: mov ebx,DWORD PTR [rax+rdx*4+0xc03c]
    (0x1393823, '8b9c9014c00000', 3, 0xC014),  # 1392d10: mov ebx,DWORD PTR [rax+rdx*4+0xc014]
    (0x139386D, '8b9c903cc00000', 3, 0xC03C),  # 1392d10: mov ebx,DWORD PTR [rax+rdx*4+0xc03c]
    (0x13938B7, '8b9c9064c00000', 3, 0xC064),  # 1392d10: mov ebx,DWORD PTR [rax+rdx*4+0xc064]
    (0x13941AB, '8b841110c00000', 3, 0xC010),  # 1394040: mov eax,DWORD PTR [rcx+rdx*1+0xc010]
    (0x1394367, '8b8c0110c00000', 3, 0xC010),  # 1394280: mov ecx,DWORD PTR [rcx+rax*1+0xc010]
    (0x13949AD, '49399008c00000', 3, 0xC008),  # 13949a0: cmp QWORD PTR [r8+0xc008],rdx
    (0x13949B6, '4d898808c00000', 3, 0xC008),  # 13949a0: mov QWORD PTR [r8+0xc008],r9
    (0x13949BD, '4d3990a8c00000', 3, 0xC0A8),  # 13949a0: cmp QWORD PTR [r8+0xc0a8],r10
    (0x13949C6, '4d8988a8c00000', 3, 0xC0A8),  # 13949a0: mov QWORD PTR [r8+0xc0a8],r9
    (0x13949CD, '4d399048c10000', 3, 0xC148),  # 13949a0: cmp QWORD PTR [r8+0xc148],r10
    (0x13949D6, '4d898848c10000', 3, 0xC148),  # 13949a0: mov QWORD PTR [r8+0xc148],r9
    (0x13949DD, '4d3990e8c10000', 3, 0xC1E8),  # 13949a0: cmp QWORD PTR [r8+0xc1e8],r10
    (0x13949E6, '4d8988e8c10000', 3, 0xC1E8),  # 13949a0: mov QWORD PTR [r8+0xc1e8],r9
    (0x13949ED, '4d399088c20000', 3, 0xC288),  # 13949a0: cmp QWORD PTR [r8+0xc288],r10
    (0x13949F6, '4d898888c20000', 3, 0xC288),  # 13949a0: mov QWORD PTR [r8+0xc288],r9
    (0x13949FD, '4d399028c30000', 3, 0xC328),  # 13949a0: cmp QWORD PTR [r8+0xc328],r10
    (0x1394A06, '4d898828c30000', 3, 0xC328),  # 13949a0: mov QWORD PTR [r8+0xc328],r9
    (0x1394A0D, '4d3990c8c30000', 3, 0xC3C8),  # 13949a0: cmp QWORD PTR [r8+0xc3c8],r10
    (0x1394A16, '4d8988c8c30000', 3, 0xC3C8),  # 13949a0: mov QWORD PTR [r8+0xc3c8],r9
    (0x1394A1D, '4d399068c40000', 3, 0xC468),  # 13949a0: cmp QWORD PTR [r8+0xc468],r10
    (0x1394A26, '4d898868c40000', 3, 0xC468),  # 13949a0: mov QWORD PTR [r8+0xc468],r9
    (0x1394CAE, '448b8110c00000', 3, 0xC010),  # 1394c80: mov r8d,DWORD PTR [rcx+0xc010]
    (0x1394CB5, '488b8908c00000', 3, 0xC008),  # 1394c80: mov rcx,QWORD PTR [rcx+0xc008]
    (0x1394E04, '8b8ca914c00000', 3, 0xC014),  # 1394de0: mov ecx,DWORD PTR [rcx+rbp*4+0xc014]
    (0x1394E27, '8b94ae14c00000', 3, 0xC014),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc014]
    (0x1394E41, '8b94ae14c00000', 3, 0xC014),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc014]
    (0x1394E50, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394E66, '8b94ae14c00000', 3, 0xC014),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc014]
    (0x1394E6D, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394E7D, 'c784ae14c0000000000000', 3, 0xC014),  # 1394de0: mov DWORD PTR [rsi+rbp*4+0xc014],0x0
    (0x1394E88, '8b8cae3cc00000', 3, 0xC03C),  # 1394de0: mov ecx,DWORD PTR [rsi+rbp*4+0xc03c]
    (0x1394EAB, '8b94ae3cc00000', 3, 0xC03C),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc03c]
    (0x1394EC5, '8b94ae3cc00000', 3, 0xC03C),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc03c]
    (0x1394ED4, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394EEA, '8b94ae3cc00000', 3, 0xC03C),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc03c]
    (0x1394EF1, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394F01, 'c784ae3cc0000000000000', 3, 0xC03C),  # 1394de0: mov DWORD PTR [rsi+rbp*4+0xc03c],0x0
    (0x1394F0C, '8b8cae64c00000', 3, 0xC064),  # 1394de0: mov ecx,DWORD PTR [rsi+rbp*4+0xc064]
    (0x1394F2F, '8b94ae64c00000', 3, 0xC064),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc064]
    (0x1394F49, '8b94ae64c00000', 3, 0xC064),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc064]
    (0x1394F58, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394F6E, '8b94ae64c00000', 3, 0xC064),  # 1394de0: mov edx,DWORD PTR [rsi+rbp*4+0xc064]
    (0x1394F75, '488b8c3308c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rbx+rsi*1+0xc008]
    (0x1394F85, 'c784ae64c0000000000000', 3, 0xC064),  # 1394de0: mov DWORD PTR [rsi+rbp*4+0xc064],0x0
    (0x1394FA3, '488b8c3108c00000', 4, 0xC008),  # 1394de0: mov rcx,QWORD PTR [rcx+rsi*1+0xc008]
    (0x1397283, '438b942c90c00000', 4, 0xC090),  # 13971f0: mov edx,DWORD PTR [r12+r13*1+0xc090]
    (0x13972E2, '4b8b8c2c08c00000', 4, 0xC008),  # 13971f0: mov rcx,QWORD PTR [r12+r13*1+0xc008]
    (0x139731F, '478b842c10c00000', 4, 0xC010),  # 13971f0: mov r8d,DWORD PTR [r12+r13*1+0xc010]
    (0x1397329, '4b8b8c2c08c00000', 4, 0xC008),  # 13971f0: mov rcx,QWORD PTR [r12+r13*1+0xc008]
    (0x139738C, '4380bc2ca4c0000000', 4, 0xC0A4),  # 13971f0: cmp BYTE PTR [r12+r13*1+0xc0a4],0x0
    (0x1397397, '438b842ca0c00000', 4, 0xC0A0),  # 13971f0: mov eax,DWORD PTR [r12+r13*1+0xc0a0]
    (0x13973B7, '4389bc2c14c00000', 4, 0xC014),  # 13971f0: mov DWORD PTR [r12+r13*1+0xc014],edi
    (0x13973C6, '4389bc2c3cc00000', 4, 0xC03C),  # 13971f0: mov DWORD PTR [r12+r13*1+0xc03c],edi
    (0x13973D0, '4389bc2c64c00000', 4, 0xC064),  # 13971f0: mov DWORD PTR [r12+r13*1+0xc064],edi
    (0x1397500, '448b8518c00000', 3, 0xC018),  # 1397430: mov r8d,DWORD PTR [rbp+0xc018]
    (0x139750C, '8b9590c00000', 2, 0xC090),  # 1397430: mov edx,DWORD PTR [rbp+0xc090]
    (0x139751A, '448b8540c00000', 3, 0xC040),  # 1397430: mov r8d,DWORD PTR [rbp+0xc040]
    (0x1397526, '448b8568c00000', 3, 0xC068),  # 1397430: mov r8d,DWORD PTR [rbp+0xc068]
    (0x13975E8, '4d8b842408c00000', 4, 0xC008),  # 1397430: mov r8,QWORD PTR [r12+0xc008]
    (0x1397721, '468bb42090c00000', 4, 0xC090),  # 1397670: mov r14d,DWORD PTR [rax+r12*1+0xc090]
    (0x139782D, '4a8b8c2708c00000', 4, 0xC008),  # 1397670: mov rcx,QWORD PTR [rdi+r12*1+0xc008]
    (0x139786B, '468b842710c00000', 4, 0xC010),  # 1397670: mov r8d,DWORD PTR [rdi+r12*1+0xc010]
    (0x1397881, '4a8b8c2708c00000', 4, 0xC008),  # 1397670: mov rcx,QWORD PTR [rdi+r12*1+0xc008]
    (0x1397B3C, '4280bc27a4c0000000', 4, 0xC0A4),  # 1397670: cmp BYTE PTR [rdi+r12*1+0xc0a4],0x0
    (0x1397B47, '428b8427a0c00000', 4, 0xC0A0),  # 1397670: mov eax,DWORD PTR [rdi+r12*1+0xc0a0]
    (0x1397B7F, '4589bc8414c00000', 4, 0xC014),  # 1397670: mov DWORD PTR [r12+rax*4+0xc014],r15d
    (0x1397B8E, '4589bc843cc00000', 4, 0xC03C),  # 1397670: mov DWORD PTR [r12+rax*4+0xc03c],r15d
    (0x1397BBD, '4589bc8464c00000', 4, 0xC064),  # 1397670: mov DWORD PTR [r12+rax*4+0xc064],r15d
    (0x1397BDA, '4d8ba43c08c00000', 4, 0xC008),  # 1397670: mov r12,QWORD PTR [r12+rdi*1+0xc008]
    (0x1397E43, '488d819cc00000', 3, 0xC09C),  # 1397e40: lea rax,[rcx+0xc09c]
    (0x139847B, '8b8c2b10c00000', 3, 0xC010),  # 1398450: mov ecx,DWORD PTR [rbx+rbp*1+0xc010]
    (0x13984A6, '8b942b10c00000', 3, 0xC010),  # 1398450: mov edx,DWORD PTR [rbx+rbp*1+0xc010]
    (0x13984D0, '488db514c00000', 3, 0xC014),  # 1398450: lea rsi,[rbp+0xc014]
    (0x1398509, '8b948514c00000', 3, 0xC014),  # 1398450: mov edx,DWORD PTR [rbp+rax*4+0xc014]
    (0x1398528, '488db53cc00000', 3, 0xC03C),  # 1398450: lea rsi,[rbp+0xc03c]
    (0x139855E, '8b94853cc00000', 3, 0xC03C),  # 1398450: mov edx,DWORD PTR [rbp+rax*4+0xc03c]
    (0x1398579, '488d9d64c00000', 3, 0xC064),  # 1398450: lea rbx,[rbp+0xc064]
    (0x13985AD, '8b948564c00000', 3, 0xC064),  # 1398450: mov edx,DWORD PTR [rbp+rax*4+0xc064]
    (0x1398972, '428b9c2094c00000', 4, 0xC094),  # 1398800: mov ebx,DWORD PTR [rax+r12*1+0xc094]
    (0x139897A, '4289942094c00000', 4, 0xC094),  # 1398800: mov DWORD PTR [rax+r12*1+0xc094],edx
    (0x1398A03, '428b9c209cc00000', 4, 0xC09C),  # 1398800: mov ebx,DWORD PTR [rax+r12*1+0xc09c]
    (0x1398A0E, '428994209cc00000', 4, 0xC09C),  # 1398800: mov DWORD PTR [rax+r12*1+0xc09c],edx
    (0x1398A6C, '428b9c2098c00000', 4, 0xC098),  # 1398800: mov ebx,DWORD PTR [rax+r12*1+0xc098]
    (0x1398A74, '4289942098c00000', 4, 0xC098),  # 1398800: mov DWORD PTR [rax+r12*1+0xc098],edx
    (0x1398ACB, '898390c00000', 2, 0xC090),  # 1398800: mov DWORD PTR [rbx+0xc090],eax
    (0x1398AD5, '8b9398c00000', 2, 0xC098),  # 1398800: mov edx,DWORD PTR [rbx+0xc098]
    (0x1398AE0, '448b839cc00000', 3, 0xC09C),  # 1398800: mov r8d,DWORD PTR [rbx+0xc09c]
    (0x1398B03, '8b9394c00000', 2, 0xC094),  # 1398800: mov edx,DWORD PTR [rbx+0xc094]
    (0x1398C16, '8b8f10c00000', 2, 0xC010),  # 1398800: mov ecx,DWORD PTR [rdi+0xc010]
    (0x1398D0B, '448b8710c00000', 3, 0xC010),  # 1398800: mov r8d,DWORD PTR [rdi+0xc010]
    (0x1398D46, '448b8f10c00000', 3, 0xC010),  # 1398800: mov r9d,DWORD PTR [rdi+0xc010]
    (0x1398D5D, '4881c73cc00000', 3, 0xC03C),  # 1398800: add rdi,0xc03c
    (0x1399430, '498db42410c00000', 4, 0xC010),  # 1398800: lea rsi,[r12+0xc010]
    (0x1399491, '428b8c2310c00000', 4, 0xC010),  # 1398800: mov ecx,DWORD PTR [rbx+r12*1+0xc010]
    (0x13994A7, '428b942310c00000', 4, 0xC010),  # 1398800: mov edx,DWORD PTR [rbx+r12*1+0xc010]
    (0x13994FC, '418b949414c00000', 4, 0xC014),  # 1398800: mov edx,DWORD PTR [r12+rdx*4+0xc014]
    (0x1399556, '418b94943cc00000', 4, 0xC03C),  # 1398800: mov edx,DWORD PTR [r12+rdx*4+0xc03c]
    (0x13995AE, '418b949464c00000', 4, 0xC064),  # 1398800: mov edx,DWORD PTR [r12+rdx*4+0xc064]
    (0x1399BC9, '83bc0e10c0000000', 3, 0xC010),  # 1399ba0: cmp DWORD PTR [rsi+rcx*1+0xc010],0x0
    (0x1399BF0, '4088ac0ea4c00000', 4, 0xC0A4),  # 1399ba0: mov BYTE PTR [rsi+rcx*1+0xc0a4],bpl
    (0x1399BF8, '4c898c0e08c00000', 4, 0xC008),  # 1399ba0: mov QWORD PTR [rsi+rcx*1+0xc008],r9
    (0x1399C00, '89840ea0c00000', 3, 0xC0A0),  # 1399ba0: mov DWORD PTR [rsi+rcx*1+0xc0a0],eax
    (0x1399C31, '89843e10c00000', 3, 0xC010),  # 1399ba0: mov DWORD PTR [rsi+rdi*1+0xc010],eax
    (0x1399C77, '8b943e10c00000', 3, 0xC010),  # 1399ba0: mov edx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399CAD, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399CD5, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399CEC, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399D14, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399D3A, '8b943e10c00000', 3, 0xC010),  # 1399ba0: mov edx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399D5D, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399D81, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399D9A, '8b8c3e10c00000', 3, 0xC010),  # 1399ba0: mov ecx,DWORD PTR [rsi+rdi*1+0xc010]
    (0x1399EC9, '39943e90c00000', 3, 0xC090),  # 1399ba0: cmp DWORD PTR [rsi+rdi*1+0xc090],edx
    (0x1399F66, '899c0e10c00000', 3, 0xC010),  # 1399ba0: mov DWORD PTR [rsi+rcx*1+0xc010],ebx
    (0x1399F8C, '83bc1f10c0000000', 3, 0xC010),  # 1399f70: cmp DWORD PTR [rdi+rbx*1+0xc010],0x0
    (0x1399FA5, '4489bc1fa0c00000', 4, 0xC0A0),  # 1399f70: mov DWORD PTR [rdi+rbx*1+0xc0a0],r15d
    (0x1399FAD, '4488bc1fa4c00000', 4, 0xC0A4),  # 1399f70: mov BYTE PTR [rdi+rbx*1+0xc0a4],r15b
    (0x1399FB5, '4c898c1f08c00000', 4, 0xC008),  # 1399f70: mov QWORD PTR [rdi+rbx*1+0xc008],r9
    (0x1399FE7, '89841f10c00000', 3, 0xC010),  # 1399f70: mov DWORD PTR [rdi+rbx*1+0xc010],eax
    (0x139A026, '8b941f10c00000', 3, 0xC010),  # 1399f70: mov edx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A06A, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A092, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A0AA, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A0D2, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A0F4, '8b941f10c00000', 3, 0xC010),  # 1399f70: mov edx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A117, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A143, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A15C, '8b8c1f10c00000', 3, 0xC010),  # 1399f70: mov ecx,DWORD PTR [rdi+rbx*1+0xc010]
    (0x139A199, '39941f90c00000', 3, 0xC090),  # 1399f70: cmp DWORD PTR [rdi+rbx*1+0xc090],edx
    (0x139A208, 'c7841f10c0000000000000', 3, 0xC010),  # 1399f70: mov DWORD PTR [rdi+rbx*1+0xc010],0x0
    (0x139C034, '4c8db510c00000', 3, 0xC010),  # 139c000: lea r14,[rbp+0xc010]
    (0x139C345, '428b8c8094c00000', 4, 0xC094),  # 139c330: mov ecx,DWORD PTR [rax+r8*4+0xc094]
    (0x139C3E0, '488d9e14c00000', 3, 0xC014),  # 139c360: lea rbx,[rsi+0xc014]
    (0x139C418, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C433, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C44F, '488d9e3cc00000', 3, 0xC03C),  # 139c360: lea rbx,[rsi+0xc03c]
    (0x139C491, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C4AC, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C4C8, '488d9e64c00000', 3, 0xC064),  # 139c360: lea rbx,[rsi+0xc064]
    (0x139C501, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C51C, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C538, '8b8e10c00000', 2, 0xC010),  # 139c360: mov ecx,DWORD PTR [rsi+0xc010]
    (0x139C5A9, '8b9610c00000', 2, 0xC010),  # 139c360: mov edx,DWORD PTR [rsi+0xc010]
    (0x139C5BE, '8b9610c00000', 2, 0xC010),  # 139c360: mov edx,DWORD PTR [rsi+0xc010]
    (0x139C5C4, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C5D3, '4489a610c00000', 3, 0xC010),  # 139c360: mov DWORD PTR [rsi+0xc010],r12d
    (0x139C5E1, '488d9e94c00000', 3, 0xC094),  # 139c360: lea rbx,[rsi+0xc094]
    (0x139C638, '488b8e08c00000', 3, 0xC008),  # 139c360: mov rcx,QWORD PTR [rsi+0xc008]
    (0x139C63F, 'c78690c0000002000000', 2, 0xC090),  # 139c360: mov DWORD PTR [rsi+0xc090],0x2
    (0x139C649, '4489a68cc00000', 3, 0xC08C),  # 139c360: mov DWORD PTR [rsi+0xc08c],r12d
    (0x139C6BF, '4183bc0998c0000000', 4, 0xC098),  # 139c6b0: cmp DWORD PTR [r9+rcx*1+0xc098],0x0
    (0x139C6CA, '4183bc099cc0000000', 4, 0xC09C),  # 139c6b0: cmp DWORD PTR [r9+rcx*1+0xc09c],0x0
    (0x139C6D5, '4183bc0994c0000000', 4, 0xC094),  # 139c6b0: cmp DWORD PTR [r9+rcx*1+0xc094],0x0
    (0x139C6E0, '4189940990c00000', 4, 0xC090),  # 139c6b0: mov DWORD PTR [r9+rcx*1+0xc090],edx
    (0x139C6ED, '410fb68409a4c00000', 5, 0xC0A4),  # 139c6b0: movzx eax,BYTE PTR [r9+rcx*1+0xc0a4]
    (0x139C707, '498b840908c00000', 4, 0xC008),  # 139c6b0: mov rax,QWORD PTR [r9+rcx*1+0xc008]
    (0x139C764, '450fb6b40aa4c00000', 5, 0xC0A4),  # 139c730: movzx r14d,BYTE PTR [r10+rcx*1+0xc0a4]
    (0x139C76D, '4d8bbc0a08c00000', 4, 0xC008),  # 139c730: mov r15,QWORD PTR [r10+rcx*1+0xc008]
    (0x139C824, '450fb6b40aa4c00000', 5, 0xC0A4),  # 139c7f0: movzx r14d,BYTE PTR [r10+rcx*1+0xc0a4]
    (0x139C82D, '4d8bbc0a08c00000', 4, 0xC008),  # 139c7f0: mov r15,QWORD PTR [r10+rcx*1+0xc008]
    (0x139C8E4, '450fb6b40aa4c00000', 5, 0xC0A4),  # 139c8b0: movzx r14d,BYTE PTR [r10+rcx*1+0xc0a4]
    (0x139C8ED, '4d8bbc0a08c00000', 4, 0xC008),  # 139c8b0: mov r15,QWORD PTR [r10+rcx*1+0xc008]
    (0x139C9B9, '8b8c0e10c00000', 3, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+rcx*1+0xc010]
    (0x139C9F8, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CA1A, '428b94368cc00000', 4, 0xC08C),  # 139c970: mov edx,DWORD PTR [rsi+r14*1+0xc08c]
    (0x139CA35, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CA55, '428984368cc00000', 4, 0xC08C),  # 139c970: mov DWORD PTR [rsi+r14*1+0xc08c],eax
    (0x139CA6A, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CA8E, '428b9c3610c00000', 4, 0xC010),  # 139c970: mov ebx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CC03, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CC35, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CC4F, '428b8c3610c00000', 4, 0xC010),  # 139c970: mov ecx,DWORD PTR [rsi+r14*1+0xc010]
    (0x139CD98, '47399c1690c00000', 4, 0xC090),  # 139ccd0: cmp DWORD PTR [r14+r10*1+0xc090],r11d
    (0x139CE21, '4339941694c00000', 4, 0xC094),  # 139ccd0: cmp DWORD PTR [r14+r10*1+0xc094],edx
    (0x139CE45, '4339bc1698c00000', 4, 0xC098),  # 139ccd0: cmp DWORD PTR [r14+r10*1+0xc098],edi
    (0x139CE6B, '43399c169cc00000', 4, 0xC09C),  # 139ccd0: cmp DWORD PTR [r14+r10*1+0xc09c],ebx
    (0x13F879D, '8b9c1110c00000', 3, 0xC010),  # 13f81a0: mov ebx,DWORD PTR [rcx+rdx*1+0xc010]
    (0x14CE5FC, '8b840110c00000', 3, 0xC010),  # 14ce320: mov eax,DWORD PTR [rcx+rax*1+0xc010]
    (0x196C147, '428b8c0910c00000', 4, 0xC010),  # 196b870: mov ecx,DWORD PTR [rcx+r9*1+0xc010]
    (0x196DB94, '428b8c0910c00000', 4, 0xC010),  # 196d8d0: mov ecx,DWORD PTR [rcx+r9*1+0xc010]
    (0x197C590, '428b8c0910c00000', 4, 0xC010),  # 197c170: mov ecx,DWORD PTR [rcx+r9*1+0xc010]
    (0x197D0A1, '428b8c0910c00000', 4, 0xC010),  # 197cde0: mov ecx,DWORD PTR [rcx+r9*1+0xc010]
]

# --- CAP: the loops over records see 12 -------------------------------------------------------------------------------
CAP_INPLACE = [
    (0x13995D4, '4183ff08', '4183ff0c', '1398800 update: records 0..11 (r15d is the slot and the viewport of 0x138F110)'),
    (0x1398198, '83fb08', '83fb0c', '1397ee0 reset: releases every record in the mask, 0..11 (0x139C360)'),
    (0x1397E51, 'ba08000000', 'ba0c000000', '1397e40 init (no caller): zeroes records 0..11'),
    # "is a record's unit still loading": the loop ran over slots 0..count-1, skipping empty ones. With records 8..11
    # in use the occupied ones are not 0..count-1, so it runs over every slot; the count test before the loop stays.
    (0x130417C, '3bb008c50000', '83fe0c0f1f00', '13040f0 busy check: slots 0..11'),
    (0x130639E, '3bb008c50000', '83fe0c0f1f00', '1306330 busy check: slots 0..11'),
    (0x13F87CF, '3bb208c50000', '83fe0c0f1f00', '13f81a0 busy check: slots 0..11'),
    # The update's request loop (0x1398870..0x1398E1F) keeps one "still loading" byte per record slot on its stack:
    # eight bytes at rbp+0x150, the stack cookie right behind them at rbp+0x158. Slot 8 read the cookie's low byte as
    # its flag, so its requests (the figure's armor, helmet and cape) were skipped forever, and a write would have
    # broken the cookie. The flags go to rbp+0x140: sixteen bytes the function only uses after this loop, zeroed
    # there before each use (0x1398EA1..0x1398ED0). xmm0 is free: the function reads no xmm register before 0x1398EA1.
    # rbp+0x150 is written again (0x1398E32) before its next read (0x1399A6C).
    (0x139884D, '48c7855001000000000000', '0f57c00f11854001000090', '1398800 update: sixteen slot flags zeroed at rbp+0x140'),
    (0x13988EE, '80bc355001000000', '80bc354001000000', '1398800 update: a slot flag read'),
    (0x1398926, 'c684355001000001', 'c684354001000001', '1398800 update: a slot flag set'),
    (0x1398936, 'c684355001000000', 'c684354001000000', '1398800 update: a slot flag cleared'),
]

# A world's records are cleared by an unrolled compare of all 8 (base = UPS, world, value). The cave adds 8..11 with
# record fields that also get DELTA (CAVE_REL), then does the replaced load of the ring index, which stays at UPS.
def _destroy(base, world, value):
    out = ''
    for k in range(8, NEW):
        d = MOVE['records'] + MOVE['stride'] * k
        out += ' cmp qword ptr [%s+0x%x], %s\n jne %d1f\n mov qword ptr [%s+0x%x], %s\n%d1:\n' % (base, d, world, k, base, d, value, k)
    return out
CAVE_REL = [MOVE['records'] + MOVE['stride'] * k for k in range(8, NEW)]   # 0xC508, 0xC5A8, 0xC648, 0xC6E8

CAP_CAVES = [
    (0x138FFE9, '8b9110c50000', _destroy('rcx', 'r9', 'rbx') + ' mov edx, dword ptr [rcx+0xc510]\n jmp game+0x138FFEF',
     '138fbb0 world destroy: records 8..11 too (rcx UPS, r9 world, rbx value)'),
    (0x1394A2D, '418b9010c50000', _destroy('r8', 'r10', 'r9') + ' mov edx, dword ptr [r8+0xc510]\n jmp game+0x1394A34',
     '13949a0 world destroy: records 8..11 too (r8 UPS, r10 world, r9 zero)'),
    # 139c000 releases the figure records 0..3 (r15d slot, r12d its mask bit, r14 the record's unit field). After 3 it
    # goes on at 8 (bit 0x100, four records on) and stops after 11. Loop head 0x139C040.
    (0x139C2D1, '4183ff040f8265fdffff',
     ' cmp r15d, 4\n jb 2f\n jne 1f\n mov r15d, 8\n shl r12d, 4\n add r14, 0x280\n2: jmp game+0x139C040\n'
     '1: cmp r15d, 12\n jb 2b\n jmp game+0x139C2DB',
     '139c000 release figures: records 0..3 and 8..11'),
]
CAVES_WITH_REL = {0x138FFE9, 0x1394A2D}

# --- SLOT: a record's session slot ----------------------------------------------------------------------------------
SLOT_CAVES = [
    # 1394fd0 loads a record's customization (edx = record slot in ebp). A slot below the bound looks up the session
    # member with ctx+0x163AC == slot, the rest the local player. 0..7 as the mod's row for the per-peer table; 8..11 are
    # session slots 4..7; 12 and up the local player. ebp is read only here and at 0x13951C2.
    (0x13951A9, '83fd047326',
     ' cmp ebp, 8\n jb game+0x13951AE\n cmp ebp, 12\n jae game+0x13951D4\n sub ebp, 4\n jmp game+0x13951AE',
     '1394fd0 player lookup: records 8..11 are session slots 4..7'),
    # 1397670 hands the callback [0x33263D0] r8b = (r13d < 4), "a player's figure"; records 8..11 get 1 too. r8d is 0
    # from 0x13978F7; the game's setb at 0x139790D sets r8b from the carry.
    (0x1397901, '4183fd04488b8c2488000000',
     ' mov rcx, qword ptr [rsp+0x88]\n cmp r13d, 4\n jb game+0x139790D\n lea r8d, [r13-8]\n cmp r8d, 4\n'
     ' mov r8d, 0\n jmp game+0x139790D',
     '1397670 player figure flag: records 8..11 too'),
]

# --- CARD: positions 4..7 on records and viewports 8..11 ------------------------------------------------------------
def slot_of(p): return p if p < 4 else p + 4
def pos_of(s): return s - 4 if s >= 8 else s
# A tile of a 4 x 2 atlas by position: the viewport (x, y, w, h as fractions of the shared target) and the image's UV
# corners (v = 1 - y), as the game's 2 x 2 cases store them for 0x143EEF0.
def atlas(p):
    x, y, w, h = 0.25 * (p % 4), 0.5 * (p // 4), 0.25, 0.5
    return [x, y, w, h, x, 1.0 - y - h, x + w, 1.0 - y]
Z12 = [10.0 * (pos_of(s) + 1) for s in range(NEW)]     # pose z by record: 10..80 by position
ATLAS12 = [atlas(pos_of(s)) for s in range(NEW)]
_atlas_rows = '\n'.join(' .float ' + ', '.join('%r' % v for v in row) for row in ATLAS12)
CARD_CAVES = [
    # 18a1a60 (F = card + 0x1FA8, edx = position) stores edx as the record slot (F+0x8E0) and the position (F+0x8E4).
    # 0..3 stay, 4..7 become 8..11, anything else -1: no figure, the constructor's value.
    (0x18A1A60, '40534883ec30',
     ' cmp edx, 4\n jb 1f\n cmp edx, 8\n jae 2f\n add edx, 4\n jmp 1f\n2: mov edx, -1\n1: push rbx\n sub rsp, 0x30\n'
     ' jmp game+0x18A1A66', '18a1a60 figure slot: positions 4..7 get records 8..11, others none'),
    # 18a1b40 spawns the figure with a pose z from a table of 4 by F+0x8E4: one of 12 by record.
    (0x18A1B94, '488d0de5589400', ' lea rcx, [rip+1f]\n jmp game+0x18A1B9B\n .balign 4\n1:\n .float ' + ', '.join('%r' % v for v in Z12),
     '18a1b40 spawn: pose z by record, 12 entries'),
    # 18a12a0: the viewport rect and the image UV by F+0x8E4 from a switch of 4 cases; a table of 12 by record. rbx is
    # F; eax, rdx, r8 and xmm0 are written before their next use. 12 and up keep the default rect (0,0,1,1) the game
    # stored at 0x18A12D4, with the UV (0,0)..(1,1).
    (0x18A12D8, '8b83e4080000',
     ' mov eax, dword ptr [rbx+0x8e4]\n cmp eax, %d\n jae 9f\n lea rdx, [rip+2f]\n shl eax, 5\n' % NEW +
     ' movups xmm0, xmmword ptr [rdx+rax]\n movups xmmword ptr [rbp-0x19], xmm0\n'
     ' mov r8, qword ptr [rdx+rax+0x10]\n mov qword ptr [rbp+0x77], r8\n'
     ' mov r8, qword ptr [rdx+rax+0x18]\n mov qword ptr [rbp+0x67], r8\n jmp game+0x18A1385\n'
     '9: mov qword ptr [rbp+0x77], 0\n mov dword ptr [rbp+0x67], 0x3f800000\n mov dword ptr [rbp+0x6b], 0x3f800000\n'
     ' jmp game+0x18A1385\n .balign 16\n2:\n' + _atlas_rows,
     '18a12a0 viewport rect and image UV by record: 4 x 2 tiles'),
]
# The figure image F+0x220 at half width: each tile is half as wide, so the image keeps the game's ratio to its tile.
CARD_INPLACE = [
    (0x18A0EF1, 'c7442450' + le32(f32(780.0)), 'c7442450' + le32(f32(390.0)), '18a0d30 figure image width 780 -> 390'),
]

# The rows of the build for eight that these replace, at the same bytes: the figure slot's cave (4 and up get none,
# loadout_sites.FIGURE_SLOT) and the per-peer table's player lookup bound (loadout_sites.SYNCED_SITES). They stay in a
# build for eight whose figures are left out.
REPLACES = {0x18A1A60: 'FIGURE_SLOT', 0x13951A9: 'SYNCED_SITES'}

def build(dump):
    """(inplace, caves, rel) checked against the dump."""
    inplace = CAP_INPLACE + CARD_INPLACE
    caves = CAP_CAVES + SLOT_CAVES + CARD_CAVES
    for rva, old, new, _ in inplace: assert dump[rva:rva + len(bytes.fromhex(old))] == bytes.fromhex(old), hex(rva)
    for rva, rep, _, _ in caves: assert dump[rva:rva + len(bytes.fromhex(rep))] == bytes.fromhex(rep), hex(rva)
    for rva, hx, at, old in REL:
        b = bytes.fromhex(hx)
        assert dump[rva:rva + len(b)] == b and struct.unpack_from('<I', b, at)[0] == old and MOVE['records'] <= old < OLD_END, hex(rva)
    assert len(REL) == len({r[0] for r in REL}) == 228
    return sorted(inplace), sorted(caves), sorted(REL)

# The game's code the tests run on the moved records: both world destroys from their load of UPS to the caves' way
# back, the release loop's tail (inc, rol, add, then the cave) and 0x139C330, which reads equipment id r8d of record
# r9d into [rdx] through a REL row with UPS as the base and the record in the index.
TEST_CODE = [(0x138FF5B, 0x138FFEF), (0x13949A0, 0x1394A34), (0x139C2C4, 0x139C2DB), (0x139C330, 0x139C353)]

def test_header(dump):
    """tests/co_figure_code.h: the TEST_CODE ranges' bytes."""
    h = ['/* Generated by scripts/gen_count_sites.py from scripts/sites/figure_sites.py; do not edit. */\n']
    for i, (start, end) in enumerate(TEST_CODE):
        b = dump[start:end]
        h.append('static const uint8_t kFigureCode%d[] = {\n' % i)
        for j in range(0, len(b), 24): h.append('    ' + ', '.join('0x%02X' % x for x in b[j:j + 24]) + ',\n')
        h.append('};\n')
    h.append('static const struct { uint32_t rva, size; const uint8_t* code; } kFigureCode[] = {\n')
    for i, (start, end) in enumerate(TEST_CODE): h.append('    {0x%Xu, 0x%Xu, kFigureCode%d},\n' % (start, end - start, i))
    h.append('};\n')
    return h
