/* A rebuilt nav cell's section swap on fake game calls (living_world.c LwNavUnload / LwNavLoad): the synchronous step
 * runs on the AI update's runner with its thread count (table +0x28, +0x38) when the AI update set one of the two
 * reviewed task runners, and on a local serial runner otherwise. A load is the swap's only for the cell unloaded in the
 * same tick; each swap's line and the summaries name its parts, and the counts start again each level. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLive(int complex);
void CoXlTestLwNavSwapFakes(uintptr_t table, void* const* fn, uintptr_t jobs, uintptr_t inl);
uintptr_t CoXlTestLwNavUnload(uintptr_t w, uint32_t* id, uintptr_t cell);
uintptr_t CoXlTestLwNavLoad(uintptr_t w, uint32_t* out, uintptr_t cell);
void CoXlTestLwNavSwapFrame(void);
void CoXlTestLwNavLevel(long level);
void CoXlTestLwNavSwapSummary(ULONGLONG now);
int CoXlTestLwNavStepThreads(uintptr_t jobs, uintptr_t inl, uintptr_t runner, uintptr_t tasks, int32_t count);
void CoXlTestLwNavSwapCounts(uint32_t* swaps, uint32_t* steps, uint32_t* parallel, uint32_t* refused, uint32_t* noLoad);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define JOBS 0x7D7C20u     /* stand-ins for the two [W+0x4C8]; only compared, never called */
#define INLINE 0x814580u
#define RUNNER 0x27C6370u  /* the AI table's runner (+0x28); only passed on */
#define IDX 1
#define CELL 0x4400   /* the completion's cell (rbx at both call sites); only compared */
#define OTHER 0x4480

static uint8_t g_table[4 * 0x80] __attribute__((aligned(16)));
static uint8_t g_aiw[0x500] __attribute__((aligned(16)));
static uint8_t g_stepper[0x40] __attribute__((aligned(16)));
static int g_permits, g_joins, g_unloads, g_loads, g_makes, g_frees, g_steps, g_waits, g_commits, g_queued, g_inFlight, g_grant;
static void* g_made;
static struct { uintptr_t w; void* runner; int threads; } g_step[8];

static uint8_t FakeAsync(uintptr_t w) { (void)w; return 1; }
static uint8_t FakeInFlight(uintptr_t w) { (void)w; return (uint8_t)g_inFlight; }
static uint8_t FakeStepDone(uintptr_t w) { (void)w; return 1; }
static void FakeWait(uintptr_t w) { (void)w; g_waits++; }
static void FakeCommit(uintptr_t w, void* p, int n) { (void)w; (void)p; (void)n; g_commits++; g_inFlight = 0; }
static uintptr_t FakeStep(uintptr_t w, void* block, void* runner, int threads)
{
    (void)block;
    if (g_steps < 8) { g_step[g_steps].w = w; g_step[g_steps].runner = runner; g_step[g_steps].threads = threads; }
    g_steps++;
    return 0;
}
static void FakeMake(void* r) { g_makes++; g_made = r; }
static void FakeFree(void* r) { if (r == g_made) g_frees++; }
static int FakeQueued(uintptr_t q) { (void)q; return g_queued; }
static int FakePermit(int idx) { g_permits++; if (g_grant && idx == IDX) g_aiw[0x478] = 1; return 0; }
static void FakeJoin(int idx) { (void)idx; g_joins++; }
static uintptr_t FakeUnload(uintptr_t w, uint32_t* id, uint8_t graph) { (void)w; (void)id; (void)graph; g_unloads++; return 7; }
static uintptr_t FakeLoad(uintptr_t w, uint32_t* out, uintptr_t inst, void* p4, uintptr_t graph, void* p6)
{ (void)w; (void)out; (void)inst; (void)p4; (void)graph; (void)p6; g_loads++; return 9; }

static void Put(uint8_t* at, uintptr_t v) { memcpy(at, &v, 8); }

