/* Stock's 8 x 8 tiles on the grid of 32 x 32 (xl/co_navtrim.h), by running the engine's own bytes in the process:
 * - the point-to-cell stub against the original step on stock's 8 x 8 grid, plus 12, over a 0.25 m lattice, every cell
 *   edge and its neighbours one ulp away, far points and non-numbers; and bit-equal to the original when disarmed;
 * - the set-up stub's array against S1's table, which the engine's fill gives at 8 x 8; the fill's own when disarmed;
 * - both stubs keep every register the original keeps, and the stack;
 * - arming across the ship, a normal level of 128 m and of 256 m, a complex level and the ship again;
 * - a changed byte in any pinned range refuses the stubs with a reason, and every normal level then logs that it is
 *   not armed and keeps the whole grid. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"
#include "xl/co_navtrim.h"
#include "xl/co_normal.h"

void CoXlTestLwNavPins(uint8_t* exe);
int CoXlTestLwNavTick(uintptr_t game);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define LEVEL 0x1500000u
#define WORLDS 0x100000u /* in the exe: the pointers, then world 0 at +0x1000 */
#define WORLD (WORLDS + 0x1000u)
#define ARRAY (WORLD + 0x10000u)

/* cell(fn, grid, out, xy): the step called as the engine calls it (x in xmm1, y in xmm2), with sentinels in rbx rsi rdi rbp r9 r10 r11
 * xmm3..5; out gets eax, those registers and the change of rsp. set(world, count, site, out): rsi and eax as at
 * 0x7B744C, sentinels in rbx rdi rbp r8..r11. */
static const uint8_t kCellThunk[] = {
    0x53, 0x56, 0x57, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x4D, 0x89, 0xC4, 0x49, 0x89, 0xCD, 0x49, 0x89, 0xE6, 0xF3, 0x41, 0x0F, 0x10, 0x09,
    0xF3, 0x41, 0x0F, 0x10, 0x51, 0x04, 0x48, 0x89, 0xD1, 0x48, 0xBB, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x48, 0xBE, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x48, 0xBF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x48, 0xBD, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
    0x44, 0x49, 0xB9, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x49, 0xBA, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x49, 0xBB, 0x77,
    0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x48, 0xB8, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01, 0x66, 0x48, 0x0F, 0x6E, 0xD8, 0x66, 0x48,
    0x0F, 0x6E, 0xE0, 0x66, 0x48, 0x0F, 0x6E, 0xE8, 0x41, 0xFF, 0xD5, 0x49, 0x89, 0x04, 0x24, 0x49, 0x89, 0x5C, 0x24, 0x08, 0x49, 0x89, 0x74, 0x24,
    0x10, 0x49, 0x89, 0x7C, 0x24, 0x18, 0x49, 0x89, 0x6C, 0x24, 0x20, 0x4D, 0x89, 0x4C, 0x24, 0x28, 0x4D, 0x89, 0x54, 0x24, 0x30, 0x4D, 0x89, 0x5C,
    0x24, 0x38, 0x66, 0x41, 0x0F, 0xD6, 0x5C, 0x24, 0x40, 0x66, 0x41, 0x0F, 0xD6, 0x64, 0x24, 0x48, 0x66, 0x41, 0x0F, 0xD6, 0x6C, 0x24, 0x50, 0x49,
    0x29, 0xE6, 0x4D, 0x89, 0x74, 0x24, 0x58, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0x5F, 0x5E, 0x5B, 0xC3};
static const uint8_t kSetThunk[] = {
    0x53, 0x56, 0x57, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x4D, 0x89, 0xCC, 0x4D, 0x89, 0xC5, 0x49, 0x89, 0xE6, 0x48, 0x89, 0xCE, 0x89, 0xD0,
    0x48, 0xBB, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x48, 0xBF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x48, 0xBD, 0x44, 0x44,
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x49, 0xB8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x49, 0xB9, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x49, 0xBA, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x49, 0xBB, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x41, 0xFF,
    0xD5, 0x49, 0x89, 0x04, 0x24, 0x49, 0x89, 0x5C, 0x24, 0x08, 0x49, 0x89, 0x74, 0x24, 0x10, 0x49, 0x89, 0x7C, 0x24, 0x18, 0x49, 0x89, 0x6C, 0x24,
    0x20, 0x4D, 0x89, 0x4C, 0x24, 0x28, 0x4D, 0x89, 0x54, 0x24, 0x30, 0x4D, 0x89, 0x5C, 0x24, 0x38, 0x4D, 0x89, 0x44, 0x24, 0x40, 0x49, 0x29, 0xE6,
    0x4D, 0x89, 0x74, 0x24, 0x58, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0x5F, 0x5E, 0x5B, 0xC3};
