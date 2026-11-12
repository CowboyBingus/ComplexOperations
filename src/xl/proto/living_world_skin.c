/* Skin instrument I for the palette levers A (living_world_skincopy.c) and B (living_world_skinreuse.c); design and
 * review: ~/xl-work/static/living/levers-static3/render/final.md. The kick 0x2592A0 (only caller 0x10A41E) and the
 * palette-job wait 0x25A1D0 run on the render thread, so they give A the render thread's id and the render worlds without
 * the render-help lever, and B a frame count and a point where no upload job runs: S+0x184 is 1 from before the jobs
 * are kicked until all were waited (0x259DDF, 0x25A263). The kick hook stays short: the 0x0A handler holds the
 * renderer's lock r+0x14A0 across the kicks, and the game thread can wait on that lock.
 * HELLFORGE_LW_SKINSTAT=1 / command "skinstat 0|1"; the hooks also go in with A or B. */
#define LW_SKIN_WORLDS 8
#define LW_SKIN_STALE 64u /* kicks of other worlds after which a world is taken as gone */
#define LW_SKIN_WAITS 7
typedef void (*LwSkinKickFn)(uintptr_t S, uintptr_t dev, int arg);
typedef void (*LwSkinWaitFn)(uintptr_t S);
typedef struct
{
    uintptr_t S; uint32_t seen, lastBytes; volatile uint32_t frame; /* B's frame at this world's last kick */
    volatile LONG64 kicks, mats, listA, listB;
} LwSkinWorld;
static struct
{
    int init, hooked, stat; uintptr_t exe; void *kickOrig, *waitOrig;
    volatile DWORD renderTid; uint32_t kickSeq; volatile LONG nWorlds; LwSkinWorld w[LW_SKIN_WORLDS]; uintptr_t mainS;
    volatile LONG64 frame; /* kicks of the main world */
    volatile LONG64 waits, waitTicks, waitMax, waitHist[LW_SKIN_WAITS];
    volatile double tscPerMs; uint64_t calTsc; LONGLONG calQpc; ULONGLONG notedAt; LONG64 frameAt;
} g_lwSkin;
static const float g_lwSkinWaitEdges[LW_SKIN_WAITS - 1] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

static void LwSkrServeClear(void); static void LwSkcFrame(ULONGLONG now); static void LwSkrFrame(ULONGLONG now);
static void LwSkcNote(double secs); static void LwSkrNote(double secs, LONG64 frames);
static void LwSkcCmd(const char* args); static void LwSkrCmd(const char* args); static void LwSkinEnvLevers(void);
static int LwSkcActive(void); static int LwSkrActive(void);

static int LwSkinLive(const LwSkinWorld* w) { return w->S != 0 && g_lwSkin.kickSeq - w->seen < LW_SKIN_STALE; }

/* One more in the bucket of a tick count against edges in ms (n edges, n + 1 buckets); none before the clock is
 * calibrated, the first second after the hooks. */
static void LwSkinBucket(volatile LONG64* hist, uint64_t ticks, const float* edges, int n)
{
    double tpm = g_lwSkin.tscPerMs, ms = tpm > 0 ? (double)ticks / tpm : 0;
    int b = 0;
    while (b < n && ms > (double)edges[b]) b++;
    if (tpm > 0) InterlockedIncrement64(&hist[b]);
}

/* A running maximum that the game thread may reset at any time. */
static void LwSkinMax(volatile LONG64* m, LONG64 v)
{
    for (LONG64 was = *m; v > was; was = *m)
        if (InterlockedCompareExchange64(m, v, was) == was) return;
}

