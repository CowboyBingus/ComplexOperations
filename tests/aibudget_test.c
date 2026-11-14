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

/* A fresh instance of the mod in a session whose world another instance lowered: nothing written yet, and the read
 * is not the game's. The field is left alone until a read that can be the game's. */
static void Unknown(void)
{
    const CoAiDrain d = {CO_AIB_DRAIN_US, CO_AIB_DRAIN_RUN_MS, CO_AIB_DRAIN_QUEUE};
    const float low = 500.0f / 1000000.0f;
    CoAiBudget b = {0};
    CHECK(co_aibudget_stock(&b, 0x1000, low) == 0.0f);
    CHECK(co_aibudget_step(&b, &d, 1, 900.0f, 500, 500, 1, 900.0f, 5000, 0.004f) == 0.0f);
    CHECK(co_aibudget_step(&b, &d, 0, 900.0f, 500, 500, 1, 900.0f, 5000, 0.0f) == 0.0f);
    CHECK(co_aibudget_stock(&b, 0x1000, low) == 0.0f);
    CHECK(co_aibudget_stock(&b, 0x1000, 0.00099f) == 0.0f);
    /* the bound itself is a game's value, and the same world's later read is taken */
    CHECK(co_aibudget_stock(&b, 0x1000, CO_AIB_STOCK_LEAST) == CO_AIB_STOCK_LEAST);
    CHECK(co_aibudget_step(&b, &d, 0, 0.0f, 500, 500, 0, 0.0f, 0, 0.0f) == CO_AIB_STOCK_LEAST);
    /* a known stock is kept over a later world's low read that is not the last write */
    co_aibudget_wrote(&b, 0.008f);
    CHECK(co_aibudget_stock(&b, 0x3000, low) == CO_AIB_STOCK_LEAST);
    CHECK(co_aibudget_stock(&b, 0x4000, 0.003f) == 0.003f);
}

/* The changes a run applied between two frames, from the run's own count. */
static void Applied(void)
{
    CoAiBudget b = {0};
    CHECK(co_aibudget_applied(&b, 0, 0) == 0);
    CHECK(co_aibudget_applied(&b, 0xA0, 5) == 5);   /* a run first seen: all it has applied */
    CHECK(co_aibudget_applied(&b, 0xA0, 5) == 0);
    CHECK(co_aibudget_applied(&b, 0xA0, 12) == 7);
    CHECK(co_aibudget_applied(&b, 0xA0, 11) == 0);  /* a count read while it moved is no negative work */
    CHECK(co_aibudget_applied(&b, 0xA0, 13) == 2);
    CHECK(co_aibudget_applied(&b, 0, 99) == 0);     /* committed: nothing in flight */
    CHECK(co_aibudget_applied(&b, 0xA0, 3) == 3);   /* the next run at the same address counts from its start */
    CHECK(co_aibudget_applied(&b, 0xB0, 4) == 4);
    CHECK(co_aibudget_applied(&b, 0xB0, 4) == 0);
}

/* One frame with the drain, as LwAibSteer runs it. */
static float FrameDrain(CoAiBudget* b, const CoAiDrain* d, uintptr_t world, float field, float waitS, int inFlight, float runS, int queued)
{
    co_aibudget_stock(b, world, field);
    float want = co_aibudget_step(b, d, 1, waitS, 500, 500, inFlight, runS, queued, 0.0f);
    if (want != field) { field = want; co_aibudget_wrote(b, want); }
    return field;
}

