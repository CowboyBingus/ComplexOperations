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
    "daylight", "tactical map", "density", "scatter merge", "moving tiles", "ground keep", "far grass", "nav swaps", "nav tick"};

static struct
{
    int64_t freq, last, at; /* QPC: ticks a second, the last tick's start, the last mark */
    DWORD thread;           /* CoXlTick's */
    int lastPlay, cores;
    long mission, budgetMission;
    int64_t part[CO_FC_PARTS]; /* the frame being closed: its parts so far */
    int64_t extra[CO_FX_COUNT]; int grass, bodies;
    int64_t worldAt, inside; /* the world update under way since; ticks of parts and joins timed inside it this frame */
    volatile LONG64 offThread; /* ticks timed on other threads since the window began */
    CoFcContextFn context;
    /* the window */
    int64_t windowAt;
    uint64_t sysBusy, sysAll, proc; /* CPU times at the window's start, 100 ns */
    float ms[CO_FC_FRAMES];
    uint32_t frames, kept, over25, over33, over1s, slowNoted;
    uint32_t over16, led[CO_FC_CAUSES], only[CO_FC_CAUSES]; /* frames over CO_FC_TARGET_MS, by cause */
    double sum, worst, worstTimed, worstTop, timedSum;
    int worstTopPart;
    int windowLines, detailLines;
    /* the world update's own time in the frames the streamer changed bodies in, and in the others; once a mission had one */
    uint32_t bodyFrames, quietFrames, quietOver; double bodySum, bodyMost, quietSum, quietMost; long bodyMission; int bodySeen;
    uint32_t lastBodyFrames, lastQuietFrames, lastQuietOver; double lastBodyMean, lastBodyMost, lastQuietMean, lastQuietMost; int lastBodyLine;
    /* the window's slowest frames over CO_FC_SLOW_MS, slowest first, noted at its close */
    struct { double ms, timed, top, at, extra[CO_FX_COUNT]; int part, grass; const char* front; } slow[CO_FC_SLOW_LINES_WINDOW];
    /* the last window, for the tests */
    double low1, low01; uint32_t lastOver25, lastOver33, lastOver1s; int lastStarved;
    uint32_t lastOver16, lastLed[CO_FC_CAUSES], lastOnly[CO_FC_CAUSES];
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
    g_fc.frames = g_fc.kept = g_fc.over25 = g_fc.over33 = g_fc.over1s = g_fc.slowNoted = g_fc.over16 = 0;
    memset(g_fc.led, 0, sizeof g_fc.led); memset(g_fc.only, 0, sizeof g_fc.only);
    memset(g_fc.slow, 0, sizeof g_fc.slow);
    g_fc.sum = g_fc.worst = g_fc.worstTimed = g_fc.worstTop = g_fc.timedSum = 0; g_fc.worstTopPart = 0;
    g_fc.bodyFrames = g_fc.quietFrames = g_fc.quietOver = 0; g_fc.bodySum = g_fc.bodyMost = g_fc.quietSum = g_fc.quietMost = 0;
}

/* A change frame should hold the static layer's rebuild and the others should not: the two means apart say whether
 * coalescing the streamer's changes took the rebuild out of the frames between. */
static void BodiesLine(void)
{
    g_fc.lastBodyFrames = g_fc.bodyFrames; g_fc.lastQuietFrames = g_fc.quietFrames; g_fc.lastQuietOver = g_fc.quietOver;
    g_fc.lastBodyMean = g_fc.bodyFrames ? g_fc.bodySum / g_fc.bodyFrames : 0; g_fc.lastBodyMost = g_fc.bodyMost;
    g_fc.lastQuietMean = g_fc.quietFrames ? g_fc.quietSum / g_fc.quietFrames : 0; g_fc.lastQuietMost = g_fc.quietMost;
    g_fc.lastBodyLine = g_fc.bodySeen && g_fc.bodyMission == g_fc.mission;
    if (!g_fc.lastBodyLine) return;
    co_log("frame times, world update's own time by the streamer's body changes: %u frame(s) with a change, mean %.1f ms, the longest %.1f ms; "
        "%u without, mean %.1f ms, the longest %.1f ms; %u of those over %.1f ms in all", g_fc.bodyFrames, g_fc.lastBodyMean, g_fc.bodyMost,
        g_fc.quietFrames, g_fc.lastQuietMean, g_fc.quietMost, g_fc.quietOver, CO_FC_TARGET_MS);
}

