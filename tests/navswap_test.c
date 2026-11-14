/* A rebuilt nav cell's section swap on fake game calls (living_world.c LwNavUnload / LwNavLoad): the synchronous step
 * runs on the AI update's runner with its thread count (table +0x28, +0x38) when the AI update set one of the two
 * reviewed task runners, and on a local serial runner otherwise. A load is the swap's only for the cell unloaded in the
 * same tick; each swap's line and the summaries name its parts, and the counts start again each level.
 *
 * The hold (LwNavPop): the tick's pop on a fake list. A built cell's completion is taken only with its AI world idle
 * and then swaps with no drain; a world's later completions wait until that swap is applied and committed (the queue's
 * buffer changed and no step in flight); after 100 ms a built cell goes with other changes queued; a level's end
 * keeps the game's order and the drain; a world's records that wait 2000 ms get its step finished once, noted. */
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
void CoXlTestLwNavHold(uintptr_t worlds, void (*enter)(uintptr_t), void (*leave)(uintptr_t));
uintptr_t CoXlTestLwNavPop(uintptr_t list, uint32_t flush, ULONGLONG now);
int CoXlTestLwNavHoldBusy(void);
int CoXlTestLwCutNote(uintptr_t exe, uintptr_t at, uintptr_t bad, const CONTEXT* c);
void CoXlTestLwCutReset(void);
void CoXlTestLwNavRing(uintptr_t ring, volatile uint32_t* next);
void CoXlTestLwNavHoldCounts(uint32_t* v);
int CoXlTestLwNavWatch(uint16_t cx, uint16_t cy, uint32_t flags, uint32_t section);
float CoXlTestLwAibSteer(float stock, float cur, float lat);
float CoXlTestLwAibWorld(uintptr_t w, float stock, float lat, int on, ULONGLONG now);
int CoXlTestLwNavLeft(int read, uint32_t state);
void CoXlTestLwFpJoin(double ms);

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
/* the hold's cases: a second AI world, queues that the unload / load fill and a step empties, and the list's lock */
#define IDX2 2
#define Q2 0x6000u
static uint8_t g_aiw2[0x500] __attribute__((aligned(16)));
static int g_model, g_queued2, g_locked, g_enters, g_leaves, g_lockedCalls;
/* the test's own account: a world's swap / any of its section commands queued or in the step in flight; whether the
 * step in flight took them */
static int g_unapplied[4], g_section[4], g_took;
static void Took(int world) { g_unapplied[world] = g_section[world] = 0; }
/* a step's start takes a queue that holds something and gives it a new buffer ([W+0x498]) */
static void Take(uint8_t* w) { uintptr_t b; memcpy(&b, w + 0x498, 8); b += 0x100; memcpy(w + 0x498, &b, 8); }
static void Flight(int on);
static void* g_made;
static struct { uintptr_t w; void* runner; int threads; } g_step[8];

static uint8_t FakeAsync(uintptr_t w) { (void)w; return 1; }
static uint8_t FakeInFlight(uintptr_t w) { (void)w; if (g_locked) g_lockedCalls++; return (uint8_t)g_inFlight; }
static uint8_t FakeStepDone(uintptr_t w) { (void)w; return 1; }
static void FakeWait(uintptr_t w) { (void)w; g_waits++; if (g_locked) g_lockedCalls++; }
static void FakeCommit(uintptr_t w, void* p, int n) { (void)w; (void)p; (void)n; g_commits++; Flight(0); if (g_took) Took(IDX); g_took = 0; if (g_locked) g_lockedCalls++; }
static uintptr_t FakeStep(uintptr_t w, void* block, void* runner, int threads)
{
    (void)block;
    if (g_steps < 8) { g_step[g_steps].w = w; g_step[g_steps].runner = runner; g_step[g_steps].threads = threads; }
    g_steps++;
    if (g_model && w == (uintptr_t)g_aiw2) { if (g_queued2) Take(g_aiw2); g_queued2 = 0; Took(IDX2); }
    else if (g_model) { if (g_queued) Take(g_aiw); g_queued = 0; Took(IDX); }
    if (g_locked) g_lockedCalls++;
    return 0;
}
static void FakeMake(void* r) { g_makes++; g_made = r; }
static void FakeFree(void* r) { if (r == g_made) g_frees++; }
static int FakeQueued(uintptr_t q) { return q == Q2 ? g_queued2 : g_queued; }
static int FakePermit(int idx) { g_permits++; if (g_grant && idx == IDX) g_aiw[0x478] = 1; if (g_grant && idx == IDX2) g_aiw2[0x478] = 1; return 0; }
static void FakeJoin(int idx) { (void)idx; g_joins++; if (g_locked) g_lockedCalls++; }
static void Queue(uintptr_t w) { if (g_model) { if (w == (uintptr_t)g_aiw2) g_queued2++; else g_queued++; } }
static uintptr_t FakeUnload(uintptr_t w, uint32_t* id, uint8_t graph) { (void)id; (void)graph; g_unloads++; Queue(w); return 7; }
static uint8_t* g_slots; static int g_slotsMove; /* the collection a load reserves its slot in; the array moves when full */
static uintptr_t FakeLoad(uintptr_t w, uint32_t* out, uintptr_t inst, void* p4, uintptr_t graph, void* p6)
{
    (void)out; (void)inst; (void)p4; (void)graph; (void)p6; g_loads++; Queue(w);
    if (g_slots) { int32_t n; memcpy(&n, g_slots + 0x40, 4); n++; memcpy(g_slots + 0x40, &n, 4); if (g_slotsMove) { uintptr_t a = 0x2000; memcpy(g_slots + 0x38, &a, 8); } }
    return 9;
}
static void FakeEnter(uintptr_t list) { (void)list; g_enters++; g_locked = 1; }
static void FakeLeave(uintptr_t list) { (void)list; g_leaves++; g_locked = 0; }

static void Put(uint8_t* at, uintptr_t v) { memcpy(at, &v, 8); }
/* the step in flight as the hooks' calls and the hold's read of [W+0x4B0] see it */
static void Flight(int on) { g_inFlight = on; Put(g_aiw + 0x4B0, (uintptr_t)on); }

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
    g_model = g_queued2 = g_locked = g_enters = g_leaves = g_lockedCalls = 0;
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
    CHECK(fake_host_logged("1 step(s) on 12 thread(s); held 0 tick(s), 0 other change(s) queued; 1 swaps this level, worst "));
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
    Reset("drain lines a level", 12, JOBS, JOBS, INLINE);
    for (int i = 0; i < 31; i++) Swap(5);
    CHECK(!fake_host_logged("drain lines this level"));
    Swap(5);
    CHECK(fake_host_logged("living world: nav: 64 drain lines this level; the 30 s summaries go on"));
    g_fake.log[0] = 0;
    Swap(5);
    CHECK(!fake_host_logged("a step finished on the frame thread") && fake_host_logged("swapped in at once"));
    CoXlTestLwNavLevel(2);
    Swap(5);
    CHECK(fake_host_logged("a step finished on the frame thread (before an unload")); /* a new level: its own lines */
    Reset("swap lines a level", 12, JOBS, JOBS, INLINE);
    for (int i = 0; i < 400; i++) { g_fake.log[0] = 0; Swap(5); }
    g_fake.log[0] = 0;
    Swap(9);
    CHECK(!fake_host_logged("swapped in at once"));
    uint32_t swaps, steps, parallel, refused, noLoad;
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(swaps == 401);
}

/* ---- the hold ---- */
#define NEXT 0x10        /* the list's link offset ([list+0x38]) */
#define RECS 16
#define CELLS 16
#define STRIDE 0x70
static uint8_t g_list[0x40] __attribute__((aligned(16)));
static uint8_t g_rec[RECS][0x20] __attribute__((aligned(16)));
static uint8_t g_nav[2][0x6800] __attribute__((aligned(16)));
static uint8_t g_cells[2][CELLS * STRIDE] __attribute__((aligned(16)));
static uintptr_t g_navs[4], g_navsVar;
static int g_done[64], g_dones, g_broken;

