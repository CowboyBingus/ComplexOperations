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
int CoXlTestLayerWhy(uint64_t unit, uint64_t field, int* slot);
float CoXlTestTraceGridAt(int slot, float x, float y);
void CoXlTestProbePlayer(int count, float x, float y);
void CoXlTestProbe(char kind, float x, float y, float used, int why);
void CoXlTestProbeCounts(long* reads, long* off, long* written);
uint64_t CoXlTestLayerSpawn(uint64_t unit, uint64_t count, uint64_t field);
uint64_t CoXlTestThunk(int which, float* out, uint64_t* r9, uint64_t* fifth);
void CoXlTestMembers(uint64_t desc, const float* poses, uint32_t count);
void CoXlTestMembersNote(void);
uint64_t CoXlTestLayerSnap(uint64_t unit, uint64_t field);
void CoXlTestLayerSpawnInstall(uintptr_t exe);
uintptr_t CoXlTestLayerSpawnDetour(void);
uintptr_t CoXlTestLayerSpawnOriginal(void);
int CoXlTestGridEnter(uint64_t desc);
void CoXlTestGridLeave(int k);
const float* CoXlTestHeightCopy(void);

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
static uint16_t g_coarse[4], g_tileA[4], g_tileB[4], g_tileC[4];
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

/* A stamp's level unit for the layer spawn (exe 0xDBEC0): place +0x50, its SpeedTree groups +0x3D0, resource +0x10
 * whose particle effects are counted at +0x48. */
static uint8_t g_spawnUnit[0x400], g_spawnRes[0x100];
static uint64_t SpawnUnit(float x, float y, uint32_t trees, uint32_t effects)
{
    memset(g_spawnUnit, 0, sizeof g_spawnUnit); memset(g_spawnRes, 0, sizeof g_spawnRes);
    float at[2] = {x, y};
    uint64_t res = (uint64_t)(uintptr_t)g_spawnRes;
    memcpy(g_spawnUnit + 0x50, at, 8); memcpy(g_spawnUnit + 0x3D0, &trees, 4); memcpy(g_spawnUnit + 0x10, &res, 8); memcpy(g_spawnRes + 0x48, &effects, 4);
    return (uint64_t)(uintptr_t)g_spawnUnit;
}

/* Hex bytes written into the fake engine image at rva. */
static void Bytes(uint8_t* exe, uint32_t rva, const char* hex)
{
    for (const char* c = hex; *c;)
    {
        if (*c == ' ') { c++; continue; }
        unsigned v = 0; sscanf(c, "%2x", &v); exe[rva++] = (uint8_t)v; c += 2;
    }
}
static void SpawnPins(uint8_t* exe)
{
    Bytes(exe, 0xDBEC0u, "45 85 C0 0F 84 B4 23 00 00 48 8B C4 4C 89 48 20");
    Bytes(exe, 0xDBEE2u, "48 8D A8 C8 F5 FF FF");
    Bytes(exe, 0xDC47Au, "48 8B 85 58 0A 00 00");
    Bytes(exe, 0xDD871u, "48 8B 95 58 0A 00 00");
    Bytes(exe, 0x20BFD3u, "48 8B 8D C8 0A 00 00 48 89 8D A0 00 00 00");
    Bytes(exe, 0x32E63Au, "E8 81 D8 DA FF 48 83 C4 58 C3");
    Bytes(exe, 0x32E69Au, "E8 21 D8 DA FF 48 83 C4 58 C3");
}
/* Where the call at rva now goes: through a `jmp [rip+0]` stub, its target; else the call's own target. */
static uintptr_t CallGoesTo(const uint8_t* exe, uint32_t rva)
{
    int32_t rel; memcpy(&rel, exe + rva + 1, 4);
    const uint8_t* to = exe + rva + 5 + rel;
    if (exe[rva] != 0xE8) return 0;
    if (to >= exe && to < exe + FAKE_EXE_SIZE) return (uintptr_t)to;
    if (to[0] != 0xFF || to[1] != 0x25 || to[2] || to[3] || to[4] || to[5]) return 0;
    uint64_t target; memcpy(&target, to + 6, 8);
    return (uintptr_t)target;
}

