/* The AI path budget's stock value and its raise: a world still holding the mod's lowered value after a level is not
 * read as the game's, also when the next world has the same address; the raise then gives the game's value back. */
#include <math.h>
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
    /* a fresh instance over a world in a drain, or under another part's raise: over any game value, not taken */
    CoAiBudget c = {0};
    CHECK(co_aibudget_stock(&c, 0x1000, 0.008f) == 0.0f);
    CHECK(co_aibudget_stock(&c, 0x1000, 0.004f) == 0.0f);
    CHECK(co_aibudget_stock(&c, 0x1000, 0.0030001f) == 0.0f);
    CHECK(co_aibudget_step(&c, &d, 1, 900.0f, 500, 500, 1, 900.0f, 5000, 0.004f) == 0.0f);
    CHECK(co_aibudget_stock(&c, 0x1000, CO_AIB_STOCK_MOST) == CO_AIB_STOCK_MOST);
    CoAiBudget g = {0};
    CHECK(co_aibudget_stock(&g, 0x1000, 0.002f) == 0.002f);
    /* a known stock is kept over a later world's high read too */
    CHECK(co_aibudget_stock(&g, 0x2000, 0.008f) == 0.002f);
}

/* HellForge 0.5's ai-path-budget, as perf_ai_path_budget.c steers a field each frame: only a field holding exactly the
 * game's 2 ms or its 500 us, to 500 us, or to 2 ms while the wait is over 0.5 s until it is under 0.25 s. */
static int g_hfRaised;
static float HellForgeSteer(float field, float waitS)
{
    if (field != 0.002f && field != 0.0005f) return field;
    if (waitS > 0.5f) g_hfRaised = 1; else if (waitS < 0.25f) g_hfRaised = 0;
    return g_hfRaised ? 0.002f : 0.0005f;
}
/* One frame of the mod's steer (LwAibSteer: the step's value, written as co_aibudget_put gives it) and HellForge's. */
static float BothFrame(CoAiBudget* b, const CoAiDrain* d, float field, int leverOn, float waitS)
{
    float want = co_aibudget_step(b, d, leverOn, waitS, 500, 500, 0, 0.0f, 0, 0.0f), put = co_aibudget_put(b, want);
    if (want > 0 && field != put) { field = put; co_aibudget_wrote(b, put); }
    return HellForgeSteer(field, waitS);
}
static void Beside(void)
{
    const CoAiDrain off = {0, 3000, 1024};
    const float up = nextafterf(0.002f, 1.0f);
    CoAiBudget b = {0};
    /* HellForge lowered the world before the mission: the stock from the game's constant, then kept for the world */
    CHECK(co_aibudget_stock(&b, 0x1000, 0.0005f) == 0.0f);
    CHECK(co_aibudget_stock_game(&b, 0x1000, 0.0f) == 0.0f && co_aibudget_stock_game(&b, 0x1000, 0.0005f) == 0.0f);
    CHECK(co_aibudget_stock_game(&b, 0x1000, 0.002f) == 0.002f && b.world == 0x1000);
    CHECK(co_aibudget_stock(&b, 0x1000, 0.0005f) == 0.002f);
    /* the stock value is written a float step above; every other value as it is */
    CHECK(co_aibudget_put(&b, 0.002f) == up && up > 0.002f && up < 0.0020001f);
    CHECK(co_aibudget_put(&b, 0.0005f) == 0.0005f && co_aibudget_put(&b, 0.008f) == 0.008f && co_aibudget_put(&b, 0.004f) == 0.004f);
    CoAiBudget none = {0};
    CHECK(co_aibudget_put(&none, 0.002f) == 0.002f);
    /* with both levers: the mod's stock (its lever off: a nav hold waits) stays through HellForge's frame */
    g_hfRaised = 0;
    float field = 0.0005f;
    field = BothFrame(&b, &off, field, 0, 0.0f);
    CHECK(field == up);
    field = BothFrame(&b, &off, field, 0, 0.0f);
    CHECK(field == up);
    /* the lever back on with a short wait: both want 500 us */
    field = BothFrame(&b, &off, field, 1, 0.0f);
    CHECK(field == 0.0005f);
    /* a long wait: both raise; the mod's raise stays while the wait is between the two bounds, then both drop */
    field = BothFrame(&b, &off, field, 1, 0.6f);
    CHECK(field == up);
    field = BothFrame(&b, &off, field, 1, 0.3f);
    CHECK(field == up);
    field = BothFrame(&b, &off, field, 1, 0.1f);
    CHECK(field == 0.0005f);
}

