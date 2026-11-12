/* Living world density (KB living-world-density-design): a budget per player bubble instead of one ceiling for the
 * whole map, the network-entity bound and the upload governor. Included by living_world.c after the intents.
 *
 * The larger budget needs Complex Operations' guards in the game's code (natives counted apart from these squads, the
 * crash guards, the AiJob gate at 1024): without them natives would lose their room, so the rules here stay as before. */

#define LW_BUBBLE 500.0f       /* players within this of each other share one bubble (2 x R_on, D) */
#define LW_SYNC_MAX 1900u      /* the synchronizer holds 2048 networked entities on every peer, unchecked (S4) */
#define LW_SYNC_A_UNIT 4u
#define LW_UPLOAD_KBPS 5000.0f /* host upload at 7 peers; scaled by the peer count (D, the decision rule) */

static struct
{
    int on;                       /* -1 unknown, 0 legacy rules, 1 density rules */
    ULONGLONG checkedAt;
    float factor;                 /* the level: 1.5 = L1 */
    int global;                   /* the loader's units over all bubbles */
    int count, of[EVT_MAX_PLAYERS], players[EVT_MAX_PLAYERS], units[EVT_MAX_PLAYERS], budget[EVT_MAX_PLAYERS];
    float kbps, budgetKbps;       /* smoothed upload; its budget now */
    ULONGLONG overSince, shedAt;
    uint32_t sync, refusedSync, refusedUpload, shed, refusedFull;
    int fullMax;                  /* C9: loader units at the full update before no new squad comes online (0 off) */
    int budgetOverride;           /* test command "budget N": every bubble's budget is N (measurement runs) */
} g_lwDensity = {.on = -1, .budgetOverride = -1};

/* Test instrument (HELLFORGE_LW_TEST_TRACE=1): the game thread's frame intervals by QPC in 0.5 ms bins to 100 ms, and
 * the peaks of the counts the density limits watch, reported with the 30 s note (measurement 1, KB design step 3). */
static struct { LARGE_INTEGER last; uint32_t bins[201], frames, over33; double worst; uint32_t rays, pairs, jobs, sync; } g_lwFrames;
/* The time from one frame over 33 ms to the next, in bins: a stall on a timer shows as one full bin. */
static struct { LARGE_INTEGER last; uint32_t gap[6]; } g_lwSlowGaps;

static void LwDensityBudgetCmd(int n)
{
    g_lwDensity.budgetOverride = n;
    Note("living world: test: every bubble's budget is %d%s", n, n < 0 ? " (the rules' own)" : "");
}

static int LwTraceOn(void)
{
    static int on = -1;
    if (on < 0)
    {
        char t[4] = "";   /* TEST_FRAMES alone: these notes without the other files' per-event trace lines */
        on = (GetEnvironmentVariableA("HELLFORGE_LW_TEST_TRACE", t, sizeof t) > 0 && t[0] == '1') ||
            (GetEnvironmentVariableA("HELLFORGE_LW_TEST_FRAMES", t, sizeof t) > 0 && t[0] == '1');
    }
    return on;
}

static void LwFlatFrame(double ms); /* living_world_flat.c */
static void LwAllocNote(void); /* living_world_alloc.c */
static void LwFlatNote(void);
static void LwFramesInterval(void)
{
    LARGE_INTEGER now, hz; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&hz);
    if (g_lwFrames.last.QuadPart)
    {
        double ms = 1000.0 * (double)(now.QuadPart - g_lwFrames.last.QuadPart) / (double)hz.QuadPart;
        int bin = (int)(ms * 2.0); g_lwFrames.bins[bin < 200 ? bin : 200]++; g_lwFrames.frames++;
        if (ms > 33.0)
        {
            static const double edge[5] = {250.0, 750.0, 1250.0, 1750.0, 2250.0};
            double since = g_lwSlowGaps.last.QuadPart ? 1000.0 * (double)(now.QuadPart - g_lwSlowGaps.last.QuadPart) / (double)hz.QuadPart : 1e9;
            int bin = 0;
            while (bin < 5 && since >= edge[bin]) bin++;
            g_lwSlowGaps.gap[bin]++;
            g_lwSlowGaps.last = now;
            g_lwFrames.over33++;
        }
        if (ms > g_lwFrames.worst) g_lwFrames.worst = ms;
        LwFlatFrame(ms);
    }
    g_lwFrames.last = now;
}

static void LwFramesSample(void)
{
    if (!LwTraceOn()) return;
    LwFramesInterval();
    uintptr_t w, b, j; uint32_t v;
    if (EvpPtr(g_lw.game + 0x3326340u, &w) && (v = EvpU32(w + 0x793A600u, 0)) > g_lwFrames.rays && v <= 4096) g_lwFrames.rays = v; /* the ray queue (S6) */
    if (EvpPtr(g_lw.game + 0x3326838u, &b) && (v = EvpU32(b + 0x1640, 0)) > g_lwFrames.pairs && v <= 64) g_lwFrames.pairs = v;   /* budget pairs */
    if (EvpPtr(g_lw.game + LW_AIJOB, &j) && (v = EvpU32(j + 0xC, 0)) > g_lwFrames.jobs) g_lwFrames.jobs = v;
    if (g_lwDensity.sync > g_lwFrames.sync) g_lwFrames.sync = g_lwDensity.sync;
}

static double LwFramesPercentile(double q)
{
    uint32_t want = (uint32_t)(q * g_lwFrames.frames), seen = 0;
    for (int i = 0; i <= 200; i++) { seen += g_lwFrames.bins[i]; if (seen > want) return (i + 1) * 0.5; }
    return 100.0;
}

/* Test instrument: main-thread time of the world update and its AI passes, from typed detours whose arguments are the
 * call sites' own (world update 0xAB5EC9, passes 0x57395F..0x573F63: (manager, dt) in rcx / xmm1, or (manager, start,
 * end, dt) with dt in xmm3). It says which pass a LOD should cut before code is written for it. */
