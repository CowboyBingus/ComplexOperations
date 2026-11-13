#!/usr/bin/env python3
# Writes src/co_manager_sites.h: the code sites of the per-player component managers (game build 25480438), from the
# reviewed lists in scripts/sites/ and a dump of the game's code:
#   scripts/gen_manager_sites.py <game.dll image dump> [out]
# A manager is a struct inside the game's component block with four slots and the next manager right behind it. The
# mod builds each one in its own memory with eight slots; the avatar manager (eight slots, players' bodies too) with
# sixteen, in a build for eight players only (SLOTS, EIGHT_ONLY). Four kinds of site:
#   BLOCK  reaches the manager from the block (block + displacement): gets the distance to the new struct
#   ARRAY  reaches a per-slot array that eight slots of its neighbour would overrun: the array has a new offset
#   CAP    the capacity check `cmp eax,4` (the avatar's `cmp eax,8`), or another four in a loop over a moved array
#   CODE   four bytes of code replaced
# FOLDS are the code that reaches a moved array through an index register instead of a displacement (an ARRAY row
# for the offset, a CODE row for a record count); the dump is scanned for that form, and every hit must be listed.
# and caves: code the game wrote out for four rows of a moved array, done for eight in code of the mod's (CAVES).
# Every row is checked against the dump here, and again by the mod against the running game before it writes.
import re, struct, subprocess, sys, os, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)) + '/sites/'
# name: (global, offset in the block, size, new size, [(old start, old end, new start) of each array that moves])
MANAGERS = {
    'score':              (0x3326AE0, 0x7C7368, 0x37E38, 0x380A0, [(0x37D70, 0x37D90, 0x37EE0), (0xC440, 0xC4A0, 0x37F20), (0xC4A0, 0xC500, 0x37FE0)]),
    # The applied records (0x44 each) move a whole number of records, so the game's (slot + 0x24) * 0x44 reads of the
    # voice stay right with a new 0x24 (FOLDS).
    'customization':      (0x33264F8, 0xE9FC98, 0xB80,   0xEBC,   [(0x968, 0x96C, 0xC80), (0x96C, 0xA7C, 0xC9C)]),
    'kill_streak':        (0x3326AF0, 0x2A988,  0x4A8,   0xCE8,   [(0x88, 0x4A8, 0x4A8)]),
    'ship_inventory':     (0x33266D0, 0xEB5178, 0x168,   0x318,   [(0x90, 0x160, 0x170), (0x160, 0x164, 0x310)]),
    'ship_customization': (0x33267B8, 0x3CA0,   0x2B8,   0x620,   [(0x90, 0x98, 0x398), (0x98, 0x1D8, 0x3A0)]),
    'ship_modules_sync':  (0x3326998, 0x4148,   0x267E8, 0x73548, [(0x88, 0x26528, 0x26948), (0x26528, 0x26688, 0x73288)]),
    # A2 (0x38 each) moves 26 records, for the (slot + 0xD) * 0x38 writes of FOLDS.
    'player':             (0x3326468, 0xEA0818, 0x428,   0xA38,   [(0x108, 0x2C8, 0x4A8), (0x2C8, 0x3A8, 0x878)]),
    # A0 (slot pointers at +0x110) grows in place into A1's old room; A1..A4 and pool C move behind the old end, each at
    # its old offset modulo 16. A1's distance is a multiple of 32 (FOLD below).
    'avatar':             (0x3326D20, 0x956128, 0x5497C8, 0xFDC258, [(0x150, 0x53D8B0, 0x5497D0), (0x53D8B0, 0x546A70, 0xFC4690),
                                                                   (0x546A70, 0x547830, 0xFD6A10), (0x547830, 0x547BF0, 0xFD8590),
                                                                   (0x547D24, 0x5497C4, 0xFD8D14)]),
}
# Slots before and after (CAP checks the first and writes the second); four and eight unless named here.
SLOTS = {'avatar': (8, 16)}
# Managers only a build for eight players moves; a build for four leaves them as the game has them. That is all of them:
# a build for four joins four players, and HellForge and other mods read these managers at the game's offsets.
EIGHT_ONLY = set(MANAGERS)
# Bytes per slot of each moved array, by (manager, old start).
STRIDE = {('score', 0x37D70): 8, ('score', 0xC440): 0x18, ('score', 0xC4A0): 0x18, ('customization', 0x968): 1,
          ('customization', 0x96C): 0x44, ('kill_streak', 0x88): 0x108, ('ship_inventory', 0x90): 0x34,
          ('ship_inventory', 0x160): 1, ('ship_customization', 0x90): 1, ('ship_customization', 0x98): 0x50,
          ('ship_modules_sync', 0x88): 0x9928, ('ship_modules_sync', 0x26528): 0x58, ('player', 0x108): 0x70,
          ('player', 0x2C8): 0x38, ('avatar', 0x150): 0xA7AEC, ('avatar', 0x53D8B0): 0x1238, ('avatar', 0x546A70): 0x1B8,
          ('avatar', 0x547830): 0x78, ('avatar', 0x547D24): 0x354}
