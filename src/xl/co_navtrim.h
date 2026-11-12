/* Stock's 8 x 8 navigation tiles on a grid of 32 x 32 (DESIGN-AUDIT, gate 10 criterion 6b ruling, option A). The grid
 * cannot shrink per level, so a normal level or the ship keeps it and an armed world gets two engine stubs:
 * - exe 0x7ACDD0, the point-to-cell step: for an armed world's grid it runs the engine's own instructions on stock's
 *   bounds with 8 x 8 cells and adds 12 on each axis, so every point lands in cells 12..19 as stock's lands in 0..7;
 * - exe 0x7B744C, behind op 0x68's priority fill: stock's 64 priorities in cells 12..19, every other cell -2^20 - its
 *   index (never built), and 64 tiles left.
 * Disarmed worlds and every other grid run the original bytes. The array keeps the fill's layout (entry = cell id):
 * the scheduler's write-back (0x7B8690) and the raisers (0x78EE80, 0x7B6295, 0x7B642C) index it by cell id. */
#ifndef CO_NAVTRIM_H
#define CO_NAVTRIM_H

#include "co.h"

#define CO_NAVTRIM_CELL 0x7ACDD0u   /* exe: (grid rcx, x xmm1, y xmm2) -> eax = cy << 16 | cx */
#define CO_NAVTRIM_SET 0x7B7440u    /* exe: lea rdx, grid; call the fill; mov [rsi+0xC4], eax (at +0xC) */
#define CO_NAVTRIM_SET_SITE 0x7B744Cu
#define CO_NAVTRIM_SET_BACK 0x7B7452u
#define CO_NAVTRIM_FILL 0x7B31B0u   /* exe: the priority fill (S1) */
#define CO_NAVTRIM_WORLDS 4u        /* nav ids that can be armed */
#define CO_NAVTRIM_OUTER (-(1 << 20))

extern const uint8_t kCoNavCell[0x7B], kCoNavSet[0x20], kCoNavFill[0x137];
/* The fill's constants: 0.5, the abs mask, 2.0 and 50.0, at these exe addresses. */
extern const uint32_t kCoNavFillConst[4][2];
/* Stock's 8 x 8 priorities in the fill's order (cell y x 8 + x), from S1. */
extern const int32_t kCoNavStockPrio[64];

/* Takes the stubs' block within reach of the exe; at the mod's start, as co_nav_reserve. */
void co_navtrim_reserve(uintptr_t exe);
/* Pins the three code ranges and the constants, then writes both stubs. NULL, or why nothing was written. */
const char* co_navtrim_install(uintptr_t exe);
/* Why the last install refused, or NULL once the stubs are in. */
const char* co_navtrim_refused(void);
/* Arms nav id `id` (world `world`) with stock's bounds `low`..`high` on both axes. 0, or 1 when it cannot be. */
int co_navtrim_arm(uint32_t id, uintptr_t world, float low, float high);
void co_navtrim_disarm(uint32_t id);
/* `bytes`, read at `at`, as the engine's where they cover a site holding the trim's jump into its reviewed block; any
 * other form of a site is left as read. Other mods' pins on the two sites compare against the engine's bytes through it. */
void co_navtrim_unpatched(uintptr_t at, uint8_t* bytes, size_t size);
/* The world armed at `id`, or 0. */
uintptr_t co_navtrim_armed(uint32_t id);

#ifdef CO_TEST
void co_navtrim_test_reset(void);
uintptr_t co_navtrim_test_block(void);
#endif

#endif