static void Drain(void)
{
    const CoAiDrain d = {CO_AIB_DRAIN_US, CO_AIB_DRAIN_RUN_MS, CO_AIB_DRAIN_QUEUE};
    const float stock = 0.002f, low = 500.0f / 1000000.0f, up = 8000.0f / 1000000.0f;
    CoAiBudget b = {0};
    CHECK(d.us == 8000 && d.runMs == 3000 && d.queue == 1024);
    /* nothing in flight, a short queue: the lever's own budget */
    float field = FrameDrain(&b, &d, 0x1000, stock, 0.0f, 0, 0.0f, 0);
    CHECK(field == low); CHECK(!b.draining);
    /* a young run: the lever's rule alone (its wait raises to the stock) */
    field = FrameDrain(&b, &d, 0x1000, field, 2.9f, 1, 2.9f, 10);
    CHECK(field == stock); CHECK(!b.draining);
    /* the run reaches the age: more than the game's own, and bounded */
    field = FrameDrain(&b, &d, 0x1000, field, 3.0f, 1, 3.0f, 10);
    CHECK(field == up); CHECK(b.draining == 1); CHECK(field > b.stock);
    field = FrameDrain(&b, &d, 0x1000, field, 900.0f, 1, 900.0f, 0);
    CHECK(field == up);
    /* the run committed, a quarter of the queue left: the next run takes it at the same budget */
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 0, 0.0f, 256);
    CHECK(field == up); CHECK(b.draining == 1);
    /* a new run under half the age keeps it only while the queue is long enough */
    field = FrameDrain(&b, &d, 0x1000, field, 1.6f, 1, 1.6f, 0);
    CHECK(field == up);
    field = FrameDrain(&b, &d, 0x1000, field, 1.4f, 1, 1.4f, 256);
    CHECK(field == up);
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 1, 1.4f, 255);
    CHECK(field == low); CHECK(!b.draining);
    /* between the bounds nothing starts it */
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 1, 2.0f, 1023);
    CHECK(field == low); CHECK(!b.draining);
    /* a long queue starts it with no run in flight; an age with no run does not */
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 0, 0.0f, 1024);
    CHECK(field == up);
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 0, 0.0f, 0);
    CHECK(field == low);
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 0, 50.0f, 0);
    CHECK(field == low); CHECK(!b.draining);
    /* a queue that does not read, an age that is no number: no drain */
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 1, __builtin_nanf(""), -1);
    CHECK(field == low); CHECK(!b.draining);

    /* off: none, and one under way ends */
    CoAiDrain off = d; off.us = 0;
    field = FrameDrain(&b, &d, 0x1000, field, 0.0f, 1, 5.0f, 0); CHECK(field == up);
    field = FrameDrain(&b, &off, 0x1000, field, 0.0f, 1, 5.0f, 5000);
    CHECK(field == low); CHECK(!b.draining);
    /* a drain budget under the lever's is not a cut: the stock one stays while requests wait */
    CoAiDrain small = d; small.us = 1000;
    field = FrameDrain(&b, &small, 0x1000, field, 5.0f, 1, 5.0f, 0);
    CHECK(field == stock); CHECK(b.draining == 1);
    /* the lever off: the game's own budget, and the drain over it */
    CoAiBudget o = {0};
    co_aibudget_stock(&o, 0x1000, stock);
    CHECK(co_aibudget_step(&o, &d, 0, 0.0f, 500, 500, 0, 0.0f, 0, 0.0f) == stock);
    CHECK(co_aibudget_step(&o, &d, 0, 9.0f, 500, 500, 1, 9.0f, 0, 0.0f) == up);
    /* another part's raise for the step: the largest of the three, never a sum */
    CHECK(co_aibudget_step(&o, &d, 1, 0.0f, 500, 500, 1, 9.0f, 0, 0.004f) == up);
    CHECK(co_aibudget_step(&o, &d, 1, 0.0f, 500, 500, 0, 0.0f, 0, 0.0f) == low);
    CHECK(co_aibudget_step(&o, &d, 1, 0.0f, 500, 500, 0, 0.0f, 0, 0.004f) == 0.004f);
    CHECK(co_aibudget_step(&o, &d, 1, 0.0f, 500, 500, 0, 0.0f, 0, 0.0001f) == low);
    CHECK(co_aibudget_step(&o, &d, 1, 0.0f, 500, 500, 0, 0.0f, 0, 0.012f) == 0.012f);
    /* other limits are the caller's */
    CoAiDrain other = {12000, 10000, 100}; CoAiBudget c = {0};
    co_aibudget_stock(&c, 0x1000, stock);
    CHECK(co_aibudget_drain(&c, &other, 1, 9.9f, 99) == 0.0f);
    CHECK(co_aibudget_drain(&c, &other, 1, 10.0f, 0) == 12000.0f / 1000000.0f);
    CHECK(co_aibudget_drain(&c, &other, 0, 0.0f, 25) == 12000.0f / 1000000.0f);
    CHECK(co_aibudget_drain(&c, &other, 0, 0.0f, 24) == 0.0f);
}

