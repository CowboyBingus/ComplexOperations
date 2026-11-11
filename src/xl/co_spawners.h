/* The enemy director's spawner list per mission. At mission load the game appends the map's 'spawner' locations to a
 * list of 128 in the HiveMind manager with no check; HellForge's level 11 stops at 128. A complex operation's map has
 * more (197 were counted), and a spawner left out is an outpost without its fabricators. So a complex mission's
 * entries go to a list of 1024 of the mod's, which GuardForce's builder then reads; a normal mission keeps the
 * manager's list behind HellForge's guard, and the builder's loop is the game's.
 * Which list a mission has is decided at its first append: the state change clears the manager, the count with it,
 * and appends in the same call, after the mission's level init. The builder follows the list the count belongs to. */
#ifndef CO_SPAWNERS_H
#define CO_SPAWNERS_H

#include "co.h"

/* NULL when HellForge's guard is at the append and the append's body, the builder's loop and its places are the
 * game's code this was made for. Reads only. */
const char* co_spawners_check(uintptr_t game);
/* The mod's step in front of HellForge's guard, the loop's two tests and the group's places. All or none. */
const char* co_spawners_install(uintptr_t game);
/* A mission's level init (1: complex), and the ship (0), where a complex mission's counts are logged once. */
void co_spawners_mission(int complex);
/* Spawner positions left out past a group's 16 places on complex missions so far; NULL before the install. */
volatile long* co_spawners_places_full(void);

/* The page's stubs, each built for `code`. `cells` is the page's data: the mission's flag, the list in use, the two
 * counters, the list's base less the entries' offset. Each returns its size. */
enum { CO_SPAWNER_CELL_FLAG = 0x00, CO_SPAWNER_CELL_OWN = 0x04, CO_SPAWNER_CELL_LEFT = 0x08, CO_SPAWNER_CELL_FULL = 0x0C, CO_SPAWNER_CELL_BASED = 0x10 };
#define CO_SPAWNER_STUB_APPEND 0x000u
#define CO_SPAWNER_STUB_FIRST 0x100u
#define CO_SPAWNER_STUB_NEXT 0x140u
#define CO_SPAWNER_STUB_PLACES 0x1C0u
#define CO_SPAWNER_STUB_END 0x240u
#define CO_SPAWNER_CELLS 0x1000u
#define CO_SPAWNER_LIST 0x2000u
size_t co_spawners_append_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t guard);
size_t co_spawners_first_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_next_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells);
size_t co_spawners_places_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t next);

#ifdef CO_TEST
void co_spawners_test_reset(void);
uint8_t* co_spawners_test_page(void);
#endif

#endif
