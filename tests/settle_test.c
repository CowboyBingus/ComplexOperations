/* Where the world streams for a player passing in a hellpod: the first reading counts, a jump or a speed above a
 * Helldiver's keeps the place held until one second calm, a new level forgets every player, and stamps 2 km from every
 * player go with the bigger batch. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_settle.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static CoSettle g_s;
static float g_use[2];
static int At(uint32_t player, float x, float y, uint64_t ms)
{
    const float at[2] = {x, y};
    return co_settle_at(&g_s, player, at, ms, g_use);
}
static int Used(float x, float y) { return g_use[0] == x && g_use[1] == y; }

int main(void)
{
    co_settle_level(&g_s, 1);

    g_case = "the first reading counts as it is";
    CHECK(At(0, -1610.0f, -2381.0f, 1000) == 0 && Used(-1610.0f, -2381.0f));

    g_case = "a Helldiver running and a vehicle at speed";
    CHECK(At(0, -1608.0f, -2381.0f, 1250) == 0 && Used(-1608.0f, -2381.0f));  /* 8 m/s */
    CHECK(At(0, -1594.0f, -2381.0f, 1750) == 0 && Used(-1594.0f, -2381.0f));  /* 28 m/s */
    CHECK(At(0, -1579.0f, -2381.0f, 2000) == 0 && Used(-1579.0f, -2381.0f));  /* 60 m/s: not above */

    g_case = "the new Helldiver at the pod's start: a jump, the place held stays";
    CHECK(At(0, -838.0f, -1726.0f, 30000) == 1 && Used(-1579.0f, -2381.0f));  /* 989 m in 28 s: under 60 m/s, over 200 m */
    g_case = "the pod sweeping in";
    CHECK(At(0, -1088.0f, -1900.0f, 30250) == 1 && Used(-1579.0f, -2381.0f));
    CHECK(At(0, -1300.0f, -2050.0f, 30500) == 1 && Used(-1579.0f, -2381.0f));
    CHECK(At(0, -1316.0f, -2061.0f, 30750) == 1 && Used(-1579.0f, -2381.0f)); /* 78 m/s */
    g_case = "slow, but not yet one second calm";
    CHECK(At(0, -1320.0f, -2064.0f, 31000) == 1 && Used(-1579.0f, -2381.0f));
    CHECK(At(0, -1321.0f, -2064.0f, 31900) == 1 && Used(-1579.0f, -2381.0f));
    g_case = "one second calm: the landing counts";
    CHECK(At(0, -1321.0f, -2065.0f, 32000) == 0 && Used(-1321.0f, -2065.0f));
    CHECK(At(0, -1320.0f, -2065.0f, 32250) == 0 && Used(-1320.0f, -2065.0f));

    g_case = "a burst inside the calm second starts it again";
    CHECK(At(1, 0.0f, 0.0f, 0) == 0);
    CHECK(At(1, 500.0f, 0.0f, 250) == 1 && Used(0.0f, 0.0f));
    CHECK(At(1, 501.0f, 0.0f, 500) == 1);
    CHECK(At(1, 530.0f, 0.0f, 750) == 1);   /* 116 m/s */
    CHECK(At(1, 531.0f, 0.0f, 1000) == 1);
    CHECK(At(1, 531.0f, 0.0f, 1600) == 1 && Used(0.0f, 0.0f)); /* 1.1 s after the first calm reading, 0.85 s after the burst */
    CHECK(At(1, 531.0f, 0.0f, 2000) == 0 && Used(531.0f, 0.0f));

    g_case = "a reading that is not a number";
    CHECK(At(1, NAN, 0.0f, 2250) == 1 && Used(531.0f, 0.0f));
    CHECK(At(1, 531.0f, 0.0f, 2500) == 1 && Used(531.0f, 0.0f));
    CHECK(At(1, 531.0f, 0.0f, 3500) == 0);
    CHECK(At(2, NAN, 1.0f, 0) == 0 && isnan(g_use[0]) && g_use[1] == 1.0f); /* a first one: as read, and not kept */
    CHECK(At(2, 7.0f, 8.0f, 50) == 0 && Used(7.0f, 8.0f));

    g_case = "two readings at the same moment";
    CHECK(At(3, 0.0f, 0.0f, 100) == 0);
    CHECK(At(3, 150.0f, 0.0f, 100) == 0 && Used(150.0f, 0.0f)); /* no time: only a jump says passing */
    CHECK(At(3, 190.0f, 0.0f, 100) == 0 && Used(190.0f, 0.0f)); /* measured from the reading before, at (0, 0) */
    CHECK(At(3, 400.0f, 0.0f, 100) == 1 && Used(190.0f, 0.0f));

    g_case = "readings closer than 0.1 s give no speed";
    CHECK(At(4, 0.0f, 0.0f, 0) == 0);
    CHECK(At(4, 1.0f, 0.0f, 1) == 0 && Used(1.0f, 0.0f));   /* 1 m in the tick count's 1 ms */
    CHECK(At(4, 2.0f, 0.0f, 120) == 0 && Used(2.0f, 0.0f)); /* 17 m/s from the reading at 0 */
    CHECK(At(4, 20.0f, 0.0f, 200) == 0 && Used(20.0f, 0.0f));
    CHECK(At(4, 40.0f, 0.0f, 240) == 1 && Used(20.0f, 0.0f)); /* 317 m/s from the reading at 120 */

    g_case = "a slot past the table";
    CHECK(At(CO_SETTLE_SLOTS, 1.0f, 2.0f, 0) == 0 && Used(1.0f, 2.0f));
    CHECK(At(CO_SETTLE_SLOTS, 5000.0f, 2.0f, 10) == 0 && Used(5000.0f, 2.0f));

    g_case = "the same level keeps the players; a new one forgets them";
    co_settle_level(&g_s, 1);
    CHECK(At(0, 3000.0f, 0.0f, 33000) == 1 && Used(-1320.0f, -2065.0f));
    co_settle_level(&g_s, 2);
    CHECK(At(0, 3000.0f, 0.0f, 33100) == 0 && Used(3000.0f, 0.0f));

    g_case = "the removals";
    CHECK(co_settle_removals(1999.0f * 1999.0f, 8, 32) == 8);
    CHECK(co_settle_removals(2000.0f * 2000.0f, 8, 32) == 8);
    CHECK(co_settle_removals(2001.0f * 2001.0f, 8, 32) == 32);
    CHECK(co_settle_removals(3.402823466e38f, 8, 32) == 32);

    if (g_failed) { printf("settle: %d failed\n", g_failed); return 1; }
    printf("settle: all passed\n");
    return 0;
}
