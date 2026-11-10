# The in-mission squad HUD with seven teammate panels (game build 25480438): the sites scripts/gen_ui_sites.py reads.
# P = H2 + 0x1F8C8 (HUD = [0x346D538], H2 = HUD + 0x24E340) is a widget of 0x1FA90 bytes packed between other HUD
# members, so it cannot grow: header 0..0x298, W 3 x 0x158 at +0x298, PAN 3 x 0xA6A0 at +0x6A0, POS u32[3] at
# +0x1FA80, CNT at +0x1FA8C. P moves whole to a block of the mod's with seven of each:
#   W +0x298 (7 x 0x158), POS +0xC00 (u32[7]), CNT +0xC1C, PAN +0xC20 (7 x 0xA6A0, to 0x49A80).
# A site outside P that reaches P or a panel from H2 or HUD becomes old + D + K[region], D = P2 - (H2 + OLD). P's own
# functions hold P in a register: their displacements change in place. Formats as in map_sites.py.
OLD = 0x1F8C8                               # H2-relative address of P
SIZE = 0x49A80
K = {'HP': 0x0, 'HPAN': 0xC20 - 0x6A0}
RANGE = {'HP': [(0x1F8C8, 0x1F8C9), (0x26DC08, 0x26DC09)],
         'HPAN': [(0x1FF68, 0x1FF68 + 3 * 0xA6A0), (0x26E2A8, 0x26E2A8 + 3 * 0xA6A0)]}


def region_of(v):
    for r, spans in RANGE.items():
        if any(lo <= v < hi for lo, hi in spans): return r
    return None


REL = [
    # P pointer (H2 + 0x1F8C8): init 12e96d0 (twice), reset 12ea5b0, 12eb460, update 12ebc50, layout 12ec800 (twice)
    (0x12E9D75, 7, 3, 0x1F8C8, 'HP'), (0x12E9D9C, 7, 3, 0x1F8C8, 'HP'), (0x12EAAC2, 7, 3, 0x1F8C8, 'HP'),
    (0x12EB49E, 7, 3, 0x1F8C8, 'HP'), (0x12EBECA, 7, 3, 0x1F8C8, 'HP'), (0x12ECA5B, 7, 3, 0x1F8C8, 'HP'),
    (0x12ECA78, 7, 3, 0x1F8C8, 'HP'),
    # panel 0 + x from H2: reset loop (+0x23A8), lookup by entity id 12eb3a0 (+0xA66C, base), 12eb460 (+0x9510, base)
    (0x12EA9E4, 7, 3, 0x22310, 'HPAN'), (0x12EB3B4, 7, 3, 0x2A5D4, 'HPAN'), (0x12EB3DB, 7, 3, 0x1FF68, 'HPAN'),
    (0x12EB4B1, 7, 3, 0x29478, 'HPAN'), (0x12EB551, 7, 3, 0x1FF68, 'HPAN'),
    # panel lookups by entity id (+0xA66C) from HUD: 609210, 60b090, b83820
    (0x60ACE9, 7, 3, 0x278914, 'HPAN'), (0x60AD06, 7, 3, 0x26E2A8, 'HPAN'), (0x60B278, 7, 3, 0x278914, 'HPAN'),
    (0x60B29C, 7, 3, 0x26E2A8, 'HPAN'), (0xB83A3D, 7, 3, 0x278914, 'HPAN'), (0xB83A60, 7, 3, 0x26E2A8, 'HPAN'),
]
INPLACE = [
    # panel counts 3 -> 7, friend rows 4 -> 8
    (0x12EA9EE, '448d7b03', '448d7b07', '12ea5b0 reset: panels (rbx = 0 here)'),
    (0x12EB3CE, '83f803', '83f807', '12eb3a0: panel lookup'),
    (0x12EB569, '83fe03', '83fe07', '12eb460: panels'),
    (0x60ACFD, '83f903', '83f907', '609210: panel lookup'),
    (0x60B28C, '83fe03', '83fe07', '60b090: panel lookup'),
    (0xB83A50, '83f903', '83f907', 'b83820: panel lookup'),
    (0x182D86A, '83fe03', '83fe07', '182d3e0 init: W and panels'),
    (0x182DBA9, '83ff03', '83ff07', '182d9e0: panels filled'),
    (0x182DD3B, '4183fe04', '4183fe08', '182d9e0: friend rows (8 x 0xC0 at [0x347CED8])'),
    (0x182DD70, '83ff03', '83ff07', '182d9e0: unused panels'),
    (0x182DD90, 'bd03000000', 'bd07000000', '182d9e0: unused panels count'),
    (0x182D8D4, 'bd03000000', 'bd07000000', '182d8b0 (no caller in code): panels'),
    # P-internal displacements
    (0x182D7E3, '488d8fa0060000', '488d8f200c0000', '182d3e0: panel i ctor (PAN)'),
    (0x182D802, '488d97a0060000', '488d97200c0000', '182d3e0: panel i as child of P (PAN)'),
    (0x182DBBF, '4805a0060000', '4805200c0000', '182d9e0: panel i (PAN)'),
    (0x182DD89, '498db5a0060000', '498db5200c0000', '182d9e0: unused panels (PAN)'),
    (0x182D8CD, '488db1482a0000', '488db1c82f0000', '182d8b0: panel 0 + 0x23A8'),
    (0x182DB56, '4d8dbd80fa0100', '4d8dbd000c0000', '182d9e0: POS'),
    (0x182DE71, '4139bd8cfa0100', '4139bd1c0c0000', '182d9e0: CNT'),
    (0x182DE7E, '4189bd8cfa0100', '4189bd1c0c0000', '182d9e0: CNT'),
    (0x182D42D, '48c78180fa0100ffffffff', '48c781000c0000ffffffff', '182d3e0: POS[0..1] = -1'),
]
CAVES = [
    (0x182D422, '48c78188fa0100ffffffff',
     'mov qword ptr [rcx+0xc08], -1\n mov qword ptr [rcx+0xc10], -1\n mov qword ptr [rcx+0xc18], -1\n'
     ' jmp game+0x182D42D', '182d3e0: POS[2..6] and CNT = -1 (was one qword at +0x1FA88)', []),
]
# No guards: the panel loops have their own bound of three, which stays while P is the game's.
GUARD4, GUARD4_SITES, GUARD8 = [], [], []
