/* Moving tiles at their expiry, also while another tile builds, and rpmalloc's trim after tile events: a release during a
 * build only of a tile clear of every merge camera, the keep time refreshed while building, one release a frame; the
 * trim once a burst of events is 8 frames past, outside a build, only on the reviewed code. Run on the fake game and exe. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMission(uintptr_t level, int complex);
void CoXlTestMover(float ox, float oy, int state, int merge, int a, int b);
void CoXlTestMoverTile(int slot, int a, int b, ULONGLONG seen);
void CoXlTestMoverPlayer(float x, float y);
int CoXlTestMoverLive(int slot);
ULONGLONG CoXlTestMoverSeen(int slot);
int CoXlTestMoverRelease(ULONGLONG now);
int CoXlTestMoverState(void);
void CoXlTestMoverTick(uintptr_t level, float observerX, float observerY);
int CoXlTestTrimPins(void);
uintptr_t CoXlTestTrimCall(void);
void CoXlTestTrimUse(void (*trim)(uint64_t), void (*stats)(void*));
void CoXlTestTrimEvent(const char* why);
int CoXlTestTrimTick(void);
void CoXlTestMoverSet(int state);
void CoXlTestMoverTileIn(int slot, int a, int b);
void CoXlTestMoverNear(int state);
uint32_t CoXlTestMoverFrames(void);
void CoXlTestMoverWorld(uint64_t world);
void CoXlTestMoverTileHandle(int slot, uint64_t handle);
int CoXlTestDrainPins(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { MV_IDLE, MV_PACKAGE, MV_STAMPS, MV_KICK, MV_SETUP, MV_LOOKUP, MV_REPLAY, MV_TEARDOWN, MV_UNSTAMP, MV_SPAWN, MV_READY, MV_FAILING };
enum { NEAR_IDLE, NEAR_START, NEAR_STEP, NEAR_DRAIN, NEAR_READBACK, NEAR_SPAWN, NEAR_READY, NEAR_IN };
#define CELL 2048.44f
#define KEEP_MS 30000ull
/* mp-city2's grid: the drop tile's cell (0, 0) at (2, 2), next to the world origin; one case moves it */
#define OX 2.0f
#define OY 2.0f

/* rpmalloc as the trim and the statistics leaf see it: mapped bytes, the global cache, the calls. */
static LONG64 g_mapped, g_global;
static int g_trims;
static void TestTrim(uint64_t unused) { (void)unused; g_trims++; g_mapped -= 300ll << 20; g_global = 0; }
static void TestStats(void* out) { LONG64 s[7] = {g_mapped, g_mapped, g_global, 0, 0, 0, 0}; memcpy(out, s, sizeof s); }

/* The destroy's render fence as the reviewed exe has it: the bytes pinned, and the world, render world, renderer and
 * device it reads. The renderer starts busy: its thread a buffer behind the game's, with 3 messages queued there. */
static const struct { uint32_t at; const char* bytes; } kDrain[] = {
    {0x1ADF8Bu, "49 8B 8E F8 01 23 00 48 8B D5 E8 E6 B4 F1 FF 49 8B 86 F8 01 23 00 B2 01 48 8B 18 48 8B CB E8 92 7E F5 FF 8B D0 48 8B CB E8 78 80 F5 FF"},
    {0x105E47u, "80 B9 80 14 00 00 00"},
    {0x105F62u, "8B B3 98 13 00 00 8B D6 8B 84 B3 80 13 00 00 41 3B C7 74 EC 8D 48 01"},
    {0x10A850u, "B8 FF FF FF FF F0 45 0F B1 A4 BF 80 13 00 00 41 87 BF 98 13 00 00"},
    {0x113F4Au, "44 8B 83 9C 13 00 00 B8 AB AA AA AA 41 8D 68 01 F7 E5 C1 EA 02 8D 04 52 03 C0 2B E8"},
    {0x113EC0u, "8B 83 9C 13 00 00 48 83 C0 62"},
    {0x113EF3u, "8B 83 9C 13 00 00 48 8D 0C 83 41 8B C7 F0 0F C1 81 80 13 00 00"},
    {0x1139A4u, "E6 32 11 00"},
    {0x1132E6u, "49 8B 18 80 7B 48 00 74 08 49 8B CE E8 39 88 FF FF"},
    {0x10BB44u, "48 8B B9 68 11 00 00 48 8B E9 33 DB 48 8D B7 C8 03 00 00 48 8B CE FF 15 00 AC 2E 01 8B BF D8 03 00 00"},
};
static uint8_t g_world[0x230200], g_rw[0x10], g_renderer[0x1C00], g_device[0x400], g_api[0x20], g_table[0x200];
static uint64_t g_destroyWorld, g_destroyHandle;
static void TestDestroy(uint64_t world, uint64_t handle) { g_destroyWorld = world; g_destroyHandle = handle; }