static uintptr_t Get(const uint8_t* at) { uintptr_t v; memcpy(&v, at, 8); return v; }
static void Section(int world, int cell, int32_t id) { memcpy(g_cells[world == IDX2] + cell * STRIDE + 0x60, &id, 4); }
static void NavState(int world, uint32_t state) { memcpy(g_nav[world == IDX2] + 0x20, &state, 4); }
static void AiUpdate(int on) { int32_t v = on; memcpy(g_table + 0x80 * IDX + 0x54, &v, 4); }
/* A completion pushed as the build threads push it: to the head (0x7B2690). */
static uintptr_t Push(int r, int world, int cell, uint32_t flags)
{
    uint8_t* rec = g_rec[r];
    memset(rec, 0, sizeof g_rec[r]);
    uint32_t c = (uint32_t)cell, f = flags | (uint32_t)world;
    memcpy(rec + 8, &c, 4); memcpy(rec + 0xC, &f, 4);
    Put(rec + NEXT, Get(g_list + 0x28));
    if (!Get(g_list + 0x30)) Put(g_list + 0x30, (uintptr_t)rec);
    Put(g_list + 0x28, (uintptr_t)rec);
    return (uintptr_t)rec;
}
/* A complex level in play with two AI worlds, both idle, their navs running, every cell never built, an empty list. */
static void ResetHold(const char* name)
{
    Reset(name, 12, JOBS, JOBS, INLINE);
    g_model = 1; g_queued = 0; g_locked = 1; g_dones = g_broken = 0;
    memset(g_unapplied, 0, sizeof g_unapplied); memset(g_section, 0, sizeof g_section); memset(g_done, 0, sizeof g_done); g_took = 0;
    memset(g_list, 0, sizeof g_list); memset(g_rec, 0, sizeof g_rec); memset(g_nav, 0, sizeof g_nav); memset(g_aiw2, 0, sizeof g_aiw2);
    uint32_t off = NEXT; memcpy(g_list + 0x38, &off, 4);
    Put(g_table + 0x80 * IDX2, (uintptr_t)g_aiw2);
    Put(g_aiw2 + 0x4A0, Q2); Put(g_aiw2 + 0x4A8, (uintptr_t)g_stepper);
    memset(g_navs, 0, sizeof g_navs);
    for (int n = 0; n < 2; n++)
    {
        uint16_t stride = STRIDE;
        memcpy(g_nav[n] + 0x6736, &stride, 2); Put(g_nav[n] + 0x6748, (uintptr_t)g_cells[n]);
        g_navs[n ? IDX2 : IDX] = (uintptr_t)g_nav[n];
        NavState(n ? IDX2 : IDX, 3);
        for (int c = 0; c < CELLS; c++) Section(n ? IDX2 : IDX, c, -1);
    }
    g_navsVar = (uintptr_t)g_navs;
    CoXlTestLwNavHold((uintptr_t)&g_navsVar, FakeEnter, FakeLeave);
    CoXlTestLwNavRing(0, 0);
}
/* The game's work on a popped record: a built cell's unload, then every cell's load. In play it must never meet a swap
 * of its world that is not applied yet, and a built cell's must find no step in flight and no section command of its
 * world unapplied (g_broken). */
static void Process(uintptr_t rec, int play)
{
    uint32_t cell, flags, id, out;
    memcpy(&cell, (uint8_t*)rec + 8, 4); memcpy(&flags, (uint8_t*)rec + 0xC, 4);
    g_locked = 0;
    if (g_dones < 64) g_done[g_dones++] = (int)((rec - (uintptr_t)g_rec) / sizeof g_rec[0]);
    if (!(flags & 0x80))
    {
        int world = (int)(flags & 0x7F), two = world == IDX2;
        uintptr_t w = two ? (uintptr_t)g_aiw2 : (uintptr_t)g_aiw, at = (uintptr_t)g_cells[two] + cell * STRIDE;
        int32_t section; memcpy(&section, (uint8_t*)at + 0x60, 4);
        id = (uint32_t)section; out = 100 + cell;
        if (play && g_unapplied[world]) g_broken++;
        if (section != -1)
        {
            if (play && ((two ? 0 : g_inFlight) || g_section[world])) g_broken++;
            CoXlTestLwNavUnload(w, &id, at);
            g_unapplied[world] = 1;
        }
        CoXlTestLwNavLoad(w, &out, at);
        g_section[world] = 1; if (!two) g_took = 0;
        Section(world, (int)cell, (int32_t)out);
    }
    g_locked = 1;
}
/* One tick of the game at `now` ms: every record the pop gives, then the mod's tick. How many it took. */
static int Tick(uint32_t flush, ULONGLONG now)
{
    int n = 0;
    for (uintptr_t rec; (rec = CoXlTestLwNavPop((uintptr_t)g_list, flush, now)) != 0; n++) Process(rec, !flush);
    CoXlTestLwNavSwapFrame();
    return n;
}
/* The AI update's driver for world IDX (0xBD1890): a step starts when something is queued and commits a frame later. */
static void Driver(void)
{
    if (g_inFlight) { Flight(0); if (g_took) Took(IDX); g_took = 0; }
    else if (g_queued) { g_queued = 0; Take(g_aiw); g_took = 1; Flight(1); }
}
/* A step of world IDX that starts and commits inside one driver call. */
static void StepAtOnce(void) { if (g_queued) Take(g_aiw); g_queued = 0; Took(IDX); }
static void HoldCounts(uint32_t* v) { CoXlTestLwNavHoldCounts(v); }

static void TestPopOrder(void)
{
    ResetHold("records of no world's rules leave oldest first, the list kept whole");
    uintptr_t a = Push(0, IDX, 0, 0x80), b = Push(1, IDX, 1, 0x80), c = Push(2, 9, 2, 0); /* c: a world the table has not */
    CHECK(Get(g_list + 0x28) == c && Get(g_list + 0x30) == a);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == a);
    CHECK(Get(g_list + 0x28) == c && Get(g_list + 0x30) == b && Get(g_rec[1] + NEXT) == 0 && Get(g_rec[0] + NEXT) == 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == b);
    CHECK(Get(g_list + 0x28) == c && Get(g_list + 0x30) == c);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == c);
    CHECK(Get(g_list + 0x28) == 0 && Get(g_list + 0x30) == 0 && Get(g_rec[2] + NEXT) == 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == 0);
    ResetHold("a record from the middle");
    Section(IDX, 0, 40); Flight(1);
    a = Push(0, IDX, 0, 0); b = Push(1, IDX, 1, 0x80); c = Push(2, IDX, 2, 0); /* a waits for the step, c behind it */
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == b);
    CHECK(Get(g_list + 0x28) == c && Get(g_rec[2] + NEXT) == a && Get(g_list + 0x30) == a && Get(g_rec[1] + NEXT) == 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == 0);
    CHECK(Get(g_list + 0x28) == c && Get(g_list + 0x30) == a);
    ResetHold("the head taken with others left");
    Flight(1);
    Section(IDX, 0, 40);
    a = Push(0, IDX, 0, 0); b = Push(1, IDX2, 1, 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == b); /* world 2's first build; world 1's built cell waits */
    CHECK(Get(g_list + 0x28) == a && Get(g_list + 0x30) == a);
    ResetHold("a list longer than the nav's jobs make keeps the game's order");
    static uint8_t many[4100][0x20];
    memset(many, 0, sizeof many);
    for (int i = 0; i < 4100; i++)
    {
        uint32_t f = IDX; memcpy(many[i] + 0xC, &f, 4);
        Put(many[i] + NEXT, Get(g_list + 0x28));
        if (!Get(g_list + 0x30)) Put(g_list + 0x30, (uintptr_t)many[i]);
        Put(g_list + 0x28, (uintptr_t)many[i]);
    }
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == (uintptr_t)many[4099]);
}

