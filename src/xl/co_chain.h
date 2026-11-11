/* The map generator's constants for a complex level. HellForge's level 11 jumps from each of these instructions to a
 * stub of its own that acts when the level's disk ([lg+0x2D7C]) is its own 1022 / 1024, and is the game's instruction
 * on any other level. A complex level has another disk, so it needs the constants of its own size at the same
 * instructions: the mod's stub goes in front of HellForge's jump and passes every level that is not complex on to
 * HellForge's stub, which is neither changed nor written. */
#ifndef CO_CHAIN_H
#define CO_CHAIN_H

#include "co.h"

/* What a stub is filled with: the level's disk as an immediate, a jump into the game, the address of a value of the
 * stub's owner, such a value itself, or another address of the owner's. */
enum { CO_FILL_KEY, CO_FILL_GAME, CO_FILL_CELL, CO_FILL_FLOAT, CO_FILL_OWNER };
typedef struct CoFill { uint8_t at, kind; uint32_t arg; } CoFill;
/* The mod's values for a level of 16384 m, by the prototype's rules at map scale 20: the route sites' spacing (m), the
 * Voronoi grid, the route cells' factor x 4 on the disk, 0.25, the same factor on the site square, and the vista
 * level's horizontal scale. */
enum { CO_CELL_SPACING, CO_CELL_GRID, CO_CELL_FACTOR, CO_CELL_QUARTER, CO_CELL_SQUARE, CO_CELL_VISTA, CO_CELLS };
void co_chain_values(float cells[CO_CELLS]);

#define CO_CHAINS 11
typedef struct CoChain
{
    const char* what;
    uint32_t site; uint8_t length;       /* HellForge's jump: E9 rel32, then 0x90 to the length */
    const uint8_t* game;                 /* what the game has there */
    const uint8_t* stock; uint8_t stockSize; const CoFill* stockFills; uint8_t stockFillCount;
    const uint8_t* own; uint8_t ownSize; const CoFill* ownFills; uint8_t ownFillCount;
} CoChain;
const CoChain* co_chain_sites(void);

/* The mod's stubs, each in a slot of CO_CHAIN_SLOT bytes of one page, the values behind them. A stub keeps the flags,
 * tests `flag` (1 while the level being generated is complex) and jumps to HellForge's stub when it is 0; else its own
 * code follows, which tests the level's disk for the mod's key itself. */
#define CO_CHAIN_SLOT 0x80u
#define CO_CHAIN_HEAD 17u
#define CO_CHAIN_CELLS (CO_CHAINS * CO_CHAIN_SLOT)
#define CO_CHAIN_PAGE 0x1000u

/* NULL when each site holds HellForge's jump to a stub of the form this was made for, with disk 1022 / 1024; else what
 * is not. `stubs`, when given, gets where each jump leads. Reads only. */
const char* co_chain_check(uintptr_t game, uintptr_t stubs[CO_CHAINS]);
/* Fills `page` (CO_CHAIN_PAGE bytes, to run at `code`) for HellForge's stubs at `stubs`. */
void co_chain_build(uint8_t* page, uintptr_t code, uintptr_t game, uintptr_t flag, const uintptr_t stubs[CO_CHAINS]);
/* Checks, builds a page of the mod's own and points the eleven jumps at it. NULL, or why every site is as it was. */
const char* co_chain_install(uintptr_t game, uintptr_t flag);
/* Where the mod's stub of a site runs once installed, for a step that goes in front of it (co_pocket.c); else 0. */
uintptr_t co_chain_slot(int chain);

#ifdef CO_TEST
void co_chain_test_reset(void);
uintptr_t co_chain_test_page(void);
#endif

#endif
