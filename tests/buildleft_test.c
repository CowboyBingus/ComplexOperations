/* A tile build the level's end cuts short (proto/xl_free.c MoveBuildLeft): MoveTick stops with the level, so the build's
 * stamp units, its merge cameras and textures and the held package would stay in the main world, which outlives the
 * level. The ship's set-up and each level init end them on the game thread, before the tile terrains go. Run on the
 * fake game: the world's unit list as exe 0x338960 reads it, the merge object inside the level object. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestWorldShip(void);
void CoXlTestTileReset(void);
void CoXlTestTileTerrain(int k, uint64_t handle, uint64_t tex0, uint64_t tex1, uint64_t band);
void CoXlTestLevelGone(void);
void CoXlTestMoverWorld(uint64_t world);
void CoXlTestStampUnits(uint64_t* units, uint32_t n);
void CoXlTestBuildCut(uintptr_t level, int state, int merge, int held, uint32_t gone, uint64_t records, uint32_t at66);
void CoXlTestBuildEnv(uint64_t env);
int CoXlTestBuildLeft(int what);
int CoXlTestTeardown(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u     /* [game+0x3326308]: +8 the world calls, +0x10 the render calls */
#define TABLE8 0x3601000u  /* +0x60 a node's destroy, +0x1E0 a terrain's, +0x388 a unit's, +0x410 the animation wait, +0x478 R+0x108's */
#define TABLE10 0x3602000u /* +0x190 a texture's release, +0x1E0 a readback's, +0x218 a name's unpublish, +0x300 a package's release */
#define ENGINE 0x3700000u  /* [game+0x3326340]; +0x10E8 the main world */
#define UNITS 0x230588u    /* world: the unit count, the array's address 8 bytes on */
#define TERRAINS 0x230550u /* world: the terrains' map (empty here but for one tile) */
#define MERGE 0x2B30u      /* level: the merge object R */
#define STEP_REPLAY 6      /* MV_REPLAY */
#define STEP_STAMPS 2      /* MV_STAMPS */
#define ENV 0xE000ull
#define PACKAGE 0x5157EF2020B48DCDull
enum { C_WAIT = 1, C_UNIT, C_NODE, C_CAMERA, C_MERGED, C_ENVIRON, C_UNPUBLISH, C_RELEASE, C_READBACK, C_PACKAGE_OUT, C_TERRAIN };

static uint8_t *g_world, *g_other, *g_level, *g_gen, *g_records;
static uint64_t g_list[64];
static struct { int what; uint64_t a, b; } g_calls[128];
static int g_callCount;

static void Call(int what, uint64_t a, uint64_t b)
{
    if (g_callCount < 128) { g_calls[g_callCount].what = what; g_calls[g_callCount].a = a; g_calls[g_callCount].b = b; }
    g_callCount++;
}
/* How many calls of a kind (with first argument `a`, or any when 0), and the place of the first in the order of calls. */
static int Count(int what, uint64_t a)
{
    int n = 0;
    for (int i = 0; i < g_callCount && i < 128; i++) n += g_calls[i].what == what && (a == 0 || g_calls[i].a == a);
    return n;
}
static int First(int what, uint64_t a)
{
    for (int i = 0; i < g_callCount && i < 128; i++) if (g_calls[i].what == what && (a == 0 || g_calls[i].a == a)) return i + 1;
    return 0;
}

static uint32_t ListCount(void) { uint32_t n; memcpy(&n, g_world + UNITS, 4); return n; }
static void ListSet(const uint64_t* units, uint32_t n)
{
    const uint64_t at = (uint64_t)(uintptr_t)g_list;
    memcpy(g_list, units, (size_t)n * 8);
    memcpy(g_world + UNITS, &n, 4);
    memcpy(g_world + UNITS + 8, &at, 8);
}
/* The game's destroy: the unit's place in the list is taken by the last one. One it does not find takes the last
 * one's place all the same (exe 0x3389D1..0x3389EC), which the caller must never let happen. */
