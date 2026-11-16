/* The render thread's frame parts (co_rframe.h). The game thread owns all of this; the render thread never calls in. */
#if defined(CO_RESEARCH) || defined(CO_TEST)
#include <stdio.h>
#include <string.h>

#include "co.h"
#include "xl/co_rframe.h"

static const char* const kCol[CO_RF_COLS] = {
    "msg07", "msg08", "msg09", "msg0A", "msg0B", "msg0E", "msg10", "other",
    "cull(0x1EC4AF)", "lod(0x1E2FA6)", "lod(0x1E3E69)", "trees(0x1E3E32)", "join(0x1EDA86)", "scatter graph wait (0x21140C)", "dispatch(0x6CC130|0x6CA5F0)",
    "apply8(0x1E84A0)", "spin8(0x1E95B6)", "chain(0x10BB30)", "gpuwait(0x6C5F43)", "drain(0x6C601D)", "present1(0x6C601D-0x6C6178)",
    "period", "outside(idle 0x113F1D, block switch 0x113F4A, loop)", "gpu",
    "bake view", "bake cells", "bake instances", "bake wrappers us"};
/* The slowest 1% line's short names, in column order. */
static const char* const kShort[CO_RF_COLS] = {
    "07", "08", "09", "0A", "0B", "0E", "10", "other", "cull", "lodA", "lodB", "trees", "join", "scatwait", "dispatch", "apply8", "spin8",
    "chain", "gpuwait", "drain", "present1", "period", "outside", "gpu", "bake", "bakeCells", "bakeInst", "bakeCoUs"};

static struct
{
    int64_t freq, windowAt;
    double clockNs;
    uint32_t timed;
    float col[CO_RF_FRAMES][CO_RF_COLS];
    uint32_t frames, kept, gpuFrames;
    double sum[CO_RF_COLS], worst[CO_RF_COLS];
    uint64_t types[CO_RF_TYPES], applies, applyCut, dispatches, records, clocks, walked, rounds8, foreign, spinEndsAtClose;
    uint32_t recordsMost, clocksMost, walkedMost;
    float fence[CO_RF_FRAMES];
    uint32_t fences, fenceKept;
    double fenceSum, fenceWorst;
    float scratch[CO_RF_FRAMES];
    /* the last closed window, for the tests */
    double lastMean[CO_RF_COLS], lastP99[CO_RF_COLS], lastWorst[CO_RF_COLS], lastSlow[CO_RF_COLS];
    uint32_t lastSlowN, lastFences;
    float lastEdge;
    double lastFence[3];
    uint32_t lastBakeFrames, lastBakeOver; double lastBakeAdded;
} g_rf;

static double Ms(double ticks) { return g_rf.freq > 0 ? ticks * 1000.0 / (double)g_rf.freq : 0.0; }

float co_rframe_select(float* v, uint32_t n, uint32_t k)
{
    if (n == 0) return 0.0f;
    if (k >= n) k = n - 1;
    int32_t l = 0, r = (int32_t)n - 1;
    const int32_t kk = (int32_t)k;
    while (l < r) /* Wirth's selection: v[k] ends as the k-th smallest */
    {
        const float x = v[kk];
        int32_t i = l, j = r;
        do
        {
            while (v[i] < x) i++;
            while (x < v[j]) j--;
            if (i <= j) { const float t = v[i]; v[i] = v[j]; v[j] = t; i++; j--; }
        } while (i <= j);
        if (j < kk) l = i;
        if (kk < i) r = j;
    }
    return v[kk];
}

static void WindowStart(int64_t now)
{
    g_rf.windowAt = now;
    g_rf.frames = g_rf.kept = g_rf.gpuFrames = g_rf.fences = g_rf.fenceKept = 0;
    memset(g_rf.sum, 0, sizeof g_rf.sum); memset(g_rf.worst, 0, sizeof g_rf.worst); memset(g_rf.types, 0, sizeof g_rf.types);
    g_rf.applies = g_rf.applyCut = g_rf.dispatches = g_rf.records = g_rf.clocks = g_rf.walked = g_rf.rounds8 = g_rf.foreign = g_rf.spinEndsAtClose = 0;
    g_rf.recordsMost = g_rf.clocksMost = g_rf.walkedMost = 0;
    g_rf.fenceSum = g_rf.fenceWorst = 0;
}

