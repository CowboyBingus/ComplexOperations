/* The loader's player list reader over the mod's own layout: the game's offsets and four players while the managers
 * are where the game has them, the moved offsets and eight once a build for eight has moved them. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "xl/co_shim.h"

void co_managers_test_force(int state);
void co_managers_test_reset(void);
int co_counts_test_players(void);

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define MANAGER 0x3500000u      /* free places in the image */
#define USER 0x3510000u
#define LOCAL_PEER 0x347CEF0u

/* A player manager with `count` players, their peers and lifecycles at the given offsets; player 2 is this machine's. */
static uintptr_t Start(const char* name, uint32_t count, uint32_t peers, uint32_t lives)
{
    uint8_t* image;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    image = g_fake.image;
    *(uint64_t*)(image + CO_SHIM_PLAYER_MANAGER) = (uint64_t)(uintptr_t)(image + MANAGER);
    *(uint64_t*)(image + LOCAL_PEER) = (uint64_t)(uintptr_t)(image + USER);
    *(uint64_t*)(image + USER + 0xB398u) = 0x1100000000000002ull;
    *(uint32_t*)(image + MANAGER + 0x84u) = count;
    for (uint32_t i = 0; i < 8; i++)
    {
        *(uint64_t*)(image + MANAGER + peers + 0x38u * i) = 0x1100000000000000ull + i;
        *(uint32_t*)(image + MANAGER + lives + 0x38u * i) = 10u + i;
        *(uint32_t*)(image + MANAGER + 0x3A8u + 0x20u * i) = 0x7FFFu;
    }
    return (uintptr_t)image;
}

int main(void)
{
    const int eight = co_counts_test_players() == 8;
    EvpPlayer players[CO_SHIM_MAX_PLAYERS];
    uintptr_t game;

    co_managers_test_reset();
    CHECK(co_managers_slots(CO_SHIM_PLAYER_MANAGER) == 4u && co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0x2C8u) == 0x2C8u);
    game = Start("the game's layout", 3, 0x2C8u, 0x2E0u);
    CHECK(co_evp_players(game, players) == 3);
    CHECK(players[0].peer == 0x1100000000000000ull && players[2].peer == 0x1100000000000002ull && players[1].life == 11u);
    CHECK(players[2].local == 1 && players[0].local == 0 && players[1].avatar == 0);
    game = Start("more players than the game's four slots", 5, 0x2C8u, 0x2E0u);
    CHECK(co_evp_players(game, players) == -1);
    game = Start("no manager", 3, 0x2C8u, 0x2E0u);
    *(uint64_t*)(g_fake.image + CO_SHIM_PLAYER_MANAGER) = 0;
    CHECK(co_evp_players(game, players) == -1);

    /* Moved: only a build for eight moves the player manager; a build for four reads the game's layout as before. */
    co_managers_test_force(1);
    CHECK(co_managers_slots(CO_SHIM_PLAYER_MANAGER) == (eight ? 8u : 4u));
    CHECK(co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0x2C8u) == (eight ? 0x828u : 0x2C8u));
    CHECK(co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0x2E0u) == (eight ? 0x840u : 0x2E0u));
    CHECK(co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0x84u) == 0x84u && co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0xE8u) == 0xE8u);
    CHECK(co_managers_offset(CO_SHIM_PLAYER_MANAGER, 0x3A8u) == 0x3A8u);
    game = Start("the moved layout", 7, eight ? 0x828u : 0x2C8u, eight ? 0x840u : 0x2E0u);
    CHECK(co_evp_players(game, players) == (eight ? 7 : -1));
    if (eight) CHECK(players[6].peer == 0x1100000000000006ull && players[6].life == 16u && players[2].local == 1);
    game = Start("more players than eight", 9, eight ? 0x828u : 0x2C8u, eight ? 0x840u : 0x2E0u);
    CHECK(co_evp_players(game, players) == -1);

    /* Back where the game has them: the reader follows. */
    co_managers_test_force(0);
    game = Start("the game's layout again", 4, 0x2C8u, 0x2E0u);
    CHECK(co_evp_players(game, players) == 4 && players[3].peer == 0x1100000000000003ull);

    g_case = "the hash";
    CHECK(co_murmur64a("", 0) == 0);
    CHECK(co_murmur64a("content/fac_bugs", 16) == co_murmur64a("content/fac_bugs", 16) && co_murmur64a("a", 1) != co_murmur64a("b", 1));
    co_managers_test_reset();

    printf("%s\n", g_failed ? "shim: failures" : "shim: all cases passed");
    return g_failed != 0;
}
