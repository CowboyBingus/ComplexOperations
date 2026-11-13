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
uint32_t CoXlTestStampsReplace(float cx, float cy);
int CoXlTestStreamFlag(uint32_t i);
void CoXlTestStreamScan(void);
void CoXlTestStreamSlotState(uint32_t i, int* live, int* due, uint64_t* unit);
int CoXlTestLayerWhy(uint64_t unit, uint64_t field, int* slot);
float CoXlTestTraceGridAt(int slot, float x, float y);
void CoXlTestProbePlayer(int count, float x, float y);
void CoXlTestProbe(char kind, float x, float y, float used, int why);
void CoXlTestProbeCounts(long* reads, long* off, long* written);

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

    Start("the streamed stamps on a new tile's ground go out and come back; the rest stay");
    StreamGame();
    static uint8_t context[0x100], array[4 * 0xE0];
    uint64_t world = 0xABC0;
    memcpy(context + 0x40, &world, 8);
    CoXlTestStreamSetup((uintptr_t)g_fake.image, context, array, 4);
    CoXlTestStreamSlot(0, BX + 900.0f, AY, 0x1000, 0);        /* on the new tile's ground (its band) */
    CoXlTestStreamSlot(1, BX + 1600.0f, AY, 0x2000, 0);       /* the drop tile's interior: past the new tile's span and reach */
    CoXlTestStreamSlot(2, BX, AY, 0x3000, 1);                 /* a mission's stamp: never streamed out */
    CoXlTestStreamSlot(3, BX - 1400.0f, AY + 300.0f, 0x4000, 0); /* within the span's reach on the far side */
    CHECK(CoXlTestStampsReplace(BX, AY) == 2);
    CHECK(CoXlTestStreamFlag(0) && !CoXlTestStreamFlag(1) && !CoXlTestStreamFlag(2) && CoXlTestStreamFlag(3));
    CoXlTestStreamScan();
    int live = 0, due = 0; uint64_t unit = 0;
    CHECK(g_destroys == 2);
    CoXlTestStreamSlotState(0, &live, &due, &unit);
    CHECK(!live && due && unit == 0 && !CoXlTestStreamFlag(0));
    CoXlTestStreamSlotState(1, &live, &due, &unit);
    CHECK(live && unit == 0x2000);
    CoXlTestStreamSlotState(2, &live, &due, &unit);
    CHECK(live && unit == 0x3000);
    CoXlTestStreamSlotState(3, &live, &due, &unit);
    CHECK(!live && due && unit == 0);

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

    if (g_failed) { printf("layergrid: %d checks failed\n", g_failed); return 1; }
    printf("layergrid: all cases passed\n");
    return 0;
}
