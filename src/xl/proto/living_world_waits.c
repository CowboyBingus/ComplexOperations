/* Test instrument (HELLFORGE_LW_WAITS=1; KB xl-living-world-frame-budget): the engine's job wait (helldivers2.exe
 * 0x627160: runs queued jobs and spins until a counter is 0) timed by thread and caller, to see which waits fill the
 * render and game threads' frames. Printed with the 30 s notes as milliseconds a frame. */
#define LW_WAIT_SLOTS 256u
typedef uintptr_t (*LwWaitFn)(uintptr_t pool, uintptr_t counter, uintptr_t c, uintptr_t d);
static struct { void* orig; uintptr_t exe; volatile LONG64 key[LW_WAIT_SLOTS], ticks[LW_WAIT_SLOTS]; volatile LONG calls[LW_WAIT_SLOTS]; uint32_t frame; } g_lwWait;

static uintptr_t LwWait(uintptr_t pool, uintptr_t counter, uintptr_t c, uintptr_t d)
{
    LARGE_INTEGER a, b;
    LONG64 key = (LONG64)GetCurrentThreadId() << 32 | (uint32_t)((uintptr_t)__builtin_return_address(0) - g_lwWait.exe);
    QueryPerformanceCounter(&a);
    uintptr_t r = ((LwWaitFn)g_lwWait.orig)(pool, counter, c, d);
    QueryPerformanceCounter(&b);
    for (uint32_t i = (uint32_t)(key * 0x9E3779B1u) % LW_WAIT_SLOTS, n = 0; n < LW_WAIT_SLOTS; n++, i = (i + 1) % LW_WAIT_SLOTS)
    {
        LONG64 at = g_lwWait.key[i];
        if (at == 0) at = InterlockedCompareExchange64(&g_lwWait.key[i], key, 0);
        if (at != 0 && at != key) continue;
        InterlockedAdd64(&g_lwWait.ticks[i], b.QuadPart - a.QuadPart);
        InterlockedIncrement(&g_lwWait.calls[i]);
        break;
    }
    return r;
}


/* Same switch: the unit's function at vtable +0x238 (helldivers2.exe 0x2C4AE0, heavy matrix arithmetic; 18% of the game
 * thread with 180 enemies online) counted and timed by caller, in processor ticks (the note converts them). */
#define LW_U71_SLOTS 64u
typedef uintptr_t (*LwU71Fn)(uintptr_t unit, uintptr_t b, uintptr_t c, uintptr_t d);
static struct { void* orig; volatile LONG64 key[LW_U71_SLOTS], ticks[LW_U71_SLOTS]; volatile LONG calls[LW_U71_SLOTS]; uint64_t tsc; LARGE_INTEGER qpc; } g_lwU71;

/* Test switch (command "box N"): the bone LOD pass (caller 0x5631CA) gets a unit's box from at most N frames ago. The
 * function writes only its 0x58-byte output (stores at 0x2C4CE5-0x2C4D39 and 0x2C697C-0x2C69A5) and the pass uses the
 * box for a sphere test, so an older box only moves a LOD choice. A slot in use by another thread is passed by. */
#define LW_BOX_SLOTS 4096u
static struct { volatile LONG busy; uintptr_t unit; uint32_t frame, flag; int same; uintptr_t ret; uint8_t box[0x58]; } g_lwBox[LW_BOX_SLOTS];
static struct { volatile LONG frames, hits, misses; } g_lwBoxes;