void co_rframe_reset(int64_t freq, double clockNs, uint32_t timed, int64_t now)
{
    memset(&g_rf, 0, sizeof g_rf);
    g_rf.freq = freq; g_rf.clockNs = clockNs; g_rf.timed = timed;
    WindowStart(now);
}
void co_rframe_timed(uint32_t timed) { g_rf.timed = timed; }

void co_rframe_frame(const CoRfFrame* f)
{
    float row[CO_RF_COLS];
    double messages = 0;
    for (int p = 0; p < CO_RF_TIMED; p++)
    {
        row[p] = (float)Ms((double)f->t[p]);
        if (p <= CO_RF_MOTHER) messages += (double)f->t[p];
    }
    row[CO_RF_PERIOD] = (float)Ms((double)f->period);
    const double outside = (double)f->period - messages; /* nested messages are timed once, by the outer one */
    row[CO_RF_OUTSIDE] = outside > 0 ? (float)Ms(outside) : 0.0f;
    row[CO_RF_GPU] = f->gpuMs;
    row[CO_RF_BAKE] = f->bakeUpdates > 0 ? 1.0f : 0.0f;
    row[CO_RF_BAKECELLS] = (float)f->bakeCells; row[CO_RF_BAKEINST] = (float)f->bakeInst;
    row[CO_RF_BAKECO] = (float)(Ms((double)f->bakeTicks) * 1000.0);
    for (int p = 0; p < CO_RF_COLS; p++)
    {
        if (p == CO_RF_GPU && row[p] < 0) continue;
        g_rf.sum[p] += row[p];
        if (row[p] > g_rf.worst[p]) g_rf.worst[p] = row[p];
    }
    if (row[CO_RF_GPU] >= 0) g_rf.gpuFrames++;
    if (g_rf.kept < CO_RF_FRAMES) memcpy(g_rf.col[g_rf.kept++], row, sizeof row);
    g_rf.frames++;
    for (int t = 0; t < CO_RF_TYPES; t++) g_rf.types[t] += f->types[t];
    g_rf.applies += f->applies; g_rf.applyCut += f->applyCut; g_rf.dispatches += f->dispatches; g_rf.records += f->records;
    g_rf.clocks += f->clocks; g_rf.walked += f->walked; g_rf.rounds8 += f->rounds8; g_rf.foreign += f->foreign; g_rf.spinEndsAtClose += f->spinEndsAtClose;
    if (f->recordsMost > g_rf.recordsMost) g_rf.recordsMost = f->recordsMost;
    if (f->clocks > g_rf.clocksMost) g_rf.clocksMost = f->clocks;
    if (f->walked > g_rf.walkedMost) g_rf.walkedMost = f->walked;
}

void co_rframe_fence(int64_t ticks)
{
    const float ms = (float)Ms((double)ticks);
    g_rf.fences++; g_rf.fenceSum += ms;
    if (ms > g_rf.fenceWorst) g_rf.fenceWorst = ms;
    if (g_rf.fenceKept < CO_RF_FRAMES) g_rf.fence[g_rf.fenceKept++] = ms;
}

int co_rframe_due(int64_t now) { return g_rf.freq > 0 && (double)(now - g_rf.windowAt) >= CO_RF_WINDOW_S * (double)g_rf.freq; }

/* Nearest rank: the value 99% of the kept values are at or under. */
static double P99(const float* v, uint32_t n)
{
    if (n == 0) return 0;
    memcpy(g_rf.scratch, v, (size_t)n * sizeof *v);
    return co_rframe_select(g_rf.scratch, n, (uint32_t)((99ull * n + 99) / 100) - 1);
}

