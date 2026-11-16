/* The render frame instrument (src/xl/co_rframe.c, src/xl/proto/xl_render_frame.c): the window's statistics, the
 * parts each hook's call site counts under, message 8's walk and spin, present's split, the ring, the pools, the far
 * stamps' bands, the stall sample's reading and the install, on a test clock of 1,000,000 ticks a second. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_rframe.h"

void CoXlTestXrfReset(uintptr_t exe, int64_t freq, int64_t now);
void CoXlTestXrfClock(int64_t now);
void CoXlTestXrfHookOut(int hook);
void CoXlTestXrfThread(DWORD render);
int CoXlTestXrfPartFor(int hook, uintptr_t ret);
void CoXlTestXrfAdd(int part, int64_t t0, int64_t t1);
void CoXlTestXrfFrameEnd(uint64_t r);
void CoXlTestXrfMessageDone(uint16_t type, int64_t a, int64_t b);
uint64_t CoXlTestXrfMessage(uint64_t r, uint64_t message, uint64_t payload, uint64_t (*orig)(uint64_t, uint64_t, uint64_t));
void CoXlTestXrfWalk(uint64_t stream);
void CoXlTestXrfApplied(uintptr_t ret, int64_t t1);
void CoXlTestXrfRoundAt(uintptr_t ret, int64_t now);
void CoXlTestXrfSpinClose(void);
void CoXlTestXrfRoundPending(uintptr_t ret);
void CoXlTestXrfRound(uintptr_t ret, uintptr_t dev);
void CoXlTestXrfBake(int updates, uint64_t cells, uint64_t instances, int64_t ticks);
LONG CoXlTestXrfInPart(void);
uint32_t CoXlTestXrfReads(void);
void CoXlTestXrfSetOrig(int hook, void* fn);
uint64_t CoXlTestXrfDispatchAt(uintptr_t ret, uint64_t records);
void CoXlTestGame(uintptr_t game);
void CoXlTestXrfRecords(uint32_t n);
void CoXlTestXrfDrained(int64_t t1);
void CoXlTestXrfAfterAt(uintptr_t ret, int64_t now);
void CoXlTestXrfCellsAt(uintptr_t ret, uint64_t owner);
void CoXlTestXrfFence(int64_t in, int64_t out);
const CoRfFrame* CoXlTestXrfCurrent(void);
uint32_t CoXlTestXrfRingFill(void);
uint32_t CoXlTestXrfLost(void);
void CoXlTestXrfTick(void);
void CoXlTestXrfPoolsRw(uint64_t rw);
uint32_t CoXlTestXrfTimed(void);
int CoXlTestXrfStallDue(int64_t at, int64_t now, LONG seq);
void CoXlTestXrfStall(int k, uint64_t rip, double ms, double suspendedUs, const void* stack, uint32_t len);
int CoXlTestXrfInstall(const char** in, const char** refused, const char** off);
void* CoXlTestXrfOrig(int hook);
void CoXlTestXrfStampsSet(int on, uint32_t base, uint32_t n, const float (*at)[2], const float* out2, const uint8_t* live, int players, const float (*playerAt)[2]);

/* xl_render_frame.c's hook order */
enum { XH_JOBWAIT, XH_LODWAIT, XH_TREES, XH_JOIN, XH_DISPA, XH_DISPB, XH_APPLY, XH_CHAIN, XH_GPUWAIT, XH_DRAIN, XH_AFTER, XH_CELLS, XH_MESSAGE, XH_COUNT };

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define US 1000000
#define CLOSE(a, b) ((a) > (b) - 1e-3 && (a) < (b) + 1e-3)

static uintptr_t g_exe;
static int64_t g_now;

static void Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_exe = (uintptr_t)fake_host_exe();
    g_now = 1000;
    CoXlTestXrfReset(g_exe, US, g_now);
    CoXlTestXrfTick(); /* the first tick starts the window */
}
static void Clock(int64_t us) { g_now += us; CoXlTestXrfClock(g_now); }
static void Msg(uint16_t type, int64_t us) { CoXlTestXrfMessageDone(type, g_now, g_now + us); Clock(us); }
static void FrameEnd(uint64_t r) { CoXlTestXrfClock(g_now); CoXlTestXrfFrameEnd(r); }
/* Every log line under the log's 512. */
static int LinesShort(void)
{
    const char* p = g_fake.log;
    while (*p)
    {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= 511) { printf("long line (%u): %.80s\n", (unsigned)n, p); return 0; }
        p += n + (e != NULL);
    }
    return 1;
}

static int Ascending(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return (x > y) - (x < y); }

