/* What a complex level's near terrain leaves when it ends early: the one-mip spawn slot, the replay flag, the crater
 * hold, the tiles and the hidden coarse nodes. Every level init and the ship's set-up end them, and a level init clears
 * nothing that the level being set up makes afterwards. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestWorldShip(void);
void CoXlTestRoadsLevel(const uint8_t* ops, uint32_t n, uint32_t planned);
uint8_t CoXlTestLocationOp(uint32_t i);
LONG CoXlTestLevels(void);
void CoXlTestMission(uintptr_t level, int complex);
void CoXlTestOneMipArm(uint64_t tex, uint64_t* renderSlot);
int CoXlTestOneMipArmed(void);
uint64_t CoXlTestOneMipTex(void);
uint64_t CoXlTestOneMipSpawn(uint32_t type, uint32_t name);
void CoXlTestNearActive(LONG on, uint64_t cam);
LONG CoXlTestNearActiveNow(void);
int CoXlTestCamShift(uint64_t camera);
void CoXlTestTileAdd(float cx, float cy, const uint16_t* heights, uint32_t n, float size);
int CoXlTestTilesLive(void);
float CoXlTestVisibleAt(float x, float y);
void CoXlTestHide(uint64_t handle);
uint64_t CoXlTestHideHandle(void);
void CoXlTestMoveHold(int on);
LONG64 CoXlTestMoveHoldSince(void);
void CoXlTestMapsNextLevel(uint64_t matTex);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u   /* [game+0x3326308]: the engine's interface, +8 the table whose +0x198 is the terrain spawn */
#define TABLE 0x3601000u
#define HEIGHTMAP 0x2A9E3232u /* generated_heightmap, generated_materials: the level terrain's spawn */
#define MATERIALS 0x95E7E3ACu
#define TEX 0x7777000ull      /* the complex level's one-mip copy */
#define STOCK_TEX 0x5555000ull /* what the render config's slot holds */
#define CAM 0xCA000ull
#define CX 9000.0f
#define CY -3000.0f

static uint8_t* g_lg;
static uint64_t g_renderSlot = STOCK_TEX, g_spawnSaw;
static int g_spawns;
static uint16_t g_heights[4] = {32768, 32768, 32768, 32768};

static uint64_t FakeSpawn(void* world, float* pose, uint32_t a, uint32_t type, uint32_t name, uint64_t resource, uint32_t hash, float* size,
    float* range, uint8_t flag)
{
    (void)world; (void)pose; (void)a; (void)type; (void)name; (void)resource; (void)hash; (void)size; (void)range; (void)flag;
    g_spawns++; g_spawnSaw = g_renderSlot;
    return 0x99;
}

/* The texture releases ([api+0x10]: +0x198 the one-mip copy's, +0x6B0 the material map's) and the main world
 * ([[game+0x3326340]+0x10E8]) with its terrains by handle (+0x230550: entries +8, count +0x18, buckets +0x1C). */
#define TABLE10 0x3602000u
#define ENGINE 0x3603000u
#define MAT_TEX 0x8888000ull
static uint64_t g_released, g_matGone;
static int g_releases, g_matGones;
static uint8_t* g_mainWorld;
static uint8_t g_terrainEntry[0x170];
static void FakeRelease(uint64_t tex) { g_releases++; g_released = tex; }
static void FakeMatGone(uint64_t tex) { g_matGones++; g_matGone = tex; }
static void MapsGame(uint64_t listed, int worldReads)
{
    const uintptr_t game = (uintptr_t)g_fake.image, t10 = game + TABLE10, engine = game + ENGINE, release = (uintptr_t)FakeRelease, gone = (uintptr_t)FakeMatGone;
    const uint64_t world = worldReads ? (uint64_t)(uintptr_t)g_mainWorld : 0, entries = (uint64_t)(uintptr_t)g_terrainEntry;
    const uint32_t count = listed != 0, buckets = 1, last = 0x7FFFFFFFu;
    memcpy(g_fake.image + API + 0x10, &t10, 8);
    memcpy(g_fake.image + TABLE10 + 0x198, &release, 8);
    memcpy(g_fake.image + TABLE10 + 0x6B0, &gone, 8);
    memcpy(g_fake.image + 0x3326340u, &engine, 8);
    memcpy(g_fake.image + ENGINE + 0x10E8, &world, 8);
    memcpy(g_terrainEntry, &listed, 8); memcpy(g_terrainEntry + 0x168, &last, 4);
    memcpy(g_mainWorld + 0x230550u + 8, &entries, 8); memcpy(g_mainWorld + 0x230550u + 0x18, &count, 4); memcpy(g_mainWorld + 0x230550u + 0x1C, &buckets, 4);
    g_releases = g_matGones = 0; g_released = g_matGone = 0;
}

static uint64_t Slot(void) { uint64_t v; memcpy(&v, g_fake.image + TABLE + 0x198, 8); return v; }

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API, table = game + TABLE, spawn = (uintptr_t)FakeSpawn;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    memcpy(g_fake.image + API + 8, &table, 8);
    memcpy(g_fake.image + TABLE + 0x198, &spawn, 8);
    CoXlTestGame(game);
    g_renderSlot = STOCK_TEX; g_spawns = 0; g_spawnSaw = 0;
}

/* What a complex level's near terrain has while it runs: op 0x38 and 0x69's slot, the replay, a tile, its hide, a hold. */
static void NearSetUp(void)
{
    CoXlTestOneMipArm(TEX, &g_renderSlot);
    CoXlTestNearActive(1, CAM);
    CoXlTestTileAdd(CX, CY, g_heights, 2, 512.0f);
    CoXlTestHide(0xB0B);
    CoXlTestMoveHold(1);
}

