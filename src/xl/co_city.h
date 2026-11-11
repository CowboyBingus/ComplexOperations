/* The city generator on a complex level (generate_city_roads, op 0xA7, 0xFA9E10). Its footprint list (0xFA8FE0) is
 * made anew from the generation arena at every call, with HellForge's 0x2000 places; three cities' buildings need
 * 0x8000. Its buildings are added to the location list up to the capacity the op copies from its record, which is
 * the table's: 65024 once this mod has asked for them, where HellForge alone has 8192. So the list's places and the
 * capacity the op sees are the level's: a normal level gets HellForge's list and HellForge's 8192 for the whole op;
 * a complex one the larger list, and its placer (0xF993E0) stops 4096 short of the generation's 16384 locations,
 * which leaves the stamps placed after the cities their room. HellForge's own steps there are not written. */
#ifndef CO_CITY_H
#define CO_CITY_H

#include "co.h"

/* NULL when the list's three sites hold HellForge's values and the two capacity reads the game's bytes. Reads only. */
const char* co_city_check(uintptr_t game);
/* The stubs, each for `code`; `cells` is the address of {the list's places, the op's limit, the placer's limit}.
 * The allocation's reads the places once, stores them as the record's capacity and leaves the size in edx; the
 * clear's takes its size from that capacity; a limit's leaves min(the capacity, the limit) in eax. Return the size. */
enum { CO_CITY_CELL_PLACES, CO_CITY_CELL_OP, CO_CITY_CELL_PLACER, CO_CITY_CELLS };
#define CO_CITY_SLOT 0x20u
size_t co_city_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t cells);
/* The four stubs behind jumps, the capacity's own store taken out. All or none. */
const char* co_city_install(uintptr_t game);
/* Level init: the three numbers for this level. Nothing before the install. */
void co_city_level(int complex);
uint32_t co_city_cell(int which);

#ifdef CO_TEST
void co_city_test_reset(void);
const uint8_t* co_city_test_page(void);
#endif

#endif
