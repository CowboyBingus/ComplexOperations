#include <string.h>

#include "fake_stock.h"
#include "xl/co_chain.h"

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
