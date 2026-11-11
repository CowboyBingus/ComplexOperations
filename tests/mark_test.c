/* The mark hook on a synthetic frame: the game's fill runs first with every argument as it came, then a level-11
 * entry of the war object's table gets the decision in its seed, from the planet of the board's record at its slot. */
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
static void Planet(uint32_t slot, uint16_t planet) { memcpy(g_fake.image + WAR + 0xF7280u + slot * 0x5Cu + 0x10u, &planet, 2); }

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
static uint32_t Operation(uint32_t planet, int complex)
{
    for (uint32_t seed = 1;; seed++) if (co_complex_roll(seed, planet, CO_COMPLEX_CHANCE) == complex) return seed;
}

int main(void)
{
    const uintptr_t game = (uintptr_t)(Start("the entry"), g_fake.image);
    const uint32_t yes = Operation(77, 1), no = Operation(77, 0);
    Planet(5, 77);
    /* a host that rolls: bit 0 is the decision either way, the rest of the seed is the game's */
    SetSeed(3, 0x12345678u); co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(3), 1);
    CHECK(Seed(3) == 0x12345679u && co_seed_is_complex(Seed(3)));
    SetSeed(3, 0x12345679u); co_mark_entry(game, 5, 11, no, (uintptr_t)Entry(3), 1);
    CHECK(Seed(3) == 0x12345678u);
    /* a machine that does not roll clears the mark */
    SetSeed(3, 0x12345679u); co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(3), 0);
    CHECK(Seed(3) == 0x12345678u);
    /* the planet is the slot's: another slot's planet gives that planet's roll */
    Planet(6, 78);
    SetSeed(4, 0x1000u); co_mark_entry(game, 6, 11, yes, (uintptr_t)Entry(4), 1);
    CHECK((Seed(4) & 1u) == (uint32_t)co_complex_roll(yes, 78, CO_COMPLEX_CHANCE) && (Seed(4) & ~1u) == 0x1000u);
    /* other levels, a slot the board does not have, an entry that is not the table's: untouched */
    SetSeed(3, 0xABCDEF01u);
    co_mark_entry(game, 5, 10, yes, (uintptr_t)Entry(3), 1); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 5, 0, yes, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 110, 11, yes, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(3) + 4, 0); CHECK(Seed(3) == 0xABCDEF01u);
    co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(0x14A), 0);
    co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(0) - 0x4C, 0);
    SetSeed(0, 0x11u); SetSeed(0x149, 0x21u);
    co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(0), 0); co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(0x149), 0);
    CHECK(Seed(0) == 0x10u && Seed(0x149) == 0x20u); /* the first and the last entry are the table's */
    memset(g_fake.image + CO_WAR_OBJECT, 0, 8);
    co_mark_entry(game, 5, 11, yes, (uintptr_t)Entry(3), 0); CHECK(Seed(3) == 0xABCDEF01u); /* no war object yet */

    Start("the install");
    CHECK(co_mark_install(game) == NULL && fake_host_detour(CO_MARK_FILL) != NULL && g_fake.writes == 0);
    CHECK(co_mark_install(game) == NULL && g_fake.hookCount == 1);
    /* the detour, as the game calls it: 14 arguments, the 32-bit ones with other bits above; solo, part off: no roll */
    Planet(5, 77);
    SetSeed(3, 0x7001u);
    const FillFn detour = (FillFn)fake_host_detour(CO_MARK_FILL);
    CHECK(detour(1, 2, 3, 4, 0xAAAAAAAA00000005ull, 6, 0xBBBBBBBB0000000Bull, 8, 0xCCCCCCCC00000000ull | yes, (uint64_t)(uintptr_t)Entry(3), 11, 12, 13, 14) == 0);
    CHECK(Seed(3) == 0x7000u);
    SetSeed(3, 0x7001u);
    detour(1, 2, 3, 4, 5, 6, 10, 8, yes, (uint64_t)(uintptr_t)Entry(3), 11, 12, 13, 14);
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
