/* The engine's destroy queue (exe 0x12D2D0) on the fake exe: on a normal mission and on the ship the game's enqueue gets
 * exactly the caller's handles; on a complex mission a unit not made yet waits, one gone is dropped and a full list
 * holds. What waits at a complex mission's end never reaches the next level's world. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLive(int complex);
int CoXlTestDestroyInstall(void);
void CoXlTestDestroyTick(void);
LONG CoXlTestDestroyWaiting(void);
void CoXlTestMission(uintptr_t level, int complex);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define QUEUE 0x12D2D0u
#define TABLE 0x1A100F0u
#define LOCK_SHARED 0x13F6760u
#define UNLOCK_SHARED 0x13F6758u
#define UNITS 8u

typedef void (*QueueFn)(uint64_t, uint32_t*, uint32_t);

/* What the game's enqueue was handed, call by call. */
static struct { uint64_t world; uint32_t* handles; uint32_t n, copy[16]; } g_calls[16];
static int g_callCount, g_locks;
static void TestQueue(uint64_t world, uint32_t* handles, uint32_t n)
{
    if (g_callCount == 16) return;
    g_calls[g_callCount].world = world; g_calls[g_callCount].handles = handles; g_calls[g_callCount].n = n;
    for (uint32_t i = 0; i < n && i < 16; i++) g_calls[g_callCount].copy[i] = handles[i];
    g_callCount++;
}
static void TestLock(void* lock) { (void)lock; g_locks++; }
static void TestUnlock(void* lock) { (void)lock; }

/* The unit table's layout (0x12D310..0x12D359): pointers +0x88, count +0x98, generation bytes +0xA0, lock +0xD0. */
static uint8_t g_table[0x100], g_gens[UNITS];
static uint64_t g_units[UNITS], g_unit;
static uint8_t* g_world;

static uint32_t Handle(uint32_t index) { return (uint32_t)g_gens[index] << 22 | index; }
static void ListCount(int32_t count) { memcpy(g_world + 0x10030, &count, 4); }
static void Reset(void) { g_callCount = 0; memset(g_calls, 0, sizeof g_calls); }

