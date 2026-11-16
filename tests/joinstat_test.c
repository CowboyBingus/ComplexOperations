/* The AI joins' 10 s statistic (co_joinstat.h): the frames, their mean, p99 and worst, and the same by the budget the
 * step ran with; a budget that is not known, a join that is no number, the last bin and the quantile's edges. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_joinstat.h"

static int g_failed;
static const char* g_case = "";
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s (line %d)\n", g_case, #what, __LINE__); g_failed++; } } while (0)
static int Near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void)
{
    static CoJoinStat s;
    g_case = "an empty window";
    co_joinstat_clear(&s);
    CHECK(s.frames == 0 && co_joinstat_quantile(&s, 0.99) == 0.0 && s.worst == 0.0);

    g_case = "frames by budget kind: counts, sums and worsts apart, the window's over all";
    co_joinstat_add(&s, 0.35, CO_JS_LOW);
    co_joinstat_add(&s, 0.25, CO_JS_LOW);
    co_joinstat_add(&s, 1.9, CO_JS_STOCK);
    co_joinstat_add(&s, 4.2, CO_JS_RAISE);
    co_joinstat_add(&s, 7.8, CO_JS_DRAIN);
    co_joinstat_add(&s, 21.5, CO_JS_DRAIN);
    co_joinstat_add(&s, 0.5, -1);
    co_joinstat_add(&s, 0.5, CO_JS_KINDS); /* past the kinds: not known */
    CHECK(s.frames == 8 && s.unknown == 2);
    CHECK(s.kindFrames[CO_JS_LOW] == 2 && s.kindFrames[CO_JS_STOCK] == 1 && s.kindFrames[CO_JS_RAISE] == 1 && s.kindFrames[CO_JS_DRAIN] == 2);
    CHECK(Near(s.kindSum[CO_JS_LOW], 0.6) && Near(s.kindWorst[CO_JS_LOW], 0.35) && Near(s.kindSum[CO_JS_DRAIN], 29.3) && Near(s.kindWorst[CO_JS_DRAIN], 21.5));
    CHECK(Near(s.kindWorst[CO_JS_STOCK], 1.9) && Near(s.kindWorst[CO_JS_RAISE], 4.2) && Near(s.kindSum[CO_JS_RAISE], 4.2));
    CHECK(Near(s.sum, 0.35 + 0.25 + 1.9 + 4.2 + 7.8 + 21.5 + 1.0) && Near(s.worst, 21.5));

    g_case = "the quantile is the upper edge of its 0.1 ms bin, never past the worst";
    /* sorted: 0.25 0.35 0.5 0.5 1.9 4.2 7.8 21.5; the half is the 4th (bin 5: 0.5 to 0.6) */
    CHECK(Near(co_joinstat_quantile(&s, 0.5), 0.6));
    CHECK(Near(co_joinstat_quantile(&s, 0.125), 0.3));   /* the first: 0.25 in bin 2, edge 0.3 */
    CHECK(Near(co_joinstat_quantile(&s, 0.13), 0.4));    /* just past one frame: the second, 0.35, in bin 3 */
    CHECK(Near(co_joinstat_quantile(&s, 0.99), 21.5));   /* the last frame: bin 215's edge 21.6 is past the worst */
    CHECK(Near(co_joinstat_quantile(&s, 0.875), 7.9));
    CHECK(Near(co_joinstat_quantile(&s, 2.0), 21.5));

    g_case = "a long join goes to the last bin; one that is no number or negative is 0";
    co_joinstat_clear(&s);
    co_joinstat_add(&s, 80.0, CO_JS_DRAIN);
    co_joinstat_add(&s, NAN, CO_JS_LOW);
    co_joinstat_add(&s, -0.05, CO_JS_LOW);
    CHECK(s.frames == 3 && s.bin[CO_JS_BINS - 1] == 1 && s.bin[0] == 2 && Near(s.sum, 80.0) && Near(s.worst, 80.0));
    CHECK(s.kindFrames[CO_JS_LOW] == 2 && s.kindSum[CO_JS_LOW] == 0.0 && s.kindWorst[CO_JS_LOW] == 0.0);
    CHECK(Near(co_joinstat_quantile(&s, 0.5), 0.1) && Near(co_joinstat_quantile(&s, 0.99), 80.0));
    co_joinstat_add(&s, 49.95, CO_JS_STOCK); /* the edge of the last bin: bin 499 */
    CHECK(s.bin[CO_JS_BINS - 1] == 2 && s.bin[CO_JS_BINS - 2] == 0);
    co_joinstat_add(&s, 49.85, CO_JS_STOCK);
    CHECK(s.bin[CO_JS_BINS - 2] == 1);
    co_joinstat_add(&s, 0.1, CO_JS_STOCK);
    CHECK(s.bin[1] == 1 && s.bin[0] == 2);

    g_case = "cleared";
    co_joinstat_clear(&s);
    CHECK(s.frames == 0 && s.unknown == 0 && s.sum == 0.0 && s.bin[CO_JS_BINS - 1] == 0 && s.kindFrames[CO_JS_DRAIN] == 0);

    if (g_failed) { printf("joinstat: %d check(s) failed\n", g_failed); return 1; }
    printf("joinstat: all checks passed\n");
    return 0;
}
