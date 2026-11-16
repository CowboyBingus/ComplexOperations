/* The SpeedTree forest's room (co_forest.h): the trees read as live record ids from a stamp unit's world, and the
 * streamer's SpawnLevelLayers call waits while one more call could fill the forest's instance grid (crash exe+0x20CF57:
 * the grid's insert 0x7AD5E0 gives 0 when all 98304 slots are taken, and the spawn writes through it). Run on a fake
 * world, forest and SpawnLevelLayers that adds trees per record. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_forest.h"
#include "xl/co_framecost.h"
#include "xl/co_shim.h"

void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count);
void CoXlTestStreamSpawnSetup(void* spawn, uint8_t* pass);
void CoXlTestStreamDue(uint32_t i, uint64_t unit);
void CoXlTestStreamFloorDue(uint32_t* due, uint32_t* spawned);
void CoXlTestStreamSpawnDue(void);
uint32_t CoXlTestStreamChange(unsigned long long now, int layerBusy, unsigned long long* next);
void CoXlTestStreamChangeAt(unsigned long long next);
void CoXlTestStreamRemoveWait(unsigned long long at, int lastKind);
uint32_t CoXlTestStreamDrain(double ms, uint32_t* waiting);
void CoXlTestStreamSpawned(uint32_t i, int* spawned, int* due);
void CoXlTestStreamTrees(uint32_t* holds, uint32_t* most, uint32_t* cursor);
int CoXlTestWindowTreesOver(float share);
int CoXlTestStreamSpawnInto(uint32_t layer, double ms, uint32_t most);
void CoXlTestStreamMakeEstimate(double perLocation);
uint32_t CoXlTestStreamSpawnedIn(void);
uint32_t CoXlTestStreamSpawnCaps(void);
uint32_t CoXlTestStreamLayerOf(uint32_t i);
void CoXlTestStreamExpiredSet(uint32_t n);
int CoXlTestStreamDropPush(uint32_t i);
void CoXlTestStreamDistance(uint32_t i, float distance2);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define ROOM 0x18000u /* the forest grid's room as 0x20AE1E passes it */
static uint8_t* g_world;  /* the unit's world: its forest at +0x2308B8 */
static uint8_t g_forest[0x200];
static uint8_t g_unit[0x40];
static uint8_t* g_exe;

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

static void Pins(void)
{
    Hex(g_exe + 0xDC46Eu, "49 8B 4D 18");
    Hex(g_exe + 0xDC485u, "48 8B 89 B8 08 23 00");
    Hex(g_exe + 0x20BF00u, "8B 96 D8 00 00 00");
    Hex(g_exe + 0x20BF22u, "44 8B B6 F0 00 00 00");
    Hex(g_exe + 0x20ADEAu, "48 8D 8E 20 01 00 00");
    Hex(g_exe + 0x20CF2Cu, "48 8D 8E 20 01 00 00");
    Hex(g_exe + 0x20CF49u, "E8 92 06 5A 00 44 8B 4D B0 B9 FF FF FF FF 44 89 08");
    Hex(g_exe + 0x7AD36Du, "44 89 6F 1C");
    Hex(g_exe + 0x7AD60Cu, "41 83 FC FF");
}

static void Forest(uint32_t made, uint32_t idle, uint32_t room)
{
    memcpy(g_forest + CO_FOREST_RECORDS, &made, 4);
    memcpy(g_forest + CO_FOREST_FREE, &idle, 4);
    memcpy(g_forest + CO_FOREST_GRID + CO_FOREST_GRID_ROOM, &room, 4);
}
static uint32_t Made(void) { uint32_t m; memcpy(&m, g_forest + CO_FOREST_RECORDS, 4); return m; }

/* SpawnLevelLayers as far as the forest: every record of the call adds g_perRecord trees, and all records are done. */
static uint32_t g_perRecord, g_calls;
static uint8_t Spawn(uint8_t* pass, uint8_t* record, uint8_t* context)
{
    uint64_t countAt = 0;
    uint32_t count = 0;
    memcpy(&countAt, pass + 0x48, 8);
    memcpy(&count, (void*)(uintptr_t)countAt, 4);
    uint32_t made = Made() + count * g_perRecord;
    memcpy(g_forest + CO_FOREST_RECORDS, &made, 4);
    memcpy(pass + 0xD8, &count, 4);
    g_calls++;
    return 1;
}

