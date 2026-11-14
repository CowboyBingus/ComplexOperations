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
void CoXlTestMergeBegin(void);
void CoXlTestBuildCentre(float y);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u     /* [game+0x3326308]: +8 the world calls, +0x10 the render calls */
#define TABLE8 0x3601000u  /* +0x60 a node's destroy, +0x1E0 a terrain's, +0x388 a unit's, +0x410 the animation wait, +0x478 R+0x108's */
#define TABLE10 0x3602000u /* +0x190 a texture's release, +0x1E0 a readback's, +0x218 a name's unpublish, +0x300 a package's release */
#define ENGINE 0x3700000u  /* [game+0x3326340]; +0x10E8 the main world */
#define UNITS 0x230588u    /* world: the unit count, the array's address 8 bytes on */
#define TERRAINS 0x230550u /* world: the terrains' map (empty here but for one tile) */
#define WORLD_SIZE 0x231000u
#define MERGE 0x2B30u      /* level: the merge object R */
#define STEP_REPLAY 6      /* MV_REPLAY */
#define STEP_STAMPS 2      /* MV_STAMPS */
#define ENV 0xE000ull
#define STATE 0x1E4u      /* level: the generator's state (0 idle; a level init has written its 1 before the mod's hook) */
#define JOB 0xF718u        /* level: the generator's job, running while its +8 is set */
#define STEP_SPAWN 9       /* MV_SPAWN */
#define STEP_KICK 3        /* MV_KICK */
#define CENTRE 300.0f      /* the tile's centre in y; its stamp units stand 20000 m north of their places */
#define PACKAGE 0x5157EF2020B48DCDull
enum { C_WAIT = 1, C_UNIT, C_NODE, C_CAMERA, C_MERGED, C_ENVIRON, C_UNPUBLISH, C_RELEASE, C_READBACK, C_PACKAGE_OUT, C_TERRAIN };

static uint8_t *g_world, *g_other, *g_level, *g_gen, *g_records;
static uint64_t g_list[64];
static uint8_t g_unitMem[10][0x60], g_job[16];
#define U(n) ((uint64_t)(uintptr_t)g_unitMem[n])
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

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at++) n++;
    return n;
}

/* The list R+0x108's destroy looks its object up in ([world+0x230910], count +0x230908): the merge's 0xCA00 last of
 * three, or not in it. */
