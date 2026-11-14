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
int CoXlTestLwSightVerdict(const uint8_t* hits, uint32_t n, uint32_t target, const float* eye, const float* tpos, float nearR, float folR, int* at);
int CoXlTestLwStaticKind(float z, float drawn, float coarse, int tile);
int CoXlTestLwSenseRule(uint32_t alert, float d, float visibility, uint32_t code, int clear);
int CoXlTestLwHostileRule(uint8_t entity, uint8_t on, uint8_t always, int avatar, uint32_t lists, uint32_t faction, uint32_t mask);
int CoXlTestLwSenseRoute(float awareness);
int CoXlTestLwSenseLost(uint32_t flags, uint32_t state);

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

/* A ray hit as the game's ray writes it: +0 x, +4 y, +0x18 metres, +0x1C unit (0 static), +0x24 type. */
static void Hit(uint8_t* hits, int i, float x, float y, float d, uint32_t unit, uint32_t type)
{
    uint8_t* h = hits + 0x2C * i;
    memcpy(h, &x, 4); memcpy(h + 4, &y, 4); memcpy(h + 0x18, &d, 4); memcpy(h + 0x1C, &unit, 4); memcpy(h + 0x24, &type, 4);
}

/* The sight ray's hits read in order, as 0x64D1D0: the target, foliage near either end, static and other units. */
static void SightVerdict(void)
{
    enum { TARGET = 0x51, OTHER = 0x77 };
    const uint32_t FOL = 0x4B8DC02Bu, ROCK = 0x1234u;
    const float eye[3] = {0, 0, 2}, tpos[3] = {20, 0, 1};
    uint8_t h[8 * 0x2C]; int at = 7;
    g_case = "a sight ray with no hits sees the target";
    CHECK(CoXlTestLwSightVerdict(h, 0, TARGET, eye, tpos, 5, 2, &at) == 1 && at == -1);
    g_case = "the target first: seen, whatever follows";
    memset(h, 0, sizeof h); Hit(h, 0, 19.6f, 0, 19.6f, TARGET, 0); Hit(h, 1, 10, 0, 10, 0, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 1 && at == 0);
    g_case = "a static hit before the target blocks it";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, ROCK); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "another unit blocks";
    memset(h, 0, sizeof h); Hit(h, 0, 12, 0, 12, OTHER, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage halfway blocks";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, FOL); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage within the foliage radius of the target, or exactly on it, is passed";
    memset(h, 0, sizeof h); Hit(h, 0, 18, 0, 18, 0, FOL); Hit(h, 1, 19.6f, 0, 19.6f, TARGET, 0);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 1 && at == 1);
    Hit(h, 0, 17.9f, 0, 17.9f, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "foliage nearer than the foliage radius to the eye is passed, not on it";
    memset(h, 0, sizeof h); Hit(h, 0, 1.9f, 0, 1.9f, 0, FOL); Hit(h, 1, 10, 0, 10, 0, ROCK);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 1);
    Hit(h, 0, 2, 0, 2, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 2, TARGET, eye, tpos, 5, 2, &at) == 0 && at == 0);
    g_case = "with the target within the near radius across, all foliage is passed";
    memset(h, 0, sizeof h); Hit(h, 0, 10, 0, 10, 0, FOL);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 20.5f, 2, &at) == 1 && at == -1);
    CHECK(CoXlTestLwSightVerdict(h, 1, TARGET, eye, tpos, 20, 2, &at) == 0 && at == 0);
    g_case = "a static hit against the ground";
    CHECK(CoXlTestLwStaticKind(5.2f, 5.0f, 9.0f, 0) == 3);     /* the drawn ground */
    CHECK(CoXlTestLwStaticKind(8.9f, 5.0f, 9.0f, 0) == 4);     /* the coarse collision over a live tile */
    CHECK(CoXlTestLwStaticKind(8.9f, 5.0f, 9.0f, -1) == 5);    /* off a tile: an object */
    CHECK(CoXlTestLwStaticKind(4.2f, 5.0f, 4.0f, 0) == 5);     /* under the drawn ground on the coarse rows: not the coarse body over it */
    CHECK(CoXlTestLwStaticKind(7.0f, 5.0f, 9.0f, 0) == 5);     /* an object */
    CHECK(CoXlTestLwStaticKind(7.0f, -9999.0f, 9.0f, 0) == 6); /* no drawn ground */
    g_case = "a calm enemy close beside a player notices them only with a clear line"; /* 0 yes, 1 alert, 2 far, 3 stealth, 4 in view, 5 blocked */
    CHECK(CoXlTestLwSenseRule(0, 14.9f, 1.0f, 0, 1) == 0);
    CHECK(CoXlTestLwSenseRule(0, 15.0f, 1.0f, 0, 1) == 0);  /* the edge counts */
    CHECK(CoXlTestLwSenseRule(0, 15.1f, 1.0f, 0, 1) == 2);  /* too far */
    CHECK(CoXlTestLwSenseRule(1, 10.0f, 1.0f, 0, 1) == 1);  /* already suspicious: the game's own path */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 1.0f, 2, 1) == 4);  /* in view: the game's sight */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 1.0f, 1, 1) == 4);  /* seen close (within 0.3 x the range) */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 1.0f, 0, 0) == 5);  /* blocked: not through walls or units */
    CHECK(CoXlTestLwSenseRule(0, 10.0f, 1.0f, 0, -1) == 5); /* no ray */
    CHECK(CoXlTestLwSenseRule(0, 7.4f, 0.5f, 0, 1) == 0);   /* a hidden player: half the reach */
    CHECK(CoXlTestLwSenseRule(0, 7.6f, 0.5f, 0, 1) == 3);
    CHECK(CoXlTestLwSenseRule(0, 1.0f, 0.0f, 0, 1) == 3);   /* invisible */
    g_case = "the game's hostility test"; /* entity +0x14, +0x13F0, always, avatar, lists, faction, mask */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x2, 0x4) == 1);   /* no shared faction bit */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x6, 0x4) == 0);   /* a shared faction bit: an ally */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 0, 0x2, 0x4) == 0);   /* the list off */
    CHECK(CoXlTestLwHostileRule(0, 1, 0, 1, 1, 0x2, 0x0) == 0);   /* no mask */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 1, 0, 0x6, 0x4) == 1);   /* every avatar an enemy */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 0, 0, 0x6, 0x4) == 0);   /* ... but the target is not an avatar */
    CHECK(CoXlTestLwHostileRule(2, 1, 1, 1, 1, 0x2, 0x4) == 0);   /* the detector entity's bit 1: it scans nothing */
    CHECK(CoXlTestLwHostileRule(1, 1, 0, 1, 1, 0x2, 0x4) == 1);   /* its bit 0 alone */
    CHECK(CoXlTestLwHostileRule(0, 1, 1, 1, 1, 0x0, 0x4) == 0);   /* faction 0 */
    CHECK(CoXlTestLwHostileRule(0, 0, 1, 1, 1, 0x2, 0x4) == 0);   /* +0x13F0 off */
    g_case = "a notice takes the route the game reads"; /* 0 hearing, 1 flag 0x10 */
    CHECK(CoXlTestLwSenseRoute(0.0f) == 0);
    CHECK(CoXlTestLwSenseRoute(0.99f) == 0);
    CHECK(CoXlTestLwSenseRoute(1.0f) == 1);  /* the game's comiss/jb: 1.0 is the eye's case */
    CHECK(CoXlTestLwSenseRoute(3.0f) == 1);
    g_case = "a unit the awareness update skips is not noticed"; /* 0 yes, 10 state 16, 11 flags */
    CHECK(CoXlTestLwSenseLost(0, 0) == 0);
    CHECK(CoXlTestLwSenseLost(0x1, 3) == 0);   /* bit 0 is not one of them */
    CHECK(CoXlTestLwSenseLost(0x10, 3) == 0);  /* nor bit 4 */
    CHECK(CoXlTestLwSenseLost(0x2, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x4, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x8, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0x20, 3) == 11);
    CHECK(CoXlTestLwSenseLost(0, 0x10) == 10);
    CHECK(CoXlTestLwSenseLost(0x2, 0x10) == 10); /* state 16 is tested first (0x7EB136) */
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
    SightVerdict();

    if (g_failed) { printf("livingworld: %d checks failed\n", g_failed); return 1; }
    printf("livingworld: all cases passed\n");
    return 0;
}
