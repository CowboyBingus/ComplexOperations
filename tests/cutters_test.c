/* The navmesh cutter destroy (co_cutters.h) on a fake exe: a batch whose handles include 0 reaches the engine without
 * them, in order and in batches of at most 64; a batch with none passes as it is. The engine's stand-in reads the pool
 * of the batch's first handle as 0x8127A0 does (crash exe+0x812807 when that pool is empty). Also the pins, the pool's
 * reader and the failed makes' count; and the trees' record: armed, a forest make on a complex level is recorded and
 * its description left as it was, a destroy drops it, and nothing else reaches the engine. */
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

static uint32_t g_makeAnswer, g_makes, g_addSeen;
/* While the engine makes the cutter: 1 the AI world goes, 2 the pool goes, 3 another pool takes its place whose
 * mask the set cannot hold, 4 the level ends. */
static int g_makeDropsWorld;
static uint8_t g_poolWide[0x40];
static uint32_t EngineMake(const void* description)
{
    g_makes++; g_addSeen = ((const uint32_t*)description)[3];
    if (g_makeDropsWorld == 1) memset(g_exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD, 0, 8);
    if (g_makeDropsWorld == 2) memset(g_exe + CO_CUTTERS_POOLS + 8u * CO_CUTTERS_WORLD, 0, 8);
    if (g_makeDropsWorld == 3)
    {
        const uint32_t mask = 0x1FFFFF; const uint64_t at = (uint64_t)(uintptr_t)g_poolWide;
        memcpy(g_poolWide + CO_CUTTERS_MASK, &mask, 4); memcpy(g_exe + CO_CUTTERS_POOLS + 8u * CO_CUTTERS_WORLD, &at, 8);
    }
    if (g_makeDropsWorld == 4) co_cutters_live(0);
    return g_makeAnswer;
}

/* The engine's switch: what it was handed, call by call. */
static uint32_t g_swCalls, g_swOn[1024], g_swOnCount, g_swOff[64], g_swOffCount, g_swBiggest;
static void EngineSwitch(const uint32_t* handles, uint32_t count, int on)
{
    g_swCalls++;
    if (count > g_swBiggest) g_swBiggest = count;
    for (uint32_t i = 0; i < count; i++)
    {
        if (on && g_swOnCount < 1024) g_swOn[g_swOnCount++] = handles[i];
        if (!on && g_swOffCount < 64) g_swOff[g_swOffCount++] = handles[i];
    }
}
/* A make and a destroy on a thread of their own, as a generator thread's would be. */
typedef struct OffThread { const void* description; uintptr_t from; void (*destroy)(const uint32_t*, uint32_t); uint32_t handle; } OffThread;
static DWORD WINAPI OffThreadRun(void* arg)
{
    const OffThread* o = arg;
    if (o->description != NULL) co_cutters_test_make(o->description, o->from);
    if (o->destroy != NULL) o->destroy(&o->handle, 1);
    return 0;
}
static void OnOtherThread(OffThread o)
{
    HANDLE t = CreateThread(NULL, 0, OffThreadRun, &o, 0, NULL);
    if (t != NULL) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
}
static void StepTask(uint64_t handle) { memcpy(g_exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD + CO_CUTTERS_STEP_TASK, &handle, 8); }

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

