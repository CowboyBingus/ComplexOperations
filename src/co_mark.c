/* The mark. Every peer builds its mission table from the operation records, and the mission seed a table entry keeps
 * is what a level is generated from on every machine. So the decision "this operation is complex" travels in the
 * seed: the entry fill is hooked, the game's own fill runs first (its row pick and use counters stay the game's), and
 * a level-11 entry's seed then gets the decision in bit 0. */
#include <string.h>

#include "co.h"

#define BOARD 0xF7280u       /* in the war object: records of 0x5C, the planet a u16 at +0x10 */
#define BOARD_SLOTS 110u
#define TABLE 0xF9A10u       /* the mission table: 0x14A entries of 0x4C, the seed at +0x34 */
#define TABLE_ENTRIES 0x14Au

/* push rbp, r12..r15; sub rsp, 0x20: arguments 5 and up are read from the stack as the detour's signature says */
static const uint8_t kFill[14] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};

typedef uint64_t (*FillFn)(uint64_t a, uint64_t b, uint64_t settings, uint64_t d, uint64_t slot, uint64_t placement, uint64_t difficulty,
    uint64_t mission_seed, uint64_t operation_seed, uint64_t entry, uint64_t rows, uint64_t count, uint64_t type, uint64_t counters);
static FillFn g_fill;
static uintptr_t g_game;

void co_mark_entry(uintptr_t game, uint32_t slot, uint32_t difficulty, uint32_t operation_seed, uintptr_t entry, int rolls)
{
    uint64_t war = 0;
    uint16_t planet = 0;
    uint32_t seed = 0;
    if (difficulty != CO_COMPLEX_LEVEL || slot >= BOARD_SLOTS) return;
    if (g_coApi->read(game + CO_WAR_OBJECT, &war, 8) != 0 || war == 0) return;
    /* only an entry of the war object's own table: its slot is then a record of the board beside it */
    if (entry < war + TABLE || entry >= war + TABLE + TABLE_ENTRIES * 0x4Cu || (entry - (war + TABLE)) % 0x4Cu != 0) return;
    if (g_coApi->read((uintptr_t)war + BOARD + slot * 0x5Cu + 0x10u, &planet, 2) != 0 || g_coApi->read(entry + 0x34, &seed, 4) != 0) return;
    seed = co_complex_entry_seed(seed, difficulty, operation_seed, planet, rolls);
    memcpy((void*)(entry + 0x34), &seed, 4);
}

static uint64_t FillDetour(uint64_t a, uint64_t b, uint64_t settings, uint64_t d, uint64_t slot, uint64_t placement, uint64_t difficulty,
    uint64_t mission_seed, uint64_t operation_seed, uint64_t entry, uint64_t rows, uint64_t count, uint64_t type, uint64_t counters)
{
    const uint64_t result = g_fill(a, b, settings, d, slot, placement, difficulty, mission_seed, operation_seed, entry, rows, count, type, counters);
    /* the game passes 32-bit values in these places: the upper halves are not the arguments' */
    co_mark_entry(g_game, (uint32_t)slot, (uint32_t)difficulty, (uint32_t)operation_seed, (uintptr_t)entry,
        co_complex_rolls(co_peers_is_host(), co_complex_on(), co_peers_host_row()));
    return result;
}

const char* co_mark_check(uintptr_t game)
{
    uint8_t now[sizeof kFill];
    if (g_coApi->read(game + CO_MARK_FILL, now, sizeof now) != 0 || memcmp(now, kFill, sizeof now) != 0)
        return "the mission entry fill is not the game's code this mod was made for";
    return NULL;
}

const char* co_mark_install(uintptr_t game)
{
    const char* why;
    if (g_fill != NULL) return NULL;
    if ((why = co_mark_check(game)) != NULL) return why;
    g_game = game;
    if (g_coApi->hook(g_coMod, game + CO_MARK_FILL, (void*)FillDetour, (void**)&g_fill) != 0) { g_fill = NULL; return "the mission entry fill could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_mark_test_reset(void) { g_fill = NULL; g_game = 0; }
#endif