static void Cameras(int listed)
{
    static uint64_t list[3];
    const uint64_t at = (uint64_t)(uintptr_t)list; const uint32_t n = 3;
    list[0] = 0xC100; list[1] = 0xC200; list[2] = listed ? 0xCA00 : 0xC300;
    memcpy(g_world + 0x230908u, &n, 4); memcpy(g_world + 0x230910u, &at, 8);
    if (listed < 0) memset(g_world + 0x230908u, 0, 16); /* an empty world: no list */
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

/* A unit as the spawn's constructor leaves it (exe 0xD7879..0xD78A8): its world at +0x18, its pose at +0x20, whose
 * translation's y is at +0x54. */
static void Unit(int n, const uint8_t* world, float y)
{
    const uint64_t w = (uint64_t)(uintptr_t)world;
    memset(g_unitMem[n], 0, sizeof g_unitMem[n]);
    memcpy(g_unitMem[n] + 0x18, &w, 8); memcpy(g_unitMem[n] + 0x54, &y, 4);
}
static void Generator(uint32_t state, int runs)
{
    const uint64_t job = (uint64_t)(uintptr_t)g_job;
    memcpy(g_level + STATE, &state, 4); memcpy(g_level + JOB, &job, 8);
    g_job[8] = (uint8_t)runs;
}

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
    memset(g_world, 0, WORLD_SIZE); memset(g_other, 0, WORLD_SIZE);
    Cameras(1);
    memset(g_level, 0, 0x10000); memset(g_gen, 0, 0x1000); memset(g_records, 0, 0x4000);
    { const uint64_t manager = 0xA5A5ull, gen = (uint64_t)(uintptr_t)g_gen; memcpy(g_world + 0x230050u, &manager, 8); memcpy(g_level + 0x2C8, &gen, 8); }
    CoXlTestGame((uintptr_t)image);
    CoXlTestTileReset();
    CoXlTestMoverWorld((uint64_t)(uintptr_t)g_world);
    CoXlTestBuildEnv(ENV);
    CoXlTestBuildCentre(CENTRE);
    for (int n = 0; n < 10; n++) Unit(n, g_world, CENTRE + 20000.0f + 100.0f * (float)n);
    Generator(0, 0);
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
    g_world = (uint8_t*)VirtualAlloc(NULL, WORLD_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_other = (uint8_t*)VirtualAlloc(NULL, WORLD_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_level = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_gen = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_records = (uint8_t*)VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t* lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL || g_other == NULL || g_level == NULL || g_gen == NULL || g_records == NULL || lg == NULL) { printf("buildleft: no memory\n"); return 1; }
    const uint64_t world = (uint64_t)(uintptr_t)g_world;
    /* the world's units, not in order of address; the build's are 0x5000, 0x3000 and 0x9000 */
    const uint64_t kWorld[7] = {U(8), U(5), U(1), U(9), U(2), U(3), U(7)};
    static uint64_t units[6];
    const uint64_t kUnits[6] = {U(4) /* destroyed in an earlier frame */, U(5), U(6) /* not in the world */, 0, U(3), U(9)};

    Start("the ship ends a build cut short in its replay: its stamp units are forgotten and none is destroyed, listed at its place or not");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 1, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestBuildLeft(0) == 5 && CoXlTestBuildLeft(4) == 1);
    CoXlTestWorldShip();
    CHECK(Count(C_UNIT, 0) == 0 && Count(C_WAIT, 0) == 0 && ListCount() == 7); /* the unload took the build's; these are the ship's */
    CHECK(CoXlTestBuildLeft(0) == 0 && CoXlTestBuildLeft(4) == 0); /* and the next level's 0x63 looks where its stamps are */
    CHECK(Count(C_NODE, 0) == 0 && Count(C_MERGED, 0) == 0 && Count(C_PACKAGE_OUT, 0) == 0);
    CHECK(fake_host_logged("the ship: the build of cell (0, 0) was cut short in step 'replay': 0 of its 5 stamp units destroyed, 1 already gone, "
        "WARNING: 3 still listed at the build's place are left in the world (nothing is destroyed after the ship); the merge was not up; "
        "the merge package was not held (generator state 0; R+0x108 not looked at;"));
    CHECK(!fake_host_logged("spawn ring"));
    g_callCount = 0;
    CoXlTestWorldShip();
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_callCount == 0 && Lines("was cut short") == 1); /* ended: nothing again, no second line */

    Start("a level init ends it too (the ship's step may be off)");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_STAMPS, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_UNIT, 0) == 3 && Count(C_UNIT, U(4)) == 0 && CoXlTestBuildLeft(0) == 0); /* 0x4000 is not in the world either */
    CHECK(Count(C_UNIT, U(5)) == 1 && Count(C_UNIT, U(3)) == 1 && Count(C_UNIT, U(9)) == 1 && Count(C_UNIT, U(6)) == 0); /* once each, the listed ones */
    CHECK(Count(C_WAIT, 0) == 1 && Count(C_WAIT, 0xA5A5) == 1 && First(C_WAIT, 0) < First(C_UNIT, 0));
    CHECK(g_calls[First(C_UNIT, 0) - 1].b == world && ListCount() == 4 && fake_host_logged("; the spawn ring "));
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
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0); /* straight from play */
    CHECK(Count(C_UNIT, 0) == 0 && Count(C_WAIT, 0) == 0 && CoXlTestBuildLeft(0) == 0 && CoXlTestBuildLeft(4) == 0);
    CHECK(fake_host_logged("0 of its 6 stamp units destroyed, 0 already gone, the rest KEPT in the world"));

    Start("the animation wait is not the reviewed one: the units are kept");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    g_fake.strictCode = 1;
    g_fake.exe[0x3340A0u] ^= 0x40;
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
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
    CHECK(Count(C_MERGED, r) == 1 && g_calls[First(C_MERGED, 0) - 1].b == world && Count(C_ENVIRON, 0) == 0); /* slot 5: the level's own teardown ended it */
    CHECK(Count(C_RELEASE, 0) == 3 && Count(C_RELEASE, 0x9800) == 1 && Count(C_RELEASE, 0xA000) == 1 && Count(C_RELEASE, 0xD800) == 1);
    CHECK(Count(C_UNPUBLISH, 0) == 3 && Count(C_UNPUBLISH, 0xA2003184u) == 1 && Count(C_UNPUBLISH, 0x1BB41B90u) == 1 && Count(C_UNPUBLISH, 0xEBD2B190u) == 1);
    CHECK(Count(C_READBACK, 0) == 0 && Count(C_UNPUBLISH, 0xDBCD240Du) == 0 && Read(g_lookup, 0x18, 8) == 0x700C);
    CHECK(First(C_NODE, 0) < First(C_RELEASE, 0) && First(C_CAMERA, 0) < First(C_RELEASE, 0)); /* the cameras before what they draw into */
    CHECK(Count(C_TERRAIN, 0x7100) == 1 && First(C_CAMERA, 0) < First(C_TERRAIN, 0)); /* and before the tile terrains */
    CHECK(Read(g_level + MERGE, 0x108, 8) == 0 && Read(g_level + MERGE, 0x98, 8) == 0 && Read(g_level + MERGE, 0xD8, 8) == 0 && g_level[MERGE + 0xE8] == 0 &&
        g_level[MERGE + 0xF0] == 0);
    CHECK(CoXlTestBuildLeft(1) == 0 && CoXlTestBuildLeft(2) == 0);
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(3) == 1); /* never in the call that destroyed the merge */
    CHECK(fake_host_logged("0 of its 0 stamp units destroyed, 0 already gone; the merge torn down; the merge package is left to the next level init, "
        "which takes it over (generator state 0; R+0x108 destroyed;"));
    g_callCount = 0;
    CoXlTestWorldShip();
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(3) == 1); /* no ship step sends it */
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0x44);
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(3) == 0); /* the level init asked for the package before the hook: it is the level's */
    CoXlTestLevelGone();
    CHECK(Count(C_PACKAGE_OUT, 0) == 0); /* and the replay does not take it from the level's merge */

    Start("the merge before the level's own teardown: both cameras and all four textures");
    Merge(0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_NODE, 0) == 2 && Count(C_NODE, 0x111) == 1 && Count(C_NODE, 0x222) == 1 && Count(C_CAMERA, 0xCA00) == 1);
    CHECK(Count(C_RELEASE, 0) == 4 && Count(C_RELEASE, 0xB800) == 1 && Count(C_UNPUBLISH, 0x2FFE957Du) == 1 && Read(g_level + MERGE, 0x8, 4) == 0);
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(2) == 0 && CoXlTestBuildLeft(3) == 0 && Count(C_ENVIRON, 0) == 0);
    CHECK(fake_host_logged("the merge torn down; the merge package is this level init's now (generator state 1;"));

    Start("the package of a build with no merge yet: owed at the ship, the level's at the next level init, never sent");
    CoXlTestBuildCut((uintptr_t)g_level, STEP_STAMPS, 0, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(2) == 0 && CoXlTestBuildLeft(3) == 1);
    CHECK(fake_host_logged("the merge was not up; the merge package is left to the next level init"));
    CoXlTestWorldShip();
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(3) == 1);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_PACKAGE_OUT, 0) == 0 && CoXlTestBuildLeft(3) == 0);

    Start("the generator is not idle at the ship: all of the build is kept for the level init");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    Merge(0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 1, (uint64_t)(uintptr_t)g_records, 1);
    Generator(3, 0);
    CoXlTestWorldShip();
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(0) == 5 && CoXlTestBuildLeft(1) == 1 && CoXlTestBuildLeft(2) == 1 && CoXlTestBuildLeft(4) == 1);
    CHECK(fake_host_logged("the ship: the build of cell (0, 0) was cut short in step 'replay', and the level generator is not idle (state 3, its job is gone): "
        "its 5 stamp units, its merge (up) and its package (held) wait for the next level init"));
    Generator(0, 1); /* the state's teardown is past but the job still runs */
    CoXlTestWorldShip();
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(0) == 5 && fake_host_logged("(state 0, its job runs)"));
    Generator(1, 1); /* and a level init beside a running job, which the game never makes */
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(0) == 5 && CoXlTestBuildLeft(1) == 1);
    Generator(0, 0); /* a level init's hook comes after its own state 1 */
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_callCount == 0 && CoXlTestBuildLeft(0) == 5);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_UNIT, 0) == 0 && Count(C_WAIT, 0) == 0 && Count(C_NODE, 0) == 2 && Count(C_MERGED, 0) == 1 && CoXlTestBuildLeft(0) == 0 && CoXlTestBuildLeft(1) == 0);
    CHECK(fake_host_logged("a level init: the build of cell (0, 0) was cut short in step 'replay': 0 of its 5 stamp units destroyed, 1 already gone, "
        "WARNING: 3 still listed at the build's place are left in the world (nothing is destroyed after the ship); the merge torn down")); /* the ship was between */
    CHECK(fake_host_logged("R+0x108 destroyed;") && !fake_host_logged("spawn ring"));

    Start("an address in the list that is no longer the build's unit is left alone");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    Unit(5, g_world, CENTRE + 2000.0f);          /* in the main world, but on the map: not at the offset */
    Unit(3, g_other, CENTRE + 20000.0f);         /* at the offset, in another world */
    Unit(9, g_world, CENTRE + 20000.0f + 2176.0f); /* the farthest a stamp of the tile can stand */
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_UNIT, 0) == 1 && Count(C_UNIT, U(9)) == 1 && Count(C_UNIT, U(5)) == 0 && Count(C_UNIT, U(3)) == 0 && ListCount() == 6);
    CHECK(fake_host_logged("1 of its 6 stamp units destroyed, 2 already gone, 2 at their addresses are not this build's units (left alone); the merge was not up"));
    Start("a unit past the tile's reach at the offset is not the build's either");
    ListSet(kWorld, 7);
    memcpy(units, kUnits, sizeof units);
    CoXlTestStampUnits(units, 6);
    Unit(5, g_world, CENTRE + 20000.0f + 2200.0f);
    Unit(3, g_world, CENTRE + 20000.0f - 2200.0f);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    Generator(1, 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Count(C_UNIT, 0) == 1 && Count(C_UNIT, U(9)) == 1 && fake_host_logged("2 at their addresses are not this build's units"));

    Start("the second camera's node a teardown before destroyed is not queued again");
    Merge(1); /* R+0x18 holds 0x222, which the level's 0x66 destroyed */
    CoXlTestBuildCut((uintptr_t)g_level, STEP_KICK, 0, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestMergeBegin();
    CoXlTestWorldShip(); /* cut before 0x38 made this build's cameras */
    CHECK(Count(C_NODE, 0) == 0 && Count(C_CAMERA, 0xCA00) == 1 && Count(C_MERGED, 0) == 1 && CoXlTestBuildLeft(1) == 0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestMergeBegin();
    Field(g_level + MERGE, 0x18, 0x333, 4); /* 0x38 made a new one */
    CoXlTestMergeBegin(); /* a later frame of the merge keeps the first's reading */
    g_callCount = 0;
    CoXlTestWorldShip();
    CHECK(Count(C_NODE, 0) == 1 && Count(C_NODE, 0x333) == 1);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_KICK, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestMergeBegin();
    CHECK(CoXlTestTeardown() == 1 && Count(C_NODE, 0x333) == 1); /* in play too: a build that fails before its 0x38 */
    Field(g_level + MERGE, 0x18, 0x444, 4);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestTeardown() == 1 && Count(C_NODE, 0x444) == 1);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestTeardown() == 1 && Count(C_NODE, 0x444) == 1); /* a teardown remembers what it destroyed */

    Start("the level-end teardown runs only in the main world");
    Merge(1);
    CoXlTestMoverWorld((uint64_t)(uintptr_t)g_other);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_NODE, 0) == 0 && Count(C_CAMERA, 0) == 0 && Count(C_MERGED, 0) == 0 && fake_host_logged("the merge NOT torn down"));

    Start("a build cut after its own teardown leaves nothing: one line, once");
    CoXlTestBuildCut((uintptr_t)g_level, STEP_SPAWN, 0, 0, 0, (uint64_t)(uintptr_t)g_records, 1);
    Generator(3, 1); /* nothing to do, so the generator does not matter */
    CoXlTestWorldShip();
    CHECK(g_callCount == 0 && fake_host_logged("the ship: the build of cell (0, 0) was cut short in step 'spawn' with no stamp unit, merge or package of it left; "
        "its tile goes with the tile terrains"));
    CoXlTestWorldShip();
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(Lines("was cut short") == 1);

    Start("R+0x108 the unload took out of the world's list is not handed to the destroy; the rest goes on");
    Merge(1);
    Cameras(0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_CAMERA, 0) == 0 && Read(g_level + MERGE, 0x108, 8) == 0 && Count(C_MERGED, 0) == 1 && Count(C_NODE, 0x222) == 1 && CoXlTestBuildLeft(1) == 0);
    CHECK(fake_host_logged("the merge torn down") && fake_host_logged("(generator state 0; R+0x108 already gone;"));

    Start("a world with no such list: R+0x108 is not handed to the destroy either");
    Merge(1);
    Cameras(-1);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CoXlTestWorldShip();
    CHECK(Count(C_CAMERA, 0) == 0 && Read(g_level + MERGE, 0x108, 8) == 0 && Count(C_MERGED, 0) == 1);

    Start("in play R+0x108 goes to the destroy as the game's own teardown hands it, listed or not");
    Merge(0);
    Cameras(0);
    CoXlTestBuildCut((uintptr_t)g_level, STEP_REPLAY, 1, 1, 0, (uint64_t)(uintptr_t)g_records, 1);
    CHECK(CoXlTestTeardown() == 1 && Count(C_CAMERA, 0xCA00) == 1 && Read(g_level + MERGE, 0x108, 8) == 0);

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
    CHECK(Count(C_ENVIRON, ENV) == 1 && g_calls[First(C_ENVIRON, 0) - 1].b == 5); /* in play the build's slot 5 is its own to end */

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