static void TestSelect(void)
{
    g_case = "selection: the k-th smallest of random, equal and sorted values is qsort's";
    static float v[4096], w[4096];
    unsigned seed = 7;
    for (int round = 0; round < 60; round++)
    {
        const uint32_t n = 1 + (uint32_t)(round * 67) % 4096;
        for (uint32_t i = 0; i < n; i++)
        {
            seed = seed * 1103515245u + 12345u;
            v[i] = round % 3 == 0 ? (float)(seed >> 20 & 7) : round % 3 == 1 ? (float)i : (float)(seed >> 8) / 65536.0f;
        }
        memcpy(w, v, n * sizeof *v);
        qsort(w, n, sizeof *w, Ascending);
        const uint32_t k = (uint32_t)(round * 31) % n;
        CHECK(co_rframe_select(v, n, k) == w[k]);
        CHECK(co_rframe_select(v, n, n - 1) == w[n - 1] && co_rframe_select(v, n, 0) == w[0]);
    }
}

/* 100 render frames of 10 ms (msg07 6, msg08 2, msg09 1, outside 1; cull 3 in msg07) and one of 50 ms (msg07 46, cull 40). */
static void Frames100(uint64_t r)
{
    FrameEnd(r); /* the first frame end only starts a frame */
    for (int i = 0; i < 100; i++)
    {
        const int slow = i == 50;
        CoXlTestXrfAdd(CO_RF_CULL, 0, slow ? 40000 : 3000);
        Msg(0x07, slow ? 46000 : 6000);
        Msg(0x08, 2000);
        Msg(0x09, 1000);
        Clock(1000);
        FrameEnd(r);
    }
}

static void TestWindow(void)
{
    Reset("a window: period, parts, outside, p99, worst, the slowest 1%, the lines");
    Frames100(0);
    CHECK(CoXlTestXrfRingFill() == 100);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(CoXlTestXrfRingFill() == 0);
    double mean, p99, worst, slow;
    co_rframe_test_col(CO_RF_PERIOD, &mean, &p99, &worst, &slow);
    CHECK(CLOSE(mean, 10.4) && CLOSE(p99, 10.0) && CLOSE(worst, 50.0) && CLOSE(slow, 50.0));
    CHECK(co_rframe_test_slow() == 1);
    co_rframe_test_col(CO_RF_M07, &mean, &p99, &worst, &slow);
    CHECK(CLOSE(mean, 6.4) && CLOSE(p99, 6.0) && CLOSE(worst, 46.0) && CLOSE(slow, 46.0));
    co_rframe_test_col(CO_RF_CULL, &mean, &p99, &worst, &slow);
    CHECK(CLOSE(mean, 3.37) && CLOSE(worst, 40.0) && CLOSE(slow, 40.0));
    co_rframe_test_col(CO_RF_OUTSIDE, &mean, &p99, &worst, &slow);
    CHECK(CLOSE(mean, 1.0) && CLOSE(worst, 1.0));
    co_rframe_test_col(CO_RF_GPU, &mean, &p99, &worst, &slow);
    CHECK(mean == 0 && slow == -1); /* no renderer: not read */
    CHECK(fake_host_logged("render frame: 100 frames in 11.0 s (0 past the 4096 kept, 0 lost to a full ring); ms mean/p99/worst: period 10.40/10.00/50.00, gpu (dev+0x1480) 0.00/0.00/0.00 over 0 frames"));
    CHECK(fake_host_logged("the instrument a frame: 8.0 clock reads (most 8), 0.0 items walked (most 0), about 0.4 us (most 0.4) at 50 ns a read and 100 an item"));
    CHECK(fake_host_logged("render frame parts, ms mean/p99/worst: msg07 6.40/6.00/46.00, msg08 2.00/2.00/2.00, msg09 1.00/1.00/1.00, msg0A 0.00/0.00/0.00"));
    CHECK(fake_host_logged("outside(idle 0x113F1D, block switch 0x113F4A, loop) 1.00/1.00/1.00; a frame: messages 9.40 and outside 1.00 of the period's 10.40, the named waits and work 3.37 of the messages"));
    CHECK(fake_host_logged("render frame waits in message 7, ms mean/p99/worst: cull(0x1EC4AF) 3.37/3.00/40.00, lod(0x1E2FA6) 0.00/0.00/0.00"));
    CHECK(fake_host_logged("render frame slowest 1%: 1 frames of 50.00 ms and over, ms mean: period 50.00, 07 46.00, 08 2.00, 09 1.00, 0A 0.00"));
    CHECK(fake_host_logged(", cull 40.00, lodA 0.00,"));
    CHECK(fake_host_logged(", outside 1.00, gpu -"));
    CHECK(fake_host_logged("spin8(0x1E95B6) -")); /* no render help hook in the test: message 8's rounds are not seen */
    CHECK(!fake_host_logged("frame fence (exe 0x8A7C4")); /* no waits, no line */
    CHECK(LinesShort());
}

