/* The 3D scatter's per-cell ground ranges (proto/difficulty_world_terrain.c NearScatRange): a cached range belongs to one
 * layer's cell size, half width and cell index. Sizes 4, 16 and 64 m share their low float bits, so a key built from
 * those bits alone gave one layer's range to another; a range taken over a narrower pad left the wider cell's ground
 * out of its box. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestScatClear(void);
void CoXlTestScatStore(float size, float half, int32_t ix, int32_t iy, float lo, float hi);
int CoXlTestScatFind(float size, float half, int32_t ix, int32_t iy, float* lo, float* hi);
uint32_t CoXlTestScatSlot(float size, float half, int32_t ix, int32_t iy);
void CoXlTestScatGen(LONG gen);
LONG CoXlTestScatGenNow(void);
void CoXlTestTilesGen(LONG gen);
void CoXlTestCoarse(const uint16_t* heights, uint32_t n, float size);
void CoXlTestScatHold(uint32_t slot, int hold);
void CoXlTestScatRace(int where, float lo);
uint32_t CoXlTestScatPeekSize(uint32_t slot);
uint32_t CoXlTestScatterCell(void* fn, int32_t x, int32_t y, int32_t size, void* out, uint32_t max, uint8_t* brush, int bake);
void CoXlTestScatterCellCounts(LONG64* c);
void CoXlTestWorldCurrent(float current);
void CoXlTestTileAdd(float cx, float cy, const uint16_t* heights, uint32_t n, float size);
int CoXlTestTileAddAt(float cx, float cy, const uint16_t* heights, uint32_t n, float size);
void CoXlTestTileRemove(int slot);
void CoXlTestTileReset(void);
int CoXlTestTilesLive(void);

/* Many threads storing and finding at once: a found range is always its own cell's (lo = 16 ix + iy, hi = lo + 0.5). */
static volatile LONG g_stop, g_torn, g_found;
static DWORD WINAPI Hammer(void* arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u;
    while (!g_stop)
    {
        seed = seed * 1103515245u + 12345u;
        /* 2^20 cells on 2^16 slots: slots are taken over all the time, so a read across a store would show */
        const int32_t ix = (int32_t)(seed >> 8 & 0xFFFF), iy = (int32_t)(seed >> 24 & 15);
        const float want = (float)(ix * 16 + iy);
        float lo = 0, hi = 0;
        if (CoXlTestScatFind(16.0f, 8.0f, ix, iy, &lo, &hi)) { InterlockedIncrement(&g_found); if (lo != want || hi != want + 0.5f) InterlockedIncrement(&g_torn); }
        else CoXlTestScatStore(16.0f, 8.0f, ix, iy, want, want + 0.5f);
    }
    return 0;
}

/* The game's brushes' callback: n instances 0x30 apart at (x0 + 3k, y0), z 10. */
static float g_instX0, g_instY0;
static int g_more; /* report 16 whatever max is (writing only max) */
static uint32_t FakeBrushes(int32_t x, int32_t y, int32_t size, void* unused, void* out, uint32_t max, uint8_t* brush)
{
    (void)x; (void)y; (void)size; (void)unused; (void)brush;
    uint8_t* p = (uint8_t*)out;
    const uint32_t n = max < 16 ? max : 16;
    for (uint32_t k = 0; k < n; k++)
    {
        const float v[3] = {g_instX0 + 3.0f * (float)k, g_instY0, 10.0f};
        memcpy(p + 0x30 * k, v, 12);
    }
    return g_more ? 16 : n;
}
static uint16_t g_flatA[4 * 4], g_flatB[4 * 4];
static volatile LONG g_mixed, g_calls;
static uint8_t g_brush[0x2860];
static DWORD WINAPI Cells(void* arg)
{
    (void)arg;
    uint8_t out[16 * 0x30];
    while (!g_stop)
    {
        CoXlTestScatterCell((void*)FakeBrushes, 0, 0, 16, out, 16, g_brush, 0);
        float z0; memcpy(&z0, out + 8, 4);
        for (int k = 1; k < 16; k++) { float z; memcpy(&z, out + 0x30 * k + 8, 4); if (z != z0) { InterlockedIncrement(&g_mixed); break; } }
        InterlockedIncrement(&g_calls);
    }
    return 0;
}

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static int Has(float size, float half, int32_t ix, int32_t iy, float wantLo, float wantHi)
{
    float lo = -1.0f, hi = -1.0f;
    return CoXlTestScatFind(size, half, ix, iy, &lo, &hi) && lo == wantLo && hi == wantHi;
}

