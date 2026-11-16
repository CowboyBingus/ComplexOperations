/* A tile built in mission: op 0x41's last call (game 0xF52CC0, 0xF52F7A..0xF52FCC) destroys every unit its setup
 * spawned in one frame. The mod destroys them first, a slice a frame (proto/difficulty_world_terrain.c
 * NearMatUnitsSlice): the same call ([[api+8]+0x158] with the world [ctx+0x40]) on the same units in the same order, and
 * the list's count 0 at the end as the game's loop leaves it, so the game's call only ends the op. Nothing is destroyed
 * while the op's job calls are still to come (counter [pass+0x13C] <= T x T, T = [[pass]+0x124]). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
uint32_t CoXlTestMatUnits(uint8_t* record, uint64_t context, uint32_t* units, uint32_t* slices);
void CoXlTestMatUnitsReset(void);
void CoXlTestMatUnitsNote(double setup);
int CoXlTestNearMatLeave(int s, uint64_t context, int arenaDue);
uint8_t* CoXlTestNearMat41(int s, const uint8_t* record, uint8_t phase);
void CoXlTestMatOff(void);
int CoXlTestNearRunStep(int s, uint64_t context, void* dispatch);
void CoXlTestMoveBuilding(int on);
void CoXlTestNearJobLevel(uintptr_t level, uint8_t* block);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define UNITS 300u
static uint8_t g_record[0x19B0], g_ctx[0xE8], g_lg[0x200], g_list[0x2008];
static uint64_t g_destroyed[2000], g_worldSeen;
static int g_destroyN;
static void Spin(double us) { LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0); do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1e6 / (double)hz.QuadPart < us); }
static void Destroy(uint64_t world, uint64_t unit) { g_worldSeen = world; if (g_destroyN < 2000) g_destroyed[g_destroyN] = unit; g_destroyN++; Spin(50.0); }

static void Setup(uint32_t t, uint32_t counter)
{
    memset(g_record, 0, sizeof g_record); memset(g_list, 0, sizeof g_list);
    uint64_t v = (uint64_t)(uintptr_t)g_lg; memcpy(g_record + 8, &v, 8);
    memcpy(g_lg + 0x124, &t, 4);
    memcpy(g_record + 8 + 0x13C, &counter, 4);
    v = (uint64_t)(uintptr_t)g_list; memcpy(g_record + 8 + 0x140, &v, 8);
    for (uint32_t i = 0; i < UNITS; i++) { uint64_t unit = 0x70000000ull + i * 0x10; memcpy(g_list + i * 8, &unit, 8); }
    const uint32_t n = UNITS; memcpy(g_list + 0x2000, &n, 4);
    g_destroyN = 0; g_worldSeen = 0;
    CoXlTestMatUnitsReset();
}
static uint32_t Count(void) { uint32_t n; memcpy(&n, g_list + 0x2000, 4); return n; }

/* The dispatcher for op 0x41 as 0xC165F0 / 0xF52CC0 run it: phase 0 the setup (phase 1 after); then a job call per
 * counter value up to T x T; past it the teardown, every unit still listed destroyed, the count 0, the index moved. */