/* `count`'s parts beside the nav ones, the most first, CO_FC_CAUSE_OTHERS at most: the line stays under the log's 512. */
static void CauseOthers(const uint32_t* count, char* out, size_t room)
{
    uint8_t used[CO_FC_PARTS] = {0}; size_t o = 0; uint32_t more = 0;
    out[0] = 0;
    for (int n = 0; n < CO_FC_CAUSE_OTHERS; n++)
    {
        int best = -1;
        for (int p = 0; p < CO_FC_PARTS; p++)
            if (p != CO_FC_NAVSWAP && p != CO_FC_NAVTICK && !used[p] && count[p] > 0 && (best < 0 || count[p] > count[best])) best = p;
        if (best < 0) break;
        used[best] = 1;
        o += (size_t)snprintf(out + o, room - o, ", %s %u", kPartNames[best], count[best]);
    }
    for (int p = 0; p < CO_FC_PARTS; p++) if (p != CO_FC_NAVSWAP && p != CO_FC_NAVTICK && !used[p]) more += count[p];
    if (more > 0) snprintf(out + o, room - o, ", other parts %u", more);
}
/* The window's frames over the target by cause, one line, when it had one. The nav parts, the joins and the world
 * update are always named, so a 0 is read and not taken from a missing word. */
static void CausesLine(double wallMs)
{
    memcpy(g_fc.lastLed, g_fc.led, sizeof g_fc.led); memcpy(g_fc.lastOnly, g_fc.only, sizeof g_fc.only); g_fc.lastOver16 = g_fc.over16;
    if (g_fc.over16 == 0) return;
    char led[96], only[96];
    CauseOthers(g_fc.led, led, sizeof led); CauseOthers(g_fc.only, only, sizeof only);
    co_log("frame times, causes (%.1f s of play): %u frame(s) over %.1f ms; led by (the largest of the timed parts, AI joins, the world update's own time, "
        "the rest): nav swaps %u, nav tick %u, AI joins %u, world update %u, the rest %u%s; over only with (within %.1f ms without it): nav swaps %u, nav tick %u, AI joins %u, world update %u%s", wallMs / 1000.0, g_fc.over16, CO_FC_TARGET_MS, g_fc.led[CO_FC_NAVSWAP],
        g_fc.led[CO_FC_NAVTICK], g_fc.led[CO_FC_CAUSE_JOIN], g_fc.led[CO_FC_CAUSE_WORLD], g_fc.led[CO_FC_CAUSE_REST], led, CO_FC_TARGET_MS,
        g_fc.only[CO_FC_NAVSWAP], g_fc.only[CO_FC_NAVTICK], g_fc.only[CO_FC_CAUSE_JOIN], g_fc.only[CO_FC_CAUSE_WORLD], only);
}

static void SlowLines(void);
static void WindowClose(int64_t now)
{
    double wallMs = Ms(now - g_fc.windowAt), offMs = Ms(InterlockedExchange64(&g_fc.offThread, 0));
    if (g_fc.frames == 0) return;
    SlowLines();
    CausesLine(wallMs);
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
        "%u over %.1f ms, %u over %.0f ms, %u over %.1f ms, %u of 1 s or more; timed parts (CoXlTick, the level update) %.2f ms a frame, off the game thread %.1f ms",
        wallMs / 1000.0, g_fc.frames, g_fc.sum > 0 ? 1000.0 * g_fc.frames / g_fc.sum : 0.0, g_fc.low1, g_fc.low01, g_fc.worst, g_fc.worstTimed,
        kPartNames[g_fc.worstTopPart], g_fc.worstTop, g_fc.over16, CO_FC_TARGET_MS, g_fc.over25, CO_FC_SLOW_MS, g_fc.over33, CO_FC_HITCH_MS, g_fc.over1s, g_fc.timedSum / g_fc.frames, offMs);
    co_log("frame times, CPU: the PC %.0f%% busy (%.1f of %d cores), the game %.1f cores, everything else %.1f%s; AI %d; the nearest garrison with living spawners %s",
        busyPct, sysCores, g_fc.cores, gameCores, elseCores, starved, ai, nestText);
    BodiesLine();
}

