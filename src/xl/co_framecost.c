/* Wall-clock frame times on a complex mission (co_framecost.h). Only the game thread calls in. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "co.h"
#include "xl/co_framecost.h"

static const char* const kPartNames[CO_FC_PARTS] = {
    "status", "streamer", "span rescue", "net patience", "terrain diagnostics", "terrain probe", "living world",
    "daylight", "tactical map", "density"};

static struct
{
    int64_t freq, last, at; /* QPC: ticks a second, the last tick's start, the last mark */
    int lastLive, cores;
    int64_t part[CO_FC_PARTS]; /* the last tick's parts: the frame being closed */
    CoFcContextFn context;
    /* the window */
    int64_t windowAt;
    uint64_t sysBusy, sysAll, proc; /* CPU times at the window's start, 100 ns */
    float ms[CO_FC_FRAMES];
    uint32_t frames, kept, over25, over33, slowNoted;
    double sum, worst, modSum, modWorst, partSum[CO_FC_PARTS];
    int modWorstPart;
    int lines;
    double low1, low01; uint32_t lastOver25, lastOver33; /* the last window, for the tests */
#ifdef CO_TEST
    uint64_t testBusy, testAll, testProc;
#endif
} g_fc;

static double Ms(int64_t ticks) { return g_fc.freq > 0 ? (double)ticks * 1000.0 / (double)g_fc.freq : 0.0; }

/* The whole PC's busy and total CPU time over all cores, and this process's, in 100 ns units. */
static void CpuTimes(uint64_t* busy, uint64_t* all, uint64_t* proc)
{
#ifdef CO_TEST
    *busy = g_fc.testBusy; *all = g_fc.testAll; *proc = g_fc.testProc;
#else
#define U64(t) (((uint64_t)(t).dwHighDateTime << 32) | (t).dwLowDateTime)
    FILETIME idle, kernel, user, c, e, pk, pu;
    *busy = *all = *proc = 0;
    if (GetSystemTimes(&idle, &kernel, &user)) { *all = U64(kernel) + U64(user); *busy = *all - U64(idle); } /* kernel holds idle */
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu)) *proc = U64(pk) + U64(pu);
    if (g_fc.cores == 0) { SYSTEM_INFO si; GetSystemInfo(&si); g_fc.cores = (int)si.dwNumberOfProcessors; }
#undef U64
#endif
}

/* A menu or another window in front makes long frames that are not the mission's. */
static const char* Front(void)
{
#ifdef CO_TEST
    return "in front";
#else
    DWORD pid = 0; HWND w = GetForegroundWindow();
    if (w != NULL) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId() ? "in front" : "NOT in front";
#endif
}

/* One more line this session: 0 once the session's lines are used up. */
static int Take(void)
{
    if (g_fc.lines >= CO_FC_LINES_SESSION) return 0;
    if (++g_fc.lines == CO_FC_LINES_SESSION) co_log("frame times: %d lines this session; no more", CO_FC_LINES_SESSION);
    return 1;
}

static int Slower(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return (x < y) - (x > y); }

/* The average fps of the slowest `share` of the window's frames. */
static double Low(uint32_t n, double share)
{
    uint32_t k = (uint32_t)((double)n * share + 0.999);
    if (k == 0 || n == 0) return 0;
    double s = 0;
    for (uint32_t i = 0; i < k; i++) s += g_fc.ms[i];
    return s > 0 ? 1000.0 * k / s : 0;
}

static void WindowStart(int64_t now)
{
    g_fc.windowAt = now;
    CpuTimes(&g_fc.sysBusy, &g_fc.sysAll, &g_fc.proc);
    g_fc.frames = g_fc.kept = g_fc.over25 = g_fc.over33 = g_fc.slowNoted = 0;
    g_fc.sum = g_fc.worst = g_fc.modSum = g_fc.modWorst = 0; g_fc.modWorstPart = 0;
    for (int p = 0; p < CO_FC_PARTS; p++) g_fc.partSum[p] = 0;
}

