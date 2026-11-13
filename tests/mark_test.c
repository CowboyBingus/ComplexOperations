/* The mark hook on a synthetic frame: the game's fill runs first with every argument as it came, then a level-11
 * entry of the war object's table gets its planet's decision in its seed, from the board beside it: the planet's
 * guaranteed complex operation and each other operation's own roll. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"

void co_mark_test_reset(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define WAR 0x1000000u /* the war object, in the image */
typedef uint64_t (*FillFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static const uint8_t kFill[14] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};

static uint8_t* Entry(uint32_t index) { return g_fake.image + WAR + 0xF9A10u + index * 0x4Cu; }
static uint32_t Seed(uint32_t index) { uint32_t s; memcpy(&s, Entry(index) + 0x34, 4); return s; }
static void SetSeed(uint32_t index, uint32_t seed) { memcpy(Entry(index) + 0x34, &seed, 4); }
/* A board record as the board's build leaves it: seed +0xC, planet +0x10, level +0x20, in use +0x34. */
static void Record(uint32_t slot, uint32_t seed, uint16_t planet, uint8_t level)
{
    uint8_t* r = g_fake.image + WAR + 0xF7280u + slot * 0x5Cu;
    memcpy(r + 0x0C, &seed, 4); memcpy(r + 0x10, &planet, 2); r[0x20] = level; r[0x34] = 1;
}
static void BoardSeed(uint32_t seed) { memcpy(g_fake.image + WAR + 0x17A2BCu, &seed, 4); }
static void Rebuilding(uint8_t flag) { g_fake.image[WAR + 0xF7280u + 0x278Cu] = flag; }

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at += strlen(text)) n++;
    return n;
}

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_mark_test_reset();
    const uint64_t war = (uint64_t)(uintptr_t)(g_fake.image + WAR);
    memcpy(g_fake.image + CO_WAR_OBJECT, &war, 8);
    memcpy(g_fake.image + CO_MARK_FILL, kFill, sizeof kFill);
}

/* An operation the roll makes complex and one it does not, found with the roll itself. */
static uint32_t Operation(uint32_t planet, int complex, uint32_t from)
{
    for (uint32_t seed = from;; seed++) if (co_complex_roll(seed, planet, CO_COMPLEX_CHANCE) == complex) return seed;
}

/* A board seed whose lowest key on `planet` among slots 30..32 is `slot`. */
static uint32_t Guaranteeing(uint32_t planet, uint32_t slot)
{
    for (uint32_t b = 1;; b++)
    {
        uint32_t low = 30;
        for (uint32_t i = 31; i <= 32; i++) if (co_pick_key(b, planet, i) < co_pick_key(b, planet, low)) low = i;
        if (low == slot) return b;
    }
}