static int LwBoxCached(uintptr_t unit, uintptr_t out, uintptr_t flag, uintptr_t d, uintptr_t* ret)
{
    LONG frames = g_lwBoxes.frames;
    if (frames <= 0) return 0;
    uint32_t i = (uint32_t)(((uint64_t)unit + (flag & 0xFF)) * 0x9E3779B97F4A7C15ull >> 52), now = g_lwFlat.frame;   /* 4096 slots */
    if (InterlockedExchange(&g_lwBox[i].busy, 1)) return 0;
    if (g_lwBox[i].unit == unit && g_lwBox[i].flag == (uint32_t)(flag & 0xFF) && now - g_lwBox[i].frame < (uint32_t)frames)
    {
        memcpy((void*)out, g_lwBox[i].box, sizeof g_lwBox[i].box);
        *ret = g_lwBox[i].same ? out : g_lwBox[i].ret;
        InterlockedIncrement(&g_lwBoxes.hits);
    }
    else
    {
        *ret = ((LwU71Fn)g_lwU71.orig)(unit, out, flag, d);
        memcpy(g_lwBox[i].box, (const void*)out, sizeof g_lwBox[i].box);
        g_lwBox[i].unit = unit; g_lwBox[i].flag = (uint32_t)(flag & 0xFF); g_lwBox[i].frame = now; g_lwBox[i].same = *ret == out; g_lwBox[i].ret = *ret;
        InterlockedIncrement(&g_lwBoxes.misses);
    }
    InterlockedExchange(&g_lwBox[i].busy, 0);
    return 1;
}

static void LwBoxCmd(int frames) { InterlockedExchange(&g_lwBoxes.frames, frames); Note("living world: test: the bone LOD pass's unit boxes are reused for %d frames", frames); }

static uintptr_t LwU71(uintptr_t unit, uintptr_t b, uintptr_t c, uintptr_t d)
{
    LONG64 key = (LONG64)((uintptr_t)__builtin_return_address(0) - g_lwWait.exe);
    uintptr_t cached;
    if (key == 0x5631CA && LwBoxCached(unit, b, c, d, &cached)) return cached;
    uint64_t t = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwU71Fn)g_lwU71.orig)(unit, b, c, d);
    t = __builtin_ia32_rdtsc() - t;
    for (uint32_t i = (uint32_t)(key * 0x9E3779B1u) % LW_U71_SLOTS, n = 0; n < LW_U71_SLOTS; n++, i = (i + 1) % LW_U71_SLOTS)
    {
        LONG64 at = g_lwU71.key[i];
        if (at == 0) at = InterlockedCompareExchange64(&g_lwU71.key[i], key, 0);
        if (at != 0 && at != key) continue;
        InterlockedAdd64(&g_lwU71.ticks[i], (LONG64)t);
        InterlockedIncrement(&g_lwU71.calls[i]);
        break;
    }
    return r;
}

static void LwU71Note(uint32_t frames)
{
    char line[900]; size_t at = 0;
    LARGE_INTEGER q, f;
    uint64_t tsc = __builtin_ia32_rdtsc();
    QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    double perMs = g_lwU71.tsc && q.QuadPart > g_lwU71.qpc.QuadPart ? (double)(tsc - g_lwU71.tsc) / ((double)(q.QuadPart - g_lwU71.qpc.QuadPart) * 1000.0 / (double)f.QuadPart) : 0;
    g_lwU71.tsc = tsc; g_lwU71.qpc = q;
    for (uint32_t i = 0; i < LW_U71_SLOTS && at < sizeof line - 80; i++)
    {
        LONG64 ticks = InterlockedExchange64(&g_lwU71.ticks[i], 0);
        LONG calls = InterlockedExchange(&g_lwU71.calls[i], 0);
        if (calls == 0 || perMs <= 0) continue;
        at += (size_t)snprintf(line + at, sizeof line - at, " %X %.3f ms (%.1f calls, %.0f ns each)", (unsigned)g_lwU71.key[i],
            (double)ticks / perMs / frames, (double)calls / frames, (double)ticks / perMs * 1e6 / calls);
    }
    line[at] = 0;
    Note("living world: test: unit +0x238 calls a frame (boxes reused %ld frames: %.1f taken from before, %.1f computed), by caller:%s",
        g_lwBoxes.frames, (double)InterlockedExchange(&g_lwBoxes.hits, 0) / frames, (double)InterlockedExchange(&g_lwBoxes.misses, 0) / frames, line);
}


/* Same switch (command "delay N": N > 0 spins the game thread N microseconds a frame, N < 0 the render thread at its
 * frame end): the thread whose delay the frame takes in full is the one that sets the frame's length. */
