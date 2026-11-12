/* The living world in a complex operation: on without a switch, and acting only while the mission in play is complex.
 * A normal mission, the ship and a war-table preview see none of it. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_patrols.h"

void CoXlTestLive(int complex);
int CoXlTestLiving(uintptr_t game);
void CoXlTestLwPins(uint8_t* image);
LONG CoXlTestNoiseHead(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NOISE 0x64A890u
#define DEFER_SITE 0xFDAA8Au

typedef void (*NoiseFn)(void*, uint8_t, uint32_t, const float*, uint32_t, float, uint32_t);

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    memcpy(g_fake.image + CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp); /* the director's patrol gate */
    memcpy(g_fake.image + CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate);
    memcpy(g_fake.image + CO_PATROL_NEXT, kCoPatrolNext, sizeof kCoPatrolNext);
    memcpy(g_fake.image + CO_PATROL_SKIP, kCoPatrolSkip, sizeof kCoPatrolSkip);
    g_fake.writes = 0;
    return g_fake.image;
}

int main(void)
{
    uint8_t* image = Reset("the living world's installs");
    uint8_t defer[44];
    CoXlTestLwPins(image);
    memcpy(defer, image + DEFER_SITE, sizeof defer);
    CHECK(CoXlTestLiving((uintptr_t)image) == 1); /* on with no environment variable: the game is the reviewed build */
    /* no density guards in this game: the setup pass is the game's, not rerouted */
    CHECK(memcmp(defer, image + DEFER_SITE, sizeof defer) == 0 && fake_host_logged("no density guards"));

    NoiseFn noise = (NoiseFn)fake_host_detour(NOISE);
    CHECK(noise != NULL);
    if (noise != NULL)
    {
        const float at[3] = {10.0f, 20.0f, 0.0f};
        const LONG head = CoXlTestNoiseHead();
        CoXlTestLive(0); /* the ship or a normal mission: no noise is kept */
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head);
        CoXlTestLive(1);
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head + 1);
        CoXlTestLive(0);
        noise(NULL, 5, 77, at, 0, 1.0f, 0);
        CHECK(CoXlTestNoiseHead() == head + 1);
    }

    /* the director's patrols: the gate's stub reads the live decision, and the rows stay the game's */
    image = g_fake.image;
    CHECK(image[CO_PATROL_GATE] == 0xE9);
    if (image[CO_PATROL_GATE] == 0xE9)
    {
        int32_t rel; memcpy(&rel, image + CO_PATROL_GATE + 1, 4);
        const uint8_t* flag = image + CO_PATROL_GATE + 5 + rel + 0x1000;
        CoXlTestLive(1);
        CHECK(*flag == 1);
        CoXlTestLive(0);
        CHECK(*flag == 0);
    }

    if (g_failed) { printf("livingworld: %d checks failed\n", g_failed); return 1; }
    printf("livingworld: all cases passed\n");
    return 0;
}
