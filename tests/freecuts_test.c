/* Memory given back once nothing reads it: a tile's band pixels 4 frame kicks after its texture's create, op 0x2B's job
 * regions at the op thread's next dispatcher call, and the render job contexts' emptied version copies once the table is
 * short, in play. Each goes once, never early, never twice; a refused pin keeps it. Run on the fake game and exe. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_render_job_bytes.h"

void CoXlTestWorldCurrent(float current);
void CoXlTestLive(int complex);
void CoXlTestRenderCopies(uintptr_t dev, void (*resize)(uintptr_t, uint32_t));
uint64_t CoXlTestPlaceJob(uint64_t job, uint64_t (*original)(uint64_t, uint64_t, uint64_t, uint64_t));
int CoXlTestPlaceRegions(void);
void CoXlTestPlaceDone(void);
void CoXlTestPlaceFreeTry(void);
void CoXlTestPlaceJobsIn(LONG delta);
void CoXlTestBandTile(int slot, uint32_t* pixels, uint32_t made);
uint32_t* CoXlTestBandTilePixels(int slot);
void CoXlTestBandDrop(uint64_t tex, uint32_t* pixels, uint32_t made, int stashed);
uint32_t* CoXlTestBandDropPixels(void);
void CoXlTestBandTick(void);
int CoXlTestRenderFrame(uint32_t* frame);
void CoXlTestBandLevelGone(void);
void CoXlTestGame(uintptr_t game);
void CoXlTestMission(uintptr_t level, int complex);
void CoXlTestDispatchBegins(uint64_t records, uint64_t index);
void CoXlTestBandBuild(uint64_t tex, uint32_t* pixels, uint32_t made);
void CoXlTestBandTileIn(int slot);
void CoXlTestRenderFramePins(void);
void CoXlTestRenderAfter(uintptr_t state, void (*rehash)(uintptr_t, uint32_t), void (*resize)(uintptr_t, uint32_t));
uintptr_t CoXlTestRenderCopiesPins(uintptr_t exe);
void CoXlTestRenderJob(uintptr_t ret, uintptr_t after, uintptr_t job, void (*resize)(uintptr_t, uint32_t));
void CoXlTestRenderJobHooked(void* original);
int CoXlTestRenderJobGate(void);
uint64_t CoXlTestRenderRecord(uint64_t job);
int CoXlTestRenderJobPins(uintptr_t exe);
void* CoXlTestRenderJobsInstall(uintptr_t exe, uintptr_t resize);
int CoXlTestRenderInstall(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define APP 0x1A10208u

/* The renderer as the exe holds it: app [exe+0x1A10208], r [app+0x338], render thread byte [r+0x1480], kicks [r+0x1A50]. */
static uint8_t g_app[0x400], g_r[0x2000];
static void Frame(uint32_t kicks) { memcpy(g_r + 0x1A50, &kicks, 4); }

