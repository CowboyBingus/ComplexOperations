/* Test instrument (HELLFORGE_LW_NAVTIME=1): where the main thread's nav cell tick 0x7B8A10 spends its time while the
 * XL grid builds (KB normal-play-tail-norm1). The tick runs the scheduler 0x7B8200 (job starts 0x7B7590 at 0x7B8501),
 * then drains completions: connect 0x7B7FD0 (0x7B915B) and the queued section load 0xBB19B0 (0x7B9244). Every tick
 * over 2 ms is noted with its QPC time (the frame recorder's clock) and the parts, at most 60 per 30 s. Read only. */
typedef uintptr_t (*LwNtFn)(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f);
enum { LW_NT_SCHED, LW_NT_START, LW_NT_CONNECT, LW_NT_LOAD, LW_NT_PARTS };
static struct
{
    void *tick, *sched, *start, *connect, *load;
    DWORD thread; int inTick;
    LONGLONG t[LW_NT_PARTS]; int n[LW_NT_PARTS];
    double qpcMs; ULONGLONG notedAt; int noted, ticks, slow; double worst;
} g_lwNt;

static LONGLONG LwNtNow(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

/* One timed part of the tick, counted only on the tick's own thread while it runs. */
static uintptr_t LwNtPart(int part, void* orig, const uintptr_t* x)
{
    if (!g_lwNt.inTick || GetCurrentThreadId() != g_lwNt.thread) return ((LwNtFn)orig)(x[0], x[1], x[2], x[3], x[4], x[5]);
    LONGLONG t0 = LwNtNow();
    uintptr_t r = ((LwNtFn)orig)(x[0], x[1], x[2], x[3], x[4], x[5]);
    g_lwNt.t[part] += LwNtNow() - t0; g_lwNt.n[part]++;
    return r;
}
static uintptr_t LwNtSched(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwNtPart(LW_NT_SCHED, g_lwNt.sched, x); }
static uintptr_t LwNtStart(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwNtPart(LW_NT_START, g_lwNt.start, x); }
static uintptr_t LwNtConnect(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwNtPart(LW_NT_CONNECT, g_lwNt.connect, x); }
static uintptr_t LwNtLoad(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwNtPart(LW_NT_LOAD, g_lwNt.load, x); }

static double LwNtMs(LONGLONG ticks) { return (double)ticks * g_lwNt.qpcMs; }

/* Every 30 s: the ticks, the slow ones and the worst. */
static void LwNtWindow(void)
{
    ULONGLONG now = GetTickCount64();
    if (now - g_lwNt.notedAt < 30000) return;
    Note("living world: test: nav ticks in 30 s: %d, %d over 2 ms, worst %.2f ms", g_lwNt.ticks, g_lwNt.slow, g_lwNt.worst);
    g_lwNt.notedAt = now; g_lwNt.noted = g_lwNt.ticks = g_lwNt.slow = 0; g_lwNt.worst = 0;
}

static uintptr_t LwNtTick(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
    if (g_lwNt.inTick) return ((LwNtFn)g_lwNt.tick)(a, b, c, d, e, f);   /* not re-entered in the game; kept plain if it is */
    memset(g_lwNt.t, 0, sizeof g_lwNt.t); memset(g_lwNt.n, 0, sizeof g_lwNt.n);
    g_lwNt.thread = GetCurrentThreadId(); g_lwNt.inTick = 1;
    LONGLONG t0 = LwNtNow();
    uintptr_t r = ((LwNtFn)g_lwNt.tick)(a, b, c, d, e, f);
    LONGLONG t1 = LwNtNow();
    g_lwNt.inTick = 0;
    double ms = LwNtMs(t1 - t0);
    g_lwNt.ticks++;
    if (ms > g_lwNt.worst) g_lwNt.worst = ms;
    if (ms > 2.0 && ++g_lwNt.slow && ++g_lwNt.noted <= 60)
        Note("living world: test: nav tick at %.4f s: %.2f ms; scheduler %.2f (%d job starts, %.2f); connects %d, %.2f ms; loads %d, %.2f ms",
            (double)t1 * g_lwNt.qpcMs / 1000.0, ms, LwNtMs(g_lwNt.t[LW_NT_SCHED]), g_lwNt.n[LW_NT_START], LwNtMs(g_lwNt.t[LW_NT_START]),
            g_lwNt.n[LW_NT_CONNECT], LwNtMs(g_lwNt.t[LW_NT_CONNECT]), g_lwNt.n[LW_NT_LOAD], LwNtMs(g_lwNt.t[LW_NT_LOAD]));
    LwNtWindow();
    return r;
}

/* After LwInstallNavSwapSync: its pins read 0xBB19B0's first bytes, which this hook replaces. */
static void LwInstallNavTime(void)
{
    static int done; char v[4] = ""; LARGE_INTEGER hz;
    if (done++ || GetEnvironmentVariableA("HELLFORGE_LW_NAVTIME", v, sizeof v) == 0 || v[0] != '1') return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    QueryPerformanceFrequency(&hz); g_lwNt.qpcMs = 1000.0 / (double)hz.QuadPart; g_lwNt.notedAt = GetTickCount64();
    if (!ApiCodeMatches(exe + 0x7B8A10u, "48 8B C4 89 48 08 55 53 56 57 41 54") || !ApiCodeMatches(exe + 0x7B8200u, "4C 8B DC 55 56 41 55 41 56 48 81 EC 98 00 00 00") ||
        !ApiCodeMatches(exe + 0x7B7590u, "4C 89 4C 24 20 4C 89 44 24 18 53 55 56 57 41 54") || !ApiCodeMatches(exe + 0x7B7FD0u, "89 54 24 10 55 56 41 54 41 55 41 56 41 57") ||
        !ApiCodeMatches(exe + 0xBB19B0u, "4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54") ||
        LoaderHook("nav time (test): start", exe + 0x7B7590u, (void*)LwNtStart, &g_lwNt.start) || !g_lwNt.start ||
        LoaderHook("nav time (test): connect", exe + 0x7B7FD0u, (void*)LwNtConnect, &g_lwNt.connect) || !g_lwNt.connect ||
        LoaderHook("nav time (test): load", exe + 0xBB19B0u, (void*)LwNtLoad, &g_lwNt.load) || !g_lwNt.load ||
        LoaderHook("nav time (test): scheduler", exe + 0x7B8200u, (void*)LwNtSched, &g_lwNt.sched) || !g_lwNt.sched ||
        LoaderHook("nav time (test): tick", exe + 0x7B8A10u, (void*)LwNtTick, &g_lwNt.tick) || !g_lwNt.tick)
    { Note("living world: test: nav time: this build's code is not what it expects; off"); return; }
    Note("living world: test: nav tick parts timed (ticks over 2 ms noted)");
}