static void TestHoldIdle(void)
{
    uint32_t v[8], swaps, steps, parallel, refused, noLoad;
    ResetHold("a built cell's completion with the world idle: taken at once, no drain");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_done[0] == 0 && g_broken == 0);
    CHECK(g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0 && g_enters == 0 && g_leaves == 0);
    CHECK(g_permits == 1 && g_aiw[0x478] == 0); /* the query jobs asked about once, the permit handed back */
    CHECK(g_unloads == 1 && g_loads == 1 && g_queued == 2); /* the swap is left for the game's next async step */
    CHECK(fake_host_logged("living world: nav: section 103 swapped in with the world idle, for its next step (world 1): "));
    CHECK(fake_host_logged("before the unload 0.0 (AI update 0.0, permit 0.0, step in flight 0.0, its commit 0.0, queued 0.0), the game's unload "));
    CHECK(fake_host_logged("after the load 0.0 (AI update 0.0, step in flight 0.0, the swap's step 0.0); 0 step(s) on 0 thread(s); held 0 tick(s), 0 other change(s) queued; 1 swaps this level"));
    CHECK(!fake_host_logged("swapped in at once"));
    HoldCounts(v); Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(v[0] == 0 && v[2] == 0 && v[3] == 0 && swaps == 1 && steps == 0 && refused == 0);
    ResetHold("a first build waits out a step in flight: its load takes a slot only between steps, no drain");
    Flight(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0 && Tick(0, 5000) == 0 && g_loads == 0 && g_joins == 0 && g_steps == 0);
    AiUpdate(1); Flight(0);
    CHECK(Tick(0, 5010) == 0 && g_loads == 0); /* the AI update runs: not between steps */
    AiUpdate(0);
    CHECK(Tick(0, 5020) == 1 && g_joins == 0 && g_steps == 0 && g_loads == 1 && g_broken == 0);
    ResetHold("every waiting first build leaves in the one tick between steps, oldest first, before a built cell");
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0); Push(1, IDX, 5, 0); Push(2, IDX, 6, 0); Push(3, IDX, 7, 0);
    CHECK(Tick(0, 0) == 0);
    Flight(0);
    CHECK(Tick(0, 10) == 3 && g_done[0] == 1 && g_done[1] == 2 && g_done[2] == 3 && g_loads == 3 && g_unloads == 0);
    Driver(); CHECK(Tick(0, 20) == 0);
    Driver(); CHECK(Tick(0, 30) == 1 && g_unloads == 1 && g_broken == 0 && g_joins == 0 && g_steps == 0);
    ResetHold("no cell work (flag 0x80): taken at once whatever the world does");
    Flight(1); Section(IDX, 3, 40);
    Push(0, IDX, 3, 0x80);
    CHECK(Tick(0, 0) == 1 && g_unloads == 0 && g_loads == 0);
}

/* The job ring as the game lays it out, with the test's records in its slot `slot`; `next` is the game's counter. */
static volatile uint32_t g_ringNext;
static void Ring(uint32_t slot, uint32_t next) { g_ringNext = next; CoXlTestLwNavRing((uintptr_t)g_rec - slot * 0x210u, &g_ringNext); }
static void TestHoldRing(void)
{
    uint32_t v[8];
    ResetHold("a waiting first build 64 job starts from its ring slot's turn waits on; at 63 the step is finished here and it leaves");
    Flight(1); Ring(100, 36);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0 && g_loads == 0 && g_waits == 0);
    g_ringNext = 37;
    CHECK(Tick(0, 10) == 1 && g_loads == 1 && g_waits == 1 && g_commits == 1 && g_broken == 0 && g_lockedCalls == 0);
    CHECK(fake_host_logged("a step finished on the frame thread (a waiting record is due in the job ring; flush 0, living world live, world 1, "));
    CHECK(fake_host_logged("a waiting record left at the job ring's bound (world 1, 63 job start(s) before its turn, waited 1 tick(s))"));
    HoldCounts(v); CHECK(v[3] == 1);
    ResetHold("the ring's count wraps: slot 5 with the counter at 512 + 500 is 17 starts away");
    Flight(1); Ring(5, 512 + 500);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_waits == 1 && fake_host_logged("17 job start(s) before its turn, waited 0 tick(s))"));
    ResetHold("the slot the next job start takes: 0 left");
    Flight(1); Ring(7, 7);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && fake_host_logged("0 job start(s) before its turn"));
    ResetHold("a slot just handed out is 511 starts away: it waits");
    Flight(1); Ring(7, 8);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0 && Tick(0, 10) == 0 && g_waits == 0);
    ResetHold("a due built cell's record: the step is finished here, its swap goes with nothing in flight");
    Section(IDX, 3, 40); Flight(1); Ring(100, 90);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_unloads == 1 && g_loads == 1 && g_broken == 0 && g_waits == 1);
    ResetHold("a due record of an idle world leaves with no step finished here");
    Section(IDX, 3, 40); g_grant = 0; Ring(100, 30);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0); /* a query job reads the world: the built cell waits */
    g_ringNext = 90;
    CHECK(Tick(0, 10) == 1 && g_waits == 0 && g_steps == 0 && g_commits == 0);
    CHECK(!fake_host_logged("a waiting record is due in the job ring") && fake_host_logged("a waiting record left at the job ring's bound (world 1, 10 job start(s)"));
    g_grant = 1;
    ResetHold("a record outside the ring, or below it, is never due");
    Flight(1); g_ringNext = 0; CoXlTestLwNavRing((uintptr_t)g_rec + 0x20, &g_ringNext);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0);
    CoXlTestLwNavRing((uintptr_t)g_rec - 512 * 0x210u, &g_ringNext);
    CHECK(Tick(0, 10) == 0);
    CoXlTestLwNavRing((uintptr_t)g_rec - 511 * 0x210u, &g_ringNext); g_ringNext = 511 - 63;
    CHECK(Tick(0, 20) == 1); /* the ring's last slot */
    ResetHold("eight lines a level");
    Ring(100, 90);
    for (int k = 0; k < 7; k++) { Flight(1); Push(0, IDX, 3 + k, 0); CHECK(Tick(0, 10 * k) == 1); }
    g_fake.log[0] = 0;
    Flight(1); Push(0, IDX, 10, 0); CHECK(Tick(0, 70) == 1 && fake_host_logged("a waiting record left at the job ring's bound"));
    g_fake.log[0] = 0;
    Flight(1); Push(0, IDX, 11, 0); CHECK(Tick(0, 80) == 1 && !fake_host_logged("a waiting record left at the job ring's bound"));
}

/* A fault in the cutter: what the note holds, and which faults it is written for. */
static void TestCutFault(void)
{
    static uint8_t stack[0x400] __attribute__((aligned(16))), item[0x48], edges[0x100], all[0x48];
    const uintptr_t exe = 0x140000000u;
    CONTEXT c;
    ResetHold("a fault in the recut: the registers, the item, five edge records around r13 and the section arrays");
    CoXlTestLwCutReset();
    memset(&c, 0, sizeof c); memset(stack, 0, sizeof stack);
    for (int k = 0; k < 0x48; k++) item[k] = (uint8_t)k;
    for (int k = 0; k < 0x100; k++) edges[k] = (uint8_t)(0xA0 + k / 8);
    Put(item + 0x18, (uintptr_t)edges);
    c.Rsp = (uintptr_t)stack; c.Rbp = (uintptr_t)stack + 0x100; c.R13 = 9; c.Rax = 0x1234;
    Put(stack + 0x140, (uintptr_t)item);
    memset(all, 0, sizeof all); Put(all + 0x38, 0x5000); { int32_t n[2] = {295, (int32_t)0x80000200u}; memcpy(all + 0x40, n, 8); }
    Put(g_aiw + 0x130, (uintptr_t)all);
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC87FB6u, 0x77, &c) == 1);
    CHECK(fake_host_logged("a fault at exe+0xC87FB6 on address 0000000000000077, in or under the cutter's 0xC87A40, thread "));
    CHECK(fake_host_logged(": rax 1234 rcx 0 ") && fake_host_logged(" r13 9 r14 0 r15 0"));
    CHECK(fake_host_logged(": 0001020304050607 08090A0B0C0D0E0F 1011121314151617 "));
    CHECK(fake_host_logged(" 4041424344454647; its edge records "));
    CHECK(fake_host_logged(", five from record 7 (r13 9): A7A7A7A7A7A7A7A7 A8A8A8A8A8A8A8A8 A9A9A9A9A9A9A9A9 AAAAAAAAAAAAAAAA ABABABABABABABAB"));
    CHECK(fake_host_logged("the section arrays then: world 1: 0000000000005000, 295 of 512"));
    ResetHold("r13 below 2: from record 0; an item that cannot be read says so");
    CoXlTestLwCutReset(); c.R13 = 1;
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC87A40u, 0, &c) == 1 && fake_host_logged("five from record 0 (r13 1): A0A0A0A0A0A0A0A0 "));
    g_fake.log[0] = 0; c.Rbp = 0x10;
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC88450u, 0, &c) == 1 && fake_host_logged("the item at [rbp+0x40] could not be read"));
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC87FB6u, 0, &c) == 0 && g_fake.log[0] == 0); /* two notes a session */
    ResetHold("the functions' bounds; a fault elsewhere with no cutter frame on the stack is not the cutter's");
    c.Rbp = (uintptr_t)stack + 0x100;
    const uint32_t out[] = {0xC87A3Fu, 0xC88451u, 0xC8B30Fu, 0xC8B3AEu, 0xCB110Fu, 0xCB12CCu, 0x7B8A10u};
    for (int k = 0; k < 7; k++) { CoXlTestLwCutReset(); CHECK(CoXlTestLwCutNote(exe, exe + out[k], 0, &c) == 0); }
    const uint32_t in[] = {0xC8B310u, 0xC8B3ADu, 0xCB1110u, 0xCB12CBu};
    for (int k = 0; k < 4; k++)
    {
        CoXlTestLwCutReset(); g_fake.log[0] = 0;
        CHECK(CoXlTestLwCutNote(exe, exe + in[k], 0, &c) == 1 && fake_host_logged(k < 2 ? "the cutter's 0xC8B310, thread" : "the cutter's 0xCB1110, thread"));
        CHECK(!fake_host_logged("the item"));
    }
    ResetHold("a fault under the cutter: found by its return address, with no item");
    CoXlTestLwCutReset(); Put(stack + 8 * 95, exe + 0xC8B3A4u);
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC2F662u, 0, &c) == 1 && fake_host_logged("in or under the cutter's 0xC8B310 (its return address at rsp+0x2F8), thread"));
    CoXlTestLwCutReset(); Put(stack + 8 * 95, 0); Put(stack + 8 * 96, exe + 0xC8B3A4u);
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC2F662u, 0, &c) == 0); /* 96 stack words are read */
    CoXlTestLwCutReset(); Put(stack + 8 * 3, exe + 0xC87FB6u); g_fake.log[0] = 0;
    CHECK(CoXlTestLwCutNote(exe, exe + 0xC2F662u, 0, &c) == 1 && !fake_host_logged("the item")); /* the recut's frame is not this one */
    ResetHold("no AI world: the note says so; an unreadable collection too");
    CoXlTestLwCutReset(); Put(g_aiw + 0x130, 0x10);
    CHECK(CoXlTestLwCutNote(exe, exe + 0xCB1223u, 0, &c) == 1 && fake_host_logged("the section arrays then: world 1 unreadable"));
    CoXlTestLwCutReset();
}