static void FakeUnitDestroy(uint64_t world, uint64_t unit, uint64_t flags, uint64_t extra)
{
    (void)flags; (void)extra;
    Call(C_UNIT, unit, world);
    uint32_t n = ListCount(), i = 0;
    while (i < n && g_list[i] != unit) i++;
    if (n > 1) g_list[i] = g_list[n - 1];
    n--;
    memcpy(g_world + UNITS, &n, 4);
}
static void FakeWait(uint64_t manager) { Call(C_WAIT, manager, 0); }
static void FakeNode(uint64_t world, uint32_t node) { Call(C_NODE, node, world); }
static void FakeCamera(uint64_t world, uint64_t unit) { Call(C_CAMERA, unit, world); }
static void FakeMerged(uint64_t r, uint64_t world) { Call(C_MERGED, r, world); }
static void FakeEnv(uint64_t manager, uint32_t slot) { Call(C_ENVIRON, manager, slot); }
static void FakeUnpublish(uint32_t name) { Call(C_UNPUBLISH, name, 0); }
static void FakeRelease(uint64_t tex) { Call(C_RELEASE, tex, 0); }
static void FakeReadback(uint64_t descriptor) { Call(C_READBACK, descriptor, 0); }
static void FakePackage(uint64_t hash) { Call(C_PACKAGE_OUT, hash, 0); }
static void FakeTerrain(uint64_t world, uint64_t handle) { Call(C_TERRAIN, handle, world); }

