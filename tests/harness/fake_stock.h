/* HellForge's level 11 as the mod finds it in a game: its jumps at the generator's sites and the stubs they lead to,
 * filled the way HellForge's install fills them (native/mod-loader/difficulty_maps.c, _cities.c, _diag.c). */
#ifndef CO_FAKE_STOCK_H
#define CO_FAKE_STOCK_H

#include <stdint.h>

/* Where the stubs lie in the image: one per 0x80 bytes, HellForge's values at +0xF00. */
#define FAKE_STOCK_STUBS 0x3110000u
#define FAKE_STOCK_SPACING 4.0f   /* stand-ins for HellForge's own values: told apart from the mod's */
#define FAKE_STOCK_GRID 5.0f
#define FAKE_STOCK_FACTOR 6.0f
void fake_stock_chains(uint8_t* image, float disk);
/* The map's marker slots as its mission step leaves them (difficulty_missions.c: PrepareMarkerStubs, JumpToMarkerStubs):
 * the game's 16 and 10, and the two guards of a 17th slot, 0x20 apart. fake_stock_chains calls it. */
#define FAKE_STOCK_MARKERS (FAKE_STOCK_STUBS + 0xE00u)
void fake_stock_markers(uint8_t* image);
#define FAKE_STOCK_CLUSTERS 0x3115000u /* its two cluster count stubs, set by fake_stock_chains too */
uint32_t fake_stock_stub(int chain);

/* The row readers as HellForge leaves them: the game's bytes at the three copies' ends, the seven distance reads on
 * its table of eleven rows (row 11: level 10's doubled), and level 10's row with its radii. Sets difficulty_row(10). */
#define FAKE_STOCK_DISTANCE 0x3111000u
#define FAKE_STOCK_ROW_TEN 0x3112000u
void fake_stock_rows(uint8_t* image);

/* HellForge's two navigation stubs behind its jumps, and the engine's nav code in a fake exe (fake_host_exe). */
#define FAKE_STOCK_NAV 0x3113000u /* the mission's stub; the ship's at +0x100 */
void fake_stock_nav(uint8_t* image);

/* A game.dll just loaded under HellForge: its nine pool sizes written, the tenth, the broadphase, the navigation grid's
 * extents and cell size the game's, the plugin setup not begun; and the engine's tile size read and its main-world
 * creation at the render capacity's sites (FAKE_RENDER_*). */
#define FAKE_RENDER_CAP 0x10CDFDu
#define FAKE_RENDER_POOL0 0x10CE29u
#define FAKE_RENDER_SCENE 0x10CEB6u
void fake_stock_early(uint8_t* image);

/* HellForge's jumps and stubs at the stamps' merge, the mine list and the records' store, built as its InstallTowns and
 * InstallMineLimit build them, over the game's bytes there. Stubs at +0, +0x40, +0x80; their counters at +0x100. */
#define FAKE_STOCK_BESIDE 0x3114000u
void fake_stock_beside(uint8_t* image);

/* HellForge's guard of the director's spawner list (its PrepareSpawnerStub and JumpToSpawners) over the game's append,
 * and the builder's loop and group block as the game has them. Called by fake_stock_chains. The stub at +0, its counter
 * at +0x40. */
#define FAKE_STOCK_SPAWNERS 0x3117000u
void fake_stock_spawners(uint8_t* image);

#endif
