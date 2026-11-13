/* Stamps on the extra large map's moving tiles: a stamp's layer units take their heights from a tile's 1 m grid (the
 * placed units, exe 0xDFA60's inline read) or from the drawn ground (0xD5B10), and a tile that comes or goes changes
 * the ground under stamps that are already standing. Every grid holds the ground as drawn now, a stamp gets the grid of
 * the tile it stands by, the sampler answers while a tile builds, and the streamed stamps on a changed tile's ground go
 * out and come back. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestGenLevel(float current);
int CoXlTestTileAddAt(float cx, float cy, const uint16_t* heights, uint32_t n, float size);
void CoXlTestTileRemove(int slot);
void CoXlTestCoarse(const uint16_t* heights, uint32_t n, float size);
void CoXlTestNearBuilding(int building);
void CoXlTestLayerTile(int slot, int in, float cx, float cy);
uint64_t CoXlTestLayerField(uint64_t unit, uint64_t field);
int CoXlTestLayerHeight(uint64_t desc, float x, float y, float* h);
void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count);
void CoXlTestStreamSlot(uint32_t i, float x, float y, uint64_t unit, int mission);
uint32_t CoXlTestStampsReplace(int slot, float cx, float cy);
uint32_t CoXlTestTileGo(int slot);
void CoXlTestHeightKept(float* data, uint32_t cols, uint32_t rows, float extent);
float CoXlTestVisibleWithNone(float x, float y);
float CoXlTestVisibleAt(float x, float y);
void CoXlTestStreamObserver(int on, float x, float y);
int CoXlTestRespawnRoom(uint32_t i, float distance2);
uint32_t CoXlTestSpawnDue(uint32_t i, float distance2);
int CoXlTestStreamFlag(uint32_t i);
void CoXlTestStreamScan(void);
void CoXlTestStreamSlotState(uint32_t i, int* live, int* due, uint64_t* unit);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SPAN (2304.0f * 4095.0f / 4096.0f) /* a tile's terrain, sample to sample */
#define AX 3074.65625f                     /* the user's drop tile (session 210f2c17) and its west and south cells */
#define AY -4094.875f
#define BX 1026.21875f
#define CY2 -6143.3125f
#define API 0x3600000u
#define TABLE 0x3601000u

static uint16_t U16(float metres) { return (uint16_t)((metres + 500.0f) / 1000.0f * 65535.0f + 0.5f); }
static uint16_t g_coarse[4], g_tileA[4], g_tileB[4];
static float g_levelData[4];
static uint8_t g_levelDesc[0x28];
static uint8_t g_unit[0x60];

/* As the game sends the level's layer heightfield: data, origin (the grid's centre), extent, columns, rows. */
static void LevelDesc(void)
{
    memset(g_levelDesc, 0, sizeof g_levelDesc);
    uint64_t data = (uint64_t)(uintptr_t)g_levelData;
    float origin[2] = {0.0f, 0.0f}, extent[2] = {16384.0f, 16384.0f};
    uint32_t dims[2] = {2, 2};
    memcpy(g_levelDesc, &data, 8); memcpy(g_levelDesc + 8, origin, 8); memcpy(g_levelDesc + 0x14, extent, 8); memcpy(g_levelDesc + 0x1C, dims, 8);
}

/* The descriptor a stamp whose level unit stands at (x, y) gets for its placed units (0 = the level's own). */
static uint64_t Field(float x, float y)
{
    float at[2] = {x, y};
    memcpy(g_unit + 0x50, at, 8);
    return CoXlTestLayerField((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc);
}

/* A placed unit's height at (x, y) from a descriptor, as exe 0xDFA60 reads it (0xE059E..0xE0612: nearest sample, the
 * index compared unsigned with the last column / row). */
static float Read(uint64_t desc, float x, float y)
{
    const uint8_t* d = (const uint8_t*)(uintptr_t)desc;
    uint64_t data; float origin[2], extent[2]; uint32_t cols, rows;
    memcpy(&data, d, 8); memcpy(origin, d + 8, 8); memcpy(extent, d + 0x14, 8); memcpy(&cols, d + 0x1C, 4); memcpy(&rows, d + 0x20, 4);
    float u = ((x - origin[0]) / extent[0] + 0.5f) * (float)cols, v = ((y - origin[1]) / extent[1] + 0.5f) * (float)rows;
    uint32_t c = (uint32_t)(int64_t)u, r = (uint32_t)(int64_t)v;
    if (c > cols - 1) c = cols - 1;
    if (r > rows - 1) r = rows - 1;
    return ((const float*)(uintptr_t)data)[(size_t)(rows - 1 - r) * cols + c];
}

static int Near(float a, float b) { return a > b - 0.05f && a < b + 0.05f; }

static uint8_t* g_lg;
static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    CoXlTestWorldLevel((uintptr_t)g_lg, 0);
    CoXlTestGenLevel(8.0f);
    for (int k = 0; k < 4; k++) { g_coarse[k] = U16(40.0f); g_tileA[k] = U16(45.0f); g_tileB[k] = U16(30.0f); }
    CoXlTestCoarse(g_coarse, 2, 16383.5f);
    LevelDesc();
}

