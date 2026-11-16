/* A moving tile's composite tiles on engine jobs (proto/difficulty_world_terrain.c NearJobTake, NearJobDone, NearJobStop):
 * TerrainBuild held the game thread 35-110 ms a composite tile. One job at a time builds the run's tiles in plan order
 * into two slots; a step draws only its own built slot; the run holds the level's tile marker and spacing from its
 * start to its end and leaves the build settings' +0x5C alone; the replay's end, a draw's fault, the last step, the
 * level's end and a level update out of play join it; without a low TLS slot or after a lost job the game thread
 * builds the tiles as before; a job that never comes back is given up on after 30 s, and while one may still be inside
 * its build no tile is built under it. The test's submit keeps the job and runs it when the test says. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestNearJobUse(int (*build)(int, uint64_t, uint8_t*, uint8_t*), uint64_t (*submit)(void*, uint64_t*, const char*, uint32_t, uint32_t, uint32_t, uint64_t));
void CoXlTestNearJobSteps(int steps, int first, int last);
void CoXlTestNearJobLevel(uintptr_t level, uint8_t* block);
int CoXlTestNearJobTake(int s, uint64_t context);
void CoXlTestNearJobDrawn(int s);
const uint8_t* CoXlTestNearJobDrawing(void);
int CoXlTestNearJobOn(long* inflight);
int CoXlTestNearRunStep(int s, uint64_t context, void* dispatch);
void CoXlTestNearJobLevelUpdate(uintptr_t level, uintptr_t moving);
void CoXlTestMoveLevelUpdate(uint64_t level, uintptr_t moving, void (*update)(uint64_t));
DWORD CoXlTestNearJobTls(DWORD k);
void CoXlTestNearJobBroken(int broken);
void CoXlTestNearJobAge(ULONGLONG ms);
int CoXlTestNearJobStuckTake(void);
void CoXlTestTerrainBuildNote(const char* text);
void CoXlTestNearJobArena(uint64_t context);
void CoXlTestNearReplayEnd(void);
void CoXlTestNearLevelLeft(int ship);
void CoXlTestNearBgQueue(int on, uint32_t* queue, uint32_t* priority);
void CoXlTestNearAutoReset(void);
void CoXlTestNearAutoMark(int shaped, int zero);
int CoXlTestNearAutoEnd(long samples);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SET5C 0x3F7AE148u
static uint8_t g_level[0x3000], g_other[0x3000], g_lg[0x3000], g_p[0x100], g_settings[0x120], g_ctx[0xE8], g_block0[0x2000];
static float g_spacing;
static uint32_t g_disk;
static uint64_t Context(void) { return (uint64_t)(uintptr_t)g_ctx; }
static uint32_t Marker(void) { uint32_t m; memcpy(&m, g_lg + 0x2D7C, 4); return m; }
static void LevelState(uint8_t* level, uint32_t state) { memcpy(level + 0x1E4, &state, 4); }
static void Restore(void) { memcpy(g_lg + 0x2D7C, &g_disk, 4); g_spacing = 18.5f; }
static long Inflight(void) { long n = 0; CoXlTestNearJobOn(&n); return n; }
static int On(void) { long n = 0; return CoXlTestNearJobOn(&n); }

/* the test's engine: one job kept until RunJob */
typedef uint64_t (*JobFn)(uint64_t, uint64_t, uint64_t, uint64_t);
static JobFn g_jobFn;
static uint64_t* g_jobData;
static volatile LONG g_pending;
static int g_submits, g_twoAtOnce, g_badQueue;
static uint32_t g_wantQueue = 1, g_wantPriority = 3; /* queue 10; 14 is (2, 4) */
static int g_auto; /* each job runs on a worker of its own 20 ms after its submit */
static HANDLE g_autoThreads[64];
static int g_autoCount;
static DWORD WINAPI Worker(LPVOID delay)
{
    Sleep((DWORD)(uintptr_t)delay);
    g_jobFn(0, 0, 0, *g_jobData);
    return 0;
}
static uint64_t Submit(void* fn, uint64_t* data, const char* name, uint32_t queue, uint32_t priority, uint32_t count, uint64_t zero)
{
    if (g_pending) g_twoAtOnce++;
    if (queue != g_wantQueue || priority != g_wantPriority || count != 1 || zero != 0 || name == NULL) g_badQueue++;
    g_jobFn = (JobFn)fn; g_jobData = data; g_submits++;
    if (g_auto && g_autoCount < 64) { g_autoThreads[g_autoCount++] = CreateThread(NULL, 0, Worker, (LPVOID)(uintptr_t)20, 0, NULL); return 0; }
    InterlockedExchange(&g_pending, 1);
    return 0;
}
static void AutoDone(void)
{
    for (int k = 0; k < g_autoCount; k++) { WaitForSingleObject(g_autoThreads[k], INFINITE); CloseHandle(g_autoThreads[k]); }
    g_autoCount = 0; g_auto = 0;
}
/* the kept job on a thread of its own (a worker) or on this one (the game thread); `delay` ms later without waiting */
static HANDLE g_late;
static int RunJob(int worker, DWORD delay)
{
    if (!g_pending) return 0;
    InterlockedExchange(&g_pending, 0);
    if (!worker) { g_jobFn(0, 0, 0, *g_jobData); return 1; }
    HANDLE t = CreateThread(NULL, 0, Worker, (LPVOID)(uintptr_t)delay, 0, NULL);
    if (delay != 0) { g_late = t; return 1; }
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 1;
}
static void LateDone(void) { if (g_late != NULL) { WaitForSingleObject(g_late, INFINITE); CloseHandle(g_late); g_late = NULL; } }

