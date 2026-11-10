/* The engine's peer-count readers against a stand-in image: four bits only when all five sites are the reviewed ones.
 * A build for eight also lets the lobby search list lobbies of up to seven members, on its own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_engine_test_fill(uint8_t* image);
int co_engine_test_state(void);
int co_engine_test_lobby(void);
int co_counts_test_players(void);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static const uint32_t kValues[] = {0x29186Fu, 0x2918A4u, 0x291736u, 0x294AC6u, 0x294AFCu};
#define LOBBY_SEARCH 0x8DE9CBu      /* cmp dword [r14-4],4: the member count a search result is skipped at */

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_engine_test_fill(g_fake.image);
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
}

int main(void)
{
    const int eight = co_counts_test_players() == 8;
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;

    Start("the reviewed engine");
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == 1 && g_fake.writes == 5 + eight);
    for (int i = 0; i < 5; i++) { CHECK(g_before[kValues[i]] == 3 && g_fake.image[kValues[i]] == 4); g_fake.image[kValues[i]] = 3; }
    CHECK(co_engine_test_lobby() == (eight ? 1 : -1) && g_fake.image[LOBBY_SEARCH + 4] == (eight ? 8 : 4));
    g_fake.image[LOBBY_SEARCH + 4] = 4;
    CHECK(memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
    CHECK(fake_host_logged("engine: the peer list message counts up to 15 peers (5 code sites changed)"));
    CHECK(fake_host_logged("engine: the lobby search lists lobbies with up to 7 members") == eight);

    Start("one changed site");
    g_fake.image[0x294AFDu] ^= 1;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == -1 && g_fake.writes == eight);
    g_fake.image[LOBBY_SEARCH + 4] = 4;
    CHECK(memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
    CHECK(fake_host_logged("helldivers2.exe+0x294AFA is not the reviewed one"));

    for (int at = 0; at < 5; at++)
    {
        Start("a write fails");
        g_fake.failWriteAt = at;
        co_engine_install((uintptr_t)g_fake.image);
        CHECK(co_engine_test_state() == -1 && co_engine_test_lobby() == (eight ? 1 : -1));
        g_fake.image[LOBBY_SEARCH + 4] = 4;
        CHECK(memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
        CHECK(fake_host_logged("could not be written; the peer list message stays as it is"));
    }

    /* The lobby search's site is its own part: a change there leaves the readers on. */
    Start("a changed lobby search site");
    g_fake.image[LOBBY_SEARCH + 2] ^= 1;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == 1 && co_engine_test_lobby() == -1 && g_fake.writes == 5 && g_fake.image[LOBBY_SEARCH + 4] == 4);
    CHECK(fake_host_logged("helldivers2.exe+0x8DE9CB is not the reviewed one; the lobby search skips lobbies of four as before") == eight);

    Start("the lobby search's write fails");
    g_fake.failWriteAt = 5;
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == 1 && co_engine_test_lobby() == -1 && g_fake.image[LOBBY_SEARCH + 4] == 4);
    CHECK(fake_host_logged("helldivers2.exe+0x8DE9CB could not be written; the lobby search skips lobbies of four as before") == eight);

    Start("no engine module");
    co_engine_install(0);
    CHECK(co_engine_test_state() == -1 && co_engine_test_lobby() == -1 && g_fake.writes == 0);

    printf("%s\n", g_failed ? "engine: failures" : "engine: all cases passed");
    return g_failed != 0;
}