static void TestHoldWaits(void)
{
    uint32_t v[8];
    ResetHold("a step in flight: the built cell's completion waits for its commit");
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0);
    for (int t = 0; t < 3; t++) { CHECK(Tick(0, 10 * t) == 0); CHECK(CoXlTestLwNavHoldBusy()); }
    CHECK(g_unloads == 0 && g_joins == 0 && g_waits == 0);
    Driver(); /* the commit */
    CHECK(Tick(0, 40) == 1 && g_broken == 0 && g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0);
    CHECK(fake_host_logged("swapped in with the world idle, for its next step") && fake_host_logged("held 3 tick(s), 0 other change(s) queued; 1 swaps this level"));
    HoldCounts(v);
    CHECK(v[0] == 1 && v[1] == 3 && v[2] == 0 && v[3] == 0);
    static uint8_t sections[0x48]; int32_t slots = 77;
    memcpy(sections + 0x40, &slots, 4); Put(g_aiw + 0x130, (uintptr_t)sections);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("1 completion(s) held, the longest 3 tick(s); 0 drain(s) on the frame thread; 0 swap(s) let through with other changes queued; this level 1 swap(s) and 0 "
        "unload(s) with no load for 0 built cell(s) marked; 77 of 1024 section slots"));
    ResetHold("other changes queued (cutters): it waits for the step that takes them, then goes with nothing queued");
    Section(IDX, 3, 40); g_queued = 1;
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0);
    Driver(); CHECK(g_inFlight == 1 && Tick(0, 1010) == 0);
    Driver(); CHECK(Tick(0, 1020) == 1 && g_broken == 0 && g_steps == 0);
    CHECK(fake_host_logged("0 other change(s) queued"));
    ResetHold("other changes always queued: after 100 ms it goes between steps, with no drain");
    Section(IDX, 3, 40); g_queued = 7;
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 1099) == 0 && g_unloads == 0);
    CHECK(Tick(0, 1100) == 1 && g_broken == 0);
    CHECK(g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0 && g_leaves == 0);
    CHECK(fake_host_logged("swapped in with the world idle, for its next step") && fake_host_logged("held 2 tick(s), 7 other change(s) queued; 1 swaps"));
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("0 drain(s) on the frame thread; 1 swap(s) let through with other changes queued;"));
    ResetHold("other changes queued and a step in flight: not after 100 ms either");
    Section(IDX, 3, 40); g_queued = 7; Flight(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 1500) == 0 && g_unloads == 0);
    Flight(0);
    CHECK(Tick(0, 1510) == 1 && g_broken == 0);
    ResetHold("other changes queued and the world's own swap unapplied: only a step frees the next built cell");
    Section(IDX, 3, 40); Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1000) == 1);
    g_queued += 5;
    CHECK(Tick(0, 1200) == 0 && Tick(0, 2500) == 0); /* 100 ms and more: the swap is still in the queue */
    Driver(); g_queued = 5; CHECK(Tick(0, 2510) == 0); /* taken, in flight; cutters queue again */
    Driver(); CHECK(Tick(0, 2520) == 1 && g_done[1] == 1 && g_broken == 0 && g_joins == 0 && g_steps == 0);
    CHECK(fake_host_logged("5 other change(s) queued"));
    Section(IDX, 5, 42); Push(2, IDX, 5, 0); /* and again for the second swap: the first one's step does not count */
    CHECK(Tick(0, 2700) == 0 && Tick(0, 3500) == 0);
    Driver(); g_queued = 5; CHECK(Tick(0, 3510) == 0);
    Driver(); CHECK(Tick(0, 3520) == 1 && g_done[2] == 2 && g_broken == 0 && g_joins == 0);
    ResetHold("a first build's load is a section command too: the built cell behind it waits for its step");
    Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1000) == 1 && g_done[0] == 0);
    g_queued += 4;
    CHECK(Tick(0, 1200) == 0 && Tick(0, 1900) == 0);
    Driver(); g_queued = 3; CHECK(Tick(0, 1910) == 0);
    Driver(); CHECK(Tick(0, 1920) == 1 && g_done[1] == 1 && g_broken == 0 && g_joins == 0);
    ResetHold("the buffer's change is kept from the tick that saw it, whatever address the next buffer has");
    Section(IDX, 3, 40); Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1000) == 1);
    uintptr_t first = Get(g_aiw + 0x498);
    Driver(); g_queued = 5; CHECK(Tick(0, 1200) == 0);
    Driver(); Put(g_aiw + 0x498, first); /* committed; a later buffer at the first one's address */
    CHECK(Tick(0, 1210) == 1 && g_broken == 0);
    ResetHold("a query job reads the world: the built cell waits, and no permit is kept");
    Section(IDX, 3, 40); g_grant = 0;
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 1300) == 0 && g_unloads == 0 && g_permits == 2 && g_aiw[0x478] == 0);
    g_grant = 1;
    CHECK(Tick(0, 1310) == 1 && g_joins == 0 && g_aiw[0x478] == 0);
    ResetHold("the AI update still runs: it waits");
    Section(IDX, 3, 40); AiUpdate(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0 && g_unloads == 0);
    AiUpdate(0);
    CHECK(Tick(0, 10) == 1 && g_joins == 0);
    ResetHold("an older first build's load in the same tick: the built cell behind it waits for the step");
    Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 0) == 1 && g_done[0] == 0 && g_queued == 1);
    Driver(); CHECK(Tick(0, 10) == 0);
    Driver(); CHECK(Tick(0, 20) == 1 && g_done[1] == 1 && g_broken == 0 && g_steps == 0 && g_joins == 0);
}

