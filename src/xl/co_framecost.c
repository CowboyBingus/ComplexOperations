/* Wall-clock frame times on a complex mission (co_framecost.h). CoXlTick's thread owns the state; a part timed on
 * another thread only adds to one interlocked counter. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "co.h"
#include "xl/co_framecost.h"

static const char* const kPartNames[CO_FC_PARTS] = {
    "status", "streamer", "span rescue", "net patience", "terrain diagnostics", "terrain probe", "living world",
    "daylight", "tactical map", "density", "scatter merge", "moving tiles", "ground keep", "far grass"};

static struct
{
    int64_t freq, last, at; /* QPC: ticks a second, the last tick's start, the last mark */
    DWORD thread;           /* CoXlTick's */
    int lastPlay, cores;
    long mission, budgetMission;
    int64_t part[CO_FC_PARTS]; /* the frame being closed: its parts so far */
    volatile LONG64 offThread; /* ticks timed on other threads since the window began */
    CoFcContextFn context;
    /* the window */
    int64_t windowAt;
    uint64_t sysBusy, sysAll, proc; /* CPU times at the window's start, 100 ns */
    float ms[CO_FC_FRAMES];
    uint32_t frames, kept, over25, over33, over1s, slowNoted;
    double sum, worst, worstTimed, worstTop, timedSum;
    int worstTopPart;
    int windowLines, detailLines;
    /* the last window, for the tests */
    double low1, low01; uint32_t lastOver25, lastOver33, lastOver1s; int lastStarved;
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
    InterlockedExchange64(&g_fc.offThread, 0);
    g_fc.frames = g_fc.kept = g_fc.over25 = g_fc.over33 = g_fc.over1s = g_fc.slowNoted = 0;
    g_fc.sum = g_fc.worst = g_fc.worstTimed = g_fc.worstTop = g_fc.timedSum = 0; g_fc.worstTopPart = 0;
}

static void WindowClose(int64_t now)
{
    double wallMs = Ms(now - g_fc.windowAt), offMs = Ms(InterlockedExchange64(&g_fc.offThread, 0));
    if (g_fc.frames == 0) return;
    if (g_fc.budgetMission != g_fc.mission) { g_fc.budgetMission = g_fc.mission; g_fc.windowLines = 0; }
    if (g_fc.windowLines > CO_FC_WINDOW_LINES_MISSION) return;
    if (++g_fc.windowLines > CO_FC_WINDOW_LINES_MISSION)
    { co_log("frame times: %d window lines this mission; no more until the next", CO_FC_WINDOW_LINES_MISSION); return; }
    qsort(g_fc.ms, g_fc.kept, sizeof g_fc.ms[0], Slower);
    g_fc.low1 = Low(g_fc.kept, 0.01); g_fc.low01 = Low(g_fc.kept, 0.001);
    g_fc.lastOver25 = g_fc.over25; g_fc.lastOver33 = g_fc.over33; g_fc.lastOver1s = g_fc.over1s;
    uint64_t busy, all, proc;
    CpuTimes(&busy, &all, &proc);
    /* cores busy = CPU time / wall time; the game's share is this process's */
    double wall100ns = wallMs * 10000.0, sysCores = wall100ns > 0 ? (double)(busy - g_fc.sysBusy) / wall100ns : 0;
    double gameCores = wall100ns > 0 ? (double)(proc - g_fc.proc) / wall100ns : 0;
    double elseCores = sysCores - gameCores > 0 ? sysCores - gameCores : 0.0, freeCores = g_fc.cores - gameCores;
    double busyPct = all > g_fc.sysAll ? 100.0 * (double)(busy - g_fc.sysBusy) / (double)(all - g_fc.sysAll) : 0;
    /* everything else took the cores the game left free (less one): a game thread that wakes waits for a core */
    g_fc.lastStarved = elseCores > freeCores - CO_FC_STARVED_IDLE;
    int ai = -1; float nest = -1;
    if (g_fc.context != NULL) g_fc.context(&ai, &nest);
    char nestText[48], starved[96] = "";
    if (nest >= 0) snprintf(nestText, sizeof nestText, "%.0f m", (double)nest); else snprintf(nestText, sizeof nestText, "none known");
    if (g_fc.lastStarved) snprintf(starved, sizeof starved, "; STARVED: everything else took %.1f of the %.1f cores the game left free", elseCores, freeCores);
    /* two lines: the loader's log takes 512 characters a line */
    co_log("frame times (%.1f s of play, QPC): %u frames, %.1f fps, 1%% low %.1f, 0.1%% low %.1f; worst %.1f ms (timed parts %.2f ms, the most: %s %.2f ms); "
        "%u over %.0f ms, %u over %.1f ms, %u of 1 s or more; timed parts (CoXlTick, the level update) %.2f ms a frame, off the game thread %.1f ms",
        wallMs / 1000.0, g_fc.frames, g_fc.sum > 0 ? 1000.0 * g_fc.frames / g_fc.sum : 0.0, g_fc.low1, g_fc.low01, g_fc.worst, g_fc.worstTimed,
        kPartNames[g_fc.worstTopPart], g_fc.worstTop, g_fc.over25, CO_FC_SLOW_MS, g_fc.over33, CO_FC_HITCH_MS, g_fc.over1s, g_fc.timedSum / g_fc.frames, offMs);
    co_log("frame times, CPU: the PC %.0f%% busy (%.1f of %d cores), the game %.1f cores, everything else %.1f%s; AI %d; the nearest garrison with living spawners %s",
        busyPct, sysCores, g_fc.cores, gameCores, elseCores, starved, ai, nestText);
}

