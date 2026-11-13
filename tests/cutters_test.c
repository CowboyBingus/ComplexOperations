/* The navmesh cutter destroy (co_cutters.h) on a fake exe: a batch whose handles include 0 reaches the engine without
 * them, in order and in batches of at most 64; a batch with none passes as it is. The engine's stand-in reads the pool
 * of the batch's first handle as 0x8127A0 does (crash exe+0x812807 when that pool is empty). Also the pins, the pool's
 * reader and the failed makes' count. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_cutters.h"
#include "xl/co_shim.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The pool of world 2 as 0x7D7F60 lays it out: room at +0x24, in use at +0x20. */
static uint8_t g_pool[0x40];
static uint8_t* g_exe;

/* The engine's destroy as far as the crash: the first handle's world picks the pool for the whole batch. */
static uint32_t g_calls, g_seen[256], g_seenCount, g_wouldCrash, g_biggest;
static void EngineDestroy(const uint32_t* handles, uint32_t count)
{
    g_calls++;
    if (count > g_biggest) g_biggest = count;
    uint64_t pool = 0;
    memcpy(&pool, g_exe + CO_CUTTERS_POOLS + 8u * (handles[0] >> 30), 8);
    if (pool == 0) g_wouldCrash++; /* the read of [0 + 0x28] */
    for (uint32_t i = 0; i < count && g_seenCount < 256; i++) g_seen[g_seenCount++] = handles[i];
}

static uint32_t g_makeAnswer, g_makes;
static uint32_t EngineMake(const void* description) { g_makes++; return g_makeAnswer; }

static void Hex(uint8_t* at, const char* hex)
{
    for (; *hex; hex++)
    {
        unsigned v = 0;
        if (*hex == ' ') continue;
        sscanf(hex, "%2x", &v);
        *at++ = (uint8_t)v;
        hex++;
    }
}

static void Pins(uint8_t* exe)
{
    Hex(exe + CO_CUTTERS_DESTROY, "85 D2 0F 84 32 01 00 00 41 54 41 55 48 83 EC 38 48 89 5C 24 50 45 33 C0");
    Hex(exe + 0x8127F6u, "49 8B 9C C3 30 63 7C 02 66 90 41 8B 74 BD 00 8B CE 23 4B 28 39 4B 24");
    Hex(exe + CO_CUTTERS_MAKE, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57");
    Hex(exe + 0x812DBAu, "83 F8 FF 0F 85 3F 03 00 00 48 8B CF E8 45 9F 18 00 8B 85 10 08 00 00");
    Hex(exe + 0x7D7F60u, "41 89 48 24");
    Hex(exe + 0x7D8090u, "4F 89 84 EC 30 63 7C 02");
    Hex(exe + 0x75331u, "F0 41 FF 41 20");
    Hex(exe + 0x754A6u, "F0 FF 49 20");
}

static void Pool(uint32_t used, uint32_t room)
{
    uint64_t at = (uint64_t)(uintptr_t)g_pool;
    memcpy(g_exe + CO_CUTTERS_POOLS + 16u, &at, 8); /* world 2; world 0's slot stays 0 as in a mission */
    memcpy(g_pool + CO_CUTTERS_USED, &used, 4);
    memcpy(g_pool + CO_CUTTERS_ROOM, &room, 4);
}

static void Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_cutters_test_reset();
    g_exe = fake_host_exe();
    g_fake.strictCode = 1;
    Pins(g_exe);
    memset(g_pool, 0, sizeof g_pool);
    Pool(100, 65536);
    g_fake.originals[0].target = (uintptr_t)g_exe + CO_CUTTERS_DESTROY;
    g_fake.originals[0].original = (void*)EngineDestroy;
    g_fake.originals[1].target = (uintptr_t)g_exe + CO_CUTTERS_MAKE;
    g_fake.originals[1].original = (void*)EngineMake;
    g_calls = g_seenCount = g_wouldCrash = g_biggest = 0;
    g_makes = 0; g_makeAnswer = 0;
}

typedef void (*DestroyFn)(const uint32_t*, uint32_t);
typedef uint32_t (*MakeFn)(const void*);
static DestroyFn Detour(void) { return (DestroyFn)fake_host_detour((uintptr_t)g_exe + CO_CUTTERS_DESTROY - (uintptr_t)g_fake.image); }
static MakeFn MakeDetour(void) { return (MakeFn)fake_host_detour((uintptr_t)g_exe + CO_CUTTERS_MAKE - (uintptr_t)g_fake.image); }