static void TestHoldOnePerRun(void)
{
    uint32_t v[8];
    ResetHold("one swap a step: the next completion waits until the swap is applied and committed");
    Section(IDX, 3, 40); Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0); Push(2, IDX, 5, 0); /* two built cells, then a first build */
    CHECK(Tick(0, 0) == 1 && g_done[0] == 2 && g_queued == 1); /* the first build; its load is unapplied */
    for (int t = 1; t < 50; t++) CHECK(Tick(0, 10 * t) == 0); /* no step yet: time alone frees nothing */
    Driver(); CHECK(Tick(0, 600) == 0);       /* the step that took the load is in flight */
    Driver(); CHECK(Tick(0, 610) == 1 && g_done[1] == 0 && g_queued == 2); /* committed: the older built cell, and only it */
    CHECK(CoXlTestLwNavHoldBusy());
    Driver(); CHECK(Tick(0, 620) == 0);
    Driver(); CHECK(Tick(0, 630) == 1 && g_done[2] == 1);
    CHECK(g_broken == 0 && g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0 && g_dones == 3);
    HoldCounts(v);
    CHECK(v[0] == 2 && v[3] == 0);
    ResetHold("a step that starts and commits inside one driver call: seen from the state, not from time");
    Section(IDX, 3, 40); Section(IDX, 4, 41);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 0) == 1);
    StepAtOnce(); /* started and committed before the next tick: never seen in flight */
    CHECK(Tick(0, 10) == 1 && g_broken == 0 && g_steps == 0);
    ResetHold("a first build behind a swap waits for it");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1);
    Push(1, IDX, 5, 0);
    CHECK(Tick(0, 10) == 0 && g_loads == 1);
    Driver(); CHECK(Tick(0, 20) == 0);
    Driver(); CHECK(Tick(0, 30) == 1 && g_loads == 2 && g_broken == 0);
    ResetHold("a waiting record is counted once, whatever leaves before it");
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 5, 0);
    CHECK(Tick(0, 0) == 0);
    Push(1, IDX, 3, 0);
    CHECK(Tick(0, 10) == 0);
    Driver();
    CHECK(Tick(0, 20) == 1 && g_done[0] == 0); /* the first build; the built cell waits for its load's step */
    CHECK(Tick(0, 30) == 0 && Tick(0, 40) == 0);
    HoldCounts(v);
    CHECK(v[0] == 2 && v[1] == 2);
    ResetHold("the pop's leave for a cell is used once, by that cell's unload");
    Section(IDX, 3, 40); Section(IDX, 4, 41);
    Push(0, IDX, 3, 0);
    uintptr_t rec = CoXlTestLwNavPop((uintptr_t)g_list, 0, 0);
    CHECK(rec == (uintptr_t)g_rec[0]);
    uint32_t id = 41;
    g_locked = 0;
    CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, (uintptr_t)g_cells[0] + 4 * STRIDE); /* another cell: the drain, as before the hold */
    CHECK(g_joins == 1 && g_permits == 2);
    ResetHold("the leave does not outlive its pop");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == (uintptr_t)g_rec[0]);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 0) == 0);
    id = 40; g_locked = 0; g_queued = 0;
    CoXlTestLwNavUnload((uintptr_t)g_aiw, &id, (uintptr_t)g_cells[0] + 3 * STRIDE);
    CHECK(g_joins == 1);
}

static void TestHoldTwoWorlds(void)
{
    ResetHold("a busy world holds its own completions only");
    Section(IDX, 3, 40); Section(IDX2, 3, 50); Flight(1);
    Push(0, IDX, 3, 0); Push(1, IDX2, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_done[0] == 1 && g_queued2 == 2 && g_queued == 0 && g_joins == 0);
    Push(2, IDX2, 4, 0); /* world 2's swap is unapplied: its first build waits; world 1 still in flight */
    CHECK(Tick(0, 10) == 0);
    Take(g_aiw2); g_queued2 = 0; Took(IDX2);
    CHECK(Tick(0, 20) == 1 && g_done[1] == 2);
    Driver();
    CHECK(Tick(0, 30) == 1 && g_done[2] == 0 && g_broken == 0 && g_steps == 0);
}

/* A level's end: the tick's flush argument or a stopped nav. The game's order, and each swap's step finished on the
 * frame thread as before the hold; a swap the hold left to the next step is finished before any other completion. */
static void TestHoldLevelEnd(void)
{
    uint32_t v[8];
    ResetHold("the flush tick: the game's order and the drain");
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0); Push(1, IDX, 5, 0);
    CHECK(Tick(1, 0) == 2 && g_done[0] == 1 && g_done[1] == 0); /* newest first, as the game pops */
    CHECK(g_joins == 2 && g_waits == 1 && g_commits == 1 && g_steps == 2 && fake_host_logged("swapped in at once"));
    CHECK(!CoXlTestLwNavHoldBusy());
    ResetHold("a swap left to the next step, then the flush tick: finished before the next completion's connect");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_queued == 2 && g_steps == 0);
    Push(1, IDX, 5, 0);
    CHECK(Tick(0, 10) == 0);
    g_fake.log[0] = 0;
    CHECK(Tick(1, 20) == 1 && g_done[1] == 1);
    CHECK(fake_host_logged("a step finished on the frame thread (the game's order, behind a swap not applied yet; flush 1, living world live, world 1, 2 queued before): "));
    CHECK(g_steps == 1 && g_queued == 1 && g_joins == 1 && g_leaves == 1 && g_enters == 1 && g_lockedCalls == 0); /* queued: the first build's own load */
    HoldCounts(v);
    CHECK(v[3] == 1 && v[2] == 0);
    ResetHold("the same with a step in flight that took the swap");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1);
    Driver();
    Push(1, IDX, 5, 0);
    CHECK(Tick(1, 10) == 1 && g_waits == 1 && g_commits == 1 && g_leaves == 1 && g_enters == 1 && g_lockedCalls == 0);
    ResetHold("a stopped nav (bit 2 off, bit 4 on): the game's record, the unapplied swap finished first");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1);
    NavState(IDX, 5);
    Push(1, IDX, 5, 0);
    CHECK(Tick(0, 10) == 1 && g_steps == 1 && g_leaves == 1 && g_enters == 1 && g_lockedCalls == 0 && g_broken == 0);
    CHECK(fake_host_logged("a step finished on the frame thread (a stopped nav's record, behind a swap not applied yet; flush 0, living world live, world 1, 2 queued before): "));
    ResetHold("a stopped nav: a built cell's completion drains as before the hold");
    Section(IDX, 3, 40); Flight(1); NavState(IDX, 5);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_waits == 1 && g_commits == 1 && fake_host_logged("swapped in at once"));
    ResetHold("a nav with bit 4 only: the game's record too");
    Section(IDX, 3, 40); Flight(1); NavState(IDX, 7);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_waits == 1);
    ResetHold("a nav that does not run");
    Section(IDX, 3, 40); Flight(1); NavState(IDX, 1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1 && g_waits == 1);
    ResetHold("a normal mission: the game's pop, nothing held, nothing drained");
    CoXlTestLive(0);
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0); Push(1, IDX, 5, 0);
    CHECK(Tick(0, 0) == 2 && g_done[0] == 1 && g_joins == 0 && g_waits == 0 && g_steps == 0 && !CoXlTestLwNavHoldBusy());
    ResetHold("the swap's permit refused at the flush: the completion goes on, the marks are held");
    Section(IDX, 3, 40);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 1);
    g_grant = 0;
    Push(1, IDX, 5, 0);
    uint32_t swaps, steps, parallel, refused, noLoad;
    CHECK(Tick(1, 10) == 1);
    Counts(&swaps, &steps, &parallel, &refused, &noLoad);
    CHECK(refused == 1 && g_steps == 0 && g_leaves == 1 && g_enters == 1);
}

static void Budget(float v) { memcpy(g_aiw + 0x47C, &v, 4); }
static void RunAge(float s) { memcpy(g_aiw + 0x484, &s, 4); }
static float Aib(int on, ULONGLONG now) { return CoXlTestLwAibWorld((uintptr_t)g_aiw, 0.002f, 0.0f, on, now); }

/* A run that stays in flight: the completion waits however long, no step is finished here, and 30 s is said once. */
static void TestHoldLong(void)
{
    uint32_t v[8];
    ResetHold("a world whose run stays in flight: its completion waits on, nothing is finished on the frame thread");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 5000) == 0);
    CHECK(Tick(0, 7000) == 0 && Tick(0, 34999) == 0 && !fake_host_logged("LONG HOLD"));
    CHECK(Tick(0, 35000) == 0);
    CHECK(fake_host_logged("living world: nav: LONG HOLD: 1 completion(s) of world 1 have waited 30000 ms and wait on: run in flight 1, done 1, permit 1, "
        "0 queued, AI update 0, section commands unapplied 0, budget 2000 us"));
    HoldCounts(v);
    CHECK(v[7] == 1);
    g_fake.log[0] = 0;
    CHECK(Tick(0, 70000) == 0 && Tick(0, 700000) == 0 && !fake_host_logged("LONG HOLD")); /* once a world and level */
    CHECK(g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0 && g_leaves == 0);
    Driver();
    CHECK(Tick(0, 700016) == 1 && g_broken == 0 && g_joins == 0 && g_waits == 0);
    CHECK(fake_host_logged("swapped in with the world idle, for its next step"));
    HoldCounts(v);
    CHECK(v[3] == 0 && v[0] == 1);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("0 drain(s) on the frame thread; 0 swap(s) let through") && !fake_host_logged("a step finished on the frame thread"));
    ResetHold("the long hold's line: no run in flight, no permit, a queue; again in the next level");
    Section(IDX, 3, 40); g_queued = 7; AiUpdate(1); g_grant = 0; Budget(0.008f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 30999) == 0 && !fake_host_logged("LONG HOLD"));
    CHECK(Tick(0, 31000) == 0);
    CHECK(fake_host_logged("have waited 30000 ms and wait on: run in flight 0, done -1, permit 0, 7 queued, AI update 1, section commands unapplied 0, budget 8000 us"));
    CoXlTestLwNavLevel(2); g_fake.log[0] = 0;
    CHECK(Tick(0, 40000) == 0 && Tick(0, 69999) == 0 && !fake_host_logged("LONG HOLD"));
    CHECK(Tick(0, 70000) == 0 && fake_host_logged("LONG HOLD: 1 completion(s) of world 1 have waited 30000 ms"));
    ResetHold("a taken record starts the relaxed wait again, and no wait ends in a drain");
    Section(IDX, 3, 40); Section(IDX, 4, 41); Section(IDX, 5, 42);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0); Push(2, IDX, 5, 0);
    CHECK(Tick(0, 1000) == 1);       /* the first; the others wait from here */
    CHECK(Tick(0, 2800) == 0);
    Driver(); Driver();
    CHECK(Tick(0, 2900) == 1);       /* the second, by the step; the third waits from here */
    CHECK(Tick(0, 3100) == 0 && Tick(0, 9000) == 0 && g_joins == 0 && g_steps == 0 && g_broken == 0);
}