typedef void (*CellThunk)(uintptr_t fn, const void* grid, uint64_t* out, const float* xy);
typedef void (*SetThunk)(uintptr_t world, uint32_t count, uintptr_t site, uint64_t* out);
typedef uint32_t (*FillFn)(void* array, const void* grid);

static uint8_t* g_exe;
static uint8_t* g_code; /* the thunks, and the original step outside the exe */
static uint8_t* Reset(const char* name)
{
    DWORD old;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_nav_test_reset();
    fake_stock_nav(g_fake.image);
    g_exe = g_fake.exe;
    VirtualProtect(g_exe, FAKE_EXE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    return g_fake.image;
}

static void World(void)
{
    const uint64_t worlds = (uint64_t)(uintptr_t)(g_exe + WORLDS), world = (uint64_t)(uintptr_t)(g_exe + WORLD), array = (uint64_t)(uintptr_t)(g_exe + ARRAY);
    const uint32_t count = 1, entries = 1024;
    const uint16_t grid[2] = {CO_NAV_CELLS, CO_NAV_CELLS};
    const uint64_t back = array + 12u * (entries + 100u);
    memcpy(g_exe + CO_NAV_WORLDS, &worlds, 8); memcpy(g_exe + CO_NAV_COUNT, &count, 4); memcpy(g_exe + WORLDS, &world, 8);
    memcpy(g_exe + WORLD + 0x6710, grid, 4); memcpy(g_exe + WORLD + 0x6758, &array, 8); memcpy(g_exe + WORLD + 0x6750, &entries, 4);
    memcpy(g_exe + WORLD + 0x78, &back, 8);
    memcpy(g_exe + WORLD + 0x6708, &array, 8); /* the priority array; the 12-byte one behind the tiles' list is not read here */
}
static uintptr_t Level(float disk, float radius)
{
    memcpy(g_fake.image + LEVEL + 0x2D7C, &disk, 4); memcpy(g_fake.image + LEVEL + 0x2D8C, &radius, 4);
    return (uintptr_t)g_fake.image + LEVEL;
}
static float F(uint32_t at) { float f; memcpy(&f, g_exe + WORLD + at, 4); return f; }

/* A grid as the step reads it: W, H, the tile, low x, low y, high x, high y. */
typedef struct { uint16_t w, h; uint32_t bits; float tile, low[2], high[2]; uint8_t rest[0x20]; } Grid;
static Grid Stock(float low, float high) { Grid g = {8, 8, 3, (high - low) / 8.0f, {low, low}, {high, high}, {0}}; return g; }

static uint32_t Call(uintptr_t fn, const void* grid, float x, float y, int* kept)
{
    uint64_t out[12];
    const float xy[2] = {x, y};
    ((CellThunk)(void*)g_code)(fn, grid, out, xy);
    const uint64_t want[] = {0x1111111111111111ull, 0x2222222222222222ull, 0x3333333333333333ull, 0x4444444444444444ull, 0x5555555555555555ull,
        0x6666666666666666ull, 0x7777777777777777ull, 0x0123456789ABCDEFull, 0x0123456789ABCDEFull, 0x0123456789ABCDEFull, 0};
    if (memcmp(out + 1, want, sizeof want) != 0) *kept = 0;
    return (uint32_t)out[0];
}

/* The stub at the exe's step, armed for this world's grid, against the original on stock's grid plus 12 a side. */
static int g_points, g_wrong, g_kept;
static void Point(const Grid* stock, float x, float y)
{
    const uintptr_t original = (uintptr_t)g_code + 0x400u, stub = (uintptr_t)g_exe + CO_NAVTRIM_CELL;
    const uint32_t want = Call(original, stock, x, y, &g_kept) + 0x000C000Cu, got = Call(stub, g_exe + WORLD + 0x6710, x, y, &g_kept);
    g_points++;
    if (got != want && g_wrong++ < 5) printf("  cell of (%.9g, %.9g): 0x%08X, stock's + 12 0x%08X\n", (double)x, (double)y, got, want);
}
static void Points(const Grid* stock)
{
    const float distant[] = {600.0f, 2048.0f, 5000.0f, 1100.0f, 1.0e30f, INFINITY};
    for (int i = -4400; i <= 4400; i++) { Point(stock, (float)i * 0.25f, 3.0f); Point(stock, -7.0f, (float)i * 0.25f); }
    for (int i = -44; i <= 44; i++)
        for (int j = -44; j <= 44; j++) Point(stock, (float)i * 25.0f + 0.125f, (float)j * 25.0f - 0.375f);
    for (int k = 0; k <= 8; k++)
    {
        const float edge = stock->low[0] + (float)k * stock->tile;
        const float edgeAt[3] = {nextafterf(edge, -INFINITY), edge, nextafterf(edge, INFINITY)};
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) Point(stock, edgeAt[a], edgeAt[b]);
    }
    for (size_t i = 0; i < sizeof distant / sizeof distant[0]; i++)
        for (int s = -1; s <= 1; s += 2) { Point(stock, s * distant[i], 0.0f); Point(stock, 0.0f, s * distant[i]); Point(stock, s * distant[i], -s * distant[i]); }
    Point(stock, NAN, 0.0f); Point(stock, 0.0f, NAN); Point(stock, NAN, NAN);
}
/* Disarmed: the stub on `grid` equals the original on it, bit for bit. */
static int Through(const void* grid)
{
    const uintptr_t original = (uintptr_t)g_code + 0x400u, stub = (uintptr_t)g_exe + CO_NAVTRIM_CELL;
    const float xs[] = {-5000.0f, -2047.75f, -600.0f, -511.75f, -0.5f, 0.0f, 3.25f, 384.25f, 512.25f, 600.0f, 2048.25f, 9000.0f, NAN};
    int same = 1, kept = 1;
    for (size_t i = 0; i < sizeof xs / sizeof xs[0]; i++)
        for (size_t j = 0; j < sizeof xs / sizeof xs[0]; j++)
            same &= Call(stub, grid, xs[i], xs[j], &kept) == Call(original, grid, xs[i], xs[j], &kept);
    for (int i = -4400; i <= 4400; i += 3) same &= Call(stub, grid, (float)i * 0.25f, (float)-i, &kept) == Call(original, grid, (float)i * 0.25f, (float)-i, &kept);
    return same && kept;
}