/* A complex level's AI world at IDX with a running AI job, 3 queued changes, the AI update's count `threads`, a stepper
 * count of 2 the step must not take, and [W+0x4C8] `tasks`. */
static void Reset(const char* name, int32_t threads, uintptr_t tasks, uintptr_t jobs, uintptr_t inl)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    memset(g_table, 0, sizeof g_table); memset(g_aiw, 0, sizeof g_aiw); memset(g_stepper, 0, sizeof g_stepper);
    memset(g_step, 0, sizeof g_step);
    g_permits = g_joins = g_unloads = g_loads = g_makes = g_frees = g_steps = g_waits = g_commits = g_inFlight = 0;
    g_queued = 3; g_grant = 1; g_made = NULL;
    Put(g_table + 0x80 * IDX, (uintptr_t)g_aiw);
    Put(g_table + 0x80 * IDX + 0x20, 1);
    Put(g_table + 0x80 * IDX + 0x28, RUNNER);
    Put(g_aiw + 0x4A0, 0x5000);
    Put(g_aiw + 0x4A8, (uintptr_t)g_stepper);
    Put(g_aiw + 0x4C8, tasks);
    memcpy(g_table + 0x80 * IDX + 0x38, &threads, 4);
    int32_t stepperCount = 2; memcpy(g_stepper + 0x28, &stepperCount, 4);
    void* const fn[13] = {(void*)FakeUnload, (void*)FakeLoad, (void*)FakeAsync, (void*)FakeInFlight, (void*)FakeStepDone, (void*)FakeWait,
                          (void*)FakeCommit, (void*)FakeStep, (void*)FakeMake, (void*)FakeFree, (void*)FakeQueued, (void*)FakePermit, (void*)FakeJoin};
    CoXlTestLwNavSwapFakes((uintptr_t)g_table, fn, jobs, inl);
    CoXlTestLive(1);
    CoXlTestLwNavLevel(1);
}

static void Counts(uint32_t* swaps, uint32_t* steps, uint32_t* parallel, uint32_t* refused, uint32_t* noLoad)
{ CoXlTestLwNavSwapCounts(swaps, steps, parallel, refused, noLoad); }

/* One completion of CELL in one tick: its unload, then its load. */
static void Swap(uint32_t section)
{
    uint32_t id = section, out = section;
    CHECK(CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, CELL) == 7);
    CHECK(CoXlTestLwNavLoad((uintptr_t)g_aiw, &out, CELL) == 9);
    CoXlTestLwNavSwapFrame();
}

static void TestThreads(void)
{
    g_case = "the step's threads";
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 12) == 12);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, INLINE, 12) == 12);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, 0x123456, 12) == 1); /* a [W+0x4C8] the AI update did not set */
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, 0, JOBS, 12) == 1);          /* no runner */
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 1) == 1);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 0) == 1);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, -5) == 1);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 2) == 2);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 100) == 100); /* workers + 4 on a big PC: the game's value */
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 256) == 256);
    CHECK(CoXlTestLwNavStepThreads(JOBS, INLINE, RUNNER, JOBS, 257) == 1);   /* not a count the game makes: serial, never cut */
    CHECK(CoXlTestLwNavStepThreads(0, 0, RUNNER, 0, 12) == 1);                /* not the reviewed code: no dispatchers */
    CHECK(CoXlTestLwNavStepThreads(0, 0, RUNNER, JOBS, 12) == 1);
}

