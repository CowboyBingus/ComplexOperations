#!/usr/bin/env python3
# The type registry's list of loadout card widgets (type 0xCA) for eight cards, game build 25480438. Static analysis
# only. The registry ([0x3326E68], 0x73F8 bytes inside the engine object, set once at engine init 0xAB1D0D) keeps 909
# lists: a u32 count, then entries {u64 object, u32 type, pad} from the next 8-byte boundary. Each card widget
# (card+0x5B78) adds itself to the 0xCA list at +0x2C00, which has room for four; the 0xCB list's count follows at
# +0x2C48. No add checks the room, so eight cards overwrite the next two lists (the launch crash at 0x137D9BC).
# Way: the list moves to mod memory with room for eight. Every load of the global whose value reaches the list (7 of
# 2628; no other load reaches +0x2C00..+0x2C47) reads a mod qword instead, which holds the new list minus LIST. Those
# loads only access +LIST, +LIST+8 and +LIST+0x10 through it.
GLOBAL = 0x3326E68
LIST = 0x2C00        # the count; entries from +0x2C08
ENTRY = 16
ROOM_GAME = 4
ROOM = 8             # the cards of a build for eight
NEXT = 0x2C48        # the next list's count
assert LIST + 8 + ROOM_GAME * ENTRY == NEXT

# (rva, bytes: mov r64,[rip+disp32] loading GLOBAL, the register, what it is). The disp32 is at +3.
SITES = [
    (0x1893F6E, '48 8b 15 f3 2e a9 01', 'rdx', 'add 0x1893F30, reached only from the card loop 0x1466AF0'),
    (0x1894A4A, '4c 8b 15 17 24 a9 01', 'r10', 'swap-remove 0x1894A30'),
    (0x1899875, '4c 8b 0d ec d5 a8 01', 'r9', 'swap-remove in 0x1899770 (callers 0x1467526, 0x189B887)'),
    (0xB64BFA, '48 8b 3d 67 22 7c 02', 'rdi', 'walker 0xB64BF0: type test 0xCA'),
    (0x873D53, '48 8b 35 0e 31 ab 02', 'rsi', 'inline walker in 0x873C10, the same loop'),
    (0xB29E48, '48 8b 0d 19 d0 7f 02', 'rcx', 'add stub 0xB29E40: no caller and no pointer to it'),
    (0xB3D050, '4c 8b 15 11 9e 7e 02', 'r10', 'remove stub 0xB3D050: no caller and no pointer to it'),
]