static uint8_t* g_lg;
static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    CoXlTestWorldLevel((uintptr_t)g_lg, 0);
    CoXlTestGenLevel(8.0f);
    for (int k = 0; k < 4; k++) { g_coarse[k] = U16(40.0f); g_tileA[k] = U16(45.0f); g_tileB[k] = U16(30.0f); g_tileC[k] = U16(20.0f); }
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

    Start("the research trace reads a grid as the game does and names why a stamp keeps the level's heights");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    int c = CoXlTestTileAddAt(AX, CY2, g_tileC, 2, SPAN); /* south: the drop grid differs north and south */
    CoXlTestLayerTile(c, 1, AX, CY2);
    fa = Field(2100.0f, AY);
    int differ = 0;
    for (float x = 1990.0f; x <= 2110.0f; x += 0.37f) /* across the two tiles' meeting, the trace's read is the game's */
        CHECK(CoXlTestTraceGridAt(a, x, AY + 11.7f) == Read(fa, x, AY + 11.7f));
    for (float y = AY - 1700.0f; y <= AY + 1700.0f; y += 3.1f)
    {
        CHECK(CoXlTestTraceGridAt(a, AX + 5.3f, y) == Read(fa, AX + 5.3f, y));
        differ += !Near(Read(fa, AX + 5.3f, y), Read(fa, AX + 5.3f, 2.0f * AY - y));
    }
    CHECK(differ > 100); /* the grid is not symmetric about its centre, so a flipped row shows */
    int slot = -2;
    float at[2] = {2100.0f, AY};
    memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 0 && slot == a);
    at[0] = BX - 1500.0f; memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 5 && slot == -1); /* no grid */
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, 0, &slot) == 1);
    float wide[2] = {4096.0f, 4096.0f};
    memcpy(g_levelDesc + 0x14, wide, 8); /* not the level's extent */
    at[0] = 2100.0f; memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 3);

    Start("a stamp's layer spawn by a tile hands its grid to every member, SpeedTree and particles included");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    for (int k = 0; k < 4; k++) g_levelData[k] = 52.0f; /* the level's heights: 7 m over the tile, as at the user's drop */
    CoXlTestHeightKept(g_levelData, 2, 2, 16384.0f);
    CoXlTestProbePlayer(1, 2120.0f, AY);
    uint64_t level = (uint64_t)(uintptr_t)g_levelDesc;
    uint64_t given = CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 2, 1), 3, level);
    CHECK(given != level && given == Field(2100.0f, AY));
    CHECK(Near(Read(given, 2600.0f, AY), 45.0f) && Near(Read(given, 1900.0f, AY), 40.0f)); /* what an inline read there takes */
    CHECK(fake_host_logged("layer spawns (the first): 1, 1 of them on a tile's grid for every member (1 with SpeedTree, 1 with particle effects); "
        "1 within 300 m of a player, their members 0."));
    CHECK(fake_host_logged(" 0 over 0.5 m; the drawn ground within 16 m of their centres up to "));
    CHECK(!fake_host_logged("layer spawn off the ground"));
    CHECK(CoXlTestLayerSpawn(SpawnUnit(1126.0f, AY, 2, 1), 3, level) == level);  /* no tile within reach: the level's own */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 3, 0) == 0);          /* no heightfield (0xDAEC8, 0xDB4F3) */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 3, given) == given);  /* a grid already: kept */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 0, level) == level);  /* no members: the engine returns at once */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2600.0f, AY, 3, 0), 3, level) == given);  /* counted: SpeedTree, no particles */
    {
        /* Prefab members' final matrices: set on the level's 52 m beside a tile drawn at about 43 m (floating), set on the
         * grid, and one 8 m over the ground it was given (a member authored above its stamp's ground). */
        float poses[3][16] = {{0}};
        for (int i = 0; i < 3; i++) { poses[i][0] = poses[i][5] = poses[i][10] = poses[i][15] = 1.0f; poses[i][12] = 2100.0f; poses[i][13] = AY; }
        poses[0][14] = 52.0f;
        poses[1][13] = AY + 5.0f; poses[1][14] = Read(given, 2100.0f, AY + 5.0f);
        poses[2][13] = AY - 5.0f; poses[2][14] = 60.0f;
        CoXlTestMembers(level, poses[0], 1);
        CoXlTestMembers(given, poses[1], 2);
        CoXlTestMembersNote();
        CHECK(fake_host_logged("layer member off the ground (the level's heights): at (2100.0, -4094.9), 20 m from a player: z 52.00, given 52.00, drawn 4"));
        CHECK(fake_host_logged("layer members (a test): 3 placed by 2 prefab batches, 3 within 300 m of a player; 2 set on the ground they were given, "
            "1 of them over 0.5 m above the drawn ground and 0 under it (worst "));
        CHECK(!fake_host_logged("layer member off the ground (a tile's grid)"));
    }
    CoXlTestLayerTile(a, 1, AX, AY);
    CHECK(fake_host_logged("layer spawns (a tile came in): 3, 2 of them on a tile's grid for every member (2 with SpeedTree, 1 with particle effects)"));
    CHECK(fake_host_logged("layer spawns (a tile came in): 0 ran on the game thread, the rest on 1 other thread(s); 0 spawned on the level's heights by a tile"));

    g_case = "a stamp by a tile whose grid is not ready spawns on the level's heights, is listed and logged off the ground";
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN); /* in, its grid not made yet */
    CoXlTestProbePlayer(1, BX + 20.0f, AY);
    CHECK(CoXlTestLayerSpawn(SpawnUnit(BX, AY, 1, 0), 3, level) == level);
    CHECK(fake_host_logged("layer spawn off the ground: the stamp at (1026.2, -4094.9), 20 m from a player: its members read 52.00 (the level's heights)"));
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("1 spawned on the level's heights by a tile, 0 of them since put back on a grid, 0 kept (not the streamer's to put back), 1 waiting"));

    g_case = "a listed stamp the streamer holds goes out and comes back once a tile's grid covers it";
    StreamGame();
    Stream(1);
    Slot(0, BX, AY, 20.0f, (uint64_t)(uintptr_t)g_spawnUnit, 0);
    long destroys = g_destroys;
    CoXlTestStreamScan();
    int pLive = 0, pDue = 0; uint64_t pUnit = 0;
    CoXlTestStreamSlotState(0, &pLive, &pDue, &pUnit);
    CHECK(g_destroys == destroys + 1 && !pLive && pDue);
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("1 spawned on the level's heights by a tile, 1 of them since put back on a grid, 0 kept (not the streamer's to put back), 0 waiting"));

    g_case = "a grid write waits for the spawns reading it and says when one is still reading";
    uint64_t current = Field(2100.0f, AY);
    int k = CoXlTestGridEnter(current);
    CHECK(k == a);
    CoXlTestLayerTile(a, 1, AX, AY);
    CHECK(fake_host_logged("waited for layer spawns reading them, 1 went on with one still reading"));
    CHECK(CoXlTestGridEnter(current) < 0); /* its layout was written again: the old descriptor is not handed on */
    CoXlTestGridLeave(k);
    k = CoXlTestGridEnter(Field(2100.0f, AY));
    CHECK(k == a);
    CoXlTestGridLeave(k);
    CoXlTestLayerTile(b, 0, BX, AY);
    CoXlTestTileRemove(b);

    g_case = "0xDFA60 after the spawn keeps the grid it is given and never keeps it as the level's heightfield";
    int slot2 = -2;
    CHECK(CoXlTestLayerSnap(SpawnUnit(2100.0f, AY, 0, 0), given) == given);
    CHECK(CoXlTestHeightCopy() == g_levelData);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_spawnUnit, given, &slot2) == 0 && slot2 == a);
    CHECK(CoXlTestLayerSnap(SpawnUnit(2100.0f, AY, 0, 0), level) == Field(2100.0f, AY)); /* a placed-unit call on its own still gets the grid */
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestProbePlayer(0, 0.0f, 0.0f);

    Start("the layer spawn's two callers with a heightfield go through it, only on the reviewed code");
    uint8_t* exe = fake_host_exe();
    DWORD old;
    VirtualProtect(exe, FAKE_EXE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    g_fake.strictCode = 1;
    SpawnPins(exe);
    CoXlTestLayerSpawnInstall((uintptr_t)exe);
    CHECK(CoXlTestLayerSpawnOriginal() == (uintptr_t)exe + 0xDBEC0u);
    CHECK(CallGoesTo(exe, 0x32E63Au) == CoXlTestLayerSpawnDetour() && CallGoesTo(exe, 0x32E69Au) == CoXlTestLayerSpawnDetour());
    CHECK(fake_host_logged("a stamp layer by a tile spawns on its grid for every member"));
    exe = fake_host_exe();
    SpawnPins(exe);
    exe[0xDC47Au + 3] = 0x50; /* SpeedTree's descriptor from another slot: not the reviewed code */
    CoXlTestLayerSpawnInstall((uintptr_t)exe);
    CHECK(CoXlTestLayerSpawnOriginal() == 0);
    CHECK(CallGoesTo(exe, 0x32E63Au) == (uintptr_t)exe + 0xDBEC0u && CallGoesTo(exe, 0x32E69Au) == (uintptr_t)exe + 0xDBEC0u);
    CHECK(fake_host_logged("the layer spawn is not the reviewed code"));
    g_fake.strictCode = 0;

    Start("every build logs the layer reads near a player, each one off the ground as drawn, within a line budget");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    long reads = 0, off = 0, written = 0;
    CoXlTestProbePlayer(1, AX, AY);
    CoXlTestProbe('H', AX + 10.0f, AY, 45.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 1 && off == 0 && written == 1);
    CHECK(fake_host_logged("layer probe: ok H 0x0 at (3084.7, -4094.9), 10 m from a player: used 45.00, drawn 45.0"));
    CoXlTestProbe('S', AX + 250.0f, AY, 45.45f, 0); /* under 0.5 m: one of the plain reads, not written */
    CoXlTestProbe('S', AX, AY + 250.0f, 52.0f, 5);  /* the level's heights, 7 m over the tile */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 3 && off == 1 && written == 2);
    CHECK(fake_host_logged("layer probe: OFF S 0x0 at (3074.7, -3844.9), 250 m from a player: used 52.00, drawn 45.0"));
    CHECK(fake_host_logged("source 0 (no tile's grid)"));
    CoXlTestProbe('S', AX + 350.0f, AY, 99.0f, 0);
    CoXlTestProbe('S', AX + 212.2f, AY + 212.2f, 99.0f, 0); /* 300.1 m on the diagonal: a circle, not a square */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 3 && off == 1);
    CoXlTestProbePlayer(0, AX, AY - 500.0f); /* no one has landed: the start area */
    CoXlTestProbe('H', AX, AY - 600.0f, 45.0f, 0);
    CoXlTestProbe('H', AX, AY + 400.0f, 45.0f, 0); /* 900 m from it */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 4 && off == 1);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("layer probe (a tile came in): 4 reads within 300 m of a player, 1 of them over 0.5 m off the ground as drawn (worst 6.99 m); ok 3, no tile's grid 1; 2 of 400 lines written"));
    for (int k = 0; k < 300; k++) CoXlTestProbe('S', AX, AY - 500.0f, 60.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 304 && off == 301 && written == 153); /* the first 150 off, then the 192nd and the 256th */
    for (int k = 0; k < 19700; k++) CoXlTestProbe('S', AX, AY - 500.0f, 60.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 20004 && off == 20001 && written == 400); /* past the budget */

    Start("the leaf thunks keep xmm1-xmm5 for the game's callers and pass the return address or the fifth argument on");
    float out[8]; uint64_t r9 = 0, fifth = 0;
    CHECK(CoXlTestThunk(0, out, &r9, &fifth) == 77);
    CHECK(out[0] == 7.0f && out[1] == 1.0f && out[2] == 2.0f && out[3] == 3.0f && out[4] == 4.0f && out[5] == 5.0f);
    CHECK(r9 != 0 && r9 == *(uint64_t*)&out[6]); /* the sampler's caller, for its RVA */
    CHECK(CoXlTestThunk(1, out, &r9, &fifth) == 77);
    CHECK(out[0] == 0.5f && out[1] == 1.0f && out[2] == 2.0f && out[3] == 3.0f && out[4] == 4.0f && out[5] == 5.0f);
    CHECK(r9 == 0x44 && fifth == 0x5555);

    if (g_failed) { printf("layergrid: %d checks failed\n", g_failed); return 1; }
    printf("layergrid: all cases passed\n");
    return 0;
}