/* A slow frame kept for the window's close if it is among the window's slowest: the first ones were the lines before,
 * and a window with a 1.3 s stall noted two 25 ms frames instead of it. */
static void SlowKeep(double ms, double timed, int topPart, double top, double at)
{
    int k = CO_FC_SLOW_LINES_WINDOW;
    while (k > 0 && ms > g_fc.slow[k - 1].ms) k--;
    if (k == CO_FC_SLOW_LINES_WINDOW) return;
    memmove(&g_fc.slow[k + 1], &g_fc.slow[k], (CO_FC_SLOW_LINES_WINDOW - 1 - k) * sizeof g_fc.slow[0]);
    g_fc.slow[k].ms = ms; g_fc.slow[k].timed = timed; g_fc.slow[k].top = top; g_fc.slow[k].part = topPart; g_fc.slow[k].at = at;
    g_fc.slow[k].front = Front(); g_fc.slow[k].grass = g_fc.grass;
    for (int x = 0; x < CO_FX_COUNT; x++) g_fc.slow[k].extra[x] = Ms(g_fc.extra[x]);
}

/* The window's slowest frames, one detailed line each while the session's budget lasts. */
static void SlowLines(void)
{
    for (int k = 0; k < CO_FC_SLOW_LINES_WINDOW && g_fc.slow[k].ms > 0; k++)
    {
        if (g_fc.detailLines >= CO_FC_DETAIL_LINES_SESSION) break;
        g_fc.slowNoted++;
        co_log("frame times: a %.1f ms frame (QPC; the game's window %s): timed parts (CoXlTick, the level update) %.2f ms (the most: %s %.2f ms), untimed %.1f ms; "
            "%.1f s into the window; AI joins %.1f ms, world update %.1f ms, far-grass job %s", g_fc.slow[k].ms, g_fc.slow[k].front, g_fc.slow[k].timed,
            kPartNames[g_fc.slow[k].part], g_fc.slow[k].top, g_fc.slow[k].ms - g_fc.slow[k].timed, g_fc.slow[k].at, g_fc.slow[k].extra[CO_FX_JOIN],
            g_fc.slow[k].extra[CO_FX_WORLD], g_fc.slow[k].grass ? "out" : "not out");
        if (++g_fc.detailLines == CO_FC_DETAIL_LINES_SESSION) co_log("frame times: %d slow-frame lines this session; the window lines go on", CO_FC_DETAIL_LINES_SESSION);
    }
    memset(g_fc.slow, 0, sizeof g_fc.slow);
}

/* A frame over the target, counted under the cause that led it and under each one it would have been inside the
 * target without. The world update holds what is timed inside it; that comes off, so a nav tick in it is not hidden. */
