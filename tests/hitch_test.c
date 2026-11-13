/* The frames as a reinforcement pod comes in: a watch from the pod's start, the slowest frames kept with the mod's share,
 * counts over the frame-time marks, the watch's end, and a bounded number of watches per level. */
#include <stdio.h>
#include <string.h>

#include "xl/co_hitch.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static CoHitch g_h;
static int Frame(uint64_t ms, float frame, float stream, uint16_t removed, uint16_t sun)
{
    CoHitchCost c;
    memset(&c, 0, sizeof c);
    c.stream = stream; c.window = 0.25f; c.spawn = 0.5f; c.living = 1.0f; c.tiles = 0.125f; c.removed = removed; c.sunWhole = sun;
    return co_hitch_frame(&g_h, ms, frame, &c);
}

int main(void)
{
    co_hitch_level(&g_h, 3);

    g_case = "no watch before a pod";
    CHECK(Frame(1000, 80.0f, 0, 0, 0) == 0 && g_h.frames == 0 && g_h.top[0].ms == 0.0f);

    g_case = "a pod starts a watch";
    CHECK(co_hitch_arm(&g_h, 10000) == 1 && g_h.on && g_h.watches == 1);
    CHECK(co_hitch_arm(&g_h, 10500) == 0 && g_h.watches == 1 && g_h.startMs == 10000); /* one at a time */

    g_case = "every frame counts; the slowest are kept, slowest first, with their time and the mod's share";
    int over = 0;
    for (int k = 0; k < 100; k++)
    {
        float ms = 10.0f;
        if (k == 20) ms = 60.0f;
        if (k == 40) ms = 30.0f;
        if (k == 41) ms = 20.0f;
        if (k == 60) ms = 40.0f;
        over |= Frame(10000 + (uint64_t)k * 16, ms, k == 20 ? 3.0f : 0.5f, k == 20 ? 4 : 0, k == 60 ? 1 : 0);
    }
    CHECK(over == 0 && g_h.frames == 100);
    CHECK(g_h.over16 == 4 && g_h.over25 == 3 && g_h.over33 == 2 && g_h.over50 == 1);
    CHECK(g_h.top[0].ms == 60.0f && g_h.top[0].atMs == 320 && g_h.top[0].cost.removed == 4 && g_h.top[0].cost.stream == 3.0f);
    CHECK(g_h.top[1].ms == 40.0f && g_h.top[1].atMs == 960 && g_h.top[1].cost.sunWhole == 1);
    CHECK(g_h.top[2].ms == 30.0f && g_h.top[3].ms == 20.0f && g_h.top[4].ms == 10.0f && g_h.top[CO_HITCH_KEEP - 1].ms == 10.0f);
    CHECK(g_h.removed == 4 && g_h.sunWhole == 1);
    CHECK(g_h.sum == 96.0f * 10.0f + 60.0f + 40.0f + 30.0f + 20.0f);
    CHECK(co_hitch_own(&g_h.top[0].cost) == 3.0f + 0.25f + 0.5f + 1.0f + 0.125f);
    CHECK(g_h.own == 99.0f * (0.5f + 1.875f) + 3.0f + 1.875f);

    g_case = "a stalled clock's frame is left out of the figures";
    CHECK(Frame(11700, 20000.0f, 0, 0, 0) == 0 && g_h.frames == 100 && g_h.top[0].ms == 60.0f);
    CHECK(Frame(11710, -1.0f, 0, 0, 0) == 0 && g_h.frames == 100);

    g_case = "the watch ends with the first frame past its time; the figures stay for the note";
    CHECK(Frame(10000 + CO_HITCH_WATCH_MS - 1, 10.0f, 0, 0, 0) == 0 && g_h.on);
    CHECK(Frame(10000 + CO_HITCH_WATCH_MS, 10.0f, 0, 0, 0) == 1 && !g_h.on && g_h.frames == 102 && g_h.top[0].ms == 60.0f);
    CHECK(Frame(10000 + CO_HITCH_WATCH_MS + 16, 90.0f, 0, 0, 0) == 0 && g_h.frames == 102);

    g_case = "the next pod starts afresh";
    CHECK(co_hitch_arm(&g_h, 30000) == 1 && g_h.frames == 0 && g_h.top[0].ms == 0.0f && g_h.over16 == 0 && g_h.watches == 2 && g_h.level == 3);
    CHECK(Frame(30016, 15.0f, 0, 0, 0) == 0 && g_h.top[0].ms == 15.0f && g_h.top[0].atMs == 16 && g_h.over16 == 0);

    g_case = "a level has its CO_HITCH_WATCHES";
    int armed = 0;
    for (int k = 0; k < 40; k++) { g_h.on = 0; armed += co_hitch_arm(&g_h, 40000 + (uint64_t)k); }
    CHECK(armed == CO_HITCH_WATCHES - 2 && g_h.watches == CO_HITCH_WATCHES);

    g_case = "the same level keeps its count; a new level starts it again";
    co_hitch_level(&g_h, 3);
    g_h.on = 0;
    CHECK(co_hitch_arm(&g_h, 50000) == 0);
    co_hitch_level(&g_h, 4);
    CHECK(g_h.watches == 0 && !g_h.on && g_h.level == 4 && co_hitch_arm(&g_h, 60000) == 1);

    if (g_failed) { printf("hitch: %d failed\n", g_failed); return 1; }
    printf("hitch: all passed\n");
    return 0;
}
