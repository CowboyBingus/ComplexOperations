/* FixUnreachableAreas (0xF67980) on a complex level. The pass joins every pocket of open route cells to the main area
 * by a path of at most 7 blocked cells, never starts that search from a pocket cell that holds a location, and starts
 * the level over when a pocket stays apart. On a world of 16384 m a location's zone is about its own cell, so pockets
 * of location cells alone are common. A complex level searches from location cells too, lists 512 components where
 * the game lists 32, and joins a pocket the pass gave up on by a breadth-first path before the restart. Any other
 * level runs the game's instructions and HellForge's restart step. */
#ifndef CO_POCKET_H
#define CO_POCKET_H

#include "co.h"

/* The pass's arrays: route cells of 0x110 (16 routes at +8, a location at +0x98; a route's two cells at +0x20 and
 * +0x28), subregions of 0x20 (type +0, type object +0xC, its route cell +0x18), a component id a cell. */
typedef struct CoPocketGraph
{
    uint64_t cells, subregions, ids, types, ctx;
    uint32_t count;
    uint8_t planet;                               /* a type object without bit 5 of +0x170 blocks its cells */
    char (*in_disk)(uint64_t ctx, uint64_t subregion);
} CoPocketGraph;
#define CO_POCKET_CELLS_MAX 65536u

/* Joins component `pocket` to `main` over blocked cells that lie in the disk and hold no location, as the pass
 * applies a path of its own: the pocket's cells take the main's id, each path cell the pocket's last cell's first 16
 * bytes and the main's id. The path's cells, or -1 with nothing written. `searched`: the cells the search took up. */
int co_pocket_reconnect(const CoPocketGraph* g, uint32_t pocket, uint32_t main, uint32_t* searched);
/* The restart stub's call, in the pass's frame. 1 when the pocket is joined and the pass goes on. */
char co_pocket_frame(uint8_t* rsp, uint8_t* rbp, uint64_t ctx, uint32_t pocket, uint64_t types, uint32_t main);

/* The four stubs of one page to run at `code`: the search at +0, the list's two sizes at +0x40 and +0x60, the
 * restart at +0x80, which hands every level it does not join a pocket of to `next`. */
#define CO_POCKET_PAGE 0x1000u
#define CO_POCKET_AT_SEARCH 0x00u
#define CO_POCKET_AT_ALLOC 0x40u
#define CO_POCKET_AT_CLEAR 0x60u
#define CO_POCKET_AT_RESTART 0x80u
void co_pocket_build(uint8_t* page, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t next);

/* NULL when the three sites and the code around them are the game's. Reads only. */
const char* co_pocket_check(uintptr_t game);
/* After the chain's install, whose restart stub this goes in front of. All four sites or none. */
const char* co_pocket_install(uintptr_t game, uintptr_t flag);

#ifdef CO_TEST
void co_pocket_test_reset(void);
uintptr_t co_pocket_test_page(void);
void co_pocket_test_in_disk(char (*in_disk)(uint64_t, uint64_t));
#endif

#endif
