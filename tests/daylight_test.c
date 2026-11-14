/* The test daylight switch's closed loop (xl/co_daylight.h): the minute wrap, the angle wrap, and the loop against
 * model skies: linear either way, uneven, a sky installed in between, and a clock that ignores the angle. */
#include <math.h>
#include <stdio.h>

#include "xl/co_daylight.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NEAR(a, b, by) (fabsf((a) - (b)) <= (by))

static float Day(float m) { m = fmodf(m, 1440.0f); return m < 0 ? m + 1440.0f : m; }
/* Model skies: the clock at angle x. */
static float g_m0, g_slope;
static float Linear(float x) { return Day(g_m0 + g_slope * x * 1440.0f / CO_DAY_TAU); }
static float Uneven(float x) { return Day(g_m0 + (x + 0.6f * sinf(x)) * 1440.0f / CO_DAY_TAU); } /* rising, 0.4x to 1.6x */

/* Readings until the clock is on the wanted minute, or -1 after 40. */
static int Settle(CoDayLoop* loop, float (*sky)(float), float want)
{
    const char* why;
    for (int n = 0; n < 40; n++)
    {
        const float m = sky(loop->x);
        if (!co_day_loop(loop, m, want, &why) && fabsf(co_day_diff(m, want)) <= CO_DAY_NEAR) return n;
    }
    return -1;
}

int main(void)
{
    g_case = "the minute wrap";
    CHECK(co_day_diff(410, 1020) == 610 && co_day_diff(1020, 410) == -610);
    CHECK(co_day_diff(1185, 250) == 505 && co_day_diff(250, 1185) == -505); /* over midnight, the short way */
    CHECK(co_day_diff(0, 720) == 720 && co_day_diff(720, 0) == 720);        /* half a day: +720, never -720 */
    CHECK(co_day_diff(1439, 1) == 2 && co_day_diff(1, 1439) == -2 && co_day_diff(0, 1440) == 0);

    g_case = "the angle wrap";
    CHECK(NEAR(co_day_angle(4.0f), 4.0f - CO_DAY_TAU, 1e-5f) && NEAR(co_day_angle(-4.0f), CO_DAY_TAU - 4.0f, 1e-5f));
    CHECK(co_day_angle(1.0f) == 1.0f && NEAR(co_day_angle(3.0f * CO_DAY_TAU + 0.5f), 0.5f, 1e-4f));

    g_case = "a linear sky: one step, the angle of planet 211's mission (625.88 min at 0, 229.18 min a radian)";
    CoDayLoop loop = {0};
    g_m0 = 625.88f; g_slope = 1.0f;
    CHECK(Settle(&loop, Linear, 720.0f) == 1 && NEAR(loop.x, 0.4107f, 0.002f));
    CHECK(Settle(&loop, Linear, 1020.0f) == 1 && NEAR(loop.x, 1.7197f, 0.002f));
    CHECK(Settle(&loop, Linear, 1020.0f) == 0); /* on the minute: no step */

    g_case = "another sky installed: followed from the angle in use";
    g_m0 = 97.0f;
    CHECK(Settle(&loop, Linear, 1020.0f) == 1 && NEAR(Linear(loop.x), 1020.0f, 1.0f));

    g_case = "minutes falling with the angle: the sign turns, then one step";
    CoDayLoop falling = {0};
    g_m0 = 300.0f; g_slope = -1.0f;
    int n = Settle(&falling, Linear, 720.0f);
    CHECK(n > 0 && n <= 4 && falling.sign == -1.0f && NEAR(Linear(falling.x), 720.0f, CO_DAY_NEAR));

    g_case = "an uneven rising sky: reached by read back";
    CoDayLoop uneven = {0};
    g_m0 = 1181.0f;
    n = Settle(&uneven, Uneven, 1020.0f);
    CHECK(n > 0 && n <= 12 && NEAR(Uneven(uneven.x), 1020.0f, CO_DAY_NEAR));

    g_case = "a clock that ignores the angle: three steps, then rest until it or the wanted minute moves";
    CoDayLoop dead = {0};
    const char* why = NULL;
    int steps = 0, rested = 0;
    for (int i = 0; i < 20; i++) { steps += co_day_loop(&dead, 0.0f, 720.0f, &why); rested += why != NULL; }
    CHECK(steps == 3 && rested == 1 && dead.resting);
    CHECK(co_day_loop(&dead, 400.0f, 720.0f, &why) == 1 && !dead.resting); /* a sky came into use */
    for (int i = 0; i < 6; i++) co_day_loop(&dead, 400.0f, 720.0f, &why);
    CHECK(dead.resting && co_day_loop(&dead, 400.0f, 1020.0f, &why) == 1); /* the harness asked for another minute */

    g_case = "readings that are not a clock, and a wanted minute that is not one: nothing moves";
    CoDayLoop off = {0};
    CHECK(!co_day_loop(&off, -1.0f, 720.0f, &why) && !co_day_loop(&off, 2000.0f, 720.0f, &why) && !co_day_loop(&off, NAN, 720.0f, &why));
    CHECK(!co_day_loop(&off, 300.0f, -5.0f, &why) && !co_day_loop(&off, 300.0f, NAN, &why) && off.x == 0.0f);

    printf("daylight: %d failed\n", g_failed);
    return g_failed != 0;
}