static void TestUntimedAndGpu(void)
{
    Reset("parts without their hook print -, and the GPU time is read from [[r+0x1168]+0x1480] in seconds");
    static uint8_t r[0x1200], dev[0x1500];
    *(uint64_t*)(r + 0x1168) = (uint64_t)(uintptr_t)dev;
    *(float*)(dev + 0x1480) = 0.0125f;
    CoXlTestXrfHookOut(XH_LODWAIT);
    CoXlTestXrfHookOut(XH_DISPB); /* one of the two dispatches out: the dispatch is not whole */
    Frames100((uint64_t)(uintptr_t)r);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("lod(0x1E3E69) -, trees(0x1E3E32) 0.00/0.00/0.00"));
    CHECK(fake_host_logged("dispatch(0x6CC130|0x6CA5F0) -"));
    CHECK(fake_host_logged(", lodB -, trees 0.00,"));
    CHECK(fake_host_logged("gpu (dev+0x1480) 12.50/12.50/12.50 over 100 frames"));
    double mean, p99, worst, slow;
    co_rframe_test_col(CO_RF_GPU, &mean, &p99, &worst, &slow);
    CHECK(CLOSE(mean, 12.5) && CLOSE(slow, 12.5));
    *(float*)(dev + 0x1480) = 50.0f; /* not a frame time: not read */
    Frames100((uint64_t)(uintptr_t)r);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("gpu (dev+0x1480) 0.00/0.00/0.00 over 0 frames"));
    /* frames with and without a read in one window: the mean is over the read ones only */
    for (int i = 0; i < 4; i++)
    {
        *(float*)(dev + 0x1480) = i < 2 ? 0.010f : 50.0f;
        Clock(10000); FrameEnd((uint64_t)(uintptr_t)r);
    }
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("gpu (dev+0x1480) 10.00/10.00/10.00 over 2 frames"));
}

static void TestPartFor(void)
{
    Reset("each hook's call sites by return address; another exe site none; an address outside the exe is another hook's");
    const struct { int hook; uint32_t ret; int part; } k[] = {
        {XH_JOBWAIT, 0x1EC4AFu, CO_RF_CULL}, {XH_JOBWAIT, 0x1E2FA6u, CO_RF_LODA}, {XH_JOBWAIT, 0x1E3E69u, -1}, {XH_LODWAIT, 0x1E3E69u, CO_RF_LODB},
        {XH_LODWAIT, 0x1EC4AFu, -1}, {XH_TREES, 0x1E3E32u, CO_RF_TREES}, {XH_TREES, 0x3658CCu, -1}, {XH_JOIN, 0x1EDA86u, CO_RF_JOIN},
        {XH_DISPA, 0x6CED4Cu, CO_RF_DISPATCH}, {XH_DISPA, 0x6CED5Du, -1}, {XH_DISPB, 0x6CED5Du, CO_RF_DISPATCH}, {XH_APPLY, 0x1E9940u, CO_RF_APPLY},
        {XH_CHAIN, 0x10A97Cu, CO_RF_CHAIN}, {XH_GPUWAIT, 0x6C5F43u, CO_RF_GPUWAIT}, {XH_GPUWAIT, 0x6C5F44u, -1}, {XH_DRAIN, 0x6C601Du, CO_RF_DRAIN},
        {XH_DRAIN, 0x6C05BBu, -1}, {XH_LODWAIT, 0x211411u, CO_RF_SCATWAIT}, {XH_JOBWAIT, 0x211411u, -1}};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) CHECK(CoXlTestXrfPartFor(k[i].hook, g_exe + k[i].ret) == k[i].part);
    CHECK(CoXlTestXrfPartFor(XH_JOBWAIT, 0x140000000ull) == -2 && CoXlTestXrfPartFor(XH_JOBWAIT, g_exe + 0x2800000u) == -2);
    CHECK(CoXlTestXrfPartFor(XH_APPLY, 0x140000000ull) == CO_RF_APPLY); /* the apply and the chain spin are timed from any caller */
}

static uint64_t g_msgs, g_inner;
static uint64_t Handler(uint64_t r, uint64_t m, uint64_t p) { (void)r; (void)p; g_msgs++; (void)m; return 7; }
static uint8_t g_msg0b[16], g_msg08[16];
static uint64_t Nested(uint64_t r, uint64_t m, uint64_t p)
{
    g_inner = CoXlTestXrfMessage(r, (uint64_t)(uintptr_t)g_msg08, p, Handler); /* a nested dispatch: timed by the outer */
    return Handler(r, m, p);
}
static DWORD WINAPI OtherThread(void* unused)
{
    (void)unused;
    g_inner = CoXlTestXrfMessage(0, (uint64_t)(uintptr_t)g_msg08, 0, Handler);
    return 0;
}