/* the test's TerrainBuild: what the run holds while a tile builds, the order, the block, a note */
static int g_order[64], g_builds, g_failTile = -1, g_tookFailed = -1, g_badMarker, g_badSpacing, g_bad5C, g_bad50;
static uint8_t* g_blocks[64];
static HANDLE g_holdBuild; /* set: a build waits on it (a job stuck inside its build) */
static volatile LONG g_inBuild;
static int Build(int tile, uint64_t context, uint8_t* block, uint8_t* record)
{
    uint64_t queue = 1;
    memcpy(&queue, (const void*)(uintptr_t)(context + 0x50), 8);
    if (queue != 0) g_bad50++; /* the level's GPU job queue is not the runner's */
    InterlockedExchange(&g_inBuild, 1);
    if (g_holdBuild != NULL) WaitForSingleObject(g_holdBuild, INFINITE);
    if (Marker() != (g_disk ^ 1u)) g_badMarker++;
    if (g_spacing != 16.0f) g_badSpacing++;
    uint64_t settings = 0; uint32_t v = 0;
    memcpy(&settings, (const void*)(uintptr_t)(context + 8), 8);
    if (settings == (uint64_t)(uintptr_t)g_settings) g_bad5C++; /* the runner's own copy */
    else { memcpy(&v, (const void*)(uintptr_t)(settings + 0x5C), 4); if (v != SET5C) g_bad5C++; }
    if (g_builds < 64) { g_order[g_builds] = tile; g_blocks[g_builds] = block; }
    g_builds++;
    memcpy(record + 0x1000, &tile, 4);
    /* the runner's arena ({head chunk, ...}, chunk {used, ...}) as an op left it: tile - 100 MiB in use, 104 the most */
    uint64_t arena = 0, head = 0, used = (uint64_t)(tile == 104 ? 9 : tile - 100) << 20;
    memcpy(&arena, (const void*)(uintptr_t)context, 8); memcpy(&head, (const void*)(uintptr_t)arena, 8);
    memcpy((void*)(uintptr_t)head, &used, 8);
    CoXlTestNearJobArena(context);
    char text[64];
    snprintf(text, sizeof text, "research: tile %d built on the job", tile);
    CoXlTestTerrainBuildNote(text);
    return tile != g_failTile;
}

