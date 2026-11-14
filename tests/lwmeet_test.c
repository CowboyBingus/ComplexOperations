/* A road patrol meets the players: the step that enters the ring, the point it steps back to, when it waits, steps
 * back and turns, the squad that never gives way, and what a player has near as the 30 s line states it. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_lw_meet.h"
#include "xl/co_settle.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int main(void)
{
    const float one[1][3] = {{1000, 0, 0}}, two[2][3] = {{1000, 0, 0}, {0, 400, 0}};
    float at[2] = {840, 0}, to[2] = {846, 0}, out[2];

    g_case = "a step that ends inside the edge and nearer the player goes in";
    CHECK(co_lw_meet_enters(at, to, one, 1, 155.0f) == 1);
    to[0] = 844; CHECK(co_lw_meet_enters(at, to, one, 1, 155.0f) == 0); /* 156 m: still outside */
    to[0] = 845.5f; CHECK(co_lw_meet_enters(at, to, one, 1, 155.0f) == 1);
    g_case = "a step away from the player inside the edge is no entry";
    at[0] = 900; to[0] = 890; CHECK(co_lw_meet_enters(at, to, one, 1, 155.0f) == 0);
    g_case = "a step along the ring that comes no nearer is no entry";
    at[0] = 1000; at[1] = 150; to[0] = 1010; to[1] = 150; CHECK(co_lw_meet_enters(at, to, one, 1, 155.0f) == 0);
    g_case = "the second player's ring counts";
    at[0] = 0; at[1] = 200; to[0] = 0; to[1] = 250; CHECK(co_lw_meet_enters(at, to, two, 2, 155.0f) == 1 && co_lw_meet_enters(at, to, one, 1, 155.0f) == 0);
    g_case = "no player: nothing is entered";
    CHECK(co_lw_meet_enters(at, to, one, 0, 155.0f) == 0);

    g_case = "the way back out: the first point on the stretch outside the edge";
    at[0] = 900; at[1] = 0; float back[2] = {800, 0};
    CHECK(co_lw_meet_back(at, back, one, 1, 155.0f, out) == 1 && out[0] <= 845.0f && out[0] > 840.9f && out[1] == 0);
    g_case = "a stretch that ends inside the ring has no such point";
    back[0] = 850; CHECK(co_lw_meet_back(at, back, one, 1, 155.0f, out) == 0);
    g_case = "the stretch's end counts when only it is outside";
    back[0] = 845; CHECK(co_lw_meet_back(at, back, one, 1, 155.0f, out) == 1 && out[0] == 845.0f);
    g_case = "a point clear of one player and inside another's ring is not taken";
    { const float pair[2][3] = {{1000, 0, 0}, {700, 0, 0}}; back[0] = 800; CHECK(co_lw_meet_back(at, back, pair, 2, 155.0f, out) == 0); }
    g_case = "standing on the node: nowhere to step";
    back[0] = 900; CHECK(co_lw_meet_back(at, back, one, 1, 155.0f, out) == 0);

    g_case = "it waits outside the ring, steps back with a player inside it, turns after the wait";
    CHECK(co_lw_meet_next(1000, 2000, 151.0f * 151.0f, 150.0f) == CO_LW_MEET_WAITS);
    CHECK(co_lw_meet_next(1000, 2000, 149.0f * 149.0f, 150.0f) == CO_LW_MEET_BACK);
    CHECK(co_lw_meet_next(1000, 1000 + CO_LW_MEET_WAIT_MS - 1, 151.0f * 151.0f, 150.0f) == CO_LW_MEET_WAITS);
    CHECK(co_lw_meet_next(1000, 1000 + CO_LW_MEET_WAIT_MS, 151.0f * 151.0f, 150.0f) == CO_LW_MEET_TURNS);
    CHECK(co_lw_meet_next(1000, 1000 + CO_LW_MEET_WAIT_MS, 100.0f, 150.0f) == CO_LW_MEET_TURNS);

    g_case = "a road patrol within the online radius never gives way; others do";
    CHECK(co_lw_meet_keeps(1, 249.0f * 249.0f, 250.0f, 8, 72) == 1 && co_lw_meet_keeps(2, 100.0f, 250.0f, 8, 72) == 1);
    CHECK(co_lw_meet_keeps(1, 250.0f * 250.0f, 250.0f, 8, 72) == 0 && co_lw_meet_keeps(0, 100.0f, 250.0f, 8, 72) == 0);
    g_case = "a road patrol is kept only while the road patrols hold no more than half their bubble's budget";
    CHECK(co_lw_meet_keeps(1, 100.0f, 250.0f, 11, 23) == 1 && co_lw_meet_keeps(1, 100.0f, 250.0f, 12, 23) == 0 && co_lw_meet_keeps(1, 100.0f, 250.0f, 36, 72) == 1);
    g_case = "a filler squad leaves for a road patrol only within that half";
    CHECK(co_lw_meet_road_room(0, 8, 23) == 1 && co_lw_meet_road_room(8, 8, 23) == 0 && co_lw_meet_road_room(28, 8, 72) == 1 && co_lw_meet_road_room(29, 8, 72) == 0);

    g_case = "the time between two calls: a far walk over the squads the kept half of the far share holds, for each player on a road";
    CHECK(co_lw_meet_call_ms(18.0f, 4, 1, 465.0f) == 103333u && co_lw_meet_call_ms(46.0f, 4, 8, 661.0f) > 459000u && co_lw_meet_call_ms(46.0f, 4, 8, 661.0f) < 460500u);
    CHECK(co_lw_meet_call_ms(18.0f, 8, 1, 465.0f) > 206000u && co_lw_meet_call_ms(18.0f, 4, 0, 465.0f) == 103333u);
    g_case = "never under a minute or over ten; a share too small for one squad still passes one at a time";
    CHECK(co_lw_meet_call_ms(72.0f, 4, 1, 100.0f) == CO_LW_MEET_CALL_MIN_MS && co_lw_meet_call_ms(18.0f, 4, 8, 3000.0f) == CO_LW_MEET_CALL_MAX_MS);
    CHECK(co_lw_meet_call_ms(4.0f, 4, 1, 465.0f) == 232500u);
    g_case = "the patrol to call: the wanted side first, then the nearer";
    CHECK(co_lw_meet_call_better(5, 1, -1, 0) == 1 && co_lw_meet_call_better(5, 0, -1, 1) == 1);
    CHECK(co_lw_meet_call_better(9, 1, 3, 0) == 1 && co_lw_meet_call_better(3, 0, 9, 1) == 0);
    CHECK(co_lw_meet_call_better(3, 1, 9, 1) == 1 && co_lw_meet_call_better(9, 1, 3, 1) == 0 && co_lw_meet_call_better(3, 0, 3, 0) == 0 && co_lw_meet_call_better(3, 0, 9, 0) == 1);
    g_case = "a patrol refused the far share stands while its refusal is fresh, 90 s at most";
    CHECK(co_lw_meet_zone_waits(1000, 5000, 6000) == 1 && co_lw_meet_zone_waits(1000, 5000, 5000 + CO_LW_MEET_ZONE_ASK_MS - 1) == 1);
    CHECK(co_lw_meet_zone_waits(1000, 5000, 5000 + CO_LW_MEET_ZONE_ASK_MS) == 0 && co_lw_meet_zone_waits(1000, 0, 1500) == 0);
    CHECK(co_lw_meet_zone_waits(1000, 90500, 1000 + CO_LW_MEET_ZONE_WAIT_MS - 1) == 1 && co_lw_meet_zone_waits(1000, 90500, 1000 + CO_LW_MEET_ZONE_WAIT_MS) == 0);
    g_case = "a road patrol has 4 units of a larger group, and a small group's own";
    CHECK(co_lw_meet_road_members(9) == 4 && co_lw_meet_road_members(4) == 4 && co_lw_meet_road_members(3) == 3 && co_lw_meet_road_members(1) == 1);

    g_case = "a player on a road patrol's way: within the width of a stretch, its ends and a single point";
    { const float way[3][2] = {{0, 0}, {100, 0}, {100, 200}}, on[2] = {10, 79}, off[2] = {10, 81}, corner[2] = {150, 250}, past[2] = {-60, -60}, by[2] = {160, 100};
      CHECK(co_lw_meet_on_way(way, 3, on, 80.0f) == 1 && co_lw_meet_on_way(way, 3, off, 80.0f) == 0);
      CHECK(co_lw_meet_on_way(way, 3, corner, 80.0f) == 1 && co_lw_meet_on_way(way, 3, past, 80.0f) == 0);
      CHECK(co_lw_meet_on_way(way, 3, by, 80.0f) == 1 && co_lw_meet_on_way(way, 2, by, 80.0f) == 0); /* the second stretch only */
      CHECK(co_lw_meet_on_way(way, 1, past, 90.0f) == 1 && co_lw_meet_on_way(way, 1, by, 80.0f) == 0 && co_lw_meet_on_way(way, 0, on, 80.0f) == 0); }

    g_case = "inside the ring and beyond the outer zone a patrol has no rule";
    CHECK(co_lw_meet_act(1, 149.0f, 150.0f, 250.0f, -451.0f, 0, 0, 1, 1) == CO_LW_MEET_WALK && co_lw_meet_act(1, 700.0f, 150.0f, 250.0f, 100.0f, 0, 0, 1, 1) == CO_LW_MEET_WALK);
    g_case = "beyond the band: a patrol on a player's road that is seen asks there; hidden, or off the roads, it walks on";
    CHECK(co_lw_meet_act(1, 699.0f, 150.0f, 250.0f, 99.0f, 0, 0, 0, 1) == CO_LW_MEET_ASK_BEYOND && co_lw_meet_act(1, 600.0f, 150.0f, 250.0f, 0.0f, 0, 0, 1, 1) == CO_LW_MEET_ASK_BEYOND);
    CHECK(co_lw_meet_act(1, 650.0f, 150.0f, 250.0f, 50.0f, 1, 1, 0, 1) == CO_LW_MEET_WALK && co_lw_meet_act(0, 650.0f, 150.0f, 250.0f, 50.0f, 0, 0, 1, 1) == CO_LW_MEET_WALK);
    g_case = "beyond the band a group never measured does not ask: its band is a guess";
    CHECK(co_lw_meet_act(1, 650.0f, 150.0f, 250.0f, 50.0f, 0, 0, 0, 0) == CO_LW_MEET_WALK && co_lw_meet_act(1, 400.0f, 150.0f, 250.0f, -200.0f, 0, 0, 1, 0) == CO_LW_MEET_ASK_COVER);
    g_case = "the bands decide the zone, not the nearest player's distance: 900 m out inside a scoped player's band is inside";
    CHECK(co_lw_meet_act(1, 700.0f, 150.0f, 250.0f, -1.0f, 0, 0, 0, 1) == CO_LW_MEET_WALK && co_lw_meet_act(1, 900.0f, 150.0f, 250.0f, 0.0f, 0, 0, 0, 1) == CO_LW_MEET_ASK_BEYOND);
    CHECK(co_lw_meet_act(1, 900.0f, 150.0f, 250.0f, 99.0f, 0, 0, 0, 1) == CO_LW_MEET_ASK_BEYOND && co_lw_meet_act(1, 900.0f, 150.0f, 250.0f, 100.0f, 0, 0, 0, 1) == CO_LW_MEET_WALK);
    g_case = "it stays for its walk in from where it came online to the offline radius";
    CHECK(co_lw_meet_stay_ms(794.0f, 350.0f) == 296000u && co_lw_meet_stay_ms(1011.0f, 350.0f) > 440000u && co_lw_meet_stay_ms(1011.0f, 350.0f) < 441000u && co_lw_meet_stay_ms(300.0f, 350.0f) == 0);
    g_case = "beside the road: the first point with a clear line and no enemy near; with none, the clear one with the fewest";
    { const uint8_t clear[4] = {0, 1, 1, 1}; const uint16_t crowd[4] = {0, 41, 0, 0}, all[4] = {0, 41, 7, 9}, none[4] = {0, 0, 0, 0}; const uint8_t blocked[4] = {0, 0, 0, 0}; int least = 9;
      CHECK(co_lw_meet_beside(clear, crowd, 4, &least) == 2 && co_lw_meet_beside(clear, none, 4, &least) == 1);
      CHECK(co_lw_meet_beside(clear, all, 4, &least) == -1 && least == 2);
      CHECK(co_lw_meet_beside(blocked, none, 4, &least) == -1 && least == -1 && co_lw_meet_beside(clear, none, 0, &least) == -1 && least == -1); }
    g_case = "a sighting a player noticed: 5 s of watching it walk without a break, told once";
    { uint8_t run = 0; int told = 0;
      for (int k = 0; k < 4; k++) told += co_lw_meet_watched(&run, 1);
      CHECK(told == 0 && co_lw_meet_watched(&run, 0) == 0 && run == 0);
      for (int k = 0; k < 4; k++) told += co_lw_meet_watched(&run, 1);
      CHECK(told == 0 && co_lw_meet_watched(&run, 1) == 1 && co_lw_meet_watched(&run, 1) == 0 && co_lw_meet_watched(&run, 0) == 0 && co_lw_meet_watched(&run, 1) == 0);
      run = 0; for (int k = 0; k < 5; k++) told += co_lw_meet_watched(&run, 1);
      CHECK(told == 1); }
    g_case = "inside the band: seen with a remembered point it steps back, road patrol or not; seen without one it walks";
    CHECK(co_lw_meet_act(1, 599.0f, 150.0f, 250.0f, -1.0f, 0, 0, 1, 1) == CO_LW_MEET_ASK_COVER && co_lw_meet_act(0, 200.0f, 150.0f, 250.0f, -400.0f, 0, 0, 1, 1) == CO_LW_MEET_ASK_COVER);
    CHECK(co_lw_meet_act(1, 150.0f, 150.0f, 250.0f, -450.0f, 0, 1, 1, 1) == CO_LW_MEET_ASK_COVER && co_lw_meet_act(1, 400.0f, 150.0f, 250.0f, -200.0f, 0, 1, 0, 1) == CO_LW_MEET_WALK);
    g_case = "hidden beyond the online radius: only a patrol on a player's road whose next stretch is seen asks";
    CHECK(co_lw_meet_act(1, 400.0f, 150.0f, 250.0f, -200.0f, 1, 1, 0, 1) == CO_LW_MEET_ASK && co_lw_meet_act(1, 400.0f, 150.0f, 250.0f, -200.0f, 1, 0, 0, 1) == CO_LW_MEET_WALK);
    CHECK(co_lw_meet_act(0, 400.0f, 150.0f, 250.0f, -200.0f, 1, 1, 0, 1) == CO_LW_MEET_WALK && co_lw_meet_act(1, 250.0f, 150.0f, 250.0f, -350.0f, 1, 1, 1, 1) == CO_LW_MEET_ASK);
    g_case = "hidden inside the online radius: the near band's own request asks";
    CHECK(co_lw_meet_act(1, 249.0f, 150.0f, 250.0f, -351.0f, 1, 1, 1, 1) == CO_LW_MEET_WALK);

    g_case = "a remembered point: on the same plan, fresh, the edge from every player, behind the squad";
    { CoLwMeetCover c; memset(&c, 0, sizeof c); const uint16_t path[4] = {7, 8, 9, 10}; const float here[2] = {500, 0}, mem[2] = {480, 0};
      const float far1[1][3] = {{700, 0, 0}}, near2[2][3] = {{700, 0, 0}, {480, 154, 0}};
      CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 5000) == 0); /* none remembered */
      co_lw_meet_cover_set(&c, mem, 6, 8, 1, 1000);
      CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 5000) == 1 && co_lw_meet_cover_ok(&c, here, path, 2, 4, far1, 1, 155.0f, 5000) == 1);
      CHECK(co_lw_meet_cover_ok(&c, here, path, 0, 4, far1, 1, 155.0f, 5000) == 0); /* a new plan: it has not come that far */
      { const uint16_t other[4] = {7, 3, 9, 10}; CHECK(co_lw_meet_cover_ok(&c, here, other, 1, 4, far1, 1, 155.0f, 5000) == 0); }
      CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 1, far1, 1, 155.0f, 5000) == 0); /* a plan that ends before it */
      CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 1000 + CO_LW_MEET_COVER_MS - 1) == 1 && co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 1000 + CO_LW_MEET_COVER_MS) == 0);
      CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, near2, 2, 155.0f, 5000) == 0 && co_lw_meet_cover_ok(&c, here, path, 1, 4, near2, 1, 155.0f, 5000) == 1);
      CHECK(co_lw_meet_cover_ok(&c, mem, path, 1, 4, far1, 1, 155.0f, 5000) == 0); /* it stands there */
      c.steps = CO_LW_MEET_COVER_STEPS - 1; CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 5000) == 1);
      c.steps = CO_LW_MEET_COVER_STEPS; CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 5000) == 0);
      c.steps = 0; c.known = 0; CHECK(co_lw_meet_cover_ok(&c, here, path, 1, 4, far1, 1, 155.0f, 5000) == 0); } /* used once: forgotten */

    g_case = "the far share: half is kept for patrols on a player's road, and the whole is never passed";
    CHECK(co_lw_meet_far_room(0, 0, 8, 18, 0, 1) == 1 && co_lw_meet_far_room(2, 0, 8, 18, 0, 1) == 0 && co_lw_meet_far_room(2, 0, 8, 18, 0, 0) == 1);
    CHECK(co_lw_meet_far_room(8, 0, 8, 18, 1, 1) == 1 && co_lw_meet_far_room(11, 0, 8, 18, 1, 1) == 0 && co_lw_meet_far_room(10, 0, 8, 18, 1, 1) == 1);
    CHECK(co_lw_meet_far_room(9, 8, 8, 18, 0, 1) == 1 && co_lw_meet_far_room(10, 8, 8, 18, 0, 1) == 0 && co_lw_meet_far_room(16, 16, 8, 18, 0, 1) == 0);

    g_case = "room held for patrols: one hold a bubble, counted in its bubble only and not against its own squad";
    { static CoLwMeetHolds h; int bubble[CO_LW_MEET_HOLDS]; for (int k = 0; k < CO_LW_MEET_HOLDS; k++) bubble[k] = -1;
      CHECK(co_lw_meet_held(&h, bubble, 3, 0, 1000) == 0);
      CHECK(co_lw_meet_hold(&h, bubble, 10, 8, 0, 1000, 20000) == 1);
      bubble[0] = 0;
      CHECK(co_lw_meet_held(&h, bubble, 3, 0, 1000) == 8 && co_lw_meet_held(&h, bubble, 10, 0, 1000) == 0 && co_lw_meet_held(&h, bubble, 3, 1, 1000) == 0);
      CHECK(co_lw_meet_hold(&h, bubble, 20, 6, 0, 2000, 20000) == 0 && co_lw_meet_hold(&h, bubble, 30, 6, 1, 2000, 20000) == 1);
      bubble[1] = 1;
      CHECK(co_lw_meet_held(&h, bubble, 3, 1, 3000) == 6 && co_lw_meet_held(&h, bubble, 3, 0, 20999) == 8 && co_lw_meet_held(&h, bubble, 3, 0, 21000) == 0);
      CHECK(co_lw_meet_hold(&h, bubble, 20, 6, 0, 21000, 20000) == 1 && h.idx[0] == 20); /* the lapsed hold's place */
      co_lw_meet_hold_end(&h, 30);
      CHECK(co_lw_meet_held(&h, bubble, 3, 1, 3000) == 0);
      for (int k = 0; k < CO_LW_MEET_HOLDS; k++) { bubble[k] = 5 + k; h.idx[k] = k; h.until[k] = 99999; }
      CHECK(co_lw_meet_hold(&h, bubble, 40, 6, 0, 1000, 20000) == 0); } /* the table is full */

    g_case = "travel steps read by the hellpod rule: never a pass at 20 m/s or less, whatever the tick";
    { static const uint32_t tick[5] = {7, 16, 33, 100, 250}; static const float speed[3] = {3.0f, 15.0f, 20.0f};
      for (int v = 0; v < 3; v++) for (int t = 0; t < 5; t++)
      {
          static CoSettle st; float at[2] = {0, 0}, use[2]; uint64_t now = 5000; int passed = 0; double way = 0;
          memset(&st, 0, sizeof st);
          for (int k = 0; k < 2000; k++)
          {
              passed |= co_settle_at(&st, 0, at, now, use);
              const float step = co_lw_meet_travel_step(speed[v], (float)tick[t] / 1000.0f);
              at[0] += step; way += step; now += tick[t];
              if (k % 97 == 0) { now += 1; passed |= co_settle_at(&st, 0, at, now, use); } /* a second reader a millisecond on */
          }
          CHECK(passed == 0);
          CHECK(tick[t] > 100 || fabs(way / (2000.0 * tick[t] / 1000.0) - speed[v]) < 0.01 * speed[v]); /* the speed it stands for */
      } }
    g_case = "a travel step is capped at a tenth of a second and at 2 m";
    CHECK(co_lw_meet_travel_step(20.0f, 0.016f) > 0.319f && co_lw_meet_travel_step(20.0f, 0.016f) < 0.321f && co_lw_meet_travel_step(15.0f, 5.0f) == 1.5f && co_lw_meet_travel_step(40.0f, 0.1f) == 2.0f);
    g_case = "without the cap the same travel is read as a hellpod";
    { static CoSettle st; float at[2] = {0, 0}, use[2]; uint64_t now = 5000; int passed = 0;
      for (int k = 0; k < 50; k++) { passed |= co_settle_at(&st, 0, at, now, use); at[0] += 13.0f; now += 110; }
      CHECK(passed == 1); }

    g_case = "a sighting is new after five samples out of view, and counted once";
    { uint8_t gap = 255; CHECK(co_lw_meet_sighting(&gap, 1) == 1 && co_lw_meet_sighting(&gap, 1) == 0);
      for (int k = 0; k < 4; k++) CHECK(co_lw_meet_sighting(&gap, 0) == 0);
      CHECK(co_lw_meet_sighting(&gap, 1) == 0); /* four samples: the same sighting */
      for (int k = 0; k < 5; k++) co_lw_meet_sighting(&gap, 0);
      CHECK(co_lw_meet_sighting(&gap, 1) == 1);
      gap = 254; co_lw_meet_sighting(&gap, 0); co_lw_meet_sighting(&gap, 0); CHECK(gap == 255); }

    g_case = "near a player: the first sample weighs nothing";
    static CoLwMeet m; char line[512];
    const float dist[4] = {40, 120, 240, 260}; const uint8_t units[4] = {6, 8, 5, 9}, road[4] = {0, 1, 1, 1};
    co_lw_meet_sample(&m, 0, 10000, 1, dist, units, road, 4);
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 0);
    g_case = "near a player: squads by distance, units by ring, time by a road";
    co_lw_meet_sample(&m, 0, 11000, 1, dist, units, road, 4);  /* 1 s: road squads at 120 and 240 m */
    co_lw_meet_sample(&m, 0, 12000, 0, dist, units, road, 1);  /* 1 s: only the squad off the roads */
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 1);
    CHECK(strstr(line, "near player 1 (2 s, by a road 50%)") != NULL);
    CHECK(strstr(line, "within 250 m 1.00 on average (nearest 120 m on average, least 120 m; none 50% of the time)") != NULL);
    CHECK(strstr(line, "within 50 / 100 / 150 m 6.0 / 6.0 / 10.0 on average") != NULL);
    g_case = "a nearer squad later lowers the least; a gap over 3 s weighs 3 s";
    { const float d2[1] = {40}; const uint8_t u2[1] = {7}, r2[1] = {1}; co_lw_meet_sample(&m, 0, 22000, 1, d2, u2, r2, 1); }
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 1 && strstr(line, "(5 s, by a road 80%)") != NULL && strstr(line, "least 40 m") != NULL);
    CHECK(strstr(line, "nearest 60 m on average") != NULL); /* 1 s at 120 m, 3 s at 40 m */
    g_case = "no road squad near: said so";
    co_lw_meet_restart(&m);
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 0);
    co_lw_meet_sample(&m, 0, 23000, 0, dist, units, road, 1);
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 1 && strstr(line, "(no such squad; none 100% of the time)") != NULL && strstr(line, "(1 s, by a road 0%)") != NULL);
    g_case = "the least distance is the nearest of the window, not the last";
    co_lw_meet_restart(&m);
    { const float da[1] = {200}, db[1] = {90}, dc[1] = {210}; const uint8_t u1[1] = {5}, r1[1] = {1};
      co_lw_meet_sample(&m, 0, 24000, 1, da, u1, r1, 1); co_lw_meet_sample(&m, 0, 25000, 1, db, u1, r1, 1); co_lw_meet_sample(&m, 0, 26000, 1, dc, u1, r1, 1); }
    CHECK(co_lw_meet_note(&m, 0, line, sizeof line) == 1 && strstr(line, "least 90 m; none 0% of the time") != NULL && strstr(line, "within 250 m 1.00 on average") != NULL);
    g_case = "what a player sees: road patrols by kind, new sightings, the view's angle to the road";
    co_lw_meet_restart(&m);
    CHECK(co_lw_meet_seen_note(&m, 0, line, sizeof line) == 0);
    co_lw_meet_seen(&m, 0, 2, 1, 1, 0, 0, 10.0f); co_lw_meet_seen(&m, 0, 0, 0, 0, 3, 2, -1.0f);
    co_lw_meet_seen(&m, 0, 1, 1, 0, 0, 0, 30.0f); co_lw_meet_seen(&m, 0, 1, 0, 0, 1, 0, 20.0f);
    CHECK(co_lw_meet_seen_note(&m, 0, line, sizeof line) == 1 && strstr(line, "seen by player 1 (4 samples): road patrols in view and walking 1.00 on average (any 75% of the time), 2 new sightings (1 of the arterials')") != NULL);
    CHECK(strstr(line, "other patrols in view and walking 1.00 on average, 2 new sightings") != NULL && strstr(line, "20 deg on average (3 samples)") != NULL);
    co_lw_meet_restart(&m); co_lw_meet_seen(&m, 0, 0, 0, 0, 0, 0, -1.0f);
    CHECK(co_lw_meet_seen_note(&m, 0, line, sizeof line) == 1 && strstr(line, "direction: not read") != NULL && strstr(line, "0.00 on average (any 0% of the time), 0 new") != NULL);
    co_lw_meet_seen(&m, CO_LW_MEET_PLAYERS, 1, 1, 1, 1, 1, 1.0f);
    g_case = "a slot past the table and an unread slot have no line";
    co_lw_meet_sample(&m, CO_LW_MEET_PLAYERS, 23000, 0, dist, units, road, 1);
    CHECK(co_lw_meet_note(&m, CO_LW_MEET_PLAYERS, line, sizeof line) == 0 && co_lw_meet_note(&m, 3, line, sizeof line) == 0);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