static void TestDispatcher(void)
{
    Reset("the dispatcher: 0x0B ends the frame, a nested call is timed once, another thread passes through");
    *(uint16_t*)(g_msg0b + 0xC) = 0x0B; *(uint16_t*)(g_msg08 + 0xC) = 0x08;
    g_msgs = 0;
    CHECK(CoXlTestXrfMessage(0, (uint64_t)(uintptr_t)g_msg0b, 0, Handler) == 7 && g_msgs == 1);
    CHECK(CoXlTestXrfRingFill() == 0); /* the first 0x0B starts a frame */
    CHECK(CoXlTestXrfMessage(0, (uint64_t)(uintptr_t)g_msg08, 0, Nested) == 7 && g_inner == 7 && g_msgs == 3);
    CHECK(CoXlTestXrfCurrent()->messages == 2); /* the 0x0B and the 0x08; the nested one is inside the outer's time */
    HANDLE t = CreateThread(NULL, 0, OtherThread, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE); CloseHandle(t);
    CHECK(g_msgs == 4 && CoXlTestXrfCurrent()->messages == 2);
    CHECK(CoXlTestXrfMessage(0, (uint64_t)(uintptr_t)g_msg0b, 0, Handler) == 7 && CoXlTestXrfRingFill() == 1);
    CHECK(CoXlTestXrfCurrent()->messages == 1 && CoXlTestXrfCurrent()->clocks == 3); /* the new frame holds the 0x0B */
}

/* Message 8's nodes: next at +0, the type's offset at +0xE, the type there. */
static uint8_t g_nodes[400][0x20];
static uintptr_t g_slot;
static uint64_t Stream(uint32_t n, const uint32_t* types, uint8_t* stream)
{
    for (uint32_t i = 0; i < n; i++)
    {
        memset(g_nodes[i], 0, sizeof g_nodes[i]);
        *(uintptr_t*)g_nodes[i] = i + 1 < n ? (uintptr_t)g_nodes[i + 1] : 0;
        *(uint16_t*)(g_nodes[i] + 0xE) = 0x18;
        *(uint32_t*)(g_nodes[i] + 0x18) = types[i % 4];
    }
    g_slot = n ? (uintptr_t)g_nodes[0] : 0;
    *(uintptr_t*)(stream + 0x10) = (uintptr_t)&g_slot;
    return (uint64_t)(uintptr_t)stream;
}

static void TestWalk(void)
{
    Reset("message 8's walk: items by type, at most 256 a call, another type in slot 0");
    static uint8_t stream[0x20];
    const uint32_t types[4] = {2, 2, 0x15, 0x40};
    FrameEnd(0);
    CoXlTestXrfWalk(Stream(10, types, stream));
    const CoRfFrame* f = CoXlTestXrfCurrent();
    CHECK(f->types[2] == 6 && f->types[0x15] == 2 && f->types[0] == 2 && f->walked == 10 && f->applies == 1 && f->applyCut == 0);
    CoXlTestXrfWalk(Stream(0, types, stream));
    CHECK(f->walked == 10 && f->applies == 2);
    CoXlTestXrfWalk(Stream(300, types, stream));
    CHECK(f->walked == 10 + CO_RF_WALK && f->applies == 3 && f->applyCut == 1 && f->types[2] == 6 + 128);
    CoXlTestXrfWalk(Stream(256, types, stream)); /* exactly 256: walked whole */
    CHECK(f->applyCut == 1);
    Clock(1000);
    FrameEnd(0);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("render frame counts: message 8 applies 4 (1 walked only to their first 256 items), items by type: 00:"));
}

static void TestSpinAndPresent(void)
{
    Reset("message 8's spin: from the apply's return at 0x1E95A0 to its last round's end; present's Present1 from the drain's end");
    CoXlTestXrfApplied(g_exe + 0x1E95A0u, 100);
    CoXlTestXrfRoundAt(g_exe + 0x1E95F8u, 400);
    CoXlTestXrfRoundAt(g_exe + 0x10BB9Au, 900); /* the other spin's round: not message 8's */
    CoXlTestXrfRoundAt(g_exe + 0x1E95F8u, 700);
    CoXlTestXrfSpinClose();
    const CoRfFrame* f = CoXlTestXrfCurrent();
    CHECK(f->t[CO_RF_SPIN8] == 600 && f->rounds8 == 2);
    CoXlTestXrfRoundAt(g_exe + 0x1E95F8u, 2000); /* closed: a later round is not this spin's */
    CoXlTestXrfSpinClose();
    CHECK(f->t[CO_RF_SPIN8] == 600 && f->rounds8 == 2);
    CoXlTestXrfApplied(g_exe + 0x1E95A0u, 3000); /* no rounds: no spin */
    CoXlTestXrfSpinClose();
    CHECK(f->t[CO_RF_SPIN8] == 600);
    CoXlTestXrfApplied(g_exe + 0x1E9940u, 4000); /* the serial apply has no spin after it */
    CoXlTestXrfRoundAt(g_exe + 0x1E95F8u, 5000);
    CoXlTestXrfSpinClose();
    CHECK(f->t[CO_RF_SPIN8] == 600);
    CoXlTestXrfApplied(g_exe + 0x1E95A0u, 6000); /* a message's end closes it */
    CoXlTestXrfRoundAt(g_exe + 0x1E95F8u, 6500);
    CoXlTestXrfMessageDone(0x08, 0, 7000);
    CHECK(f->t[CO_RF_SPIN8] == 1100 && f->t[CO_RF_M08] == 7000);

    CoXlTestXrfDrained(10000);
    CoXlTestXrfAfterAt(g_exe + 0x6C1FE0u, 11000); /* the other caller of 0x6F0360 */
    CHECK(f->t[CO_RF_PRESENT1] == 0);
    CoXlTestXrfAfterAt(g_exe + 0x6C617Du, 12500);
    CHECK(f->t[CO_RF_PRESENT1] == 2500);
    CoXlTestXrfAfterAt(g_exe + 0x6C617Du, 14000); /* once a drain */
    CHECK(f->t[CO_RF_PRESENT1] == 2500);
}

