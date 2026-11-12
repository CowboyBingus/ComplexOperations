/* A complex level's share of difficulty 11's row. The row is HellForge's and is never written: each reader copies it
 * and works on its copy, and the mod's stub changes the copy right behind the reader's last store. A complex
 * operation is three missions on one map of 16384 m: its outposts and objective budgets are three times the row's,
 * and its radii are level 10's by the map's growth over the world's (20 / 8). */
#ifndef CO_ROWS_H
#define CO_ROWS_H

#include "co.h"

#define CO_MISSIONS 3u
#define CO_OBJECTIVE_ROOM 18u /* of the selection's 30 places, 12 stay a mission's own */
#define CO_RADIUS_SCALE 2.5f  /* map scale 20 over a world 8 times the game's */
#define CO_XL_PLAYABLE 0.95f  /* the playable fraction of half the world; the row's factor is this over 0.38 */

/* The copy's last store and the instruction behind it, at each reader. */
#define CO_ROW_RADII_SITE 0xF417B7u      /* 0xF415E0, lg = r13, the copy at rbp-0x80: its +0xF0 is [rbp+0x70] */
#define CO_ROW_RADII_BACK 0xF417BFu
#define CO_ROW_OUTPOSTS_SITE 0xF7E4F5u   /* 0xF7E250, lg = [[rsp+0x60]+0x18], the copy at rbp+0xD0 */
#define CO_ROW_OUTPOSTS_BACK 0xF7E4FDu
#define CO_ROW_OBJECTIVES_SITE 0x175686Cu /* 0x1756730, no level in reach: [rsp+0x60] has the seed and, at +0x10, the level */
#define CO_ROW_OBJECTIVES_BACK 0x1756872u
extern const uint8_t kCoRowRadii[8], kCoRowOutposts[8], kCoRowObjectives[6];
#define CO_ROW_RADII_STUB 54u
#define CO_ROW_OUTPOSTS_STUB 64u
#define CO_ROW_OBJECTIVES_STUB 79u
/* Each stub runs the site's own two instructions, the second last (the game branches on its flags). The first two
 * act on a level whose disk is the mod's key; the third on a level-11 seed with the mark while `honours` is 1. */
size_t co_row_radii_stub(uint8_t* out, uintptr_t code, uintptr_t game, const float radii[4]);
size_t co_row_outposts_stub(uint8_t* out, uintptr_t code, uintptr_t game);
size_t co_row_objectives_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t honours);

/* The distance row: seven instructions read `table[level - 1][k]`, three factors a level; HellForge pointed them at
 * its table of eleven rows. They read the mod's copy of it instead, whose row 11 is HellForge's or, while a complex
 * level generates, the factor of a 16384 m world. */
#define CO_DISTANCE_SITES 7
#define CO_DISTANCE_GAME 0x2143170u
typedef struct CoDistanceSite { uint32_t rva; uint8_t at; uint8_t bytes[6]; } CoDistanceSite;
const CoDistanceSite* co_distance_sites(void);

/* NULL when the three sites hold the game's bytes and the seven reads are the game's with one table behind them all.
 * Reads only. */
/* The row reads are the game's code; usable before level 11 is installed. */
const char* co_rows_code_check(uintptr_t game);
/* HellForge's distance table is behind the reads (level 11 installed); NULL too when the code is not the game's. */
const char* co_rows_level_check(uintptr_t game);
const char* co_rows_check(uintptr_t game);
/* Checks, then the three stubs and the distance copy. NULL, or why; sites written before a failure are the game's or
 * HellForge's again. */
const char* co_rows_install(uintptr_t game);
/* A level is set up: the distance row is the complex one or HellForge's. */
void co_rows_level(int complex);
/* Whether this machine takes a marked seed for a complex mission now (co_complex_honours): the objective stub's switch. */
void co_rows_honours(int on);
/* Level 10's city factor over the complex one's: what the prototype scales the city zone by. 1 before the install. */
float co_rows_city_ratio(void);

#ifdef CO_TEST
void co_rows_test_reset(void);
const float* co_rows_test_distance(void);
uintptr_t co_rows_test_code(void);
#endif

#endif
