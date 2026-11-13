/* Where the world streams for a player who is passing through (co_settle.h). */
#include <math.h>
#include <string.h>

#include "co_settle.h"

void co_settle_level(CoSettle* s, long level)
{
    if (s->level == level) return;
    memset(s->slot, 0, sizeof s->slot);
    s->level = level;
}

int co_settle_at(CoSettle* s, uint32_t player, const float* at, uint64_t nowMs, float* use)
{
    use[0] = at[0]; use[1] = at[1];
    if (player >= CO_SETTLE_SLOTS) return 0;
    CoSettleSlot* p = &s->slot[player];
    if (!p->seen)
    {
        if (!isfinite(at[0]) || !isfinite(at[1])) return 0;
        memset(p, 0, sizeof *p);
        p->seen = 1; p->held[0] = p->last[0] = at[0]; p->held[1] = p->last[1] = at[1]; p->lastMs = nowMs;
        return 0;
    }
    float step = hypotf(at[0] - p->last[0], at[1] - p->last[1]);
    uint64_t dt = nowMs > p->lastMs ? nowMs - p->lastMs : 0;
    /* a reading that is not a number is a jump: the place held stays */
    const int jump = !(step <= CO_SETTLE_JUMP);
    /* the tick count steps by about 16 ms: over a shorter time a frame's move reads as a speed it never had */
    if (!jump && dt < CO_SETTLE_SAMPLE_MS)
    {
        if (!p->passing) { p->held[0] = at[0]; p->held[1] = at[1]; }
        use[0] = p->held[0]; use[1] = p->held[1];
        return p->passing;
    }
    int fast = jump || step * 1000.0f > CO_SETTLE_FAST * (float)dt;
    if (fast) { p->passing = 1; p->calm = 0; }
    else if (p->passing)
    {
        if (!p->calm) { p->calm = 1; p->calmMs = nowMs; }
        if (nowMs - p->calmMs >= CO_SETTLE_CALM_MS) p->passing = 0;
    }
    if (isfinite(at[0]) && isfinite(at[1])) { p->last[0] = at[0]; p->last[1] = at[1]; p->lastMs = nowMs; }
    if (!p->passing) { p->held[0] = at[0]; p->held[1] = at[1]; }
    use[0] = p->held[0]; use[1] = p->held[1];
    return p->passing;
}

uint32_t co_settle_removals(float nearest2, uint32_t normal, uint32_t behind)
{
    return nearest2 > CO_SETTLE_BEHIND * CO_SETTLE_BEHIND ? behind : normal;
}