/* The lever, the drain and another part's raise (the nav hold's 4000 us) in one step: the largest, in every order. */
static void ThreeWay(void)
{
    const float stock = 0.002f, low = 500.0f / 1000000.0f, hold = 0.004f;
    const CoAiDrain d8 = {8000, 3000, 1024}, d3 = {3000, 3000, 1024}, d1 = {1000, 3000, 1024};
    CoAiBudget b = {0};
    co_aibudget_stock(&b, 0x1000, stock);
    /* drain 8000 > hold 4000 > lever (stock, its wait raised it); `ran` names whose value it is */
    CHECK(co_aibudget_step(&b, &d8, 1, 9.0f, 500, 500, 1, 9.0f, 0, hold) == 0.008f && b.ran == CO_AIB_RAN_DRAIN);
    /* hold 4000 > drain 3000 > lever 2000 */
    CHECK(co_aibudget_step(&b, &d3, 1, 9.0f, 500, 500, 1, 9.0f, 0, hold) == hold && b.ran == CO_AIB_RAN_MORE && b.draining);
    /* hold 4000 > lever 2000 > drain 1000 */
    CHECK(co_aibudget_step(&b, &d1, 1, 9.0f, 500, 500, 1, 9.0f, 0, hold) == hold && b.ran == CO_AIB_RAN_MORE);
    /* lever 2000 > hold 1500 > drain 1000; and drain 3000 > lever 2000 > hold 1500 */
    CHECK(co_aibudget_step(&b, &d1, 1, 9.0f, 500, 500, 1, 9.0f, 0, 0.0015f) == stock && b.ran == CO_AIB_RAN_LEVER && b.draining);
    CHECK(co_aibudget_step(&b, &d3, 1, 9.0f, 500, 500, 1, 9.0f, 0, 0.0015f) == 0.003f && b.ran == CO_AIB_RAN_DRAIN);
    /* a tie is the first one's: the drain's over the hold's, the lever's over the drain's */
    { const CoAiDrain d4 = {4000, 3000, 1024}, d2 = {2000, 3000, 1024};
      CHECK(co_aibudget_step(&b, &d4, 1, 9.0f, 500, 500, 1, 9.0f, 0, hold) == hold && b.ran == CO_AIB_RAN_DRAIN);
      CHECK(co_aibudget_step(&b, &d2, 1, 9.0f, 500, 500, 1, 9.0f, 0, 0.0f) == stock && b.ran == CO_AIB_RAN_LEVER); }
    /* a stock not known: nothing is anyone's */
    { CoAiBudget u = {0}; u.ran = CO_AIB_RAN_MORE; CHECK(co_aibudget_step(&u, &d8, 1, 9.0f, 500, 500, 1, 9.0f, 0, hold) == 0.0f && u.ran == CO_AIB_RAN_LEVER); }
    /* the lever held at the stock by the hold (leverOn 0), no drain: the hold's raise is not lost */
    CHECK(co_aibudget_step(&b, &d8, 0, 0.0f, 500, 500, 0, 0.0f, 0, hold) == hold);
    CHECK(co_aibudget_step(&b, &d8, 0, 0.0f, 500, 500, 0, 0.0f, 0, 0.0f) == stock);
    /* the lever low, no drain: the hold alone, then the lever alone */
    CHECK(co_aibudget_step(&b, &d8, 1, 0.0f, 500, 500, 0, 0.0f, 0, hold) == hold);
    CHECK(co_aibudget_step(&b, &d8, 1, 0.0f, 500, 500, 0, 0.0f, 0, 0.0f) == low);
}

/* The watchdog's stack copy stays on the thread's own stack. */
static void Stack(void)
{
    size_t n = 99;
    CHECK(co_aibudget_stack(0x8000, 0x1000, 0x9000, 0x1000, &n) == 1 && n == 0x1000);
    CHECK(co_aibudget_stack(0x8F00, 0x1000, 0x9000, 0x1000, &n) == 1 && n == 0x100);   /* a parked thread: to the top only */
    CHECK(co_aibudget_stack(0x8FF4, 0x1000, 0x9000, 0x1000, &n) == 1 && n == 8);       /* whole words */
    CHECK(co_aibudget_stack(0x1000, 0x1000, 0x9000, 0x1000, &n) == 1 && n == 0x1000);  /* the limit itself is on it */
    CHECK(co_aibudget_stack(0x9000, 0x1000, 0x9000, 0x1000, &n) == 0);                 /* the top is not */
    CHECK(co_aibudget_stack(0xFFF, 0x1000, 0x9000, 0x1000, &n) == 0);                  /* another stack, below */
    CHECK(co_aibudget_stack(0xA000, 0x1000, 0x9000, 0x1000, &n) == 0);                 /* and above */
    CHECK(co_aibudget_stack(0x8000, 0, 0, 0x1000, &n) == 0);                           /* bounds that did not read */
    CHECK(co_aibudget_stack(0x8000, 0x9000, 0x9000, 0x1000, &n) == 0);
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
    Beside();
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
    field = Frame(&b, 0x3000, 0.003f, 0.0f); CHECK(b.stock == 0.003f); CHECK(field == low);
    /* a world change drops the raise */
    field = Frame(&b, 0x3000, field, 1.0f); CHECK(b.raised == 1);
    field = Frame(&b, 0x4000, 0.003f, 0.3f); CHECK(b.raised == 0); CHECK(field == low);

    /* a low budget above the stock is held to the stock */
    CoAiBudget c = {0};
    co_aibudget_stock(&c, 0x1000, 0.0012f);
    CHECK(co_aibudget_want(&c, 0.0f, 1500, 500) == 0.0012f);
    /* a low budget set inside the game's range (the command's 1500 us) is still the mod's when read back */
    CoAiBudget e = {0};
    co_aibudget_stock(&e, 0x1000, stock);
    co_aibudget_wrote(&e, co_aibudget_want(&e, 0.0f, 1500, 500));
    co_aibudget_leave(&e);
    CHECK(co_aibudget_stock(&e, 0x1000, 0.0015f) == stock);
    Unknown();
    Applied();
    ThreeWay();
    Stack();

    Drain();
    DrainMissions();
    if (g_failed) { printf("aibudget: %d check(s) failed\n", g_failed); return 1; }
    printf("aibudget: ok\n");
    return 0;
}
