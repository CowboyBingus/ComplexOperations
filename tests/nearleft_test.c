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
    CoXlTestWorldLevel(lg, 0);
    const LONG levels = CoXlTestLevels();
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

    if (g_failed) { printf("nearleft: %d checks failed\n", g_failed); return 1; }
    printf("nearleft: all cases passed\n");
    return 0;
}