static int g_dispatches, g_gameDestroyed;
static uint64_t Dispatch41(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    (void)context; (void)d; (void)f; (void)g; (void)h;
    g_dispatches++;
    uint8_t* pass = (uint8_t*)(uintptr_t)record + 8;
    if (*(uint8_t*)(uintptr_t)phase == 0) { *(uint8_t*)(uintptr_t)phase = 1; return 0; }
    uint32_t counter, t; memcpy(&counter, pass + 0x13C, 4); memcpy(&t, g_lg + 0x124, 4);
    if (counter <= t * t) { counter++; memcpy(pass + 0x13C, &counter, 4); return 0; }
    uint32_t n = Count();
    g_gameDestroyed += (int)n;
    const uint32_t zero = 0; memcpy(g_list + 0x2000, &zero, 4);
    *(volatile uint32_t*)(uintptr_t)index = 1;
    return 0;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    static uint64_t api[0x30], t8[0x40];
    t8[0x158 / 8] = (uint64_t)(uintptr_t)Destroy;
    api[1] = (uint64_t)(uintptr_t)t8;
    uint64_t apiAt = (uint64_t)(uintptr_t)api; memcpy(g_fake.image + 0x3326308u, &apiAt, 8);
    uint64_t world = 0xAB0; memcpy(g_ctx + 0x40, &world, 8);
    uint32_t units = 0, slices = 0;

    g_case = "the jobs' calls still to come (counter <= T x T): nothing destroyed, the list as it was";
    Setup(1, 1);
    CHECK(CoXlTestMatUnits(g_record, (uint64_t)(uintptr_t)g_ctx, &units, &slices) == 0 && g_destroyN == 0 && Count() == UNITS);
    Setup(2, 4);
    CHECK(CoXlTestMatUnits(g_record, (uint64_t)(uintptr_t)g_ctx, &units, &slices) == 0 && g_destroyN == 0 && Count() == UNITS);

    g_case = "the last call due: every unit destroyed in list order with the world, in 3 ms slices, the count 0 at the end";
    Setup(1, 2);
    {
        uint32_t left = 0; int frames = 0, shrinking = 1, prev = 0;
        while ((left = CoXlTestMatUnits(g_record, (uint64_t)(uintptr_t)g_ctx, &units, &slices)) != 0 && frames < 1000)
        { frames++; if (g_destroyN <= prev) shrinking = 0; prev = g_destroyN; CHECK(left == UNITS); }
        CHECK(left == 0 && shrinking && g_destroyN == (int)UNITS && units == UNITS && slices >= 4 && slices == (uint32_t)frames + 1);
        int order = 1;
        for (uint32_t i = 0; i < UNITS; i++) if (g_destroyed[i] != 0x70000000ull + i * 0x10) order = 0;
        CHECK(order && g_worldSeen == 0xAB0 && Count() == 0);
        g_destroyN = 0;
        CHECK(CoXlTestMatUnits(g_record, (uint64_t)(uintptr_t)g_ctx, &units, &slices) == 0 && g_destroyN == 0); /* nothing twice */
    }

    g_case = "the step's note";
    g_fake.log[0] = 0;
    CoXlTestMatUnitsNote(12.5);
    CHECK(fake_host_logged("op 0x41 on the game thread: the setup 12.5 ms"));

    g_case = "the arena emptied on the material leave's first frame, once, and not when none is due";
    {
        static const uint8_t reset[] = {0xFF, 0x01, 0xC3}; /* inc dword [rcx]; ret: the arena reset, counted */
        memcpy(g_fake.image + 0x173BAB0u, reset, sizeof reset);
        DWORD old; VirtualProtect(g_fake.image + 0x173BAB0u, sizeof reset, PAGE_EXECUTE_READWRITE, &old);
        static uint8_t level[0x400], gen[0x10];
        uint64_t v = (uint64_t)(uintptr_t)gen; memcpy(level + 0x2C8, &v, 8);
        CoXlTestNearJobLevel((uintptr_t)level, NULL);
        static uint32_t arena;
        uint64_t a = (uint64_t)(uintptr_t)&arena; memcpy(g_ctx, &a, 8);
        arena = 0;
        CHECK(CoXlTestNearMatLeave(3, (uint64_t)(uintptr_t)g_ctx, 1) == 1 && arena == 1);
        CHECK(CoXlTestNearMatLeave(3, (uint64_t)(uintptr_t)g_ctx, 0) == 1 && arena == 1);
    }

    g_case = "the step (NearRunStepIn): the jobs' calls the game's; then only slices, no game call, until the list is done; "
             "the game's last call in that frame destroys nothing; the arena on the leave's frame, not the last call's";
    {
        static uint32_t arena;
        uint64_t a = (uint64_t)(uintptr_t)&arena; memcpy(g_ctx, &a, 8); /* the context's arena: the reset counts in it */
        CoXlTestMoveBuilding(1);
        Setup(1, 0);
        uint8_t* one = CoXlTestNearMat41(4, g_record, 1);
        memcpy(g_list + 0x2000, &(uint32_t){UNITS}, 4);
        g_dispatches = 0; g_gameDestroyed = 0; arena = 0;
        const uint64_t ctx = (uint64_t)(uintptr_t)g_ctx;
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 1 && g_destroyN == 0); /* the tile job */
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 2 && g_destroyN == 0); /* the final job */
        uint32_t counter; memcpy(&counter, one + 8 + 0x13C, 4); CHECK(counter == 2);
        int frames = 0, r = 0;
        while ((r = CoXlTestNearRunStep(4, ctx, (void*)Dispatch41)) == 0 && g_dispatches == 2 && frames < 1000) frames++;
        CHECK(frames >= 3 && r == 0 && g_dispatches == 3 && g_destroyN == (int)UNITS && g_gameDestroyed == 0 && Count() == 0);
        CHECK(arena == 0); /* the last call's frame: no reset */
        CoXlTestMatOff();
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 1 && arena == 1 && g_dispatches == 3);

        g_case = "the setup's frame (phase 0): no slice even with the counter past T x T";
        Setup(1, 2);
        CoXlTestNearMat41(4, g_record, 0);
        g_dispatches = 0;
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 1 && g_destroyN == 0 && Count() == UNITS);

        g_case = "not in play (no build moving): the game's own teardown, whole, in one call";
        CoXlTestMoveBuilding(0);
        Setup(1, 2);
        CoXlTestNearMat41(4, g_record, 1);
        g_dispatches = 0; g_gameDestroyed = 0;
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 1 && g_destroyN == 0 && g_gameDestroyed == (int)UNITS);
        CoXlTestMatOff();
    }

    if (g_failed) { printf("mat41: %d check(s) failed\n", g_failed); return 1; }
    printf("mat41: all checks passed\n");
    return 0;
}