typedef uintptr_t (*LwPassDtFn)(uintptr_t manager, float dt);
typedef uintptr_t (*LwPassRangeFn)(uintptr_t manager, uint32_t start, uint32_t end, float dt);
static struct { const char* name; uint32_t rva; const char* pin; void* orig; LONGLONG ticks; } g_lwPass[8] = {
    {"world update", 0x571250, "48 8B C4 48 89 58 18 55"}, {"steering", 0x7D9FA0, "4C 8B DC 49 89 5B 08 55"},
    {"motion", 0x59F440, "41 3B D0 73 61 48 89 5C"}, {"behaviour", 0x843040, "4C 8B DC 57 48 81 EC E0"},
    {"detector", 0x887760, "48 8B C4 55 41 55 48 81"}, {"AiEnemy", 0x7E63B0, "48 8B C4 57 41 54 48 83"},
    {"squad", 0x8038D0, "4C 8B DC 55 56 57 41 54"}, {"squad behaviour", 0x7FC900, "48 8B C4 55 41 57 48 83"}};
#define LW_PASS_TIMED(i, call) LARGE_INTEGER a, b; QueryPerformanceCounter(&a); uintptr_t r = call; QueryPerformanceCounter(&b); \
    g_lwPass[i].ticks += b.QuadPart - a.QuadPart; return r;
#define LW_PASS_DT(i) static uintptr_t LwPassDt##i(uintptr_t m, float dt) { LW_PASS_TIMED(i, ((LwPassDtFn)g_lwPass[i].orig)(m, dt)) }
#define LW_PASS_RANGE(i) static uintptr_t LwPassRange##i(uintptr_t m, uint32_t s, uint32_t e, float dt) { LW_PASS_TIMED(i, ((LwPassRangeFn)g_lwPass[i].orig)(m, s, e, dt)) }
/* Test instrument (HELLFORGE_LW_PROFILE=1 with the test trace): a sampler thread stops the game thread about every
 * millisecond it spends in the world update and counts the world update's call site on its unwound stack, which splits
 * that pass's time over its ~300 component calls. While the thread is stopped the sampler only copies its context and
 * stack: taking a lock there could wait on one the stopped thread holds. */
#define LW_PROF_SITES 512
#define LW_WU_END 0x5740A4u                 /* .pdata end of 0x571250 */
static struct {
    volatile LONG inside; volatile uintptr_t top; HANDLE thread; int on; DWORD threadId; uint32_t frame;
    uint32_t ret[LW_PROF_SITES]; volatile uint32_t count[LW_PROF_SITES], seen[LW_PROF_SITES];
    volatile uint32_t samples, self, lost, unknown, disagree; uint32_t seenSamples, seenSelf, seenLost, seenUnknown, seenDisagree;
    uint32_t fnLo[8], fnHi[8]; int fns;   /* HELLFORGE_LW_PROFILE_FN=lo-hi,...: nested functions whose call sites count too */
} g_lwProf;

static int LwProfCallBefore(const uint8_t* r)
{
    return r[-5] == 0xE8 || r[-2] == 0xFF || r[-3] == 0xFF || r[-4] == 0xFF || r[-6] == 0xFF || r[-7] == 0xFF;
}

static void LwProfCount(uint32_t ret)
{
    uint32_t h = (ret * 2654435761u) >> 23;
    for (int k = 0; k < LW_PROF_SITES; k++, h = (h + 1) & (LW_PROF_SITES - 1))
    {
        if (g_lwProf.ret[h] == ret) { g_lwProf.count[h]++; return; }
        if (g_lwProf.ret[h] == 0) { g_lwProf.ret[h] = ret; g_lwProf.count[h]++; return; }
    }
    g_lwProf.unknown++;
}

/* The highest value in the world update's range is its own return slot: its callees write theirs further down. Kept
 * as a cross-check of the unwound chain at the world update's own level, where it is exact. */
static uintptr_t LwProfReturn(uintptr_t rsp, uintptr_t top, uintptr_t lo, uintptr_t hi)
{
    if (rsp >= top || top - rsp >= (1u << 20)) return 0;
    for (const uintptr_t* q = (const uintptr_t*)top - 1; (uintptr_t)q >= rsp; q--)
        if (*q > lo + 8 && *q < hi && LwProfCallBefore((const uint8_t*)*q)) return *q;
    return 0;
}

/* The stack between RSP and the detour's frame, copied while the thread is stopped and unwound after it runs again
 * (the unwinder's function lookup may lock). The slack after it takes a large frame's reads past the copy. */
#define LW_PROF_STACK 0x20000
static uint8_t g_lwProfStack[LW_PROF_STACK + 0x10000];

static void LwProfShift(CONTEXT* w, uintptr_t lo, uintptr_t hi, intptr_t delta)
{
    DWORD64* r[] = {&w->Rbx, &w->Rbp, &w->Rsi, &w->Rdi, &w->R12, &w->R13, &w->R14, &w->R15};
    for (int i = 0; i < 8; i++) if (*r[i] >= lo && *r[i] < hi) *r[i] += delta;
}

/* A frame that addresses its stack through a frame register needs that register inside the copy, or the unwinder
 * would read wherever it points. */
static int LwProfFrameInCopy(const CONTEXT* w, DWORD64 base, const RUNTIME_FUNCTION* f, uintptr_t end)
{
    DWORD unwind = f->UnwindData;
    if (unwind & 1) unwind = ((const RUNTIME_FUNCTION*)(base + (unwind & ~1u)))->UnwindData;
    int reg = ((const uint8_t*)(base + unwind))[3] & 0xF;
    if (reg == 0) return 1;
    DWORD64 value = (&w->Rax)[reg];
    return value >= (DWORD64)(uintptr_t)g_lwProfStack && value < end;
}

/* The return addresses from the leaf up to the first one inside the world update (lo..hi), unwound on the copy of
 * `len` bytes taken at c->Rsp: their count, 0 when the walk did not reach the world update. */
