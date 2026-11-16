/* A tile built in mission: op 0x63 renders the tile's stamp terrains in ranges, one a frame (proto/difficulty_world_move.c
 * Split63*). The stamps below range 0 are hidden before the op queues its job ([[api+0x110]+0x20], 0x3486C0); the
 * game's consumer renders the job with range 0 shown; then each frame hides range k, shows range k+1 and renders the
 * job's copy again (0x318090, then render_world through the wrapper), from the highest location down; after the last
 * range every stamp is shown again. A stamp gone is never touched (its id's live check first). Any check that fails
 * before the hides renders whole; one after them shows every stamp again in the same frame. A build that fails shows
 * them again before 0x65; a level leave leaves them to the teardown. No range renders after either. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMove2FPhaseB(uint8_t* pass, uint64_t context);
void CoXlTestSplitL2(uint32_t* out, const uint32_t** ids);
void CoXlTestMoveRun2FDone(uint32_t count);
void CoXlTestSplitIds(const uint32_t* ids, uint32_t m, uint32_t extra, int idsOk);
void CoXlTestSplitHooks(void* consume, void* run, uint64_t camera);
int CoXlTestSplitPrepare(uint32_t queued);
int CoXlTestSplitQueued(uint64_t context);
int CoXlTestSplitLeave(void);
void CoXlTestSplitFail(const char* why);
int CoXlTestSplitRestore(void);
void CoXlTestSplitLeft(const char* why);
void CoXlTestSplitBuildLeft(int ship);
int CoXlTestSplitMoveFail(uint8_t* records);
int CoXlTestSplitState(uint32_t* k, uint32_t* K, uint32_t* hidden, int* pending);
void CoXlTestNearStampLeave(int on);
uint64_t CoXlTestNearRunAt(int consumer, uint64_t a, uint64_t camera, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h);
int CoXlTestNearRunStep63(int s, uint8_t* records, uint64_t context, void* dispatch, int first);
int CoXlTestNearStampLeaving(void);
void CoXlTestNearConsume(uint64_t queue, int replays, void* original);
void CoXlTestNearConsumeQueue(uint64_t queue, void* original);
void CoXlTestNearReplaying(int active);
void CoXlTestMoveBuilding(int on);
void CoXlTestNearJobLevel(uintptr_t level, uint8_t* block);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { SP_NONE, SP_WHOLE, SP_HIDING, SP_READY, SP_QUEUED, SP_RANGES, SP_SHOWING, SP_DONE, SP_RESTORE };
#define N 2000u
#define IDS 0x1000u
#define OBJS 0x50000000ull
#define WORLD 0xAB0ull
#define CAM 0xCA30ull
#define C10 0xC10ull
#define E18 0xE18ull

static uint8_t g_visible[N], g_dead[N], g_events[200000];
static int g_eventN, g_toggles, g_callToggles, g_showDead, g_badArgs, g_renders, g_flushBad, g_consumed;
static uint32_t g_drawn[N * 4], g_drawnN, g_size[64];
static float g_posY[16]; static int g_posN;
static uint8_t g_queue[0x18 + 8 * 0x1D0], g_ctx[0xE8];
static const float kRect[4] = {1.0f, 2.0f, 3.0f, 4.0f};

static void Event(uint8_t e) { if (g_eventN < (int)sizeof g_events) g_events[g_eventN++] = e; }
static void Spin(double us) { LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0); do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1e6 / (double)hz.QuadPart < us); }

static char Live(uint32_t id) { return id >= IDS && id < IDS + N && !g_dead[id - IDS]; }
static uint64_t Obj(uint32_t id, uint32_t zero) { (void)zero; return OBJS + id; }
static void Show(uint64_t obj, uint32_t show)
{
    const uint32_t x = (uint32_t)(obj - OBJS) - IDS;
    if (x >= N) return;
    if (g_dead[x]) g_showDead++;
    g_visible[x] = show != 0; g_toggles++; g_callToggles++;
    Event('T');
    Spin(2.0);
}
static void Flush(uint64_t world) { if (world != WORLD) g_badArgs++; Event('F'); }
/* The render thread's cull of rw+0x268 with the hides applied: every shown stamp, the highest location first. */
static uint64_t Render(uint64_t a, uint64_t camera, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    if (a != WORLD || camera != CAM || c != C10 || e != E18 || f != 0 || g != 0x10000u || h != 0 || memcmp((const void*)(uintptr_t)d, kRect, 16) != 0) g_badArgs++;
    if (g_eventN == 0 || g_events[g_eventN - 1] != 'F') g_flushBad++;
    Event('R');
    uint32_t n = 0;
    for (uint32_t x = N; x-- > 0;)
        if (g_visible[x] && !g_dead[x]) { if (g_drawnN < N * 4) g_drawn[g_drawnN++] = x; n++; }
    if (g_renders < 64) g_size[g_renders] = n;
    g_renders++;
    return 0;
}
/* The game's consumer (0xAB0590): each record copied to its stack, a render-path one flushed and rendered, the count 0. */
static void Consume(uint64_t queue)
{
    uint32_t n = 0; memcpy(&n, (void*)(uintptr_t)(queue + 0x10), 4);
    for (uint32_t i = 0; i < n; i++)
    {
        __attribute__((aligned(16))) uint8_t copy[0x1D0];
        memcpy(copy, (void*)(uintptr_t)(queue + 0x18 + (uint64_t)i * 0x1D0), sizeof copy);
        uint32_t kind; memcpy(&kind, copy + 0x68, 4);
        if (kind != 0xFFFFFFFFu) continue;
        uint64_t a, cam, c, e; uint32_t flags;
        memcpy(&a, copy, 8); memcpy(&cam, copy + 8, 8); memcpy(&c, copy + 0x10, 8); memcpy(&e, copy + 0x18, 8); memcpy(&flags, copy + 0x20, 4);
        Flush(a);
        CoXlTestNearRunAt(1, a, cam, c, (uint64_t)(uintptr_t)(copy + 0x58), e, 0, flags | 0x10000u, 0);
        g_consumed++;
    }
    const uint32_t zero = 0; memcpy((void*)(uintptr_t)(queue + 0x10), &zero, 4);
}
static uint64_t Queue(void) { return (uint64_t)(uintptr_t)g_queue; }
static uint32_t Queued(void) { uint32_t n; memcpy(&n, g_queue + 0x10, 4); return n; }
/* Op 0x63's push as 0xC178B1 / 0xC14DCE write it. */
static void Push(uint64_t camera)
{
    uint32_t n = Queued();
    uint8_t* r = g_queue + 0x18 + (size_t)n * 0x1D0;
    memset(r, 0x5A, 0x1D0); /* what an older job left there */
    uint64_t v = WORLD; memcpy(r, &v, 8); memcpy(r + 8, &camera, 8); v = C10; memcpy(r + 0x10, &v, 8); v = E18; memcpy(r + 0x18, &v, 8);
    memcpy(r + 0x58, kRect, 16); r[0x24] = 0; r[0x38] = r[0x39] = 0;
    const uint32_t zero = 0, all = 0xFFFFFFFFu; memcpy(r + 0x20, &zero, 4); memcpy(r + 0x68, &all, 4);
    n++; memcpy(g_queue + 0x10, &n, 4);
}
static void Frame(void) { CoXlTestNearConsume(Queue(), 1, (void*)Consume); } /* the render callback's tail call */