static uint8_t g_context[0x100], g_pass[0x100], g_array[24 * 0xE0];
/* A captured level of `count` locations, each with a stamp unit in the world above; none due yet. */
static void Level(uint32_t count)
{
    uint64_t world = (uint64_t)(uintptr_t)g_world, array = (uint64_t)(uintptr_t)g_array;
    memcpy(g_unit + CO_FOREST_UNIT_WORLD, &world, 8);
    memcpy(g_context + 0x40, &world, 8);
    memset(g_pass, 0, sizeof g_pass);
    memcpy(g_pass + 8, &array, 8);
    CoXlTestStreamSetup((uintptr_t)g_fake.image, g_context, g_array, count);
    CoXlTestStreamSpawnSetup((void*)Spawn, g_pass);
    g_calls = 0;
}

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_exe = fake_host_exe();
    g_fake.strictCode = 1;
    Pins();
    uint64_t forest = (uint64_t)(uintptr_t)g_forest;
    memcpy(g_world + CO_FOREST_OF_WORLD, &forest, 8);
    memset(g_forest, 0, sizeof g_forest);
    Forest(1000, 0, ROOM);
    g_perRecord = 100;
}

int main(void)
{
    g_world = (uint8_t*)VirtualAlloc(NULL, CO_FOREST_OF_WORLD + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL) { printf("forest: no memory\n"); return 1; }
    const uint64_t unit = (uint64_t)(uintptr_t)g_unit;

    Start("the forest from a unit's world; its trees are the records made less the free ids");
    {
        uint64_t world = (uint64_t)(uintptr_t)g_world;
        memcpy(g_unit + CO_FOREST_UNIT_WORLD, &world, 8);
        uintptr_t f = 0;
        CHECK(co_forest_of_unit(unit, &f) && f == (uintptr_t)g_forest);
        CHECK(!co_forest_of_unit(0, &f));
        uint32_t trees = 0, room = 0;
        Forest(98334, 30, ROOM);
        CHECK(co_forest_trees((uintptr_t)g_forest, &trees, &room) && trees == 98304 && room == ROOM);
        Forest(100, 101, ROOM); /* more free than made: not a forest */
        CHECK(!co_forest_trees((uintptr_t)g_forest, &trees, &room));
        Forest(100, 0, 0);
        CHECK(!co_forest_trees((uintptr_t)g_forest, &trees, &room));
        Forest(100, 0, CO_FOREST_ROOM_MAX + 1);
        CHECK(!co_forest_trees((uintptr_t)g_forest, &trees, &room));
        Forest(CO_FOREST_ROOM_MAX + 1, 0, ROOM);
        CHECK(!co_forest_trees((uintptr_t)g_forest, &trees, &room));
        Forest(CO_FOREST_ROOM_MAX, 0, CO_FOREST_ROOM_MIN);
        CHECK(co_forest_trees((uintptr_t)g_forest, &trees, &room) && trees == CO_FOREST_ROOM_MAX && room == CO_FOREST_ROOM_MIN);
        memset(g_world + CO_FOREST_OF_WORLD, 0, 8);
        CHECK(!co_forest_of_unit(unit, &f));
    }

    g_case = "the shares and the reserve, at their edges";
    CHECK(!co_forest_over(78643, ROOM, 0.80f) && co_forest_over(78644, ROOM, 0.80f));
    CHECK(!co_forest_over(68812, ROOM, 0.70f) && co_forest_over(68813, ROOM, 0.70f));
    CHECK(co_forest_over(ROOM / 2u, ROOM, 0.50f) && !co_forest_over(ROOM / 2u - 1u, ROOM, 0.50f)); /* the share itself is over */
    CHECK(co_forest_call_fits(ROOM - ROOM / 10u, ROOM, 0) && !co_forest_call_fits(ROOM - ROOM / 10u + 1u, ROOM, 0));
    CHECK(co_forest_call_fits(ROOM - 20000u, ROOM, 10000) && !co_forest_call_fits(ROOM - 19999u, ROOM, 10000));
    CHECK(!co_forest_call_fits(ROOM + 30u, ROOM, 0)); /* the crash: record ids past the grid's room */
    CHECK(!co_forest_call_fits(0xFFFFFFF0u, 0xFFFFFFF8u, 0x7FFFFFFFu));

    Start("pins: the engine's code as read, and one site changed");
    CHECK(co_forest_pins((uintptr_t)g_exe) == 1);
    g_exe[0xDC485u + 3] = 0xC0; /* another world offset */
    CHECK(co_forest_pins((uintptr_t)g_exe) == 0);
    CHECK(co_forest_pins(0) == 0);

    Start("room in the forest: the call goes and its trees are counted");
    Level(2);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    CoXlTestStreamSpawnDue();
    uint32_t holds = 0, most = 0, cursor = 0;
    int spawned = 0, due = 0;
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 1 && holds == 0 && most == 200 && Made() == 1200);
    CoXlTestStreamSpawned(0, &spawned, &due);
    CHECK(spawned);
    CoXlTestStreamSpawned(1, &spawned, &due);
    CHECK(spawned);
    CHECK(!CoXlTestWindowTreesOver(0.80f));

    Start("the spawns go in a change frame, none while a layer fill runs, and a frame that spawned nothing leaves the next free");
    Level(2);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    unsigned long long next = 7;
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamChange(1000, 1, &next) == 0 && g_calls == 0 && next == 0);
    CHECK(CoXlTestStreamChange(1001, 0, &next) == 1 && g_calls == 1 && next == 1251);
    CoXlTestStreamSpawned(1, &spawned, &due);
    CHECK(spawned);

    /* Removals and spawns never share a frame: a removal frame (here forced) spawns nothing, the next try spawns. */
    Start("a removal frame spawns nothing; the spawns go in the next frame");
    Level(6);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    CHECK(CoXlTestStreamDropPush(5));
    CoXlTestStreamChangeAt(0);
    CoXlTestStreamRemoveWait(1, 0);                /* the removal waited since the start of the clock: forced */
    CHECK(CoXlTestStreamChange(3000, 0, &next) == 0 && g_calls == 0); /* slot 5 holds no unit: nothing removed, none spawned */
    CoXlTestStreamSpawned(0, &spawned, &due);
    CHECK(!spawned && due);
    CHECK(CoXlTestStreamChange(3001, 0, &next) == 1 && g_calls == 1); /* the queue is empty now: a spawn frame */

    /* A change frame's stamps are rebuilt in one broad phase layer: each slot keeps the layer it went to, for its removal. */
    Start("spawns into a broad phase layer tag their slots; a held call tags none; a pass cut by its time says so");
    Level(3);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    CHECK(CoXlTestStreamSpawnInto(7, 1.0e9, 24) == 0 && g_calls == 1);
    CHECK(CoXlTestStreamLayerOf(0) == 7 && CoXlTestStreamLayerOf(1) == 7 && CoXlTestStreamLayerOf(2) == 0);
    CoXlTestStreamDue(2, unit);
    CoXlTestStreamSpawnDue();                      /* no layer of its own: layer 4 */
    CHECK(g_calls == 2 && CoXlTestStreamLayerOf(2) == 0 && CoXlTestStreamLayerOf(0) == 7);
    Forest(ROOM, 0, ROOM);                         /* the forest full: the call waits */
    Level(1);
    CoXlTestStreamDue(0, unit);
    CHECK(CoXlTestStreamSpawnInto(8, 1.0e9, 24) == 0 && g_calls == 0 && CoXlTestStreamLayerOf(0) == 0);
    Forest(1000, 0, ROOM);
    Level(20);
    for (uint32_t k = 0; k < 20; k++) CoXlTestStreamDue(k, unit);
    CHECK(CoXlTestStreamSpawnInto(9, 0.0, 24) == 1 && g_calls == 1);   /* one call of 16, then the time is used */
    CHECK(CoXlTestStreamLayerOf(15) == 9 && CoXlTestStreamLayerOf(16) == 0);
    CHECK(CoXlTestStreamSpawnInto(9, 0.0, 24) == 0 && g_calls == 2);   /* the last 4: nothing left when it stops */
    CHECK(CoXlTestStreamLayerOf(19) == 9);

    /* The units of a change frame's spawns are made in that frame (the ring's flush), about 30 bodies a location, which
     * the spawn calls' own time does not see: a count caps the locations, and the last flush's time each the rest. */
    Start("a change frame's spawns: at most its location count, and its time takes the units' making in");
    Level(20);
    for (uint32_t k = 0; k < 20; k++) CoXlTestStreamDue(k, unit);
    CHECK(CoXlTestStreamSpawnInto(7, 1.0e9, 10) == 2 && g_calls == 1 && CoXlTestStreamSpawnedIn() == 10); /* one call of 10 */
    CHECK(CoXlTestStreamLayerOf(9) == 7 && CoXlTestStreamLayerOf(10) == 0);
    CHECK(CoXlTestStreamSpawnInto(7, 1.0e9, 10) == 0 && g_calls == 2 && CoXlTestStreamSpawnedIn() == 20); /* the last 10 */
    CHECK(CoXlTestStreamSpawnCaps() == 1);
    Level(24);
    for (uint32_t k = 0; k < 24; k++) CoXlTestStreamDue(k, unit);
    CHECK(CoXlTestStreamSpawnInto(7, 1.0e9, 20) == 2 && g_calls == 2 && CoXlTestStreamSpawnedIn() == 20); /* 16, then the 4 left */
    CHECK(CoXlTestStreamLayerOf(19) == 7 && CoXlTestStreamLayerOf(20) == 0);
    Level(20);
    for (uint32_t k = 0; k < 20; k++) CoXlTestStreamDue(k, unit);
    CoXlTestStreamMakeEstimate(1.0);                 /* 1 ms a location: 3 in a 3 ms frame */
    CHECK(CoXlTestStreamSpawnInto(7, 3.0, 24) == 1 && g_calls == 1 && CoXlTestStreamSpawnedIn() == 3);
    CHECK(CoXlTestStreamLayerOf(2) == 7 && CoXlTestStreamLayerOf(3) == 0);
    CoXlTestStreamMakeEstimate(10.0);                /* more than the frame: the floor of 2, so a frame always moves on */
    CHECK(CoXlTestStreamSpawnInto(7, 3.0, 24) == 1 && g_calls == 2 && CoXlTestStreamSpawnedIn() == 5);
    CHECK(CoXlTestStreamLayerOf(4) == 7 && CoXlTestStreamLayerOf(5) == 0);
    CoXlTestStreamMakeEstimate(0.1);                 /* 10 fit in 1 ms; 16 a call at most */
    CHECK(CoXlTestStreamSpawnInto(7, 1.0, 24) == 1 && g_calls == 3 && CoXlTestStreamSpawnedIn() == 15);
    Level(24);
    for (uint32_t k = 0; k < 24; k++) CoXlTestStreamDue(k, unit);
    CoXlTestStreamMakeEstimate(0.25);                /* a call of 16 (4 ms to make), then the 1.6 ms left make 6 */
    CHECK(CoXlTestStreamSpawnInto(7, 5.6, 40) == 1 && g_calls == 2 && CoXlTestStreamSpawnedIn() == 22);
    CHECK(CoXlTestStreamLayerOf(21) == 7 && CoXlTestStreamLayerOf(22) == 0);
    CoXlTestStreamMakeEstimate(0.0);

    /* At its budget's floor a change frame past the window's design (the live stamps past their removal radius more than
     * a frame's removals, some queued) spawns only the nearest due inside kStreamInRadius, kStreamSpawnFloor of them:
     * near content keeps coming while the removals catch up. */
    Start("a change frame at its floor past the window's design: the nearest 2 inside 500 m, the rest wait");
    {
        int64_t t = 1000000;
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 16500; } /* quiet frames of 16.5 ms: no room */
        Level(5);
        CoXlTestStreamDue(0, unit); CoXlTestStreamDue(1, unit); CoXlTestStreamDue(2, unit);
        CoXlTestStreamDistance(0, 100.0f * 100.0f); CoXlTestStreamDistance(1, 400.0f * 400.0f); CoXlTestStreamDistance(2, 600.0f * 600.0f);
        CoXlTestStreamExpiredSet(65);
        CHECK(CoXlTestStreamDropPush(4));
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(2000, 0, &next) == 1 && g_calls == 1);       /* 0 and 1 in one call */
        {
            uint32_t floorDue = 9, floorSpawned = 9;
            CoXlTestStreamFloorDue(&floorDue, &floorSpawned);
            CHECK(floorDue == 1 && floorSpawned == 2);                   /* a judged floor frame, its 2 locations */
        }
        CoXlTestStreamSpawned(1, &spawned, &due); CHECK(spawned);
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(!spawned && due);     /* 600 m: it waits */
        CoXlTestStreamDropPush(4);                                 /* still queued: removals gather */
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(2001, 0, &next) == 0 && g_calls == 1);       /* only the far one left: nothing */
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(!spawned && due);
        CoXlTestStreamDropPush(4);                                 /* still queued: removals gather */
        CoXlTestStreamExpiredSet(64);                                     /* a frame's removals: the window holds */
        CHECK(CoXlTestStreamChange(2002, 0, &next) == 1 && g_calls == 2);
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(spawned);
        /* three near ones past the design: the floor's 2 */
        Level(5);
        CoXlTestStreamDue(0, unit); CoXlTestStreamDue(1, unit); CoXlTestStreamDue(2, unit);
        for (uint32_t k = 0; k < 3; k++) CoXlTestStreamDistance(k, 10.0f);
        CoXlTestStreamExpiredSet(65);
        CHECK(CoXlTestStreamDropPush(4));
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(2300, 0, &next) == 1 && g_calls == 1);
        CoXlTestStreamSpawned(1, &spawned, &due); CHECK(spawned);
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(!spawned && due);
        /* nothing queued: the window holds, all go */
        { uint32_t left = 9; CoXlTestStreamDrain(1.0e9, &left); CHECK(left == 0); }
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(2400, 0, &next) == 1 && g_calls == 2);
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(spawned);
        /* room (the change frames are no quiet frames: 33 push the last 16.5 out): the far one goes too */
        for (int k = 0; k < 33; k++) { co_framecost_test_begin(t, 1, 1); t += 10000; }
        Level(5);
        CoXlTestStreamDue(0, unit); CoXlTestStreamDue(1, unit); CoXlTestStreamDue(2, unit);
        CoXlTestStreamDistance(2, 600.0f * 600.0f);
        CHECK(CoXlTestStreamDropPush(4));
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(2600, 0, &next) == 1 && g_calls == 1);
        CoXlTestStreamSpawned(2, &spawned, &due); CHECK(spawned);
        {
            uint32_t floorDue = 9, floorSpawned = 9;
            CoXlTestStreamFloorDue(&floorDue, &floorSpawned);
            CHECK(floorDue == 0 && floorSpawned == 0);                   /* a frame with room: not a floor frame's spawns */
        }
        for (uint32_t k = 0; k < 3; k++) CoXlTestStreamDistance(k, 0.0f);
        CoXlTestStreamExpiredSet(0);
        co_framecost_test_reset(0);
    }

    Start("the forest near full: the call waits and its records stay due");
    Forest(ROOM - ROOM / 10u + 1u, 0, ROOM);
    Level(2);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    CoXlTestStreamSpawnDue();
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 0 && holds == 1 && cursor == 0 && Made() == ROOM - ROOM / 10u + 1u);
    CoXlTestStreamSpawned(0, &spawned, &due);
    CHECK(!spawned && due);
    CHECK(fake_host_logged("content streaming waits: the SpeedTree forest holds"));
    CHECK(CoXlTestWindowTreesOver(0.80f) && CoXlTestWindowTreesOver(0.70f));
    /* removals gave ids back: the same records go */
    Forest(ROOM - ROOM / 10u + 1u, 5000, ROOM);
    CoXlTestStreamSpawnDue();
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 1 && holds == 1);
    CoXlTestStreamSpawned(1, &spawned, &due);
    CHECK(spawned);

    Start("the reserve grows with the most one call added");
    Forest(50000, 0, ROOM);
    Level(2);
    g_perRecord = 6000;
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamDue(1, unit);
    CoXlTestStreamSpawnDue();
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 1 && most == 12000 && Made() == 62000);
    Forest(75000, 0, ROOM); /* 23304 left: a tenth of the room fits, twice 12000 does not */
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamSpawnDue();
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 1 && holds == 1);
    Forest(74304, 0, ROOM); /* 24000 left: it fits */
    CoXlTestStreamSpawnDue();
    CHECK(g_calls == 2);

    Start("the window's shares read the same forest");
    Level(1);
    CoXlTestStreamDue(0, unit);
    Forest(68813, 0, ROOM);
    CHECK(CoXlTestWindowTreesOver(0.70f) && !CoXlTestWindowTreesOver(0.80f));
    Forest(78644, 0, ROOM);
    CHECK(CoXlTestWindowTreesOver(0.80f));

    Start("an engine whose forest code differs: nothing is counted or held, once noted");
    g_exe[0x7AD36Du] = 0x90;
    Forest(ROOM, 0, ROOM);
    Level(1);
    CoXlTestStreamDue(0, unit);
    CoXlTestStreamSpawnDue();
    CoXlTestStreamTrees(&holds, &most, &cursor);
    CHECK(g_calls == 1 && holds == 0);
    CHECK(fake_host_logged("the SpeedTree forest code is not the code this mod was made for"));
    CHECK(!CoXlTestWindowTreesOver(0.10f));

    if (g_failed) { printf("forest: %d checks failed\n", g_failed); return 1; }
    printf("forest: all cases passed\n");
    return 0;
}
