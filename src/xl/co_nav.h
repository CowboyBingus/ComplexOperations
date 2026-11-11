/* The navigation grid per level, in front of HellForge's two steps. HellForge retiles a grid of 8 x 8 and leaves any
 * other alone; a process whose grid was laid out 32 x 32 for a 16384 m world is the mod's to retile, for every level
 * and the ship: the grid cannot change size between levels (its block, bitsets and carvers are laid out once). A
 * normal level then gets HellForge's tile size with bounds four times wider (co_nav_plan), so its inner 8 x 8 tiles
 * have HellForge's bounds bit for bit. */
#ifndef CO_NAV_H
#define CO_NAV_H

#include "co.h"

#define CO_NAV_CELLS 32u
#define CO_NAV_STUB 129u      /* the mission's; the ship's has 5 more */
#define CO_NAV_SHIP_STUB 134u
/* NULL when HellForge's two jumps and stubs and the engine's nav code are the ones this was made for; `stubs` gets
 * the two stubs' addresses. Reads only. */
const char* co_nav_check(uintptr_t game, uintptr_t stubs[2]);
/* Each stub keeps every register, calls `retile` with the id (ecx) and the level (rdx; 0 on the ship) and goes on at
 * HellForge's stub. Returns the size. */
size_t co_nav_stub(uint8_t* out, uintptr_t code, int ship, uintptr_t retile, uintptr_t stock);
/* Checks, then points HellForge's two jumps at the mod's stubs. NULL, or why; both jumps are HellForge's again after a
 * failure. */
const char* co_nav_install(uintptr_t game);
/* What a stub calls before a grid is set up. The ship's call first runs `ship` (play's world back to the game's). */
void co_nav_retile(uint32_t id, uintptr_t level);
void co_nav_ship(void (*ship)(void));

#ifdef CO_TEST
void co_nav_test_reset(void);
uintptr_t co_nav_test_page(void);
const float* co_nav_test_tile(void);
#endif

#endif
