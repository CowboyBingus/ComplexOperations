/* The game thread's AI joins a 10 s window (issue C): the join at a frame's start waits for the Havok AI step, which
 * sleeps in its drive for the budget the mod wrote ([hkaiWorld+0x47C]): 500 us from the lever, the game's 2000 us, the
 * nav hold's 4000 us or the drain's 8000 us. The causes line counts frames the join led; this says how long the join
 * was, its tail, and which budget the step it waited for ran with. No game call and no clock of its own. */
#ifndef CO_JOINSTAT_H
#define CO_JOINSTAT_H

#include <stdint.h>

enum { CO_JS_LOW, CO_JS_STOCK, CO_JS_RAISE, CO_JS_DRAIN, CO_JS_KINDS };
#define CO_JS_BIN_MS 0.1   /* the p99's step */
#define CO_JS_BINS 500     /* 0 to 50 ms; the last bin holds every longer join */
#define CO_JS_LINES 1080   /* window lines a mission: three hours of play */

typedef struct CoJoinStat
{
    uint32_t frames, unknown, bin[CO_JS_BINS], kindFrames[CO_JS_KINDS];
    double sum, worst, kindSum[CO_JS_KINDS], kindWorst[CO_JS_KINDS];
} CoJoinStat;

/* One frame's joins in ms, and the budget kind of the step they waited for (under 0, or past the kinds: not known). */
void co_joinstat_add(CoJoinStat* s, double ms, int kind);
/* The join no longer than which `share` of the frames were (the bin's upper edge, ms); 0 with no frame. A share past
 * the last bin's frames gives the worst. */
double co_joinstat_quantile(const CoJoinStat* s, double share);
void co_joinstat_clear(CoJoinStat* s);

#endif
