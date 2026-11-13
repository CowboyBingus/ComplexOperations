/* The mark. Every peer builds its mission table from the operation records, and the mission seed a table entry keeps
 * is what a level is generated from on every machine. So the decision "this operation is complex" travels in the
 * seed: the entry fill is hooked, the game's own fill runs first (its row pick and use counters stay the game's), and
 * a level-11 entry's seed then gets the decision in bit 0. The decision is the planet's (co_roll.c), from the board. */
#include <string.h>

#include "co.h"

#define TABLE 0xF9A10u       /* the mission table: 0x14A entries of 0x4C, the seed at +0x34 */
#define TABLE_ENTRIES 0x14Au

/* push rbp, r12..r15; sub rsp, 0x20: arguments 5 and up are read from the stack as the detour's signature says */
static const uint8_t kFill[14] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};

typedef uint64_t (*FillFn)(uint64_t a, uint64_t b, uint64_t settings, uint64_t d, uint64_t slot, uint64_t placement, uint64_t difficulty,
    uint64_t mission_seed, uint64_t operation_seed, uint64_t entry, uint64_t rows, uint64_t count, uint64_t type, uint64_t counters);
static FillFn g_fill;
static uintptr_t g_game;

static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* The records the mission table takes (0x11E5757..0x11E5769: in use, the table's planet) at level 11 (+0x20). */
uint32_t co_board_level11(const uint8_t* board, uint32_t planet, CoOperation* ops)
{
    uint32_t n = 0;
    for (uint32_t slot = 0; slot < CO_BOARD_SLOTS; slot++)
    {
        const uint8_t* r = board + slot * 0x5Cu;
        uint16_t p;
        memcpy(&p, r + 0x10, 2);
        if (r[0x34] != 0 && p == planet && r[0x20] == CO_COMPLEX_LEVEL) { ops[n].slot = slot; ops[n].seed = U32(r + 0x0C); n++; }
    }
    return n;
}

/* The whole planet is read for each answer: the board holds one planet (0x12D5550 clears it for another), and a fill
 * and the banner each see it whole once its build is done. A board flagged for a rebuild may be another seed's. */
int co_board_decide(uintptr_t war, uint32_t slot, CoPlanetDecision* d)
{
    uint8_t board[CO_BOARD_BYTES];
    CoOperation ops[CO_BOARD_SLOTS];
    uint8_t complex[CO_BOARD_SLOTS];
    uint16_t planet;
    memset(d, 0, sizeof *d);
    if (slot >= CO_BOARD_SLOTS || g_coApi->read(war + CO_BOARD, board, sizeof board) != 0 || board[0x278C] != 0) return 0;
    if (g_coApi->read(war + CO_BOARD_SEED, &d->board_seed, 4) != 0) return 0;
    memcpy(&planet, board + slot * 0x5Cu + 0x10, 2);
    d->planet = planet;
    d->count = co_board_level11(board, planet, ops);
    const uint32_t guaranteed = co_planet_decide(ops, d->count, d->board_seed, planet, co_complex_chance(), complex);
    int found = 0;
    for (uint32_t i = 0; i < d->count; i++)
    {
        d->complexes += complex[i];
        if (ops[i].slot == slot) { found = 1; d->seed = ops[i].seed; d->complex = complex[i]; }
    }
    if (guaranteed < d->count) d->guaranteed = ops[guaranteed].seed;
    return found;
}

/* One line per planet and board, the first time its missions are filled: a player or a tester can tell which operations
 * on the war table are complex. The table refills often, so each answer is told once. */
static void Told(const CoPlanetDecision* d, uint32_t slot, uint32_t operation_seed, int rolls, int read)
{
    static uint64_t told[64];
    static uint32_t count;
    uint64_t key = (uint64_t)d->board_seed << 32 | d->guaranteed;
    key ^= ((uint64_t)d->planet << 24 | d->count << 16 | d->complexes << 8 | (uint32_t)(rolls | read << 1)) * 0x9E3779B97F4A7C15ull;
    if (!read) key = (uint64_t)operation_seed << 32 | slot | 1ull << 31;
    for (uint32_t i = 0; i < count && i < 64u; i++) if (told[i] == key) return;
    told[count % 64u] = key; count++;
    if (!read) co_log("complex operations: operation %08X in board slot %u: its record could not be read or is another: a normal operation", operation_seed, slot);
    else if (!rolls) co_log("complex operations: planet %u: %u level-11 operations, no roll (the part is off)", d->planet, d->count);
    else co_log("complex operations: planet %u: %u of %u operations complex (%08X guaranteed)", d->planet, d->complexes, d->count, d->guaranteed);
}

void co_mark_entry(uintptr_t game, uint32_t slot, uint32_t difficulty, uint32_t operation_seed, uintptr_t entry, int rolls)
{
    uint64_t war = 0;
    uint32_t seed = 0;
    CoPlanetDecision d;
    if (difficulty != CO_COMPLEX_LEVEL || slot >= CO_BOARD_SLOTS) return;
    if (g_coApi->read(game + CO_WAR_OBJECT, &war, 8) != 0 || war == 0) return;
    /* only an entry of the war object's own table: its slot is then a record of the board beside it */
    if (entry < war + TABLE || entry >= war + TABLE + TABLE_ENTRIES * 0x4Cu || (entry - (war + TABLE)) % 0x4Cu != 0) return;
    if (g_coApi->read(entry + 0x34, &seed, 4) != 0) return;
    /* the fill's operation seed is the record's (0x11E5CCD); anything else is not the board this answer is from */
    const int read = co_board_decide((uintptr_t)war, slot, &d) && d.seed == operation_seed;
    seed = co_complex_entry_seed(seed, difficulty, rolls && read && d.complex);
    memcpy((void*)(entry + 0x34), &seed, 4);
    Told(&d, slot, operation_seed, rolls, read);
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
