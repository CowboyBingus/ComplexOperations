#include "co_aibudget.h"

float co_aibudget_stock(CoAiBudget* b, uintptr_t world, float cur)
{
    if (world == b->world && b->stock > 0) return b->stock; /* one not known yet is read again */
    b->world = world; b->raised = b->draining = 0;
    /* the game makes every world's budget from one config value, so the first unwritten read holds for all */
    if (!(cur < CO_AIB_STOCK_LEAST) && (!(b->stock > 0) || !(b->wrote > 0) || cur != b->wrote)) b->stock = cur;
    return b->stock;
}

float co_aibudget_want(CoAiBudget* b, float waitS, int lowUs, int raiseMs)
{
    if (waitS * 1000.0f > (float)raiseMs) b->raised = 1;
    else if (waitS * 1000.0f < 0.5f * (float)raiseMs) b->raised = 0;
    float want = b->raised ? b->stock : (float)lowUs / 1000000.0f;
    return want > b->stock ? b->stock : want; /* never more than the game's own */
}

float co_aibudget_drain(CoAiBudget* b, const CoAiDrain* d, int inFlight, float runS, int queued)
{
    if (d->us <= 0) { b->draining = 0; return 0.0f; }
    /* the age is the game's sum of step times; one that is not a number counts as no run */
    float runMs = inFlight && runS > 0 ? runS * 1000.0f : 0.0f;
    if (runMs >= (float)d->runMs || queued >= d->queue) b->draining = 1;
    else if (runMs < 0.5f * (float)d->runMs && queued < d->queue / 4) b->draining = 0;
    return b->draining ? (float)d->us / 1000000.0f : 0.0f;
}

float co_aibudget_step(CoAiBudget* b, const CoAiDrain* d, int leverOn, float waitS, int lowUs, int raiseMs, int inFlight, float runS, int queued, float moreS)
{
    if (!(b->stock > 0)) return 0.0f;
    float want = leverOn ? co_aibudget_want(b, waitS, lowUs, raiseMs) : b->stock;
    float drain = co_aibudget_drain(b, d, inFlight, runS, queued);
    if (drain > want) want = drain;
    return moreS > want ? moreS : want;
}

void co_aibudget_wrote(CoAiBudget* b, float value) { b->wrote = value; }

int co_aibudget_applied(CoAiBudget* b, uintptr_t run, int sum)
{
    int since = run && run == b->run ? sum - b->applied : sum;
    b->run = run; b->applied = run ? sum : 0;
    return run && since > 0 ? since : 0;
}

void co_aibudget_leave(CoAiBudget* b) { b->world = 0; b->raised = b->draining = 0; }