static void TestParallel(void)
{
    Reset("a swap's steps on the AI update's runner", 12, JOBS, JOBS, INLINE);
    Swap(5);
    CHECK(g_unloads == 1 && g_loads == 1 && g_joins == 2);
    CHECK(g_steps == 2 && g_makes == 0 && g_frees == 0); /* before the unload and after the load */
    for (int k = 0; k < 2; k++) CHECK(g_step[k].w == (uintptr_t)g_aiw && g_step[k].runner == (void*)(uintptr_t)RUNNER && g_step[k].threads == 12);
    CHECK(g_aiw[0x478] == 0); /* the permit handed back */
    uint32_t swaps, steps, parallel, refused, noLoad;
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 1 && steps == 2 && parallel == 2 && refused == 0);
    CHECK(fake_host_logged("living world: nav: section 5 swapped in at once (world 1): "));
    CHECK(fake_host_logged("1 step(s) on 12 thread(s); 1 swaps this level, worst "));
    Reset("the inline AI update's runner", 6, INLINE, JOBS, INLINE);
    Swap(6);
    CHECK(g_steps == 2 && g_step[1].runner == (void*)(uintptr_t)RUNNER && g_step[1].threads == 6 && g_makes == 0);
    Reset("an in-flight step is waited for and committed first", 12, JOBS, JOBS, INLINE);
    g_inFlight = 1;
    Swap(7);
    CHECK(g_waits == 1 && g_commits == 1 && g_steps == 2);
    Reset("nothing queued: no step", 12, JOBS, JOBS, INLINE);
    g_queued = 0;
    Swap(8);
    CHECK(g_steps == 0 && fake_host_logged("0 step(s) on "));
}

static void TestSerial(void)
{
    Reset("an unknown task runner keeps the step on the frame thread", 12, 0x123456, JOBS, INLINE);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 2 && g_frees == 2);
    for (int k = 0; k < 2; k++) CHECK(g_step[k].runner != (void*)(uintptr_t)RUNNER && g_step[k].threads == 1);
    CHECK(fake_host_logged("1 step(s) on 1 thread(s)"));
    Reset("unreviewed code: no dispatchers", 12, JOBS, 0, 0);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 2 && g_step[1].threads == 1);
    Reset("a count of one", 1, JOBS, JOBS, INLINE);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 2 && g_step[1].threads == 1);
    Reset("no count", 0, JOBS, JOBS, INLINE);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 2 && g_step[1].threads == 1);
    uint32_t swaps, steps, parallel, refused, noLoad;
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(parallel == 0 && steps == 2);
    Reset("a count the game would not pass", 257, JOBS, JOBS, INLINE);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 2 && g_step[1].threads == 1);
    Reset("the most the game could pass", 256, JOBS, JOBS, INLINE);
    Swap(5);
    CHECK(g_steps == 2 && g_makes == 0 && g_step[1].threads == 256);
}

/* 0x7B8A10 can unload without loading: that unload's state ends with the tick or at another cell's load, and no later
 * load counts it as a swap or charges its gap. */
static void TestNoLoad(void)
{
    uint32_t swaps, steps, parallel, refused, noLoad, id = 5, out = 6;
    Reset("an unload with no load, then another cell's first build in the same tick", 12, JOBS, JOBS, INLINE);
    CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, CELL);
    CoXlTestLwNavLoad((uintptr_t)g_aiw, &out, OTHER);
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 0 && noLoad == 1 && g_steps == 1 && g_joins == 1); /* the drain before the unload only */
    CHECK(!fake_host_logged("swapped in at once"));
    Reset("an unload with no load, then the same cell's load a tick later", 12, JOBS, JOBS, INLINE);
    CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, CELL);
    CoXlTestLwNavSwapFrame();
    CoXlTestLwNavLoad((uintptr_t)g_aiw, &out, CELL);
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 0 && noLoad == 1 && g_steps == 1 && !fake_host_logged("swapped in at once"));
    CoXlTestLwNavSwapFrame(); /* nothing left to end */
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(noLoad == 1);
    Reset("a swap after one with no load is whole", 12, JOBS, JOBS, INLINE);
    CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, OTHER);
    CoXlTestLwNavSwapFrame();
    Swap(7);
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 1 && noLoad == 1 && fake_host_logged("section 7 swapped in at once") && fake_host_logged("1 step(s) on 12 thread(s)"));
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("living world: nav swaps (30 s): 1 on the frame thread, "));
    CHECK(fake_host_logged("this level); 1 unload(s) with no load"));
}

