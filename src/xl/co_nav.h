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
/* 1 when both sites hold the game's own instructions and the engine's nav code is the one this was made for: a game
 * whose level 11 brought no navigation step. `whole` is then the mod's stub: HellForge's form in full, with the mod's
 * function, ending in the game's call and the way back. */
int co_nav_plain(uintptr_t game);
size_t co_nav_whole(uint8_t* out, uintptr_t code, int ship, uintptr_t retile, uintptr_t game);
/* Checks, then points HellForge's two jumps at the mod's stubs, or takes the two sites when they are the game's. NULL, or why; both jumps are HellForge's again after a
 * failure. */
const char* co_nav_install(uintptr_t game);
/* Takes the install's two pages, one within reach of each image, while the game has not filled that space yet: at
 * the mod's start for the exe, as game.dll loads for the game. Either may be 0; a page already taken is kept. */
void co_nav_reserve(uintptr_t game, uintptr_t exe);
/* What a stub calls before a grid is set up. The ship's call first runs `ship` (play's world back to the game's). */
void co_nav_retile(uint32_t id, uintptr_t level);
void co_nav_ship(void (*ship)(void));
/* For the grid's readers: the engine once the steps are in (0 before), and the tile size last written for a level
 * whose tiles are neither 128 nor 256 m, a complex level's 512 (NULL before the install, 0 until such a level). */
uintptr_t co_nav_exe(void);
const float* co_nav_world_tile(void);

#ifdef CO_TEST
void co_nav_test_reset(void);
uintptr_t co_nav_test_page(void);
const uint8_t* co_nav_test_reserved(void);
const float* co_nav_test_tile(void);
#endif

#endif