static struct { volatile LONG game, render; void* endOrig; } g_lwDelay;
static void LwDelaySpin(LONG us)
{
    LARGE_INTEGER a, b, f;
    if (us <= 0) return;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    do { QueryPerformanceCounter(&b); } while ((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart < us);
}
/* "delay cycle G R S": phases of S seconds in turn (none, game G us, none, render R us), the first second of each
 * left out, so every setting is measured many times in one session and drift between windows averages out. */
static struct { int game, render, seconds, phase, cycles; LONGLONG phaseAt; double ms[4]; uint32_t frames[4]; } g_lwCycle;
static void LwDelayGame(void)
{
    if (g_lwCycle.seconds > 0)
    {
        LARGE_INTEGER n, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n);
        static LONGLONG last;
        double sinceMs = (double)(n.QuadPart - g_lwCycle.phaseAt) * 1000.0 / (double)f.QuadPart;
        if (sinceMs > 1000.0 && last > g_lwCycle.phaseAt) { g_lwCycle.ms[g_lwCycle.phase] += (double)(n.QuadPart - last) * 1000.0 / (double)f.QuadPart; g_lwCycle.frames[g_lwCycle.phase]++; }
        last = n.QuadPart;
        if (sinceMs > g_lwCycle.seconds * 1000.0)
        {
            g_lwCycle.phase = (g_lwCycle.phase + 1) & 3; g_lwCycle.phaseAt = n.QuadPart;
            InterlockedExchange(&g_lwDelay.game, g_lwCycle.phase == 1 ? g_lwCycle.game : 0);
            InterlockedExchange(&g_lwDelay.render, g_lwCycle.phase == 3 ? g_lwCycle.render : 0);
            if (g_lwCycle.phase == 0)
            {
                double m[4]; for (int i = 0; i < 4; i++) m[i] = g_lwCycle.frames[i] ? g_lwCycle.ms[i] / g_lwCycle.frames[i] : 0.0;
                Note("living world: test: delay cycle %d (%u units): none %.2f ms, game +%d us %.2f ms, none %.2f ms, render +%d us a call %.2f ms (means over all cycles so far)",
                    ++g_lwCycle.cycles, (unsigned)g_lwWorld.onlineUnits, m[0], g_lwCycle.game, m[1], m[2], g_lwCycle.render, m[3]);
            }
        }
    }
    LwDelaySpin(g_lwDelay.game);
}
static void LwDelayRender(void) { LwDelaySpin(g_lwDelay.render); }
static void LwDelayCmd(int us)
{
    g_lwCycle.seconds = 0;
    InterlockedExchange(&g_lwDelay.game, us > 0 ? us : 0); InterlockedExchange(&g_lwDelay.render, us < 0 ? -us : 0);
    Note("living world: test: delay a frame: game thread %d us, render thread %d us%s", us > 0 ? us : 0, us < 0 ? -us : 0, "");
}
static void LwDelayCycleCmd(int game, int render, int seconds)
{
    LARGE_INTEGER n; QueryPerformanceCounter(&n);
    memset(&g_lwCycle, 0, sizeof g_lwCycle);
    g_lwCycle.game = game; g_lwCycle.render = render; g_lwCycle.phaseAt = n.QuadPart; g_lwCycle.seconds = seconds;
    InterlockedExchange(&g_lwDelay.game, 0); InterlockedExchange(&g_lwDelay.render, 0);
    Note("living world: test: delay cycle: phases of %d s: none, game +%d us, none, render +%d us a call", seconds, game, render);
}

/* Sub-step test ("substep cycle S"): the physics step (exe 0x79EB10) reads its rate (T+0x38) and maximum sub-steps
 * (T+0x3C) every frame and leaves the time it simulated in T+0x8C; T = exe+0x27BA880 + world*0xB0, world 2 the game's.
 * Phases of S seconds: stock (60 Hz, 3), 60 Hz max 1, stock, 30 Hz max 1; frame time and sub-steps a frame per phase. */