static int LwProfChain(const CONTEXT* c, size_t len, uintptr_t lo, uintptr_t hi, uintptr_t* rets, int most)
{
    uintptr_t from = (uintptr_t)c->Rsp, end = (uintptr_t)g_lwProfStack + len;
    intptr_t delta = (intptr_t)(uintptr_t)g_lwProfStack - (intptr_t)from;
    CONTEXT w = *c;
    LwProfShift(&w, from, from + len, delta);
    w.Rsp += delta;
    int n = 0;
    rets[n++] = c->Rip;
    while (n < most && !(rets[n - 1] >= lo && rets[n - 1] < hi && n > 1))
    {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(w.Rip, &base, NULL);
        if (f == NULL) { w.Rip = *(const DWORD64*)(uintptr_t)w.Rsp; w.Rsp += 8; }
        else
        {
            if (!LwProfFrameInCopy(&w, base, f, end)) return 0;
            PVOID data = NULL; DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, w.Rip, f, &w, &data, &frame, NULL);
            LwProfShift(&w, from, from + len, delta);
        }
        if (w.Rip == 0 || w.Rsp < (DWORD64)(uintptr_t)g_lwProfStack || w.Rsp + 8 > end) return 0;
        rets[n++] = (uintptr_t)w.Rip;
    }
    return n > 1 && rets[n - 1] >= lo && rets[n - 1] < hi ? n : 0;
}

/* A nested function's key from the chain below the world update: the outermost call site inside it, or its start with
 * the top bit when only the leaf is inside it; 0 when it is not on the chain. */
static uint32_t LwProfNested(const uintptr_t* rets, int n, int f)
{
    uintptr_t lo = g_lw.game + g_lwProf.fnLo[f], hi = g_lw.game + g_lwProf.fnHi[f];
    for (int i = n - 2; i >= 1; i--) if (rets[i] >= lo && rets[i] < hi) return (uint32_t)(rets[i] - g_lw.game);
    return rets[0] >= lo && rets[0] < hi ? g_lwProf.fnLo[f] | 0x80000000u : 0;
}

static void LwProfTally(const uintptr_t* rets, int n, uintptr_t scanned, uintptr_t lo, uintptr_t hi)
{
    g_lwProf.samples++;
    if (rets[0] >= lo && rets[0] < hi) { g_lwProf.self++; return; }
    if (n == 0) { g_lwProf.lost++; return; }
    LwProfCount((uint32_t)(rets[n - 1] - g_lw.game));
    if (scanned != rets[n - 1]) g_lwProf.disagree++;
    for (int f = 0; f < g_lwProf.fns; f++) { uint32_t key = LwProfNested(rets, n, f); if (key) LwProfCount(key); }
}

/* One stop of the game thread: its context, the stack copy (`len` 0 when it does not fit) and the scan's call site,
 * nothing else while it is suspended. 0 when the thread was not stopped inside the world update. */
static int LwProfSnap(CONTEXT* c, size_t* len, uintptr_t* scanned, uintptr_t lo, uintptr_t hi)
{
    if (!g_lwProf.inside || SuspendThread(g_lwProf.thread) == (DWORD)-1) return 0;
    memset(c, 0, sizeof *c);
    c->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    int ok = g_lwProf.inside && GetThreadContext(g_lwProf.thread, c);
    uintptr_t top = g_lwProf.top;
    *len = ok && c->Rsp < top && top - c->Rsp + 64 <= LW_PROF_STACK ? (size_t)(top - c->Rsp + 64) : 0;
    if (*len) memcpy(g_lwProfStack, (const void*)(uintptr_t)c->Rsp, *len);
    *scanned = ok && !(c->Rip >= lo && c->Rip < hi) ? LwProfReturn(c->Rsp, top, lo, hi) : 0;
    ResumeThread(g_lwProf.thread);
    return ok;
}

static DWORD WINAPI LwProfThread(LPVOID unused)
{
    const uintptr_t lo = g_lw.game + 0x571250, hi = g_lw.game + LW_WU_END;
    uintptr_t rets[64];
    CONTEXT c;
    for (;;)
    {
        Sleep(1);
        size_t len = 0;
        uintptr_t scanned = 0;
        if (!LwProfSnap(&c, &len, &scanned, lo, hi)) continue;
        rets[0] = (uintptr_t)c.Rip;
        int n = len && !(c.Rip >= lo && c.Rip < hi) ? LwProfChain(&c, len, lo, hi, rets, 64) : 0;
        LwProfTally(rets, n, scanned, lo, hi);
    }
    return 0;
}

/* HELLFORGE_LW_PROFILE_FN=lo-hi,... (game.dll offsets): ranges the sampler reads code bytes in, so a bad one ends the
 * list there instead of faulting the sampler. */
static void LwProfRanges(void)
{
    char fn[200] = "";
    GetEnvironmentVariableA("HELLFORGE_LW_PROFILE_FN", fn, sizeof fn);
    const char* at = fn;
    for (int f = 0; f < 8 && at != NULL && sscanf(at, "%x-%x", &g_lwProf.fnLo[f], &g_lwProf.fnHi[f]) == 2; f++)
    {
        if (g_lwProf.fnHi[f] <= g_lwProf.fnLo[f] + 8 || g_lwProf.fnHi[f] - g_lwProf.fnLo[f] > 0x40000u || g_lwProf.fnLo[f] < 0x1000u) break;
        g_lwProf.fns = f + 1;
        if ((at = strchr(at, ',')) != NULL) at++;
    }
}

static void LwProfStart(void)
{
    char t[4] = "";
    g_lwProf.on = GetEnvironmentVariableA("HELLFORGE_LW_PROFILE", t, sizeof t) > 0 && t[0] == '1' ? 1 : -1;
    if (g_lwProf.on > 0) LwProfRanges();
    if (g_lwProf.on > 0 && ((g_lwProf.thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetCurrentThreadId())) == NULL ||
        CreateThread(NULL, 0, LwProfThread, NULL, 0, NULL) == NULL)) g_lwProf.on = -1;
    Note("living world: test: world update profile %s, %d nested functions", g_lwProf.on > 0 ? "on" : "off", g_lwProf.fns);
}