static void TestRecordsCellsFence(void)
{
    Reset("dispatch records, SpeedTree cells a view from 0x212565 only, the fence on the game thread");
    FrameEnd(0);
    CoXlTestXrfRecords(300); CoXlTestXrfRecords(100);
    const CoRfFrame* f = CoXlTestXrfCurrent();
    CHECK(f->dispatches == 2 && f->records == 400 && f->recordsMost == 300);
    static uint8_t owner[0xA0];
    *(uint64_t*)(owner + 0x90) = 120;
    CoXlTestXrfCellsAt(g_exe + 0x212565u, (uint64_t)(uintptr_t)owner);
    *(uint64_t*)(owner + 0x90) = 80;
    CoXlTestXrfCellsAt(g_exe + 0x212565u, (uint64_t)(uintptr_t)owner);
    CoXlTestXrfCellsAt(g_exe + 0x213062u, (uint64_t)(uintptr_t)owner); /* the other caller: not a view's fill */
    CoXlTestXrfFence(0, 4000); CoXlTestXrfFence(0, 30000);
    for (int i = 0; i < 98; i++) CoXlTestXrfFence(0, 2000);
    Clock(5000);
    FrameEnd(0);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("dispatches 2.00 a frame, draw records a dispatch 200 (most 300)"));
    CHECK(fake_host_logged("render frame world: SpeedTree cells a view 100 (most 120, 2 views)"));
    CHECK(fake_host_logged("frame fence (exe 0x8A7C4, the game thread): 100 waits in 10.0 s, ms mean 2.30, p99 4.00, worst 30.00, 230.0 in all"));
    uint32_t waits; double mean, p99, worst;
    co_rframe_test_fence(&waits, &mean, &p99, &worst);
    CHECK(waits == 100 && CLOSE(mean, 2.3) && CLOSE(p99, 4.0) && CLOSE(worst, 30.0));
    CoXlTestXrfThread(GetCurrentThreadId() + 1); /* the fence is the game thread's: the tick's, not the render thread's */
    CoXlTestXrfFence(0, 4000);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("frame fence (exe 0x8A7C4, the game thread): 1 waits"));
}

static void TestRing(void)
{
    Reset("a full ring drops the render thread's frames and counts them");
    FrameEnd(0);
    for (int i = 0; i < 1100; i++) { Clock(1000); FrameEnd(0); }
    CHECK(CoXlTestXrfRingFill() == 1024 && CoXlTestXrfLost() == 76);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("render frame: 1024 frames in 11.1 s (0 past the 4096 kept, 76 lost to a full ring)"));
}

static void TestPools(void)
{
    Reset("the main world's culling sets: shown [CSO+0x198] and parked [CSO+0x1A0], now and the window's most");
    static uint8_t rw[0x2A0], cso[5][0x1B0];
    for (int k = 0; k < 5; k++)
    {
        *(uint64_t*)(rw + 0x268 + 8 * k) = (uint64_t)(uintptr_t)cso[k];
        *(uint32_t*)(cso[k] + 0x198) = 1000u * (k + 1); *(uint32_t*)(cso[k] + 0x1A0) = 10u * (k + 1);
    }
    CoXlTestXrfPoolsRw((uint64_t)(uintptr_t)rw);
    FrameEnd(0); Clock(5000); FrameEnd(0);
    CoXlTestXrfTick(); /* sampled */
    *(uint32_t*)(cso[0] + 0x198) = 500; *(uint32_t*)(cso[0] + 0x1A0) = 900;
    CoXlTestXrfPoolsRw((uint64_t)(uintptr_t)rw);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("culling sets (main world; shown now/most, parked now/most): 0:500/1000,900/900 1:2000/2000,20/20 2:3000/3000,30/30 3:4000/4000,40/40 4:5000/5000,50/50;"));
    CHECK(fake_host_logged("far window stamps by the nearest player: not read"));
    CoXlTestXrfPoolsRw(0);
    FrameEnd(0);
    Clock(10 * US);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("(main world; shown now/most, parked now/most): not read;"));
}