/* The set-up stub run as the engine reaches it; the way on (0x7B7452) is a ret for the run and the engine's after. */
static uint32_t SetUp(uint32_t count, int* kept)
{
    uint64_t out[12];
    const uint8_t back = g_exe[CO_NAVTRIM_SET_BACK];
    g_exe[CO_NAVTRIM_SET_BACK] = 0xC3;
    ((SetThunk)(void*)(g_code + 0x200))((uintptr_t)g_exe + WORLD, count, (uintptr_t)g_exe + CO_NAVTRIM_SET_SITE, out);
    g_exe[CO_NAVTRIM_SET_BACK] = back;
    const uint64_t want[] = {0x1111111111111111ull, (uint64_t)(uintptr_t)(g_exe + WORLD), 0x3333333333333333ull, 0x4444444444444444ull,
        0x5555555555555555ull, 0x6666666666666666ull, 0x7777777777777777ull, 0x8888888888888888ull};
    *kept = memcmp(out + 1, want, sizeof want) == 0 && out[11] == 0;
    uint32_t left;
    memcpy(&left, g_exe + WORLD + 0xC4, 4);
    return left;
}
/* The engine's fill on a grid of `cells` into `array`; its count. */
static uint32_t Fill(int32_t* array, uint16_t cells)
{
    Grid g = Stock(-511.75f, 512.25f);
    g.w = g.h = cells;
    return ((FillFn)(void*)(g_exe + CO_NAVTRIM_FILL))(array, &g);
}

