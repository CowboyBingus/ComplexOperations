#include <string.h>

#include "fake_stock.h"
#include "xl/co_chain.h"
#include "xl/co_rows.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"
#include "fake_host.h"
#include "co_early_sites.h"

static void Rel(uint8_t* image, uint32_t at, uint32_t target) { const int32_t rel = (int32_t)(target - (at + 4u)); memcpy(image + at, &rel, 4); }

uint32_t fake_stock_stub(int chain) { return FAKE_STOCK_STUBS + 0x80u * (uint32_t)chain; }

/* Per stub, from HellForge's Prepare functions: where the disk goes, then {where, what} of each address it fills.
 * What: 1 the spacing, 2 the grid, 3 the cells' factor, 4 the quarter, 5 its watch block, 6 the game behind the site,
 * 7 the pass's way on (0xF67FB7). */
static const struct { uint8_t disk; uint8_t fills[5][2]; } kFills[CO_CHAINS] = {
    {15, {{25, 1}, {30, 6}}},
    {16, {{27, 1}, {32, 6}, {42, 6}}},
    {19, {{29, 2}, {34, 6}}},
    {15, {{26, 3}, {40, 4}, {48, 6}}},
    {24, {{34, 3}, {48, 4}, {53, 6}}},
    {15, {{25, 3}, {39, 4}, {44, 6}}},
    {15, {{25, 3}, {39, 4}, {44, 6}}},
    {16, {{26, 3}, {40, 4}, {45, 6}}},
    {6, {{21, 6}, {34, 6}}},
    {18, {{65, 6}}},
    {20, {{3, 5}, {10, 5}, {38, 5}, {43, 7}, {54, 6}}}};

void fake_stock_chains(uint8_t* image, float disk)
{
    const CoChain* chains = co_chain_sites();
    const uint32_t values = FAKE_STOCK_STUBS + 0xF00u;
    const float cells[4] = {FAKE_STOCK_SPACING, FAKE_STOCK_GRID, FAKE_STOCK_FACTOR, 0.25f}, vista = 1009.24f / 620.0f;
    memcpy(image + values, cells, sizeof cells);
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &chains[i];
        const uint32_t stub = fake_stock_stub(i);
        uint8_t jump[16];
        memcpy(image + stub, c->stock, c->stockSize);
        memcpy(image + stub + kFills[i].disk, &disk, 4);
        for (int k = 0; k < 5 && kFills[i].fills[k][0] != 0; k++)
        {
            const uint32_t at = stub + kFills[i].fills[k][0], what = kFills[i].fills[k][1];
            Rel(image, at, what <= 4 ? values + 4u * (what - 1u) : what == 5 ? values + 0x40u : what == 6 ? c->site + c->length : 0xF67FB7u);
        }
        if (i == 9) { memcpy(image + stub + 80, &vista, 4); memcpy(image + stub + 84, &vista, 4); }
        memset(jump, 0x90, sizeof jump);
        jump[0] = 0xE9;
        memcpy(image + c->site, jump, c->length);
        Rel(image, c->site + 1u, stub);
    }
}

void fake_stock_rows(uint8_t* image)
{
    static const float radii[4] = {0.5f, 0.625f, 0.75f, 0.875f};
    const CoDistanceSite* sites = co_distance_sites();
    const int32_t table = (int32_t)FAKE_STOCK_DISTANCE;
    float rows[11 * 3];
    for (int level = 0; level < 10; level++)
        for (int k = 0; k < 3; k++) rows[level * 3 + k] = level == 9 ? (k == 2 ? 0.90f : 0.85f) : 0.25f + 0.0625f * (float)level;
    for (int k = 0; k < 3; k++) rows[30 + k] = rows[27 + k] * 2.0f;
    memcpy(image + CO_DISTANCE_GAME, rows, 10 * 3 * sizeof(float));
    memcpy(image + FAKE_STOCK_DISTANCE, rows, sizeof rows);
    for (int i = 0; i < CO_DISTANCE_SITES; i++)
    {
        memcpy(image + sites[i].rva, sites[i].bytes, sites[i].at);
        memcpy(image + sites[i].rva + sites[i].at, &table, 4);
    }
    memcpy(image + CO_ROW_RADII_SITE, kCoRowRadii, sizeof kCoRowRadii);
    memcpy(image + CO_ROW_OUTPOSTS_SITE, kCoRowOutposts, sizeof kCoRowOutposts);
    memcpy(image + CO_ROW_OBJECTIVES_SITE, kCoRowObjectives, sizeof kCoRowObjectives);
    memcpy(image + FAKE_STOCK_ROW_TEN + 0xF0u, radii, sizeof radii);
    g_fake.levelTen = (uintptr_t)image + FAKE_STOCK_ROW_TEN;
}

