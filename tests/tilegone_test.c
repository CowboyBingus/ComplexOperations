/* The tiles' terrains at a level's end: the main world outlives every level, so the ship's set-up and each level init
 * destroy the last level's tile terrains, and the next replay frees a tile's maps only once its terrain is out of the
 * world. Run on the fake game, with the world's terrain map laid out as exe 0x1BCF30 reads it. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestWorldShip(void);
int CoXlTestMoveFrameInBuild(int set);
void CoXlTestTileReset(void);
void CoXlTestTileTerrain(int k, uint64_t handle, uint64_t tex0, uint64_t tex1, uint64_t band);
void CoXlTestTileBuild(int move, uint64_t dropHandle, uint64_t dropTex, int nearState);
uint64_t CoXlTestTileHandle(int k);
uint64_t CoXlTestTileLeft(int i);
uint64_t CoXlTestTileTex(int k);
void CoXlTestLevelGone(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u     /* [game+0x3326308]: +8 the world calls (+0x1E0 the terrain destroy), +0x10 the render calls */
#define TABLE8 0x3601000u
#define TABLE10 0x3602000u /* +0x198 a texture's release, +0x6B0 the band texture's destroy */
#define ENGINE 0x3700000u  /* [game+0x3326340]; +0x10E8 the main world */
#define TERRAINS 0x230550u /* world: the terrains' map */
#define BUCKETS 19u        /* as the crash dump's map */
#define SLOTS 32u

/* Level 3's four tile terrains in the crash dump, at the bucket each sat in there (map 0x377310080+0x230550). */
#define H0 0x41725C180ull /* the drop tile */
#define H1 0x623D53CD0ull
#define H2 0x6760530C0ull
#define H3 0xBEC90080ull
#define HX 0x5000ABC0ull  /* a terrain the mod does not hold (the ship's, a level's own) */

static uint8_t* g_world;
static struct { int seq; uint64_t world, handle; } g_destroys[32];
static struct { int seq; uint64_t tex; } g_frees[64];
static int g_destroyCount, g_freeCount, g_seq;

static uint32_t Hash(uint64_t key)
{
    const uint32_t m = 0x5BD1E995u;
    uint32_t lo = (uint32_t)key * m, hi = (uint32_t)(key >> 32) * m;
    lo ^= lo >> 24; lo *= m; hi ^= hi >> 24; hi *= m;
    uint32_t h = (lo ^ 0xDE8F4CA8u) * m;
    h ^= hi; h ^= h >> 13; h *= m; h ^= h >> 15;
    return h;
}

static uint8_t* Entry(uint32_t i) { uint64_t e; memcpy(&e, g_world + TERRAINS + 8, 8); return (uint8_t*)(uintptr_t)e + (size_t)i * 0x170; }
static uint32_t Next(uint32_t i) { uint32_t n; memcpy(&n, Entry(i) + 0x168, 4); return n; }
static void SetNext(uint32_t i, uint32_t n) { memcpy(Entry(i) + 0x168, &n, 4); }
static uint64_t Key(uint32_t i) { uint64_t k; memcpy(&k, Entry(i), 8); return k; }

static void MapClear(void)
{
    static uint8_t entries[SLOTS * 0x170];
    memset(entries, 0, sizeof entries);
    const uint64_t at = (uint64_t)(uintptr_t)entries;
    const uint32_t count = 0, buckets = BUCKETS;
    memcpy(g_world + TERRAINS + 8, &at, 8);
    memcpy(g_world + TERRAINS + 0x18, &count, 4);
    memcpy(g_world + TERRAINS + 0x1C, &buckets, 4);
    for (uint32_t i = 0; i < SLOTS; i++) SetNext(i, 0xFFFFFFFEu);
}

/* A terrain in the map: its bucket's head, or a spare slot chained after it. Returns the slot. */
static uint32_t MapAdd(uint64_t handle)
{
    uint32_t count, b = Hash(handle) % BUCKETS, at = b;
    memcpy(&count, g_world + TERRAINS + 0x18, 4);
    if (Next(b) != 0xFFFFFFFEu)
    {
        at = BUCKETS;
        while (Next(at) != 0xFFFFFFFEu) at++;
        SetNext(at, Next(b)); SetNext(b, at);
    }
    else SetNext(b, 0x7FFFFFFFu);
    memcpy(Entry(at), &handle, 8);
    count++; memcpy(g_world + TERRAINS + 0x18, &count, 4);
    return at;
}

