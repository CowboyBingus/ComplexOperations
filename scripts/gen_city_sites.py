#!/usr/bin/env python3
# Writes src/xl/co_city_sites.h: the city generator's footprint list (0xFA8FE0) and the capacity of the location list
# its buildings are added to (generate_city_roads, op 0xA7, 0xFA9E10; the placer 0xF993E0), as HellForge's level 11
# leaves them and as a complex level needs them (game build 25480438):
#   scripts/gen_city_sites.py <game.dll image dump> <HellForge native/mod-loader> <the prototype's difficulty_maps.c> [out]
# HellForge's three footprint values and its location places are read from its source, the complex level's from the
# prototype's; the game's bytes and the way the capacity reaches the placer are checked on the dump.
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
game = open(sys.argv[1], 'rb').read()
stock_dir = sys.argv[2]
proto_maps = open(sys.argv[3]).read()
proto_dir = os.path.dirname(sys.argv[3])
out = sys.argv[4] if len(sys.argv) > 4 else HERE + '/../src/xl/co_city_sites.h'


def hexes(b): return ', '.join('0x%02X' % x for x in b)


def at(rva, text): return game[rva:rva + len(bytes.fromhex(text))] == bytes.fromhex(text)


def call(rva): return rva + 5 + struct.unpack_from('<i', game, rva + 1)[0] if game[rva] == 0xE8 else None


def capacities(text):
    body = re.search(r'kCityCapacities\[7\] = \{(.*?)\};', text, re.S).group(1)
    rows = re.findall(r'\{(0x[0-9A-F]+)u, (\d+), (\d), 4, \{([^}]*)\}, (0x[0-9A-F]+)u\}', body)
    assert len(rows) == 7
    return [(int(r, 16), int(n), int(a), bytes(int(x, 0) for x in pin.split(',')), int(v, 16)) for r, n, a, pin, v in rows]


def define(text, name): return int(re.search(r'#define %s (\d+)u' % name, text).group(1))


FOOTPRINT = 0x60
maps = open(os.path.join(stock_dir, 'difficulty_maps.c')).read()
block = open(os.path.join(stock_dir, 'difficulty_block.c')).read()
towns = open(os.path.join(stock_dir, 'difficulty_towns.c')).read()
sites_h = open(os.path.join(stock_dir, 'location_sites.h')).read()
assert 'ApiWrite(game + kCityCapacities[i].rva + kCityCapacities[i].at, &kCityCapacities[i].value, kCityCapacities[i].width)' in maps
stock, own = capacities(maps), capacities(proto_maps)
for (rva, length, a, pin, value), (prva, plength, pa, ppin, pvalue) in zip(stock, own):
    assert (rva, length, a, pin) == (prva, plength, pa, ppin) and game[rva:rva + length] == pin and a + 4 == length, hex(rva)
assert [r[0] for r in stock[:3]] == [0xFA904D, 0xFA909F, 0xFA90BE]
assert [(s[4], o[4]) for s, o in zip(stock[3:], own[3:])] == [(s[4], s[4]) for s in stock[3:]]   # the per-cell list is HellForge's in both
places_stock, places_own = stock[1][4], own[1][4]
assert stock[0][4] == stock[2][4] == places_stock * FOOTPRINT and own[0][4] == own[2][4] == places_own * FOOTPRINT
assert struct.unpack_from('<I', stock[0][3], 1)[0] == 0x800 * FOOTPRINT and struct.unpack_from('<I', stock[1][3], 6)[0] == 0x800
footprint = [(rva, length, pin[:a] + struct.pack('<I', value)) for rva, length, a, pin, value in stock[:3]]
# 0xFA8FE0: rcx the op's record, kept in rbp; nothing between the allocation's size and the allocator's call changes
# edx or rcx, and the clear's size is loaded after the call, which keeps rbp
assert at(0xFA901A, '488BE9') and at(0xFA9095, 'C7818002000000000000') and at(0xFA90A9, '498B0C24')
assert call(0xFA90B2) == 0x173B8D0 and at(0xFA90B7, '33D2') and at(0xFA90C4, '488BC8488BF0') and call(0xFA90CA) == 0x208AAA0
assert at(0xFA90CF, '4889B578020000')   # mov [rbp+0x278], rsi: rbp is still the record

# HellForge's location places, written to every pass record's capacity; op 0xA7's two builders are among them
locations_stock = define(block, 'DIF_LOCATIONS')
assert 'uint32_t size = Generation()->block, places = Generation()->caps.locations;' in block
assert 'ApiWrite(game + kLocationCaps[i].rva + kLocationCaps[i].at, &places, 4)' in block
assert re.search(r'kGenerationLimits\[GEN_FIELDS\]\[2\] = \{\{DIF_LOCATIONS, 65024u\}', block)
caps = sites_h[sites_h.index('kLocationCaps[]'):]
for builder, op in ((0xC22A4F, 0xC227E1), (0xC22DF2, 0xC22B84)):
    assert '{0x%Xu, 6, 0xAB4u}' % builder in caps[:caps.index('};')]
    assert at(builder, 'C78200020000B40A0000') and at(op, 'C700A7000000')   # mov dword [rdx+0x200], 2740 in op 0xA7's record
    assert at(builder - 0x15, '498D85208E7500')  # lea rax, [r13+0x758E20]: locations A