/* the test's dispatcher for a step's draws: done on the second call; a fault on g_faultTile's */
static int g_drawn[64], g_draws, g_faultTile = -1, g_calls;
static uint64_t Dispatch(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    int tile = 0;
    memcpy(&tile, (const void*)(uintptr_t)(record + 0x1000), 4);
    if (tile == g_faultTile) *(volatile int*)(uintptr_t)8 = 1;
    if (++g_calls == 2) { g_calls = 0; *(volatile uint32_t*)(uintptr_t)index = 1; if (g_draws < 64) g_drawn[g_draws] = tile; g_draws++; }
    return 0;
}

static int g_updateSawOn = -1;
static void Update(uint64_t level) { (void)level; g_updateSawOn = On(); }

static void Begin(const char* name, int steps, int first, int last)
{
    g_case = name;
    g_fake.log[0] = 0;
    CoXlTestNearJobSteps(steps, first, last);
    g_builds = g_draws = g_calls = 0; g_submits = g_twoAtOnce = g_badQueue = 0; g_badMarker = g_badSpacing = g_bad5C = 0;
    g_failTile = g_faultTile = g_tookFailed = -1;
    memset(g_order, 0, sizeof g_order); memset(g_drawn, 0, sizeof g_drawn);
    LevelState(g_level, 3);
    Restore();
}

/* Steps first..last through NearJobTake as the replay calls it, a job run on a worker in each frame's wait and while
 * each step draws; 1 when every step took its own tile in order. */