static int MapHas(uint64_t handle)
{
    for (uint32_t i = 0; i < SLOTS; i++) if (Next(i) != 0xFFFFFFFEu && Key(i) == handle) return 1;
    return 0;
}

/* The destroy takes the terrain out of the map (0x1BD180) and frees its object. */
static void FakeDestroy(uint64_t world, uint64_t handle)
{
    if (g_destroyCount < 32) { g_destroys[g_destroyCount].seq = ++g_seq; g_destroys[g_destroyCount].world = world; g_destroys[g_destroyCount].handle = handle; }
    g_destroyCount++;
    for (uint32_t i = 0; i < SLOTS; i++) if (Next(i) != 0xFFFFFFFEu && Key(i) == handle) { const uint64_t gone = 0xDEADull; memcpy(Entry(i), &gone, 8); }
}

static void FakeFree(uint64_t tex)
{
    if (g_freeCount < 64) { g_frees[g_freeCount].seq = ++g_seq; g_frees[g_freeCount].tex = tex; }
    g_freeCount++;
}

static int Destroyed(uint64_t handle)
{
    int n = 0;
    for (int i = 0; i < g_destroyCount && i < 32; i++) if (g_destroys[i].handle == handle) n++;
    return n;
}

static int DestroySeq(uint64_t handle)
{
    for (int i = 0; i < g_destroyCount && i < 32; i++) if (g_destroys[i].handle == handle) return g_destroys[i].seq;
    return 0;
}

/* The sequence number of the free of `tex`, 0 when it was not freed. */
static int Freed(uint64_t tex)
{
    for (int i = 0; i < g_freeCount && i < 64; i++) if (g_frees[i].tex == tex) return g_frees[i].seq;
    return 0;
}

static void Start(const char* name, int destroyCall)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API, t8 = game + TABLE8, t10 = game + TABLE10, engine = game + ENGINE;
    const uintptr_t destroy = destroyCall ? (uintptr_t)FakeDestroy : 0, free = (uintptr_t)FakeFree, world = (uintptr_t)g_world;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    memcpy(g_fake.image + API + 8, &t8, 8);
    memcpy(g_fake.image + API + 0x10, &t10, 8);
    memcpy(g_fake.image + TABLE8 + 0x1E0, &destroy, 8);
    memcpy(g_fake.image + TABLE10 + 0x198, &free, 8);
    memcpy(g_fake.image + TABLE10 + 0x6B0, &free, 8);
    memcpy(g_fake.image + 0x3326340u, &engine, 8);
    memcpy(g_fake.image + ENGINE + 0x10E8u, &world, 8);
    CoXlTestGame(game);
    CoXlTestTileReset();
    MapClear();
    g_destroyCount = g_freeCount = g_seq = 0;
}

/* A complex level with its drop tile (slot 0 and g_near) and three tiles built in mission, every one in the world. */
static void Tiles(void)
{
    CoXlTestTileTerrain(-1, H0, 0xA0, 0xA1, 0xA2);
    CoXlTestTileTerrain(0, H0, 0, 0, 0); /* the drop tile's maps are g_near's and g_matband's */
    CoXlTestTileTerrain(1, H1, 0xB0, 0xB1, 0xB2);
    CoXlTestTileTerrain(2, H2, 0xC0, 0xC1, 0xC2);
    CoXlTestTileTerrain(3, H3, 0xD0, 0xD1, 0xD2);
    MapAdd(H0); MapAdd(H1); MapAdd(H2); MapAdd(H3); MapAdd(HX);
}

static int NoHandles(void)
{
    for (int k = 0; k < 8; k++) if (CoXlTestTileHandle(k) != 0) return 0;
    return CoXlTestTileHandle(-1) == 0 && CoXlTestTileHandle(-2) == 0;
}