/* The record's pins, the AI world's slot of world 2 and the pool's slot mask. */
static void RecordPins(uint8_t* exe)
{
    size_t n = 0;
    const CoCuttersPin* pins = co_cutters_test_record_pins(&n);
    for (size_t i = 0; i < n; i++) Hex(exe + pins[i].rva, pins[i].pin);
}
static void AiWorld(uint64_t world) { memcpy(g_exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD, &world, 8); }
static void Mask(uint8_t* pool, uint32_t mask) { memcpy(pool + CO_CUTTERS_MASK, &mask, 4); }
/* The forest's description as 0x8155D5..0x8155F3 fills it. */
typedef struct Desc { uint32_t type, world, mask, add; const float* matrix; uint32_t flag, pad, body, end; } Desc;
static const float kMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 40.0f, -70.0f, 12.0f, 1.0f};

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

    Reset("not armed: a forest make is not recorded");
    RecordPins(g_exe); AiWorld(0x5000); Mask(g_pool, 0xFFFF);
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    {
        Desc d = {3, 2, 0xFFFFFFFFu, 1, kMatrix, 0, 0, 77};
        CoCuttersTick t;
        co_cutters_live(1);
        g_makeAnswer = 0x81010005u;
        CHECK(co_cutters_test_make(&d, (uintptr_t)g_exe + CO_CUTTERS_FOREST_RETURN) == 0x81010005u && g_makes == 1);
        CHECK(co_cutters_tick(NULL, 0, 1000, 16, &t) == 0);
        /* armed after: only what is made from then on */
        CHECK(co_cutters_record_arm((uintptr_t)g_exe, CO_CUTSET_CAP, CO_CUTSET_ADDS) == NULL);
        CHECK(co_cutters_test_set()->records == 0);
    }

    Reset("armed: a tree's cutter is recorded, its description left as it was");
    RecordPins(g_exe); AiWorld(0x5000); Mask(g_pool, 0xFFFF);
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    CHECK(co_cutters_record_arm((uintptr_t)g_exe, 3, CO_CUTSET_ADDS) == NULL);
    CHECK(co_cutters_record_arm((uintptr_t)g_exe, 9, 9) == NULL && co_cutters_test_set()->cap == 3); /* armed once */
    make = MakeDetour(); destroy = Detour();
    CHECK(make != NULL && destroy != NULL);
    if (make != NULL && destroy != NULL)
    {
        const CoCutSet* s = co_cutters_test_set();
        const uintptr_t forest = (uintptr_t)g_exe + CO_CUTTERS_FOREST_RETURN;
        Desc d = {3, 2, 0xFFFFFFFFu, 1, kMatrix, 0, 0, 77}, was = d;
        CoCuttersTick t;
        /* no complex level: nothing recorded */
        g_makeAnswer = 0x81010005u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010005u && s->records == 0);
        CHECK(co_cutters_tick(NULL, 0, 1000, 16, &t) == 0);
        CHECK(co_cutters_live(1) == 0);
        CHECK(co_cutters_test_make(&d, forest) == 0x81010005u && g_addSeen == 1);
        CHECK(memcmp(&d, &was, sizeof d) == 0);
        /* before any tick, a make and a destroy on other threads: counted as off the tick's once a tick says which
         * thread that is, and this thread's own are not */
        g_makeAnswer = 0x81010041u; OnOtherThread((OffThread){&d, forest, NULL, 0});
        g_makeAnswer = 0x81010042u; OnOtherThread((OffThread){&d, forest, destroy, 0x81010041u});
        OnOtherThread((OffThread){NULL, 0, destroy, 0x81010042u});
        g_makeAnswer = 0x81010005u; g_calls = g_seenCount = 0;
        CHECK(co_cutters_tick(NULL, 0, 900, 16, &t) == 1 && t.makesOff == 2 && t.destroysOff == 2 && t.threadsOff == 3 && t.records == 1 && t.trees == 3);
        CHECK(s->records == 1 && s->wanted == 1 && s->world == 0x5000 && s->pool == (uintptr_t)g_pool && s->mask == 0xFFFF);
        CHECK(s->rec[0].handle == 0x81010005u && s->rec[0].x == 40.0f && s->rec[0].y == -70.0f);
        /* a tree the game does not add: recorded as such, the game's choice passed on */
        d.add = 0; was = d; g_makeAnswer = 0x81010006u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010006u && g_addSeen == 0 && memcmp(&d, &was, sizeof d) == 0);
        CHECK(s->records == 2 && s->wanted == 1);
        /* not made (the pool was full), another world, another kind from the forest's site: no record */
        d.add = 1; g_makeAnswer = 0;
        CHECK(co_cutters_test_make(&d, forest) == 0 && s->records == 2);
        d.world = 1; g_makeAnswer = 0x41010007u;
        CHECK(co_cutters_test_make(&d, forest) == 0x41010007u && s->records == 2);
        d.world = 2; d.type = 0; g_makeAnswer = 0x81010008u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010008u && s->records == 2);
        d.type = 3; g_makeAnswer = 0x81010009u;
        CHECK(co_cutters_test_make(&d, forest + 1) == 0x81010009u && s->records == 2);
        /* the tick counts what it would switch on, capped at 3 here; the engine hears nothing */
        const uint32_t makes = g_makes;
        CoCutCentre c = {40.0f, -60.0f, CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT};
        CHECK(co_cutters_tick(&c, 1, 1000, 16, &t) == 1);
        CHECK(t.bound == 1 && t.hooked == 1 && t.records == 2 && t.wanted == 1 && t.on == 1 && t.adds == 1 && t.removes == 0 && t.cap == 3);
        CHECK(t.trees == 4 && t.others == 2 && t.unbound == 0 && t.destroyed == 2 && t.world == 0x5000 && t.binds == 1);
        CHECK(g_makes == makes && g_calls == 0);
        /* any other caller's cutter in a recorded slot (through the hook itself): the tree's old record goes */
        g_makeAnswer = 0x81030005u;
        CHECK(make(&d) == 0x81030005u && s->records == 1 && s->replaced == 1 && s->on == 0);
        /* the engine's destroy: the record goes first, the batch passes as it is */
        const uint32_t batch[3] = {0x81010006u, 0x81010099u, 0x81030006u};
        destroy(batch, 3);
        CHECK(s->records == 0 && s->stale == 0 && g_calls == 1 && g_seenCount == 3 && g_seen[0] == 0x81010006u && g_seen[2] == 0x81030006u);
        CHECK(co_cutters_tick(&c, 1, 1250, 16, &t) == 1 && t.destroyed == 3 && t.records == 0 && t.makesOff == 2 && t.destroysOff == 2);
        /* a new AI world, then a new pool: the records of the old one go before the next is taken */
        g_makeAnswer = 0x81010005u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010005u && s->records == 1 && s->binds == 1);
        AiWorld(0x6000); g_makeAnswer = 0x81010006u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010006u && s->records == 1 && s->binds == 2 && s->world == 0x6000);
        CHECK(s->rec[s->slot[6] - 1].handle == 0x81010006u);
        static uint8_t pool2[0x40];
        uint64_t at = (uint64_t)(uintptr_t)pool2;
        Mask(pool2, 0xFFFFF);
        memcpy(g_exe + CO_CUTTERS_POOLS + 16u, &at, 8);
        g_makeAnswer = 0x81110007u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81110007u && s->records == 1 && s->binds == 3 && s->mask == 0xFFFFF && s->pool == (uintptr_t)pool2);
        /* a pool whose mask the set cannot hold, and no AI world: nothing recorded, and counted */
        Mask(pool2, 0x1FFFFF); at = (uint64_t)(uintptr_t)g_pool;
        memcpy(g_exe + CO_CUTTERS_POOLS + 16u, &at, 8); Mask(g_pool, 0x1FFFFF);
        CHECK(co_cutters_test_make(&d, forest) == 0x81110007u && s->records == 0 && s->world == 0);
        Mask(g_pool, 0xFFFF); AiWorld(0);
        CHECK(co_cutters_test_make(&d, forest) == 0x81110007u && s->records == 0);
        CHECK(co_cutters_tick(&c, 1, 1500, 16, &t) == 1 && t.bound == 0 && t.unbound == 2 && t.adds == 0);
        AiWorld(0x6000);
        CHECK(co_cutters_test_make(&d, forest) == 0x81110007u && s->records == 1 && s->binds == 4);
        /* the level's end drops them, and nothing is recorded after it */
        CHECK(co_cutters_live(0) == 1 && s->records == 0);
        CHECK(co_cutters_test_make(&d, forest) == 0x81110007u && s->records == 0);
        CHECK(co_cutters_tick(&c, 1, 1750, 16, &t) == 0);
    }

    Reset("switching: a make is held back while a tick is alive, and the tick switches it on");
    RecordPins(g_exe); AiWorld(0x5000); Mask(g_pool, 0xFFFF);
    g_swCalls = g_swOnCount = g_swOffCount = g_swBiggest = 0;
    CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
    CHECK(co_cutters_record_arm((uintptr_t)g_exe, CO_CUTSET_SLOTS, CO_CUTSET_ADDS) == NULL);
    co_cutters_test_switch(EngineSwitch);
    co_cutters_switch(1);
    make = MakeDetour(); destroy = Detour();
    CHECK(make != NULL && destroy != NULL);
    if (make != NULL && destroy != NULL)
    {
        const CoCutSet* s = co_cutters_test_set();
        const uintptr_t forest = (uintptr_t)g_exe + CO_CUTTERS_FOREST_RETURN;
        Desc d = {3, 2, 0xFFFFFFFFu, 1, kMatrix, 0, 0, 77}, was = d;
        CoCuttersTick t;
        CoCutCentre c = {40.0f, -60.0f, CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT}, away = {5000.0f, 5000.0f, CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT};
        co_cutters_live(1);
        /* no tick yet (the level is loading): the game adds the cutter itself, and the set knows it is on */
        g_makeAnswer = 0x81010005u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010005u && g_addSeen == 1 && memcmp(&d, &was, sizeof d) == 0);
        CHECK(s->records == 1 && s->on == 1 && g_swCalls == 0);
        /* the first tick: nothing to add, and from now on makes are held back */
        uint32_t now = GetTickCount();
        CHECK(co_cutters_tick(&c, 1, now, 0, &t) == 1 && t.skipped == 0 && t.adds == 0 && t.stock == 1 && t.held == 0 && g_swCalls == 0);
        g_makeAnswer = 0x81010006u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010006u && g_addSeen == 0); /* the engine saw "do not add" */
        CHECK(memcmp(&d, &was, sizeof d) == 0);                                  /* and the caller's description is as it was */
        CHECK(s->records == 2 && s->wanted == 2 && s->on == 1 && g_swCalls == 0);
        /* a tree the game never adds is not held back: nothing to hold */
        d.add = 0; was = d; g_makeAnswer = 0x81010007u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010007u && g_addSeen == 0 && memcmp(&d, &was, sizeof d) == 0);
        d.add = 1; was = d;
        /* another caller's cutter, another world, another kind: the game's "add now" passes */
        g_makeAnswer = 0x81010008u;
        CHECK(co_cutters_test_make(&d, forest + 1) == 0x81010008u && g_addSeen == 1);
        d.world = 1; g_makeAnswer = 0x41010009u;
        CHECK(co_cutters_test_make(&d, forest) == 0x41010009u && g_addSeen == 1);
        d.world = 2; d.type = 1; g_makeAnswer = 0x8101000Au;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Au && g_addSeen == 1);
        d.type = 3;
        /* a make that fails while held back: the description is put back, nothing recorded or switched */
        g_makeAnswer = 0;
        CHECK(co_cutters_test_make(&d, forest) == 0 && g_addSeen == 0 && memcmp(&d, &was, sizeof d) == 0 && s->records == 3 && g_swCalls == 0);
        /* the AI step in flight: the tick chooses nothing and sends nothing */
        StepTask(0x1234);
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1 && t.skipped == 1 && t.skips == 1 && t.adds == 0 && s->on == 1 && g_swCalls == 0);
        StepTask(0);
        /* the tick: the held one goes to the engine's switch, on */
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1 && t.skipped == 0 && t.adds == 1 && t.on == 2 && t.held == 1 && t.sentOn == 1 && t.sentOff == 0);
        CHECK(g_swCalls == 1 && g_swOnCount == 1 && g_swOn[0] == 0x81010006u && g_swOffCount == 0);
        /* a held one destroyed before its tick is never handed to the switch; this thread is the tick's */
        g_makeAnswer = 0x8101000Du;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Du && g_addSeen == 0 && s->records == 4);
        { const uint32_t gone[1] = {0x8101000Du}; destroy(gone, 1); }
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1 && t.adds == 0 && g_swCalls == 1 && t.makesOff == 0 && t.destroysOff == 0 && t.stranded == 0);
        /* removes, when the tick is given some: the cutters out of reach for 5 s go off through the switch */
        CHECK(co_cutters_tick(&away, 1, (now = GetTickCount()) + 10u, 16, &t) == 1 && t.removes == 0);
        CHECK(co_cutters_tick(&away, 1, now + 6000u, 16, &t) == 1 && t.removes == 2 && t.sentOff == 2 && t.on == 0);
        CHECK(g_swCalls == 2 && g_swOffCount == 2 && g_swOnCount == 1);
        /* the last tick was over a second ago: the game adds again */
        CHECK(co_cutters_tick(&away, 1, GetTickCount() - 1500u, 0, &t) == 1);
        g_makeAnswer = 0x8101000Bu;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Bu && g_addSeen == 1 && s->on == 1);
        CHECK(co_cutters_tick(&away, 1, GetTickCount(), 0, &t) == 1 && t.stock == 2 && t.atOnce == 0);
        /* held back, and a pool the set cannot hold came during the make: no record, so the cutter is added at once */
        g_makeDropsWorld = 3; g_makeAnswer = 0x8101000Cu; g_swCalls = g_swOnCount = 0;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Cu && g_addSeen == 0 && memcmp(&d, &was, sizeof d) == 0);
        CHECK(g_swCalls == 1 && g_swOnCount == 1 && g_swOn[0] == 0x8101000Cu);
        g_makeDropsWorld = 0; Pool(100, 65536);
        CHECK(co_cutters_tick(&away, 1, GetTickCount(), 0, &t) == 1 && t.atOnce == 1 && t.noWorld == 0 && t.records == 0);
        /* held back, and the AI world went during the make: the engine's switch would read through a null, so it
         * is not called and the cutter is counted */
        g_makeDropsWorld = 1; g_makeAnswer = 0x8101000Eu; g_swCalls = g_swOnCount = 0;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Eu && g_addSeen == 0 && memcmp(&d, &was, sizeof d) == 0);
        CHECK(g_swCalls == 0);
        g_makeDropsWorld = 0; AiWorld(0x5000);
        CHECK(co_cutters_tick(&away, 1, GetTickCount(), 0, &t) == 1 && t.atOnce == 1 && t.noWorld == 1 && t.records == 0);
        /* the same with the pool gone */
        g_makeDropsWorld = 2; g_makeAnswer = 0x8101000Fu;
        CHECK(co_cutters_test_make(&d, forest) == 0x8101000Fu && g_addSeen == 0 && g_swCalls == 0);
        g_makeDropsWorld = 0; Pool(100, 65536);
        CHECK(co_cutters_tick(&away, 1, GetTickCount(), 0, &t) == 1 && t.atOnce == 1 && t.noWorld == 2);
        /* the level ended during the make, the world and the pool still there: added at once */
        g_makeDropsWorld = 4; g_makeAnswer = 0x81010010u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010010u && g_addSeen == 0 && g_swCalls == 1 && g_swOn[0] == 0x81010010u);
        g_makeDropsWorld = 0; co_cutters_live(1);
        CHECK(co_cutters_tick(&away, 1, GetTickCount(), 0, &t) == 1 && t.atOnce == 2 && t.noWorld == 2);
        /* more adds than a batch: 64 a call */
        for (uint32_t i = 0; i < 150; i++) { g_makeAnswer = 0x81010100u + i; co_cutters_test_make(&d, forest); }
        g_swCalls = g_swOnCount = g_swBiggest = 0;
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1 && t.adds == 150 && t.held == 152 && g_swCalls == 3 && g_swBiggest == 64 && g_swOnCount == 150);
        /* the level's end: the next level's first trees are the game's again */
        CHECK(co_cutters_live(0) > 0 && co_cutters_live(1) == 0);
        g_makeAnswer = 0x81010005u;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010005u && g_addSeen == 1);
        /* switching off: record only again */
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1);
        co_cutters_switch(0);
        g_makeAnswer = 0x81010006u; g_swCalls = 0;
        CHECK(co_cutters_test_make(&d, forest) == 0x81010006u && g_addSeen == 1);
        CHECK(co_cutters_tick(&c, 1, GetTickCount(), 0, &t) == 1 && t.adds == 1 && g_swCalls == 0);
    }

    {
        size_t pins = 0;
        co_cutters_test_record_pins(&pins);
        CHECK(pins == 13);
        for (size_t i = 0; i < pins; i++)
        {
            Reset("a changed byte at one of the record's sites: not armed");
            RecordPins(g_exe); AiWorld(0x5000); Mask(g_pool, 0xFFFF);
            g_exe[co_cutters_test_record_pins(&pins)[i].rva] ^= 0xFF;
            CHECK(co_cutters_install((uintptr_t)g_exe) == NULL);
            CHECK(co_cutters_record_arm((uintptr_t)g_exe, CO_CUTSET_CAP, CO_CUTSET_ADDS) != NULL);
            Desc d = {3, 2, 0xFFFFFFFFu, 1, kMatrix, 0, 0, 77};
            CoCuttersTick t;
            co_cutters_live(1);
            g_makeAnswer = 0x81010005u;
            CHECK(co_cutters_test_make(&d, (uintptr_t)g_exe + CO_CUTTERS_FOREST_RETURN) == 0x81010005u);
            CHECK(co_cutters_tick(NULL, 0, 1000, 16, &t) == 0);
        }
        Reset("no image: not armed");
        CHECK(co_cutters_record_arm(0, CO_CUTSET_CAP, CO_CUTSET_ADDS) != NULL);
    }

    if (g_failed) { printf("cutters: %d check(s) failed\n", g_failed); return 1; }
    printf("cutters: all checks passed\n");
    return 0;
}
