/* The engine's peer-count readers against a stand-in image: four bits only when all five sites are the reviewed ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_engine_test_fill(uint8_t* image);
int co_engine_test_state(void);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static const uint32_t kValues[] = {0x29186Fu, 0x2918A4u, 0x291736u, 0x294AC6u, 0x294AFCu};

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
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;

    Start("the reviewed engine");
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == 1 && g_fake.writes == 5);
    for (int i = 0; i < 5; i++) { CHECK(g_before[kValues[i]] == 3 && g_fake.image[kValues[i]] == 4); g_fake.image[kValues[i]] = 3; }
    CHECK(memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
    CHECK(fake_host_logged("engine: the peer list message counts up to 15 peers (5 code sites changed)"));

    Start("one changed site");
    g_fake.image[0x294AFDu] ^= 1;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    co_engine_install((uintptr_t)g_fake.image);
    CHECK(co_engine_test_state() == -1 && g_fake.writes == 0 && memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
    CHECK(fake_host_logged("helldivers2.exe+0x294AFA is not the reviewed one"));

    for (int at = 0; at < 5; at++)
    {
        Start("a write fails");
        g_fake.failWriteAt = at;
        co_engine_install((uintptr_t)g_fake.image);
        CHECK(co_engine_test_state() == -1 && memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
        CHECK(fake_host_logged("could not be written; the peer list message stays as it is"));
    }

    Start("no engine module");
    co_engine_install(0);
    CHECK(co_engine_test_state() == -1 && g_fake.writes == 0);

    printf("%s\n", g_failed ? "engine: failures" : "engine: all cases passed");
    return g_failed != 0;
}