static int g_index;
static uint64_t Dispatch63(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    (void)record; (void)context; (void)d; (void)phase; (void)f; (void)g; (void)h;
    Push(CAM); g_index++;
    *(volatile uint32_t*)(uintptr_t)index = 1;
    return 0;
}
static void Nop(void) {}
/* Phase B's calls: [api+0x50]+0x30 (0x32B8E0) unit 0x900 + i's terrain id at an index; the depth index each object got. */
static uint32_t g_testUnitId[6][4], g_depth[8];
static uint32_t UnitId(uint64_t unit, uint32_t index) { return unit >= 0x900 && unit < 0x906 && index < 4 ? g_testUnitId[unit - 0x900][index] : 0; }
static uint64_t WorldOf(uint64_t world) { return world == WORLD; }
static void Depth(uint64_t obj, uint32_t index) { const uint32_t id = (uint32_t)(obj - OBJS); if (id == IDS + 10) g_depth[0] = index; if (id == IDS + 20) g_depth[1] = index; if (id == IDS + 30) g_depth[2] = index; }
static uint64_t Material(uint64_t obj, uint32_t hash) { (void)obj; return hash == 0xF2760503u ? 0x77 : 0; }
static void Position(uint64_t camera, uint64_t node, const float* at) { (void)camera; (void)node; if (g_posN < 16) g_posY[g_posN++] = at[1]; Event('P'); }

