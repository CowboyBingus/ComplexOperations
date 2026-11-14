/* What each player sees of the patrols: a squad's time goes to the right sums, a sighting begins once, a squad that
 * stands loses its place in the far share, a patrol's own fights are counted, and the spawn band looks ahead of a
 * vehicle without letting a squad come online nearer than the least distance. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_lw_view.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)

static CoLwView g_v;
static CoLwViewSample At(uint32_t index, uint64_t ms, float x, float y, uint8_t seen0)
{
    CoLwViewSample s; memset(&s, 0, sizeof s);
    s.index = index; s.units = 8; s.nowMs = ms; s.pos[0] = x; s.pos[1] = y; s.patrol = 1; s.players = 2; s.seen[0] = seen0;
    return s;
}
static void Fresh(const char* name) { g_case = name; memset(&g_v, 0, sizeof g_v); co_lw_view_level(&g_v, 1, 1000); }

int main(void)
{
    char line[512]; CoLwViewSample s; float lead[2]; const float origin[2] = {0, 0};

    Fresh("a new level forgets, the same level keeps");
    s = At(5, 1000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    co_lw_view_level(&g_v, 1, 9000);
    CHECK(g_v.squad[5].known == 1 && g_v.sinceMs == 1000);
    co_lw_view_level(&g_v, 2, 9000);
    CHECK(g_v.squad[5].known == 0 && g_v.sinceMs == 9000 && g_v.player[0].sightings == 0);

    Fresh("a squad's time between samples goes to the sums; its first sample has none");
    s = At(5, 1000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.onlineMs == 0 && g_v.sums.unitMs == 0);
    s = At(5, 2000, 0, 0, CO_LW_VIEW_UNSEEN); s.road = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.onlineMs == 1000 && g_v.sums.unitMs == 8000 && g_v.sums.roadMs == 1000 && g_v.sums.movingMs == 0);
    g_case = "a squad away for long counts for one step only";
    s = At(5, 60000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.onlineMs == 1000 + CO_LW_VIEW_STEP_MS);
    g_case = "an index past the table and a position that is no number are left out";
    s = At(CO_LW_VIEW_SQUADS, 61000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    s = At(6, 61000, NAN, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 0 && g_v.squad[6].known == 0);

    Fresh("walking is more than 0.3 m/s over 5 s");
    s = At(7, 1000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(7, 6000, 1.4f, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);   /* 0.28 m/s */
    CHECK(g_v.squad[7].walking == 0 && co_lw_view_stood(&g_v, 7, 6000) == 5000);
    s = At(7, 11000, 3.0f, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);  /* 0.32 m/s */
    CHECK(g_v.squad[7].walking == 1 && co_lw_view_stood(&g_v, 7, 11000) == 0);
    g_case = "a shorter time is not read as a speed";
    s = At(7, 12000, 30.0f, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.squad[7].walking == 1 && g_v.squad[7].ref[0] == 3.0f);
    g_case = "moving time counts once the squad walks";
    CHECK(g_v.sums.movingMs == 4000 && g_v.sums.onlineMs == 7000); /* a 5 s gap counts as one 3 s step */
    g_case = "a squad that stops has stood since its last walking sample";
    s = At(7, 16000, 30.0f, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);  /* 27 m in 5 s: walking */
    s = At(7, 21000, 30.0f, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);  /* 0 m in 5 s */
    CHECK(g_v.squad[7].walking == 0 && co_lw_view_stood(&g_v, 7, 51000) == 35000);
    CHECK(co_lw_view_stood(&g_v, 8, 51000) == 0 && co_lw_view_stood(&g_v, CO_LW_VIEW_SQUADS, 51000) == 0);

    Fresh("a sighting begins once; near and far are apart; the second player sees nothing");
    s = At(3, 1000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    s = At(3, 2000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    s = At(3, 3000, 0, 0, CO_LW_VIEW_SEEN_FAR); s.road = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 1 && g_v.player[0].nearMs == 1000 && g_v.player[0].farMs == 1000);
    CHECK(g_v.player[0].nearUnitMs == 8000 && g_v.player[0].farUnitMs == 8000 && g_v.player[0].roadMs == 1000);
    CHECK(g_v.player[1].sightings == 0 && g_v.player[1].nearMs == 0 && g_v.player[1].farMs == 0);
    g_case = "behind a rock for a moment is the same sighting";
    s = At(3, 4000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(3, 8000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 1);
    g_case = "out of sight for 5 s, the squad is met again";
    s = At(3, 9000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(3, 14000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 2 && g_v.player[0].movingSightings == 0);
    g_case = "a squad on an errand is no patrol in sight";
    s = At(4, 14000, 0, 0, CO_LW_VIEW_SEEN_NEAR); s.patrol = 0; co_lw_view_sample(&g_v, &s);
    s = At(4, 15000, 0, 0, CO_LW_VIEW_SEEN_NEAR); s.patrol = 0; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 2 && g_v.player[0].nearMs == 7000 && g_v.sums.onlineMs == 11000);
    g_case = "the second player's sight is its own";
    s = At(4, 16000, 0, 0, CO_LW_VIEW_UNSEEN); s.seen[1] = CO_LW_VIEW_SEEN_FAR; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[1].sightings == 1 && g_v.player[1].farMs == 1000 && g_v.player[0].sightings == 2);
    g_case = "a player past the sample's count is not read";
    s = At(4, 17000, 0, 0, CO_LW_VIEW_UNSEEN); s.players = 1; s.seen[1] = CO_LW_VIEW_SEEN_FAR; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[1].farMs == 1000);
    g_case = "a squad that went offline starts afresh";
    co_lw_view_gone(&g_v, 3); co_lw_view_gone(&g_v, CO_LW_VIEW_SQUADS);
    s = At(3, 18000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 3 && g_v.squad[3].lastMs == 18000);

    Fresh("a sighting of a walking squad");
    s = At(9, 1000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(9, 6000, 10, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(9, 7000, 12, 0, CO_LW_VIEW_SEEN_FAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 1 && g_v.player[0].movingSightings == 1 && g_v.player[0].movingMs == 1000);

    Fresh("a patrol's fight counts when it was calm 10 s; one it came online in does not");
    s = At(1, 1000, 0, 0, CO_LW_VIEW_UNSEEN); s.fighting = 1; co_lw_view_sample(&g_v, &s);
    s = At(2, 1000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(2, 10999, 0, 0, CO_LW_VIEW_UNSEEN); s.fighting = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.fights == 0);
    s = At(2, 12000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(2, 22000, 0, 0, CO_LW_VIEW_UNSEEN); s.fighting = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.fights == 1);
    s = At(2, 23000, 0, 0, CO_LW_VIEW_UNSEEN); s.fighting = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.fights == 1);
    g_case = "a squad sent to a fight starts none";
    s = At(2, 24000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(2, 40000, 0, 0, CO_LW_VIEW_UNSEEN); s.fighting = 1; s.patrol = 0; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.fights == 1);

    Fresh("far units and their standing share");
    s = At(1, 1000, 0, 0, CO_LW_VIEW_UNSEEN); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    s = At(1, 2000, 0, 0, CO_LW_VIEW_UNSEEN); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    s = At(2, 1000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    s = At(2, 2000, 0, 0, CO_LW_VIEW_UNSEEN); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.farUnitMs == 8000 && g_v.sums.farStandMs == 8000);
    s = At(1, 6000, 20, 0, CO_LW_VIEW_UNSEEN); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    s = At(1, 7000, 22, 0, CO_LW_VIEW_UNSEEN); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    CHECK(g_v.sums.farUnitMs == 8000 + 24000 + 8000 && g_v.sums.farStandMs == 8000);

    g_case = "the far share: by sightline, beyond R_on, and walking";
    CHECK(co_lw_view_far(1, 251.0f * 251.0f, 250.0f, 0) == 1);
    CHECK(co_lw_view_far(1, 250.0f * 250.0f, 250.0f, CO_LW_VIEW_STAND_MS - 1) == 1);
    CHECK(co_lw_view_far(0, 251.0f * 251.0f, 250.0f, 0) == 0);
    CHECK(co_lw_view_far(1, 249.0f * 249.0f, 250.0f, 0) == 0);
    CHECK(co_lw_view_far(1, 600.0f * 600.0f, 250.0f, CO_LW_VIEW_STAND_MS) == 0);

    Fresh("a player's speed and way; a jump is no speed");
    co_lw_view_player(&g_v, 0, origin, 1000);
    { const float a[2] = {20, 0}; co_lw_view_player(&g_v, 0, a, 2000); }
    CHECK(NEAR(g_v.player[0].v[0], 20) && NEAR(g_v.player[0].v[1], 0) && NEAR(g_v.player[0].pathM, 20));
    { const float a[2] = {21, 0}; co_lw_view_player(&g_v, 0, a, 2100); }       /* too soon: not read */
    CHECK(NEAR(g_v.player[0].v[0], 20) && NEAR(g_v.player[0].pathM, 20));
    { const float a[2] = {900, 0}; co_lw_view_player(&g_v, 0, a, 3000); }      /* 880 m/s: a hellpod */
    CHECK(g_v.player[0].v[0] == 0 && NEAR(g_v.player[0].pathM, 20) && g_v.player[0].last[0] == 900);
    { const float a[2] = {900, 5}; co_lw_view_player(&g_v, 0, a, 9000); }      /* not read for 6 s: no speed */
    CHECK(g_v.player[0].v[1] == 0 && NEAR(g_v.player[0].pathM, 20));
    { const float a[2] = {NAN, 5}; co_lw_view_player(&g_v, 0, a, 10000); co_lw_view_player(&g_v, CO_LW_VIEW_PLAYERS, origin, 10000); }
    CHECK(g_v.player[0].last[0] == 900);

    Fresh("the band looks ahead of a vehicle, not of a Helldiver");
    co_lw_view_player(&g_v, 0, origin, 1000);
    { const float a[2] = {0, 8}; co_lw_view_player(&g_v, 0, a, 2000); CHECK(co_lw_view_lead(&g_v, 0, a, lead) == 0 && lead[0] == 0 && lead[1] == 8); }
    { const float a[2] = {0, 20}; co_lw_view_player(&g_v, 0, a, 3000); CHECK(co_lw_view_lead(&g_v, 0, a, lead) == 1 && NEAR(lead[0], 0) && NEAR(lead[1], 140)); } /* 12 m/s x 10 s */
    { const float a[2] = {30, 20}; co_lw_view_player(&g_v, 0, a, 4000); CHECK(co_lw_view_lead(&g_v, 0, a, lead) == 1 && NEAR(lead[0], 230) && NEAR(lead[1], 20)); } /* 30 m/s: 200 m at most */
    CHECK(co_lw_view_lead(&g_v, 1, origin, lead) == 0 && co_lw_view_lead(&g_v, CO_LW_VIEW_PLAYERS, origin, lead) == 0);

    g_case = "the band's distance: the lead point's band counts, the least distance to a player holds";
    CHECK(co_lw_view_band2(400.0f * 400.0f, 100.0f * 100.0f, 150.0f, 250.0f) == 150.0f * 150.0f);  /* ahead, at the lead point */
    CHECK(co_lw_view_band2(400.0f * 400.0f, 200.0f * 200.0f, 150.0f, 250.0f) == 200.0f * 200.0f);
    CHECK(co_lw_view_band2(400.0f * 400.0f, 250.0f * 250.0f, 150.0f, 250.0f) == 400.0f * 400.0f);  /* outside the lead band */
    CHECK(co_lw_view_band2(100.0f * 100.0f, 200.0f * 200.0f, 150.0f, 250.0f) == 100.0f * 100.0f);  /* too near a player */
    CHECK(co_lw_view_band2(200.0f * 200.0f, 10.0f * 10.0f, 150.0f, 250.0f) == 200.0f * 200.0f);    /* in the player's own band */
    CHECK(co_lw_view_band2(400.0f * 400.0f, 3.0e38f, 150.0f, 250.0f) == 400.0f * 400.0f);          /* no lead */

    Fresh("the notes");
    co_lw_view_player(&g_v, 0, origin, 1000);
    { const float a[2] = {30, 0}; co_lw_view_player(&g_v, 0, a, 4000); }
    s = At(1, 1000, 0, 0, CO_LW_VIEW_SEEN_NEAR); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    s = At(1, 4000, 0, 0, CO_LW_VIEW_SEEN_NEAR); s.sightline = 1; co_lw_view_sample(&g_v, &s);
    CHECK(co_lw_view_note(&g_v, 1000, line, sizeof line) == 0 && co_lw_view_player_note(&g_v, 0, 1000, line, sizeof line) == 0);
    CHECK(co_lw_view_player_note(&g_v, 1, 7000, line, sizeof line) == 0 && co_lw_view_player_note(&g_v, CO_LW_VIEW_PLAYERS, 7000, line, sizeof line) == 0);
    CHECK(co_lw_view_note(&g_v, 7000, line, sizeof line) == 1);
    CHECK(strcmp(line, "patrols (6 s): 0.5 squads online on average (4.0 units), 0.0 of them moving, 0.0 by a road; far units 4.0, "
        "100% of them standing; 0 fights started by patrol squads") == 0);
    CHECK(co_lw_view_player_note(&g_v, 0, 7000, line, sizeof line) == 1);
    CHECK(strcmp(line, "view of player 1 (6 s, 5.0 m/s): patrol squads in a clear line from the eye 0.5 within 300 m (4.0 units), "
        "0.0 from there to the draw limit (0.0 units); 0.0 of them moving, 0.0 by a road; 1 sightings began (0 of a moving squad)") == 0);
    g_case = "after the notes the sums start again; the squads and the players stay";
    co_lw_view_restart(&g_v, 7000);
    CHECK(g_v.sums.onlineMs == 0 && g_v.player[0].nearMs == 0 && g_v.player[0].sightings == 0 && g_v.player[0].pathM == 0 && g_v.sinceMs == 7000);
    CHECK(g_v.squad[1].known == 1 && g_v.player[0].known == 1 && g_v.player[0].v[0] == 10.0f);
    s = At(1, 8000, 0, 0, CO_LW_VIEW_SEEN_NEAR); co_lw_view_sample(&g_v, &s);
    CHECK(g_v.player[0].sightings == 0 && g_v.player[0].nearMs == 3000);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
