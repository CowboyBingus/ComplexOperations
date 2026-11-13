/* A build's composite tiles off the game thread (proto/difficulty_world_terrain.c NearPreFor, NearPreDone, NearPreStop):
 * TerrainBuild held the game thread 35-130 ms a tile. A thread builds the run of composite steps in order into two
 * slots; a step's slot is ready only once its own tile is built there, the thread never runs more than the slots ahead
 * of the draws, a failed build reaches its step as failed, and the run's last step (or a stop) joins the thread. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestNearPreUse(int (*build)(int, uint64_t, uint8_t*, uint8_t*));
void CoXlTestNearPreSteps(int steps, int first, int last);
void* CoXlTestNearPreFor(int s);
int CoXlTestNearPreSlot(void* p, int* step, int* ok);
void CoXlTestNearPreDone(void* p, int s);
int CoXlTestNearPreOn(void);
void CoXlTestNearPreStop(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { FREE, BUILDING, BUILT };
static volatile LONG g_order[64], g_builds, g_done;
static int g_failTile = -1;
static DWORD g_gameThread;
static volatile LONG g_offThread = 1;

static int Build(int tile, uint64_t context, uint8_t* block, uint8_t* record)
{
    (void)context; (void)block;
    if (GetCurrentThreadId() == g_gameThread) InterlockedExchange(&g_offThread, 0);
    LONG n = InterlockedIncrement(&g_builds);
    if (n - g_done > 2) InterlockedExchange(&g_order[63], 1); /* more built than the slots ahead of the draws */
    if (n <= 62) g_order[n - 1] = tile;
    Sleep(5);
    memset(record, tile & 0xFF, 16);
    return tile != g_failTile;
}

/* The game thread's side of one step: wait for its slot (a frame at a time), then "draw" it and end it. */
static int Step(int s, int* ok)
{
    void* p = CoXlTestNearPreFor(s);
    if (p == NULL) return 0;
    int step = -1, waited = 0;
    while (CoXlTestNearPreSlot(p, &step, ok) != BUILT || step != s) { Sleep(1); if (++waited > 5000) return 0; }
    Sleep(3);
    InterlockedIncrement(&g_done);
    CoXlTestNearPreDone(p, s);
    return 1;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    g_gameThread = GetCurrentThreadId();

    g_case = "a run of seven composite steps: built in order off the game thread, two at most ahead of the draws";
    CoXlTestNearPreUse(Build);
    CoXlTestNearPreSteps(12, 2, 8);
    g_builds = g_done = 0;
    for (int s = 2; s <= 8; s++) { int ok = 0; CHECK(Step(s, &ok) && ok); }
    CHECK(g_builds == 7 && g_order[63] == 0 && g_offThread);
    for (int k = 0; k < 7; k++) CHECK(g_order[k] == 102 + k);
    CHECK(!CoXlTestNearPreOn()); /* the last step joined it */
    CHECK(fake_host_logged("composite tiles built off the game thread (the run is drawn): 7 built, 0 failed"));
    CHECK(CoXlTestNearPreFor(9) == NULL); /* a step after the run: built in place */

    g_case = "a tile whose build fails reaches its step as failed; the rest go on";
    CoXlTestNearPreSteps(12, 2, 5);
    g_builds = g_done = 0; g_failTile = 103;
    for (int s = 2; s <= 5; s++) { int ok = 0; CHECK(Step(s, &ok) && ok == (s != 3)); }
    CHECK(fake_host_logged("3 built, 1 failed"));
    g_failTile = -1;

    g_case = "a stop in the middle (a failed replay): the thread joined, nothing more built";
    CoXlTestNearPreSteps(12, 2, 9);
    g_builds = g_done = 0;
    { int ok = 0; CHECK(Step(2, &ok) && ok); }
    CoXlTestNearPreStop();
    CHECK(!CoXlTestNearPreOn());
    LONG after = g_builds;
    Sleep(30);
    CHECK(g_builds == after && after <= 3);

    if (g_failed) { printf("nearpre: %d check(s) failed\n", g_failed); return 1; }
    printf("nearpre: all checks passed\n");
    return 0;
}