int main(void)
{
    const uintptr_t game = (uintptr_t)(Start("the planet's operations"), g_fake.image);
    const uint32_t no1 = Operation(77, 0, 1), no2 = Operation(77, 0, no1 + 1), no3 = Operation(77, 0, no2 + 1), yes = Operation(77, 1, 1);
    /* planet 77 has three level-11 operations; a level-10 one, another planet's and a record not in use do not count */
    Record(30, no1, 77, 11); Record(31, no2, 77, 11); Record(32, no3, 77, 11);
    Record(5, yes, 77, 10); Record(40, yes, 78, 11); Record(33, yes, 77, 11); g_fake.image[WAR + 0xF7280u + 33u * 0x5Cu + 0x34u] = 0;
    BoardSeed(Guaranteeing(77, 31));
    CoOperation ops[CO_BOARD_SLOTS];
    CHECK(co_board_level11(g_fake.image + WAR + 0xF7280u, 77, ops) == 3 && ops[0].slot == 30 && ops[1].slot == 31 && ops[2].slot == 32);
    CHECK(ops[1].seed == no2);
    /* a host that rolls: bit 0 is the decision, the rest of the seed the game's; only the guaranteed one is complex */
    SetSeed(0, 0x12345679u); co_mark_entry(game, 30, 11, no1, (uintptr_t)Entry(0), 1);
    SetSeed(1, 0x12345678u); co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(1), 1);
    SetSeed(2, 0x12345679u); co_mark_entry(game, 32, 11, no3, (uintptr_t)Entry(2), 1);
    CHECK(Seed(0) == 0x12345678u && Seed(1) == 0x12345679u && Seed(2) == 0x12345678u);
    char line[160];
    snprintf(line, sizeof line, "complex operations: planet 77: 1 of 3 operations complex (%08X guaranteed)", no2);
    CHECK(Lines(line) == 1); /* one line per planet, told once */
    /* another operation that rolls complex: two of three */
    Record(32, yes, 77, 11);
    SetSeed(2, 0x2000u); co_mark_entry(game, 32, 11, yes, (uintptr_t)Entry(2), 1);
    CHECK(Seed(2) == 0x2001u);
    snprintf(line, sizeof line, "complex operations: planet 77: 2 of 3 operations complex (%08X guaranteed)", no2);
    CHECK(Lines(line) == 1);
    /* the guaranteed one gone: another slot takes it */
    g_fake.image[WAR + 0xF7280u + 31u * 0x5Cu + 0x34u] = 0;
    Record(32, no3, 77, 11);
    SetSeed(0, 0x3000u); co_mark_entry(game, 30, 11, no1, (uintptr_t)Entry(0), 1);
    SetSeed(2, 0x3000u); co_mark_entry(game, 32, 11, no3, (uintptr_t)Entry(2), 1);
    CHECK((Seed(0) & 1u) + (Seed(2) & 1u) == 1u);
    Record(31, no2, 77, 11);
    /* a machine that does not roll clears the mark, one line for the planet */
    SetSeed(1, 0x12345679u); co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(1), 0);
    CHECK(Seed(1) == 0x12345678u && Lines("complex operations: planet 77: 3 level-11 operations, no roll (the part is off)") == 1);
    /* a board being rebuilt, or a fill whose operation is not the slot's: a normal operation */
    Rebuilding(1);
    SetSeed(1, 0x12345679u); co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(1), 1);
    CHECK(Seed(1) == 0x12345678u);
    Rebuilding(0);
    SetSeed(1, 0x12345679u); co_mark_entry(game, 31, 11, no2 + 1, (uintptr_t)Entry(1), 1);
    snprintf(line, sizeof line, "operation %08X in board slot 31: its record could not be read or is another: a normal operation", no2 + 1);
    CHECK(Seed(1) == 0x12345678u && Lines(line) == 1);
    /* the same entry once more: the guaranteed one again */
    SetSeed(1, 0x12345678u); co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(1), 1);
    CHECK(Seed(1) == 0x12345679u);
    /* other levels, a slot the board does not have, an entry that is not the table's: untouched */
    SetSeed(3, 0xABCDEF01u);
    co_mark_entry(game, 31, 10, no2, (uintptr_t)Entry(3), 1); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 31, 0, no2, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 110, 11, no2, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(3) + 4, 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(0x14A), 0);
    co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(0) - 0x4C, 0);
    SetSeed(0, 0x11u); SetSeed(0x149, 0x21u);
    co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(0), 0); co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(0x149), 0);
    CHECK(Seed(0) == 0x10u && Seed(0x149) == 0x20u); /* the first and the last entry are the table's */
    memset(g_fake.image + CO_WAR_OBJECT, 0, 8);
    co_mark_entry(game, 31, 11, no2, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u); /* no war object yet */

    Start("the install");
    CHECK(co_mark_install(game) == NULL && fake_host_detour(CO_MARK_FILL) != NULL && g_fake.writes == 0);
    CHECK(co_mark_install(game) == NULL && g_fake.hookCount == 1);
    /* the detour, as the game calls it: 14 arguments, the 32-bit ones with other bits above; solo, part off: no roll */
    Record(5, 1u, 77, 11);
    SetSeed(3, 0x7001u);
    const FillFn detour = (FillFn)fake_host_detour(CO_MARK_FILL);
    CHECK(detour(1, 2, 3, 4, 0xAAAAAAAA00000005ull, 6, 0xBBBBBBBB0000000Bull, 8, 0xCCCCCCCC00000000ull | 1u, (uint64_t)(uintptr_t)Entry(3), 11, 12, 13, 14) == 0);
    CHECK(Seed(3) == 0x7000u && Lines("complex operations: planet 77: 1 level-11 operations, no roll (the part is off)") == 1);
    SetSeed(3, 0x7001u);
    detour(1, 2, 3, 4, 5, 6, 10, 8, 1u, (uint64_t)(uintptr_t)Entry(3), 11, 12, 13, 14);
    CHECK(Seed(3) == 0x7001u);

    Start("another game's code");
    g_fake.image[CO_MARK_FILL + 12] ^= 4;
    CHECK(co_mark_install(game) != NULL && g_fake.hookCount == 0);
    Start("a refused hook");
    g_fake.noHook = CO_MARK_FILL;
    CHECK(co_mark_install(game) != NULL && g_fake.hookCount == 0);

    g_case = "who rolls";
    CHECK(co_complex_rolls(1, 1, CO_HOST_ROW_NONE) && !co_complex_rolls(1, 0, CO_HOST_ROW_ON));
    CHECK(co_complex_rolls(0, 1, CO_HOST_ROW_ON) && co_complex_rolls(0, 0, CO_HOST_ROW_ON));
    CHECK(!co_complex_rolls(0, 1, CO_HOST_ROW_OFF) && !co_complex_rolls(0, 1, CO_HOST_ROW_NONE));
    /* the row as a client holds it */
    Start("the host's row");
    g_fake.role = HF_SESSION_JOINER;
    CHECK(co_peers_host_row() == CO_HOST_ROW_NONE && !co_peers_is_host());
    g_fake.row[0] = CO_SHARE_PROTOCOL; g_fake.row[1] = 1; g_fake.rowBytes = 2; CHECK(co_peers_host_row() == CO_HOST_ROW_ON);
    g_fake.row[1] = 0; CHECK(co_peers_host_row() == CO_HOST_ROW_OFF);
    g_fake.row[0] = 2; g_fake.row[1] = 1; CHECK(co_peers_host_row() == CO_HOST_ROW_NONE); /* a protocol this build does not read */
    g_fake.row[0] = CO_SHARE_PROTOCOL; g_fake.rowBytes = 3; CHECK(co_peers_host_row() == CO_HOST_ROW_NONE);
    g_fake.role = HF_SESSION_SOLO; CHECK(co_peers_is_host() && co_peers_host_row() == CO_HOST_ROW_OFF); /* its own part is off here */

    if (g_failed) { printf("mark: %d checks failed\n", g_failed); return 1; }
    printf("mark: all cases passed\n");
    return 0;
}