/* Render thread: the world's entry, a new one, or the oldest gone one reused; NULL when all 8 are live. */
static LwSkinWorld* LwSkinWorldOf(uintptr_t S)
{
    LwSkinWorld* gone = NULL;
    for (LONG i = 0; i < g_lwSkin.nWorlds; i++)
    {
        if (g_lwSkin.w[i].S == S) return &g_lwSkin.w[i];
        if (!gone && !LwSkinLive(&g_lwSkin.w[i])) gone = &g_lwSkin.w[i];
    }
    if (g_lwSkin.nWorlds < LW_SKIN_WORLDS) gone = &g_lwSkin.w[g_lwSkin.nWorlds];
    if (!gone) return NULL;
    gone->S = S; gone->lastBytes = 0;
    InterlockedExchange64(&gone->kicks, 0); InterlockedExchange64(&gone->mats, 0);
    if (gone == &g_lwSkin.w[g_lwSkin.nWorlds]) InterlockedIncrement(&g_lwSkin.nWorlds); /* after S: other threads read up to n */
    return gone;
}

/* The main world is the one with the largest palette; a factor 2 keeps two similar worlds from taking turns. */
static void LwSkinPickMain(uintptr_t S, const LwSkinWorld* w)
{
    const LwSkinWorld* m = NULL;
    for (LONG i = 0; i < g_lwSkin.nWorlds && g_lwSkin.mainS; i++) if (g_lwSkin.w[i].S == g_lwSkin.mainS) m = &g_lwSkin.w[i];
    if (!m || !LwSkinLive(m) || (w && w != m && w->lastBytes > 2u * m->lastBytes)) g_lwSkin.mainS = S;
}

/* B's frame for a palette job of world S (worker threads): the value its kick saw. */
static uint32_t LwSkinFrameOf(uintptr_t S)
{
    for (LONG i = 0; i < g_lwSkin.nWorlds; i++) if (g_lwSkin.w[i].S == S) return g_lwSkin.w[i].frame;
    return (uint32_t)g_lwSkin.frame;
}

static void LwSkinKick(uintptr_t S, uintptr_t dev, int arg)
{
    if (!g_lwSkin.renderTid) g_lwSkin.renderTid = GetCurrentThreadId();
    g_lwSkin.kickSeq++;
    LwSkinWorld* w = LwSkinWorldOf(S);
    if (w) w->seen = g_lwSkin.kickSeq;
    LwSkinPickMain(S, w);
    if (S == g_lwSkin.mainS) InterlockedIncrement64(&g_lwSkin.frame); /* before the original pushes the jobs that read it */
    if (w) w->frame = (uint32_t)g_lwSkin.frame;
    LwSkrServeClear();
    uint32_t before = *(volatile uint32_t*)(S + 0x184);
    ((LwSkinKickFn)g_lwSkin.kickOrig)(S, dev, arg);
    if (!w || before == 1 || *(volatile uint32_t*)(S + 0x184) != 1) return; /* no kick this call (0x2592D1, 0x2592F0) */
    uint32_t bytes = *(volatile uint32_t*)(S + 0x158);
    w->lastBytes = bytes;
    InterlockedIncrement64(&w->kicks); InterlockedAdd64(&w->mats, bytes / 48u);
    InterlockedAdd64(&w->listA, *(volatile uint32_t*)(S + 0x88)); InterlockedAdd64(&w->listB, *(volatile uint32_t*)(S + 0xB8));
}

/* Timed only when the jobs are still out: then the original waits for every upload job (0x627160 at 0x25A24D). */
static void LwSkinWait(uintptr_t S)
{
    if (*(volatile uint32_t*)(S + 0x184) != 1) { ((LwSkinWaitFn)g_lwSkin.waitOrig)(S); return; }
    uint64_t t0 = __builtin_ia32_rdtsc();
    ((LwSkinWaitFn)g_lwSkin.waitOrig)(S);
    uint64_t t = __builtin_ia32_rdtsc() - t0;
    InterlockedIncrement64(&g_lwSkin.waits); InterlockedAdd64(&g_lwSkin.waitTicks, (LONG64)t); LwSkinMax(&g_lwSkin.waitMax, (LONG64)t);
    LwSkinBucket(g_lwSkin.waitHist, t, g_lwSkinWaitEdges, LW_SKIN_WAITS - 1);
}