static void WindowClose(int64_t now)
{
    double wallMs = Ms(now - g_fc.windowAt);
    if (g_fc.frames > 0 && g_fc.lines < CO_FC_LINES_SESSION)
    {
        qsort(g_fc.ms, g_fc.kept, sizeof g_fc.ms[0], Slower);
        g_fc.low1 = Low(g_fc.kept, 0.01); g_fc.low01 = Low(g_fc.kept, 0.001);
        g_fc.lastOver25 = g_fc.over25; g_fc.lastOver33 = g_fc.over33;
        uint64_t busy, all, proc;
        CpuTimes(&busy, &all, &proc);
        /* cores busy = CPU time / wall time; the game's share is this process's */
        double wall100ns = wallMs * 10000.0, sysCores = wall100ns > 0 ? (double)(busy - g_fc.sysBusy) / wall100ns : 0;
        double gameCores = wall100ns > 0 ? (double)(proc - g_fc.proc) / wall100ns : 0;
        double busyPct = all > g_fc.sysAll ? 100.0 * (double)(busy - g_fc.sysBusy) / (double)(all - g_fc.sysAll) : 0;
        int ai = -1; float nest = -1;
        if (g_fc.context != NULL) g_fc.context(&ai, &nest);
        char nestText[48];
        if (nest >= 0) snprintf(nestText, sizeof nestText, "%.0f m", (double)nest); else snprintf(nestText, sizeof nestText, "none known");
        if (Take()) co_log("frame times (10 s, QPC): %u frames, %.1f fps, 1%% low %.1f, 0.1%% low %.1f, worst %.1f ms; %u over %.0f ms, %u over %.1f ms; "
            "CPU: the PC %.0f%% busy (%.1f of %d cores), the game %.1f cores, everything else %.1f; the mod's tick %.2f ms a frame, worst %.2f ms (%s); "
            "AI %d; the nearest garrison with living spawners %s",
            g_fc.frames, g_fc.sum > 0 ? 1000.0 * g_fc.frames / g_fc.sum : 0.0, g_fc.low1, g_fc.low01, g_fc.worst, g_fc.over25, CO_FC_SLOW_MS,
            g_fc.over33, CO_FC_HITCH_MS, busyPct, sysCores, g_fc.cores, gameCores, sysCores - gameCores > 0 ? sysCores - gameCores : 0.0,
            g_fc.modSum / g_fc.frames, g_fc.modWorst, kPartNames[g_fc.modWorstPart], ai, nestText);
    }
    WindowStart(now);
}

/* The frame from the last tick's start to `now`, with that tick's parts. */
static void FrameClose(int64_t now)
{
    double ms = Ms(now - g_fc.last);
    if (ms <= 0 || ms >= CO_FC_LOAD_MS) return;
    double mod = 0, top = 0; int topPart = 0;
    for (int p = 0; p < CO_FC_PARTS; p++)
    {
        double t = Ms(g_fc.part[p]);
        mod += t; g_fc.partSum[p] += t;
        if (t > top) { top = t; topPart = p; }
    }
    if (g_fc.kept < CO_FC_FRAMES) g_fc.ms[g_fc.kept++] = (float)ms;
    g_fc.frames++; g_fc.sum += ms; g_fc.modSum += mod;
    if (ms > g_fc.worst) g_fc.worst = ms;
    if (mod > g_fc.modWorst) { g_fc.modWorst = mod; g_fc.modWorstPart = topPart; }
    if (ms > CO_FC_HITCH_MS) g_fc.over33++;
    if (!(ms > CO_FC_SLOW_MS)) return;
    g_fc.over25++;
    if (g_fc.slowNoted >= CO_FC_SLOW_LINES_WINDOW || !Take()) return;
    g_fc.slowNoted++;
    co_log("frame times: a %.1f ms frame (QPC; the game's window %s): the mod's tick %.2f ms (the most: %s %.2f ms), the rest %.1f ms",
        ms, Front(), mod, kPartNames[topPart], top, ms - mod);
}

static void Begin(int64_t now, int live)
{
    if (live && (!g_fc.lastLive || g_fc.windowAt == 0)) WindowStart(now); /* windows count complex-mission frames only */
    else if (live)
    {
        FrameClose(now);
        if (Ms(now - g_fc.windowAt) >= CO_FC_WINDOW_MS) WindowClose(now);
    }
    for (int p = 0; p < CO_FC_PARTS; p++) g_fc.part[p] = 0;
    g_fc.last = g_fc.at = now; g_fc.lastLive = live;
}

static void Mark(int part, int64_t now)
{
    if (part < 0 || part >= CO_FC_PARTS || g_fc.at == 0) return;
    g_fc.part[part] += now - g_fc.at;
    g_fc.at = now;
}

static int64_t Now(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    if (g_fc.freq == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_fc.freq = f.QuadPart; }
    return t.QuadPart;
}

void co_framecost_context(CoFcContextFn fn) { g_fc.context = fn; }
void co_framecost_begin(int live) { Begin(Now(), live); }
void co_framecost_mark(int part) { Mark(part, Now()); }

#ifdef CO_TEST
void co_framecost_test_reset(int64_t freq) { memset(&g_fc, 0, sizeof g_fc); g_fc.freq = freq; g_fc.cores = 16; }
void co_framecost_test_cpu(uint64_t systemBusy, uint64_t systemAll, uint64_t process, int cores)
{ g_fc.testBusy = systemBusy; g_fc.testAll = systemAll; g_fc.testProc = process; g_fc.cores = cores; }
void co_framecost_test_begin(int64_t now, int live) { Begin(now, live); }
void co_framecost_test_mark(int part, int64_t now) { Mark(part, now); }
int co_framecost_test_lines(void) { return g_fc.lines; }
void co_framecost_test_last(double* low1, double* low01, uint32_t* over25, uint32_t* over33)
{ *low1 = g_fc.low1; *low01 = g_fc.low01; *over25 = g_fc.lastOver25; *over33 = g_fc.lastOver33; }
#endif