static void Reset(uint32_t m, uint32_t extra, int idsOk)
{
    static uint32_t ids[N];
    for (uint32_t x = 0; x < N; x++) { ids[x] = IDS + x; g_visible[x] = 1; g_dead[x] = 0; }
    CoXlTestSplitIds(ids, m, extra, idsOk);
    for (uint32_t x = m; x < N; x++) g_visible[x] = 0; /* not this build's */
    memset(g_queue, 0, sizeof g_queue);
    g_eventN = g_toggles = g_showDead = g_badArgs = g_renders = g_flushBad = g_consumed = 0; g_drawnN = 0; g_posN = 0;
    memset(g_size, 0, sizeof g_size);
    g_fake.log[0] = 0;
    CoXlTestSplitHooks((void*)Consume, (void*)Render, CAM);
    CoXlTestNearReplaying(1); CoXlTestMoveBuilding(1); CoXlTestNearStampLeave(1);
    Frame(); /* the replay's queue known to the hook */
}
static int State(void) { uint32_t k, K, h; int p; return CoXlTestSplitState(&k, &K, &h, &p); }
static uint32_t Hidden(void) { uint32_t k, K, h; int p; CoXlTestSplitState(&k, &K, &h, &p); return h; }
static int Pending(void) { uint32_t k, K, h; int p; CoXlTestSplitState(&k, &K, &h, &p); return p; }
/* Every live stamp of the build drawn once, from the highest location down. */
static int Descending(uint32_t m)
{
    uint32_t want = 0, at = 0;
    for (uint32_t x = m; x-- > 0;) { if (g_dead[x]) continue; if (at >= g_drawnN || g_drawn[at] != x) return 0; at++; want++; }
    return at == g_drawnN && want == g_drawnN;
}
static int Prepare(int* frames, int* maxToggles)
{
    int r = 0; *frames = 0; *maxToggles = 0;
    while (*frames < 1000) { g_callToggles = 0; r = CoXlTestSplitPrepare(Queued()); (*frames)++; if (g_callToggles > *maxToggles) *maxToggles = g_callToggles; if (r != 0) break; }
    return r;
}
/* MoveTick's leave, then the frame's consumer, until the leave is done: its result. */
static int Leave(int* frames, int* maxToggles)
{
    int r = 0; *frames = 0;
    while (*frames < 3000) { g_callToggles = 0; r = CoXlTestSplitLeave(); (*frames)++; if (g_callToggles > *maxToggles) *maxToggles = g_callToggles; if (r != 0) break; Frame(); }
    return r;
}

