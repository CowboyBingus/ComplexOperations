/* The engine's SpeedTree forest (world + 0x2308B8, ctor 0x20AA90): every tree a stamp's layers spawn takes one slot of
 * its instance grid (forest + 0x120, laid out by 0x7AD1D0 at 0x20AE39 with room 0x18000, fixed in the exe). The spawn
 * 0x20BC80 first takes a record id per tree (free list +0xD8/+0xE0, else the next of the +0xF0 records), then a grid
 * slot per tree from 0x7AD5E0, and writes through it unchecked (0x20CF57): a full grid (free head [grid+0x2C] = -1)
 * returns 0 and the write crashes. The unspawn 0x20D1A0 gives both back. The grid keeps no count, so the trees are
 * counted as live record ids (records made minus ids free), which is never below the grid slots in use. */
#ifndef CO_FOREST_H
#define CO_FOREST_H

#include "co.h"

#define CO_FOREST_UNIT_WORLD 0x18u     /* unit: its world (0xDC46E, rcx = [r13+0x18]) */
#define CO_FOREST_OF_WORLD 0x2308B8u   /* world: the forest (0xDC485, the spawn's rcx) */
#define CO_FOREST_FREE 0xD8u           /* forest: record ids free for reuse (0x20BF00) */
#define CO_FOREST_RECORDS 0xF0u        /* forest: records made (0x20BF22) */
#define CO_FOREST_GRID 0x120u          /* forest: the instance grid (0x20ADEA, 0x20CF2C) */
#define CO_FOREST_GRID_ROOM 0x1Cu      /* grid: its room (0x7AD36D) */
#define CO_FOREST_ROOM_MIN 1024u
#define CO_FOREST_ROOM_MAX (1u << 24)

/* 1 when the engine's code at the sites above is the code these offsets were read from. */
int co_forest_pins(uintptr_t exe);
/* The forest a unit's layers spawn into. 1 with it, 0 when the chain does not read. */
int co_forest_of_unit(uint64_t unit, uintptr_t* forest);
/* The forest's trees (live record ids) and its grid's room. 1 when both read and make sense, else 0. */
int co_forest_trees(uintptr_t forest, uint32_t* trees, uint32_t* room);
/* 1 when `trees` is at least `share` of `room`. */
int co_forest_over(uint32_t trees, uint32_t room, float share);
/* 1 when one more spawn call fits: `trees` plus a reserve of the larger of a tenth of the room and twice `most` (the
 * most trees one call has added) stays within `room`. */
int co_forest_call_fits(uint32_t trees, uint32_t room, uint32_t most);

#endif
