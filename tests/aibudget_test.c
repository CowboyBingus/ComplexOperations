/* The AI path budget's stock value and its raise: a world still holding the mod's lowered value after a level is not
 * read as the game's, also when the next world has the same address; the raise then gives the game's value back. */
#include <stdio.h>

#include "xl/co_aibudget.h"

static int g_failed;
#define CHECK(what) do { if (!(what)) { printf("FAILED: %s (line %d)\n", #what, __LINE__); g_failed++; } } while (0)

/* One frame as living_world_aibudget.c runs it; returns the field after it. */
static float Frame(CoAiBudget* b, uintptr_t world, float field, float waitS)
{
    co_aibudget_stock(b, world, field);
    float want = co_aibudget_want(b, waitS, 500, 500);
    if (want != field) { field = want; co_aibudget_wrote(b, want); }
    return field;
}

int main(void)
{
    CoAiBudget b = {0};
    const float stock = 0.002f, low = 500.0f / 1000000.0f;

    /* mission 1: the stock is read before the first write, then lowered */
    float field = Frame(&b, 0x1000, stock, 0.0f);
    CHECK(b.stock == stock); CHECK(field == low);
    /* a long wait raises to the stock, a short one lowers again, with the band between kept */
    field = Frame(&b, 0x1000, field, 0.6f); CHECK(field == stock);
    field = Frame(&b, 0x1000, field, 0.3f); CHECK(field == stock);
    field = Frame(&b, 0x1000, field, 0.2f); CHECK(field == low);

    /* the level ends; the old world lives on for frames with the lowered value */
    co_aibudget_leave(&b);
    field = Frame(&b, 0x1000, field, 0.0f);
    CHECK(b.stock == stock); CHECK(field == low);
    co_aibudget_leave(&b);
    /* mission 2's world at the same address, fresh from the game's config */
    field = Frame(&b, 0x1000, stock, 0.0f);
    CHECK(b.stock == stock); CHECK(field == low);
    field = Frame(&b, 0x1000, field, 900.0f);
    CHECK(field == stock); /* the run's fault: 500 us here for 15 minutes */

    /* the wait is over: lowered again, and no leave before the next world (the frame missed the old world's end) */
    field = Frame(&b, 0x1000, field, 0.0f); CHECK(b.stock == stock); CHECK(field == low);
    /* another address holding the lowered value */
    field = Frame(&b, 0x2000, low, 0.0f); CHECK(b.stock == stock); CHECK(field == low);
    /* another address with another game value: taken */
    field = Frame(&b, 0x3000, 0.004f, 0.0f); CHECK(b.stock == 0.004f); CHECK(field == low);
    /* a world change drops the raise */
    field = Frame(&b, 0x3000, field, 1.0f); CHECK(b.raised == 1);
    field = Frame(&b, 0x4000, 0.004f, 0.3f); CHECK(b.raised == 0); CHECK(field == low);

    /* a low budget above the stock is held to the stock */
    CoAiBudget c = {0};
    co_aibudget_stock(&c, 0x1000, 0.0003f);
    CHECK(co_aibudget_want(&c, 0.0f, 500, 500) == 0.0003f);
    /* nothing written yet: a first read equal to the low value is the game's */
    CoAiBudget d = {0};
    CHECK(co_aibudget_stock(&d, 0x1000, low) == low);

    if (g_failed) { printf("aibudget: %d check(s) failed\n", g_failed); return 1; }
    printf("aibudget: ok\n");
    return 0;
}