static void TestStamps(void)
{
    Reset("far window stamps by the nearest player's distance; near window stamps and empty slots left out");
    const float at[7][2] = {{600, 0}, {0, 850}, {-1200, 0}, {2000, 0}, {100, 0}, {0, 0}, {5000, 5000}};
    const float out2[7] = {800.0f * 800.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f}; /* slot 5: a near one (no radius of its own) */
    const uint8_t live[7] = {1, 1, 0, 1, 0, 1, 0};
    const float players[2][2] = {{0, 0}, {5000, 4000}};
    CoXlTestXrfStampsSet(1, 40, 7, at, out2, live, 2, players);
    FrameEnd(0); Clock(5000); FrameEnd(0);
    Clock(10 * US);
    CoXlTestXrfTick();
    /* 600 -> 550-800; 850 -> 800-1100; 1200 -> 1100-1500; 2000 -> over 1500; 100 -> under 550; (5000, 5000) is 1000 m from player 2 */
    CHECK(fake_host_logged("far window stamps by the nearest player: under 550 m 1, 550-800 1, 800-1100 2, 1100-1500 1, over 1500 1 (6 far, 3 live)"));
    CoXlTestXrfStampsSet(0, 40, 0, at, out2, live, 2, players);
}

static void TestStall(void)
{
    Reset("a stall sample: due once a frame past 25 ms; read on the game thread, its first known wait named by return address");
    CHECK(!CoXlTestXrfStallDue(0, 30000, 1));             /* no frame yet */
    CHECK(!CoXlTestXrfStallDue(1000, 26000, 1));          /* 25 ms */
    CHECK(CoXlTestXrfStallDue(1000, 26001, 1));
    CHECK(!CoXlTestXrfStallDue(1000, 26001, 0));          /* this frame was sampled (the watcher's last) */
    uint8_t* x = (uint8_t*)g_exe;
    x[0x1EC4AAu] = 0xE8; x[0x113EEEu] = 0xE8;             /* the calls before the return addresses */
    uint64_t stack[4] = {0x1234, g_exe + 0x1EC4AFu, g_exe + 0x500000u, g_exe + 0x113EF3u};
    CoXlTestXrfStall(0, g_exe + 0x627200u, 31.04, 412.6, stack, sizeof stack);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("render stall sample: the render thread 31.0 ms into its frame at exe+0x627200 (function not found); suspended 413 us; wait: message 7's cull job wait (0x627160 from 0x1EC4AA); stack (32 bytes): exe+0x1EC4AF, exe+0x113EF3"));
    g_fake.log[0] = 0;
    CoXlTestXrfTick();
    CHECK(!fake_host_logged("render stall sample")); /* once */
    x[0x500000u] = 0xE8;                                   /* a call site no wait is known at, before the known one */
    uint64_t stack2[3] = {g_exe + 0x500005u, g_exe + 0x1E95A0u, g_exe + 0x113EF3u};
    x[0x1E959Bu] = 0xE8;
    CoXlTestXrfStall(1, g_exe + 0x58E700u, 40.0, 1800.0, stack2, sizeof stack2);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("suspended 1800 us; wait: message 8's object messages (0x1E84A0 from 0x1E959B); stack (24 bytes): exe+0x500005, exe+0x1E95A0, exe+0x113EF3"));
    CHECK(co_rframe_wait_name(0x6C5F43u) != NULL && co_rframe_wait_name(0x113F22u) != NULL && co_rframe_wait_name(0x1EC4AAu) == NULL);
}