static uintptr_t LwPassDt0(uintptr_t m, float dt)
{
    if (g_lwProf.on == 0) LwProfStart();
    g_lwProf.threadId = GetCurrentThreadId();
    g_lwProf.frame++;
    g_lwProf.top = (uintptr_t)__builtin_frame_address(0);
    InterlockedExchange(&g_lwProf.inside, 1);
    LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
    uintptr_t r = ((LwPassDtFn)g_lwPass[0].orig)(m, dt);
    QueryPerformanceCounter(&b);
    InterlockedExchange(&g_lwProf.inside, 0);
    g_lwPass[0].ticks += b.QuadPart - a.QuadPart;
    return r;
}

/* One note line of the 12 sites with most samples left in delta (taken out of it): 0 when none is left. */
static int LwProfLine(uint32_t* delta, uint32_t n, double wuMs)
{
    char text[900]; size_t used = 0;
    for (int k = 0; k < 12 && used < sizeof text - 40; k++)
    {
        int best = -1;
        for (int i = 0; i < LW_PROF_SITES; i++) if (delta[i] && (best < 0 || delta[i] > delta[best])) best = i;
        if (best < 0) break;
        used += (size_t)snprintf(text + used, sizeof text - used, " %x %.1f%% %.2f", g_lwProf.ret[best], 100.0 * delta[best] / n, wuMs * delta[best] / n);
        delta[best] = 0;
    }
    if (used > 0) Note("living world: profile sites (return, share, ms a frame):%s", text);
    return used > 0;
}

/* The 30 s profile: the sites with most samples since the last note, each with its share and the world update's
 * milliseconds a frame times that share. */
static void LwProfNote(double wuMs)
{
    if (g_lwProf.on <= 0) return;
    uint32_t n = g_lwProf.samples - g_lwProf.seenSamples, self = g_lwProf.self - g_lwProf.seenSelf;
    uint32_t lost = g_lwProf.lost - g_lwProf.seenLost, unknown = g_lwProf.unknown - g_lwProf.seenUnknown;
    uint32_t disagree = g_lwProf.disagree - g_lwProf.seenDisagree;
    g_lwProf.seenSamples += n; g_lwProf.seenSelf += self; g_lwProf.seenLost += lost; g_lwProf.seenUnknown += unknown;
    g_lwProf.seenDisagree += disagree;
    uint32_t delta[LW_PROF_SITES];
    for (int i = 0; i < LW_PROF_SITES; i++) { uint32_t c = g_lwProf.count[i]; delta[i] = c - g_lwProf.seen[i]; g_lwProf.seen[i] = c; }
    if (n == 0) return;
    Note("living world: profile (30 s): %u samples in the world update (%.2f ms a frame): self %.1f%%, not unwound %.1f%%, table full %u, chain and scan disagree %u",
        n, wuMs, 100.0 * self / n, 100.0 * lost / n, unknown, disagree);
    for (int line = 0; line < 4 + 2 * g_lwProf.fns && LwProfLine(delta, n, wuMs); line++) {}
}

LW_PASS_RANGE(1) LW_PASS_RANGE(2) LW_PASS_DT(3) LW_PASS_DT(4) LW_PASS_DT(5) LW_PASS_DT(6) LW_PASS_DT(7)
static void* const g_lwPassDetour[8] = {(void*)LwPassDt0, (void*)LwPassRange1, (void*)LwPassRange2, (void*)LwPassDt3,
    (void*)LwPassDt4, (void*)LwPassDt5, (void*)LwPassDt6, (void*)LwPassDt7};

/* Test instrument (with the profile): calls a frame of the objective count 0x5DDC70, (rcx unused, edx entity id) ->
 * int. Each call scans every objective_dynamic_link and recurses into linked objectives, so its cost in a layout is
 * calls x links; m8 measured 4.3 ms of it in some XL layouts and 0.1 ms in others. */
typedef uintptr_t (*LwObjCountFn)(uintptr_t unused, uint32_t id);
static struct {
    void* orig; volatile LONG calls, top; LONG maxDepth;
    uint32_t repeats, differ, id[512], stamp[512], value[512];   /* same-frame memo check, game thread only */
} g_lwObjCount;
static _Thread_local LONG g_lwObjDepth;

/* A per-frame cache would be exact only if a repeat call in one world update returns what the first call did; this
 * counts the repeats a cache would save and any that differ, on the game thread inside the world update only. */
static void LwObjMemoCheck(uint32_t id, uint32_t value)
{
    uint32_t h = (id * 2654435761u) >> 23;
    for (int k = 0; k < 512; k++, h = (h + 1) & 511)
    {
        if (g_lwObjCount.stamp[h] != g_lwProf.frame) { g_lwObjCount.stamp[h] = g_lwProf.frame; g_lwObjCount.id[h] = id; g_lwObjCount.value[h] = value; return; }
        if (g_lwObjCount.id[h] == id) { g_lwObjCount.repeats++; if (g_lwObjCount.value[h] != value) g_lwObjCount.differ++; return; }
    }
}

static uintptr_t LwObjCount(uintptr_t unused, uint32_t id)
{
    InterlockedIncrement(&g_lwObjCount.calls);
    if (g_lwObjDepth == 0) InterlockedIncrement(&g_lwObjCount.top);
    if (++g_lwObjDepth > g_lwObjCount.maxDepth) g_lwObjCount.maxDepth = g_lwObjDepth;
    uintptr_t r = ((LwObjCountFn)g_lwObjCount.orig)(unused, id);
    g_lwObjDepth--;
    if (g_lwProf.inside && GetCurrentThreadId() == g_lwProf.threadId) LwObjMemoCheck(id, (uint32_t)r);
    return r;
}

