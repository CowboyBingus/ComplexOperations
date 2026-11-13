/* Memory given back once nothing reads it: a spawned tile's band pixels after the render thread's upload (4 frame kicks
 * after the texture's create), and op 0x2B's job regions after the op returns. Each goes once, never twice, and not
 * while a reader may still be there. Run on the fake game and exe. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestWorldCurrent(float current);
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

static uint64_t TestJob(uint64_t a, uint64_t b, uint64_t c, uint64_t job) { (void)a; (void)b; (void)c; (void)job; return 1; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    const uint64_t app = (uint64_t)(uintptr_t)g_app, r = (uint64_t)(uintptr_t)g_r;
    memcpy(exe + APP, &app, 8); memcpy(g_app + 0x338, &r, 8);
    g_r[0x1480] = 1;
    Frame(100);

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

    if (g_failed) { printf("freecuts: %d check(s) failed\n", g_failed); return 1; }
    printf("freecuts: all checks passed\n");
    return 0;
}