/* The run in flight gets 4 ms a frame, never more, while a completion is held behind it or a backlog waits. */
static void TestHoldRaise(void)
{
    uint32_t v[8];
    ResetHold("the budget of a run in flight with a completion held");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0);
    CHECK(Aib(1, 1099) == 0.002f);  /* held: the game's own, not the low one */
    CHECK(Aib(1, 1100) == 0.004f && Aib(1, 1499) == 0.004f);
    CHECK(Aib(1, 1500) == 0.004f && Aib(1, 600000) == 0.004f); /* never more */
    HoldCounts(v);
    CHECK(v[2] == 4 && v[5] == 0);
    CHECK(Aib(0, 1099) == 0.002f && Aib(0, 1100) == 0.004f && Aib(0, 1500) == 0.004f); /* the lever off: the raise alone */
    CHECK(CoXlTestLwAibWorld((uintptr_t)g_aiw, 0.01f, 0.0f, 1, 1500) == 0.01f);     /* a stock budget above it stays */
    CHECK(CoXlTestLwAibWorld((uintptr_t)g_aiw, 0.01f, 0.0f, 0, 1500) == 0.01f);
    CHECK(CoXlTestLwAibWorld((uintptr_t)g_aiw2, 0.002f, 0.0f, 1, 1500) == 0.002f);  /* another world: its own records only */
    Flight(0);
    CHECK(Aib(1, 1600) == 0.002f && Aib(0, 1600) == 0.002f); /* no run in flight: nothing to speed up */
    Flight(1);
    CHECK(Aib(1, 1600) == 0.004f);
    Driver();
    CHECK(Tick(0, 1616) == 1 && g_broken == 0);
    CHECK(Aib(1, 1632) == 0.002f);  /* none held; its swap unapplied: the game's own */
    Driver(); Tick(0, 1648); Driver(); CHECK(Tick(0, 1664) == 0 && !CoXlTestLwNavHoldBusy());
    CHECK(Aib(1, 1680) == 0.0005f && Aib(0, 1680) == 0.002f);
    ResetHold("the raise counts from the oldest held completion, not from the last one taken");
    Section(IDX, 3, 40); Section(IDX, 4, 41); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1000) == 0);
    Driver();
    CHECK(Tick(0, 1400) == 1 && g_done[0] == 0);
    Driver();                        /* the swap's run starts */
    CHECK(Tick(0, 1450) == 0 && g_inFlight == 1);
    CHECK(Aib(1, 1099) == 0.002f && Aib(1, 1100) == 0.004f);
    ResetHold("a first build held behind an unapplied swap raises too; with nothing held, nothing");
    Flight(1); Budget(0.002f);
    CHECK(Tick(0, 1000) == 0 && Aib(1, 9000) == 0.0005f);
    ResetHold("the AI joins after a raised budget, for the summary");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Aib(1, 1050) == 0.002f);
    CoXlTestLwFpJoin(9.0);                 /* the join of a frame with the game's own budget: not counted */
    CHECK(Aib(1, 1100) == 0.004f);
    CoXlTestLwFpJoin(3.0);
    CHECK(Aib(1, 1200) == 0.004f);
    CoXlTestLwFpJoin(1.0); CoXlTestLwFpJoin(4.0); /* two joins in one frame */
    Flight(0);
    CHECK(Aib(1, 1300) == 0.002f);
    CoXlTestLwFpJoin(7.0);
    CHECK(Aib(1, 1400) == 0.002f);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("living world: nav hold (30 s): AI budget raised to 4000 us on 2 frame(s) (0 for a backlog, no completion held); the AI joins after them 4.00 ms on average, 5.00 ms at worst "
        "(2 frame(s)); 0 tick(s) given the game's order with no game frame before them"));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(61000);
    CHECK(!fake_host_logged("nav hold (")); /* nothing raised since */
    Flight(1);
    CHECK(Aib(1, 2000) == 0.004f);
    CoXlTestLwFpJoin(2.0);
    CHECK(Aib(1, 2016) == 0.004f);
    CoXlTestLwNavSwapSummary(91000);
    CHECK(fake_host_logged("AI budget raised to 4000 us on 2 frame(s) (0 for a backlog, no completion held); the AI joins after them 2.00 ms on average, 2.00 ms at worst (1 frame(s)); 0 tick(s)"));
    ResetHold("records the flush tick took are not held any more");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Aib(0, 1600) == 0.004f);
    CHECK(Tick(1, 1700) == 1);
    Flight(1);
    CHECK(Aib(0, 1800) == 0.002f);
    ResetHold("a low budget above the game's own is never used");
    CHECK(CoXlTestLwAibSteer(0.0003f, 0.0003f, 0.0f) == 0.0003f);
    ResetHold("a long queue: the game's own budget, not the low one");
    Budget(0.002f); g_queued = 1024;
    CHECK(Aib(1, 1000) == 0.0005f);
    g_queued = 1025;
    CHECK(Aib(1, 1000) == 0.002f && Aib(0, 1000) == 0.002f);
    ResetHold("a backlog behind a run in flight: the raised budget with no completion held");
    Budget(0.002f); Flight(1); g_queued = 1024;
    CHECK(Aib(1, 1000) == 0.0005f && Aib(0, 1000) == 0.002f); /* a short queue behind a young run: nothing */
    g_queued = 1025;
    CHECK(Aib(1, 1000) == 0.004f && Aib(0, 1000) == 0.004f);
    HoldCounts(v);
    CHECK(v[2] == 2 && v[5] == 2);
    Flight(0);
    CHECK(Aib(1, 1000) == 0.002f);  /* no run to speed up: the game's own for the one that starts */
    Flight(1); g_queued = 1; RunAge(4.999f);
    CHECK(Aib(1, 1000) == 0.0005f);
    RunAge(5.0f);
    CHECK(Aib(1, 1000) == 0.004f && Aib(0, 1000) == 0.004f);
    g_queued = 0; RunAge(900.0f);
    CHECK(Aib(1, 1000) == 0.0005f); /* an old run with nothing behind it */
    g_queued = 1; RunAge(__builtin_nanf(""));
    CHECK(Aib(1, 1000) == 0.0005f);
    RunAge(60.0f); Flight(0);
    CHECK(Aib(1, 1000) == 0.0005f);
    HoldCounts(v);
    CHECK(v[2] == 4 && v[5] == 4);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("AI budget raised to 4000 us on 4 frame(s) (4 for a backlog, no completion held); "));
    g_fake.log[0] = 0;
    Flight(1);
    CHECK(Aib(1, 32000) == 0.004f);
    CoXlTestLwNavSwapSummary(61000);
    CHECK(fake_host_logged("AI budget raised to 4000 us on 1 frame(s) (1 for a backlog, no completion held); ")); /* counted since the last line */
    ResetHold("in play the summary is written with nothing to count: the queue, the run and the slots");
    CoXlTestLwNavLevel(7); Budget(0.002f);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(!fake_host_logged("nav swaps (")); /* not in play yet */
    CHECK(CoXlTestLwNavLeft(1, 3) == 0);
    g_queued = 3;
    CoXlTestLwNavSwapSummary(61000);
    CHECK(fake_host_logged("living world: nav swaps (30 s): 0 on the frame thread, ") && fake_host_logged("section slots; world 1: 3 command(s) queued, no run in flight"));
    g_fake.log[0] = 0;
    Flight(1); RunAge(6.5f); g_queued = 930;
    Aib(1, 62000); Aib(1, 62016);    /* two frames with the run finished and not committed */
    CoXlTestLwNavSwapSummary(90999);
    CHECK(!fake_host_logged("nav swaps ("));
    CoXlTestLwNavSwapSummary(91000);
    CHECK(fake_host_logged("section slots; world 1: 930 command(s) queued behind a run 6.5 s old (done 1, no query job 1); a finished run waited for its commit on 2 frame(s)"));
    g_fake.log[0] = 0;
    g_queued2 = 931; Flight(0);
    CoXlTestLwNavSwapSummary(121000);
    CHECK(fake_host_logged("section slots; world 2: 931 command(s) queued, no run in flight; a finished run waited for its commit on 0 frame(s)"));
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwNavLeft(1, 0) == 1);
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(151000);
    CHECK(!fake_host_logged("nav swaps (")); /* the level left play */
}