static void LwObjCountNote(uint32_t frames)
{
    if (g_lwObjCount.orig == NULL || frames == 0) return;
    uintptr_t objectives, links;
    uint32_t nObj = EvpPtr(g_lw.game + 0x3326DA0u, &objectives) ? EvpU32(objectives + 0x24, 0) : 0;   /* loop head 0x5D2E15 */
    uint32_t nLink = EvpPtr(g_lw.game + 0x3326D08u, &links) ? EvpU32(links + 0xC, 0) : 0;            /* scan 0x5DE528 */
    Note("living world: objective count 0x5DDC70: %.1f calls a frame (%.1f from outside it), deepest %ld; %u objectives, %u dynamic links; same-frame repeats %.1f a frame, %u differing",
        (double)InterlockedExchange(&g_lwObjCount.calls, 0) / frames, (double)InterlockedExchange(&g_lwObjCount.top, 0) / frames,
        g_lwObjCount.maxDepth, nObj, nLink, (double)g_lwObjCount.repeats / frames, g_lwObjCount.differ);
    g_lwObjCount.maxDepth = 0; g_lwObjCount.repeats = 0; g_lwObjCount.differ = 0;
}

static void LwInstallObjCount(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_PROFILE", t, sizeof t) == 0 || t[0] != '1') return;
    if (!ApiCodeMatches(g_lw.game + 0x5DDC70u, "40 57 41 55 48 81 EC 98") ||
        LoaderHook("living world: objective count (test profile)", g_lw.game + 0x5DDC70u, (void*)LwObjCount, &g_lwObjCount.orig))
    { g_lwObjCount.orig = NULL; Note("living world: test: the objective count 0x5DDC70 is not counted"); }
}

static void LwInstallPassTimes(void)
{
    static int done;
    if (!LwTraceOn() || done++) return;
    LwInstallObjCount();
    for (int i = 0; i < 8; i++)
        if (!ApiCodeMatches(g_lw.game + g_lwPass[i].rva, g_lwPass[i].pin) ||
            LoaderHook("living world: pass times (test trace)", g_lw.game + g_lwPass[i].rva, g_lwPassDetour[i], &g_lwPass[i].orig) ||
            g_lwPass[i].orig == NULL)
        { g_lwPass[i].orig = NULL; Note("living world: test: pass %s (0x%X) is not timed", g_lwPass[i].name, g_lwPass[i].rva); }
}

static void LwPassNote(uint32_t frames)
{
    LARGE_INTEGER hz; QueryPerformanceFrequency(&hz);
    char line[400]; size_t used = 0; double sum = 0, wuMs = 0;
    for (int i = 0; i < 8 && used < sizeof line - 40; i++)
    {
        double ms = 1000.0 * (double)g_lwPass[i].ticks / (double)hz.QuadPart / frames;
        if (i > 0) sum += ms; else wuMs = ms;
        used += (size_t)snprintf(line + used, sizeof line - used, "%s%s %.2f", i ? ", " : "", g_lwPass[i].name, ms);
        g_lwPass[i].ticks = 0;
    }
    line[used < sizeof line ? used : sizeof line - 1] = 0;
    Note("living world: pass times (ms a frame): %s; AI passes together %.2f", line, sum);
    LwProfNote(wuMs);
    LwObjCountNote(frames);
}

static void LwRecNote(void); /* living_world_records.c */
static void LwMoverNote(void); static int LwMoverRoom(uint32_t members); static void LwMoverSent(uint32_t members); static uint32_t LwMoverPending(void); /* living_world_mover.c */
static void LwTreeNote(void); /* living_world_trees.c */
/* Test note with the frame times: loader units by their squad's distance to the nearest player and in a player's
 * view, to set against the slow frames (the online count alone does not predict them). */
static void LwNearNote(void)
{
    int within[3] = {0, 0, 0}, viewed = 0, online = 0;
    static const float reach[3] = {50.0f, 100.0f, 200.0f};
    for (int s = 0; s < g_lwWorld.count && s < LW_SQUADS_MAX; s++)
    {
        const LwSquad* q = &g_lwSquads[s];
        if (q->state != LW_ONLINE) continue;
        float d2 = LwNearestPlayer2(q->pos[0], q->pos[1]);
        online += q->alive;
        if (q->inView) viewed += q->alive;
        for (int k = 0; k < 3; k++) if (d2 < reach[k] * reach[k]) within[k] += q->alive;
    }
    Note("living world: near (now): %d units within 50 m of a player, %d within 100 m, %d within 200 m, %d in a player's view, of %d online",
        within[0], within[1], within[2], viewed, online);
}

static void LwFramesNote(void)
{
    static int minutes;
    if (g_lwFrames.frames > 0 && (minutes++ & 1) == 0) LwSyncCensus(0); /* every minute with the 30 s notes (test runs) */
    if (g_lwFrames.frames == 0) return;
    Note("living world: frames (30 s, %d units online): %u, p50 %.1f ms, p99 %.1f ms, worst %.1f ms, %u over 33 ms; peaks: ray queue %u, budget pairs %u, AiJob %u, networked entities %u",
        g_lwWorld.onlineUnits, g_lwFrames.frames, LwFramesPercentile(0.5), LwFramesPercentile(0.99), g_lwFrames.worst, g_lwFrames.over33,
        g_lwFrames.rays, g_lwFrames.pairs, g_lwFrames.jobs, g_lwFrames.sync);
    if (g_lwPass[0].orig) LwPassNote(g_lwFrames.frames);
    if (g_lwFrames.over33)
        Note("living world: slow frames (30 s): time since the one before: %u under 0.25 s, %u to 0.75 s, %u to 1.25 s, %u to 1.75 s, %u to 2.25 s, %u longer",
            g_lwSlowGaps.gap[0], g_lwSlowGaps.gap[1], g_lwSlowGaps.gap[2], g_lwSlowGaps.gap[3], g_lwSlowGaps.gap[4], g_lwSlowGaps.gap[5]);
    memset(g_lwSlowGaps.gap, 0, sizeof g_lwSlowGaps.gap);
    LwRecNote();
    LwMoverNote();
    LwTreeNote();
    LwNearNote();
    LwAllocNote();
    LwFlatNote();
    LARGE_INTEGER last = g_lwFrames.last;
    memset(&g_lwFrames, 0, sizeof g_lwFrames);
    g_lwFrames.last = last;
}

