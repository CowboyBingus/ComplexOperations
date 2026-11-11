/* A complex level's objective list. The game selects one mission's objectives (at most 32 ids in the descriptor) and
 * HellForge's level 11 mixes one partner mission's primaries into them; the map's list then holds what its places take
 * (HellForge's 30). A complex operation is three missions on the one map, so the list is made again from the
 * selection: every mission's primaries as whole blocks, a block being one mission row's main objectives and steps,
 * and the side objectives in the room that is left. Only the selection and the game's tables decide it, so every peer
 * makes the same list. */
#ifndef CO_OBJECTIVES_H
#define CO_OBJECTIVES_H

#include "co.h"

#define CO_LIST_MOST 64u   /* the most places a list is made for */
#define CO_LIST_BLOCKS 16u /* the most blocks on a list (the mission arcs deal that many) */

typedef struct CoListRules
{
    uint32_t missions;     /* the operation's missions on the map */
    uint32_t places;       /* the list's places, as HellForge reports them: no write passes them */
    int primaries;         /* the HUD groups its rows by objective: the other missions' primaries may be added */
    uint32_t wantMajor, wantOptional; /* what the missions want together: the row's counts x the missions */
} CoListRules;

typedef struct CoList
{
    uint32_t ids[CO_LIST_MOST], count;
    /* each entry's block (1.., 0 for a side objective), whether it is its block's main, and each block's mission row */
    uint8_t block[CO_LIST_MOST], main[CO_LIST_MOST], blockRow[CO_LIST_BLOCKS];
    uint32_t blocks;       /* on the list; 0 when the selection's primaries are no mission rows' blocks */
    uint32_t unit;         /* the selection's blocks: one mission's */
    uint32_t own;          /* the selection's leading primaries that are the base row's own */
    uint32_t held;         /* missions that hold their first block */
    uint32_t short_;       /* blocks the missions lack because the places were full */
    uint32_t mains, steps; /* on the list */
    uint32_t major[2], optional[2], dropped[2]; /* selected and on the list; dropped: major, optional */
    uint8_t capped[2];     /* the row's ids hold no more: major, optional */
} CoList;

/* The list for `selected` (n ids of mission row `row`). NULL, or why nothing can be said (the caller then keeps the
 * selection). Never more than rules->places ids, and never a part of a block.
 *  - Each mission past the first gets the selection's first block (the base row's primaries), then its other blocks
 *    (a partner's). A first block takes the place of optional and then major objectives from the end of the
 *    selection; another block of optional ones only, as HellForge's own mixing does.
 *  - The live event's objective, the radar station, a class the row gives neither 2 nor 3, and an objective with no
 *    place on the map (definition +0x2D: the game spawns it from the descriptor) are never taken out.
 *  - While places are free, major and then optional ids of the selection again, an id at most what three missions
 *    of the row hold (the row entry's and the definition's most uses, the definition's exclusive bits). */
const char* co_list(uintptr_t game, uint32_t row, const uint32_t* selected, uint32_t n, const CoListRules* rules, CoList* out);

/* The blocks of a list co_list made, found again by its whole content (a level's blocks never come from another
 * level's list). 0 when the list is none of the last eight made with blocks. */
int co_list_kept(uint32_t row, const uint32_t* ids, uint32_t count, uint8_t* block, uint8_t* main, uint8_t* blockRow, uint32_t* blocks);

#ifdef CO_TEST
void co_list_test_reset(void);
#endif

#endif
