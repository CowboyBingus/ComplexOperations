/* The game thread's AI joins a window, by the budget the step ran with (co_joinstat.h). */
#include <string.h>

#include "co_joinstat.h"

void co_joinstat_add(CoJoinStat* s, double ms, int kind)
{
    if (!(ms >= 0.0)) ms = 0.0; /* a clock that went back, or no number: nothing waited */
    const double at = ms / CO_JS_BIN_MS;
    const uint32_t b = at >= (double)(CO_JS_BINS - 1) ? CO_JS_BINS - 1 : (uint32_t)at;
    s->frames++; s->bin[b]++; s->sum += ms;
    if (ms > s->worst) s->worst = ms;
    if (kind < 0 || kind >= CO_JS_KINDS) { s->unknown++; return; }
    s->kindFrames[kind]++; s->kindSum[kind] += ms;
    if (ms > s->kindWorst[kind]) s->kindWorst[kind] = ms;
}

double co_joinstat_quantile(const CoJoinStat* s, double share)
{
    if (s->frames == 0) return 0.0;
    double want = share * (double)s->frames;
    uint32_t seen = 0;
    for (uint32_t b = 0; b < CO_JS_BINS - 1; b++)
    {
        seen += s->bin[b];
        if ((double)seen >= want) return (double)(b + 1) * CO_JS_BIN_MS < s->worst ? (double)(b + 1) * CO_JS_BIN_MS : s->worst;
    }
    return s->worst;
}

void co_joinstat_clear(CoJoinStat* s) { memset(s, 0, sizeof *s); }