static void FrameCause(double ms, double timed)
{
    double cost[CO_FC_CAUSES], inside = Ms(g_fc.inside); int lead = 0;
    for (int p = 0; p < CO_FC_PARTS; p++) cost[p] = Ms(g_fc.part[p]);
    cost[CO_FC_CAUSE_JOIN] = Ms(g_fc.extra[CO_FX_JOIN]);
    cost[CO_FC_CAUSE_WORLD] = Ms(g_fc.extra[CO_FX_WORLD]) > inside ? Ms(g_fc.extra[CO_FX_WORLD]) - inside : 0.0;
    double rest = ms - timed - cost[CO_FC_CAUSE_JOIN] - cost[CO_FC_CAUSE_WORLD];
    cost[CO_FC_CAUSE_REST] = rest > 0 ? rest : 0.0;
    g_fc.over16++;
    for (int c = 0; c < CO_FC_CAUSES; c++)
    {
        if (cost[c] > cost[lead]) lead = c;
        if (c != CO_FC_CAUSE_REST && cost[c] > 0 && ms - cost[c] <= CO_FC_TARGET_MS) g_fc.only[c]++;
    }
    g_fc.led[lead]++;
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
    const double world = Ms(g_fc.extra[CO_FX_WORLD]) > Ms(g_fc.inside) ? Ms(g_fc.extra[CO_FX_WORLD]) - Ms(g_fc.inside) : 0.0;
    if (g_fc.bodies) { g_fc.bodyFrames++; g_fc.bodySum += world; if (world > g_fc.bodyMost) g_fc.bodyMost = world; }
    else { g_fc.quietFrames++; g_fc.quietSum += world; if (world > g_fc.quietMost) g_fc.quietMost = world; g_fc.quietOver += ms > CO_FC_TARGET_MS; }
    g_fc.frames++; g_fc.sum += ms; g_fc.timedSum += timed;
    if (ms > g_fc.worst) { g_fc.worst = ms; g_fc.worstTimed = timed; g_fc.worstTop = top; g_fc.worstTopPart = topPart; }
    if (ms > CO_FC_TARGET_MS) FrameCause(ms, timed);
    if (ms >= CO_FC_LONG_MS) g_fc.over1s++;
    if (ms > CO_FC_HITCH_MS) g_fc.over33++;
    if (!(ms > CO_FC_SLOW_MS)) return;
    g_fc.over25++;
    SlowKeep(ms, timed, topPart, top, Ms(g_fc.last - g_fc.windowAt) / 1000.0); /* where it started */
}

static void Begin(int64_t now, int play, long mission)
{
    g_fc.thread = GetCurrentThreadId();
    /* a frame that starts inside the world update: the update's time so far is the closing frame's */
    if (g_fc.worldAt) { g_fc.extra[CO_FX_WORLD] += now - g_fc.worldAt; g_fc.worldAt = now; }
    if (g_fc.lastPlay && (!play || mission != g_fc.mission) && g_fc.frames > 0)
    { SlowLines(); CausesLine(Ms(now - g_fc.windowAt)); } /* a window cut short still names its stalls and their causes */
    if (play && (!g_fc.lastPlay || g_fc.windowAt == 0 || mission != g_fc.mission)) WindowStart(now); /* windows count play only */
    else if (play)
    {
        FrameClose(now);
        if (Ms(now - g_fc.windowAt) >= CO_FC_WINDOW_MS) { WindowClose(now); WindowStart(now); }
    }
    for (int p = 0; p < CO_FC_PARTS; p++) g_fc.part[p] = 0;
    memset(g_fc.extra, 0, sizeof g_fc.extra); g_fc.grass = 0; g_fc.bodies = 0; g_fc.inside = 0;
    g_fc.last = g_fc.at = now; g_fc.lastPlay = play; g_fc.mission = mission;
}

static void Mark(int part, int64_t now)
{
    if (part < 0 || part >= CO_FC_PARTS || g_fc.at == 0) return;
    g_fc.part[part] += now - g_fc.at;
    if (g_fc.worldAt) g_fc.inside += now - g_fc.at;
    g_fc.at = now;
}

static int64_t Add(int part, int64_t since, int64_t now)
{
    int64_t ticks = now - since;
    if (part < 0 || part >= CO_FC_PARTS || since == 0) return 0;
    if (GetCurrentThreadId() == g_fc.thread) { g_fc.part[part] += ticks; if (g_fc.worldAt) g_fc.inside += ticks; }
    else InterlockedExchangeAdd64(&g_fc.offThread, ticks);
    return ticks;
}

static void Nested(int inner, int outer, int64_t since, int64_t now)
{
    int64_t ticks = Add(inner, since, now);
    if (outer >= 0 && outer < CO_FC_PARTS && GetCurrentThreadId() == g_fc.thread)
    { g_fc.part[outer] -= ticks; if (g_fc.worldAt) g_fc.inside -= ticks; } /* the outer part's own add holds them again */
}