int main(void)
{
    uint32_t kept[8];
    g_case = "keep";
    {
        const uint32_t in[6] = {0, 0x81018958u, 0, 0, 0x81018957u, 0x81010001u};
        CHECK(co_cutters_keep(in, 6, kept) == 3);
        CHECK(kept[0] == 0x81018958u && kept[1] == 0x81018957u && kept[2] == 0x81010001u);
        const uint32_t none[3] = {0, 0, 0};
        CHECK(co_cutters_keep(none, 3, kept) == 0);
        CHECK(co_cutters_keep(in + 1, 1, kept) == 1 && kept[0] == 0x81018958u);
    }

    Reset("the crash's batch: three handles of 0");
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    DestroyFn destroy = Detour();
    CHECK(destroy != NULL);
    if (destroy != NULL)
    {
        const uint32_t batch[3] = {0, 0, 0};
        destroy(batch, 3);
        CHECK(g_calls == 0 && g_wouldCrash == 0);
        CoCuttersSeen seen;
        co_cutters_seen(&seen);
        CHECK(seen.dropped == 3);
    }

    Reset("a 0 first, then handles of world 2");
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    destroy = Detour();
    if (destroy != NULL)
    {
        const uint32_t batch[4] = {0, 0x81018958u, 0, 0x81018957u};
        destroy(batch, 4);
        CHECK(g_calls == 1 && g_wouldCrash == 0);
        CHECK(g_seenCount == 2 && g_seen[0] == 0x81018958u && g_seen[1] == 0x81018957u);
        CoCuttersSeen seen;
        co_cutters_seen(&seen);
        CHECK(seen.dropped == 2);
    }

    Reset("no 0: the game's batch as it is");
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    destroy = Detour();
    if (destroy != NULL)
    {
        const uint32_t batch[2] = {0x81018958u, 0x81018957u};
        destroy(batch, 2);
        CHECK(g_calls == 1 && g_seenCount == 2 && g_seen[0] == 0x81018958u && g_seen[1] == 0x81018957u);
        CoCuttersSeen seen;
        co_cutters_seen(&seen);
        CHECK(seen.dropped == 0);
    }

    Reset("a long batch with a 0: every other handle, in order, at most 64 a call");
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    destroy = Detour();
    if (destroy != NULL)
    {
        uint32_t batch[150];
        for (uint32_t i = 0; i < 150; i++) batch[i] = i % 50 == 0 ? 0 : 0x81010000u + i;
        destroy(batch, 150);
        CHECK(g_wouldCrash == 0 && g_biggest <= CO_CUTTERS_BATCH && g_seenCount == 147);
        uint32_t k = 0, ordered = 1;
        for (uint32_t i = 0; i < 150; i++)
            if (batch[i] != 0) ordered &= k < g_seenCount && g_seen[k++] == batch[i];
        CHECK(ordered);
    }

    Reset("a changed byte at the pool read: nothing hooked");
    g_exe[0x812807u] ^= 0xFF;
    CHECK(co_cutters_install((uintptr_t)g_exe) != NULL);
    CHECK(Detour() == NULL && MakeDetour() == NULL);

    Reset("a changed byte at the make's full branch: nothing hooked");
    g_exe[0x812DBCu] ^= 0xFF;
    CHECK(co_cutters_install((uintptr_t)g_exe) != NULL);
    CHECK(Detour() == NULL);

    Reset("the destroy's hook refused");
    g_fake.noHook = (uintptr_t)g_exe + CO_CUTTERS_DESTROY - (uintptr_t)g_fake.image;
    CHECK(co_cutters_install((uintptr_t)g_exe) != NULL);
    CHECK(MakeDetour() == NULL);

    Reset("the pool");
    {
        uint32_t used = 0, room = 0;
        CHECK(co_cutters_pool((uintptr_t)g_exe, 2, &used, &room) == 1 && used == 100 && room == 65536);
        CHECK(co_cutters_pool((uintptr_t)g_exe, 0, &used, &room) == 0); /* no world 0 */
        CHECK(co_cutters_pool((uintptr_t)g_exe, 4, &used, &room) == 0);
        Pool(65537, 65536);
        CHECK(co_cutters_pool((uintptr_t)g_exe, 2, &used, &room) == 0);
        Pool(0, (1u << 21) + 1);
        CHECK(co_cutters_pool((uintptr_t)g_exe, 2, &used, &room) == 0);
        Pool(52428, 65536);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.80f) == 0);
        Pool(52429, 65536);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.80f) == 1);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.70f) == 1);
        Pool(32768, 65536); /* at the share: held */
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.50f) == 1);
        Pool(45874, 65536);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.70f) == 0);
        Pool(838860, 1u << 20); /* the mod's early room */
        CHECK(co_cutters_pool((uintptr_t)g_exe, 2, &used, &room) == 1 && used == 838860 && room == (1u << 20));
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.80f) == 0);
        Pool(838861, 1u << 20);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.80f) == 1);
        memset(g_exe + CO_CUTTERS_POOLS + 16u, 0, 8);
        CHECK(co_cutters_over((uintptr_t)g_exe, 0.0f) == 0); /* not read: no hold */
    }

    Reset("a make that fails is counted with the pool it failed in");
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    MakeFn make = MakeDetour();
    CHECK(make != NULL);
    if (make != NULL)
    {
        uint32_t description[16] = {3, 2};
        Pool(65536, 65536);
        g_makeAnswer = 0x81010005u;
        CHECK(make(description) == 0x81010005u);
        CoCuttersSeen seen;
        co_cutters_seen(&seen);
        CHECK(seen.failed == 0);
        g_makeAnswer = 0;
        CHECK(make(description) == 0 && make(description) == 0);
        Pool(1, 65536); /* the first failure's pool is kept */
        CHECK(make(description) == 0);
        co_cutters_seen(&seen);
        CHECK(seen.failed == 3 && seen.usedAtFail == 65536 && seen.roomAtFail == 65536 && g_makes == 4);
    }

    if (g_failed) { printf("cutters: %d check(s) failed\n", g_failed); return 1; }
    printf("cutters: all checks passed\n");
    return 0;
}