static void TestInstall(void)
{
    g_case = "the install on the fake exe: every hook on its pins; a prologue or call site not the reviewed code refuses that hook alone";
    const char *in, *refused, *off;
    g_coApi = fake_host_reset(CO_GAME_BUILD); g_coMod = (HfMod*)&g_fake; g_exe = (uintptr_t)fake_host_exe();
    CHECK(CoXlTestXrfInstall(&in, &refused, &off) == 1 && off == NULL);
    CHECK(strcmp(refused, "") == 0 && strstr(in, "job wait 0x627160, LOD wait 0x6097A0 (+9), SpeedTree view wait 0x213540 (+9)") != NULL && strstr(in, "render messages 0x113010") != NULL);
    CHECK(g_fake.hookCount == XH_COUNT);
    for (int h = 0; h < XH_COUNT; h++) CHECK(CoXlTestXrfOrig(h) != NULL);
    const uintptr_t image = (uintptr_t)g_fake.image; /* the fake records a hook's target from the image */
    CHECK(g_fake.hooks[XH_LODWAIT].target + image - g_exe == 0x6097A9u && g_fake.hooks[XH_TREES].target + image - g_exe == 0x213549u);
    CHECK(g_fake.hooks[XH_JOBWAIT].target + image - g_exe == 0x627160u && g_fake.hooks[XH_JOIN].target + image - g_exe == 0x252460u);
    CHECK(CoXlTestXrfTimed() == ((1u << CO_RF_TIMED) - 1u & ~(1u << CO_RF_SPIN8))); /* every part but the rounds' */
    g_coApi = fake_host_reset(CO_GAME_BUILD); g_coMod = (HfMod*)&g_fake; g_exe = (uintptr_t)fake_host_exe();
    g_fake.noMatchExe[0] = 0x1E3E64u;                 /* the LOD wait's call site */
    g_fake.noMatchExe[1] = 0x6C0590u;                 /* the GPU wait's prologue */
    CHECK(CoXlTestXrfInstall(&in, &refused, &off) == 1);
    CHECK(strcmp(refused, "LOD wait 0x6097A0 (+9) (call site), GPU wait 0x6C0590 (prologue)") == 0 && strstr(in, "LOD wait") == NULL);
    CHECK(CoXlTestXrfOrig(XH_LODWAIT) == NULL && CoXlTestXrfOrig(XH_GPUWAIT) == NULL && CoXlTestXrfOrig(XH_JOBWAIT) != NULL);
    CHECK(g_fake.hookCount == XH_COUNT - 2);
    const uint32_t t = CoXlTestXrfTimed();
    CHECK(!(t >> CO_RF_LODB & 1) && !(t >> CO_RF_GPUWAIT & 1) && (t >> CO_RF_CULL & 1) && (t >> CO_RF_M07 & 1));
    CoXlTestXrfTick();
    CHECK(fake_host_logged("render frame instrument (research): hooks in: job wait 0x627160, SpeedTree view wait 0x213540 (+9), generator join"));
    CHECK(fake_host_logged("refused: LOD wait 0x6097A0 (+9) (call site), GPU wait 0x6C0590 (prologue); through other hooks: render messages yes (its own 0x113010 hook), the frame fence (0x8A7C4) not yet"));
    g_fake.log[0] = 0;
    CoXlTestXrfTick();
    CHECK(!fake_host_logged("render frame instrument")); /* once, until what is timed changes */
    g_fake.noMatchExe[0] = g_fake.noMatchExe[1] = 0;
    g_coApi = fake_host_reset(CO_GAME_BUILD); g_coMod = (HfMod*)&g_fake; g_exe = (uintptr_t)fake_host_exe();
    g_fake.noMatchExe[0] = 0x2136AFu;                 /* the ret the SpeedTree view wait's skipped lead-in jumps to */
    CHECK(CoXlTestXrfInstall(&in, &refused, &off) == 1);
    CHECK(strcmp(refused, "SpeedTree view wait 0x213540 (+9) (prologue)") == 0 && CoXlTestXrfOrig(XH_TREES) == NULL && g_fake.hookCount == XH_COUNT - 1);
    g_coApi = fake_host_reset(CO_GAME_BUILD); g_coMod = (HfMod*)&g_fake; g_exe = (uintptr_t)fake_host_exe();
    g_fake.noMatchExe[0] = 0x6098FDu;                 /* the LOD wait's */
    CHECK(CoXlTestXrfInstall(&in, &refused, &off) == 1 && strcmp(refused, "LOD wait 0x6097A0 (+9) (prologue)") == 0);
    g_fake.noMatchExe[0] = 0;
}