/* The streamer's removal on the fake game: the world interface's destroy (+0x388) and the animation wait (+0x410,
 * the reviewed exe 0x3340A0, here a `ret`). */
static int g_destroys;
static uint64_t g_destroyed;
static void FakeDestroy(uint64_t world, uint64_t unit, uint64_t a, uint64_t b) { (void)world; (void)a; (void)b; g_destroys++; g_destroyed = unit; }
static void StreamGame(void)
{
    uint8_t* exe = fake_host_exe();
    DWORD old;
    VirtualProtect(exe + 0x334000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    exe[0x3340A0u] = 0xC3;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API, table = game + TABLE, wait = (uintptr_t)exe + 0x3340A0u,
        destroy = (uintptr_t)FakeDestroy;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    memcpy(g_fake.image + API + 8, &table, 8);
    memcpy(g_fake.image + TABLE + 0x388, &destroy, 8);
    memcpy(g_fake.image + TABLE + 0x410, &wait, 8);
    g_destroys = 0; g_destroyed = 0;
}

/* The streamer on the fake game with `count` slots, the level's heightfield kept (40 m but where a case sets it). */
static uint8_t g_context[0x100], g_array[64 * 0xE0];
static void Stream(uint32_t count)
{
    uint64_t world = 0xABC0;
    memcpy(g_context + 0x40, &world, 8);
    memset(g_array, 0, sizeof g_array);
    CoXlTestStreamSetup((uintptr_t)g_fake.image, g_context, g_array, count);
    for (int k = 0; k < 4; k++) g_levelData[k] = 40.0f;
    CoXlTestHeightKept(g_levelData, 2, 2, 16384.0f);
}

/* A streamed stamp at (x, y) with a square footprint of `side` m (record +0xB0, map metres: the setup's half is 0). */
static void Slot(uint32_t i, float x, float y, float side, uint64_t unit, int mission)
{
    float box[4] = {x - side / 2, y - side / 2, x + side / 2, y + side / 2};
    memcpy(g_array + (size_t)i * 0xE0 + 0xB0, box, sizeof box);
    CoXlTestStreamSlot(i, x, y, unit, mission);
}

/* The mission world's physics bodies in use of 1048576 and its navmesh cutters of 1000 (the readers'
 * [exe+0x27BA890+0x160]+0x4C/+0x40 and [exe+0x27C6330+16]+0x20/+0x24). */
static void Budgets(uint32_t bodies, uint32_t cutters)
{
    uint8_t* exe = g_fake.exe;
    uint64_t physics = (uint64_t)(uintptr_t)(exe + 0x27E0000u), pool = (uint64_t)(uintptr_t)(exe + 0x27F0000u);
    uint32_t slots = 1048576u, room = 1000u;
    memcpy(exe + 0x27BA890u + 0x160u, &physics, 8);
    memcpy(exe + 0x27E0000u + 0x40, &slots, 4); memcpy(exe + 0x27E0000u + 0x4C, &bodies, 4);
    memcpy(exe + 0x27C6330u + 16u, &pool, 8);
    memcpy(exe + 0x27F0000u + 0x20, &cutters, 4); memcpy(exe + 0x27F0000u + 0x24, &room, 4);
}

int main(void)
{
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL) { printf("layergrid: no memory\n"); return 1; }

    Start("a stamp by the drop tile alone: its grid holds the tile inside and the coarse ground past it");
    int a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    uint64_t fa = Field(2100.0f, AY);
    CHECK(fa != 0 && Near(Read(fa, 2600.0f, AY), 45.0f) && Near(Read(fa, 1900.0f, AY), 40.0f));
    CHECK(Field(1126.0f, AY) == 0); /* no tile within reach: the level's own heightfield, the coarse ground as drawn */

    Start("a tile comes west of the drop tile: the grids hold its ground, and a stamp there gets its grid");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    int b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    fa = Field(2100.0f, AY); /* nearer the drop tile: its grid, filled again where the new tile lies */
    CHECK(fa != 0 && Near(Read(fa, 1900.0f, AY), 30.0f) && Near(Read(fa, 2600.0f, AY), 45.0f));
    uint64_t fb = Field(1900.0f, AY); /* nearer the new tile: its own grid */
    CHECK(fb != 0 && fb != fa && Near(Read(fb, 1900.0f, AY), 30.0f) && Near(Read(fb, 2300.0f, AY), 45.0f));
    CHECK(Field(BX - 1500.0f, AY) == 0);
    float h = 0.0f;
    CHECK(CoXlTestLayerHeight(fb, 1900.0f, AY, &h) && Near(h, 30.0f));

    Start("while a moving tile builds, stamps still get the drawn ground");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    CoXlTestNearBuilding(1);
    fa = Field(2600.0f, AY);
    CHECK(fa != 0 && Near(Read(fa, 2600.0f, AY), 45.0f));
    h = 0.0f;
    CHECK(CoXlTestLayerHeight((uint64_t)(uintptr_t)g_levelDesc, 2600.0f, AY, &h) && Near(h, 45.0f));
    CoXlTestNearBuilding(0);

    Start("a tile goes: the other grids hold the coarse ground there again, and its own grid is out of use");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(AX, CY2, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, AX, CY2);
    /* Past the drop tile's span: in its band the drawn ground is the higher of the tile and the coarse terrain. */
    fa = Field(AX, AY - 900.0f);
    CHECK(fa != 0 && Near(Read(fa, AX, AY - 1200.0f), 30.0f));
    CoXlTestTileRemove(b);
    CoXlTestLayerTile(b, 0, AX, CY2);
    fa = Field(AX, AY - 900.0f);
    CHECK(fa != 0 && Near(Read(fa, AX, AY - 1200.0f), 40.0f));
    CHECK(Field(AX, CY2) == 0);

    Start("a tile comes: the stamps whose ground moved go out and come back; the rest stay");
    StreamGame();
    Stream(6);
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);         /* on the new tile: the level's 40 m before, its 30 m now */
    Slot(1, BX + 1600.0f, AY, 20.0f, 0x2000, 0);        /* the drop tile's interior: past the new tile's span and reach */
    Slot(2, BX, AY, 20.0f, 0x3000, 1);                  /* a mission's stamp: never streamed out */
    Slot(3, BX - 1400.0f, AY + 300.0f, 20.0f, 0x4000, 0); /* in reach past the span: its grid now holds the coarse 40 m it stood on */
    Slot(4, BX + 1100.0f, AY, 20.0f, 0x5000, 0);        /* in the drop tile's square and the new tile's span: still the drop tile's 45 m */
    Slot(5, BX + 1100.0f, AY, 400.0f, 0x6000, 0);       /* the same, its footprint reaching into the new tile's square */
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 2);
    CHECK(CoXlTestStreamFlag(0) && !CoXlTestStreamFlag(1) && !CoXlTestStreamFlag(2) && !CoXlTestStreamFlag(3) && !CoXlTestStreamFlag(4) &&
        CoXlTestStreamFlag(5));
    CoXlTestStreamScan();
    int live = 0, due = 0; uint64_t unit = 0;
    CHECK(g_destroys == 2);
    CoXlTestStreamSlotState(0, &live, &due, &unit);
    CHECK(!live && due && unit == 0 && !CoXlTestStreamFlag(0));
    for (uint32_t k = 1; k <= 4; k++)
    {
        CoXlTestStreamSlotState(k, &live, &due, &unit);
        CHECK(live && unit == 0x1000u * (k + 1));
    }
    CHECK(CoXlTestVisibleWithNone(BX + 900.0f, AY) == CoXlTestVisibleAt(BX + 900.0f, AY) &&
        CoXlTestVisibleWithNone(BX + 1100.0f, AY) == CoXlTestVisibleAt(BX + 1100.0f, AY) &&
        CoXlTestVisibleWithNone(BX - 1100.0f, AY) == CoXlTestVisibleAt(BX - 1100.0f, AY));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile comes whose ground is within 0.5 m of what the stamps stood on: they stay");
    StreamGame();
    Stream(2);
    g_tileB[0] = g_tileB[1] = g_tileB[2] = g_tileB[3] = U16(40.3f);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 0 && !CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile comes 0.7 m off the ground the stamps stood on: they go");
    StreamGame();
    Stream(2);
    g_tileB[0] = g_tileB[1] = g_tileB[2] = g_tileB[3] = U16(40.7f);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 1 && CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a stamp that stood on the level's heightfield gets a grid: it goes when the two differ there");
    StreamGame();
    Stream(2);
    g_levelData[2] = 39.0f; /* x < 0, y < 0: column 0, row 0, counted from the far edge: index 2 */
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX - 1400.0f, AY + 300.0f, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 1 && CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile goes: the stamps on its ground go, the rest stay");
    StreamGame();
    Stream(4);
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);         /* on it: its 30 m, then the level's 40 m */
    Slot(1, BX - 1400.0f, AY, 20.0f, 0x2000, 0);        /* its grid's coarse 40 m, then the level's 40 m */
    Slot(2, BX + 1100.0f, AY, 20.0f, 0x3000, 0);        /* the drop tile's square */
    CHECK(CoXlTestTileGo(b) == 1);
    CHECK(CoXlTestStreamFlag(0) && !CoXlTestStreamFlag(1) && !CoXlTestStreamFlag(2));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a scan takes the least seen first: past the distance a stamp is drawn to, then the farthest for its size");
    StreamGame();
    Stream(33);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    const float ox = BX - 900.0f;
    Slot(0, BX, AY, 19.0f, 0x1000, 0);                  /* 900 m away, drawn to ~830 m: unseen */
    Slot(1, BX + 100.0f, AY, 52.0f, 0x2000, 0);         /* 1000 m away, drawn to ~1.2 km: seen */
    for (uint32_t k = 2; k < 33; k++) Slot(k, BX, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); /* small and unseen */
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 33);
    CoXlTestStreamObserver(1, ox, AY);
    CoXlTestStreamScan();
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    CHECK(g_destroys == 32);
    CoXlTestStreamSlotState(0, &live, &due, &unit);
    CHECK(!live && unit == 0);
    CoXlTestStreamSlotState(1, &live, &due, &unit);
    CHECK(live && unit == 0x2000 && CoXlTestStreamFlag(1));
    CoXlTestStreamScan();
    CHECK(g_destroys == 33);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a level location's new unit waits under the window's budgets");
    StreamGame();
    Stream(2);
    Budgets(500000, 100);
    CHECK(CoXlTestRespawnRoom(0, 100.0f * 100.0f));
    Budgets(900000, 100);
    CHECK(!CoXlTestRespawnRoom(0, 100.0f * 100.0f));                  /* bodies over 80 % */
    Budgets(500000, 900);
    CHECK(!CoXlTestRespawnRoom(0, 100.0f * 100.0f));                  /* navmesh cutters over 80 % */
    Budgets(500000, 100);
    CHECK(!CoXlTestRespawnRoom(0, 1000.0f * 1000.0f));                /* far: the culling pools do not read on the fake game */
    Budgets(500000, 100);
    uint32_t held = CoXlTestSpawnDue(0, 100.0f * 100.0f); /* the passes held so far: the refusals above */
    CHECK(held == 3);
    Budgets(900000, 100);
    CHECK(CoXlTestSpawnDue(0, 100.0f * 100.0f) == held + 1 && CoXlTestSpawnDue(0, 100.0f * 100.0f) == held + 2);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    if (g_failed) { printf("layergrid: %d checks failed\n", g_failed); return 1; }
    printf("layergrid: all cases passed\n");
    return 0;
}