/* 1 when both hooks are in. Pins: the hooked prologues, the only kick call, and the S+0x184 stores the levers rely on. */
static int LwSkinHook(void)
{
    if (g_lwSkin.hooked) return g_lwSkin.hooked > 0;
    g_lwSkin.hooked = -1;
    uintptr_t exe = g_lwSkin.exe;
    if (strcmp(g_build, "25480438") ||
        !ApiCodeMatches(exe + 0x2592A0u, "44 89 44 24 18 48 89 54 24 10 55 57 41 57 48 8D AC 24 70 EE") ||
        !ApiCodeMatches(exe + 0x25A1D0u, "40 53 48 83 EC 20 48 83 B9 48 01 00 00 00 48 8B D9 0F 84 91") ||
        !ApiCodeMatches(exe + 0x10A417u, "48 81 C1 18 03 00 00 E8 7D EE 14 00") ||
        !ApiCodeMatches(exe + 0x259DD9u, "89 87 58 01 00 00 C7 87 84 01 00 00 01 00 00 00") ||
        !ApiCodeMatches(exe + 0x25A263u, "C7 83 84 01 00 00 02 00 00 00") || !ApiCodeMatches(exe + 0x25A326u, "89 AF 84 01 00 00"))
    { Note("living world: skin: the palette kick or wait is not the reviewed code; no skin levers"); return 0; }
    if (LoaderHook("living world skin kick", exe + 0x2592A0u, (void*)LwSkinKick, &g_lwSkin.kickOrig) || !g_lwSkin.kickOrig ||
        LoaderHook("living world skin wait", exe + 0x25A1D0u, (void*)LwSkinWait, &g_lwSkin.waitOrig) || !g_lwSkin.waitOrig)
    { Note("living world: skin: a hook failed; no skin levers"); return 0; }
    g_lwSkin.hooked = 1;
    Note("living world: skin: palette kick (0x2592A0) and wait (0x25A1D0) hooked");
    return 1;
}

static void LwSkinStatCmd(int on)
{
    if (on && !LwSkinHook()) return;
    g_lwSkin.stat = on != 0;
    Note("living world: skin instrument %s", g_lwSkin.stat ? "on (30 s notes)" : "off");
}

/* rdtsc ticks a millisecond against QueryPerformanceCounter, every second or more. */
static void LwSkinCalibrate(void)
{
    LARGE_INTEGER q, hz; uint64_t t = __builtin_ia32_rdtsc();
    QueryPerformanceCounter(&q); QueryPerformanceFrequency(&hz);
    double ms = g_lwSkin.calQpc ? (double)(q.QuadPart - g_lwSkin.calQpc) * 1000.0 / (double)hz.QuadPart : 0;
    if (g_lwSkin.calQpc && ms < 1000.0) return;
    if (ms > 0) g_lwSkin.tscPerMs = (double)(t - g_lwSkin.calTsc) / ms;
    g_lwSkin.calTsc = t; g_lwSkin.calQpc = q.QuadPart;
}

/* "a:b:c" counts of a histogram, and the upper edge of the bucket that holds the 99.9th percentile. */
static double LwSkinHist(volatile LONG64* hist, const float* edges, int n, char* out, size_t size)
{
    LONG64 c[16], all = 0, run = 0; int w = 0; double p999 = 0;
    for (int b = 0; b <= n && b < 16; b++) { c[b] = InterlockedExchange64(&hist[b], 0); all += c[b]; }
    out[0] = 0;
    for (int b = 0; b <= n && b < 16; b++)
    {
        w += snprintf(out + w, size - (size_t)w, "%s%lld", b ? ":" : "", (long long)c[b]);
        run += c[b];
        if (p999 == 0 && all && run * 1000 >= all * 999) p999 = b < n ? (double)edges[b] : 1e9;
        if ((size_t)w >= size) break;
    }
    return p999;
}