/* Message 8's spin with the device's chain count: rounds counted without the clock, the clock read after the last. */
static LONG g_seenInPart = -1;
static uint64_t OrigSeesPart(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { (void)a; (void)b; (void)c; (void)d; g_seenInPart = CoXlTestXrfInPart(); return 7; }
static void TestSpinRounds(void)
{
    Reset("message 8's spin rounds: the return address first, no clock until the round after which the chain count is 0");
    static uint8_t dev[0x400];
    uint32_t pending = 3, none = 0;
    memcpy(dev + 0x3D8, &pending, 4);
    const CoRfFrame* f = CoXlTestXrfCurrent();
    CoXlTestXrfApplied(g_exe + 0x1E95A0u, 100);
    Clock(200);
    const uint32_t reads = CoXlTestXrfReads();
    CoXlTestXrfRound(g_exe + 0x10BB9Au, (uintptr_t)dev);  /* another caller of 0x528210: nothing */
    CoXlTestXrfRound(g_exe + 0x1E95F8u, (uintptr_t)dev);  /* the chain still has work: counted, no clock */
    CHECK(f->rounds8 == 1 && f->clocks == 0 && CoXlTestXrfReads() == reads);
    Clock(300);
    memcpy(dev + 0x3D8, &none, 4);
    CoXlTestXrfRound(g_exe + 0x1E95F8u, (uintptr_t)dev);  /* the last: the clock at its end */
    CHECK(CoXlTestXrfReads() == reads + 1);
    Clock(5000);
    CoXlTestXrfMessageDone(0x08, 0, g_now);
    CHECK(f->rounds8 == 2 && f->t[CO_RF_SPIN8] == g_now - 5000 - 100 && f->spinEndsAtClose == 0 && f->clocks == 2);
    /* a last round not seen as the last (the count changed after the read): the spin closes at the next event */
    CoXlTestXrfApplied(g_exe + 0x1E95A0u, g_now);
    CoXlTestXrfRoundPending(g_exe + 0x1E95F8u);
    Clock(700);
    CoXlTestXrfMessageDone(0x08, 0, g_now);
    CHECK(f->rounds8 == 3 && f->spinEndsAtClose == 1 && f->t[CO_RF_SPIN8] == 1400 + 700); /* 100 to the last round at 1500, then 700 */

    Reset("a timed call: the stall watcher sees the render thread inside a part while it runs, outside after");
    CoXlTestXrfSetOrig(XH_DISPA, (void*)OrigSeesPart);
    g_seenInPart = -1;
    CHECK(CoXlTestXrfDispatchAt(g_exe + 0x6CED4Cu, 5) == 7 && g_seenInPart == 1 && CoXlTestXrfInPart() == 0);
    g_seenInPart = -1;
    CHECK(CoXlTestXrfDispatchAt(g_exe + 0x6CED5Du, 5) == 7 && g_seenInPart == 0); /* not its call site: not timed */
}

/* Render frames with and without a far-grass bake view: what one adds, and its work, in the window's lines. */
static void TestBake(void)
{
    Reset("bake views: their frames against the others, the added ms, frames over 25 ms, cells, instances, wrapper time");
    FrameEnd(0);
    for (int i = 0; i < 200; i++)
    {
        const int bake = i % 10 == 0;           /* 20 frames with a bake view, 2 of them 30 ms */
        if (bake) { CoXlTestXrfBake(20, 1200, 30000, 400); CoXlTestXrfAdd(CO_RF_SCATWAIT, 0, 2000); }
        Msg(0x07, bake ? (i % 100 == 0 ? 29000 : 11000) : 9000);
        Clock(1000);
        FrameEnd(0);
    }
    Clock(10 * US);
    CoXlTestXrfTick();
    uint32_t frames = 0, over = 0; double added = 0;
    co_rframe_test_bake(&frames, &over, &added);
    /* with: 18 x 12 + 2 x 30 = 276 over 20; without: 10 */
    CHECK(frames == 20 && over == 2 && CLOSE(added, 276.0 / 20 - 10.0));
    CHECK(fake_host_logged("render frame bake views: 20 of 200 frames ran a far-grass bake view; period ms mean/p99/worst with one 13.80/30.00/30.00, without 10.00/10.00/10.00; "
        "a bake view adds 3.80 ms (OVER the goal of 3.0 ms); frames over 25 ms: 2 with a bake view, 0 without; a bake view: cells 1200 (most 1200), instances 30000 "
        "(most 30000), the mod's wrappers 400 us (most 400) over its threads, scatter graph wait (0x21140C) 2.00 ms (most 2.00)"));
    CHECK(fake_host_logged(", bake 1.00, bakeCells 1200.00, bakeInst 30000.00, bakeCoUs 400.00"));
    CHECK(LinesShort());
    /* a window without bake views has no such line */
    g_fake.log[0] = 0;
    for (int i = 0; i < 10; i++) { Msg(0x07, 9000); Clock(1000); FrameEnd(0); }
    Clock(10 * US);
    CoXlTestXrfTick();
    co_rframe_test_bake(&frames, &over, &added);
    CHECK(frames == 0 && !fake_host_logged("render frame bake views"));
    CHECK(co_rframe_wait_name(0x211411u) != NULL && strstr(co_rframe_wait_name(0x211411u), "0x21140C") != NULL);
}

/* The stall sample's trail keeps game.dll's return addresses with the exe's. */
static void TestStallGame(void)
{
    Reset("a stall sample's stack: game.dll's frames in the trail beside the exe's");
    uint8_t* x = (uint8_t*)g_exe;
    const uintptr_t game = (uintptr_t)g_fake.image;
    CoXlTestGame(game);
    x[0x1EC4AAu] = 0xE8; ((uint8_t*)game)[0x1000] = 0xE8;
    uint64_t stack[3] = {g_exe + 0x1EC4AFu, game + 0x1005u, 0x42};
    CoXlTestXrfStall(0, g_exe + 0x627200u, 30.0, 100.0, stack, sizeof stack);
    CoXlTestXrfTick();
    CHECK(fake_host_logged("stack (24 bytes): exe+0x1EC4AF, game+0x1005"));
    CoXlTestGame(0);
}

int main(void)
{
    TestSelect();
    TestWindow();
    TestUntimedAndGpu();
    TestPartFor();
    TestDispatcher();
    TestWalk();
    TestSpinAndPresent();
    TestRecordsCellsFence();
    TestRing();
    TestPools();
    TestStamps();
    TestStall();
    TestSpinRounds();
    TestBake();
    TestStallGame();
    TestInstall();
    if (g_failed) { printf("rframe: %d check(s) failed\n", g_failed); return 1; }
    printf("rframe: all checks passed\n");
    return 0;
}
