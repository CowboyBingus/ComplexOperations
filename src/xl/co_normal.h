/* A normal level 11 mission with the mod loaded is the mission it is without it. What the mod widens for a complex
 * mission is decided per level here: the level's kind, the navigation tiles, the limits a compare applies, and the HUD
 * stubs that run the game's own code unless the mission in play is complex. */
#ifndef CO_NORMAL_H
#define CO_NORMAL_H

#include "co.h"

/* A complex level's radii, by HellForge's own rule for a larger map (border 389.12 x map scale + 174, fade + 57,
 * disk + 117) on a world of 16384 m: [lg+0x2D78] fade and [lg+0x2D7C] disk as fractions of half the world, +0x2D84 and
 * +0x2D88 the rectangle, +0x2D8C the disk + 1. The disk fraction is the level's key: HellForge's level 11 has
 * 1022 / 1024 there, and its radius field (1023) passes every "larger than" test, so the key is what tells them apart. */
#define CO_COMPLEX_MAP_SCALE 20.0f
void co_complex_radii(float radii[5]);
float co_disk_key(void);
int co_level_complex(float disk);

/* The radii of a complex level. HellForge's step at 0xF71CD8 writes its own for every level 11; the mod's stub runs
 * right behind it (0xF71CDF) and writes these over them while `flag` is 1, so HellForge's step is not touched and its
 * later steps, which test the disk, leave a complex level alone. `check` is NULL when the game's bytes behind and
 * HellForge's step (its form, with disk 1022 / 1024) are the ones this was made for. */
#define CO_RADII_SITE 0xF71CD8u
#define CO_RADII_BEHIND 0xF71CDFu /* xor r10d, r10d; mov [r13+0x218], rdx: 10 bytes */
#define CO_RADII_BACK 0xF71CE9u
#define CO_RADII_STUB 79u
extern const uint8_t kCoRadiiBehind[10];
const char* co_radii_check(uintptr_t game);
size_t co_radii_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag);

/* The navigation grid for a level: HellForge's own tiles (8 x 8; 256 m when the radius field is above 682, else and on
 * the ship 128 m) unless the level is complex (32 x 32 tiles of 512 m over 16384 m). */
typedef struct CoNavPlan { uint32_t grid; float low, high, tile; } CoNavPlan;
void co_nav_plan(float disk, float radius, CoNavPlan* plan);

/* What a list or pool takes: HellForge's limit on a normal level, the mod's on a complex one. */
enum { CO_LIMIT_SPAWNERS, CO_LIMIT_MARKER_SLOTS, CO_LIMIT_MARKER_TRACKERS, CO_LIMIT_WELD_VERTICES, CO_LIMIT_STAMP_SLOTS, CO_LIMITS };
uint32_t co_level_limit(int which, int complex);

/* The HUD's objective rows. Each stub tests `flag` (1: the mission in play is complex) and otherwise runs the bytes
 * the site held, so a normal mission's rows are the game's. `code` is where the stub will run. Returns its size. */
#define CO_HUD_LIST_SITE 0x17CF6E7u /* call 0x5D96F0, 5 bytes: the builder's list part begins */
#define CO_HUD_LIST_NEXT 0x17CF6ECu
#define CO_HUD_LIST_BACK 0x17CF79Au /* behind the list part */
#define CO_HUD_LIST_CALL 0x5D96F0u
#define CO_HUD_FIND_SITE 0x17CFA0Bu /* the updater's row search, 43 bytes */
#define CO_HUD_FIND_BACK 0x17CFA36u
#define CO_HUD_FLAG_SITE 0x17CFA66u /* mov [rsp+rax+0x30], cl; test r8b, r8b: 7 bytes */
#define CO_HUD_FLAG_BACK 0x17CFA6Du
#define CO_HUD_LIST_STUB 39u
#define CO_HUD_FIND_HEAD 57u
#define CO_HUD_FLAG_STUB 27u
extern const uint8_t kCoHudListBytes[5], kCoHudFindBytes[43], kCoHudFlagBytes[7];
/* NULL when the three sites hold the game's bytes. Reads only. */
const char* co_hud_check(uintptr_t game);
size_t co_hud_list_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t rows);
/* The search's head: the game's 43 bytes on a normal mission; the grouped search follows it at `code` + its size. */
size_t co_hud_find_head(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag);
size_t co_hud_flag_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag);

#endif