/* MoveTick's leave until the next range's toggles are all posted (its render due), no consumer between. */
static int Advance(void)
{
    int r = 0, f = 0;
    while (f++ < 100 && (r = CoXlTestSplitLeave()) == 0 && !Pending()) {}
    return r == 0 && Pending();
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    static uint64_t api[0x30], t08[0x40], t10[0x40], t18[0x100], t20[0x40], t28[2], t50[0x10], t110[0x10];
    t18[0x720 / 8] = (uint64_t)(uintptr_t)Live; t18[0x460 / 8] = (uint64_t)(uintptr_t)Obj; t110[0x20 / 8] = (uint64_t)(uintptr_t)Show;
    t10[0x30 / 8] = (uint64_t)(uintptr_t)Flush;
    t50[0x30 / 8] = (uint64_t)(uintptr_t)UnitId; t08[0x48 / 8] = (uint64_t)(uintptr_t)WorldOf; t18[0x450 / 8] = (uint64_t)(uintptr_t)Nop;
    t110[0x18 / 8] = (uint64_t)(uintptr_t)Depth; t110[0x10 / 8] = (uint64_t)(uintptr_t)Material; t28[0] = (uint64_t)(uintptr_t)Nop;
    t20[0x98 / 8] = (uint64_t)(uintptr_t)Nop; t20[0x20 / 8] = (uint64_t)(uintptr_t)Position; t08[0x100 / 8] = (uint64_t)(uintptr_t)Nop;
    api[0x08 / 8] = (uint64_t)(uintptr_t)t08; api[0x10 / 8] = (uint64_t)(uintptr_t)t10; api[0x18 / 8] = (uint64_t)(uintptr_t)t18;
    api[0x20 / 8] = (uint64_t)(uintptr_t)t20; api[0x28 / 8] = (uint64_t)(uintptr_t)t28; api[0x50 / 8] = (uint64_t)(uintptr_t)t50;
    api[0x110 / 8] = (uint64_t)(uintptr_t)t110;
    uint64_t apiAt = (uint64_t)(uintptr_t)api; memcpy(g_fake.image + 0x3326308u, &apiAt, 8);
    uint64_t v = WORLD; memcpy(g_ctx + 0x40, &v, 8); v = Queue(); memcpy(g_ctx + 0x50, &v, 8);
    const uint64_t ctx = (uint64_t)(uintptr_t)g_ctx;
    int frames = 0, maxToggles = 0;

    g_case = "split: the stamps below range 0 hidden over frames, at most a frame's budget of toggles a call";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1 && frames >= 3 && maxToggles <= 700 && State() == SP_READY);
    CHECK(fake_host_logged("difficulty: moving tiles: 0x63 split: on: 2000 stamps in 4 ranges of 545 (5.5 us a stamp, 3.0 ms a range)"));
    {
        int ok = 1;
        for (uint32_t x = 0; x < N; x++) if (g_visible[x] != (x >= N - 545)) ok = 0;
        CHECK(ok && Hidden() == N - 545 && g_renders == 0);
    }
    g_case = "split: the op's job checked and kept; the consumer renders range 0, then each range's copy a frame, top down, once each";
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0 && State() == SP_QUEUED);
    Frame();
    CHECK(g_renders == 1 && g_size[0] == 545);
    maxToggles = 0;
    CHECK(Leave(&frames, &maxToggles) == 1 && State() == SP_DONE);
    CHECK(g_renders == 4 && g_size[1] == 545 && g_size[2] == 545 && g_size[3] == 365 && Descending(N));
    CHECK(g_badArgs == 0 && g_flushBad == 0 && maxToggles <= 700);
    {
        int all = 1;
        for (uint32_t x = 0; x < N; x++) if (!g_visible[x]) all = 0;
        CHECK(all && Hidden() == 0);
    }
    CHECK(fake_host_logged("difficulty: moving tiles: 0x63 split: 4 ranges of 545 stamps, toggles "));
    CHECK(fake_host_logged(" ms (frame max ") && fake_host_logged(" ms, 3090 shows, 3090 hides, 0 skipped dead), renders "));
    CHECK(fake_host_logged(" ms (range max ") && fake_host_logged(" ms, first ") && fake_host_logged(" ms), frames "));
    Frame(); Frame();
    CHECK(g_renders == 4 && CoXlTestSplitLeave() == 1); /* nothing again */

    g_case = "split: a stamp gone mid-split is never shown, hidden or drawn again, and counted";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0);
    g_dead[1460] = 1; g_dead[1000] = 1; g_dead[100] = 1; /* shown in range 0; hidden in ranges 1 and 3 */
    Frame();
    CHECK(Leave(&frames, &maxToggles) == 1 && g_renders == 4 && g_showDead == 0 && Descending(N) && Hidden() == 0);
    CHECK(fake_host_logged(", 3 skipped dead), renders "));

    g_case = "whole: extra live terrain ids at the locations: no hide at all, one render of everything";
    Reset(N, 3, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1 && frames == 1 && g_toggles == 0 && State() == SP_WHOLE);
    CHECK(fake_host_logged("difficulty: moving tiles: 0x63 split: off: 3 extra live terrain ids at its locations would draw in every range; the stamps render whole in one frame"));
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0 && State() == SP_WHOLE);
    Frame();
    CHECK(Leave(&frames, &maxToggles) == 1 && frames == 1 && g_renders == 1 && g_size[0] == N && Descending(N));

    g_case = "whole: phase B's ids not kept";
    Reset(N, 0, 0);
    CHECK(Prepare(&frames, &maxToggles) == 1 && g_toggles == 0 && State() == SP_WHOLE);
    CHECK(fake_host_logged("0x63 split: off: op 0x2F's last part did not run in the mod, so its terrain ids were not kept"));

    g_case = "whole: the stamps fit one range";
    Reset(500, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1 && g_toggles == 0 && State() == SP_WHOLE);
    CHECK(fake_host_logged("0x63 split: off: its 500 stamps fit one range"));

    g_case = "whole after the hides: the job's camera is not the merge camera: every stamp shown before the consumer";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1 && Hidden() == N - 545);
    Push(CAM + 8);
    CHECK(CoXlTestSplitQueued(ctx) == 0 && State() == SP_WHOLE && Hidden() == 0);
    CHECK(fake_host_logged("0x63 split: off after the hides: its job's camera is not the merge camera; 1455 stamps shown again in this frame ("));
    CHECK(fake_host_logged("), the stamps render whole in one frame"));
    Frame();
    CHECK(Leave(&frames, &maxToggles) == 1 && g_renders == 1 && g_size[0] == N);

    g_case = "whole after the hides: the op queued two jobs";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM); Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0 && State() == SP_WHOLE && Hidden() == 0);
    CHECK(fake_host_logged("off after the hides: the op queued 2 jobs, not one"));

    g_case = "whole after the hides: the job is not a plain render";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM); g_queue[0x18 + 0x38] = 1;
    CHECK(CoXlTestSplitQueued(ctx) == 0 && State() == SP_WHOLE && Hidden() == 0);
    CHECK(fake_host_logged("off after the hides: its job is not a plain render (+0x68 FFFFFFFF, +0x24 0, +0x38 1, +0x39 0)"));

    g_case = "cut short mid-split (a failed build): no range renders again; the stamps shown again over frames before 0x65";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0);
    Frame();
    CHECK(Advance()); Frame(); /* range 1 rendered */
    CHECK(Advance() && g_renders == 2); /* range 2's toggles posted, its render due */
    g_fake.log[0] = 0;
    CoXlTestNearReplaying(0);
    {
        static uint8_t records65[0x19B0];
        CHECK(CoXlTestSplitMoveFail(records65)); /* MoveFail: the split cut short, 0x65 next in MV_FAILING */
    }
    CoXlTestNearReplaying(1);
    CHECK(!Pending() && State() == SP_RESTORE && fake_host_logged("0x63 split: cut short in range 3 of 4 (the replay failed): 1455 stamps hidden, shown again before 0x65"));
    Frame(); Frame();
    CHECK(g_renders == 2);
    {
        int r = 0, f = 0; maxToggles = 0;
        while (f < 100 && (g_callToggles = 0, r = CoXlTestSplitRestore()) == 0) { f++; if (g_callToggles > maxToggles) maxToggles = g_callToggles; }
        int all = 1;
        for (uint32_t x = 0; x < N; x++) if (!g_visible[x]) all = 0;
        CHECK(r == 1 && f >= 1 && maxToggles <= 700 && all && Hidden() == 0 && State() == SP_DONE && CoXlTestSplitRestore() == 1);
    }

    g_case = "a level leave mid-split: nothing shown, the state dropped, no range renders again";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0);
    Frame();
    CHECK(Advance());
    g_fake.log[0] = 0;
    {
        const int toggles = g_toggles;
        CoXlTestSplitBuildLeft(0);
        CHECK(fake_host_logged("0x63 split: a level init in range 2 of 4: 1455 stamps stay hidden; they go with the build's units"));
        CHECK(g_toggles == toggles && State() == SP_NONE && !Pending());
        Frame(); Frame();
        CHECK(g_renders == 1 && CoXlTestSplitLeave() == 1 && CoXlTestSplitRestore() == 1);
    }

    g_case = "the replay only while the build's replay waits in 0x63";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0);
    Frame();
    CHECK(Advance());
    {
        static uint8_t other[0x18]; /* the render callback's other consumer call (0xAB7D4F) */
        CoXlTestNearConsumeQueue((uint64_t)(uintptr_t)other, (void*)Consume);
        CHECK(g_renders == 1 && Pending());
    }
    CoXlTestNearReplaying(0); Frame(); CHECK(g_renders == 1 && Pending());
    CoXlTestNearReplaying(1); CoXlTestNearStampLeave(0); Frame(); CHECK(g_renders == 1 && Pending());
    CoXlTestNearStampLeave(1); Frame(); CHECK(g_renders == 2 && !Pending());

    g_case = "a range the consumer never renders: the build fails after 600 frames";
    Reset(N, 0, 1);
    CHECK(Prepare(&frames, &maxToggles) == 1);
    Push(CAM);
    CHECK(CoXlTestSplitQueued(ctx) == 0);
    Frame();
    {
        int r = 0, f = 0;
        CHECK(Advance());
        while (f < 1000 && (r = CoXlTestSplitLeave()) == 0) f++;
        CHECK(r == -1 && f == 599 && fake_host_logged("0x63 split: range 2 of 4 was not rendered in 600 frames; the build fails"));
    }

    g_case = "phase B: each drawn location's first live id kept in order; the extra live ids counted (L2)";
    {
        static uint8_t pass[0x60], locs[6 * 0xE0];
        static uint32_t table[6 * 3], count = 6;
        static const uint32_t e[6][3] = {{0, 1, 0}, {0xFFFFFFFFu, 1, 0}, {0, 3, 0}, {0, 2, 1}, {0, 0, 0}, {0, 4, 0}};
        memcpy(table, e, sizeof table);
        for (uint64_t i = 0; i < 6; i++) { uint64_t unit = 0x900 + i; memcpy(locs + i * 0xE0 + 0xA8, &unit, 8); }
        uint64_t p = (uint64_t)(uintptr_t)locs; memcpy(pass + 0x08, &p, 8);
        p = (uint64_t)(uintptr_t)&count; memcpy(pass + 0x28, &p, 8);
        p = (uint64_t)(uintptr_t)table; memcpy(pass + 0x50, &p, 8);
        /* unit i's ids by index: 0 none; ids >= IDS + N are dead */
        static const uint32_t ids[6][4] = {{IDS + 10, 0, 0, 0}, {0, 0, 0, 0}, {IDS + N + 1, IDS + 20, IDS + 21, 0}, {0, IDS + 30, IDS + 31, 0}, {0, 0, 0, 0}, {0, IDS + N + 2, IDS + N + 3, IDS + N + 4}};
        memcpy(g_testUnitId, ids, sizeof ids);
        for (uint32_t x = 0; x < N; x++) g_dead[x] = 0;
        CoXlTestMove2FPhaseB(pass, ctx);
        uint32_t out[6]; const uint32_t* kept = NULL;
        CoXlTestSplitL2(out, &kept);
        CHECK(out[0] == 4 && out[1] == 3 && out[2] == 2 && out[3] == 1);
        CHECK(out[4] == 3 && kept[0] == IDS + 10 && kept[1] == IDS + 20 && kept[2] == IDS + 30 && out[5] == 1);
        CHECK(g_depth[0] == 1 && g_depth[1] == 3 && g_depth[2] == 4); /* the game's own calls as before */
        g_fake.log[0] = 0;
        CoXlTestMoveRun2FDone(6);
        CHECK(fake_host_logged("difficulty: moving tiles: 0x63 terrains: 4 locations, 3 with e[1] > 1, 2 extra live ids (most at one location 1)"));
    }

    g_case = "the replay's 0x63 step: hides, the op, range 0, the ranges, every stamp shown, the cameras back only after the last render";
    {
        static uint8_t level[0x400], gen[0x10], records[0x19B0];
        uint64_t p = (uint64_t)(uintptr_t)gen; memcpy(level + 0x2C8, &p, 8);
        CoXlTestNearJobLevel((uintptr_t)level, NULL);
        const uint32_t op = 0x63; memcpy(records, &op, 4);
        Reset(N, 0, 1);
        int r = CoXlTestNearRunStep63(0, records, ctx, (void*)Dispatch63, 1), f = 1, prepFrames = 0;
        while (r == 0 && f < 4000)
        {
            if (!CoXlTestNearStampLeaving() && g_index == 0) prepFrames++;
            Frame();
            r = CoXlTestNearRunStep63(0, records, ctx, (void*)Dispatch63, 0);
            f++;
        }
        int firstR = -1, lastR = -1, firstP = -1, lastP = -1;
        for (int i = 0; i < g_eventN; i++)
        {
            if (g_events[i] == 'R') { if (firstR < 0) firstR = i; lastR = i; }
            if (g_events[i] == 'P') { if (firstP < 0) firstP = i; lastP = i; }
        }
        CHECK(r == 1 && g_index == 1 && prepFrames >= 2 && g_renders == 4 && Descending(N) && Hidden() == 0 && !CoXlTestNearStampLeaving());
        CHECK(g_posN == 4 && g_posY[0] == 20000.0f && g_posY[1] == 20000.0f && g_posY[2] == 0.0f && g_posY[3] == 0.0f);
        CHECK(firstP >= 0 && firstP < firstR && lastP > lastR && g_events[lastP - 1] == 'P' && g_events[lastP - 2] == 'T');
        CHECK(fake_host_logged("op 0x63's render (the job consumer's once its op had run): 4 calls"));
        CHECK(fake_host_logged("difficulty: moving tiles: 0x63 split: 4 ranges of 545 stamps"));
    }

    printf("%s\n", g_failed ? "split63: FAILED" : "split63: passed");
    return g_failed ? 1 : 0;
}