int main(void)
{
    g_world = (uint8_t*)VirtualAlloc(NULL, TERRAINS + 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t* lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL || lg == NULL) { printf("tilegone: no memory\n"); return 1; }

    Start("the map is laid out as the crash dump's", 1);
    CHECK(MapAdd(H0) == 4 && MapAdd(H1) == 9 && MapAdd(H2) == 10 && MapAdd(H3) == 12); /* the slots each sat in there */

    Start("the ship destroys every tile terrain once; its maps go at the next replay, after the destroys", 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    Tiles();
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 4 && Destroyed(H0) == 1 && Destroyed(H1) == 1 && Destroyed(H2) == 1 && Destroyed(H3) == 1);
    CHECK(g_destroys[0].world == (uint64_t)(uintptr_t)g_world && g_destroys[3].world == (uint64_t)(uintptr_t)g_world);
    CHECK(!MapHas(H0) && !MapHas(H3) && MapHas(HX) && Destroyed(HX) == 0); /* a terrain the mod does not hold stays */
    CHECK(g_freeCount == 0 && NoHandles());
    CoXlTestMoveFrameInBuild(1); /* a build the level's end cuts short: the stall watch's gate closes with it */
    CoXlTestLevelGone();
    CHECK(CoXlTestMoveFrameInBuild(-1) == 0);
    CHECK(Freed(0xB0) > DestroySeq(H1) && Freed(0xB1) > 0 && Freed(0xB2) > 0 && Freed(0xD2) > DestroySeq(H3) && Freed(0xC0) > 0);
    CHECK(CoXlTestTileTex(-1) == 0xA0); /* the drop tile's maps are not kept: the replay releases them */

    Start("a level init destroys them too (the ship's step may be off)", 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    Tiles();
    CoXlTestWorldLevel((uintptr_t)lg, 0x44);
    CHECK(g_destroyCount == 4 && Destroyed(H0) == 1 && Destroyed(H3) == 1 && NoHandles());

    Start("a handle not in the world is never given to the destroy (it would insert it and free its object)", 1);
    CoXlTestTileTerrain(1, H1, 0xB0, 0xB1, 0xB2);
    CoXlTestTileTerrain(5, 0x7000ull, 0xE0, 0, 0); /* released already, or never spawned */
    MapAdd(H1);
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 1 && Destroyed(H1) == 1 && Destroyed(0x7000ull) == 0 && NoHandles());
    CoXlTestLevelGone();
    CHECK(Freed(0xE0) > 0 && Freed(0xB0) > DestroySeq(H1));

    Start("a forgotten handle's address reused by another terrain is left alone", 1);
    Tiles();
    CoXlTestWorldShip();
    MapAdd(H1); /* a later terrain, not the mod's, at a freed tile's address */
    CoXlTestWorldShip();
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    CHECK(g_destroyCount == 4 && Destroyed(H1) == 1 && MapHas(H1));

    Start("a build with the drop tile stashed: the new tile and the drop tile both go", 1);
    CoXlTestTileTerrain(-1, H2, 0xC0, 0xC1, 0xC2); /* the new tile's, in g_near while it builds */
    CoXlTestTileTerrain(1, H1, 0xB0, 0xB1, 0xB2);
    CoXlTestTileBuild(1, H0, 0xA0, 1);
    MapAdd(H0); MapAdd(H1); MapAdd(H2);
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 3 && Destroyed(H0) == 1 && Destroyed(H1) == 1 && Destroyed(H2) == 1 && NoHandles());

    Start("the drop tile between its 0x69 and 0x6B stays at the ship (its 0x6A may still run) and goes at the next level init", 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    Tiles();
    CoXlTestTileBuild(0, 0, 0, 1); /* no build; g_near ready, not yet in */
    CoXlTestWorldShip();
    CHECK(Destroyed(H0) == 0 && Destroyed(H1) == 1 && Destroyed(H3) == 1 && MapHas(H0));
    CHECK(CoXlTestTileHandle(-1) == H0 && CoXlTestTileHandle(0) == H0 && CoXlTestTileHandle(1) == 0);
    CoXlTestWorldLevel((uintptr_t)lg, 0x44); /* the generator's job has left by now: nothing finalizes it any more */
    CHECK(g_destroyCount == 4 && Destroyed(H0) == 1 && !MapHas(H0) && NoHandles());
    CoXlTestLevelGone();
    CHECK(Freed(0xB0) > DestroySeq(H1) && Freed(0xD2) > 0 && CoXlTestTileTex(-1) == 0xA0 && CoXlTestTileLeft(0) == 0);
    CHECK(fake_host_logged("0 tile terrains left from the last level"));

    Start("the generator's job still runs at the ship: every terrain stays (no wait on its lock), the level init takes them", 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    Tiles();
    static uint8_t job[0x10];
    { const uint64_t at = (uint64_t)(uintptr_t)job; memcpy(lg + 0xF718, &at, 8); }
    job[8] = 1;
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 0 && MapHas(H0) && MapHas(H3) && CoXlTestTileHandle(1) == H1 && CoXlTestTileHandle(-1) == H0);
    CHECK(fake_host_logged("0 already gone, 4 kept (the generator's job still runs)"));
    job[8] = 0;
    CoXlTestWorldLevel((uintptr_t)lg, 0x44);
    CHECK(g_destroyCount == 4 && !MapHas(H0) && !MapHas(H3) && NoHandles());
    memset(lg + 0xF718, 0, 8);

    Start("a level init alone destroys a drop tile between its 0x69 and 0x6B", 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0);
    Tiles();
    CoXlTestTileBuild(0, 0, 0, 1);
    CoXlTestWorldLevel((uintptr_t)lg, 0x44);
    CHECK(g_destroyCount == 4 && Destroyed(H0) == 1 && !MapHas(H0) && NoHandles());

    Start("a terrain the level's end could not destroy keeps the level's maps and waits for the next level's end", 0);
    Tiles();
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 0);
    CoXlTestLevelGone();
    CHECK(g_freeCount == 0 && CoXlTestTileTex(-1) == 0 && CoXlTestTileTex(2) == 0 && NoHandles());
    /* each once (the drop tile is slot 0's and g_near's), in the mod's order */
    CHECK(CoXlTestTileLeft(0) == H0 && CoXlTestTileLeft(1) == H1 && CoXlTestTileLeft(2) == H2 && CoXlTestTileLeft(3) == H3 && CoXlTestTileLeft(4) == 0);
    CHECK(fake_host_logged("4 tile terrain(s) left in the world from the last level"));
    FakeDestroy((uint64_t)(uintptr_t)g_world, H1); /* gone some other way before the next replay */
    g_destroyCount = 0;
    CoXlTestTileTerrain(2, 0x7100ull, 0xE0, 0xE1, 0xE2); /* the new level's tile in a slot one of them had; not in the world */
    CoXlTestLevelGone();
    CHECK(CoXlTestTileLeft(0) == H0 && CoXlTestTileLeft(1) == H2 && CoXlTestTileLeft(2) == H3 && CoXlTestTileLeft(3) == 0);
    CHECK(g_freeCount == 0 && CoXlTestTileTex(2) == 0); /* one is still drawn: this level's maps stay too */
    { const uintptr_t destroy = (uintptr_t)FakeDestroy; memcpy(g_fake.image + TABLE8 + 0x1E0, &destroy, 8); } /* the call reads again */
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 3 && Destroyed(H0) == 1 && Destroyed(H2) == 1 && Destroyed(H3) == 1 && Destroyed(H1) == 0);
    CHECK(!MapHas(H0) && !MapHas(H2) && !MapHas(H3) && MapHas(HX) && CoXlTestTileLeft(0) == 0 && CoXlTestTileLeft(2) == 0);
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 3); /* forgotten: not again */

    Start("no destroy call: nothing is destroyed and no map is freed", 0);
    Tiles();
    CoXlTestWorldShip();
    CHECK(g_destroyCount == 0 && CoXlTestTileHandle(1) == H1);
    CoXlTestLevelGone();
    CHECK(g_freeCount == 0 && CoXlTestTileTex(-1) == 0 && CoXlTestTileTex(2) == 0 && NoHandles());

    Start("the main world does not read: nothing is destroyed and no map is freed", 1);
    Tiles();
    { const uint64_t none = 0; memcpy(g_fake.image + ENGINE + 0x10E8u, &none, 8); }
    CoXlTestWorldShip();
    CoXlTestLevelGone();
    CHECK(g_destroyCount == 0 && g_freeCount == 0 && NoHandles());

    Start("a replay with the last level's tiles gone frees their maps", 1);
    Tiles();
    { const uint64_t played[4] = {H0, H1, H2, H3}; for (int i = 0; i < 4; i++) FakeDestroy((uint64_t)(uintptr_t)g_world, played[i]); } /* gone some other way */
    g_destroyCount = 0;
    CoXlTestLevelGone();
    CHECK(Freed(0xB0) > 0 && Freed(0xC2) > 0 && Freed(0xD1) > 0 && CoXlTestTileTex(-1) == 0xA0 && NoHandles());

    if (g_failed) { printf("tilegone: %d checks failed\n", g_failed); return 1; }
    printf("tilegone: all cases passed\n");
    return 0;
}