assert sorted(STRIDE) == sorted((m, lo) for m in MANAGERS for lo, _, _ in MANAGERS[m][4])
# Sites the lists miss, found later: (manager, rva, offset of the disp32, old value[, what it adds])
# The avatar's FOLD at 0xA4F4FD: add r9,0xE6; shl r9,5 reaches A1 + 0x1B70 of a slot (0xE6 x 32 = 0x150 + 0x1B70) with
# no array offset of its own, so it adds A1's distance divided by 32.
EXTRA = [('avatar', 0xA4F4FD, 3, 0xE6, (0x5497D0 - 0x150) // 32)]
assert (0x5497D0 - 0x150) % 32 == 0
# More site lists of a manager. Score's loot tables (four rows of 0x18 by score slot at +0xC440, four by player index at
# +0xC4A0) move behind the manager, so that eight rows of each fit.
LISTS = {'score': ['patch_sites_score.tsv', 'patch_sites_score_loot.tsv']}
LOOT_ROWS = 54
# The loops over the loot rows by score slot count to eight once the rows have moved: (manager, rva, instruction, offset
# of the four)
LOOPS = [('score', 0x63090B, '83 f9 04', 2), ('score', 0x63095B, '83 f8 04', 2), ('score', 0x630980, '44 8d 40 04', 3),
         ('score', 0x126EBFC, '83 f8 04', 2), ('score', 0x1347C1D, '83 f9 04', 2), ('score', 0x1380C68, '41 be 04 00 00 00', 2),
         ('score', 0x19940EB, '83 f9 04', 2)]
# r14d of the loop at 0x1380C68 is also the mission end report's outcome code, read at 0x1380F0B: mov eax,r14d becomes
# push 4; pop rax, the value it had. (manager, rva, four bytes, four new bytes)
CODE = [('score', 0x1380F0B, '41 8b c6 89', '6a 04 58 89')]
# Index registers that carry a moved array's offset: U `imul r,x,S` then the array's offset added (an ARRAY row), S
# k added before `imul ...,S`, with (slot + k) * S in the array (a CODE row: k plus the move in records, so the move is a
# whole number of records). (manager, rva, the instruction's bytes, U or S). Dead: no reference to the function and no
# pointer to it in the image, kept so that every form of the record is moved.
FOLDS = [
    ('customization', 0x878329, '48 83 c0 24', 'S'),            # the avatar spawn's voice delta (0x878260)
    ('customization', 0xB51CB2, '48 83 c0 24', 'S'),            # the language change's package release
    ('customization', 0x879C6B, '48 83 c1 24', 'S'), ('customization', 0x879F42, '48 83 c0 24', 'S'),  # dead
    ('customization', 0xAB0A2E, '48 05 6c 09 00 00', 'U'),      # dead
    ('player', 0xACC65B, '4c 8d a0 c8 02 00 00', 'U'),
    ('player', 0x60933B, '49 8d 87 08 01 00 00', 'U'),          # 0x609210: each slot's entity, from 0x571250
    ('player', 0x6ED25E, '48 05 c8 02 00 00', 'U'),             # 0x6ECEC0
    ('player', 0xACE444, '48 83 c0 0d', 'S'),                   # 0xACDF50 clears A2 +0x10 of every slot
    ('player', 0xBA8974, '48 83 c1 0d', 'S'),                   # 0xBA8900 (vtable 0x214C688) sets it
    ('player', 0xBB23AB, '48 8d 43 0d', 'S'),                   # 0xBB22F0 (vtable 0x214CC40) sets it for one slot
    ('player', 0x591C6E, '48 05 c8 02 00 00', 'U'), ('player', 0x7C739E, '48 05 08 01 00 00', 'U'),  # dead
    ('player', 0x609004, '48 83 c0 0d', 'S'), ('player', 0x6090C6, '48 83 c0 0d', 'S'),              # dead
    ('player', 0x6091D8, '48 8d 43 0d', 'S'), ('player', 0xB83464, '48 83 c1 0d', 'S'),              # dead
    ('player', 0xB835BC, '48 8d 43 0d', 'S'),                                                         # dead
    ('ship_inventory', 0x17868BE, '48 05 90 00 00 00', 'U'),    # dead
    ('ship_modules_sync', 0x51BBB4, '48 81 c1 88 00 00 00', 'U'),  # the add (op1) clears the new slot's A1 record
    ('ship_modules_sync', 0x7AF805, '48 81 c1 88 00 00 00', 'U'),  # dead copy
]
# The scan's hits on other objects with the same stride (rva: whose).
NOT_FOLDS = {**dict.fromkeys([0x4F4440, 0x500833, 0x504D60, 0x50CEA0, 0x511F4C, 0x511FFD], 'a record table at [state + 0xF1xxxx]'),
             **dict.fromkeys([0x6F4454, 0x6F4472, 0x6F4490, 0x6F44AE], 'records of 0x26D0 from [0x3326AB0] + 0x8060'),
             0x13CDAE8: 'the object at [0x347CF20]', 0x205A2AF: 'a count at +0x168 of a frame object',
             0x205D0D7: 'a dead copy of 0x205A2AF'}
for fm, rva, raw, kind in FOLDS:
    raw = bytes.fromhex(raw)
    if kind == 'U': EXTRA.append((fm, rva, len(raw) - 4, struct.unpack('<I', raw[-4:])[0])); continue
    moves = [(a, STRIDE[(fm, a[0])]) for a in MANAGERS[fm][4] if a[0] <= raw[3] * STRIDE[(fm, a[0])] < a[1]]
    assert len(raw) == 4 and len(moves) == 1, hex(rva)
    (lo, _, new), stride = moves[0]
    assert (new - lo) % stride == 0 and raw[3] + (new - lo) // stride < 0x80, (hex(rva), 'move a whole number of records')
    CODE.append((fm, rva, raw.hex(' '), (raw[:3] + bytes([raw[3] + (new - lo) // stride])).hex(' ')))
# The loot rows' clears and sums, written out for four rows. With the rows moved, a jump replaces the first instruction
# and the mod's code does the eight rows at their new place: a clear stores the replaced instruction's 16 bytes and rows
# 4 to 7 (the game's other stores are ARRAY rows and clear the rest; xmm0 is zero there), a sum adds the count at +0xC of
# all eight (it replaces the whole sum: the adds it skips are not rows). The caves exist exactly while the rows are
# moved. (manager, rva, bytes the jump replaces, where the code goes back to (0: it returns), code)
ROW, COUNT = 0x18, 0xC
def moved(m, old): return [new for lo, _, new in MANAGERS[m][4] if lo == old][0]
def clear(reg, old): return ''.join(' movups xmmword ptr [%s+0x%x], xmm0\n' % (reg, moved('score', old) + at) for at in [0] + list(range(4 * ROW, 8 * ROW, 16)))
def total(reg): return ' mov eax, dword ptr [%s+0x%x]\n' % (reg, moved('score', 0xC440) + COUNT) + ''.join(
    ' add eax, dword ptr [%s+0x%x]\n' % (reg, moved('score', 0xC440) + i * ROW + COUNT) for i in range(1, 8))
CAVES = [
    ('score', 0x62D24F, '0f 11 83 40 c4 00 00', 0x62D256, clear('rbx', 0xC440)),       # mission start
    ('score', 0x63132D, '0f 11 81 40 c4 00 00', 0x631334, clear('rcx', 0xC440)),       # the loot rows' refill by slot
    ('score', 0xAC5F22, '0f 11 83 40 c4 00 00', 0xAC5F29, clear('rbx', 0xC440)),       # mission start, a second copy
    ('score', 0x6316C1, '41 0f 11 86 a0 c4 00 00', 0x6316C9, clear('r14', 0xC4A0)),   # and the rows by index behind it
    ('score', 0x6309B7, '8b 81 94 c4 00 00', 0, total('rcx') + ' ret'),                 # a leaf: the sum is its value
    ('score', 0x126EC60, '8b 87 94 c4 00 00', 0x126EC78, total('rdi')),                # the team's sum
]
# The bytes each cave leaves dead: the replaced ones, and the skipped adds up to where it goes back or the sum's ret.
DEAD = {0x6309B7: 0x6309D0, 0x126EC60: 0x126EC78}
CAVE_CODE = 64      # CoManagerCave.code; a cave's room (CO_MGR_CAVE_ROOM) also holds the jump back

R64 = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi'] + ['r%d' % i for i in range(8, 16)]
FAM = {r: r for r in R64}
for names_ in [('eax', 'ax', 'al', 'rax'), ('ecx', 'cx', 'cl', 'rcx'), ('edx', 'dx', 'dl', 'rdx'), ('ebx', 'bx', 'bl', 'rbx'),
               ('esi', 'si', 'sil', 'rsi'), ('edi', 'di', 'dil', 'rdi'), ('ebp', 'bp', 'bpl', 'rbp'), ('esp', 'sp', 'spl', 'rsp')]:
    FAM.update(dict.fromkeys(names_[:3], names_[3]))
for i in range(8, 16): FAM.update(dict.fromkeys(['r%dd' % i, 'r%dw' % i, 'r%db' % i], 'r%d' % i))
IMM = re.compile(r'0x[0-9a-f]+$')

def operands(rest):
    out, depth, cur = [], 0, ''
    for ch in rest.split('#')[0]:
        depth += ch == '['; depth -= ch == ']'
        if ch == ',' and not depth: out.append(cur.strip()); cur = ''
        else: cur += ch
    return out + [cur.strip()] if cur.strip() else out

def added(mn, ops):
    """(register, constant) of an instruction that adds a constant to a register"""
    if mn in ('add', 'sub') and len(ops) == 2 and ops[0] in FAM and IMM.match(ops[1]) and int(ops[1], 16) < 1 << 31:
        return FAM[ops[0]], int(ops[1], 16) * (1 if mn == 'add' else -1)
    if mn == 'inc' and len(ops) == 1 and ops[0] in FAM: return FAM[ops[0]], 1
    m = re.match(r'\[(\w+)(?:\*\d)?\+0x([0-9a-f]+)\]$', ops[1]) if mn == 'lea' and len(ops) == 2 and ops[0] in FAM else None
    return (FAM[ops[0]], int(m.group(2), 16)) if m and m.group(1) in FAM else None

def fold_scan(path, start, end, arrays):
    """FOLDS' two forms over the whole linear listing, for every stride of `arrays` [(manager, lo, hi, stride)]:
    {rva of the constant's instruction: (U or S, manager, the array's start)}. Registers are followed through moves only,
    8 instructions either side of the scale, not across calls and jumps."""
    by = {}
    for a in arrays:
        if a[3] > 1: by.setdefault(a[3], []).append(a)
    p = subprocess.Popen(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel', '--no-show-raw-insn',
                          '--start-address=%#x' % start, '--stop-address=%#x' % end, path], stdout=subprocess.PIPE, text=True)
    back, fwd, hits = [], [], {}
    for line in p.stdout:
        m = re.match(r'^\s*([0-9a-f]+):\t(\S+)\s*(.*)$', line)
        if not m: continue
        a, mn, ops = int(m.group(1), 16), m.group(2), operands(m.group(3))
        k = added(mn, ops)
        base = re.match(r'\[(\w+)', ops[1]).group(1) if mn == 'lea' and len(ops) == 2 and ops[1].startswith('[') else None
        live = []
        for reg, S, left in fwd:
            if k and (k[0] == reg or FAM.get(base) == reg):
                hits.update({a: ('U', arr[0], arr[1]) for arr in by[S] if arr[1] <= k[1] < arr[2]})
            elif not (ops and FAM.get(ops[0]) == reg and mn not in ('cmp', 'test', 'push')) and mn not in ('call', 'ret', 'jmp') and left > 1:
                live.append((reg, S, left - 1))
        fwd, S = live, None
        if mn == 'imul' and len(ops) == 3 and ops[0] in FAM and ops[1] in FAM and IMM.match(ops[2]):
            S, dst, src = int(ops[2], 16), FAM[ops[0]], FAM[ops[1]]
        elif mn == 'shl' and len(ops) == 2 and ops[0] in FAM and IMM.match(ops[1]) and int(ops[1], 16) < 32:
            S, dst, src = 1 << int(ops[1], 16), FAM[ops[0]], FAM[ops[0]]
        if S in by:
            for b, bmn, bops in reversed(back):
                bk = added(bmn, bops)
                if bk and bk[0] == src:
                    hits.update({b: ('S', arr[0], arr[1]) for arr in by[S] if arr[1] <= bk[1] * S < arr[2]})
                    break
                if bmn == 'mov' and len(bops) == 2 and FAM.get(bops[0]) == src and bops[1] in FAM: src = FAM[bops[1]]; continue
                if bops and FAM.get(bops[0]) == src and bmn not in ('cmp', 'test', 'push'): break
            fwd.append((dst, S, 8))
        back = [] if mn in ('call', 'ret', 'jmp') else (back + [(a, mn, ops)])[-8:]
    assert p.wait() == 0
    return hits

def assemble(text):
    with tempfile.TemporaryDirectory() as tmp:
        open(tmp + '/c.s', 'w').write('.intel_syntax noprefix\n.text\n%s\n' % text)
        subprocess.run(['as', '--64', '-o', tmp + '/c.o', tmp + '/c.s'], check=True)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', tmp + '/c.o', tmp + '/c.bin'], check=True)
        return open(tmp + '/c.bin', 'rb').read()

# classb_final.tsv lists every instruction with a fitting value. In these functions the base register is not the block
# (another object, or the manager itself, which patch_sites covers), except at the sites of BLOCK_ONLY.
NOT_BLOCK = {('ship_modules_sync', 'fn 0x569490'), ('ship_modules_sync', 'fn 0x571250'), ('ship_modules_sync', 'fn 0xbc2d30')}
BLOCK_ONLY = {0x57142E, 0xBD13E7}
CLASSB = ['classb_final.tsv', 'classb_avatar.tsv']
# Block sites the list misses: the statistics reset at 0x56FFF0 reaches the block through a second global.
EXTRA_BLOCK = [('ship_customization', 0x57009B, 4), ('ship_modules_sync', 0x5700C5, 0), ('kill_streak', 0x5700D1, 0)]
only = None
args = [a for a in sys.argv[1:] if not a.startswith('--only=')]
for a in sys.argv[1:]:
    if a.startswith('--only='): only = a[7:].split(',')
dump = open(args[0], 'rb').read(); names = [m for m in MANAGERS if not only or m in only]; rows = []


def delta(m, field):
    for lo, hi, new in MANAGERS[m][4]:
        if lo <= field < hi: return new - lo
    return None


for mi, m in enumerate(names):
    g, off, size, new_size, moved = MANAGERS[m]
    for line in [l for f in LISTS.get(m, [f'patch_sites_{m}.tsv']) for l in open(HERE + f).read().splitlines()[1:]]:
        c = line.split('\t'); rva, at, raw = int(c[0], 16), int(c[5]), bytes.fromhex(c[6])
        old = struct.unpack_from('<I', raw, at)[0]; assert dump[rva:rva + len(raw)] == raw, (m, hex(rva))
        assert old == int(c[4], 16) and delta(m, old) is not None, (m, line)
        rows.append((rva, at, old, mi, delta(m, old), 'CO_MGR_ARRAY'))
    for em, rva, at, old, *add in EXTRA:
        if em == m:
            assert struct.unpack_from('<I', dump, rva + at)[0] == old and (add or delta(m, old) is not None)
            rows.append((rva, at, old, mi, add[0] if add else delta(m, old), 'CO_MGR_ARRAY'))
    for line in [l for f in CLASSB for l in open(HERE + f).read().splitlines()]:
        c = line.split('\t')
        if c[0] != m or (m, c[3]) in NOT_BLOCK and int(c[2], 16) not in BLOCK_ONLY: continue
        rva, field = int(c[2], 16), int(c[1], 16); want = struct.pack('<I', off + field)
        at = [a for a in range(1, 9) if dump[rva + a:rva + a + 4] == want]; assert len(at) == 1, (m, hex(rva), at)
        assert 0 <= field < size, line
        rows.append((rva, at[0], off + field, mi, delta(m, field) or 0, 'CO_MGR_BLOCK'))
    for em, rva, field in EXTRA_BLOCK:
        if em == m:
            at = dump.find(struct.pack('<I', off + field), rva, rva + 12) - rva; assert 1 <= at <= 8, hex(rva)
            rows.append((rva, at, off + field, mi, delta(m, field) or 0, 'CO_MGR_BLOCK'))
    caps = 0
    for line in open(HERE + 'cap_sites.tsv').read().splitlines()[1:]:
        c = line.split('\t')
        if c[0] != m: continue
        old = SLOTS.get(m, (4, 8))[0]
        rva = int(c[3], 16); assert dump[rva:rva + 3] == bytes([0x83, 0xF8, old]), hex(rva); rows.append((rva, 2, old, mi, 0, 'CO_MGR_CAP')); caps += 1
    assert caps == 4, (m, caps)
    for em, rva, raw, at in LOOPS:
        raw = bytes.fromhex(raw)
        if em == m:
            assert dump[rva:rva + len(raw)] == raw and raw[at] == 4, hex(rva); rows.append((rva, at, 4, mi, 0, 'CO_MGR_CAP'))
    for em, rva, raw, new in CODE:
        if em == m:
            assert dump[rva:rva + 4] == bytes.fromhex(raw)
            rows.append((rva, 0, struct.unpack('<I', bytes.fromhex(raw))[0], mi, struct.unpack('<i', bytes.fromhex(new))[0], 'CO_MGR_CODE'))
if only is None:
    # Every index constant of FOLDS' forms is listed, an ARRAY row, or another object's.
    pe = struct.unpack_from('<I', dump, 0x3C)[0]; first = pe + 24 + struct.unpack_from('<H', dump, pe + 20)[0]
    size, start = struct.unpack_from('<II', dump, first + 8)
    hits = fold_scan(args[0], start, start + size, [(m, lo, hi, STRIDE[(m, lo)]) for m in MANAGERS for lo, hi, _ in MANAGERS[m][4]])
    arrays = {r[0] for r in rows if r[5] == 'CO_MGR_ARRAY'}
    for fm, rva, raw, kind in FOLDS:
        assert dump[rva:rva + len(bytes.fromhex(raw))] == bytes.fromhex(raw) and hits.get(rva, ('',))[:2] == (kind, fm), hex(rva)
    late = [hex(a) for a, h in sorted(hits.items()) if a not in NOT_FOLDS and a not in {f[1] for f in FOLDS} and not (h[0] == 'U' and a in arrays)]
    assert not late, ('an index register reaches a moved array: list it in FOLDS or NOT_FOLDS', late)
    assert set(NOT_FOLDS) <= set(hits), 'a NOT_FOLDS row the scan no longer finds'
    loot = sum(1 for r in rows if r[5] == 'CO_MGR_ARRAY' and names[r[3]] == 'score' and 0xC440 <= r[2] < 0xC500)
    assert loot == LOOT_ROWS, loot
caves = []
for em, rva, raw, back, text in CAVES:
    if em not in names: continue
    raw = bytes.fromhex(raw); code = assemble(text)
    # the replaced instruction addresses the old place of the array it does for eight rows
    old = struct.unpack_from('<I', raw, len(raw) - 4)[0]
    assert dump[rva:rva + len(raw)] == raw and 5 <= len(raw) <= 12 and old in (0xC440, 0xC4A0, 0xC494), hex(rva)
    assert len(code) <= CAVE_CODE and (back == 0) == code.endswith(b'\xc3'), hex(rva)
    caves.append((rva, raw, back, code))
# No row of this step, the session's or the pods' lies in the bytes a cave replaces or leaves dead.
def listed(name, part=None):
    text = open(os.path.dirname(HERE[:-1]) + '/../src/' + name).read()
    if part: text = text[text.index(part):]
    return [(int(a, 16), int(b)) for a, b in re.findall(r'^    \{0x([0-9A-F]+)u, (\d+),', text, re.M)]
stubs = listed('co_session_sites.h', 'kCoSessionStubs')
taken = [(rva, rva + size) for rva, size in stubs] + [(rva + at, rva + at + 4) for rva, at in listed('co_session_sites.h') if (rva, at) not in stubs]
taken += [(rva + at, rva + at + 4) for rva, at in listed('co_pod_sites.h')] + [(r[0] + r[1], r[0] + r[1] + (1 if r[5] == 'CO_MGR_CAP' else 4)) for r in rows]
for rva, raw, back, _ in caves:
    end = max(rva + len(raw), DEAD.get(rva, 0))
    assert all(b <= rva or a >= end for a, b in taken), hex(rva)
from collections import Counter
dup = [r for r in sorted(rows) if Counter(x[0] for x in rows)[r[0]] > 1]
for r in dup: print('DUP %x at %d old %x %s add %x %s' % (r[0], r[1], r[2], names[r[3]], r[4], r[5]))
assert not dup, 'a site is listed twice'
out = ['/* Generated by scripts/gen_manager_sites.py from scripts/sites/; do not edit. Game build 25480438. */',
       '#ifndef CO_MANAGER_SITES_H', '#define CO_MANAGER_SITES_H', '',
       '/* {name, the global that holds it, offset in the block, size, new size, slots, the build that moves it (players)} */',
       'static const CoManager kCoManagers[] = {']
out += ['    {"%s", 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu, %d, %d},' % (m, *MANAGERS[m][:4], SLOTS.get(m, (4, 8))[1], 8 if m in EIGHT_ONLY else 4) for m in names]
out += ['};', '', '/* {rva, offset of the value in the instruction, its vanilla value, manager, what the new array offset adds,',
        ' * kind, the instruction bytes before the value} */', 'static const CoManagerSite kCoManagerSites[] = {']
for rva, at, old, mi, add, kind in sorted(rows):
    pin = ', '.join('0x%02X' % b for b in dump[rva:rva + at]) or '0'
    out.append('    {0x%Xu, %d, 0x%Xu, %d, %d, %s, {%s}},' % (rva, at, old, mi, add, kind, pin))
hexes = lambda b: ', '.join('0x%02X' % x for x in b)
out += ['};', '', '/* {rva, bytes the jump replaces, where the cave goes back to (0: its code returns), the code\'s length, the replaced',
        ' * bytes, the code} */', 'static const CoManagerCave kCoManagerCaves[] = {']
out += ['    {0x%Xu, %d, 0x%Xu, %d, {%s}, {%s}},' % (rva, len(raw), back, len(code), hexes(raw), hexes(code)) for rva, raw, back, code in sorted(caves)]
out += ['};', '', '#endif', '']
open(args[1] if len(args) > 1 else 'src/co_manager_sites.h', 'w').write('\n'.join(out))
print(len(rows), 'sites,', len(caves), 'caves;', ', '.join('%s %d' % (m, sum(1 for r in rows if r[3] == i)) for i, m in enumerate(names)))