static int Misses(float size, float half, int32_t ix, int32_t iy)
{
    float lo = 0.0f, hi = 0.0f;
    return !CoXlTestScatFind(size, half, ix, iy, &lo, &hi);
}

int main(void)
{
    g_case = "an empty cache has nothing";
    CoXlTestScatClear();
    CHECK(Misses(16.0f, 8.0f, 0, 0));
    CHECK(Misses(16.0f, 8.0f, -3, 7));

    g_case = "layers of 4, 16 and 64 m cells keep their own ranges at the same index";
    CoXlTestScatClear();
    CoXlTestScatStore(4.0f, 2.0f, 5, -6, 1.0f, 2.0f);
    CoXlTestScatStore(16.0f, 2.0f, 5, -6, 3.0f, 4.0f);
    CHECK(Has(4.0f, 2.0f, 5, -6, 1.0f, 2.0f));
    CHECK(Has(16.0f, 2.0f, 5, -6, 3.0f, 4.0f));
    CHECK(Misses(64.0f, 2.0f, 5, -6));
    CHECK(Misses(2.0f, 2.0f, 5, -6));

    g_case = "a wider pad is another range";
    CoXlTestScatClear();
    CoXlTestScatStore(16.0f, 8.0f, 1, 1, 10.0f, 11.0f);
    CHECK(Misses(16.0f, 12.0f, 1, 1));
    CoXlTestScatStore(16.0f, 12.0f, 1, 1, 9.0f, 13.0f);
    CHECK(Has(16.0f, 8.0f, 1, 1, 10.0f, 11.0f));
    CHECK(Has(16.0f, 12.0f, 1, 1, 9.0f, 13.0f));

    g_case = "x and y are not swapped, negative indices and the far corners stay apart";
    CoXlTestScatClear();
    CoXlTestScatStore(16.0f, 8.0f, 3, 4, 1.0f, 1.5f);
    CHECK(Misses(16.0f, 8.0f, 4, 3));
    CoXlTestScatStore(16.0f, 8.0f, -512, 511, 2.0f, 2.5f);
    CoXlTestScatStore(16.0f, 8.0f, 511, -512, 3.0f, 3.5f);
    CoXlTestScatStore(16.0f, 8.0f, -512 + 0x100000, 511, 4.0f, 4.5f); /* the old key kept 20 bits of an index */
    CHECK(Has(16.0f, 8.0f, -512, 511, 2.0f, 2.5f));
    CHECK(Has(16.0f, 8.0f, 511, -512, 3.0f, 3.5f));
    CHECK(Has(16.0f, 8.0f, -512 + 0x100000, 511, 4.0f, 4.5f));
    CHECK(Has(16.0f, 8.0f, 3, 4, 1.0f, 1.5f));

    g_case = "cells on one slot that differ only in half width, or only in y, stay apart";
    {
        CoXlTestScatClear();
        uint32_t want = CoXlTestScatSlot(16.0f, 8.0f, 7, 9);
        float half = 0.0f; int32_t iy = 0;
        for (int k = 1; half == 0.0f && k < 50000000; k++)
            if (CoXlTestScatSlot(16.0f, 8.0f + (float)k / 1024.0f, 7, 9) == want) half = 8.0f + (float)k / 1024.0f;
        for (int32_t k = 10; iy == 0 && k < 50000000; k++)
            if (CoXlTestScatSlot(16.0f, 8.0f, 7, k) == want) iy = k;
        CHECK(half != 0.0f && iy != 0);
        CoXlTestScatStore(16.0f, 8.0f, 7, 9, 1.0f, 2.0f);
        CHECK(Misses(16.0f, half, 7, 9));
        CHECK(Misses(16.0f, 8.0f, 7, iy));
        CoXlTestScatStore(16.0f, half, 7, 9, 3.0f, 4.0f);
        CoXlTestScatStore(16.0f, 8.0f, 7, iy, 5.0f, 6.0f);
        CHECK(Has(16.0f, 8.0f, 7, 9, 1.0f, 2.0f));
        CHECK(Has(16.0f, half, 7, 9, 3.0f, 4.0f));
        CHECK(Has(16.0f, 8.0f, 7, iy, 5.0f, 6.0f));
    }

    g_case = "storing a cell again replaces its range in place";
    CoXlTestScatClear();
    CoXlTestScatStore(16.0f, 8.0f, 2, 2, 1.0f, 2.0f);
    CoXlTestScatStore(16.0f, 8.0f, 2, 2, 5.0f, 6.0f);
    CHECK(Has(16.0f, 8.0f, 2, 2, 5.0f, 6.0f));

    g_case = "nine cells on one slot: the probes fill, then the eighth slot is reused and the first eight are kept";
    {
        CoXlTestScatClear();
        uint32_t want = CoXlTestScatSlot(16.0f, 8.0f, 0, 0);
        int32_t same[9]; int n = 0;
        for (int32_t i = 1; n < 9 && i < 50000000; i++)
            if (CoXlTestScatSlot(16.0f, 8.0f, i, 0) == want) same[n++] = i;
        CHECK(n == 9);
        if (n == 9)
        {
            for (int k = 0; k < 8; k++) CoXlTestScatStore(16.0f, 8.0f, same[k], 0, (float)k, (float)k + 0.5f);
            for (int k = 0; k < 8; k++) CHECK(Has(16.0f, 8.0f, same[k], 0, (float)k, (float)k + 0.5f));
            CoXlTestScatStore(16.0f, 8.0f, same[8], 0, 100.0f, 101.0f);
            CHECK(Has(16.0f, 8.0f, same[8], 0, 100.0f, 101.0f));
            for (int k = 0; k < 7; k++) CHECK(Has(16.0f, 8.0f, same[k], 0, (float)k, (float)k + 0.5f));
            CHECK(Misses(16.0f, 8.0f, same[7], 0));
        }
    }

    g_case = "slots spread: 4096 cells of one layer land on at least 3000 slots";
    {
        static uint8_t used[1u << 16];
        memset(used, 0, sizeof used);
        int distinct = 0;
        for (int32_t y = -32; y < 32; y++)
            for (int32_t x = -32; x < 32; x++)
            {
                uint32_t s = CoXlTestScatSlot(16.0f, 8.0f, x, y);
                CHECK(s < (1u << 16));
                if (s < (1u << 16) && !used[s]) { used[s] = 1; distinct++; }
            }
        CHECK(distinct >= 3000);
    }

    g_case = "another generation's entries are free: a change of tiles needs no clearing";
    CoXlTestScatClear();
    CoXlTestScatGen(5);
    CoXlTestScatStore(16.0f, 8.0f, 4, 4, 1.0f, 2.0f);
    CHECK(Has(16.0f, 8.0f, 4, 4, 1.0f, 2.0f));
    CoXlTestScatGen(6);
    CHECK(Misses(16.0f, 8.0f, 4, 4));
    CoXlTestScatStore(16.0f, 8.0f, 4, 4, 3.0f, 4.0f); /* the stale slot is taken */
    CHECK(Has(16.0f, 8.0f, 4, 4, 3.0f, 4.0f));
    CoXlTestScatGen(5);
    CHECK(Misses(16.0f, 8.0f, 4, 4));

    g_case = "the readers' generation never comes back: the coarse ground on, off and on again, a tile, and past 2^19 changes";
    {
        static uint16_t coarse[4] = {0, 0, 0, 0};
        LONG seen[6];
        CoXlTestTilesGen(5);
        seen[0] = CoXlTestScatGenNow();
        CoXlTestCoarse(coarse, 2, 16384.0f);
        seen[1] = CoXlTestScatGenNow();
        CoXlTestCoarse(NULL, 0, 0.0f);
        seen[2] = CoXlTestScatGenNow();
        CoXlTestCoarse(coarse, 2, 16384.0f);
        seen[3] = CoXlTestScatGenNow();
        CoXlTestTilesGen(0x7FFFFFFF); /* the shift that was undefined past 2^19 */
        seen[4] = CoXlTestScatGenNow();
        CoXlTestCoarse(coarse, 2, 16384.0f);
        seen[5] = CoXlTestScatGenNow();
        int distinct = 1;
        for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) distinct &= seen[a] != seen[b];
        CHECK(distinct);
        /* a range taken with the coarse ground on is not found after it went and came back */
        CoXlTestScatClear();
        CoXlTestScatGen(seen[1]);
        CoXlTestScatStore(16.0f, 8.0f, 7, 7, 1.0f, 2.0f);
        CoXlTestScatGen(seen[3]);
        CHECK(Misses(16.0f, 8.0f, 7, 7));
        CoXlTestCoarse(NULL, 0, 0.0f);
        CoXlTestTilesGen(0);
        CoXlTestScatClear();
    }

    g_case = "a slot a writer holds: a find passes it by, a store leaves it to the writer";
    {
        CoXlTestScatClear();
        const uint32_t s0 = CoXlTestScatSlot(16.0f, 8.0f, 9, 9);
        CoXlTestScatStore(16.0f, 8.0f, 9, 9, 1.0f, 2.0f);
        CoXlTestScatHold(s0, 1);
        CHECK(Misses(16.0f, 8.0f, 9, 9));
        CoXlTestScatStore(16.0f, 8.0f, 9, 9, 7.0f, 8.0f); /* the same cell again: its slot is held, nothing kept */
        CoXlTestScatHold(s0, 0);
        CHECK(Has(16.0f, 8.0f, 9, 9, 1.0f, 2.0f));
    }

    g_case = "a store that lands inside a read: the read misses rather than mix the two";
    {
        CoXlTestScatClear();
        const uint32_t s0 = CoXlTestScatSlot(16.0f, 8.0f, 3, 3);
        CoXlTestScatStore(16.0f, 8.0f, 3, 3, 1.0f, 2.0f);
        CoXlTestScatRace(0, 50.0f);
        CHECK(Misses(16.0f, 8.0f, 3, 3));
        CHECK(Has(16.0f, 8.0f, 3, 3, 50.0f, 2.0f)); /* the other store's, read whole */
        (void)s0;
    }

    g_case = "a slot another writer takes between our look and our store: ours is not written";
    {
        CoXlTestScatClear();
        const uint32_t s0 = CoXlTestScatSlot(16.0f, 8.0f, 6, 6);
        CoXlTestScatRace(1, 70.0f);
        CoXlTestScatStore(16.0f, 8.0f, 6, 6, 1.0f, 2.0f);
        CHECK(CoXlTestScatPeekSize(s0) == 0); /* the other writer's slot: none of ours written into it */
        CoXlTestScatHold(s0, 0); /* the other writer lets go, its cell not ours */
        CHECK(Misses(16.0f, 8.0f, 6, 6));
    }

    g_case = "eight threads storing and finding at once never read a torn or another cell's range";
    {
        CoXlTestScatClear();
        g_stop = 0; g_torn = 0; g_found = 0;
        HANDLE t[8];
        for (int k = 0; k < 8; k++) t[k] = CreateThread(NULL, 0, Hammer, (void*)(uintptr_t)(k + 1), 0, NULL);
        Sleep(400);
        g_stop = 1;
        WaitForMultipleObjects(8, t, TRUE, INFINITE);
        for (int k = 0; k < 8; k++) CloseHandle(t[k]);
        CHECK(g_torn == 0 && g_found > 1000);
    }

    g_case = "a cell's instances on the tile: z by tile - coarse, counted once a call; off the tile and away from it left";
    {
        CoXlTestWorldCurrent(8.0f);
        CoXlTestTileReset();
        /* the coarse image: 2 x 2 of 0x8000 (0.0 m); the tile flat at 32768 + 655 (about 10 m) */
        static uint16_t image[4] = {0x8000, 0x8000, 0x8000, 0x8000};
        static uint8_t header[0x18], globals[0x18];
        const uint32_t dims[2] = {2, 2}; const uint64_t data = (uint64_t)(uintptr_t)image, hp = (uint64_t)(uintptr_t)header, gp = (uint64_t)(uintptr_t)globals;
        memcpy(header, dims, 8); memcpy(header + 0x10, &data, 8); memcpy(globals + 0x10, &hp, 8); memcpy(g_brush + 0x2858, &gp, 8);
        for (int k = 0; k < 16; k++) { g_flatA[k] = 0x8000 + 655; g_flatB[k] = 0x8000 + 1310; }
        CoXlTestTileAdd(0.0f, 0.0f, g_flatA, 4, 4096.0f);
        CHECK(CoXlTestTilesLive() == 1);
        LONG64 c0[5], c1[5];
        CoXlTestScatterCellCounts(c0);
        uint8_t out[16 * 0x30];
        g_instX0 = 10.0f; g_instY0 = 20.0f;
        CHECK(CoXlTestScatterCell((void*)FakeBrushes, 0, 1, 16, out, 16, g_brush, 1) == 16);
        const float tile = (float)(0x8000 + 655) / 65535.0f * 1000.0f - 500.0f, coarse = (float)0x8000 / 65535.0f * 1000.0f - 500.0f;
        int moved = 1;
        for (int k = 0; k < 16; k++) { float z; memcpy(&z, out + 0x30 * k + 8, 4); moved &= fabsf(z - (10.0f + tile - coarse)) < 1e-3f; }
        CoXlTestScatterCellCounts(c1);
        CHECK(moved && c1[0] - c0[0] == 16 && c1[1] - c0[1] == 16 && c1[2] == c0[2] && c1[3] == c0[3] && c1[4] - c0[4] == 1);
        /* only 5 asked for: the rest of the buffer untouched */
        memset(out, 0, sizeof out);
        CHECK(CoXlTestScatterCell((void*)FakeBrushes, 0, 1, 16, out, 5, g_brush, 0) == 5);
        CoXlTestScatterCellCounts(c0);
        CHECK(c0[0] - c1[0] == 5 && out[0x30 * 5 + 8] == 0);
        /* a callback that says more than it was given room for: only `max` are touched */
        g_more = 1;
        memset(out, 0, sizeof out);
        CHECK(CoXlTestScatterCell((void*)FakeBrushes, 0, 1, 16, out, 5, g_brush, 0) == 16);
        g_more = 0;
        CHECK(out[0x30 * 5 + 8] == 0 && out[0x30 * 5] == 0);
        CoXlTestScatterCellCounts(c0);
        /* a cell 9 km away: not near the tile, nothing moved or counted but the call */
        g_instX0 = 9000.0f;
        CHECK(CoXlTestScatterCell((void*)FakeBrushes, 562, 0, 16, out, 16, g_brush, 0) == 16);
        CoXlTestScatterCellCounts(c1);
        float z; memcpy(&z, out + 8, 4);
        CHECK(z == 10.0f && c1[0] == c0[0] && c1[4] == c0[4]);
        /* no image: one count a call near the tile */
        memset(g_brush + 0x2858, 0, 8);
        g_instX0 = 10.0f;
        CHECK(CoXlTestScatterCell((void*)FakeBrushes, 0, 1, 16, out, 16, g_brush, 0) == 16);
        CoXlTestScatterCellCounts(c0);
        CHECK(c0[3] - c1[3] == 1 && c0[0] == c1[0]);
        memcpy(g_brush + 0x2858, &gp, 8);

        g_case = "a cell's instances read one snapshot of the tiles while the game thread swaps them";
        g_stop = 0; g_mixed = 0; g_calls = 0;
        HANDLE t[4];
        for (int k = 0; k < 4; k++) t[k] = CreateThread(NULL, 0, Cells, NULL, 0, NULL);
        int slot = 0;
        for (int round = 0; round < 3000; round++)
        {
            CoXlTestTileRemove(slot);
            slot = CoXlTestTileAddAt(0.0f, 0.0f, round & 1 ? g_flatB : g_flatA, 4, 4096.0f);
        }
        g_stop = 1;
        WaitForMultipleObjects(4, t, TRUE, INFINITE);
        for (int k = 0; k < 4; k++) CloseHandle(t[k]);
        CHECK(g_mixed == 0 && g_calls > 100);
        CoXlTestTileReset();
    }

    if (g_failed) { printf("scatcache: %d check(s) failed\n", g_failed); return 1; }
    printf("scatcache: all checks passed\n");
    return 0;
}
