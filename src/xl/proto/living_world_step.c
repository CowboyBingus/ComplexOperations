/* The Havok AI step's anatomy and the navmesh queue by kind (../co_stepstat.h), a line each every 30 s on a complex
 * level, every build: joins far over the budget are told apart by the step's part that took the time, and the queue
 * by who fed it (the streamed forest's cutters or the nav cells). Each hook times or counts and calls the game's code
 * with the same arguments; off the game thread only the step's own stack and a spinlock of interlocked ops are used. */
#include "../co_stepstat.h"

typedef uintptr_t (*LwStepRunFn)(uintptr_t h, float dt, uintptr_t q, uintptr_t threads);
typedef uintptr_t (*LwStepDriveFn)(uintptr_t h, float seconds);
typedef uintptr_t (*LwStepFourFn)(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);
static struct
{
    void *run, *part[CO_STEP_PARTS], *req, *yield, *apply, *add, *remove;
    volatile uintptr_t world; volatile DWORD game; volatile LONG lock, inStep;
    int64_t freq; int installed, paths;
    CoStepWin win; CoStepPaths p; CoStepQueue q; /* under the lock: steps, path requests and cutters write from any thread */
    CoStepClock clock;                           /* the game thread's */
} g_lwAiStep;
/* The step or path request this thread is inside; a step's calls go to it, others are counted outside a step. */
static _Thread_local CoStepOne* t_lwStep;
static _Thread_local CoStepReq* t_lwStepReq;