/* Each world has its own wait: records of other worlds or of no world's rules that leave do not start it again. */
static void TestHoldOwnWait(void)
{
    ResetHold("a held world's wait goes on while other records leave every tick");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    for (int t = 0; t < 20; t++)
    {
        Push(1 + t % 2, t % 2 ? IDX2 : IDX, 5 + t / 2, t % 2 ? 0 : 0x80); /* world 2's first builds and plain records */
        CHECK(Tick(0, 1000 + 100 * t) == 1 && g_joins == 0);
    }
    CHECK(Aib(1, 2950) == 0.004f && g_joins == 0); /* 1950 ms held, counted from its own first tick */
    ResetHold("a take starts its world's relaxed wait again");
    Section(IDX, 3, 40); Section(IDX, 4, 41); g_queued = 5;
    Push(0, IDX, 3, 0); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 1100) == 1);
    Driver(); Driver(); g_queued = 5;     /* its swap applied; cutter changes queued again */
    CHECK(Tick(0, 1150) == 0);            /* 50 ms since the take, 150 since the first wait began */
    CHECK(Tick(0, 1200) == 1 && g_broken == 0 && g_joins == 0);
    ResetHold("another world's take does not start it again");
    Section(IDX2, 3, 50); g_queued2 = 5;
    Push(0, IDX2, 3, 0);
    CHECK(Tick(0, 1000) == 0);
    Push(1, IDX, 5, 0);
    CHECK(Tick(0, 1050) == 1 && g_done[0] == 1); /* world 1's first build */
    CHECK(Tick(0, 1100) == 1 && g_done[1] == 0); /* 100 ms from 1000 */
    ResetHold("a wait does not outlive its records");
    Section(IDX, 3, 40); g_queued = 5;
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0);
    NavState(IDX, 5);
    CHECK(Tick(0, 1010) == 1);            /* its nav stopped: the game's record */
    NavState(IDX, 3); StepAtOnce(); g_queued = 5; g_joins = 0;
    Section(IDX, 4, 41); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 2000) == 0 && Tick(0, 2099) == 0); /* a new wait, from 2000 */
    CHECK(Tick(0, 2100) == 1 && g_joins == 0);
    ResetHold("a world's wait ends when its last record leaves by the game's rules");
    Section(IDX, 3, 40); Flight(1); Budget(0.002f);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0);
    NavState(IDX, 5);
    g_fake.log[0] = 0;
    CHECK(Tick(0, 1100) == 1); /* its nav stopped: the game's record, drained as before the hold */
    CHECK(fake_host_logged("living world: nav: a step finished on the frame thread (before an unload the pop did not let through; flush 0, living world live, "
        "world 1, 0 queued before): "));
    CHECK(fake_host_logged("a step finished on the frame thread (after a load the pop did not let through; flush 0, living world live, world 1, 2 queued before): "));
    NavState(IDX, 3); StepAtOnce(); Flight(1); g_joins = 0;
    Section(IDX, 4, 41); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 5000) == 0 && Aib(1, 5099) == 0.002f && Aib(1, 5100) == 0.004f); /* waits from here, not from 1000 */
}

/* A caller that runs the tick again with no game frame between would wait for a held record for ever. */
static void TestBreaker(void)
{
    uint32_t v[8];
    ResetHold("a second tick with no game frame between: the game's order, with the drain");
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 1000) == 0 && Tick(0, 1016) == 0 && g_waits == 0); /* one tick a frame: held */
    uintptr_t rec = CoXlTestLwNavPop((uintptr_t)g_list, 0, 1032);     /* held, then again before the mod's tick */
    CHECK(rec == 0);
    rec = CoXlTestLwNavPop((uintptr_t)g_list, 0, 1032);
    CHECK(rec == (uintptr_t)g_rec[0]);
    CHECK(fake_host_logged("living world: nav: the cell tick ran again with no game frame between and completions held: the game's order for this tick (1 this level)"));
    Process(rec, 0);
    CHECK(g_waits == 1 && g_commits == 1 && fake_host_logged("a step finished on the frame thread (before an unload the pop did not let through; flush 0, "));
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 1032) == 0 && !fake_host_logged("ran again")); /* the same tick: one line */
    HoldCounts(v);
    CHECK(v[6] == 1);
    CoXlTestLwNavSwapFrame();
    StepAtOnce(); Flight(1);
    Section(IDX, 4, 41); Push(1, IDX, 4, 0);
    CHECK(Tick(0, 1048) == 0 && Tick(0, 1064) == 0 && !fake_host_logged("ran again")); /* a frame between: held again */
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("nav hold (30 s): AI budget raised to 4000 us on 0 frame(s) (0 for a backlog, no completion held); the AI joins after them 0.00 ms on average, 0.00 ms at worst (0 frame(s)); "
        "1 tick(s) given the game's order"));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(61000);
    CHECK(!fake_host_logged("nav hold ("));
    ResetHold("ticks that left nothing held may repeat in a frame");
    Push(0, IDX, 3, 0x80);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 1000) == (uintptr_t)g_rec[0] && CoXlTestLwNavPop((uintptr_t)g_list, 0, 1000) == 0);
    Section(IDX, 3, 40); Flight(1);
    Push(1, IDX, 3, 0);
    CHECK(CoXlTestLwNavPop((uintptr_t)g_list, 0, 1000) == 0 && !fake_host_logged("ran again")); /* held from here */
    ResetHold("its lines a level");
    for (int n = 0; n < 17; n++)
    {
        Section(IDX, 3, 40); Flight(1);
        Push(0, IDX, 3, 0);
        g_fake.log[0] = 0;
        CHECK(Tick(0, 1000 + 100 * n) == 0);
        CoXlTestLwNavPop((uintptr_t)g_list, 0, 1000 + 100 * n); /* the mod's tick ran in Tick: this one holds */
        rec = CoXlTestLwNavPop((uintptr_t)g_list, 0, 1000 + 100 * n);
        CHECK(rec != 0 && (fake_host_logged("ran again") != 0) == (n < 16));
        if (rec) Process(rec, 0);
        CoXlTestLwNavSwapFrame(); StepAtOnce();
    }
    HoldCounts(v);
    CHECK(v[6] == 17);
    CoXlTestLwNavLevel(2);
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 9000) == 0);
    CoXlTestLwNavPop((uintptr_t)g_list, 0, 9000);
    rec = CoXlTestLwNavPop((uintptr_t)g_list, 0, 9000);
    CHECK(rec != 0 && fake_host_logged("for this tick (1 this level)"));
}

static void TestLeft(void)
{
    ResetHold("a level that left play marks no more cells");
    CoXlTestLwNavLevel(5);
    CHECK(CoXlTestLwNavLeft(1, 1) == 0 && CoXlTestLwNavLeft(0, 0) == 0); /* still generating: not left */
    CHECK(CoXlTestLwNavLeft(1, 3) == 0 && CoXlTestLwNavLeft(1, 3) == 0 && !fake_host_logged("left play"));
    CHECK(CoXlTestLwNavLeft(1, 0) == 1 && fake_host_logged("living world: nav: the level left play (state 0); no more cells are marked, 0 rebuild(s) still watched"));
    g_fake.log[0] = 0;
    CHECK(CoXlTestLwNavLeft(0, 0) == 1 && CoXlTestLwNavLeft(1, 4) == 1 && !fake_host_logged("left play"));
    CHECK(CoXlTestLwNavLeft(1, 3) == 0); /* in play again */
    CoXlTestLwNavLevel(6);
    CHECK(CoXlTestLwNavLeft(1, 0) == 0 && CoXlTestLwNavLeft(0, 3) == 0); /* the next level has not been in play; unread is not play */
    CHECK(CoXlTestLwNavLeft(1, 0) == 0);
}