/* The counts and the line cap are a level's: a new level notes the last one's summary and starts them again. */
static void TestLevel(void)
{
    uint32_t swaps, steps, parallel, refused, noLoad;
    Reset("a new level", 12, JOBS, JOBS, INLINE);
    CoXlTestLwNavSwapSummary(1000);
    for (int i = 0; i < 400; i++) { g_fake.log[0] = 0; Swap(5); }
    CHECK(fake_host_logged("400 swap lines this level"));
    g_fake.log[0] = 0;
    Swap(6);
    CHECK(!fake_host_logged("swapped in at once"));
    CoXlTestLwNavLevel(2);
    CoXlTestLwNavSwapSummary(2000); /* before the 30 s: the level's last summary all the same */
    CHECK(fake_host_logged("living world: nav swaps (the level's last): 401 on the frame thread, "));
    CHECK(fake_host_logged("(the last on 12 thread(s); 802 of 802 steps on the AI update's runner this level)"));
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 0 && steps == 0 && parallel == 0 && noLoad == 0);
    g_fake.log[0] = 0;
    Swap(8);
    CHECK(fake_host_logged("section 8 swapped in at once") && fake_host_logged("1 swaps this level, worst "));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(3000);
    CHECK(!fake_host_logged("nav swaps (")); /* the same level, 30 s not out */
    Reset("a new level seen first by an unload", 12, JOBS, JOBS, INLINE);
    Swap(5); Swap(5);
    CoXlTestLwNavLevel(3);
    Swap(9);
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 1 && steps == 2 && fake_host_logged("nav swaps (the level's last): 2 on the frame thread"));
}

static void TestNotLiveOrRefused(void)
{
    Reset("a normal mission's swaps stay the game's", 12, JOBS, JOBS, INLINE);
    CoXlTestLive(0);
    Swap(5);
    CHECK(g_joins == 0 && g_permits == 0 && g_steps == 0 && !fake_host_logged("swapped in at once"));
    Reset("no permit: nothing done, the marks held", 12, JOBS, JOBS, INLINE);
    g_grant = 0;
    Swap(5);
    uint32_t swaps, steps, parallel, refused, noLoad;
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(g_steps == 0 && swaps == 0 && refused == 1 && g_unloads == 1 && g_loads == 1);
    CHECK(!fake_host_logged("swapped in at once"));
}

static void TestLinesAndSummary(void)
{
    Reset("the 30 s summary", 12, JOBS, JOBS, INLINE);
    CoXlTestLwNavSwapSummary(1000); /* starts the clock */
    Swap(5); Swap(6);
    CoXlTestLwNavSwapSummary(20000);
    CHECK(!fake_host_logged("nav swaps (30 s)"));
    CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("living world: nav swaps (30 s): 2 on the frame thread, "));
    CHECK(fake_host_logged("(the last on 12 thread(s); 4 of 4 steps on the AI update's runner this level); 0 unload(s) with no load"));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(62000); /* none since: no line */
    CHECK(!fake_host_logged("nav swaps (30 s)"));
    Reset("swap lines a level", 12, JOBS, JOBS, INLINE);
    for (int i = 0; i < 400; i++) { g_fake.log[0] = 0; Swap(5); }
    CHECK(fake_host_logged("section 5 swapped in at once") && fake_host_logged("living world: nav: 400 swap lines this level; the 30 s summaries go on"));
    g_fake.log[0] = 0;
    Swap(9);
    CHECK(!fake_host_logged("swapped in at once"));
    uint32_t swaps, steps, parallel, refused, noLoad;
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 401);
}

int main(void)
{
    TestThreads();
    TestParallel();
    TestSerial();
    TestNotLiveOrRefused();
    TestLinesAndSummary();
    TestNoLoad();
    TestLevel();
    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("navswap: all checks passed\n");
    return 0;
}