static struct { int seconds, phase, cycles; LONGLONG phaseAt, last; double ms[4], steps[4]; uint32_t frames[4], over1[4]; } g_lwSub;
static uintptr_t LwSubT(void) { return (uintptr_t)GetModuleHandleA(NULL) + 0x27BA880u + 2u * 0xB0u; }
static void LwSubSet(float rate, uint32_t max) { uintptr_t t = LwSubT(); *(volatile float*)(t + 0x38) = rate; *(volatile uint32_t*)(t + 0x3C) = max; }
static void LwSubTick(void)
{
    if (g_lwSub.seconds <= 0) return;
    static const float rates[4] = { 60.0f, 60.0f, 60.0f, 30.0f }; static const uint32_t maxes[4] = { 3, 1, 3, 1 };
    LARGE_INTEGER n, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n);
    uintptr_t t = LwSubT();
    double sinceMs = (double)(n.QuadPart - g_lwSub.phaseAt) * 1000.0 / (double)f.QuadPart;
    if (sinceMs > 1000.0 && g_lwSub.last > g_lwSub.phaseAt)
    {
        double steps = (double)*(volatile float*)(t + 0x8C) * (double)*(volatile float*)(t + 0x38); /* the frame before */
        int p = g_lwSub.phase;
        g_lwSub.ms[p] += (double)(n.QuadPart - g_lwSub.last) * 1000.0 / (double)f.QuadPart; g_lwSub.steps[p] += steps; g_lwSub.frames[p]++;
        if (steps > 1.5) g_lwSub.over1[p]++;
    }
    g_lwSub.last = n.QuadPart;
    if (sinceMs <= g_lwSub.seconds * 1000.0) return;
    g_lwSub.phase = (g_lwSub.phase + 1) & 3; g_lwSub.phaseAt = n.QuadPart;
    LwSubSet(rates[g_lwSub.phase], maxes[g_lwSub.phase]);
    if (g_lwSub.phase != 0) return;
    char line[400]; size_t at = 0;
    for (int i = 0; i < 4; i++)
    {
        uint32_t fr = g_lwSub.frames[i] ? g_lwSub.frames[i] : 1;
        at += (size_t)snprintf(line + at, sizeof line - at, " [%g Hz max %u] %.2f ms, %.2f sub-steps, %.0f%% of frames over one;", (double)rates[i], (unsigned)maxes[i],
            g_lwSub.ms[i] / fr, g_lwSub.steps[i] / fr, 100.0 * g_lwSub.over1[i] / fr);
    }
    Note("living world: test: substep cycle %d (%u units):%s (means over all cycles so far)", ++g_lwSub.cycles, (unsigned)g_lwWorld.onlineUnits, line);
}
static void LwSubCycleCmd(int seconds)
{
    uintptr_t t = LwSubT();
    float rate = *(volatile float*)(t + 0x38); uint32_t max = *(volatile uint32_t*)(t + 0x3C);
    if (seconds > 1 && g_lwSub.seconds <= 0 && (rate != 60.0f || max != 3u)) { Note("living world: test: substep cycle refused: world 2 has rate %g and maximum %u, not 60 and 3", (double)rate, (unsigned)max); return; }
    for (unsigned w = 0; w < 4; w++)
    {
        uintptr_t tw = (uintptr_t)GetModuleHandleA(NULL) + 0x27BA880u + w * 0xB0u;
        Note("living world: test: physics world %u: flags %02X, rate %g, maximum %u, simulated last frame %.2f ms", w, (unsigned)*(volatile uint8_t*)(tw + 0xC),
            (double)*(volatile float*)(tw + 0x38), (unsigned)*(volatile uint32_t*)(tw + 0x3C), 1000.0 * (double)*(volatile float*)(tw + 0x8C));
    }
    LARGE_INTEGER n; QueryPerformanceCounter(&n);
    memset(&g_lwSub, 0, sizeof g_lwSub);
    if (seconds > 1) { g_lwSub.phaseAt = n.QuadPart; g_lwSub.seconds = seconds; }
    LwSubSet(60.0f, 3u);
    Note("living world: test: substep cycle: %s (phases of %d s: stock, 60 Hz max 1, stock, 30 Hz max 1)", seconds > 1 ? "on" : "off, stock values restored", seconds);
}