static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
/* The render thread in buffer `render`, the game's `game`, `queued` messages there, `jobs` device jobs. */
static void Render(uint32_t render, uint32_t game, uint32_t queued, uint32_t jobs)
{
    Put32(g_renderer + 0x139C, render); Put32(g_renderer + 0x1398, game); Put32(g_renderer + 0x1380 + 4 * game, queued); Put32(g_device + 0x3D8, jobs);
}
static void DrainWorld(uint64_t destroy)
{
    const uint64_t rw = (uint64_t)(uintptr_t)g_rw, r = (uint64_t)(uintptr_t)g_renderer, dev = (uint64_t)(uintptr_t)g_device;
    const uint64_t api = (uint64_t)(uintptr_t)g_api, table = (uint64_t)(uintptr_t)g_table;
    memcpy(g_world + 0x2301F8u, &rw, 8); memcpy(g_rw, &r, 8); memcpy(g_renderer + 0x1168, &dev, 8);
    g_renderer[0x1480] = 1;
    memcpy(g_fake.image + 0x3326308u, &api, 8); memcpy(g_api + 8, &table, 8); memcpy(g_table + 0x1E0, &destroy, 8);
    CoXlTestMoverWorld((uint64_t)(uintptr_t)g_world);
    Render(1, 2, 3, 0);
}