static int Steps(int first, int last, int worker)
{
    int s = first, frames = 0, ok = 1;
    while (s <= last && frames++ < 200)
    {
        char note[64];
        snprintf(note, sizeof note, "research: tile %d built on the job", 100 + s);
        const int noted = fake_host_logged(note);
        int r = CoXlTestNearJobTake(s, Context());
        if (r == 0) { if (CoXlTestNearJobDrawing() != NULL) ok = 0; RunJob(worker, 0); continue; }
        if (noted || !fake_host_logged(note)) ok = 0; /* noted here, when the step takes the tile */
        if (r == 1) { if (CoXlTestNearJobDrawing() != NULL) ok = 0; g_tookFailed = s++; continue; }
        const uint8_t* rec = CoXlTestNearJobDrawing();
        int tile = 0;
        if (r != 2 || rec == NULL) return 0;
        memcpy(&tile, rec + 0x1000, 4);
        if (tile != 100 + s) ok = 0;
        if (s < last && (g_pending != 1 || Inflight() != 1)) ok = 0; /* the next one builds while this one draws */
        RunJob(worker, 0);
        CoXlTestNearJobDrawn(s);
        if (s < last && (!On() || Marker() != (g_disk ^ 1u) || g_spacing != 16.0f)) ok = 0; /* held between the steps */
        s++;
    }
    return ok && s == last + 1;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.noMatch[0] = 0x173BAB0u; /* no arena reset: the test's context has no arena */
    CoXlTestGame((uintptr_t)g_fake.image);
    float disk = 0.9375f; memcpy(&g_disk, &disk, 4);
    uint64_t v = (uint64_t)(uintptr_t)g_p; memcpy(g_lg + 0x2D0, &v, 8);
    v = (uint64_t)(uintptr_t)&g_spacing; memcpy(g_p + 0x68, &v, 8);
    for (int i = 0; i < 0x120; i++) g_settings[i] = (uint8_t)(i * 7);
    { uint32_t s5c = SET5C; memcpy(g_settings + 0x5C, &s5c, 4); }
    v = (uint64_t)(uintptr_t)g_settings; memcpy(g_ctx + 8, &v, 8);
    v = (uint64_t)(uintptr_t)g_lg; memcpy(g_ctx + 0x18, &v, 8);
    v = 0x1234; memcpy(g_ctx + 0x20, &v, 8);
    v = 0x5150; memcpy(g_ctx + 0x50, &v, 8); /* the level's job queue */
    v = (uint64_t)(uintptr_t)g_p; memcpy(g_level + 0x2C8, &v, 8); /* any generator */
    LevelState(g_other, 3);
    CoXlTestNearJobLevel((uintptr_t)g_level, g_block0);
    CoXlTestNearJobUse(Build, Submit);

    Begin("a run of five composite steps: built in plan order on a worker, one job at a time, each step its own tile", 9, 2, 6);
    CHECK(Steps(2, 6, 1));
    CHECK(g_builds == 5 && g_submits == 5 && g_twoAtOnce == 0 && g_badQueue == 0);
    for (int k = 0; k < 5; k++) CHECK(g_order[k] == 102 + k);
    CHECK(g_blocks[0] == g_block0 && g_blocks[1] != g_block0 && g_blocks[1] != NULL && g_blocks[2] == g_block0 && g_blocks[3] == g_blocks[1]);
    CHECK(g_badMarker == 0 && g_badSpacing == 0 && g_bad5C == 0 && g_bad50 == 0 && g_builds > 0);
    CHECK(!On() && Marker() == g_disk && g_spacing == 18.5f); /* put back once, after the last step */
    CHECK(fake_host_logged("composite tiles on a worker: 5 built, 0 failed, longest") && fake_host_logged("jobs seen on the game thread 0;"));
    CHECK(fake_host_logged("arena peak 9.0 MiB")); /* the most, not the last tile's */
    CHECK(fake_host_logged("(the run is drawn)"));
    CHECK(CoXlTestNearJobTake(7, Context()) == -1 && g_submits == 5); /* a record step after the run */

    Begin("switched to queue 14 (HELLFORGE_XL_NEAR_BGQUEUE=1, research): the same run, each job submitted as (2, 4)", 9, 2, 4);
    {
        uint32_t q = 0, pr = 0;
        CoXlTestNearBgQueue(1, &q, &pr);
        g_wantQueue = 2; g_wantPriority = 4;
        CHECK(Steps(2, 4, 1));
        CHECK(g_builds == 3 && g_submits == 3 && g_badQueue == 0);
        CoXlTestNearBgQueue(0, &q, &pr);
        g_wantQueue = 1; g_wantPriority = 3;
    }

    Begin("a failed build reaches its step as failed; the rest go on; jobs run on the game thread are counted", 9, 2, 5);
    g_failTile = 103;
    CHECK(Steps(2, 5, 0));
    CHECK(g_builds == 4 && g_draws == 0 && g_tookFailed == 3);
    CHECK(fake_host_logged("composite tiles on a worker: 3 built, 1 failed") && fake_host_logged("jobs seen on the game thread 4;"));

    Begin("the replay's draws: a step waits for its job, draws its own slot, and the steps' draws come in plan order", 8, 1, 4);
    g_auto = 1;
    {
        int r = 0, frames = 0, s = 1, waited = 0;
        while (s <= 4 && frames++ < 5000)
        {
            r = CoXlTestNearRunStep(s, Context(), (void*)Dispatch);
            CHECK(r >= 0);
            if (r < 0) break;
            if (r == 0 && g_calls == 0) { waited++; Sleep(1); }
            if (r == 1) s++;
        }
        AutoDone();
        CHECK(s == 5 && waited >= 1 && g_draws == 4 && g_builds == 4 && g_twoAtOnce == 0);
        for (int k = 0; k < 4; k++) CHECK(g_drawn[k] == 101 + k);
        CHECK(!On() && Marker() == g_disk && g_spacing == 18.5f);
    }

    Begin("a draw that faults joins the run (its next tile's job in flight) and puts the marker back", 8, 1, 4);
    g_faultTile = 102; g_auto = 1;
    {
        int r = 0, frames = 0, s = 1;
        while (s <= 4 && frames++ < 5000)
        {
            r = CoXlTestNearRunStep(s, Context(), (void*)Dispatch);
            if (r < 0) break;
            if (r == 0 && g_calls == 0) Sleep(1);
            if (r == 1) s++;
        }
        AutoDone();
        CHECK(r == -1 && s == 2 && !On() && Inflight() == 0 && g_builds >= 2);
        CHECK(fake_host_logged("(a draw faulted)") && Marker() == g_disk && g_spacing == 18.5f);
    }
    CoXlTestNearReplayEnd(); /* the replay fails: the blocks go */

    Begin("the replay's end joins a job in flight; it builds nothing once stopped", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && On() && Inflight() == 1 && Marker() == (g_disk ^ 1u));
    RunJob(1, 40);
    CoXlTestNearReplayEnd();
    CHECK(Inflight() == 0); /* joined, not left running */
    LateDone();
    CHECK(!On() && Inflight() == 0 && g_builds == 0 && fake_host_logged("(the replay ended)"));
    CHECK(Marker() == g_disk && g_spacing == 18.5f);

    Begin("a step out of the run's order ends it there and starts another", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    RunJob(1, 0);
    CHECK(CoXlTestNearJobTake(2, Context()) == 2);
    RunJob(1, 0);
    CoXlTestNearJobDrawn(2); /* step 4's job is submitted then */
    CHECK(Inflight() == 1);
    RunJob(1, 40);
    CHECK(CoXlTestNearJobTake(5, Context()) == 0 && fake_host_logged("(the steps left the run)") && On());
    LateDone();
    CHECK(Steps(5, 6, 1) && g_order[2] == 105 && g_order[3] == 106 && !On() && Marker() == g_disk);

    Begin("a level update in play leaves the run; one with the level out of state 3 joins it and puts the marker back", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    CoXlTestNearJobLevelUpdate((uintptr_t)g_level, (uintptr_t)g_level);
    CHECK(On() && Inflight() == 1);
    LevelState(g_level, 2);
    RunJob(1, 40);
    CoXlTestNearJobLevelUpdate((uintptr_t)g_level, (uintptr_t)g_level);
    CHECK(Inflight() == 0);
    LateDone();
    CHECK(!On() && Inflight() == 0 && g_builds == 0 && Marker() == g_disk && g_spacing == 18.5f);
    CHECK(fake_host_logged("joined at generator state 2, a job in flight yes") && fake_host_logged("(the level left play)"));

    Begin("a job run once its level left state 3 builds nothing; its step takes it as failed", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    LevelState(g_level, 2); /* before any level update saw it: skipped, not counted as a failed build */
    RunJob(1, 0);
    CHECK(g_builds == 0 && CoXlTestNearJobTake(2, Context()) == 1 && CoXlTestNearJobDrawing() == NULL);
    CHECK(fake_host_logged("composite tiles on a worker") == 0 && Inflight() == 1); /* step 3's job */
    RunJob(1, 40);
    CoXlTestNearReplayEnd();
    LateDone();
    CHECK(!On() && g_builds == 0 && fake_host_logged("0 built, 0 failed") && fake_host_logged("(the replay ended)") && Marker() == g_disk);

    Begin("the level update of another level joins the run without touching the run's level", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    RunJob(1, 0);
    CoXlTestNearJobLevelUpdate((uintptr_t)g_other, (uintptr_t)g_level);
    CHECK(!On() && fake_host_logged("joined at generator state 3, a job in flight no") && Marker() == (g_disk ^ 1u));

    Begin("the level update detour joins before the game's own update", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    RunJob(1, 0);
    LevelState(g_level, 0);
    g_updateSawOn = -1;
    CoXlTestMoveLevelUpdate((uint64_t)(uintptr_t)g_level, (uintptr_t)g_level, Update);
    CHECK(g_updateSawOn == 0 && Marker() == g_disk && fake_host_logged("joined at generator state 0"));

    Begin("the level's end joins the run and leaves the level's marker alone", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0);
    RunJob(1, 0);
    CHECK(CoXlTestNearJobTake(2, Context()) == 2 && Inflight() == 1);
    RunJob(1, 40);
    CoXlTestNearLevelLeft(1);
    CHECK(Inflight() == 0);
    LateDone();
    CHECK(!On() && Inflight() == 0 && g_builds == 1 && fake_host_logged("(the level went)") && Marker() == (g_disk ^ 1u));

    Begin("auto (research): a build the level's end cuts short is not counted at the ship's cost lines; one that ends is", 9, 2, 6);
    {
        uint32_t q = 0, pr = 0;
        CoXlTestNearAutoReset();
        CoXlTestNearAutoMark(1, 1); /* its shape line: "0 frames" */
        CoXlTestNearLevelLeft(0);
        CHECK(CoXlTestNearAutoEnd(3) == 0);
        CoXlTestNearAutoMark(1, 1);
        CHECK(CoXlTestNearAutoEnd(3) == 1);
        CoXlTestNearBgQueue(0, &q, &pr);
    }

    Begin("no TLS slot under 64: the game thread builds the tiles, nothing held", 9, 2, 6);
    {
        DWORD old = CoXlTestNearJobTls(70);
        CHECK(CoXlTestNearJobTake(2, Context()) == -1 && g_submits == 0 && !On() && Marker() == g_disk && g_spacing == 18.5f);
        CHECK(fake_host_logged("the composite tiles are built on the game thread: no TLS slot under 64"));
        CoXlTestNearJobTls(old);
    }

    Begin("a tile job lost earlier: the game thread builds the tiles", 9, 2, 6);
    CoXlTestNearJobBroken(1);
    CHECK(CoXlTestNearJobTake(2, Context()) == -1 && g_submits == 0 && !On() && Marker() == g_disk);
    CoXlTestNearJobBroken(0);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && On()); /* and jobs again once the session has them */
    RunJob(1, 0);
    CoXlTestNearReplayEnd();

    Begin("a job that never runs: given up on after 30 s; none inside, so the step builds here; the late job builds nothing", 9, 2, 6);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && g_pending == 1);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && On()); /* under 30 s it waits */
    CoXlTestNearJobAge(30001);
    CHECK(CoXlTestNearJobTake(2, Context()) == -1 && !On() && Marker() == g_disk && g_spacing == 18.5f);
    CHECK(fake_host_logged("did not return within 30 s (none is building)"));
    RunJob(1, 0);
    CHECK(g_builds == 0 && Inflight() == 0);
    CHECK(CoXlTestNearJobStuckTake() == 0);
    CoXlTestNearJobBroken(0);

    Begin("a job stuck in its build: given up on; it holds the level's block, so no composite tile is built under it", 9, 2, 6);
    g_holdBuild = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && g_pending == 1);
    RunJob(1, 1); /* on a worker, into its build, where it stays */
    InterlockedExchange(&g_inBuild, 0);
    { int k = 0; while (!g_inBuild && k++ < 2000) Sleep(1); }
    CHECK(g_inBuild == 1 && g_builds == 0);
    CoXlTestNearJobAge(30001);
    CHECK(CoXlTestNearJobTake(2, Context()) == 1 && !On());
    CHECK(fake_host_logged("did not return within 30 s (one builds in the level's block)"));
    CHECK(CoXlTestNearJobTake(3, Context()) == 1 && g_submits == 1 && fake_host_logged("composite tiles fail from now on"));
    SetEvent(g_holdBuild);
    LateDone();
    CloseHandle(g_holdBuild); g_holdBuild = NULL;
    CHECK(Inflight() == 0 && g_builds == 1 && CoXlTestNearJobStuckTake() == 2);
    CoXlTestNearJobBroken(0);
    CHECK(CoXlTestNearJobTake(2, Context()) == 0 && On()); /* jobs again once nothing holds a block */
    RunJob(1, 0);
    CoXlTestNearReplayEnd();

    if (g_failed) { printf("nearjob: %d check(s) failed\n", g_failed); return 1; }
    printf("nearjob: all checks passed\n");
    return 0;
}
