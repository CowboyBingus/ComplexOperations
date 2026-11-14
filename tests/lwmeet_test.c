/* A road patrol meets the players: the step that enters the ring, the point it steps back to, when it waits, steps
 * back and turns, the squad that never gives way, and what a player has near as the 30 s line states it. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_lw_meet.h"

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
    CHECK(co_lw_meet_keeps(1, 249.0f * 249.0f, 250.0f) == 1 && co_lw_meet_keeps(2, 100.0f, 250.0f) == 1);
    CHECK(co_lw_meet_keeps(1, 250.0f * 250.0f, 250.0f) == 0 && co_lw_meet_keeps(0, 100.0f, 250.0f) == 0);

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
    g_case = "a slot past the table and an unread slot have no line";
    co_lw_meet_sample(&m, CO_LW_MEET_PLAYERS, 23000, 0, dist, units, road, 1);
    CHECK(co_lw_meet_note(&m, CO_LW_MEET_PLAYERS, line, sizeof line) == 0 && co_lw_meet_note(&m, 3, line, sizeof line) == 0);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