/* Test instrument: the synchronizer's networked entities by resource (KB living-world-density, measurement 1: 1666
 * of 2048 on XL before any enemy). Goid -> record through the network-id hash (state +0xF22EC8, as 0xFD9BA0), the
 * record (relocated on XL, g_entityRecordDisp) gives resource and unit; near = within 600 m of a player. Read only. */
static struct { uint64_t resource; uint32_t count, close; } g_lwCensus[256];
static int g_lwCensusUsed;

/* One goid's record: 0 when the hash or the record can't be read. */
static int LwCensusRecord(uintptr_t g, uint32_t goid, uint64_t* resource, uint32_t* unit)
{
    uint32_t index = 0; uint8_t d[24];
    if (!EvpHash(g + 0xF22EC8u, goid, 8, 4, &index) || index >= 0x100000) return 0;
    if (SafeRead(g + (uintptr_t)g_entityRecordDisp + (uintptr_t)index * 24, d, sizeof d)) return 0;
    memcpy(resource, d, 8); memcpy(unit, d + 12, 4);
    return 1;
}

static int LwCensusAdd(uint64_t resource, int close)
{
    int k = 0;
    while (k < g_lwCensusUsed && g_lwCensus[k].resource != resource) k++;
    if (k == g_lwCensusUsed)
    {
        if (k == 256) return 0;
        g_lwCensus[k].resource = resource; g_lwCensus[k].count = g_lwCensus[k].close = 0; g_lwCensusUsed++;
    }
    g_lwCensus[k].count++; g_lwCensus[k].close += (uint32_t)close;
    return 1;
}

static void LwCensusTop(int seq)
{
    for (int shown = 0; shown < 80 && g_lwCensusUsed > 0; shown++)
    {
        int best = 0;
        for (int k = 1; k < g_lwCensusUsed; k++) if (g_lwCensus[k].count > g_lwCensus[best].count) best = k;
        if (g_lwCensus[best].count == 0) break;
        Note("living world: census %d: %016llx x%u (%u near)", seq, (unsigned long long)g_lwCensus[best].resource, g_lwCensus[best].count, g_lwCensus[best].close);
        g_lwCensus[best].count = 0;
    }
}

static void LwSyncCensus(int seq)
{
    uintptr_t g, s;
    if (!EvpPtr(g_lw.game + 0x346BF98u, &g) || !EvpPtr(g + 8, &s)) { Note("living world: census %d: no synchronizer", seq); return; }
    uint32_t n = EvpU32(s + 0x18, 0), unknown = 0, unitless = 0, nearCount = 0;
    if (n > 2048) n = 2048;
    g_lwCensusUsed = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint64_t resource; uint32_t unit = 0; float p[3];
        if (!LwCensusRecord(g, EvpU32(s + 0x1C + 4u * i, 0), &resource, &unit)) { unknown++; continue; }
        int close = unit && EvpUnitPosition(unit, p) && g_lwWorld.playersAt > 0 && LwNearestPlayer2(p[0], p[1]) < 600.0f * 600.0f;
        unitless += !unit;
        nearCount += (uint32_t)close;
        if (!LwCensusAdd(resource, close)) unknown++;
    }
    Note("living world: census %d: %u networked entities, %d resources, %u within 600 m of a player, %u without a unit, %u not found", seq, n, g_lwCensusUsed, nearCount, unitless, unknown);
    LwCensusTop(seq);
}

/* Test instrument: every synchronizer entry as {net id, resource, position}, so two peers' lists can be compared (does
 * a client spawn the level's entities itself, with the same net ids as the host?). */
static void LwSyncDump(int seq)
{
    uintptr_t g, s;
    if (!EvpPtr(g_lw.game + 0x346BF98u, &g) || !EvpPtr(g + 8, &s)) { Note("living world: dump %d: no synchronizer", seq); return; }
    uint32_t n = EvpU32(s + 0x18, 0);
    if (n > 2048) n = 2048;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t id = EvpU32(s + 0x1C + 4u * i, 0), unit = 0; uint64_t resource = 0; float p[3] = {0, 0, 0};
        int found = LwCensusRecord(g, id, &resource, &unit);
        if (found && unit) EvpUnitPosition(unit, p);
        Note("living world: dump %d: %u %04x %016llx %.1f %.1f %.1f", seq, i, id, (unsigned long long)resource, (double)p[0], (double)p[1], (double)p[2]);
    }
    Note("living world: dump %d: %u entries", seq, n);
}

static float LwEnvFloat(const char* name, float fallback)
{
    char t[32] = "";
    if (GetEnvironmentVariableA(name, t, sizeof t) == 0) return fallback;
    float v = (float)atof(t);
    return v > 0 ? v : fallback;
}

/* Complex Operations' guards are in place when its jumps and the raised gate are in the game's code. */
static int LwDensityOn(void)
{
    ULONGLONG now = GetTickCount64();
    if (g_lwDensity.on >= 0 && now - g_lwDensity.checkedAt < 5000) return g_lwDensity.on;
    g_lwDensity.checkedAt = now;
    uint8_t room = 0, slot[5] = {0}, pair = 0; uint32_t gate = 0;
    int guards = !SafeRead(g_lw.game + 0x952F53u, &room, 1) && room == 0xE9 && !SafeRead(g_lw.game + 0x952F65u, &gate, 4) && gate == 0x400u &&
        !SafeRead(g_lw.game + 0x953942u, slot, 5) && memcmp(slot, "\xE9\x32\x07\x00\x00", 5) == 0 && !SafeRead(g_lw.game + 0xA2BE29u, &pair, 1) && pair == 0xE9;
    char t[8] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_DENSITY", t, sizeof t) > 0 && t[0] == '0') guards = 0;
    if (guards != g_lwDensity.on)
    {
        g_lwDensity.factor = LwEnvFloat("HELLFORGE_LW_DENSITY", 1.5f);
        g_lwDensity.global = (int)LwEnvFloat("HELLFORGE_LW_GLOBAL", 190.0f);
        g_lwDensity.fullMax = (int)LwEnvFloat("HELLFORGE_LW_FULL_MAX", 0.0f);
        char b[16] = ""; /* test runs: the budget a measurement starts from (then "budget N") */
        if (guards && GetEnvironmentVariableA("HELLFORGE_LW_TEST_BUDGET", b, sizeof b) > 0) g_lwDensity.budgetOverride = atoi(b);
        if (guards) Note("living world: density x%.2f, at most %d units over all bubbles (Complex Operations' guards are in the game's code)", (double)g_lwDensity.factor, g_lwDensity.global);
        else Note("living world: one budget for the map (48 solo / 64 co-op): Complex Operations' guards are not in the game's code");
    }
    g_lwDensity.on = guards;
    return guards;
}