static void LwWaitsNote(void)
{
    static int on = -1;
    char line[1500]; size_t at = 0;
    LARGE_INTEGER f;
    if (on < 0)
    {
        char t[4] = "";
        on = GetEnvironmentVariableA("HELLFORGE_LW_WAITS", t, sizeof t) > 0 && t[0] == '1';
        g_lwWait.exe = (uintptr_t)GetModuleHandleA(NULL);
        if (on && (!ApiCodeMatches(g_lwWait.exe + 0x627160u, "48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54") ||
            LoaderHook("living world: job wait timing", g_lwWait.exe + 0x627160u, (void*)LwWait, &g_lwWait.orig) || g_lwWait.orig == NULL)) on = 0;
        if (on && (!ApiCodeMatches(g_lwWait.exe + 0x2C4AE0u, "48 8B C4 48 89 58 10 48 89 70 18 55 57 41 56 48 8D A8 98 FD FF FF") ||
            LoaderHook("living world: unit +0x238 timing", g_lwWait.exe + 0x2C4AE0u, (void*)LwU71, &g_lwU71.orig) || g_lwU71.orig == NULL))
            Note("living world: test: unit +0x238 is not timed");
        Note("living world: test: job waits are %s", on ? "timed" : "not timed");
        g_lwWait.frame = g_lwFlat.frame;
        return;
    }
    if (!on) return;
    QueryPerformanceFrequency(&f);
    uint32_t frames = g_lwFlat.frame - g_lwWait.frame; g_lwWait.frame = g_lwFlat.frame;
    if (frames == 0) frames = 1;
    for (int shown = 0; shown < 16; shown++)
    {
        int best = -1;
        for (uint32_t i = 0; i < LW_WAIT_SLOTS; i++) if (g_lwWait.ticks[i] > 0 && (best < 0 || g_lwWait.ticks[i] > g_lwWait.ticks[best])) best = (int)i;
        if (best < 0) break;
        LONG64 ticks = InterlockedExchange64(&g_lwWait.ticks[best], 0);
        LONG calls = InterlockedExchange(&g_lwWait.calls[best], 0);
        at += (size_t)snprintf(line + at, sizeof line - at, " %lu:%X %.2f ms (%.1f calls)", (unsigned long)(g_lwWait.key[best] >> 32),
            (unsigned)(uint32_t)g_lwWait.key[best], 1000.0 * (double)ticks / (double)f.QuadPart / frames, (double)calls / frames);
    }
    for (uint32_t i = 0; i < LW_WAIT_SLOTS; i++) { g_lwWait.ticks[i] = 0; g_lwWait.calls[i] = 0; }
    if (g_lwU71.orig) LwU71Note(frames);
    Note("living world: test: job waits a frame over %u frames, game thread %lu, thread:caller:%s", frames, (unsigned long)g_lwFlat.gameThread, line);
}

/* AI async time test ("aitime US"): the Havok AI step (exe 0xBD1890) lets its 4 async threads run for H+0x47C seconds
 * (stock 0.002) while the step's thread sleeps and the game thread's join (0x7D7930) spins; read once a step at
 * 0xBD198C. H = [exe+0x190D4A0 + 2*0x80] (KB world-update-exe-ai-step). */
static void LwAiTimeCmd(int us)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), h = 0; float was = 0, t[2] = {0, 0};
    if (!ApiCodeMatches(exe + 0xBD198Cu, "F3 0F 10 8B 7C 04 00 00 48 8B CB E8") || SafeRead(exe + 0x190D4A0u + 2u * 0x80u, &h, 8) || h == 0 ||
        SafeRead(h + 0x47C, &was, 4) || was < 0.00001f || was > 0.1f)
    { Note("living world: test: aitime: the AI world's time budget is not where this build has it (H %p, value %g)", (void*)h, (double)was); return; }
    SafeRead(h + 0x480, t, 8);
    if (us >= 50 && us <= 20000) *(volatile float*)(h + 0x47C) = (float)us / 1000000.0f;
    Note("living world: test: aitime: async AI time %.0f us -> %.0f us (H+0x480 %g, +0x484 %g)", (double)was * 1e6, (double)*(volatile float*)(h + 0x47C) * 1e6, (double)t[0], (double)t[1]);
}