/* The game thread's only: another thread's wait or update is not this frame's. */
static void Extra(int which, int64_t since, int64_t now)
{
    if (which < 0 || which >= CO_FX_COUNT || since == 0 || g_fc.at == 0 || GetCurrentThreadId() != g_fc.thread) return;
    g_fc.extra[which] += now - since;
    if (which == CO_FX_JOIN && g_fc.worldAt) g_fc.inside += now - since;
}

static void World(int enter, int64_t now)
{
    if (g_fc.at == 0 || GetCurrentThreadId() != g_fc.thread) return;
    if (enter) { g_fc.worldAt = now; return; }
    if (g_fc.worldAt) g_fc.extra[CO_FX_WORLD] += now - g_fc.worldAt;
    g_fc.worldAt = 0;
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
void co_framecost_extra(int which, int64_t since) { Extra(which, since, co_framecost_now()); }
void co_framecost_world(int enter) { World(enter, co_framecost_now()); }
void co_framecost_grass(int out) { if (out && GetCurrentThreadId() == g_fc.thread) g_fc.grass = 1; }
void co_framecost_bodies(int changed)
{
    if (!changed || GetCurrentThreadId() != g_fc.thread) return;
    g_fc.bodies = 1; g_fc.bodySeen = 1; g_fc.bodyMission = g_fc.mission;
}

#ifdef CO_TEST
void co_framecost_test_reset(int64_t freq) { memset((void*)&g_fc, 0, sizeof g_fc); g_fc.freq = freq; g_fc.cores = 16; }
void co_framecost_test_cpu(uint64_t systemBusy, uint64_t systemAll, uint64_t process, int cores)
{ g_fc.testBusy = systemBusy; g_fc.testAll = systemAll; g_fc.testProc = process; g_fc.cores = cores; }
void co_framecost_test_begin(int64_t now, int play, long mission) { Begin(now, play, mission); }
void co_framecost_test_mark(int part, int64_t now) { Mark(part, now); }
int64_t co_framecost_test_add(int part, int64_t since, int64_t now) { return Add(part, since, now); }
void co_framecost_test_nested(int inner, int outer, int64_t since, int64_t now) { Nested(inner, outer, since, now); }
void co_framecost_test_extra(int which, int64_t since, int64_t now) { Extra(which, since, now); }
void co_framecost_test_world(int enter, int64_t now) { World(enter, now); }
void co_framecost_test_causes(uint32_t* over, uint32_t led[CO_FC_CAUSES], uint32_t only[CO_FC_CAUSES])
{ *over = g_fc.lastOver16; memcpy(led, g_fc.lastLed, sizeof g_fc.lastLed); memcpy(only, g_fc.lastOnly, sizeof g_fc.lastOnly); }
double co_framecost_test_slow(int k, double* join, double* world, int* grass)
{ *join = g_fc.slow[k].extra[CO_FX_JOIN]; *world = g_fc.slow[k].extra[CO_FX_WORLD]; *grass = g_fc.slow[k].grass; return g_fc.slow[k].ms; }
int co_framecost_test_window_lines(void) { return g_fc.windowLines; }
int co_framecost_test_bodies(uint32_t* changed, double* changedMean, double* changedMost, uint32_t* quiet, double* quietMean, double* quietMost, uint32_t* quietOver)
{
    *changed = g_fc.lastBodyFrames; *changedMean = g_fc.lastBodyMean; *changedMost = g_fc.lastBodyMost;
    *quiet = g_fc.lastQuietFrames; *quietMean = g_fc.lastQuietMean; *quietMost = g_fc.lastQuietMost; *quietOver = g_fc.lastQuietOver;
    return g_fc.lastBodyLine;
}
int co_framecost_test_detail_lines(void) { return g_fc.detailLines; }
void co_framecost_test_last(double* low1, double* low01, uint32_t* over25, uint32_t* over33, uint32_t* over1s, int* starved)
{ *low1 = g_fc.low1; *low01 = g_fc.low01; *over25 = g_fc.lastOver25; *over33 = g_fc.lastOver33; *over1s = g_fc.lastOver1s; *starved = g_fc.lastStarved; }
#endif
