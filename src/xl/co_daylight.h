/* The test daylight switch's closed loop (proto/test_daylight.c): the sun's spin angle X follows the game's own clock
 * to a wanted minute. With the spin and the orbit both fixed the clock is linear in X (1440 min a turn), so one step
 * lands; the loop still reads back, because a sky can be installed at any time (ship, preview, mission). */
#ifndef CO_DAYLIGHT_H
#define CO_DAYLIGHT_H

#include <math.h>

#define CO_DAY_MINUTES 1440.0f
#define CO_DAY_TAU 6.2831853f
#define CO_DAY_NEAR 2.0f /* minutes: the clock counts as on the wanted minute */
#define CO_DAY_MISSES 3  /* steps the clock may ignore before the loop rests */

typedef struct CoDayLoop
{
    float x;      /* the angle, in (-pi, pi] */
    float sign;   /* +1 or -1: minutes rise or fall with the angle; 0 counts as +1 */
    float last;   /* the clock at the last step, and the minutes that step asked for */
    float asked;
    int stepped;  /* a step is waiting for its read back */
    int misses;   /* steps in a row the clock did not follow */
    int resting;  /* the clock ignores the angle (no sky in use): no steps until it or the wanted minute moves */
    float restClock, restWant;
} CoDayLoop;

/* b - a in minutes of the day, in (-720, 720]. */
static inline float co_day_diff(float a, float b)
{
    float d = fmodf(b - a, CO_DAY_MINUTES);
    if (d > CO_DAY_MINUTES / 2) d -= CO_DAY_MINUTES;
    if (d <= -CO_DAY_MINUTES / 2) d += CO_DAY_MINUTES;
    return d;
}

/* An angle in (-pi, pi]. */
static inline float co_day_angle(float x)
{
    x = fmodf(x, CO_DAY_TAU);
    if (x > CO_DAY_TAU / 2) x -= CO_DAY_TAU;
    if (x <= -CO_DAY_TAU / 2) x += CO_DAY_TAU;
    return x;
}

/* One reading of the clock. 1 when loop->x changed (the caller stores it), 0 when it stays; *why is a note for
 * the log or NULL. A clock outside 0..1440 or a wanted minute outside it changes nothing. */
static inline int co_day_loop(CoDayLoop* loop, float clock, float want, const char** why)
{
    *why = NULL;
    if (!(clock >= 0 && clock <= CO_DAY_MINUTES) || !(want >= 0 && want <= CO_DAY_MINUTES)) return 0;
    if (loop->sign == 0) loop->sign = 1;
    if (loop->resting)
    {
        if (fabsf(co_day_diff(loop->restClock, clock)) < 1.0f && want == loop->restWant) return 0;
        loop->resting = 0; loop->misses = 0; loop->stepped = 0;
    }
    if (loop->stepped)
    {
        const float moved = co_day_diff(loop->last, clock);
        loop->stepped = 0;
        if (fabsf(moved) < 0.5f)
        {
            if (++loop->misses >= CO_DAY_MISSES)
            {
                loop->resting = 1; loop->restClock = clock; loop->restWant = want;
                *why = "the clock does not follow the angle (no sky in use): resting";
                return 0;
            }
        }
        else
        {
            loop->misses = 0;
            if (moved * loop->asked < 0) { loop->sign = -loop->sign; *why = "the clock ran the other way: sign turned"; }
        }
    }
    const float off = co_day_diff(clock, want);
    if (fabsf(off) <= CO_DAY_NEAR) return 0;
    loop->x = co_day_angle(loop->x + loop->sign * off * CO_DAY_TAU / CO_DAY_MINUTES);
    loop->last = clock; loop->asked = off; loop->stepped = 1;
    return 1;
}

#endif