static void Hex(uint8_t* at, const char* hex)
{
    for (const char* c = hex; *c;)
    {
        if (*c == ' ') { c++; continue; }
        unsigned v = 0;
        sscanf(c, "%2x", &v);
        *at++ = (uint8_t)v;
        c += 2;
    }
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    static uint8_t level[0x200];
    const uint32_t three = 3;
    memcpy(level + 0x1E4, &three, 4);
    CoXlTestGame((uintptr_t)g_fake.image);
    CoXlTestMission((uintptr_t)level, 1);
    const ULONGLONG now = 100000;

    g_case = "a build of a far cell: expired tiles go while it builds";
    {
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverNear(NEAR_STEP); /* the replay's cameras placed */
        CoXlTestMoverTile(0, 0, 0, 0);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverTile(2, 1, -2, now - KEEP_MS - 1000);
        for (int k = 3; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now); /* live, kept */
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL); /* at the build */
        CHECK(CoXlTestMoverRelease(now) == 1);
        CHECK(!CoXlTestMoverLive(1) && CoXlTestMoverLive(2)); /* one a frame */
        CHECK(fake_host_logged("difficulty: moving tiles: the tile of cell (0, -2) released"));
        CHECK(fake_host_logged("on the game thread, during the build of cell (2, 1) (mover replay, near state 2)"));
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(2));
        CHECK(CoXlTestMoverRelease(now) == 0);
        CHECK(CoXlTestMoverLive(0)); /* the drop tile stays */
        CHECK(CoXlTestMoverState() == MV_REPLAY);
    }

    g_case = "a tile next to the building one waits while the merge is up";
    {
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 1, 1, now - KEEP_MS - 1000); /* west of (2, 1): its terrain overlaps the tile's square */
        CoXlTestMoverTile(2, 3, 2, now - KEEP_MS - 1000); /* diagonal: overlaps at the corner */
        CoXlTestMoverPlayer(OX + 2 * CELL, OY - 4 * CELL); /* far south: no player keeps either */
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1) && CoXlTestMoverLive(2));
        /* the merge torn down (spawn, finalize): nothing of the build draws the world any more */
        CoXlTestMover(OX, OY, MV_SPAWN, 0, 2, 1);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY - 4 * CELL);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(2));
    }

    g_case = "a tile in 0x41's vanilla-space square waits while the merge is up";
    {
        CoXlTestMover(OX, OY, MV_REPLAY, 1, -2, 1);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 1, 0, now - KEEP_MS - 1000); /* centre (2050, 2): far from the build, within 2176 m of the origin */
        CoXlTestMoverPlayer(OX - 2 * CELL, OY + CELL);
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
        CoXlTestMover(OX, OY, MV_IDLE, 1, -2, 1); /* idle: a stale merge flag holds nothing */
        CoXlTestMoverPlayer(OX - 2 * CELL, OY + CELL);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
    }

    g_case = "nothing goes while the merge cameras are world-sized (kick, setup, lookup, the replay before its cameras)";
    {
        static const struct { int mv, ns; } wide[] = {{MV_KICK, NEAR_IN}, {MV_SETUP, NEAR_IN}, {MV_LOOKUP, NEAR_IN}, {MV_REPLAY, NEAR_START}};
        for (size_t i = 0; i < sizeof wide / sizeof wide[0]; i++)
        {
            CoXlTestMover(OX, OY, wide[i].mv, 1, 2, 1);
            CoXlTestMoverNear(wide[i].ns);
            CoXlTestMoverTile(0, 0, 0, 0);
            for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
            CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000); /* clear of both squares: it goes once the cameras narrow */
            CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
            CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
        }
        CHECK(!fake_host_logged("(mover kick") && !fake_host_logged("(mover setup") && !fake_host_logged("(mover lookup"));
        CoXlTestMoverNear(NEAR_STEP); /* NearCameras ran */
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
        /* MV_KICK's first frame, before MoveMergeBegin: no merge, no camera yet */
        CoXlTestMover(OX, OY, MV_KICK, 0, 2, 1);
        CoXlTestMoverNear(NEAR_IN);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
        CoXlTestMoverNear(NEAR_STEP);
    }

    g_case = "0x41's square is measured from the world origin, not from the grid's";
    {
        /* a grid far from (0, 0): cell (-2, 1) at (903, -952) lies in 0x41's square, cell (1, 0) at (7048, -3000) one cell
         * from the grid origin but far from the world's */
        CoXlTestMover(5000.0f, -3000.0f, MV_REPLAY, 1, -1, -2);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, -2, 1, now - KEEP_MS - 1000);
        CoXlTestMoverTile(2, 1, 0, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(5000.0f - CELL, -3000.0f - 2 * CELL); /* at the build */
        CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(1) && !CoXlTestMoverLive(2));
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
        CoXlTestMover(5000.0f, -3000.0f, MV_SPAWN, 0, -1, -2);
        CoXlTestMoverPlayer(5000.0f - CELL, -3000.0f - 2 * CELL);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
    }

    g_case = "a tile a player is near is kept while another builds";
    {
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(OX, OY - 2 * CELL); /* inside (0, -2) */
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1) && CoXlTestMoverSeen(1) == now);
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1); /* no player at all: nothing goes, as in idle */
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS + 1000);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1)); /* 29 s: not expired */
    }

    g_case = "the level-update tick releases during a build before the build's step";
    {
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, GetTickCount64());
        CoXlTestMoverTile(1, 0, -2, GetTickCount64() - KEEP_MS - 1000);
        CoXlTestMoverTick((uintptr_t)level, OX + 2 * CELL, OY + CELL);
        CHECK(!CoXlTestMoverLive(1) && CoXlTestMoverState() == MV_REPLAY);
    }

    g_case = "a trim once a burst of tile events is 8 frames past";
    {
        g_mapped = 12000ll << 20; g_global = 77ll << 20; g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMoverSet(MV_IDLE);
        CHECK(CoXlTestTrimTick() == 0); /* nothing happened */
        CoXlTestTrimEvent("a tile in");
        for (int f = 0; f < 3; f++) CHECK(CoXlTestTrimTick() == 0);
        CoXlTestTrimEvent("a tile released");
        for (int f = 0; f < 7; f++) CHECK(CoXlTestTrimTick() == 0);
        CHECK(g_trims == 0);
        CHECK(CoXlTestTrimTick() == 1 && g_trims == 1);
        CHECK(fake_host_logged("difficulty: moving tiles: rpmalloc trim 1 after 2 tile event(s) (the last: a tile released)"));
        CHECK(fake_host_logged("mapped 12000 -> 11700 MB (-300), global cache 77 -> 0 MB"));
        for (int f = 0; f < 59; f++) CHECK(CoXlTestTrimTick() == 0);
        CHECK(!fake_host_logged("rpmalloc 60 frames after trim 1"));
        CHECK(CoXlTestTrimTick() == 0 && fake_host_logged("rpmalloc 60 frames after trim 1: mapped 11700 MB (-300 since before it)"));
        for (int f = 0; f < 100; f++) CoXlTestTrimTick();
        CHECK(g_trims == 1); /* once */
        CoXlTestTrimEvent("a tile in"); /* the next burst counts from the trim */
        for (int f = 0; f < 8; f++) CoXlTestTrimTick();
        CHECK(g_trims == 2 && fake_host_logged("rpmalloc trim 2 after 1 tile event(s) (the last: a tile in)"));
    }

    g_case = "no trim inside a build's heavy states; the next quiet idle frame takes it";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestTrimEvent("a tile released");
        static const int heavy[] = {MV_STAMPS, MV_KICK, MV_SETUP, MV_LOOKUP, MV_REPLAY, MV_TEARDOWN, MV_UNSTAMP, MV_SPAWN, MV_READY, MV_FAILING};
        for (size_t i = 0; i < sizeof heavy / sizeof heavy[0]; i++)
        { CoXlTestMoverSet(heavy[i]); for (int f = 0; f < 10; f++) CHECK(CoXlTestTrimTick() == 0); }
        CHECK(g_trims == 0);
        CoXlTestMoverSet(MV_PACKAGE);
        CHECK(CoXlTestTrimTick() == 1 && g_trims == 1);
        CoXlTestMoverSet(MV_IDLE);
    }

    g_case = "a new level forgets a pending trim";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestTrimEvent("a tile in");
        CoXlTestMission((uintptr_t)level, 1);
        for (int f = 0; f < 20; f++) CoXlTestTrimTick();
        CHECK(g_trims == 0);
        /* nor does a trim's follow-up cross into it */
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestTrimEvent("a tile in");
        for (int f = 0; f < 8; f++) CoXlTestTrimTick();
        CHECK(g_trims == 1);
        CoXlTestMission((uintptr_t)level, 1);
        for (int f = 0; f < 70; f++) CoXlTestTrimTick();
        CHECK(!fake_host_logged("rpmalloc 60 frames after trim 0"));
    }

    g_case = "a release in the level-update tick is a trim's event";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMover(OX, OY, MV_IDLE, 0, 0, 0);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, GetTickCount64());
        CoXlTestMoverTile(1, 0, -2, GetTickCount64() - KEEP_MS - 1000);
        const float px = OX + 6 * CELL, py = OY + 6 * CELL; /* far from every cell: no build starts */
        CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(!CoXlTestMoverLive(1));
        for (int f = 0; f < 7; f++) CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(g_trims == 0);
        CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(g_trims == 1);
    }

    g_case = "the release goes before a trim due on the same frame";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMover(OX, OY, MV_IDLE, 0, 0, 0);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, GetTickCount64());
        CoXlTestMoverTile(1, 0, -2, GetTickCount64() - KEEP_MS - 1000);
        CoXlTestTrimEvent("a tile in");
        for (int f = 0; f < 7; f++) CHECK(CoXlTestTrimTick() == 0); /* the next tick would trim */
        const float px = OX + 6 * CELL, py = OY + 6 * CELL;
        CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(!CoXlTestMoverLive(1) && g_trims == 0); /* the release, and its event starts the quiet again */
        for (int f = 0; f < 7; f++) CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(g_trims == 0);
        CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(g_trims == 1 && fake_host_logged("after 2 tile event(s) (the last: a tile released)"));
    }

    g_case = "a trim frame runs no build step";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMover(OX, OY, MV_PACKAGE, 0, 3, 3);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, GetTickCount64());
        CoXlTestTrimEvent("a tile released");
        for (int f = 0; f < 7; f++) CHECK(CoXlTestTrimTick() == 0);
        const uint32_t frames = CoXlTestMoverFrames();
        CoXlTestMoverTick((uintptr_t)level, OX + 6 * CELL, OY + 6 * CELL);
        CHECK(g_trims == 1 && CoXlTestMoverFrames() == frames && CoXlTestMoverState() == MV_PACKAGE);
        CoXlTestMoverSet(MV_IDLE);
    }

    g_case = "a tile in is a trim's event";
    {
        g_trims = 0;
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMover(OX, OY, MV_READY, 0, 3, 3);
        CoXlTestMoverTileIn(3, 3, 3);
        CHECK(CoXlTestMoverLive(3));
        for (int f = 0; f < 7; f++) CHECK(CoXlTestTrimTick() == 0);
        CHECK(CoXlTestTrimTick() == 1 && g_trims == 1);
        CHECK(fake_host_logged("after 1 tile event(s) (the last: a tile in)"));
    }

    g_case = "the trim's pins: the reviewed code and the game's slot";
    {
        g_fake.strictCode = 1;
        static uint8_t api[0x20], table[0x600];
        const uint64_t apiAt = (uint64_t)(uintptr_t)api, tableAt = (uint64_t)(uintptr_t)table, fn = (uint64_t)(uintptr_t)exe + 0x3245F0u;
        memcpy(g_fake.image + 0x3326308u, &apiAt, 8); memcpy(api + 0x10, &tableAt, 8); memcpy(table + 0x520, &fn, 8);
        Hex(exe + 0x3245F0u, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8B 35 02 BC 6E 01 B1 01 48 8B 9E 18 03 00 00 E8 DC BC 30 00");
        Hex(exe + 0x324630u, "48 8B 08 48 83 C0 08 C6 41 7D 01");
        Hex(exe + 0x324690u, "B8 29 00 00 00 66 89 42 0C");
        Hex(exe + 0x66BE20u, "48 C7 41 10 00 00 00 00 4C 8B D1 48 63 15 52 54 CF 01");
        Hex(exe + 0x66BF43u, "48 C1 E2 10 49 01 52 10 C3");
        Hex(g_fake.image + 0xAB761Cu, "48 8B 05 E5 EC 86 02 49 89 8E 18 76 AF 07 48 8B 48 10 FF 91 20 05 00 00");
        CHECK(CoXlTestTrimPins() == 1 && CoXlTestTrimCall() == fn);
        const uint64_t other = fn + 0x10;
        memcpy(table + 0x520, &other, 8);
        CHECK(CoXlTestTrimPins() == 0 && CoXlTestTrimCall() == 0);
        CHECK(fake_host_logged("difficulty: moving tiles: rpmalloc's trim is not the reviewed code; tile events do not trim it"));
        memcpy(table + 0x520, &fn, 8);
        static const struct { int inExe; uint32_t at; } kBytes[] = {{1, 0x3245F0u + 33}, {1, 0x32463Au}, {1, 0x324691u}, {1, 0x66BE2Eu}, {1, 0x66BF4Bu}, {0, 0xAB762Fu}};
        for (size_t i = 0; i < sizeof kBytes / sizeof kBytes[0]; i++)
        {
            uint8_t* at = (kBytes[i].inExe ? exe : g_fake.image) + kBytes[i].at;
            *at ^= 0x40;
            CHECK(CoXlTestTrimPins() == 0);
            *at ^= 0x40;
        }
        CHECK(CoXlTestTrimPins() == 1);
        /* refused: an event never calls anything */
        memcpy(table + 0x520, &other, 8);
        CoXlTestTrimPins();
        CoXlTestTrimEvent("a tile in");
        CoXlTestMoverSet(MV_IDLE);
        for (int f = 0; f < 20; f++) CHECK(CoXlTestTrimTick() == 0);
        g_fake.strictCode = 0;
        memset(api, 0, sizeof api);
        memset(g_fake.image + 0x3326308u, 0, 8);
    }

    const uint64_t fence = (uint64_t)(uintptr_t)exe + 0x1ADEA0u; /* the reviewed destroy: never called here (handle 0) */
    const float px = OX + 6 * CELL, py = OY + 6 * CELL; /* far from every cell: no build starts */

    g_case = "a due release waits for a drained render queue, and the frame's build step with it";
    {
        DrainWorld(fence);
        CHECK(CoXlTestDrainPins() == 1);
        CoXlTestTrimUse(TestTrim, TestStats);
        CoXlTestMover(OX, OY, MV_PACKAGE, 0, 3, 3);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, GetTickCount64());
        CoXlTestMoverTile(1, 0, -2, GetTickCount64() - KEEP_MS - 1000);
        const uint32_t frames = CoXlTestMoverFrames();
        for (int f = 0; f < 3; f++)
        {
            CoXlTestMoverTick((uintptr_t)level, px, py);
            CHECK(CoXlTestMoverLive(1) && CoXlTestMoverFrames() == frames && CoXlTestMoverState() == MV_PACKAGE);
        }
        Render(2, 2, 0, 0); /* the render thread caught up with the game's buffer, nothing waiting */
        CoXlTestMoverTick((uintptr_t)level, px, py);
        CHECK(!CoXlTestMoverLive(1) && CoXlTestMoverFrames() == frames);
        CHECK(fake_host_logged("no destroy, the render queue drained after 3 held frame(s) (the render thread in buffer 2, the game's 2; 0 message(s) queued, 0 device job(s))"));
        CoXlTestMoverSet(MV_IDLE);
    }

    g_case = "the render thread a buffer behind, queued messages or device jobs hold a release too";
    {
        DrainWorld(fence);
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverNear(NEAR_STEP);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
        Render(3, 4, 0, 0); /* still in the last frame's buffer: the game's own is empty so far */
        CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(1));
        Render(4, 4, 1, 0);
        CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(1));
        Render(4, 4, 0, 2);
        CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(1));
        Render(4, 4, 0, 0);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
        CHECK(fake_host_logged("(mover replay, near state 2); no destroy, the render queue drained after 3 held frame(s)"));
    }

    g_case = "after 8 held frames a release goes anyway; the next one waits again";
    {
        DrainWorld(fence);
        CoXlTestMover(OX, OY, MV_REPLAY, 1, 2, 1);
        CoXlTestMoverNear(NEAR_STEP);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverTile(2, 1, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
        for (int f = 0; f < 8; f++) CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(1));
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1) && CoXlTestMoverLive(2));
        CHECK(fake_host_logged("the render queue NOT drained after 8 held frame(s) (the render thread in buffer 1, the game's 2; 3 message(s) queued, 0 device job(s))"));
        CHECK(CoXlTestMoverRelease(now) == 1 && CoXlTestMoverLive(2));
        Render(2, 2, 0, 0);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(2));
        CHECK(fake_host_logged("the render queue drained after 1 held frame(s)"));
    }

    g_case = "the world-sized hold outlasts a drained queue; no frame is held for a tile that may not go";
    {
        DrainWorld(fence);
        Render(3, 3, 0, 0);
        CoXlTestMover(OX, OY, MV_LOOKUP, 1, 2, 1);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(OX + 2 * CELL, OY + CELL);
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
        Render(1, 2, 3, 0);
        CHECK(CoXlTestMoverRelease(now) == 0 && CoXlTestMoverLive(1));
    }

    g_case = "no render thread: the destroy posts no fence, nothing waits";
    {
        DrainWorld(fence);
        g_renderer[0x1480] = 0;
        CoXlTestMover(OX, OY, MV_IDLE, 0, 0, 0);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverPlayer(px, py);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1));
        CHECK(fake_host_logged("on the game thread; no destroy, no render thread"));
    }

    g_case = "the destroy alone is timed; another destroy than the reviewed one is not waited for";
    {
        DrainWorld((uint64_t)(uintptr_t)TestDestroy);
        CHECK(CoXlTestDrainPins() == 0);
        CHECK(fake_host_logged("difficulty: moving tiles: the destroy's render fence is not the reviewed code; releases do not wait for a drained render queue"));
        CHECK(fake_host_logged("render queue (the destroy's slot; the slot holds 0x"));
        CoXlTestMover(OX, OY, MV_IDLE, 0, 0, 0);
        CoXlTestMoverTile(0, 0, 0, 0);
        for (int k = 1; k < 8; k++) CoXlTestMoverTile(k, 9 + k, 9, now);
        CoXlTestMoverTile(1, 0, -2, now - KEEP_MS - 1000);
        CoXlTestMoverTileHandle(1, 0x1234);
        CoXlTestMoverPlayer(px, py);
        CHECK(CoXlTestMoverRelease(now) == 1 && !CoXlTestMoverLive(1)); /* busy, but not held */
        CHECK(g_destroyWorld == (uint64_t)(uintptr_t)g_world && g_destroyHandle == 0x1234);
        CHECK(fake_host_logged("ms on the game thread; the destroy alone ") && fake_host_logged(" ms, the render queue not read"));
    }

    g_case = "the fence's pins: each pinned byte and the destroy's slot";
    {
        DrainWorld(fence);
        g_fake.strictCode = 1;
        for (size_t i = 0; i < sizeof kDrain / sizeof kDrain[0]; i++) Hex(exe + kDrain[i].at, kDrain[i].bytes);
        CHECK(CoXlTestDrainPins() == 1);
        for (size_t i = 0; i < sizeof kDrain / sizeof kDrain[0]; i++)
        {
            uint8_t* at = exe + kDrain[i].at + (strlen(kDrain[i].bytes) + 1) / 3 - 1; /* each pin's last byte */
            *at ^= 0x40;
            CHECK(CoXlTestDrainPins() == 0);
            char named[64];
            snprintf(named, sizeof named, "(a pin; the slot holds 0x");
            CHECK(fake_host_logged(named));
            snprintf(named, sizeof named, "pin exe+0x%X reads ", kDrain[i].at);
            CHECK(fake_host_logged(named));
            *at ^= 0x40;
        }
        CHECK(CoXlTestDrainPins() == 1);
        g_fake.strictCode = 0;
        memset(g_fake.image + 0x3326308u, 0, 8);
    }

    g_case = "the game's slot holds the thunk 0x336AD0: accepted with its jmp to the destroy, refused without";
    {
        const uint64_t thunk = (uint64_t)(uintptr_t)exe + 0x336AD0u;
        DrainWorld(thunk);
        g_fake.strictCode = 1;
        for (size_t i = 0; i < sizeof kDrain / sizeof kDrain[0]; i++) Hex(exe + kDrain[i].at, kDrain[i].bytes);
        Hex(exe + 0x336AD0u, "E9 CB 73 E7 FF");
        CHECK(CoXlTestDrainPins() == 1);
        Hex(exe + 0x336AD0u, "E9 CB 73 E7 FE"); /* a jmp somewhere else */
        CHECK(CoXlTestDrainPins() == 0);
        CHECK(fake_host_logged("(the destroy's slot; the slot holds 0x"));
        DrainWorld(fence + 1);
        Hex(exe + 0x336AD0u, "E9 CB 73 E7 FF");
        CHECK(CoXlTestDrainPins() == 0); /* neither the destroy nor its thunk */
        g_fake.strictCode = 0;
        memset(g_fake.image + 0x3326308u, 0, 8);
    }

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tiletrim: all checks passed\n");
    return 0;
}