static void Stats(void)
{
    for (int p = 0; p < CO_RF_COLS; p++)
    {
        uint32_t n = 0;
        for (uint32_t i = 0; i < g_rf.kept; i++)
            if (p != CO_RF_GPU || g_rf.col[i][p] >= 0) g_rf.scratch[n++] = g_rf.col[i][p];
        const uint32_t all = p == CO_RF_GPU ? g_rf.gpuFrames : g_rf.frames;
        g_rf.lastMean[p] = all ? g_rf.sum[p] / all : 0;
        g_rf.lastP99[p] = n ? co_rframe_select(g_rf.scratch, n, (uint32_t)((99ull * n + 99) / 100) - 1) : 0;
        g_rf.lastWorst[p] = g_rf.worst[p];
    }
    /* the slowest 1% by period: those over the k-th largest, then those equal to it until k */
    const uint32_t k = (g_rf.kept + 99) / 100;
    memset(g_rf.lastSlow, 0, sizeof g_rf.lastSlow);
    g_rf.lastSlowN = k; g_rf.lastEdge = 0;
    if (k == 0) return;
    for (uint32_t i = 0; i < g_rf.kept; i++) g_rf.scratch[i] = g_rf.col[i][CO_RF_PERIOD];
    const float edge = co_rframe_select(g_rf.scratch, g_rf.kept, g_rf.kept - k);
    uint32_t taken = 0, gpuTaken = 0;
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < g_rf.kept && taken < k; i++)
        {
            const float v = g_rf.col[i][CO_RF_PERIOD];
            if (pass == 0 ? !(v > edge) : v != edge) continue;
            for (int p = 0; p < CO_RF_COLS; p++)
                if (p != CO_RF_GPU) g_rf.lastSlow[p] += g_rf.col[i][p];
                else if (g_rf.col[i][p] >= 0) { g_rf.lastSlow[p] += g_rf.col[i][p]; gpuTaken++; }
            taken++;
        }
    for (int p = 0; p < CO_RF_COLS; p++) g_rf.lastSlow[p] = p == CO_RF_GPU ? (gpuTaken ? g_rf.lastSlow[p] / gpuTaken : -1) : g_rf.lastSlow[p] / taken;
    g_rf.lastEdge = edge;
}

static int Timed(int p) { return p >= CO_RF_TIMED || (g_rf.timed >> p) & 1u; }

/* " name mean/p99/worst," for each column in [from, to); untimed ones "-". */
static size_t Columns(char* out, size_t room, int from, int to)
{
    size_t o = 0;
    out[0] = 0;
    for (int p = from; p < to && o + 2 < room; p++)
    {
        if (!Timed(p)) o += (size_t)snprintf(out + o, room - o, "%s %s -", p > from ? "," : "", kCol[p]);
        else o += (size_t)snprintf(out + o, room - o, "%s %s %.2f/%.2f/%.2f", p > from ? "," : "", kCol[p], g_rf.lastMean[p], g_rf.lastP99[p], g_rf.lastWorst[p]);
    }
    return o;
}