static void LwSkinWorldsNote(double secs, LONG64 frames)
{
    char line[600]; int w = 0;
    for (LONG i = 0; i < g_lwSkin.nWorlds && w < 520; i++)
    {
        LwSkinWorld* x = &g_lwSkin.w[i];
        LONG64 k = InterlockedExchange64(&x->kicks, 0), m = InterlockedExchange64(&x->mats, 0);
        LONG64 a = InterlockedExchange64(&x->listA, 0), b = InterlockedExchange64(&x->listB, 0);
        if (k) w += snprintf(line + w, sizeof line - (size_t)w, "; world %ld%s: %lld kicks, %.0f matrices a kick (list A %.0f, list B %.0f)",
                             (long)i, x->S == g_lwSkin.mainS ? " (main)" : "", (long long)k, (double)m / (double)k, (double)a / (double)k, (double)b / (double)k);
    }
    LONG64 n = InterlockedExchange64(&g_lwSkin.waits, 0), t = InterlockedExchange64(&g_lwSkin.waitTicks, 0), mx = InterlockedExchange64(&g_lwSkin.waitMax, 0);
    char hist[160]; double tpm = g_lwSkin.tscPerMs > 0 ? g_lwSkin.tscPerMs : 1;
    double p999 = LwSkinHist(g_lwSkin.waitHist, g_lwSkinWaitEdges, LW_SKIN_WAITS - 1, hist, sizeof hist);
    Note("living world: skin (%.0f s): %lld main-world frames; palette-job wait %lld times, %.3f ms each (max %.3f, p99.9 under %.2f ms; "
         "<=0.1:0.25:0.5:1:2:4:more %s); render tid %lu, %ld worlds%s",
         secs, (long long)frames, (long long)n, n ? (double)t / tpm / (double)n : 0.0, (double)mx / tpm, p999, hist, (unsigned long)g_lwSkin.renderTid, (long)g_lwSkin.nWorlds, line);
}

static void LwSkinInit(void)
{
    char t[8] = "";
    g_lwSkin.init = 1; g_lwSkin.exe = (uintptr_t)GetModuleHandleA(NULL); g_lwSkin.notedAt = GetTickCount64();
    if (GetEnvironmentVariableA("HELLFORGE_LW_SKINSTAT", t, sizeof t) > 0 && t[0] == '1') LwSkinStatCmd(1);
    LwSkinEnvLevers();
}

/* Game thread, once a tick: the first call reads the switches; then the A/B flips, B's set and the 30 s notes. */
static void LwSkinFrame(void)
{
    ULONGLONG now = GetTickCount64();
    if (!g_lwSkin.init) LwSkinInit();
    if (g_lwSkin.hooked <= 0) return;
    LwSkinCalibrate();
    LwSkcFrame(now); LwSkrFrame(now);
    if (now - g_lwSkin.notedAt < 30000) return;
    double secs = (double)(now - g_lwSkin.notedAt) / 1000.0;
    LONG64 f = g_lwSkin.frame, frames = f - g_lwSkin.frameAt;
    g_lwSkin.notedAt = now; g_lwSkin.frameAt = f;
    if (!g_lwSkin.stat && !LwSkcActive() && !LwSkrActive()) return;
    LwSkinWorldsNote(secs, frames);
    LwSkcNote(secs);
    LwSkrNote(secs, frames);
}

/* Test commands "skinstat N", "skincopy ...", "skinreuse ..." (game thread). */
static void LwSkinCmd(const char* c)
{
    int a = 0;
    if (!g_lwSkin.init) LwSkinInit();
    if (sscanf(c, "skinstat %d", &a) == 1) LwSkinStatCmd(a);
    else if (strncmp(c, "skincopy ", 9) == 0) LwSkcCmd(c + 9);
    else if (strncmp(c, "skinreuse ", 10) == 0) LwSkrCmd(c + 10);
    else Note("living world: test: %s: not understood", c);
}