/* mov rax, target; jmp rax */
static void Jump(uint8_t* at, void* target)
{
    const uint64_t to = (uint64_t)(uintptr_t)target;
    at[0] = 0x48; at[1] = 0xB8; memcpy(at + 2, &to, 8); at[10] = 0xFF; at[11] = 0xE0;
}
static void Put(uint32_t at, void* value) { const uint64_t v = (uint64_t)(uintptr_t)value; memcpy(g_fake.image + at, &v, 8); }
static void Field(uint8_t* base, uint32_t at, uint64_t value, int bytes) { memcpy(base + at, &value, (size_t)bytes); }
static uint64_t Read(const uint8_t* base, uint32_t at, int bytes) { uint64_t v = 0; memcpy(&v, base + at, (size_t)bytes); return v; }

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    uint8_t* exe = fake_host_exe();
    DWORD old = 0;
    Put(0x3326308u, image + API); Put(API + 8, image + TABLE8); Put(API + 0x10, image + TABLE10);
    Put(TABLE8 + 0x60, FakeNode); Put(TABLE8 + 0x1E0, FakeTerrain); Put(TABLE8 + 0x388, FakeUnitDestroy); Put(TABLE8 + 0x478, FakeCamera);
    Put(TABLE10 + 0x190, FakeRelease); Put(TABLE10 + 0x1E0, FakeReadback); Put(TABLE10 + 0x218, FakeUnpublish); Put(TABLE10 + 0x300, FakePackage);
    Put(0x3326340u, image + ENGINE); Put(ENGINE + 0x10E8u, g_world);
    /* the reviewed animation wait (exe 0x3340A0): mov rcx,[rcx+0x230050]; jmp, here to the test's */
    static const uint8_t kWait[] = {0x48, 0x8B, 0x89, 0x50, 0x00, 0x23, 0x00, 0xE9, 0x54, 0x00, 0x00, 0x00};
    memcpy(exe + 0x3340A0u, kWait, sizeof kWait);
    Jump(exe + 0x334100u, FakeWait);
    VirtualProtect(exe + 0x334000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    Put(TABLE8 + 0x410, exe + 0x3340A0u);
    /* the game's own two calls of the teardown, 0xFCF2F0 and 0x1009890 */
    Jump(image + 0xFCF2F0u, FakeMerged); Jump(image + 0x1009890u, FakeEnv);
    VirtualProtect(image + 0xFCF000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    VirtualProtect(image + 0x1009000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    memset(g_world, 0, TERRAINS + 0x100); memset(g_other, 0, TERRAINS + 0x100);
    memset(g_level, 0, 0x4000); memset(g_gen, 0, 0x1000); memset(g_records, 0, 0x4000);
    { const uint64_t manager = 0xA5A5ull, gen = (uint64_t)(uintptr_t)g_gen; memcpy(g_world + 0x230050u, &manager, 8); memcpy(g_level + 0x2C8, &gen, 8); }
    CoXlTestGame((uintptr_t)image);
    CoXlTestTileReset();
    CoXlTestMoverWorld((uint64_t)(uintptr_t)g_world);
    CoXlTestBuildEnv(ENV);
    g_callCount = 0;
}

/* The merge as a build leaves it at step 0x38 and later; `own` 1 when the level's own teardown has run since (it
 * destroys the first camera, releases R+0xB8 and clears the generation block, [G] with it). */
static void Merge(int own)
{
    uint8_t* r = g_level + MERGE;
    Field(r, 0x8, own ? 0 : 0x111, 4); Field(r, 0x18, 0x222, 4); Field(r, 0x108, 0xCA00, 8);
    Field(r, 0x98, 0x9800, 8); Field(r, 0xA0, 0xA000, 8); Field(r, 0xB8, own ? 0 : 0xB800, 8); Field(r, 0xD8, 0xD800, 8);
    r[0xE8] = r[0xE9] = r[0xF0] = 1; r[0xEC] = (uint8_t)!own;
    Field(g_gen, 0, own ? 0 : (uint64_t)(uintptr_t)r, 8);
}
/* The level's 0x66 record (number 1) with a lookup that has a readback and a texture. */
static uint8_t g_lookup[0x20];
static void Lookup(void)
{
    Field(g_records, 1 * 0x19B0 + 0x18, (uint64_t)(uintptr_t)g_lookup, 8);
    Field(g_lookup, 0x10, 0x7EAD, 8); Field(g_lookup, 0x18, 0x700C, 8);
}

int main(void)
{
    g_world = (uint8_t*)VirtualAlloc(NULL, TERRAINS + 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_other = (uint8_t*)VirtualAlloc(NULL, TERRAINS + 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_level = (uint8_t*)VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_gen = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_records = (uint8_t*)VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t* lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL || g_other == NULL || g_level == NULL || g_gen == NULL || g_records == NULL || lg == NULL) { printf("buildleft: no memory\n"); return 1; }
    const uint64_t world = (uint64_t)(uintptr_t)g_world;
    /* the world's units, not in order of address; the build's are 0x5000, 0x3000 and 0x9000 */
    static const uint64_t kWorld[7] = {0x8000, 0x5000, 0x1000, 0x9000, 0x2000, 0x3000, 0x7000};
    static uint64_t units[6];
    static const uint64_t kUnits[6] = {0x4000 /* destroyed in an earlier frame */, 0x5000, 0x6000 /* not in the world */, 0, 0x3000, 0x9000};

    Start("the ship ends a build cut short in its replay: its stamp units still in the world go, once each, after one wait");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 1, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestBuildLeft(0) == 5 && CoXlTestBuildLeft(4) == 1);
    CoXlTestWorldShip();
    CHECK(Count(C_UNIT, 0) == 3 && Count(C_UNIT, 0x5000) == 1 && Count(C_UNIT, 0x3000) == 1 && Count(C_UNIT, 0x9000) == 1);
    CHECK(Count(C_UNIT, 0x6000) == 0 && Count(C_UNIT, 0x4000) == 0); /* neither the one not in the list nor the one that went before */
    CHECK(Count(C_WAIT, 0) == 1 && Count(C_WAIT, 0xA5A5) == 1 && First(C_WAIT, 0) < First(C_UNIT, 0));
    CHECK(g_calls[First(C_UNIT, 0) - 1].b == world && ListCount() == 4);
    CHECK(CoXlTestBuildLeft(0) == 0 && CoXlTestBuildLeft(4) == 0); /* and the next level's 0x63 looks where its stamps are */
    CHECK(Count(C_NODE, 0) == 0 && Count(C_MERGED, 0) == 0 && Count(C_PACKAGE_OUT, 0) == 0);
    CHECK(fake_host_logged("the ship: the build of cell (0, 0) was cut short in step 'replay': 3 of its 5 stamp units destroyed, 1 already gone; the merge was not up; "
        "the merge package was not held"));
    g_callCount = 0;
    CoXlTestWorldShip();
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_callCount == 0); /* ended: nothing again */

    Start("a level init ends it too (the ship's step may be off)");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_STAMPS, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_UNIT, 0) == 3 && Count(C_UNIT, 0x4000) == 0 && CoXlTestBuildLeft(0) == 0); /* 0x4000 is not in the world either */
    CHECK(fake_host_logged("a level init: the build of cell (0, 0) was cut short in step 'stamps': 3 of its 6 stamp units destroyed, 2 already gone"));

    Start("no unit of the build in the world: nothing destroyed, no wait");
    ListSet(kWorld, 1);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_UNIT, 0) == 0 && Count(C_WAIT, 0) == 0 && ListCount() == 1 && CoXlTestBuildLeft(0) == 0);

    Start("the build's world is not the main world: its units are kept (their list is not known to be there)");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestMoverWorld((uint64_t)(uintptr_t)g_other);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_UNIT, 0) == 0 && Count(C_WAIT, 0) == 0 && CoXlTestBuildLeft(0) == 0 && CoXlTestBuildLeft(4) == 0);
    CHECK(fake_host_logged("0 of its 6 stamp units destroyed, 0 already gone, the rest KEPT in the world"));

    Start("the animation wait is not the reviewed one: the units are kept");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    g_fake.strictCode = 1;
    g_fake.exe[0x3340A0u] ^= 0x40;
    CoXlTestWorldShip();
    CHECK(Count(C_UNIT, 0) == 0 && fake_host_logged("the rest KEPT in the world"));
    g_fake.strictCode = 0;

    Start("the merge after the level's own teardown: the second camera, R+0x108, the game's two calls and the textures it left");
    Merge(1);
    Lookup(); /* the records are the next generation's by now: whatever stands at the old 0x66's place is not touched */
    CoXlTestTileTerrain(1, 0x7100ull, 0xB0, 0xB1, 0xB2);
    { /* that tile's terrain in the world's map: its bucket's head */
        static uint8_t entries[32 * 0x170];
        const uint64_t at = (uint64_t)(uintptr_t)entries, handle = 0x7100ull; const uint32_t count = 1, buckets = 1, end = 0x7FFFFFFFu;
        memset(entries, 0, sizeof entries);
        memcpy(g_world + TERRAINS + 8, &at, 8); memcpy(g_world + TERRAINS + 0x18, &count, 4); memcpy(g_world + TERRAINS + 0x1C, &buckets, 4);
        memcpy(entries, &handle, 8); memcpy(entries + 0x168, &end, 4);
    }
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    const uint64_t r = (uint64_t)(uintptr_t)(g_level + MERGE);
    CHECK(Count(C_NODE, 0) == 1 && Count(C_NODE, 0x222) == 1 && Count(C_CAMERA, 0xCA00) == 1 && g_calls[First(C_NODE, 0) - 1].b == world);
    CHECK(Count(C_MERGED, r) == 1 && g_calls[First(C_MERGED, 0) - 1].b == world && Count(C_ENVIRON, ENV) == 1 && g_calls[First(C_ENVIRON, 0) - 1].b == 5);
    CHECK(Count(C_RELEASE, 0) == 3 && Count(C_RELEASE, 0x9800) == 1 && Count(C_RELEASE, 0xA000) == 1 && Count(C_RELEASE, 0xD800) == 1);
    CHECK(Count(C_UNPUBLISH, 0) == 3 && Count(C_UNPUBLISH, 0xA2003184u) == 1 && Count(C_UNPUBLISH, 0x1BB41B90u) == 1 && Count(C_UNPUBLISH, 0xEBD2B190u) == 1);
    CHECK(Count(C_READBACK, 0) == 0 && Count(C_UNPUBLISH, 0xDBCD240Du) == 0 && Read(g_lookup, 0x18, 8) == 0x700C);
    CHECK(First(C_NODE, 0) < First(C_RELEASE, 0) && First(C_CAMERA, 0) < First(C_RELEASE, 0)); /* the cameras before what they draw into */
    CHECK(Count(C_TERRAIN, 0x7100) == 1 && First(C_CAMERA, 0) < First(C_TERRAIN, 0)); /* and before the tile terrains */
    CHECK(Read(g_level + MERGE, 0x108, 8) == 0 && Read(g_level + MERGE, 0x98, 8) == 0 && Read(g_level + MERGE, 0xD8, 8) == 0 && g_level[MERGE + 0xE8] == 0 &&
        g_level[MERGE + 0xF0] == 0);
    CHECK(CoXlTestBuildLeft(1) == 0 && CoXlTestBuildLeft(2) == 0);
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(3) == 1); /* never in the call that destroyed the merge */
    CHECK(fake_host_logged("0 of its 0 stamp units destroyed, 0 already gone; the merge torn down; the merge package released at the next level's end"));
    g_callCount = 0;
    CoXlTestWorldLevel((uintptr_t)lg, 0x44);
    CHECK(Count(C_PACKAGE_OUT, PACKAGE) == 1 && g_callCount == 1 && CoXlTestBuildLeft(3) == 0); /* now, before this level init's own acquire */

    Start("the merge before the level's own teardown: both cameras and all four textures");
    Merge(0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_NODE, 0) == 2 && Count(C_NODE, 0x111) == 1 && Count(C_NODE, 0x222) == 1 && Count(C_CAMERA, 0xCA00) == 1);
    CHECK(Count(C_RELEASE, 0) == 4 && Count(C_RELEASE, 0xB800) == 1 && Count(C_UNPUBLISH, 0x2FFE957Du) == 1 && Read(g_level + MERGE, 0x8, 4) == 0);
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(3) == 1); /* this call tore the merge down: the release waits */

    Start("a release an earlier teardown left pending goes at the level's end; the package of a build with no merge yet waits for the next");
    CoXlTestBuildCut((uintptr_t)g_level, STEP_STAMPS, 0, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(2) == 0 && CoXlTestBuildLeft(3) == 1);
    CHECK(fake_host_logged("the merge was not up; the merge package released at the next level's end"));
    CoXlTestWorldShip();
    CHECK(Count(C_PACKAGE_OUT, PACKAGE) == 1 && CoXlTestBuildLeft(3) == 0);

    Start("the generation block names another merge object: the merge is left alone");
    Merge(0);
    Field(g_gen, 0, (uint64_t)(uintptr_t)(g_level + MERGE + 8), 8);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_NODE, 0) == 0 && Count(C_CAMERA, 0) == 0 && Count(C_MERGED, 0) == 0 && Count(C_RELEASE, 0) == 0 && CoXlTestBuildLeft(1) == 0);
    CHECK(fake_host_logged("the merge NOT torn down"));

    Start("a build's own teardown in play still releases the 0x94 lookup of the level's 0x66 record");
    Merge(0);
    Lookup();
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestTeardown() == 1);
    CHECK(Count(C_READBACK, (uint64_t)(uintptr_t)g_lookup) == 1 && Count(C_UNPUBLISH, 0xDBCD240Du) == 1 && Count(C_RELEASE, 0x700C) == 1 && Read(g_lookup, 0x18, 8) == 0);
    CHECK(Count(C_NODE, 0) == 2 && Count(C_RELEASE, 0) == 5);

    Start("nothing under way: nothing called, no line");
    ListSet(kWorld, 7);
    Merge(0);
    CoXlTestWorldShip();
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_callCount == 0 && !fake_host_logged("was cut short"));

    Start("a new level's replay puts the stamp pass's camera offset back (units the level's end kept)");
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestBuildLeft(4) == 1);
    CoXlTestLevelGone();
    CHECK(CoXlTestBuildLeft(4) == 0 && CoXlTestBuildLeft(0) == 0);

    if (g_failed) { printf("buildleft: %d check(s) failed\n", g_failed); return 1; }
    printf("buildleft: all cases passed\n");
    return 0;
}