/* The drain over two missions of one process: its budget is never the next world's stock, and a drain under way
 * does not reach the next world. */
static void DrainMissions(void)
{
    const CoAiDrain d = {CO_AIB_DRAIN_US, CO_AIB_DRAIN_RUN_MS, CO_AIB_DRAIN_QUEUE};
    const float stock = 0.002f, low = 500.0f / 1000000.0f, up = 8000.0f / 1000000.0f;
    CoAiBudget b = {0};
    float field = FrameDrain(&b, &d, 0x1000, stock, 0.0f, 0, 0.0f, 0);
    field = FrameDrain(&b, &d, 0x1000, field, 60.0f, 1, 60.0f, 4000);
    CHECK(field == up); CHECK(b.stock == stock);
    /* the level ends in a drain; the old world lives on with the drain's budget and its run */
    co_aibudget_leave(&b);
    CHECK(!b.draining);
    CHECK(co_aibudget_stock(&b, 0x1000, field) == stock);
    field = FrameDrain(&b, &d, 0x1000, field, 60.0f, 1, 61.0f, 4000);
    CHECK(b.stock == stock); CHECK(field == up);
    co_aibudget_leave(&b);
    /* mission 2's world at the same address, fresh: the game's stock, no drain */
    field = FrameDrain(&b, &d, 0x1000, stock, 0.0f, 0, 0.0f, 0);
    CHECK(b.stock == stock); CHECK(field == low); CHECK(!b.draining);
    /* the stall's conditions in mission 2: the lever's raise gives the stock, the drain more */
    field = FrameDrain(&b, &d, 0x1000, field, 2.0f, 1, 2.0f, 0); CHECK(field == stock);
    field = FrameDrain(&b, &d, 0x1000, field, 900.0f, 1, 900.0f, 0); CHECK(field == up);
    /* no leave seen before mission 3's world, which holds the drain's budget at another address or the same */
    field = FrameDrain(&b, &d, 0x2000, up, 0.0f, 0, 0.0f, 0);
    CHECK(b.stock == stock); CHECK(field == low); CHECK(!b.draining);
    field = FrameDrain(&b, &d, 0x2000, field, 0.0f, 0, 0.0f, 2000); CHECK(field == up);
    field = FrameDrain(&b, &d, 0x3000, stock, 0.0f, 0, 0.0f, 0);
    CHECK(b.stock == stock); CHECK(field == low); CHECK(!b.draining);
    /* a drain under way is the old world's: the next one's middling queue does not carry it on */
    field = FrameDrain(&b, &d, 0x3000, field, 0.0f, 0, 0.0f, 2000); CHECK(field == up);
    field = FrameDrain(&b, &d, 0x5000, stock, 0.0f, 0, 0.0f, 300);
    CHECK(field == low); CHECK(!b.draining);
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
    co_aibudget_stock(&c, 0x1000, 0.0012f);
    CHECK(co_aibudget_want(&c, 0.0f, 1500, 500) == 0.0012f);
    Unknown();
    Applied();

    Drain();
    DrainMissions();
    if (g_failed) { printf("aibudget: %d check(s) failed\n", g_failed); return 1; }
    printf("aibudget: ok\n");
    return 0;
}