/* Held for a copy or a sum only, never across a call of the game's: a holder is never waiting on anything. */
static void LwStepLock(void)
{
    for (int n = 0; InterlockedCompareExchange(&g_lwAiStep.lock, 1, 0) != 0; n++)
        if (n < 64) YieldProcessor(); else SwitchToThread();
}
static void LwStepUnlock(void) { InterlockedExchange(&g_lwAiStep.lock, 0); }
static int64_t LwStepNow(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

/* 0xBD1890(H, dt in xmm1, Q, threads): one step of an AI world, from the task body (0x7D86E9). One task per world and
 * one world in play make one step at a time; a second one in flight is counted, and each keeps its parts on its stack. */
static uintptr_t LwStepRun(uintptr_t h, float dt, uintptr_t q, uintptr_t threads)
{
    CoStepOne one = {0}, *outer = t_lwStep;
    const int atOnce = InterlockedIncrement(&g_lwAiStep.inStep) > 1;
    t_lwStep = &one;
    const int64_t t = LwStepNow();
    uintptr_t out = ((LwStepRunFn)g_lwAiStep.run)(h, dt, q, threads);
    const int64_t whole = LwStepNow() - t;
    t_lwStep = outer;
    InterlockedDecrement(&g_lwAiStep.inStep);
    const int on = !h || h != g_lwAiStep.world ? CO_STEP_ON_FOREIGN : GetCurrentThreadId() == g_lwAiStep.game ? CO_STEP_ON_GAME : CO_STEP_ON_OTHER;
    LwStepLock(); co_stepstat_step(&g_lwAiStep.win, &one, whole, on, atOnce); LwStepUnlock();
    return out;
}
static void LwStepPart(int part, int64_t ticks)
{
    CoStepOne* one = t_lwStep;
    if (one) { co_stepstat_part(one, part, ticks); return; }
    LwStepLock(); co_stepstat_outside(&g_lwAiStep.win, part, ticks); LwStepUnlock();
}
/* The run start 0xBD1380 (with its InitialAsyncProcessing wait), commit 0xBD05E0 and navigators 0xBD0BB0 take integer
 * arguments only; all four registers go on as they came. */
static uintptr_t LwStepCall(int part, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    const int64_t t = LwStepNow();
    uintptr_t out = ((LwStepFourFn)g_lwAiStep.part[part])(a, b, c, d);
    LwStepPart(part, LwStepNow() - t);
    return out;
}
static uintptr_t LwStepStart(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { return LwStepCall(CO_STEP_START, a, b, c, d); }
static uintptr_t LwStepCommit(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { return LwStepCall(CO_STEP_COMMIT, a, b, c, d); }
static uintptr_t LwStepNav(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { return LwStepCall(CO_STEP_NAV, a, b, c, d); }
/* 0xBD04A0(H, seconds in xmm1): the step passes [H+0x47C] (0xBD198C), the pool's deadline from the drive's start. */
static uintptr_t LwStepDrive(uintptr_t h, float seconds)
{
    const int64_t t = LwStepNow();
    uintptr_t out = ((LwStepDriveFn)g_lwAiStep.part[CO_STEP_DRIVE])(h, seconds);
    const int64_t ticks = LwStepNow() - t;
    CoStepOne* one = t_lwStep;
    if (one) co_stepstat_drive(one, ticks, seconds, g_lwAiStep.freq);
    else LwStepPart(CO_STEP_DRIVE, ticks);
    return out;
}

/* 0xC97D50(stage, local, index): one path request on a pool thread, with a yield before its search and one after
 * (0xC97D73, the tail jump 0xC97F70) and more inside the searches; a yield past the deadline blocks until the next drive. */
static uintptr_t LwStepReq(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    CoStepReq r, *outer = t_lwStepReq;
    const int64_t t = LwStepNow();
    co_stepstat_req_begin(&r, t);
    t_lwStepReq = &r;
    uintptr_t out = ((LwStepFourFn)g_lwAiStep.req)(a, b, c, d);
    const int64_t now = LwStepNow();
    t_lwStepReq = outer;
    const int64_t own = co_stepstat_req_end(&r, t, now);
    LwStepLock(); co_stepstat_paths(&g_lwAiStep.p, own, r.stretch); LwStepUnlock();
    return out;
}
/* 0xA115B0(pool, hash): a pool thread's yield, reached from 0xA00650 -> 0xA11580 only on a thread with a pool (its TLS
 * +0x98), so the job threads never pay for it; outside a path request it is passed straight on. */
static uintptr_t LwStepYield(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    CoStepReq* r = t_lwStepReq;
    if (!r) return ((LwStepFourFn)g_lwAiStep.yield)(a, b, c, d);
    const int64_t t = LwStepNow();
    uintptr_t out = ((LwStepFourFn)g_lwAiStep.yield)(a, b, c, d);
    co_stepstat_req_yield(r, t, LwStepNow());
    return out;
}

/* The cutter add 0xBE8840(H, gen) and remove 0xBE8D10(H, gen) push command 10 and 11 to [H+0x4A0] on the caller's
 * thread; the remove first scans the [H+0x228] list for its index. */
static void LwStepCut(int remove)
{
    const int game = GetCurrentThreadId() == g_lwAiStep.game;
    LwStepLock(); co_stepstat_cutter(&g_lwAiStep.q, remove, game); LwStepUnlock();
}
static uintptr_t LwStepAdd(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { LwStepCut(0); return ((LwStepFourFn)g_lwAiStep.add)(a, b, c, d); }
static uintptr_t LwStepRemove(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { LwStepCut(1); return ((LwStepFourFn)g_lwAiStep.remove)(a, b, c, d); }

/* The run's first task, ApplyAsyncWorldUpdatesTask (vtable 0x1482180, +0x10), files each queued change of type 7..11
 * into its context's arrays (counts at [task+0x10] + 0x1A8 + 0x10 * (type - 7)); FireEventsTask zeroes 7..9 later in
 * the same run (0xC96013..0xC96100), so a frame's read misses them: the counts are taken around this task. */
static int LwStepCounts(uintptr_t task, int32_t n[CO_STEP_TYPES])
{
    uintptr_t ctx = 0;
    if (SafeRead(task + 0x10, &ctx, 8) || !ctx) return 0;
    for (int k = 0; k < CO_STEP_TYPES; k++)
        if (SafeRead(ctx + 0x1A8u + 0x10u * (uintptr_t)k, &n[k], 4)) return 0;
    return 1;
}
static uintptr_t LwStepApply(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    int32_t before[CO_STEP_TYPES], after[CO_STEP_TYPES];
    const int read = LwStepCounts(a, before);
    uintptr_t out = ((LwStepFourFn)g_lwAiStep.apply)(a, b, c, d);
    if (read && LwStepCounts(a, after)) { LwStepLock(); co_stepstat_applied(&g_lwAiStep.q, before, after); LwStepUnlock(); }
    return out;
}

typedef struct LwStepPin { uint32_t rva; const char* bytes; } LwStepPin;
/* The step's entry, its caller's dt in xmm1 (0x7D86E2), and its calls of the four parts with the drive's budget. */
static const LwStepPin kLwStepRun[] = {
    {0xBD1890u, "48 8B C4 48 81 EC E8 00 00 00 48 89 58 10"}, {0x7D86E2u, "0F 28 CE 4A 8B 0C 33 E8 A2 91 3F 00"},
    {0xBD1963u, "E8 18 FA FF FF"}, {0xBD198Cu, "F3 0F 10 8B 7C 04 00 00 48 8B CB E8 04 EB FF FF"}, {0xBD19C9u, "E8 12 EC FF FF"},
    {0xBD1A38u, "E8 73 F1 FF FF"},
};
static const LwStepPin kLwStepParts[CO_STEP_PARTS] = {
    {0xBD1380u, "48 89 5C 24 18 48 89 74 24 20 55 57 41 56"}, {0xBD04A0u, "40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20"},
    {0xBD05E0u, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 50"}, {0xBD0BB0u, "48 89 5C 24 10 48 89 74 24 18 55 57 41 56"},
};
/* The pool's yield, its wait, and the way the yield points reach it; the request, its one caller (ProcessPathRequests)
 * and its two yields. */
static const LwStepPin kLwStepYield[] = {
    {0xA115B0u, "48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC 20"}, {0xA1163Bu, "E8 80 7F 0A 00"}, {0xA00650u, "E9 2B 0F 01 00"},
    {0xA11580u, "8B 15 BA 0E FA 00 44 8B C1 65 48 8B 04 25 58 00 00 00 B9 98 00 00 00"}, {0xA115A7u, "E9 04 00 00 00"},
};
static const LwStepPin kLwStepReqs[] = {
    {0xC97D50u, "48 89 5C 24 08 48 89 74 24 10 55 57 41 56 48 8B EC"}, {0xC97B43u, "E8 08 02 00 00"},
    {0xC97D6Bu, "B9 2D 02 36 87 48 8B F2 E8 D8 88 D6 FF"}, {0xC97F59u, "B9 2C 02 36 87"}, {0xC97F70u, "E9 DB 86 D6 FF"},
};
static const LwStepPin kLwStepAdd[] = {{0xBE8840u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40"}, {0xBE88C1u, "C7 44 24 22 00 08 0A 00"}};
static const LwStepPin kLwStepRemove[] = {
    {0xBE8D10u, "48 89 5C 24 18 56 48 83 EC 40 48 89 7C 24 58"}, {0xBE8D32u, "48 63 86 30 02 00 00"}, {0xBE8D78u, "C7 44 24 22 00 08 0B 00"},
};
/* The task's entry, its context [rsi+0x10], the switch on the change's type word from 7 and the five arrays, the count. */
static const LwStepPin kLwStepApply[] = {
    {0xC958E0u, "48 89 5C 24 10 48 89 6C 24 18 56 48 81 EC B0 00 00 00"}, {0xC958F5u, "48 8B F1"},
    {0xC95970u, "0F B7 53 04 83 EA 07 74 55 83 EA 01 74 43 83 EA 01 74 31 83 EA 01 74 1F 83 EA 01 74 0D 48 8B CB E8 DB 92 FA FF E9 85 00 00 00 "
                "48 8B 7E 10 48 81 C7 E0 01 00 00 EB 32 48 8B 7E 10 48 81 C7 D0 01 00 00 EB 25 48 8B 7E 10 48 81 C7 C0 01 00 00 EB 18 "
                "48 8B 7E 10 48 81 C7 B0 01 00 00 EB 0B 48 8B 7E 10 48 81 C7 A0 01 00 00 8B 47 0C"},
    {0xC95A1Cu, "89 57 08"},
};

/* Every pin, then the hook on the first one's address; 1 when it is in. */
static int LwStepHook(uintptr_t exe, const char* what, const LwStepPin* pins, size_t count, void* detour, void** original)
{
    for (size_t i = 0; i < count; i++)
        if (!ApiCodeMatches(exe + pins[i].rva, pins[i].bytes)) return 0;
    return !LoaderHook(what, exe + pins[0].rva, detour, original) && *original;
}
#define LW_STEP_HOOK(what, pins, detour, original) LwStepHook(exe, what, pins, sizeof pins / sizeof pins[0], (void*)detour, original)

static void LwStepInit(void)
{
    static int done;
    if (done++) return;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_lwAiStep.freq = f.QuadPart;
    const uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    static const char* const hooks[CO_STEP_PARTS] = {"AI step: the run start", "AI step: the drive", "AI step: the commit", "AI step: the navigators"};
    static const char* const names[CO_STEP_PARTS] = {"run start", "drive", "commit", "navigators"};
    void* const parts[CO_STEP_PARTS] = {(void*)LwStepStart, (void*)LwStepDrive, (void*)LwStepCommit, (void*)LwStepNav};
    const char* missing[CO_STEP_PARTS + 5]; int n = 0;
    /* a part's calls are inside a step only while the step is hooked: without it each would count as outside one */
    if (LW_STEP_HOOK("AI step: the step", kLwStepRun, LwStepRun, &g_lwAiStep.run))
    {
        for (int k = 0; k < CO_STEP_PARTS; k++)
            if (!LwStepHook(exe, hooks[k], &kLwStepParts[k], 1, parts[k], &g_lwAiStep.part[k])) missing[n++] = names[k];
    }
    else missing[n++] = "the step and its parts";
    /* without the yield's hook a request's time would hold its blocked yields: no request is measured then */
    g_lwAiStep.paths = LW_STEP_HOOK("AI step: a pool thread's yield", kLwStepYield, LwStepYield, &g_lwAiStep.yield) &&
        LW_STEP_HOOK("AI step: a path request", kLwStepReqs, LwStepReq, &g_lwAiStep.req);
    if (!g_lwAiStep.paths) missing[n++] = "path requests";
    if (!LW_STEP_HOOK("AI queue: a cutter added", kLwStepAdd, LwStepAdd, &g_lwAiStep.add)) missing[n++] = "cutters added";
    if (!LW_STEP_HOOK("AI queue: a cutter removed", kLwStepRemove, LwStepRemove, &g_lwAiStep.remove)) missing[n++] = "cutters removed";
    if (!LW_STEP_HOOK("AI queue: a run's changes", kLwStepApply, LwStepApply, &g_lwAiStep.apply)) missing[n++] = "the runs' changes by type";
    g_lwAiStep.installed = 1;
    char text[256] = ""; size_t at = 0;
    for (int i = 0; i < n && at < sizeof text; i++) at += (size_t)snprintf(text + at, sizeof text - at, "%s%s", i ? ", " : "", missing[i]);
    if (n) Note("living world: AI step and queue lines: not measured (not the code the hooks expect, or refused): %s; they read 0", text);
}

/* Game thread, once a frame from the AI budget's frame on a complex level: the game thread's id, the mission's AI
 * world (H = [exe+0x190D4A0 + 2*0x80]) and its generator count [H+0x230] (the remove's scan length, 0xBE8D32). */
static void LwStepFrame(void)
{
    if (!g_lwAiStep.installed) return;
    g_lwAiStep.game = GetCurrentThreadId();
    uintptr_t h = 0; int32_t gens = -1;
    if (SafeRead((uintptr_t)GetModuleHandleA(NULL) + LW_AIW_TABLE + 2u * 0x80u, &h, 8)) h = 0;
    g_lwAiStep.world = h;
    if (!h || SafeRead(h + 0x230, &gens, 4) || gens < 0) gens = -1;
    const int due = co_stepstat_tick(&g_lwAiStep.clock, (long)g_lwMission, GetTickCount64());
    CoStepWin w = {0}; CoStepPaths p = {0}; CoStepQueue q = {0};
    LwStepLock();
    co_stepstat_gens(&g_lwAiStep.q, gens);
    if (due != CO_STEP_IDLE)
    {
        w = g_lwAiStep.win; p = g_lwAiStep.p; q = g_lwAiStep.q;
        memset(&g_lwAiStep.win, 0, sizeof g_lwAiStep.win); memset(&g_lwAiStep.p, 0, sizeof g_lwAiStep.p); memset(&g_lwAiStep.q, 0, sizeof g_lwAiStep.q);
        g_lwAiStep.q.gens = g_lwAiStep.q.gensMost = gens;
    }
    LwStepUnlock();
    if (due != CO_STEP_DUE && due != CO_STEP_LAST) return; /* a fresh window drops what ran before it (the ship, a load) */
    char line[900];
    co_stepstat_line1(line, sizeof line, &w, &p, g_lwAiStep.paths, g_lwAiStep.freq);
    Note("living world: AI step (30 s): %s", line);
    co_stepstat_line2(line, sizeof line, &q, h ? LwAibQueued(h) : -1);
    Note("living world: AI queue (30 s): %s", line);
    if (due == CO_STEP_LAST) Note("living world: AI step and queue: %u line pairs this mission; no more until the next", CO_STEP_LINES);
}
