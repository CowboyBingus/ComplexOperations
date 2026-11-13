/* The enemy director's spawner list per mission. At mission load the game appends the map's 'spawner' locations to a
 * list of 128 in the HiveMind manager with no check; HellForge's level 11 stops at 128. A complex operation's map has
 * more (197 were counted), and a spawner left out is an outpost without its fabricators. So a complex mission's
 * entries go to a list of 1024 of the mod's, which GuardForce's builder then reads; a normal mission keeps the
 * manager's list behind HellForge's guard, and the builder's loop is the game's.
 * Which list a mission has is decided at its first append: the state change clears the manager, the count with it,
 * and appends in the same call, after the mission's level init. The builder follows the list the count belongs to.
 * The builder puts its groups in a scratch block of 2 x GuardForce's capacity (192 at a mission's start) and checks no
 * count; past it a group reads unzeroed scratch. So the block is sized for every group the builder's lists can give.
 * A group's own lists (tasks, positions, alarms, the patrol spawner) are filled by marker counts of one level unit, the
 * level's data, with no check either, and an alarm or patrol spawner whose key has no group indexes -1. Their appends
 * get a bound, as a full list in the game's own code would; every one on every level, and all go in or none. */
#ifndef CO_SPAWNERS_H
#define CO_SPAWNERS_H

#include "co.h"

/* NULL when HellForge's guard is at the append and the append's body, the builder's loop, its places, its group
 * block and a group's own appends are the game's code this was made for. Reads only. */
const char* co_spawners_check(uintptr_t game);
/* The mod's step in front of HellForge's guard, the loop's two tests, the group's places, the builder's group block
 * of co_spawners_groups() groups and the bounds of a group's own lists. All or none. */
const char* co_spawners_install(uintptr_t game);
/* The groups GuardForce's builder can make at most: the AI's tasks, the spawners and the invasion sites, one each. */
uint32_t co_spawners_groups(void);
/* A mission's level init (1: complex), and the ship (0), where a complex mission's counts are logged once. */
void co_spawners_mission(int complex);
/* Spawner positions left out past a group's 16 places on complex missions so far; NULL before the install. */
volatile long* co_spawners_places_full(void);
/* The entries a group's own lists left out so far, one of the cells below (CO_SPAWNER_CELL_TASKS ..); NULL before the
 * install. */
volatile long* co_spawners_skipped(uint32_t cell);

/* The page's stubs, each built for `code`. `cells` is the page's data: the mission's flag, the list in use, the two
 * counters, the list's base less the entries' offset. Each returns its size. */
enum { CO_SPAWNER_CELL_FLAG = 0x00, CO_SPAWNER_CELL_OWN = 0x04, CO_SPAWNER_CELL_LEFT = 0x08, CO_SPAWNER_CELL_FULL = 0x0C, CO_SPAWNER_CELL_BASED = 0x10,
    CO_SPAWNER_CELL_TASKS = 0x18, CO_SPAWNER_CELL_SITES = 0x1C, CO_SPAWNER_CELL_ALARMS = 0x20, CO_SPAWNER_CELL_PATROLS = 0x24 };
#define CO_SPAWNER_STUB_APPEND 0x000u
#define CO_SPAWNER_STUB_FIRST 0x100u
#define CO_SPAWNER_STUB_NEXT 0x140u
#define CO_SPAWNER_STUB_PLACES 0x1C0u
#define CO_SPAWNER_STUB_TASKS 0x240u
#define CO_SPAWNER_STUB_SITES 0x280u
#define CO_SPAWNER_STUB_ALARMS 0x2C0u
#define CO_SPAWNER_STUB_PATROLS 0x300u
#define CO_SPAWNER_STUB_END 0x340u
#define CO_SPAWNER_CELLS 0x1000u
#define CO_SPAWNER_LIST 0x2000u
size_t co_spawners_append_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t guard);
size_t co_spawners_first_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_next_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_places_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t next);
size_t co_spawners_tasks_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_sites_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_alarms_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_patrols_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);

#ifdef CO_TEST
void co_spawners_test_reset(void);
uint8_t* co_spawners_test_page(void);
#endif

#endif