static int Gone(const void* p)
{
    MEMORY_BASIC_INFORMATION mi;
    return VirtualQuery(p, &mi, sizeof mi) != 0 && mi.State == MEM_FREE;
}
static uint32_t* Pixels(void) { return (uint32_t*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }

/* The engine's resize 0x6D55E0 as the cut calls it: to 0, the block freed, cap 0. */
static uint32_t g_resized;
static void TestResize(uintptr_t array, uint32_t n)
{
    g_resized++;
    if (n == 0) { memset((void*)(array + 4), 0, 4); memset((void*)(array + 8), 0, 8); if (*(uint32_t*)array > 0) memset((void*)array, 0, 4); }
}
static void Context(uint8_t* x, uint32_t count, uint32_t cap, uint32_t used)
{
    static uint8_t block[16];
    uint64_t data = (uint64_t)(uintptr_t)block;
    memset(x, 0, 0x1A0);
    memcpy(x + 8, &count, 4); memcpy(x + 0xC, &cap, 4); memcpy(x + 0x10, &data, 8); memcpy(x + 0x20, &used, 4);
}
static uint32_t Cap(const uint8_t* x) { uint32_t c; memcpy(&c, x + 0xC, 4); return c; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
/* The recording 0x6FD1B0 as the hook calls it: its 7 arguments seen, its result handed back. */
/* The recording as the hooked game calls it, through three callers built in the fake exe: the copy it found, and its
 * 7 arguments. A caller saves 7 arguments' frame, calls 0x6FD1B0 from its real call site and returns from the site after. */
static uint8_t* g_driveX;
static uint32_t g_capSeen;
static uint64_t g_seen[7];
static uint64_t TestRecordSeen(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g)
{
    const uint64_t got[7] = {a, b, c, d, e, f, g};
    memcpy(g_seen, got, sizeof got);
    g_capSeen = Cap(g_driveX);
    return 0x5EC0;
}
static void Caller(uint8_t* exe, uint32_t call)
{
    static const uint8_t frame[] = {0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x44, 0x24, 0x70, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x8B, 0x44, 0x24, 0x78,
        0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8B, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x30};
    static const uint8_t back[] = {0x48, 0x83, 0xC4, 0x48, 0xC3};
    const int32_t rel = (int32_t)(0x6FD1B0 - (call + 5));
    memset(exe + call - 0x2C, 0x90, 0x2C);
    memcpy(exe + call - 0x2C, frame, sizeof frame);
    exe[call] = 0xE8; memcpy(exe + call + 1, &rel, 4); memcpy(exe + call + 5, back, sizeof back);
}
typedef uint64_t (*Caller7)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static int Drive(uint8_t* exe, uint32_t call, uint8_t* job)
{
    memset(g_seen, 0, sizeof g_seen); g_capSeen = 0;
    const uint64_t r = ((Caller7)(void*)(exe + call - 0x2C))(0x11, 0x22, (uint64_t)(uintptr_t)job, 0x44, 0x55, 0x66, 0x77);
    const uint64_t want[7] = {0x11, 0x22, (uint64_t)(uintptr_t)job, 0x44, 0x55, 0x66, 0x77};
    return r == 0x5EC0 && memcmp(g_seen, want, sizeof want) == 0;
}
static uint64_t g_recordArgs[7];
static uint64_t TestRecord(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g)
{
    const uint64_t got[7] = {a, b, c, d, e, f, g};
    memcpy(g_recordArgs, got, sizeof got);
    return 99;
}

static uint64_t TestJob(uint64_t a, uint64_t b, uint64_t c, uint64_t job) { (void)a; (void)b; (void)c; (void)job; return 1; }
/* One job of a 0x2B op over 800 items, its range starting at `first`: the region it is handed. */
static void PlaceOne(uint32_t first)
{
    static uint32_t items = 800;
    static uint8_t context[0x200], job[0x80];
    uint64_t list = (uint64_t)(uintptr_t)&items, ctx = (uint64_t)(uintptr_t)context;
    uint32_t range[2] = {first, first + 100};
    memcpy(context + 0x170, &list, 8); memcpy(job + 0x10, &ctx, 8); memcpy(job + 0x4C, range, 8);
    CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
}
static DWORD WINAPI OtherThreadFree(void* unused) { (void)unused; CoXlTestPlaceFreeTry(); return 0; }
/* The rehash 0x6F60F0 as the shrink calls it: the table's size becomes about the buckets asked for. */
static void TestRehash(uintptr_t map, uint32_t buckets) { uint32_t size = buckets + buckets / 3; memcpy((void*)map, &size, 4); memcpy((void*)(map + 0x1C), &buckets, 4); }
static void Pin(uintptr_t at) { g_fake.noMatch[0] = at - (uintptr_t)g_fake.image; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    const uint64_t app = (uint64_t)(uintptr_t)g_app, r = (uint64_t)(uintptr_t)g_r;
    memcpy(exe + APP, &app, 8); memcpy(g_app + 0x338, &r, 8);
    g_r[0x1480] = 1;
    Frame(100);
    /* a mission in play: the engine's state 4, the level live (state 3) */
    uint8_t* gameObj = (uint8_t*)VirtualAlloc(NULL, 0xB0000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    static uint8_t level[0x200];
    const uint64_t engine = (uint64_t)(uintptr_t)gameObj;
    const uint32_t four = 4, three = 3;
    memcpy(g_fake.image + 0x3326340u, &engine, 8); memcpy(gameObj + 0xAC21Cu, &four, 4); memcpy(level + 0x1E4, &three, 4);
    CoXlTestGame((uintptr_t)g_fake.image);
    CoXlTestMission((uintptr_t)level, 1);

    g_case = "the render frame";
    {
        uint32_t f = 0;
        CHECK(CoXlTestRenderFrame(&f) && f == 100);
        g_r[0x1480] = 0;
        CHECK(!CoXlTestRenderFrame(&f)); /* no render thread: the create's path is not this one */
        g_r[0x1480] = 1;
    }

    g_case = "a moving tile's band pixels";
    {
        uint32_t* p = Pixels();
        CoXlTestBandTile(1, p, 100);
        for (uint32_t k = 100; k < 104; k++) { Frame(k); CoXlTestBandTick(); }
        CHECK(CoXlTestBandTilePixels(1) == p && !Gone(p)); /* 3 kicks: the upload may still read them */
        Frame(104); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(1) == NULL && Gone(p));
        CHECK(fake_host_logged("difficulty: moving tiles: the band pixels of cell (0, 0) given back (64 MiB, 1 this level)"));
        Frame(110); CoXlTestBandTick(); /* once only */
        CHECK(CoXlTestBandTilePixels(1) == NULL);
        CoXlTestBandLevelGone(); /* the level's end finds nothing left to free */
        CHECK(CoXlTestBandTilePixels(1) == NULL);
    }

    g_case = "the frame counter wraps";
    {
        uint32_t* p = Pixels();
        CoXlTestBandTile(2, p, 0xFFFFFFFEu);
        Frame(1); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(2) == p);
        Frame(2); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(2) == NULL && Gone(p));
    }

    g_case = "no render thread: the pixels stay with the tile";
    {
        uint32_t* p = Pixels();
        CoXlTestBandTile(3, p, 200);
        g_r[0x1480] = 0;
        Frame(300); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(3) == p && !Gone(p));
        g_r[0x1480] = 1; /* the frame read again: the kicks since the create count */
        Frame(301); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(3) == NULL && Gone(p));
    }

    g_case = "the drop tile's band pixels";
    {
        uint32_t* p = Pixels();
        CoXlTestBandDrop(0x1234, p, 400, 1);
        Frame(500); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == p && !Gone(p)); /* a build holds them aside: g_matband is the new tile's */
        CoXlTestBandDrop(0x1234, p, 400, 0);
        Frame(403); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == p);
        Frame(404); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == NULL && Gone(p));
        CHECK(fake_host_logged("difficulty: moving tiles: the drop tile's band pixels given back"));
        Frame(500); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == NULL);
    }

    g_case = "op 0x2B's job regions";
    {
        uint32_t items = 800;
        uint64_t list = (uint64_t)(uintptr_t)&items;
        uint8_t context[0x200] = {0}, job[0x80] = {0};
        memcpy(context + 0x170, &list, 8);
        const uint64_t ctx = (uint64_t)(uintptr_t)context;
        memcpy(job + 0x10, &ctx, 8);
        CoXlTestWorldCurrent(8.0f);
        for (uint32_t k = 0; k < 8; k++)
        {
            uint32_t range[2] = {k * 100, k * 100 + 100};
            memcpy(job + 0x4C, range, 8);
            CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
        }
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceFreeTry(); /* no op has ended: they stay */
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceJobsIn(1); /* a job's note still reads its region */
        CoXlTestPlaceDone();
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceJobsIn(-1);
        CoXlTestPlaceFreeTry(); /* the next dispatcher call */
        CHECK(CoXlTestPlaceRegions() == 0);
        CHECK(fake_host_logged("difficulty: extra large maps: op 0x2B's 8 job regions given back after its merge (1024 MiB committed)"));
        CoXlTestPlaceFreeTry();
        CoXlTestPlaceDone(); /* once only */
        CHECK(CoXlTestPlaceRegions() == 0);
        uint32_t range[2] = {0, 100}; /* a later 0x2B makes its region again */
        memcpy(job + 0x4C, range, 8);
        CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
        CHECK(CoXlTestPlaceRegions() == 1);
        CoXlTestPlaceDone();
        CHECK(CoXlTestPlaceRegions() == 0);
        CoXlTestWorldCurrent(1.0f);
    }

    g_case = "the render job contexts' version copies";
    {
        static uint8_t dev[0x1500], x[4][0x1A0];
        uint64_t array[5] = {(uint64_t)(uintptr_t)x[0], (uint64_t)(uintptr_t)x[1], (uint64_t)(uintptr_t)x[2], 0, (uint64_t)(uintptr_t)x[3]};
        uint64_t at = (uint64_t)(uintptr_t)array;
        uint32_t count = 5, size = 2000;
        memcpy(dev + 0x700, &count, 4); memcpy(dev + 0x708, &at, 8); memcpy(dev + 0x13C8, &size, 4);
        Context(x[0], 56114, 56114, 0);  /* the load's copy, emptied: goes */
        Context(x[1], 56114, 56114, 3);  /* a copy not emptied: stays */
        Context(x[2], 6000, 6000, 0);    /* not 4 times the table: stays */
        Context(x[3], 4096, 4096, 0);    /* short: stays */
        const uintptr_t d = (uintptr_t)dev;
        CoXlTestLive(0);
        CoXlTestRenderCopies(d, TestResize); /* a normal mission: the game's */
        CHECK(g_resized == 0 && Cap(x[0]) == 56114);
        CoXlTestLive(1);
        CoXlTestRenderCopies(d, TestResize);
        CHECK(g_resized == 1 && Cap(x[0]) == 0 && Cap(x[1]) == 56114 && Cap(x[2]) == 6000 && Cap(x[3]) == 4096);
        CHECK(fake_host_logged("living world: render job contexts: 1 of 5 version copies given back (1.3 MB; table 2000 entries); 1 copies, 1.3 MB in all"));
        CoXlTestRenderCopies(d, TestResize); /* once only */
        CHECK(g_resized == 1);
        size = 0; memcpy(dev + 0x13C8, &size, 4);
        Context(x[1], 56114, 56114, 0); /* emptied later, and the table emptied too: both long copies go */
        CoXlTestRenderCopies(d, TestResize);
        CHECK(g_resized == 3 && Cap(x[1]) == 0 && Cap(x[2]) == 0 && Cap(x[3]) == 4096);
        CoXlTestLive(0);
    }

    g_case = "a render frame pin refused";
    {
        uint32_t f = 0;
        Pin((uintptr_t)exe + 0x10A81Cu); CoXlTestRenderFramePins();
        CHECK(!CoXlTestRenderFrame(&f)); /* the kick is not the reviewed code: no frame, the pixels stay */
        g_fake.noMatch[0] = 0; CoXlTestRenderFramePins();
        CHECK(CoXlTestRenderFrame(&f));
    }

    g_case = "a built tile takes the build's frame, the drop tile gets its own back";
    {
        uint32_t* drop = Pixels(), *built = Pixels();
        CoXlTestBandDrop(0x11, drop, 1000, 0);
        CoXlTestBandBuild(0x22, built, 1200); /* the build's texture, made later */
        CoXlTestBandTileIn(4);
        Frame(1203); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(4) == built && !Gone(built)); /* 3 kicks after its own create */
        CHECK(CoXlTestBandDropPixels() == NULL && Gone(drop));
        Frame(1204); CoXlTestBandTick();
        CHECK(CoXlTestBandTilePixels(4) == NULL && Gone(built));
        uint32_t* drop2 = Pixels(), *built2 = Pixels();
        CoXlTestBandDrop(0x33, drop2, 1300, 0);
        CoXlTestBandBuild(0x44, built2, 1250); /* a build whose texture is older than the drop's */
        CoXlTestBandTileIn(5);
        Frame(1303); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == drop2 && !Gone(drop2)); /* the drop's own frame came back with it */
        CHECK(CoXlTestBandTilePixels(5) == NULL && Gone(built2));
        Frame(1304); CoXlTestBandTick();
        CHECK(CoXlTestBandDropPixels() == NULL && Gone(drop2));
    }

    g_case = "op 0x2B's regions: the dispatcher's calls";
    {
        static uint8_t records[3 * 0x19B0];
        const uint32_t ops[3] = {0x2B, 0x2B, 0x10};
        for (int k = 0; k < 3; k++) memcpy(records + k * 0x19B0, &ops[k], 4);
        uint32_t at = 0;
        const uint64_t rec = (uint64_t)(uintptr_t)records, index = (uint64_t)(uintptr_t)&at;
        CoXlTestWorldCurrent(8.0f);
        PlaceOne(0); PlaceOne(400);
        CoXlTestDispatchBegins(rec, index); /* the 0x2B record's call */
        CHECK(CoXlTestPlaceRegions() == 2);
        at = 1; CoXlTestDispatchBegins(rec, index); /* another 0x2B record: its op uses them */
        CHECK(CoXlTestPlaceRegions() == 2);
        HANDLE t = CreateThread(NULL, 0, OtherThreadFree, NULL, 0, NULL); /* another thread's call: not the op's */
        if (t != NULL) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
        CHECK(CoXlTestPlaceRegions() == 2);
        at = 2; CoXlTestDispatchBegins(rec, index); /* the next op on the op's thread */
        CHECK(CoXlTestPlaceRegions() == 0);
        PlaceOne(0);
        CoXlTestWorldCurrent(1.0f);
        at = 0; CoXlTestDispatchBegins(rec, index); /* a 0x2B on a normal level marks nothing */
        at = 2; CoXlTestDispatchBegins(rec, index);
        CHECK(CoXlTestPlaceRegions() == 1);
        CoXlTestWorldCurrent(8.0f);
        at = 0; CoXlTestDispatchBegins(rec, index);
        at = 2; CoXlTestDispatchBegins(rec, index);
        CHECK(CoXlTestPlaceRegions() == 0);
        CoXlTestWorldCurrent(1.0f);
    }

    g_case = "the version copies after the shrink, in play only";
    {
        static uint8_t dev[0x1500], x[0x1A0];
        uint64_t array[1] = {(uint64_t)(uintptr_t)x}, at = (uint64_t)(uintptr_t)array;
        uint32_t count = 1, size = 56114, used = 100, buckets = 40959;
        memcpy(dev + 0x700, &count, 4); memcpy(dev + 0x708, &at, 8);
        memcpy(dev + 0x13C8, &size, 4); memcpy(dev + 0x13C8 + 0x18, &used, 4); memcpy(dev + 0x13C8 + 0x1C, &buckets, 4);
        Context(x, 56114, 56114, 0);
        g_resized = 0;
        CoXlTestLive(1);
        memcpy(level + 0x1E4, &(uint32_t){1}, 4); /* the level still loads */
        CoXlTestRenderAfter((uintptr_t)dev + 0x1340, TestRehash, TestResize);
        CHECK(g_resized == 0 && Cap(x) == 56114);
        memcpy(level + 0x1E4, &three, 4);
        memcpy(dev + 0x13C8, &size, 4); memcpy(dev + 0x13C8 + 0x1C, &buckets, 4);
        CoXlTestRenderAfter((uintptr_t)dev + 0x1340, TestRehash, TestResize); /* the shrink first, then the copy against it */
        CHECK(g_resized == 1 && Cap(x) == 0);
        CoXlTestLive(0);
    }

    g_case = "a version copies pin refused";
    {
        CHECK(CoXlTestRenderCopiesPins((uintptr_t)exe) == (uintptr_t)exe + 0x6D55E0u);
        Pin((uintptr_t)exe + 0x6E2230u); /* the reader that tests used first */
        CHECK(CoXlTestRenderCopiesPins((uintptr_t)exe) == 0);
        Pin((uintptr_t)exe + 0x6D55E0u);
        CHECK(CoXlTestRenderCopiesPins((uintptr_t)exe) == 0);
        g_fake.noMatch[0] = 0;
    }

    g_case = "a job's version copy at the job's end";
    {
        static uint8_t dev[0x1500], x[0x1A0], job[0x400];
        uint64_t array[1] = {(uint64_t)(uintptr_t)x}, at = (uint64_t)(uintptr_t)array, xp = (uint64_t)(uintptr_t)x, pool = (uint64_t)(uintptr_t)dev + 0x1340;
        uint32_t count = 1, size = 56114, used = 30000, buckets = 40959, one = 1;
        const uintptr_t after = 0x6CA321u, d = (uintptr_t)dev, j = (uintptr_t)job;
        memcpy(dev + 0x700, &count, 4); memcpy(dev + 0x708, &at, 8); memcpy(job + 0x3B0, &xp, 8);
        memcpy(dev + 0x13C8, &size, 4); memcpy(dev + 0x13C8 + 0x18, &used, 4); memcpy(dev + 0x13C8 + 0x1C, &buckets, 4); /* a burst's table: not shrunk */
        Context(x, 56114, 56114, 3); memcpy(x, &pool, 8); /* the copy the job read */
        g_resized = 0;
        CoXlTestLive(1);
        memcpy(level + 0x1E4, &three, 4);
        CoXlTestRenderJobHooked(NULL);
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
        CHECK(!CoXlTestRenderJobGate()); /* the recording not hooked: the copies wait for the dispatch */
        CoXlTestRenderJob(after, after, j, TestResize);
        CHECK(g_resized == 0 && Cap(x) == 56114);
        CoXlTestRenderJobHooked((void*)TestRecord);
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
        CHECK(CoXlTestRenderJobGate() && g_resized == 0 && Cap(x) == 56114); /* a table in use: the dispatch frees nothing */
        CoXlTestRenderJob(0x6CCBADu, after, j, TestResize); /* the dispatch's own call: its context is the next job's */
        CHECK(g_resized == 0 && Cap(x) == 56114);
        CoXlTestRenderJob(after, after, j, TestResize);
        CHECK(g_resized == 1 && Cap(x) == 0 && U32(x + 8) == 0 && U32(x + 0x20) == 0 && U32(x + 0x28) == 0 && U32(x + 0x2C) == 0xFFFFFFFFu);
        CoXlTestRenderJob(after, after, j, TestResize); /* once only */
        CHECK(g_resized == 1);
#ifdef CO_RESEARCH
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
        CHECK(fake_host_logged("living world: render dispatch: table 56114 entries, 30000 used, 40959 buckets; changed ids 0 entries, 0 used; 1 job copies over "
                               "4096 entries (1.3 MB, largest 56114); at most 1 contexts held one at once (1.3 MB), 1 of them not yet emptied"));
#else
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
#endif
        CHECK(fake_host_logged("living world: render job contexts: 1 version copies given back at their jobs' ends (1.3 MB, largest 56114 entries); 1 copies, 1.3 MB in all"));
        Context(x, 4096, 4096, 3); memcpy(x, &pool, 8);
        CoXlTestRenderJob(after, after, j, TestResize); /* short: stays */
        CHECK(g_resized == 1 && Cap(x) == 4096);
        Context(x, 56114, 56114, 3); memcpy(x, &pool, 8);
        CHECK(CoXlTestRenderRecord(j) == 99 && g_recordArgs[2] == j && g_recordArgs[0] == 1 && g_recordArgs[6] == 7);
        CHECK(g_resized == 1 && Cap(x) == 56114); /* the hook's own caller is not the job's call */
        memcpy(level + 0x1E4, &one, 4); /* the level still loads */
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
        CoXlTestRenderJob(after, after, j, TestResize);
        CHECK(!CoXlTestRenderJobGate() && g_resized == 1 && Cap(x) == 56114);
        memcpy(level + 0x1E4, &three, 4);
        CoXlTestLive(0); /* a normal mission */
        CoXlTestRenderAfter(d + 0x1340, TestRehash, TestResize);
        CoXlTestRenderJob(after, after, j, TestResize);
        CHECK(!CoXlTestRenderJobGate() && g_resized == 1 && Cap(x) == 56114);
        CoXlTestRenderJobHooked(NULL);
    }

    g_case = "a job pin refused";
    {
        const uintptr_t e = (uintptr_t)exe, resize = e + 0x6D55E0u;
        CHECK(CoXlTestRenderJobPins(e));
        CHECK(CoXlTestRenderJobsInstall(e, 0) == NULL); /* the copies' own pins refused: no job frees either */
        Pin(e + 0x6CA321u); /* the job's tail after the recording */
        CHECK(!CoXlTestRenderJobPins(e) && CoXlTestRenderJobsInstall(e, resize) == NULL);
        Pin(e + 0x6CBE0Bu); /* the job waits for its update pass */
        CHECK(!CoXlTestRenderJobPins(e));
        Pin(e + 0x6FD1B0u);
        CHECK(!CoXlTestRenderJobPins(e));
        g_fake.noMatch[0] = 0;
        CHECK(CoXlTestRenderJobsInstall(e, resize) != NULL && fake_host_logged("living world: render job contexts: a version copy over 4096 entries is given back at its job's end, in play"));
        CoXlTestRenderJobHooked(NULL);
    }

    g_case = "the job pins cover every byte the free relies on";
    {
        const uintptr_t e = (uintptr_t)exe;
        const size_t jobs = sizeof kRenderJobBytes / sizeof kRenderJobBytes[0], copies = sizeof kRenderCopiesBytes / sizeof kRenderCopiesBytes[0];
        for (size_t i = 0; i < jobs; i++) memcpy(exe + kRenderJobBytes[i].rva, kRenderJobBytes[i].bytes, kRenderJobBytes[i].size);
        for (size_t i = 0; i < copies; i++) memcpy(exe + kRenderCopiesBytes[i].rva, kRenderCopiesBytes[i].bytes, kRenderCopiesBytes[i].size);
        g_fake.strictCode = 1;
        CHECK(CoXlTestRenderJobPins(e) && CoXlTestRenderCopiesPins(e) == e + 0x6D55E0u);
        uint32_t missed = 0, first = 0;
        for (size_t i = 0; i < jobs; i++)
            for (uint32_t k = 0; k < kRenderJobBytes[i].size; k++)
            {
                uint8_t* b = exe + kRenderJobBytes[i].rva + k;
                *b ^= 0xFF;
                if (CoXlTestRenderJobPins(e) && missed++ == 0) first = kRenderJobBytes[i].rva + k;
                *b ^= 0xFF;
            }
        for (size_t i = 0; i < copies; i++)
            for (uint32_t k = 0; k < kRenderCopiesBytes[i].size; k++)
            {
                uint8_t* b = exe + kRenderCopiesBytes[i].rva + k;
                *b ^= 0xFF;
                if (CoXlTestRenderCopiesPins(e) != 0 && missed++ == 0) first = kRenderCopiesBytes[i].rva + k;
                *b ^= 0xFF;
            }
        if (missed) printf("  a changed byte the pins accept: %u, the first at exe+0x%X\n", missed, first);
        CHECK(missed == 0);
    }

    g_case = "the recording's three callers";
    {
        static uint8_t dev[0x1500], x[0x1A0], job[0x400];
        const uintptr_t e = (uintptr_t)exe;
        uint64_t array[1] = {(uint64_t)(uintptr_t)x}, at = (uint64_t)(uintptr_t)array, xp = (uint64_t)(uintptr_t)x, pool = (uint64_t)(uintptr_t)dev + 0x1340;
        uint32_t count = 1, size = 56114, used = 30000, buckets = 40959;
        memcpy(dev + 0x700, &count, 4); memcpy(dev + 0x708, &at, 8); memcpy(job + 0x3B0, &xp, 8);
        memcpy(dev + 0x13C8, &size, 4); memcpy(dev + 0x13C8 + 0x18, &used, 4); memcpy(dev + 0x13C8 + 0x1C, &buckets, 4);
        g_fake.originals[0].target = e + 0x6FD1B0u; g_fake.originals[0].original = (void*)TestRecordSeen;
        CHECK(CoXlTestRenderJobsInstall(e, (uintptr_t)TestResize) == (void*)TestRecordSeen); /* the reviewed bytes, strict */
        g_fake.strictCode = 0;
        void* detour = fake_host_detour(e + 0x6FD1B0u - (uintptr_t)g_fake.image);
        CHECK(detour != NULL);
        DWORD old;
        CHECK(VirtualProtect(exe + 0x6CA000u, 0x34000, PAGE_EXECUTE_READWRITE, &old));
        static const uint8_t jump[2] = {0xFF, 0xE0};
        exe[0x6FD1B0] = 0x48; exe[0x6FD1B1] = 0xB8; memcpy(exe + 0x6FD1B2, &detour, 8); memcpy(exe + 0x6FD1BA, jump, 2); /* the hook's jump */
        Caller(exe, 0x6CA31Cu); Caller(exe, 0x6CAE2Bu); Caller(exe, 0x6CCBA8u);
        FlushInstructionCache(GetCurrentProcess(), exe + 0x6CA000u, 0x34000);
        g_driveX = x;
        Context(x, 56114, 56114, 3); memcpy(x, &pool, 8);
        CoXlTestLive(1);
        memcpy(level + 0x1E4, &three, 4);
        CoXlTestRenderAfter((uintptr_t)dev + 0x1340, TestRehash, TestResize);
        CHECK(CoXlTestRenderJobGate());
        g_resized = 0;
        CHECK(detour != NULL && Drive(exe, 0x6CAE2Bu, job)); /* the serial path (return 0x6CAE30) */
        CHECK(g_capSeen == 56114 && g_resized == 0 && Cap(x) == 56114);
        CHECK(detour != NULL && Drive(exe, 0x6CCBA8u, job)); /* the dispatch's own call (return 0x6CCBAD) */
        CHECK(g_capSeen == 56114 && g_resized == 0 && Cap(x) == 56114);
        CHECK(detour != NULL && Drive(exe, 0x6CA31Cu, job)); /* the job (return 0x6CA321): the copy stood through the recording */
        CHECK(g_capSeen == 56114 && g_resized == 1 && Cap(x) == 0 && U32(x + 0x20) == 0);
        CoXlTestRenderAfter((uintptr_t)dev + 0x1340, TestRehash, TestResize);
        CoXlTestLive(0);
        CoXlTestRenderJobHooked(NULL);
        g_fake.originals[0].target = 0; g_fake.originals[0].original = NULL;
    }

    g_case = "the render install hooks the recording";
    CHECK(CoXlTestRenderInstall());
    CoXlTestRenderJobHooked(NULL);

    if (g_failed) { printf("freecuts: %d check(s) failed\n", g_failed); return 1; }
    printf("freecuts: all checks passed\n");
    return 0;
}