/* A count of section slots over the 1024 a face key names is said once a level. */
static void TestSlots(void)
{
    static uint8_t all[0x48] __attribute__((aligned(16)));
    ResetHold("the section slot guard");
    int32_t n = 1024; memcpy(all + 0x40, &n, 4); Put(g_aiw + 0x130, (uintptr_t)all);
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(!fake_host_logged("SECTION SLOTS"));
    n = 1025; memcpy(all + 0x40, &n, 4);
    CoXlTestLwNavSwapSummary(61000);
    CHECK(fake_host_logged("living world: nav: SECTION SLOTS: an AI world holds 1025 sections, over the 1024 a face key can name"));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(91000);
    CHECK(!fake_host_logged("SECTION SLOTS"));
    CoXlTestLwNavLevel(2); CoXlTestLwNavSwapSummary(121000); CoXlTestLwNavSwapSummary(151000);
    CHECK(fake_host_logged("SECTION SLOTS"));
}

/* A load's slot reserve with a run in flight, and one that moved the section array, are counted and said. */
static void TestReserve(void)
{
    static uint8_t all[0x48] __attribute__((aligned(16)));
    ResetHold("slot reserves: none with a run in flight in play; the flush tick's are counted");
    memset(all, 0, sizeof all); Put(all + 0x38, 0x1000); int32_t n[2] = {5, 8}; memcpy(all + 0x40, n, 8);
    Put(g_aiw + 0x130, (uintptr_t)all); g_slots = all; g_slotsMove = 0;
    CHECK(CoXlTestLwNavLeft(1, 3) == 0);
    Flight(1);
    Push(0, IDX, 5, 0);
    CHECK(Tick(0, 0) == 0);
    Flight(0);
    CHECK(Tick(0, 10) == 1 && !fake_host_logged("a section slot was reserved"));
    CoXlTestLwNavSwapSummary(1000); CoXlTestLwNavSwapSummary(31000);
    CHECK(fake_host_logged("0 slot reserve(s) with a run in flight, 0 moved the section array"));
    g_fake.log[0] = 0;
    Flight(1); g_slotsMove = 1;
    Push(1, IDX, 6, 0);
    CHECK(Tick(1, 40000) == 1);      /* the level's end: the game's order */
    CHECK(fake_host_logged("living world: nav: a section slot was reserved with a run in flight (world 1): the array 0000000000001000 -> 0000000000002000, 6 of 8 slots -> 7 of 8"));
    Flight(0); AiUpdate(1);
    Push(2, IDX, 7, 0);
    CHECK(Tick(1, 40010) == 1);      /* the AI update running counts as a run */
    AiUpdate(0); g_slotsMove = 0; Put(all + 0x38, 0x1000);
    Push(3, IDX, 8, 0);
    CHECK(Tick(0, 40020) == 1);
    CoXlTestLwNavSwapSummary(61000);
    CHECK(fake_host_logged("2 slot reserve(s) with a run in flight, 1 moved the section array"));
    g_fake.log[0] = 0;
    CoXlTestLwNavSwapSummary(91000);
    CHECK(fake_host_logged("0 slot reserve(s) with a run in flight, 0 moved the section array"));
    Flight(1);
    for (int k = 0; k < 14; k++) { Push(4, IDX, 9 + k, 0); CHECK(Tick(1, 100000 + k) == 1); }
    g_fake.log[0] = 0;
    Push(4, IDX, 30, 0); CHECK(Tick(1, 100100) == 1);
    CHECK(!fake_host_logged("a section slot was reserved")); /* 16 lines a level */
    CoXlTestLwNavLevel(3);
    Push(4, IDX, 31, 0); CHECK(Tick(1, 100200) == 1);
    CHECK(fake_host_logged("a section slot was reserved"));
    g_slots = NULL;
}

/* Marks wait for the nav's jobs in flight, which a held completion still counts in: every completion must leave. */
static void TestHoldNoStarve(void)
{
    uint32_t v[8];
    ResetHold("built cells and first builds mixed, the driver stepping: all leave in their order, nothing drained");
    for (int c = 0; c < 12; c++) { if (c % 3 != 2) Section(IDX, c, 40 + c); Push(c, IDX, c, 0); }
    int ticks = 0;
    for (; g_dones < 12 && ticks < 200; ticks++) { Tick(0, 16 * ticks); Driver(); }
    CHECK(g_dones == 12 && ticks <= 40 && g_broken == 0);
    for (int k = 0; k < 4; k++) CHECK(g_done[k] == 3 * k + 2); /* the first builds in the first tick, oldest first */
    for (int k = 4, c = 0; k < 12; k++, c++) { if (c % 3 == 2) c++; CHECK(g_done[k] == c); } /* then the built cells in their order */
    CHECK(g_joins == 0 && g_waits == 0 && g_commits == 0 && g_steps == 0 && g_leaves == 0);
    HoldCounts(v);
    CHECK(v[2] == 0 && v[3] == 0 && v[0] == 8);
    CHECK(Get(g_list + 0x28) == 0 && Get(g_list + 0x30) == 0);
    Driver(); Driver(); Tick(0, 5000);
    CHECK(!CoXlTestLwNavHoldBusy());
}

/* The AI budget lever's low budget makes the step several times longer: not while a completion waits for that step. */
static void TestHoldBudget(void)
{
    ResetHold("the AI budget: low when nothing waits, the game's own while a completion waits or a swap is unapplied");
    CHECK(CoXlTestLwAibSteer(0.002f, 0.002f, 0.0f) == 0.0005f);
    Section(IDX, 3, 40); Flight(1);
    Push(0, IDX, 3, 0);
    CHECK(Tick(0, 0) == 0 && CoXlTestLwNavHoldBusy());
    CHECK(CoXlTestLwAibSteer(0.002f, 0.002f, 0.0f) == 0.002f);
    CHECK(CoXlTestLwAibSteer(0.002f, 0.0005f, 0.0f) == 0.002f); /* lowered before: raised again */
    Driver();
    CHECK(Tick(0, 10) == 1 && CoXlTestLwNavHoldBusy()); /* taken; its swap is queued */
    CHECK(CoXlTestLwAibSteer(0.002f, 0.0005f, 0.0f) == 0.002f);
    Driver(); CHECK(Tick(0, 20) == 0 && CoXlTestLwNavHoldBusy());
    Driver(); CHECK(Tick(0, 30) == 0 && !CoXlTestLwNavHoldBusy()); /* applied and committed */
    CHECK(CoXlTestLwAibSteer(0.002f, 0.002f, 0.0f) == 0.0005f);
}

/* A cell marked again before it completes is watched once; the built cells marked are the swaps to come. */
static void TestWatch(void)
{
    uint32_t v[8];
    ResetHold("the watch list and the marked built cells");
    CHECK(CoXlTestLwNavWatch(3, 4, 0xC, 40) == 1);
    CHECK(CoXlTestLwNavWatch(3, 4, 0x1C, 40) == 1);  /* marked again before its job started: one entry, one swap to come */
    CHECK(CoXlTestLwNavWatch(4, 3, 0xC, 41) == 2);   /* (4, 3) is not (3, 4) */
    CHECK(CoXlTestLwNavWatch(5, 5, 0, 0xFFFFFFFFu) == 3); /* never built: watched, no swap */
    CHECK(CoXlTestLwNavWatch(6, 6, 0xFFFFFFFFu, 0) == 4); /* unreadable: watched, not counted */
    HoldCounts(v);
    CHECK(v[4] == 2);
    CHECK(CoXlTestLwNavWatch(3, 9, 0xC, 43) == 5);   /* the same column, another row */
    for (int c = 0; c < 200; c++) CoXlTestLwNavWatch((uint16_t)(100 + c), 7, 0xC, (uint32_t)c);
    CHECK(CoXlTestLwNavWatch(3, 4, 0x1C, 40) == 205); /* five tile changes of 36 cells fit */
    HoldCounts(v);
    CHECK(v[4] == 203);
    CoXlTestLwNavLevel(2); CoXlTestLwNavSwapSummary(1000);
    HoldCounts(v);
    CHECK(v[4] == 0);
}

int main(void)
{
    TestPopOrder();
    TestHoldIdle();
    TestHoldRing();
    TestCutFault();
    TestHoldWaits();
    TestHoldOnePerRun();
    TestHoldTwoWorlds();
    TestHoldLevelEnd();
    TestHoldLong();
    TestHoldRaise();
    TestHoldOwnWait();
    TestBreaker();
    TestLeft();
    TestSlots();
    TestReserve();
    TestHoldNoStarve();
    TestHoldBudget();
    TestWatch();
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