int main(void)
{
    DWORD old;
    g_code = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    memcpy(g_code, kCellThunk, sizeof kCellThunk);
    memcpy(g_code + 0x200, kSetThunk, sizeof kSetThunk);
    memcpy(g_code + 0x400, kCoNavCell, sizeof kCoNavCell); /* the original step, run outside the exe */
    const float hellforge = 1022.0f / 1024.0f, key = co_disk_key();

    uint8_t* image = Reset("S1: the engine's fill at 8 x 8");
    {
        int32_t* array = (int32_t*)(g_exe + ARRAY);
        CHECK(Fill(array, 8) == 64);
        int same = 1;
        for (int c = 0; c < 64; c++) same &= array[2 * c] == c && array[2 * c + 1] == kCoNavStockPrio[c] && kCoNavStockPrio[c] > 0;
        CHECK(same);
        CHECK(Fill(array, 32) == 1024); /* every cell of the mod's grid above 0: why the outer ones are built without the stub */
    }

    g_case = "the install";
    const uintptr_t game = (uintptr_t)image;
    uint8_t before[16];
    memcpy(before, g_exe + CO_NAVTRIM_CELL, 16);
    CHECK(co_nav_install(game) == NULL && co_navtrim_refused() == NULL && g_fake.writes == 4 && co_navtrim_test_block() != 0);
    {
        const uintptr_t block = co_navtrim_test_block();
        int32_t rel;
        memcpy(&rel, g_exe + CO_NAVTRIM_CELL + 1, 4);
        CHECK(g_exe[CO_NAVTRIM_CELL] == 0xE9 && (uintptr_t)g_exe + CO_NAVTRIM_CELL + 5u + (intptr_t)rel == block);
        CHECK(memcmp(g_exe + CO_NAVTRIM_CELL + 5, before + 5, 11) == 0); /* the bytes behind the jump stay the engine's */
        memcpy(&rel, g_exe + CO_NAVTRIM_SET_SITE + 1, 4);
        CHECK(g_exe[CO_NAVTRIM_SET_SITE] == 0xE9 && (uintptr_t)g_exe + CO_NAVTRIM_SET_SITE + 5u + (intptr_t)rel == block + 0x100u);
        CHECK(g_exe[CO_NAVTRIM_SET_SITE + 5] == kCoNavSet[0xC + 5] && memcmp(g_exe + CO_NAVTRIM_SET_BACK, kCoNavSet + 0x12, 0xE) == 0);
        CHECK(co_nav_install(game) == NULL && g_fake.writes == 4);
    }

    for (int scale = 0; scale < 2; scale++)
    {
        g_case = scale ? "the cell step, 256 m tiles" : "the cell step, 128 m tiles";
        World();
        co_nav_retile(0, scale ? Level(hellforge, 1023.0f) : Level(0.5f, 600.0f));
        const float low = scale ? -1023.75f : -511.75f, high = scale ? 1024.25f : 512.25f;
        const Grid stock = Stock(low, high);
        const float tile = scale ? 256.0f : 128.0f;
        CHECK(co_navtrim_armed(0) == (uintptr_t)g_exe + WORLD && F(0x6718) == tile && F(0x671C) == low - 12.0f * tile && F(0x6728) == high + 12.0f * tile);
        g_points = g_wrong = 0; g_kept = 1;
        Points(&stock);
        CHECK(g_wrong == 0 && g_kept && g_points > 17000);
        Grid other;
        memcpy(&other, g_exe + WORLD + 0x6710, sizeof other); /* the same fields at another address: another grid */
        CHECK(Through(&other));
    }
    g_case = "the cell step, disarmed";
    co_nav_retile(0, Level(key, 8074.4f));
    CHECK(co_navtrim_armed(0) == 0 && Through(g_exe + WORLD + 0x6710));

    g_case = "the set-up stub";
    {
        int32_t* array = (int32_t*)(g_exe + ARRAY);
        int kept = 0, same = 1;
        co_nav_retile(0, 0); /* the ship: armed */
        CHECK(Fill(array, 32) == 1024 && SetUp(1024, &kept) == 64 && kept);
        for (int c = 0; c < 1024; c++)
        {
            const int x = c % 32, y = c / 32, inner = x >= 12 && x < 20 && y >= 12 && y < 20;
            same &= array[2 * c] == c && array[2 * c + 1] == (inner ? kCoNavStockPrio[(y - 12) * 8 + x - 12] : -(1 << 20) - c);
        }
        CHECK(same);
        co_nav_retile(0, Level(key, 8074.4f)); /* complex: the fill's own and its count */
        int32_t* fill = malloc(1024 * 8);
        CHECK(Fill(array, 32) == 1024);
        memcpy(fill, array, 1024 * 8);
        CHECK(SetUp(1024, &kept) == 1024 && kept && memcmp(fill, array, 1024 * 8) == 0);
        free(fill);
    }

    g_case = "arm and disarm across levels";
    {
        const struct { uintptr_t level; float low, high, own; int armed; } steps[] = {
            {0, -2047.75f, 2048.25f, -511.75f, 1},                         /* the ship */
            {1, -2047.75f, 2048.25f, -511.75f, 1},                         /* a normal level, 128 m */
            {2, -4095.75f, 4096.25f, -1023.75f, 1},                        /* HellForge's level 11, 256 m */
            {3, -8190.0f, 8194.0f, -8190.0f, 0},                           /* a complex level */
            {0, -2047.75f, 2048.25f, -511.75f, 1}};                        /* the ship again */
        for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
        {
            const uintptr_t level = steps[i].level == 0 ? 0 : steps[i].level == 1 ? Level(0.5f, 600.0f) : steps[i].level == 2 ? Level(hellforge, 1023.0f) : Level(key, 8074.4f);
            co_nav_retile(0, level);
            const uintptr_t block = co_navtrim_test_block() + 0x1000u;
            float shadow[4];
            memcpy(shadow, (const void*)(block + 0x14), 16);
            CHECK((co_navtrim_armed(0) != 0) == steps[i].armed && F(0x671C) == steps[i].low && F(0x6728) == steps[i].high && F(0x08) == steps[i].own && F(0x0C) == steps[i].own);
            CHECK(!steps[i].armed || (shadow[0] == steps[i].own && shadow[3] == -steps[i].own + 0.5f && F(0x14) == -steps[i].own + 0.5f));
        }
        CHECK(!fake_host_logged("not armed"));
    }

    g_case = "a nav id past the stubs' slots";
    World();
    {
        const uint32_t count = 6;
        const uint64_t world = (uint64_t)(uintptr_t)(g_exe + WORLD);
        memcpy(g_exe + CO_NAV_COUNT, &count, 4); memcpy(g_exe + WORLDS + 8 * 5, &world, 8);
        co_nav_retile(5, Level(0.5f, 600.0f));
        CHECK(fake_host_logged("nav world 5 is not armed") && F(0x08) == -2047.75f); /* the whole grid, as before the stubs */
    }

    {
        const struct { uint32_t at; const char* why; } pins[] = {{CO_NAVTRIM_CELL + 0x63u, "point-to-cell"}, {CO_NAVTRIM_CELL + 0x7Au, "point-to-cell"},
            {CO_NAVTRIM_SET + 0xEu, "set-up"}, {CO_NAVTRIM_SET + 0x8u, "set-up"}, {CO_NAVTRIM_FILL + 0x100u, "priority fill"}, {0x16A9D7Cu + 2u, "priority constants"}};
        for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++)
        {
            image = Reset("a pinned byte is another");
            g_exe[pins[i].at] ^= 0x04;
            uint8_t* was = malloc(FAKE_EXE_SIZE);
            memcpy(was, g_exe, FAKE_EXE_SIZE);
            CHECK(co_nav_install((uintptr_t)image) == NULL && co_navtrim_refused() != NULL && strstr(co_navtrim_refused(), pins[i].why) != NULL);
            CHECK(memcmp(was, g_exe, CO_NAV_TILE_READ) == 0 && g_fake.writes == 2 && fake_host_logged("normal levels build the whole 32 x 32 grid"));
            World();
            co_nav_retile(0, Level(0.5f, 600.0f));
            CHECK(fake_host_logged("nav world 0 is not armed") && F(0x08) == -2047.75f && F(0x671C) == -2047.75f);
            free(was);
        }
    }
    for (int fail = 2; fail < 4; fail++)
    {
        image = Reset("a stub that cannot be written");
        g_fake.failWriteAt = fail;
        CHECK(co_nav_install((uintptr_t)image) == NULL && co_navtrim_refused() != NULL);
        CHECK(memcmp(g_exe + CO_NAVTRIM_CELL, kCoNavCell, sizeof kCoNavCell) == 0 && memcmp(g_exe + CO_NAVTRIM_SET, kCoNavSet, sizeof kCoNavSet) == 0);
    }

    image = Reset("the plain form");
    memcpy(image + CO_NAV_SITE, kNavGame, sizeof kNavGame); memcpy(image + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame);
    CHECK(co_nav_plain((uintptr_t)image) && co_nav_install((uintptr_t)image) == NULL && co_navtrim_refused() == NULL);
    World();
    co_nav_retile(0, 0);
    CHECK(co_navtrim_armed(0) == (uintptr_t)g_exe + WORLD && F(0x08) == -511.75f);

    /* The living world pins 0x7ACDD0 (living_world.c kLwNavPins): it reads the engine's bytes, or the trim's jump into its
     * reviewed block; any third form refuses its nav boosts and rebuilds. */
    for (int form = 0; form < 4; form++)
    {
        static const char* const kForms[] = {"the living world's pin: the trim refused, the engine's bytes", "the living world's pin: the trim's jump",
            "the living world's pin: a jump elsewhere", "the living world's pin: the trim's block changed"};
        image = Reset(kForms[form]);
        CoXlTestLwNavPins(g_exe);
        if (form == 0) g_exe[CO_NAVTRIM_FILL + 0x100u] ^= 0x04;
        CHECK(co_nav_install((uintptr_t)image) == NULL && (co_navtrim_refused() == NULL) == (form != 0));
        if (form == 2) { int32_t rel; memcpy(&rel, g_exe + CO_NAVTRIM_CELL + 1, 4); rel += 0x10; memcpy(g_exe + CO_NAVTRIM_CELL + 1, &rel, 4); }
        if (form == 3)
        {
            uint8_t* block = (uint8_t*)co_navtrim_test_block();
            VirtualProtect(block, 0x1000, PAGE_EXECUTE_READWRITE, &old);
            block[0x11] ^= 0x01; /* the dispatch's step to the next slot */
        }
        const int want = form < 2 ? 1 : -1;
        CHECK(CoXlTestLwNavTick((uintptr_t)image) == want);
        CHECK(fake_host_logged("the navmesh rebuild code is not the reviewed one") == (want < 0));
        if (want > 0) CHECK(fake_host_logged("not an extra large nav grid in rebuild mode"));
        if (form == 3) { uint8_t* block = (uint8_t*)co_navtrim_test_block(); block[0x11] ^= 0x01; }
    }
    {
        g_case = "the living world's pin: the set-up site in a pinned range";
        uint8_t bytes[0x20];
        memcpy(bytes, g_exe + CO_NAVTRIM_SET, sizeof bytes);
        co_navtrim_unpatched((uintptr_t)g_exe + CO_NAVTRIM_SET, bytes, sizeof bytes);
        CHECK(memcmp(bytes, kCoNavSet, sizeof bytes) == 0 && g_exe[CO_NAVTRIM_SET_SITE] == 0xE9);
        memcpy(bytes, g_exe + CO_NAVTRIM_CELL + 2, 8); /* a range that starts inside the jump */
        co_navtrim_unpatched((uintptr_t)g_exe + CO_NAVTRIM_CELL + 2, bytes, 8);
        CHECK(memcmp(bytes, kCoNavCell + 2, 8) == 0);
    }

    VirtualProtect(g_code, 0x1000, PAGE_READWRITE, &old);
    if (g_failed) { printf("navtrim: %d checks failed\n", g_failed); return 1; }
    printf("navtrim: all cases passed\n");
    return 0;
}