int main(void)
{
    g_case = "the hook's install";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    g_world = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL) { printf("destroy: no memory\n"); return 1; }
    const uint64_t table = (uint64_t)(uintptr_t)g_table, lock = (uint64_t)(uintptr_t)TestLock, unlock = (uint64_t)(uintptr_t)TestUnlock;
    const uint64_t pointers = (uint64_t)(uintptr_t)g_units, gens = (uint64_t)(uintptr_t)g_gens;
    const uint32_t count = UNITS;
    memcpy(exe + TABLE, &table, 8); memcpy(exe + LOCK_SHARED, &lock, 8); memcpy(exe + UNLOCK_SHARED, &unlock, 8);
    memcpy(g_table + 0x88, &pointers, 8); memcpy(g_table + 0x98, &count, 4); memcpy(g_table + 0xA0, &gens, 8);
    g_fake.originals[0].target = (uintptr_t)exe + QUEUE; g_fake.originals[0].original = (void*)TestQueue;
    CoXlTestMission(0, 0);
    CHECK(CoXlTestDestroyInstall());
    QueueFn queue = (QueueFn)fake_host_detour((uintptr_t)exe + QUEUE - (uintptr_t)g_fake.image);
    CHECK(queue != NULL);
    if (queue == NULL) { printf("destroy: %d checks failed\n", g_failed); return 1; }

    /* units 0 and 3 live, 1 handed out with no unit yet, 2 gone (its generation moved on) */
    const uint64_t world = (uint64_t)(uintptr_t)g_world;
    g_units[0] = g_units[3] = (uint64_t)(uintptr_t)&g_unit;
    g_gens[2] = 5;
    uint32_t mixed[5] = {Handle(0), Handle(1), (4u << 22) | 2u, Handle(3), 9u}; /* 9: past the table's count */
    const uint32_t kept[5] = {mixed[0], mixed[1], mixed[2], mixed[3], mixed[4]};

    const struct { const char* name; uintptr_t level; } normal[] = {{"a normal mission", 0x1000}, {"the ship", 0}};
    for (int k = 0; k < 2; k++)
    {
        g_case = normal[k].name;
        CoXlTestMission(normal[k].level, 0);
        ListCount(8100); /* past the room the complex path keeps */
        Reset(); g_locks = 0;
        queue(world, mixed, 5);
        CHECK(g_callCount == 1 && g_calls[0].world == world && g_calls[0].handles == mixed && g_calls[0].n == 5);
        CHECK(memcmp(g_calls[0].copy, kept, sizeof kept) == 0 && memcmp(mixed, kept, sizeof kept) == 0);
        CHECK(g_locks == 0 && CoXlTestDestroyWaiting() == 0); /* no look at the unit table */
        Reset();
        queue(world, NULL, 0);
        CHECK(g_callCount == 1 && g_calls[0].handles == NULL && g_calls[0].n == 0);
        Reset();
        CoXlTestDestroyTick();
        CHECK(g_callCount == 0);
    }
    CHECK(!fake_host_logged("a destroy asked for unit") && !fake_host_logged("held until the world's destroy list had room"));

    g_case = "a complex mission: all live, room for them";
    CoXlTestMission(0x2000, 1);
    ListCount(10);
    uint32_t live[2] = {Handle(0), Handle(3)};
    Reset();
    queue(world, live, 2);
    CHECK(g_callCount == 1 && g_calls[0].handles == live && g_calls[0].n == 2);

    g_case = "a complex mission: a unit not made yet waits, one gone is dropped";
    Reset();
    queue(world, mixed, 5);
    CHECK(g_callCount == 1 && g_calls[0].n == 2 && g_calls[0].copy[0] == Handle(0) && g_calls[0].copy[1] == Handle(3));
    CHECK(CoXlTestDestroyWaiting() == 1 && g_locks > 0);
    CHECK(fake_host_logged("a destroy asked for unit") && fake_host_logged("which is not made yet") && fake_host_logged("which is gone"));
    Reset();
    CoXlTestDestroyTick();
    CHECK(g_callCount == 0 && CoXlTestDestroyWaiting() == 1);
    g_units[1] = (uint64_t)(uintptr_t)&g_unit; /* the frame's drain made it */
    CoXlTestDestroyTick();
    CHECK(g_callCount == 1 && g_calls[0].world == world && g_calls[0].n == 1 && g_calls[0].copy[0] == Handle(1) && CoXlTestDestroyWaiting() == 0);

    g_case = "a complex mission: a full list holds a live unit's destroy";
    ListCount(8000);
    Reset();
    queue(world, live, 2);
    CHECK(g_callCount == 0 && CoXlTestDestroyWaiting() == 2 && fake_host_logged("held until the world's destroy list had room"));
    ListCount(0);
    CoXlTestDestroyTick();
    CHECK(g_callCount == 2 && CoXlTestDestroyWaiting() == 0);

    g_case = "what waits at a complex mission's end";
    g_units[1] = 0;
    uint32_t pending[1] = {Handle(1)};
    queue(world, pending, 1);
    ListCount(8000);
    queue(world, live, 2);
    CHECK(CoXlTestDestroyWaiting() == 3);
    CoXlTestMission(0, 0); /* the ship */
    ListCount(0);
    g_units[1] = (uint64_t)(uintptr_t)&g_unit;
    Reset();
    CoXlTestDestroyTick();
    CHECK(g_callCount == 0 && CoXlTestDestroyWaiting() == 0);
    CoXlTestMission(0x3000, 0);
    CoXlTestDestroyTick();
    CHECK(g_callCount == 0);

    if (g_failed) { printf("destroy: %d checks failed\n", g_failed); return 1; }
    printf("destroy: all cases passed\n");
    return 0;
}