/* A detailed line for a slow frame, while the session's budget lasts. */
static void SlowLine(double ms, double timed, int topPart, double top)
{
    if (g_fc.slowNoted >= CO_FC_SLOW_LINES_WINDOW || g_fc.detailLines >= CO_FC_DETAIL_LINES_SESSION) return;
    g_fc.slowNoted++;
    co_log("frame times: a %.1f ms frame (QPC; the game's window %s): timed parts (CoXlTick, the level update) %.2f ms (the most: %s %.2f ms), untimed %.1f ms",
        ms, Front(), timed, kPartNames[topPart], top, ms - timed);
    if (++g_fc.detailLines == CO_FC_DETAIL_LINES_SESSION) co_log("frame times: %d slow-frame lines this session; the window lines go on", CO_FC_DETAIL_LINES_SESSION);
}

/* The frame from the last tick's start to `now`, with its parts. */
static void FrameClose(int64_t now)
{
    double ms = Ms(now - g_fc.last);
    if (ms <= 0) return;
    double timed = 0, top = 0; int topPart = 0;
    for (int p = 0; p < CO_FC_PARTS; p++)
    {
        double t = Ms(g_fc.part[p]);
        timed += t;
        if (t > top) { top = t; topPart = p; }
    }
    if (g_fc.kept < CO_FC_FRAMES) g_fc.ms[g_fc.kept++] = (float)ms;
    g_fc.frames++; g_fc.sum += ms; g_fc.timedSum += timed;
    if (ms > g_fc.worst) { g_fc.worst = ms; g_fc.worstTimed = timed; g_fc.worstTop = top; g_fc.worstTopPart = topPart; }
    if (ms >= CO_FC_LONG_MS) g_fc.over1s++;
    if (ms > CO_FC_HITCH_MS) g_fc.over33++;
    if (!(ms > CO_FC_SLOW_MS)) return;
    g_fc.over25++;
    SlowLine(ms, timed, topPart, top);
}

static void Begin(int64_t now, int play, long mission)
{
    g_fc.thread = GetCurrentThreadId();
    if (play && (!g_fc.lastPlay || g_fc.windowAt == 0 || mission != g_fc.mission)) WindowStart(now); /* windows count play only */
    else if (play)
    {
        FrameClose(now);
        if (Ms(now - g_fc.windowAt) >= CO_FC_WINDOW_MS) { WindowClose(now); WindowStart(now); }
    }
    for (int p = 0; p < CO_FC_PARTS; p++) g_fc.part[p] = 0;
    g_fc.last = g_fc.at = now; g_fc.lastPlay = play; g_fc.mission = mission;
}

static void Mark(int part, int64_t now)
{
    if (part < 0 || part >= CO_FC_PARTS || g_fc.at == 0) return;
    g_fc.part[part] += now - g_fc.at;
    g_fc.at = now;
}

static int64_t Add(int part, int64_t since, int64_t now)
{
    int64_t ticks = now - since;
    if (part < 0 || part >= CO_FC_PARTS || since == 0) return 0;
    if (GetCurrentThreadId() == g_fc.thread) g_fc.part[part] += ticks;
    else InterlockedExchangeAdd64(&g_fc.offThread, ticks);
    return ticks;
}

static void Nested(int inner, int outer, int64_t since, int64_t now)
{
    int64_t ticks = Add(inner, since, now);
    if (outer >= 0 && outer < CO_FC_PARTS && GetCurrentThreadId() == g_fc.thread) g_fc.part[outer] -= ticks;
}

int64_t co_framecost_now(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    if (g_fc.freq == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_fc.freq = f.QuadPart; }
    return t.QuadPart;
}

void co_framecost_context(CoFcContextFn fn) { g_fc.context = fn; }
void co_framecost_begin(int play, long mission) { Begin(co_framecost_now(), play, mission); }
void co_framecost_mark(int part) { Mark(part, co_framecost_now()); }
int64_t co_framecost_add(int part, int64_t since) { return Add(part, since, co_framecost_now()); }
void co_framecost_nested(int inner, int outer, int64_t since) { Nested(inner, outer, since, co_framecost_now()); }

#ifdef CO_TEST
void co_framecost_test_reset(int64_t freq) { memset((void*)&g_fc, 0, sizeof g_fc); g_fc.freq = freq; g_fc.cores = 16; }
void co_framecost_test_cpu(uint64_t systemBusy, uint64_t systemAll, uint64_t process, int cores)
{ g_fc.testBusy = systemBusy; g_fc.testAll = systemAll; g_fc.testProc = process; g_fc.cores = cores; }
void co_framecost_test_begin(int64_t now, int play, long mission) { Begin(now, play, mission); }
void co_framecost_test_mark(int part, int64_t now) { Mark(part, now); }
int64_t co_framecost_test_add(int part, int64_t since, int64_t now) { return Add(part, since, now); }
void co_framecost_test_nested(int inner, int outer, int64_t since, int64_t now) { Nested(inner, outer, since, now); }
int co_framecost_test_window_lines(void) { return g_fc.windowLines; }
int co_framecost_test_detail_lines(void) { return g_fc.detailLines; }
void co_framecost_test_last(double* low1, double* low01, uint32_t* over25, uint32_t* over33, uint32_t* over1s, int* starved)
{ *low1 = g_fc.low1; *low01 = g_fc.low01; *over25 = g_fc.lastOver25; *over33 = g_fc.lastOver33; *over1s = g_fc.lastOver1s; *starved = g_fc.lastStarved; }
#endif
