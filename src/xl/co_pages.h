/* The world's size as the game's code reads it, on two pages of the mod's. The game has one set of constants for every
 * level; a level of another size needs its readers pointed at values that can change. One page is not enough: on the
 * ship the game plays while a war-table preview generates, so the generator's readers follow every level set up
 * (the generation page) and play's readers follow a mission only and are the game's own on the ship (the live page). */
#ifndef CO_PAGES_H
#define CO_PAGES_H

#include "co.h"

/* What generator code reads. `size` (the level's size the level-init stub stores; 0 the game's) and `capMul` (the item
 * caps' multiplier, set around single passes) are the level code's own and no scale sets them. */
typedef struct CoGenPage { float half, full, inverse, negHalf, negFull, size, clearing, quarter, padPx, padMetre, padQuad, flatPx; uint32_t capMul; } CoGenPage;
/* What play code reads: the world's size, the director's spawn gate, the drop bounds, a stratagem's clearance span, the
 * enemies' goal bounds and the units' motion bounds. */
typedef struct CoLivePage { float half, full, inverse, negHalf, negFull, gateHigh, gateLow, dropHigh, dropLow, maskSpan, goalHigh, goalLow, motionHigh, motionLow; } CoLivePage;

typedef struct CoWorldReader { uint32_t rva; uint8_t length, which, page, flags; uint32_t function; uint8_t bytes[9]; } CoWorldReader;
enum { CO_FIELD_GATE_HIGH, CO_FIELD_GATE_LOW, CO_FIELD_DROP_HIGH, CO_FIELD_DROP_LOW, CO_FIELD_MASK_SPAN, CO_FIELD_GOAL_HIGH, CO_FIELD_GOAL_LOW,
    CO_FIELD_MOTION_HIGH, CO_FIELD_MOTION_LOW, CO_FIELD_QUARTER, CO_FIELD_CLEARING };
typedef struct CoPageReader { uint32_t rva; uint8_t length, field, page; uint32_t value; uint8_t bytes[9]; } CoPageReader;
typedef struct CoPageImmediate { uint32_t rva; uint8_t at; uint32_t value; uint8_t bytes[11]; } CoPageImmediate;

/* A page's values for a world of 2048 m x scale. At scale 1 each is the game's constant, bit for bit. */
void co_page_gen_values(CoGenPage* page, float scale);
void co_page_live_values(CoLivePage* page, float scale);

/* NULL when every instruction the install changes is the game's own; else what is not. Reads only. */
const char* co_pages_check(uintptr_t game);
/* Points every reader at its page and takes over the generator's immediates. Every instruction is compared with the
 * game's bytes and every page's reach is checked before the first write: NULL, or why nothing was changed. The pages
 * are the caller's memory, within reach of a 32-bit offset from game.dll, and hold the game's values afterwards. */
const char* co_pages_install(uintptr_t game, CoGenPage* gen, CoLivePage* live);
#define CO_PAGES_GEN 1
#define CO_PAGES_LIVE 2
/* A level is set up: the generation page and the immediates take its scale; the live page only when it is a mission
 * (a preview is generated while the ship is played). Which pages changed, as CO_PAGES_ bits. */
int co_pages_level(float scale, int mission);
/* The ship is set up: the live page is the game's own again. CO_PAGES_LIVE when that changed it. */
int co_pages_ship(void);
/* The scale a page holds now (1 before the install). */
float co_pages_scale(int page);

#ifdef CO_TEST
void co_pages_test_reset(void);
const CoWorldReader* co_pages_test_readers(size_t* count);
const CoPageReader* co_pages_test_singles(size_t* count);
const CoPageImmediate* co_pages_test_immediates(size_t* count);
#endif

#endif