/* The kept frames with a far-grass bake view against those without: what a bake view adds to its render frame. */
static void BakeLine(void)
{
    uint32_t with = 0, without = 0, overWith = 0, overWithout = 0;
    double sum[2] = {0, 0}, worst[2] = {0, 0}, cells = 0, inst = 0, co = 0, wait = 0, cellsMost = 0, instMost = 0, coMost = 0, waitMost = 0, p99[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++)
    {
        uint32_t n = 0;
        for (uint32_t i = 0; i < g_rf.kept; i++)
        {
            const float* r = g_rf.col[i];
            if ((r[CO_RF_BAKE] > 0) != (pass == 0)) continue;
            g_rf.scratch[n++] = r[CO_RF_PERIOD];
            sum[pass] += r[CO_RF_PERIOD];
            if (r[CO_RF_PERIOD] > worst[pass]) worst[pass] = r[CO_RF_PERIOD];
            if (r[CO_RF_PERIOD] > CO_RF_STALL_MS) { if (pass == 0) overWith++; else overWithout++; }
            if (pass != 0) continue;
            cells += r[CO_RF_BAKECELLS]; inst += r[CO_RF_BAKEINST]; co += r[CO_RF_BAKECO]; wait += r[CO_RF_SCATWAIT];
            if (r[CO_RF_BAKECELLS] > cellsMost) cellsMost = r[CO_RF_BAKECELLS];
            if (r[CO_RF_BAKEINST] > instMost) instMost = r[CO_RF_BAKEINST];
            if (r[CO_RF_BAKECO] > coMost) coMost = r[CO_RF_BAKECO];
            if (r[CO_RF_SCATWAIT] > waitMost) waitMost = r[CO_RF_SCATWAIT];
        }
        if (pass == 0) with = n; else without = n;
        p99[pass] = n ? co_rframe_select(g_rf.scratch, n, (uint32_t)((99ull * n + 99) / 100) - 1) : 0;
    }
    const double meanWith = with ? sum[0] / with : 0, meanWithout = without ? sum[1] / without : 0, added = with && without ? meanWith - meanWithout : 0;
    g_rf.lastBakeFrames = with; g_rf.lastBakeOver = overWith; g_rf.lastBakeAdded = added;
    if (with == 0) return;
    co_log("render frame bake views: %u of %u frames ran a far-grass bake view; period ms mean/p99/worst with one %.2f/%.2f/%.2f, without %.2f/%.2f/%.2f; "
        "a bake view adds %.2f ms (%s %.1f ms); frames over %.0f ms: %u with a bake view, %u without; a bake view: cells %.0f (most %.0f), instances %.0f "
        "(most %.0f), the mod's wrappers %.0f us (most %.0f) over its threads, scatter graph wait (0x21140C) %.2f ms (most %.2f)",
        with, g_rf.kept, meanWith, p99[0], worst[0], meanWithout, p99[1], worst[1], added, without == 0 ? "no frame without one to compare; goal" :
        added < CO_RF_BAKE_ADDED_MS ? "within the goal of" : "OVER the goal of", CO_RF_BAKE_ADDED_MS, CO_RF_STALL_MS, overWith, overWithout, cells / with, cellsMost,
        inst / with, instMost, co / with, coMost, wait / with, waitMost);
}