static int NearAllOn(void)
{
    return CoXlTestOneMipArmed() && Slot() != (uint64_t)(uintptr_t)FakeSpawn && CoXlTestNearActiveNow() == 1 && CoXlTestCamShift(CAM) == 1 &&
        CoXlTestTilesLive() == 1 && CoXlTestVisibleAt(CX, CY) > -9000.0f && CoXlTestHideHandle() == 0xB0B && CoXlTestMoveHoldSince() != 0;
}

int main(void)
{
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL) { printf("nearleft: no memory\n"); return 1; }
    const uintptr_t lg = (uintptr_t)g_lg;

    Start("a complex level's own level init, then its set-up");
    { static const uint8_t earlier[3] = {0x16, 0x82, 0x2B}; CoXlTestRoadsLevel(earlier, 3, 0); } /* a level before this one */
    CoXlTestWorldLevel(lg, 0);
    const LONG levels = CoXlTestLevels();
    CHECK(CoXlTestLocationOp(0) == 0 && CoXlTestLocationOp(1) == 0 && CoXlTestLocationOp(2) == 0); /* its places' ops are not this level's */
    NearSetUp(); /* op 0x38, op 0x69, the replay and a build in play all come after the level's init */
    CHECK(NearAllOn() && CoXlTestLevels() == levels);
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99 && g_spawns == 1 && g_spawnSaw == TEX && g_renderSlot == STOCK_TEX); /* the level's own spawn takes the copy */

    Start("the ship after a complex mission that ended inside a tile build");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CHECK(NearAllOn());
    CoXlTestWorldShip();
    CHECK(Slot() == (uint64_t)(uintptr_t)FakeSpawn && !CoXlTestOneMipArmed());
    CHECK(CoXlTestNearActiveNow() == 0 && CoXlTestCamShift(CAM) == 0);
    CHECK(CoXlTestTilesLive() == 0 && CoXlTestVisibleAt(CX, CY) < -9000.0f && CoXlTestHideHandle() == 0 && CoXlTestMoveHoldSince() == 0);
    CHECK(CoXlTestOneMipTex() == TEX); /* the copy stays allocated: the next copy's creation releases it */

    Start("a level init after a complex level dropped before its spawn");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CoXlTestWorldLevel(lg, 0x44); /* a war-table preview, or the next mission */
    CHECK(Slot() == (uint64_t)(uintptr_t)FakeSpawn && !CoXlTestOneMipArmed() && CoXlTestNearActiveNow() == 0 && CoXlTestCamShift(CAM) == 0);
    CHECK(CoXlTestTilesLive() == 0 && CoXlTestVisibleAt(CX, CY) < -9000.0f && CoXlTestMoveHoldSince() == 0);
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99 && g_spawnSaw == STOCK_TEX); /* that level's own spawn is the game's */
    NearSetUp(); /* and a complex level after it sets its own up again */
    CHECK(NearAllOn());

    Start("a slot left armed into the next level");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CoXlTestMission(0, 0); /* the levels advance with the slot still armed */
    CHECK(CoXlTestOneMipArmed() && Slot() != (uint64_t)(uintptr_t)FakeSpawn);
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99 && g_spawns == 1 && g_spawnSaw == STOCK_TEX && g_renderSlot == STOCK_TEX);
    CHECK(CoXlTestOneMipSpawn(0x1234u, MATERIALS) == 0x99 && g_spawns == 2 && g_spawnSaw == STOCK_TEX); /* another spawn: the game's too */

    /* The level terrain keeps its heightmap and material map by address, uncounted (exe 0x1AB0A7, 0x1AB0B5), and a
     * generation cancelled before the spawn's ready call leaves it in the world: its maps are never released then. */
    g_mainWorld = (uint8_t*)VirtualAlloc(NULL, 0x231000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Start("the last level's terrain is still in the world at the next level: its maps stay allocated");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99);
    MapsGame(0x99, 1);
    CoXlTestMapsNextLevel(MAT_TEX);
    CHECK(g_releases == 0 && g_matGones == 0 && CoXlTestOneMipTex() == 0);
    CHECK(fake_host_logged("terrain 0x99 is still in the world: its one-mip heightmap stays allocated") && fake_host_logged("terrain 0x99 is still in the world: its material map stays allocated"));

    Start("the last level's terrain is gone at the next level (or another terrain is listed): its maps are released");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99);
    MapsGame(0x77, 1);
    CoXlTestMapsNextLevel(MAT_TEX);
    CHECK(g_releases == 1 && g_released == TEX && g_matGones == 1 && g_matGone == MAT_TEX && !fake_host_logged("stays allocated"));

    Start("no terrain was spawned with the maps: released as before");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    MapsGame(0, 1); /* the level never reached its spawn; a terrain with handle 0 is no terrain */
    CoXlTestMapsNextLevel(MAT_TEX);
    CHECK(g_releases == 1 && g_matGones == 1);

    Start("the main world does not read at the next level: released as before");
    CoXlTestWorldLevel(lg, 0);
    NearSetUp();
    CHECK(CoXlTestOneMipSpawn(HEIGHTMAP, MATERIALS) == 0x99);
    MapsGame(0x99, 0);
    CoXlTestMapsNextLevel(MAT_TEX);
    CHECK(g_releases == 1 && g_released == TEX && g_matGones == 1 && !fake_host_logged("stays allocated"));

    if (g_failed) { printf("nearleft: %d checks failed\n", g_failed); return 1; }
    printf("nearleft: all cases passed\n");
    return 0;
}