/* The vanilla-scaled base of one bubble, as the old ceiling took it. */
static int LwBubbleBase(int players) { int ceiling = players > 1 ? 64 : 48; return g_lwWorld.eff < ceiling ? g_lwWorld.eff : ceiling; }

static int LwBubbleOf(float x, float y)
{
    int best = -1; float bd = 3e38f;
    for (int i = 0; i < g_lwWorld.playersAt; i++) { float d = LwDist2(g_lwWorld.players[i], x, y); if (d < bd) { bd = d; best = i; } }
    return best < 0 ? -1 : g_lwDensity.of[best];
}

/* Joins player i's bubble `b` with every player within LW_BUBBLE of a member, and theirs in turn. */
static void LwBubbleGrow(int i, int b)
{
    int stack[EVT_MAX_PLAYERS], top = 0, n = g_lwWorld.playersAt;
    g_lwDensity.of[i] = b; stack[top++] = i;
    while (top)
    {
        const float* p = g_lwWorld.players[stack[--top]];
        for (int j = 0; j < n; j++)
            if (g_lwDensity.of[j] < 0 && LwDist2(p, g_lwWorld.players[j][0], g_lwWorld.players[j][1]) < LW_BUBBLE * LW_BUBBLE) { g_lwDensity.of[j] = b; stack[top++] = j; }
    }
}

static int LwSquadUnits(const LwSquad* q) { return q->state == LW_PENDING ? q->cap : q->state == LW_ONLINE ? q->alive : 0; }

/* Bubbles (players chained within LW_BUBBLE), each with its share of the global budget by its players. */
static void LwBubblesTick(void)
{
    int n = g_lwWorld.playersAt, count = 0;
    for (int i = 0; i < n; i++) g_lwDensity.of[i] = -1;
    for (int i = 0; i < n; i++) if (g_lwDensity.of[i] < 0) LwBubbleGrow(i, count++);
    g_lwDensity.count = count;
    memset(g_lwDensity.players, 0, sizeof g_lwDensity.players); memset(g_lwDensity.units, 0, sizeof g_lwDensity.units);
    for (int i = 0; i < n; i++) g_lwDensity.players[g_lwDensity.of[i]]++;
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        int u = LwSquadUnits(&g_lwSquads[i]), b = u ? LwBubbleOf(g_lwSquads[i].pos[0], g_lwSquads[i].pos[1]) : -1;
        if (b >= 0) g_lwDensity.units[b] += u;
    }
    for (int b = 0; b < count; b++)
    {
        if (g_lwDensity.budgetOverride >= 0) { g_lwDensity.budget[b] = g_lwDensity.budgetOverride; continue; }
        int own = (int)(g_lwDensity.factor * (float)LwBubbleBase(g_lwDensity.players[b]) + 0.5f);
        int share = g_lwDensity.global * g_lwDensity.players[b] / n;
        g_lwDensity.budget[b] = own < share ? own : share;
    }
}

/* Networked entities on this machine (the synchronizer [[game+0x346BF98]+8], count +0x18); 0 when unreadable. */
static uint32_t LwSyncCount(void)
{
    uintptr_t g, s;
    return EvpPtr(g_lw.game + 0x346BF98u, &g) && EvpPtr(g + 8, &s) ? EvpU32(s + 0x18, 0) : 0;
}

/* The host's sent kbit/s (game objects + RPCs, S+0x60E8 / +0x60F8, floats over >= 0.2 s windows); -1 when unreadable.
 * S = [[exe+0x1A10278]+0xA8], the session, null outside one. */
static float LwUploadKbps(void)
{
    static uintptr_t exe;
    if (exe == 0) exe = ApiModuleBase("helldivers2.exe", NULL);
    uintptr_t net, s, vt;
    float objects = 0, rpc = 0;
    if (exe == 0 || !EvpPtr(exe + 0x1A10278u, &net) || !EvpPtr(net + 0xA8, &s) || !EvpPtr(s, &vt) || vt != exe + 0x1675B70u ||
        SafeRead(s + 0x60E8, &objects, 4) || SafeRead(s + 0x60F8, &rpc, 4) || !isfinite(objects) || !isfinite(rpc)) return -1.0f;
    return objects + rpc;
}

/* Once a tick: bubbles, the synchronizer count, the smoothed upload. Refusing new squads does not lower an upload that
 * is already too high, so while it stays a quarter over budget for 5 s the swap's filler (LwFillerPick) goes offline,
 * one a second at most. */
static void LwDensityTick(float dt)
{
    LwFramesSample();
    if (!LwDensityOn()) return;
    LwBubblesTick();
    g_lwDensity.sync = LwSyncCount();
    float kbps = LwUploadKbps();
    int peers = g_lwWorld.playersAt - 1;
    g_lwDensity.budgetKbps = peers > 0 ? LW_UPLOAD_KBPS * (float)peers / 7.0f : 0;
    if (kbps >= 0) g_lwDensity.kbps += (kbps - g_lwDensity.kbps) * fminf(1.0f, dt / 2.0f);
    ULONGLONG now = GetTickCount64();
    int over = peers > 0 && g_lwDensity.kbps > 1.25f * g_lwDensity.budgetKbps;
    if (!over) { g_lwDensity.overSince = 0; return; }
    if (g_lwDensity.overSince == 0) g_lwDensity.overSince = now;
    if (now - g_lwDensity.overSince < 5000 || now - g_lwDensity.shedAt < 1000) return;
    float pickD;
    LwSquad* pick = LwFillerPick(NULL, &pickD);
    g_lwDensity.shedAt = now;
    if (pick == NULL) return;
    pick->forceOff = 2;
    g_lwDensity.shed++;
    Note("living world: upload %.0f kbit/s over %.0f: squad %d (calm, hidden, %.0f m from the nearest player) goes offline",
        (double)g_lwDensity.kbps, (double)g_lwDensity.budgetKbps, (int)(pick - g_lwSquads), (double)sqrtf(pickD));
}

