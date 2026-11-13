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
int CoXlTestLwSettle(uint32_t i, uint64_t peer, const float* at, uint64_t now, float* use);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NOISE 0x64A890u
#define DEFER_SITE 0xFDAA8Au
#define SPAWN 0x953580u

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
    /* HellForge's spawn guard has hooked the spawn request: still the reviewed code under the jump */
    g_fake.underHook[0].rva = SPAWN; memcpy(g_fake.underHook[0].bytes, image + SPAWN, 16);
    memcpy(image + SPAWN, "\xFF\x25\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 14);
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

    g_case = "a passing player is held at its own place, also after the player manager shifted its index";
    {
        float use[2];
        const float a0[2] = {100.0f, 200.0f}, a1[2] = {900.0f, 200.0f}, b0[2] = {5000.0f, 7000.0f}, b1[2] = {5001.0f, 7000.0f};
        CHECK(CoXlTestLwSettle(0, 0xA, a0, 1000, use) == 0 && use[0] == 100.0f);
        CHECK(CoXlTestLwSettle(0, 0xA, a1, 1200, use) == 1 && use[0] == 100.0f && use[1] == 200.0f); /* a pod: held */
        CHECK(CoXlTestLwSettle(0, 0xB, b0, 1400, use) == 0 && use[0] == 5000.0f && use[1] == 7000.0f); /* B now at index 0 */
        CHECK(CoXlTestLwSettle(0, 0xB, b1, 1600, use) == 0 && use[0] == 5001.0f);
        CHECK(CoXlTestLwSettle(1, 0xA, a1, 1600, use) == 0 && use[0] == 900.0f); /* A at index 1: its reading as it is */
    }

    if (g_failed) { printf("livingworld: %d checks failed\n", g_failed); return 1; }
    printf("livingworld: all cases passed\n");
    return 0;
}