# the op is called with the record + 8, copies {count, capacity, array} to [rbp-0x70] and hands that list down to the placer
assert at(0xC19632, '488D4E08') and call(0xC1963C) == 0xFA9E10 and at(0xFA9E4F, '488BF1')
assert at(0xFA9EF8, '8945908B86F8010000894594488B86A00100004889459 8'.replace(' ', ''))
assert at(0xFAAE3F, '488D45904889442420') and call(0xFAAE48) == 0xF9C170
assert at(0xF9C50B, '488B9C24300100004C8BC3') and call(0xF9C568) == 0xF99B20 and at(0xF99B25, '4C89442418')
assert at(0xF99EE5, '488B45604889442450') and call(0xF99F19) == 0xF993E0 and at(0xF9946C, '4C8BA500010000')
assert at(0xF9C56D, '8B43043903')       # the buildings' loop tests the same list after each call
# the calls and the flags behind the two capacity reads: a call (0xFA9F31) and a compare (0xF994AA)
assert call(0xFA9F31) == 0x173B8D0 and at(0xF994AA, '41390424')
limit = [(0xFA9EFB, 6, bytes.fromhex('8B86F8010000')), (0xF994A5, 5, bytes.fromhex('418B442404'))]
for rva, length, pin in limit: assert game[rva:rva + length] == pin
behind = bytes.fromhex('894594'), bytes.fromhex('41390424')
assert game[0xFA9F01:0xFA9F04] == behind[0]
# HellForge's own steps here stay: its counted branch right behind the placer's compare, its watch of the list's check
assert '{0xF994AEu, {0x0F, 0x83, 0x03, 0x06, 0x00, 0x00}, 0xF99AB7u, 2}' in towns and '#define DIF_FOOTPRINT_CHECK 0xFAAECEu' in maps
reserve = define(proto_maps, 'CITY_LOCATION_RESERVE')
gen = define(open(os.path.join(proto_dir, 'difficulty.c')).read(), 'DIF_GEN_LOCATIONS')
assert 'uint32_t places = DIF_LOCATIONS, reserve = DIF_GEN_LOCATIONS - CITY_LOCATION_RESERVE;' in proto_maps
assert locations_stock < gen - reserve

o = ['/* Generated by scripts/gen_city_sites.py from HellForge\'s and the prototype\'s sources and the game\'s image; do not edit. Game build 25480438. */\n',
     '#ifndef CO_CITY_SITES_H\n#define CO_CITY_SITES_H\n\n#include <stdint.h>\n\n',
     '/* The footprint list\'s places (0x60 bytes each): HellForge\'s level 11, and a complex level\'s. */\n',
     '#define CO_CITY_FOOTPRINT 0x%Xu\n#define CO_CITY_PLACES_STOCK 0x%Xu\n#define CO_CITY_PLACES_OWN 0x%Xu\n' % (FOOTPRINT, places_stock, places_own),
     '/* The location list as op 0xA7 sees it: HellForge\'s own places on a normal level; on a complex one the placer stops\n',
     ' * %d short of the generation\'s %d and the op\'s other tests keep the table\'s capacity. */\n' % (reserve, gen),
     '#define CO_CITY_LOCATIONS_STOCK %du\n#define CO_CITY_LOCATIONS_OWN %du\n#define CO_CITY_NO_LIMIT 0xFFFFFFFFu\n\n' % (locations_stock, gen - reserve),
     '/* The record\'s fields: op 0xA7 gets the record + 8, so its capacity is read at +0x1F8 where the builders write +0x200. */\n',
     '#define CO_CITY_RECORD_PLACES 0x284u\n#define CO_CITY_RECORD_LOCATIONS 0x1F8u\n\n',
     '/* {site, length, the bytes there (HellForge\'s for the first three, the game\'s for the two reads), the way on} */\n',
     'enum { CO_CITY_ALLOCATION, CO_CITY_CAPACITY, CO_CITY_CLEAR, CO_CITY_OP_LIMIT, CO_CITY_PLACER, CO_CITY_SITES };\n',
     'typedef struct CoCitySite { uint32_t rva; uint8_t length; uint8_t bytes[10]; uint32_t back; } CoCitySite;\n',
     'static const CoCitySite kCoCitySites[CO_CITY_SITES] = {\n']
for rva, length, b in footprint + limit: o.append('    {0x%Xu, %d, {%s}, 0x%Xu},\n' % (rva, length, hexes(b), rva + length))
o.append('};\n/* The game\'s instruction behind each of the two reads: the store of the op\'s copy, the placer\'s compare. */\n')
o.append('static const uint8_t kCoCityOpBehind[3] = {%s};\nstatic const uint8_t kCoCityPlacerBehind[4] = {%s};\n\n#endif\n' % (hexes(behind[0]), hexes(behind[1])))
open(out, 'w').write(''.join(o))