void fake_stock_nav(uint8_t* image)
{
    uint8_t* exe = fake_host_exe();
    const uint64_t retile = 0x1122334455667788ull, system = (uint64_t)(uintptr_t)image + CO_NAV_SYSTEM;
    const float sizes[2] = {128.0f, 256.0f};
    uint8_t jump[15];
    memcpy(image + FAKE_STOCK_NAV, kNavStock, sizeof kNavStock);
    memcpy(image + FAKE_STOCK_NAV + 60, &retile, 8); memcpy(image + FAKE_STOCK_NAV + 126, &system, 8);
    Rel(image, FAKE_STOCK_NAV + 141u, CO_NAV_BACK);
    memcpy(image + FAKE_STOCK_NAV + 0x100u, kNavShipStock, sizeof kNavShipStock);
    memcpy(image + FAKE_STOCK_NAV + 0x100u + 65, &retile, 8);
    Rel(image, FAKE_STOCK_NAV + 0x100u + 140u, CO_NAV_SHIP_BACK);
    memset(jump, 0x90, sizeof jump); jump[0] = 0xE9;
    memcpy(image + CO_NAV_SITE, jump, 15); Rel(image, CO_NAV_SITE + 1u, FAKE_STOCK_NAV);
    memcpy(image + CO_NAV_SHIP_SITE, jump, 10); Rel(image, CO_NAV_SHIP_SITE + 1u, FAKE_STOCK_NAV + 0x100u);
    memcpy(exe + CO_NAV_EXE_PIN, kNavExe, sizeof kNavExe);
    memcpy(exe + CO_NAV_TILE_READ, kNavTileRead, sizeof kNavTileRead);
    memcpy(exe + CO_NAV_TILE_128, &sizes[0], 4); memcpy(exe + CO_NAV_TILE_256, &sizes[1], 4);
}

void fake_stock_early(uint8_t* image)
{
    uint8_t* exe = g_fake.exe != NULL ? g_fake.exe : fake_host_exe(); /* a case's engine image is made once */
    const float size = 3072.0f, cell = 128.0f;
    int32_t rel;
    for (size_t i = 0; i < sizeof kCoEarlyPools / sizeof kCoEarlyPools[0]; i++) memcpy(image + kCoEarlyPools[i].rva, kCoEarlyPools[i].stock, kCoEarlyPools[i].length);
    memcpy(image + CO_EARLY_BROADPHASE_LOAD, kCoEarlyBroadphaseLoad, 8);
    memcpy(image + CO_EARLY_BROADPHASE, &size, 4);
    for (size_t i = 0; i < 4; i++) memcpy(image + kCoEarlyExtents[i].rva, kCoEarlyExtents[i].bytes, 7);
    memcpy(image + CO_EARLY_CELL_READ, kCoEarlyCellRead, 8);
    memcpy(&rel, kCoEarlyCellRead + 4, 4);
    memcpy(image + CO_EARLY_CELL_READ + 8u + (uint32_t)rel, &cell, 4);
    memset(image + CO_EARLY_SETUP, 0, 8);
    memcpy(exe + CO_EARLY_TILE_READ, kCoEarlyTileRead, 8);
}