int co_rframe_close(int64_t now, const CoRfCounts* c)
{
    const double seconds = g_rf.freq > 0 ? (double)(now - g_rf.windowAt) / (double)g_rf.freq : 0;
    int lines = 0;
    char a[700], b[700];
    g_rf.lastBakeFrames = g_rf.lastBakeOver = 0; g_rf.lastBakeAdded = 0;
    if (g_rf.frames > 0)
    {
        Stats();
        const double clocks = (double)g_rf.clocks / g_rf.frames, walked = (double)g_rf.walked / g_rf.frames;
        co_log("render frame: %u frames in %.1f s (%u past the %u kept, %u lost to a full ring); ms mean/p99/worst: period %.2f/%.2f/%.2f, "
            "gpu (dev+0x1480) %.2f/%.2f/%.2f over %u frames; the instrument a frame: %.1f clock reads (most %u), %.1f items walked (most %u), "
            "about %.1f us (most %.1f) at %.0f ns a read and %.0f an item",
            g_rf.frames, seconds, g_rf.frames - g_rf.kept, CO_RF_FRAMES, c ? c->lost : 0, g_rf.lastMean[CO_RF_PERIOD], g_rf.lastP99[CO_RF_PERIOD],
            g_rf.lastWorst[CO_RF_PERIOD], g_rf.lastMean[CO_RF_GPU], g_rf.lastP99[CO_RF_GPU], g_rf.lastWorst[CO_RF_GPU], g_rf.gpuFrames, clocks,
            g_rf.clocksMost, walked, g_rf.walkedMost, (clocks * g_rf.clockNs + walked * CO_RF_ITEM_NS) / 1000.0,
            ((double)g_rf.clocksMost * g_rf.clockNs + (double)g_rf.walkedMost * CO_RF_ITEM_NS) / 1000.0, g_rf.clockNs, CO_RF_ITEM_NS);
        double messages = 0, named = 0;
        for (int p = CO_RF_M07; p <= CO_RF_MOTHER; p++) messages += g_rf.lastMean[p];
        for (int p = CO_RF_CULL; p < CO_RF_TIMED; p++) named += Timed(p) ? g_rf.lastMean[p] : 0.0;
        Columns(a, sizeof a, CO_RF_M07, CO_RF_MOTHER + 1);
        co_log("render frame parts, ms mean/p99/worst:%s, %s %.2f/%.2f/%.2f; a frame: messages %.2f and outside %.2f of the period's %.2f, the named waits and work %.2f of the messages",
            a, kCol[CO_RF_OUTSIDE], g_rf.lastMean[CO_RF_OUTSIDE], g_rf.lastP99[CO_RF_OUTSIDE], g_rf.lastWorst[CO_RF_OUTSIDE], messages,
            g_rf.lastMean[CO_RF_OUTSIDE], g_rf.lastMean[CO_RF_PERIOD], named);
        Columns(a, sizeof a, CO_RF_CULL, CO_RF_DISPATCH + 1);
        co_log("render frame waits in message 7, ms mean/p99/worst:%s", a);
        Columns(b, sizeof b, CO_RF_APPLY, CO_RF_PRESENT1 + 1);
        co_log("render frame waits in messages 8, 0B and 9, ms mean/p99/worst:%s", b);
        size_t o = 0;
        a[0] = 0;
        for (int p = 0; p < CO_RF_COLS && o + 2 < sizeof a; p++)
            if (p == CO_RF_PERIOD) continue;
            else if (!Timed(p) || (p == CO_RF_GPU && g_rf.lastSlow[p] < 0)) o += (size_t)snprintf(a + o, sizeof a - o, ", %s -", kShort[p]);
            else o += (size_t)snprintf(a + o, sizeof a - o, ", %s %.2f", kShort[p], g_rf.lastSlow[p]);
        co_log("render frame slowest 1%%: %u frames of %.2f ms and over, ms mean: period %.2f%s", g_rf.lastSlowN, (double)g_rf.lastEdge, g_rf.lastSlow[CO_RF_PERIOD], a);
        o = 0; a[0] = 0;
        for (int t = 0; t < CO_RF_TYPES && o + 2 < sizeof a; t++)
            if (g_rf.types[t]) o += (size_t)snprintf(a + o, sizeof a - o, " %02X:%llu", t, (unsigned long long)g_rf.types[t]);
        co_log("render frame counts: message 8 applies %llu (%llu walked only to their first %d items), items by type:%s; dispatches %.2f a frame, "
            "draw records a dispatch %.0f (most %u); spin8 rounds %llu (spins closed at the next event, their last round not seen: %llu); calls through another mod's hook, not counted: %llu",
            (unsigned long long)g_rf.applies, (unsigned long long)g_rf.applyCut, CO_RF_WALK, o ? a : " none", (double)g_rf.dispatches / g_rf.frames,
            g_rf.dispatches ? (double)g_rf.records / (double)g_rf.dispatches : 0.0, g_rf.recordsMost, (unsigned long long)g_rf.rounds8,
            (unsigned long long)g_rf.spinEndsAtClose, (unsigned long long)g_rf.foreign);
        lines += 6;
        BakeLine();
        if (g_rf.lastBakeFrames) lines++;
    }
    if (c != NULL && (g_rf.frames > 0 || c->views > 0))
    {
        size_t o = 0;
        a[0] = 0;
        if (c->pools == 1)
            for (int k = 0; k < 5; k++)
                o += (size_t)snprintf(a + o, sizeof a - o, " %d:%u/%u,%u/%u", k, c->shown[k], c->shownMost[k], c->parked[k], c->parkedMost[k]);
        else snprintf(a, sizeof a, " not read");
        if (c->stamps == 1)
            snprintf(b, sizeof b, "under 550 m %u, 550-800 %u, 800-1100 %u, 1100-1500 %u, over 1500 %u (%u far, %u live)", c->band[0], c->band[1], c->band[2],
                c->band[3], c->band[4], c->farCount, c->farLive);
        else snprintf(b, sizeof b, "%s", c->stamps < 0 ? "not read (the streamer's lock was busy)" : "not read");
        co_log("render frame world: SpeedTree cells a view %.0f (most %u, %llu views); culling sets (main world; shown now/most, parked now/most):%s; "
            "far window stamps by the nearest player: %s",
            c->views ? (double)c->cells / (double)c->views : 0.0, c->cellsMost, (unsigned long long)c->views, a, b);
        lines++;
    }
    if (g_rf.fences > 0)
    {
        g_rf.lastFence[0] = g_rf.fenceSum / g_rf.fences; g_rf.lastFence[1] = P99(g_rf.fence, g_rf.fenceKept); g_rf.lastFence[2] = g_rf.fenceWorst;
        co_log("frame fence (exe 0x8A7C4, the game thread): %u waits in %.1f s, ms mean %.2f, p99 %.2f, worst %.2f, %.1f in all",
            g_rf.fences, seconds, g_rf.lastFence[0], g_rf.lastFence[1], g_rf.lastFence[2], g_rf.fenceSum);
        lines++;
    }
    g_rf.lastFences = g_rf.fences;
    WindowStart(now);
    return lines;
}

