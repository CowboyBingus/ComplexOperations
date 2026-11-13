/* The 3D scatter's per-cell ground ranges (proto/difficulty_world_terrain.c NearScatRange): a cached range belongs to one
 * layer's cell size, half width and cell index. Sizes 4, 16 and 64 m share their low float bits, so a key built from
 * those bits alone gave one layer's range to another; a range taken over a narrower pad left the wider cell's ground
 * out of its box. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestScatClear(void);
void CoXlTestScatStore(float size, float half, int32_t ix, int32_t iy, float lo, float hi);
int CoXlTestScatFind(float size, float half, int32_t ix, int32_t iy, float* lo, float* hi);
uint32_t CoXlTestScatSlot(float size, float half, int32_t ix, int32_t iy);

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

    if (g_failed) { printf("scatcache: %d check(s) failed\n", g_failed); return 1; }
    printf("scatcache: all checks passed\n");
    return 0;
}
