#include "co_aibudget.h"

float co_aibudget_stock(CoAiBudget* b, uintptr_t world, float cur)
{
    if (world == b->world) return b->stock;
    b->world = world; b->raised = 0;
    /* the game makes every world's budget from one config value, so the first unwritten read holds for all */
    if (!(b->stock > 0) || !(b->wrote > 0) || cur != b->wrote) b->stock = cur;
    return b->stock;
}

float co_aibudget_want(CoAiBudget* b, float waitS, int lowUs, int raiseMs)
{
    if (waitS * 1000.0f > (float)raiseMs) b->raised = 1;
    else if (waitS * 1000.0f < 0.5f * (float)raiseMs) b->raised = 0;
    float want = b->raised ? b->stock : (float)lowUs / 1000000.0f;
    return want > b->stock ? b->stock : want; /* never more than the game's own */
}

void co_aibudget_wrote(CoAiBudget* b, float value) { b->wrote = value; }

void co_aibudget_leave(CoAiBudget* b) { b->world = 0; b->raised = 0; }