const char* co_rframe_wait_name(uint32_t rva)
{
    static const struct { uint32_t rva; const char* name; } kWaits[] = {
        {0x1EC4AFu, "message 7's cull job wait (0x627160 from 0x1EC4AA)"},
        {0x1E2FA6u, "message 7's LOD job wait (0x627160 from 0x1E2FA1)"},
        {0x1E3E69u, "message 7's LOD wait (0x6097A0 from 0x1E3E64)"},
        {0x1E3E32u, "message 7's SpeedTree view task spin (0x213540 from 0x1E3E2D)"},
        {0x1EDA86u, "message 7's generator join (0x252460 from 0x1EDA81)"},
        {0x211411u, "the scatter graph wait (0x6097A0 from 0x21140C)"},
        {0x6CED4Cu, "the draw dispatch 0x6CC130"},
        {0x6CED5Du, "the draw dispatch 0x6CA5F0"},
        {0x1E95A0u, "message 8's object messages (0x1E84A0 from 0x1E959B)"},
        {0x1E95F8u, "message 8's device-chain spin round (0x528210 from 0x1E95F3)"},
        {0x10BB9Au, "the device-chain spin's round (0x528210 from 0x10BB95)"},
        {0x6C5F43u, "present's frames-in-flight GPU wait (0x6C0590 from 0x6C5F3E)"},
        {0x6C601Du, "present's submit drain (0x6BEED0 from 0x6C6018)"},
        {0x6C60ABu, "Present1 (the call at 0x6C60A5)"},
        {0x113F22u, "the render loop's idle job poll (0x626900 from 0x113F1D)"},
        {0x113EF3u, "a render message (0x113010 from 0x113EEE)"},
    };
    for (size_t i = 0; i < sizeof kWaits / sizeof kWaits[0]; i++)
        if (kWaits[i].rva == rva) return kWaits[i].name;
    return NULL;
}

#ifdef CO_TEST
void co_rframe_test_col(int col, double* mean, double* p99, double* worst, double* slowMean)
{
    *mean = g_rf.lastMean[col]; *p99 = g_rf.lastP99[col]; *worst = g_rf.lastWorst[col]; *slowMean = g_rf.lastSlow[col];
}
void co_rframe_test_fence(uint32_t* waits, double* mean, double* p99, double* worst)
{
    *waits = g_rf.lastFences; *mean = g_rf.lastFence[0]; *p99 = g_rf.lastFence[1]; *worst = g_rf.lastFence[2];
}
uint32_t co_rframe_test_slow(void) { return g_rf.lastSlowN; }
void co_rframe_test_bake(uint32_t* frames, uint32_t* over, double* added) { *frames = g_rf.lastBakeFrames; *over = g_rf.lastBakeOver; *added = g_rf.lastBakeAdded; }
#endif
#else
typedef int co_rframe_none; /* a package build: no instrument */
#endif
