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
int CoXlTestLwOwnDetectors(uintptr_t game, const uint32_t* units, int n, void (*use)(uintptr_t, uint32_t, uint32_t));

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

/* Free room in the fake image for the behaviour and detector managers ([game+0x3326740], [game+0x3326548]). */
#define BEH 0x3800000u
#define DET 0x3810000u
#define NONE_ID 0xFFFu
static struct { int calls; uintptr_t beh; uint32_t unit, detector; } g_use;
static void UseDetector(uintptr_t beh, uint32_t unit, uint32_t detector) { g_use.calls++; g_use.beh = beh; g_use.unit = unit; g_use.detector = detector; }

/* A game u32 map {buckets, capacity 16, empty key, multiplier 1} at `header`, its buckets at `buckets`. */
static void FakeMap(uint8_t* image, uint32_t header, uint32_t buckets, const uint32_t (*pairs)[2], int n)
{
    uint64_t at = (uint64_t)(uintptr_t)(image + buckets); uint32_t cap = 16, empty = 0x7FFFFFFFu, mul = 1;
    memcpy(image + header, &at, 8); memcpy(image + header + 8, &cap, 4); memcpy(image + header + 12, &empty, 4); memcpy(image + header + 16, &mul, 4);
    for (uint32_t i = 0; i < cap; i++) memcpy(image + buckets + 8 * i, &empty, 4);
    for (int i = 0; i < n; i++) memcpy(image + buckets + 8 * (pairs[i][0] % cap), pairs[i], 8);
}

/* Loader members that look through another detector go back to their own, and only those (LwOwnDetectors). */
static void OwnDetectors(void)
{
    uint8_t* image = Reset("loader members see through their own detectors");
    enum { S = 0x40, A = 0x21, B = 0x22, C = 0x23, D = 0x24, E = 0x25, F = 0x26 };
    const uint32_t beh[][2] = {{A, 0}, {B, 1}, {C, 2}, {D, 3}, {E, 4}}, det[][2] = {{A, 0}, {B, 1}, {D, 2}, {E, 5}, {S, 6}};
    const uint32_t looks[] = {S, B, S, NONE_ID, S}; /* Behavior +0x68 of A..E */
    uint64_t p = (uint64_t)(uintptr_t)(image + BEH), q = (uint64_t)(uintptr_t)(image + DET), recs = (uint64_t)(uintptr_t)(image + BEH + 0x2000);
    uint32_t none = NONE_ID, owned = 3;
    memcpy(image + 0x3326740u, &p, 8); memcpy(image + 0x3326548u, &q, 8); memcpy(image + 0x3483C4Cu, &none, 4);
    FakeMap(image, BEH + 0x40, BEH + 0x1000, beh, 5); memcpy(image + BEH + 0x60, &recs, 8);
    for (int i = 0; i < 5; i++) memcpy(image + BEH + 0x2000 + 0x1F8 * i + 0x68, &looks[i], 4);
    FakeMap(image, DET + 0x30, DET + 0x1000, det, 5); memcpy(image + DET + 0x20, &owned, 4);
    const uint32_t members[] = {A, B, C, D, E, F};
    memset(&g_use, 0, sizeof g_use);
    CHECK(CoXlTestLwOwnDetectors((uintptr_t)image, members, 6, UseDetector) == 1);
    /* A only: B has its own already, C has none, D looks through none, E's own is not the host's, F has no behaviour */
    CHECK(g_use.calls == 1 && g_use.beh == (uintptr_t)(image + BEH) && g_use.unit == A && g_use.detector == A);
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

    OwnDetectors();

    if (g_failed) { printf("livingworld: %d checks failed\n", g_failed); return 1; }
    printf("livingworld: all cases passed\n");
    return 0;
}
