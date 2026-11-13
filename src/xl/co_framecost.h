/* Wall-clock frame times on a complex mission, every build (issue F: stutter near a terminid nest). The mod's
 * game-thread tick (co_xl.c CoXlTick) runs once a frame, so QPC from one tick to the next is the frame; the game's own
 * dt is clamped and cannot show a stutter. Each 10 s window logs the lows, the slow frames, the PC's CPU load against
 * the game's share (a build on the same PC starves the game), the mod's tick, the AI count and the nearest garrison
 * with living spawners (bug holes on a terminid map). A slow frame is logged with the tick part that took the most.
 * Lines are bounded. */
#ifndef CO_FRAMECOST_H
#define CO_FRAMECOST_H

#include <stdint.h>

/* The tick's parts, in CoXlTick's order. */
enum
{
    CO_FC_STATUS, CO_FC_STREAM, CO_FC_SPANS, CO_FC_NET, CO_FC_TDIAG, CO_FC_TPROBE, CO_FC_LIVING, CO_FC_DAYLIGHT,
    CO_FC_TAC, CO_FC_DENSITY, CO_FC_PARTS
};

#define CO_FC_WINDOW_MS 10000.0
#define CO_FC_FRAMES 4096          /* frame times kept a window: 400 fps for 10 s */
#define CO_FC_SLOW_MS 25.0         /* a visible stutter at the 60 fps target */
#define CO_FC_HITCH_MS 33.3
#define CO_FC_SLOW_LINES_WINDOW 2  /* slow-frame lines a window */
#define CO_FC_LINES_SESSION 800    /* all lines a session: about two hours of play */
#define CO_FC_LOAD_MS 1000.0       /* longer: a load or a pause, not a frame */

/* At window close: the AI units now, and the distance from player 1 to the nearest garrison with living spawners
 * (negative when none is known). */
typedef void (*CoFcContextFn)(int* ai, float* nest);
void co_framecost_context(CoFcContextFn fn);

/* At the start of the tick: closes the frame since the last tick (when `live` then and now). */
void co_framecost_begin(int live);
/* After a part of the tick: the time since the last mark goes to `part`. */
void co_framecost_mark(int part);

#ifdef CO_TEST
/* The same on a given clock (ticks of `freq` a second) and given CPU times (100 ns units). */
void co_framecost_test_reset(int64_t freq);
void co_framecost_test_cpu(uint64_t systemBusy, uint64_t systemAll, uint64_t process, int cores);
void co_framecost_test_begin(int64_t now, int live);
void co_framecost_test_mark(int part, int64_t now);
int co_framecost_test_lines(void);
/* The last closed window: 1% and 0.1% lows (fps), frames over 25 and 33 ms. */
void co_framecost_test_last(double* low1, double* low01, uint32_t* over25, uint32_t* over33);
#endif

#endif
