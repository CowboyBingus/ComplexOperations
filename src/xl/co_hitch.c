/* The game thread's frames while a reinforcement hellpod comes in (co_hitch.h). */
#include <string.h>

#include "co_hitch.h"

void co_hitch_level(CoHitch* h, long level)
{
    if (h->level == level) return;
    memset(h, 0, sizeof *h);
    h->level = level;
}

int co_hitch_arm(CoHitch* h, uint64_t nowMs)
{
    if (h->on || h->watches >= CO_HITCH_WATCHES) return 0;
    const long level = h->level;
    const uint32_t watches = h->watches;
    memset(h, 0, sizeof *h);
    h->level = level; h->watches = watches + 1;
    h->on = 1; h->startMs = nowMs;
    return 1;
}

float co_hitch_own(const CoHitchCost* cost) { return cost->stream + cost->window + cost->spawn + cost->living + cost->tiles; }

int co_hitch_frame(CoHitch* h, uint64_t nowMs, float ms, const CoHitchCost* cost)
{
    if (!h->on) return 0;
    const uint32_t at = nowMs > h->startMs ? (uint32_t)(nowMs - h->startMs) : 0;
    /* a frame's time is not a number or over 10 s only when the clock stalled (a debugger, a suspended process) */
    if (ms >= 0.0f && ms < 10000.0f)
    {
        h->frames++; h->sum += ms; h->own += co_hitch_own(cost);
        h->over16 += ms > 1000.0f / 60.0f; h->over25 += ms > 25.0f; h->over33 += ms > 1000.0f / 30.0f; h->over50 += ms > 50.0f;
        h->removed += cost->removed; h->sunWhole += cost->sunWhole;
        int k = CO_HITCH_KEEP;
        while (k > 0 && ms > h->top[k - 1].ms) k--;
        if (k < CO_HITCH_KEEP)
        {
            memmove(&h->top[k + 1], &h->top[k], sizeof h->top[0] * (size_t)(CO_HITCH_KEEP - 1 - k));
            h->top[k].ms = ms; h->top[k].atMs = at; h->top[k].cost = *cost;
        }
    }
    if (at < CO_HITCH_WATCH_MS) return 0;
    h->on = 0;
    return 1;
}