/* The density rules' part of LwRoomFor: 1 when the squad at `pos` may bring `members` more units online. A bubble refusal
 * starts "the cap" so a far or event squad refused by it still swaps out filler (LwSwapForFar, LwHoldForIntent). */
static int LwDensityRoom(const float* pos, uint32_t members, uint32_t held, char* why, size_t whyLen)
{
    /* a unit brings 3 to 4.5 networked entities (itself, weapons, attachments; measured), and those of squads sent in
     * the last seconds are not in the count yet: forced spawns once took it to 2031 past this check */
    uint32_t coming = LW_SYNC_A_UNIT * (LwMoverPending() + members);
    static uint32_t max;   /* HELLFORGE_LW_SYNC_MAX lowers it, so a test reaches the refusal at an ordinary unit count */
    if (max == 0) { float v = LwEnvFloat("HELLFORGE_LW_SYNC_MAX", (float)LW_SYNC_MAX); max = v >= 1.0f && v < (float)LW_SYNC_MAX ? (uint32_t)v : LW_SYNC_MAX; }
    if (g_lwDensity.sync && g_lwDensity.sync + coming > max)
    { g_lwDensity.refusedSync++; snprintf(why, whyLen, "the networked entities: %u + %u over %u", g_lwDensity.sync, coming, max); return 0; }
    if (g_lwTestHold != 2 && !LwMoverRoom(members)) { snprintf(why, whyLen, "the boundary requests: no room for %u more", members); return 0; }
    if (g_lwDensity.budgetKbps > 0 && g_lwDensity.kbps > g_lwDensity.budgetKbps)
    { g_lwDensity.refusedUpload++; snprintf(why, whyLen, "the upload: %.0f kbit/s over %.0f", (double)g_lwDensity.kbps, (double)g_lwDensity.budgetKbps); return 0; }
    int b = LwBubbleOf(pos[0], pos[1]), full = g_lwDensity.fullMax > 0 ? LwLodFullUnits() : 0;
    if (b < 0) { snprintf(why, whyLen, "no player bubble"); return 0; }
    if (g_lwDensity.fullMax > 0 && full >= g_lwDensity.fullMax) /* C9: the host's AI time grows with these units */
    { g_lwDensity.refusedFull++; snprintf(why, whyLen, "the full-update cap: %d units of %d", full, g_lwDensity.fullMax); return 0; }
    if (g_lwDensity.units[b] + (int)members + (int)held > g_lwDensity.budget[b])
    { snprintf(why, whyLen, "the cap: bubble %d online + %u over %d (%d players)", g_lwDensity.units[b], members, g_lwDensity.budget[b], g_lwDensity.players[b]); return 0; }
    return 1;
}

/* A squad's spawn request went out: the rest of this tick counts it. (Counted at the grant, a request that then
 * found no place kept its bubble closed to the others for the tick.) */
static void LwDensityOnline(const float* pos, uint32_t members)
{
    LwMoverSent(members);
    int b = LwDensityOn() == 1 ? LwBubbleOf(pos[0], pos[1]) : -1;
    if (b >= 0) g_lwDensity.units[b] += (int)members;
}

/* Squads the world holds: as many as fill a solo bubble's budget at the density rules' level (D: the far band and the
 * swaps need offline squads to draw from, more than the old ceiling did). */
static float LwDensityPopulateUnits(int eff)
{
    if (!LwDensityOn()) return (float)eff;
    return g_lwDensity.factor * (float)(eff < 48 ? eff : 48);
}

/* Far squads take a quarter of the budgets under the density rules (D: the extra units go to 150-250 m, where players
 * meet them), half of the effective cap otherwise. */
static float LwFarShareLimit(void)
{
    if (g_lwDensity.on != 1) return LW_FAR_SHARE * (float)g_lwWorld.eff;
    int total = 0;
    for (int b = 0; b < g_lwDensity.count; b++) total += g_lwDensity.budget[b];
    return 0.25f * (float)total;
}

static int LwDensitySameBubble(const LwSquad* a, const LwSquad* b)
{
    return g_lwDensity.on != 1 || LwBubbleOf(a->pos[0], a->pos[1]) == LwBubbleOf(b->pos[0], b->pos[1]);
}

/* For the 30 s status note. */
static void LwDensityNote(void)
{
    LwFramesNote();
    if (g_lwDensity.on != 1) return;
    char line[512]; size_t used = 0;
    for (int b = 0; b < g_lwDensity.count && used < sizeof line - 48; b++)
        used += (size_t)snprintf(line + used, sizeof line - used, "%s%d/%d (%d players)", b ? ", " : "", g_lwDensity.units[b], g_lwDensity.budget[b], g_lwDensity.players[b]);
    line[used < sizeof line ? used : sizeof line - 1] = 0;
    Note("living world: density bubbles %s; networked entities %u; upload %.0f of %.0f kbit/s; refused (30 s) %u for entities, %u for upload; %u shed",
        g_lwDensity.count ? line : "none", g_lwDensity.sync, (double)g_lwDensity.kbps, (double)g_lwDensity.budgetKbps, g_lwDensity.refusedSync, g_lwDensity.refusedUpload, g_lwDensity.shed);
    if (g_lwDensity.fullMax > 0) Note("living world: full-update cap %d: %d units now, %u refused (30 s)", g_lwDensity.fullMax, LwLodFullUnits(), g_lwDensity.refusedFull);
    g_lwDensity.refusedSync = g_lwDensity.refusedUpload = g_lwDensity.shed = g_lwDensity.refusedFull = 0;
}
