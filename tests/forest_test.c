/* The SpeedTree forest's room (co_forest.h): the trees read as live record ids from a stamp unit's world, and the
 * streamer's SpawnLevelLayers call waits while one more call could fill the forest's instance grid (crash exe+0x20CF57:
 * the grid's insert 0x7AD5E0 gives 0 when all 98304 slots are taken, and the spawn writes through it). Run on a fake
 * world, forest and SpawnLevelLayers that adds trees per record. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_forest.h"
#include "xl/co_shim.h"

void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count);
void CoXlTestStreamSpawnSetup(void* spawn, uint8_t* pass);
void CoXlTestStreamDue(uint32_t i, uint64_t unit);
void CoXlTestStreamSpawnDue(void);
void CoXlTestStreamSpawned(uint32_t i, int* spawned, int* due);
void CoXlTestStreamTrees(uint32_t* holds, uint32_t* most, uint32_t* cursor);
int CoXlTestWindowTreesOver(float share);

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

static uint8_t g_context[0x100], g_pass[0x100], g_array[8 * 0xE0];
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
