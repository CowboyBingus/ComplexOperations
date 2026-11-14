/* The roads for the living world: a point is near a road within the reach and not past it, across cell borders and at
 * the level's edge; a road is cut into stretches that end on its own points, none crowding its last point. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_lw_roads.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static CoLwRoads g_r;
int CoXlTestLwRoadPlan(int n, int at, int prev, int refused);
int CoXlTestLwRoadPatrolsOn(void);
float CoXlTestLwMeetWalk(int index, int n, int at, float px, float px2, int moveAt, int seconds, int old, float* out);
int CoXlTestLwPlansRoads(float radius, float step, float x, float y, int plans, int* failed);
float CoXlTestLwSeenBand(void);
void CoXlTestLwSeenWalk(int index, int n, float from, const float (*pl)[2], int np, float hid0, float hid1, int ahead, int seconds, int errand, int comes, float* out);
void CoXlTestLwSeenWorld(const float (*pl)[2], int np, int budget);
void CoXlTestLwSeenSquad(int idx, float x, float y, int state, int units, int mark);
int CoXlTestLwSeenWhy(int idx, const char* why, uint64_t nowMs, int filler);
uint32_t CoXlTestLwSeenHeld(int requester, uint64_t nowMs);
int CoXlTestLwSeenStands(int idx, uint64_t nowMs);
int CoXlTestLwSeenErrand(int idx, uint64_t nowMs);
int CoXlTestLwSeenGates(int placeOk, int roomOk, int* places, int* rooms);
void CoXlTestLwSeenSwitch(int on);
void CoXlTestLwSeenEventHold(int idx, uint32_t units);
uint32_t CoXlTestLwSeenEventHeld(int requester);
int CoXlTestLwSeenKeeps(int idx);
int CoXlTestLwSeenFarRoom(int idx, int meet, int farUnits, int roadFar);
int CoXlTestLwSeenStays(int idx, float d, uint64_t sinceMs, uint64_t nowMs);
int CoXlTestLwSeenCapText(void);
void CoXlTestLwSeenEnd(void);

int main(void)
{
    uint32_t out[16];

    g_case = "no points: nothing is near";
    co_lw_roads_index(&g_r, 0, -8192.0f);
    CHECK(co_lw_roads_near(&g_r, 0, 0, 30.0f) == 0);

    g_case = "within the reach, and not past it";
    g_r.pts[0][0] = 100; g_r.pts[0][1] = 200;         /* a point alone */
    g_r.pts[1][0] = -63.5f; g_r.pts[1][1] = 0.5f;     /* just inside a cell's border */
    g_r.pts[2][0] = -8192.0f; g_r.pts[2][1] = 8191.0f; /* the level's corner */
    g_r.pts[3][0] = 100; g_r.pts[3][1] = 200;         /* twice in one cell */
    co_lw_roads_index(&g_r, 4, -8192.0f);
    CHECK(co_lw_roads_near(&g_r, 100, 229.9f, 30.0f) == 1 && co_lw_roads_near(&g_r, 100, 230.1f, 30.0f) == 0);
    CHECK(co_lw_roads_near(&g_r, 121.0f, 221.0f, 30.0f) == 1 && co_lw_roads_near(&g_r, 122.0f, 222.0f, 30.0f) == 0);
    g_case = "a point across a cell border, on each side";
    CHECK(co_lw_roads_near(&g_r, -65.0f, -1.0f, 30.0f) == 1 && co_lw_roads_near(&g_r, -40.0f, 20.0f, 30.0f) == 0);
    CHECK(co_lw_roads_near(&g_r, -63.5f, 30.0f, 30.0f) == 1 && co_lw_roads_near(&g_r, -93.0f, 0.5f, 30.0f) == 1);
    g_case = "the level's edge and past it";
    CHECK(co_lw_roads_near(&g_r, -8190.0f, 8190.0f, 30.0f) == 1 && co_lw_roads_near(&g_r, -9000.0f, 9000.0f, 30.0f) == 0);
    CHECK(co_lw_roads_near(&g_r, 8191.0f, -8191.0f, 30.0f) == 0);
    g_case = "a reach past the cell is the cell's; no number is near nothing";
    CHECK(co_lw_roads_near(&g_r, 100, 263.0f, 500.0f) == 1 && co_lw_roads_near(&g_r, 100, 265.0f, 500.0f) == 0);
    CHECK(co_lw_roads_near(&g_r, NAN, 200, 30.0f) == 0);
    g_case = "every point is in the order once";
    {
        uint32_t seen = 0;
        for (uint32_t k = 0; k < 4; k++) seen |= 1u << g_r.order[k];
        CHECK(seen == 15u && g_r.start[CO_LW_ROADS_GRID * CO_LW_ROADS_GRID] == 4);
    }
    g_case = "more points than the table holds are left out";
    co_lw_roads_index(&g_r, CO_LW_ROADS_POINTS + 5, -8192.0f);
    CHECK(g_r.count == CO_LW_ROADS_POINTS);

    g_case = "a straight road 500 m long, a point every 10 m: stretches end at 100, 200, 300, 400 m";
    static float line[64][2];
    for (int i = 0; i <= 50; i++) { line[i][0] = 10.0f * (float)i; line[i][1] = 7.0f; }
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 51, 100.0f, out, 16) == 4 && out[0] == 10 && out[1] == 20 && out[2] == 30 && out[3] == 40);
    g_case = "the last stretch is not cut within half a stretch of the road's end";
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 25, 100.0f, out, 16) == 1 && out[0] == 10);  /* 240 m: 100, then 140 */
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 26, 100.0f, out, 16) == 2 && out[1] == 20);  /* 250 m: 50 m left counts */
    g_case = "a range inside a longer table starts at its own first point";
    CHECK(co_lw_roads_cut((const float (*)[2])line, 20, 51, 100.0f, out, 16) == 2 && out[0] == 30 && out[1] == 40);
    CHECK(co_lw_roads_cut((const float (*)[2])line, 20, 45, 100.0f, out, 16) == 1 && out[0] == 30);  /* 240 m of its own, not 440 */
    g_case = "a short road, one point, an empty range and no stretch length: no cut";
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 11, 100.0f, out, 16) == 0);
    CHECK(co_lw_roads_cut((const float (*)[2])line, 5, 6, 100.0f, out, 16) == 0 && co_lw_roads_cut((const float (*)[2])line, 6, 5, 100.0f, out, 16) == 0);
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 51, 0.0f, out, 16) == 0);
    g_case = "no more cuts than there is room for";
    CHECK(co_lw_roads_cut((const float (*)[2])line, 0, 51, 100.0f, out, 2) == 2 && out[1] == 20);
    g_case = "a winding road is measured along its points, not across";
    static float zig[64][2];
    for (int i = 0; i <= 40; i++) { zig[i][0] = 6.0f * (float)i; zig[i][1] = (i & 1) ? 8.0f : 0.0f; }   /* 10 m a step, 6 m of it forward */
    CHECK(co_lw_roads_cut((const float (*)[2])zig, 0, 41, 100.0f, out, 16) == 3 && out[0] == 10 && out[2] == 30);

    g_case = "a road patrol's next node lies on a road and is not the one it came from";
    {
        const uint8_t kind[8] = {1, 2, 0, 6, 0, 8, 5, 0};  /* an end, a point, an arterial's point, a spur's point, an arterial's end */
        const uint16_t next[5] = {2, 1, 4, 3, 5};          /* two grid nodes among three road nodes */
        uint32_t got = 0;
        for (uint32_t rnd = 0; rnd < 6; rnd++) got |= 1u << co_lw_roads_step(next, 5, kind, 0xFF, -1, rnd);
        CHECK(got == ((1u << 1) | (1u << 3) | (1u << 5)));
        CHECK(co_lw_roads_step(next, 5, kind, 0xFF, 3, 0) == 1 && co_lw_roads_step(next, 5, kind, 0xFF, 3, 1) == 5 && co_lw_roads_step(next, 5, kind, 0xFF, 3, 2) == 1);
        CHECK(co_lw_roads_step(next, 5, kind, 0xFF, 1, 0) == 3);
        g_case = "a road's end: no way on but back, or only grid nodes";
        CHECK(co_lw_roads_step(next, 2, kind, 0xFF, 1, 7) == -1 && co_lw_roads_step(next, 1, kind, 0xFF, -1, 0) == -1 && co_lw_roads_step(next, 0, kind, 0xFF, -1, 0) == -1);
        CHECK(co_lw_roads_step(next + 1, 1, kind, 0xFF, -1, 9) == 1);
        g_case = "the kinds a patrol keeps to: the arterials, a spur, any road but a spur";
        for (uint32_t rnd = 0; rnd < 4; rnd++)
        {
            CHECK(co_lw_roads_step(next, 5, kind, CO_LW_ROADS_ARTERIAL, -1, rnd) == 3 && co_lw_roads_step(next, 5, kind, CO_LW_ROADS_SPUR, -1, rnd) == 5);
            CHECK(co_lw_roads_step(next, 5, kind, CO_LW_ROADS_END | CO_LW_ROADS_POINT, -1, rnd) == (rnd & 1 ? 3 : 1));
        }
        CHECK(co_lw_roads_step(next, 5, kind, CO_LW_ROADS_ARTERIAL, 3, 0) == -1 && co_lw_roads_step(next, 5, kind, CO_LW_ROADS_SPUR, 5, 0) == -1);
    }

    g_case = "a road patrol's plan goes on along the road, and turns round at its end";  /* node 0 off the roads, 1..5 a road */
    CHECK(CoXlTestLwRoadPlan(5, 3, 2, -1) == 4 && CoXlTestLwRoadPlan(5, 3, 4, -1) == 2);
    CHECK(CoXlTestLwRoadPlan(5, 5, 4, -1) == 4 && CoXlTestLwRoadPlan(5, 1, 2, -1) == 2);
    g_case = "a plan whose first node was refused (no navmesh there) takes the other way, never the same node again";
    CHECK(CoXlTestLwRoadPlan(5, 3, 2, 4) == 2 && CoXlTestLwRoadPlan(5, 3, 4, 2) == 4 && CoXlTestLwRoadPlan(5, 3, -1, 4) == 2);
    g_case = "refused with no other way on the roads: no road plan, the grid's plan draws a goal";
    CHECK(CoXlTestLwRoadPlan(5, 5, 4, 4) == -1 && CoXlTestLwRoadPlan(5, 1, -1, 2) == -1);
    g_case = "from off the roads: to the nearest road node, and no road plan once that way was refused";
    CHECK(CoXlTestLwRoadPlan(5, 0, -1, -1) == 1 && CoXlTestLwRoadPlan(5, 0, -1, 1) == -1);
    g_case = "road patrols are on in a build that reads no research switch";
    CHECK(CoXlTestLwRoadPatrolsOn() == 1);

    /* A line of road nodes 100 m apart, the patrol from node 5 (x 500) toward node 30 at 2 m/s, a player at x 2000. */
    float o[5], least;
    g_case = "a road patrol stops at the ring's edge and waits there: it never walks in offline";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 2000.0f, 9999, 700, 0, o);  /* 1345 m to the edge: 673 s */
    CHECK(least >= 155.0f && least < 156.0f && o[0] > 1844.0f && o[0] <= 1845.0f && o[1] == 1 && o[2] == 1 && o[3] == 0 && o[4] == 0);
    g_case = "a minute at the edge without coming online: it turns back and walks away";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 2000.0f, 9999, 760, 0, o);
    CHECK(least >= 155.0f && o[1] == 0 && o[2] == 1 && o[3] == 1 && o[0] < 1820.0f && o[0] > 1700.0f);
    g_case = "after the turn its next plan does not lead straight back to the player";
    for (int k = 0; k < 8; k++) /* the way on is drawn by chance where there are two */
    {
        least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 2000.0f, 9999, 900, 0, o);
        CHECK(least >= 155.0f && o[3] == 1 && o[2] == 1 && o[0] < 1560.0f);
    }
    g_case = "a player who walks up to a waiting patrol pushes it back along its stretch, the edge's distance away";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 1960.0f, 690, 692, 0, o);
    CHECK(o[4] == 1 && o[3] == 0 && o[1] == 1 && o[0] < 1805.1f && o[0] > 1800.0f);
    g_case = "pushed with the whole stretch behind it inside the ring: it turns back";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 1850.0f, 690, 692, 0, o);
    CHECK(o[3] == 1 && o[4] == 0 && o[1] == 0);
    g_case = "the player gone from its way: it walks on, without a turn";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 5000.0f, 690, 720, 0, o);
    CHECK(o[1] == 0 && o[2] == 1 && o[3] == 0 && o[0] > 1890.0f);
    g_case = "the old rules, and a squad that is no road patrol: both walk into the ring";
    least = CoXlTestLwMeetWalk(0, 30, 5, 2000.0f, 2000.0f, 9999, 760, 1, o);
    CHECK(least < 5.0f && o[2] == 0);
    least = CoXlTestLwMeetWalk(1, 30, 5, 2000.0f, 2000.0f, 9999, 760, 0, o);
    CHECK(least < 5.0f && o[2] == 0);

    /* The seen rules. A player at x 2500 on the line; the squad starts 300 m beyond the draw band and walks toward it. */
    {
        const float B = CoXlTestLwSeenBand(), P = 2500.0f, start = P - B - 300.0f; float s[12];
        const float one[1][2] = {{P, 0}};
        const int walkIn = (int)((B + 145.0f) / 2.0f); /* seconds to the ring's edge */
        g_case = "the band the rules read is the drawn distance and its margin";
        CHECK(B > 400.0f && B < 900.0f && start > 600.0f);
        g_case = "an open road: a patrol on the player's road comes online beyond the band, and holds the kept far share";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn, -1, 1, s);
        CHECK(s[1] == 3 && P - s[2] >= B && P - s[2] < B + 100.0f && P - s[2] > B + 90.0f && s[6] == 6 && s[5] == 0);
        g_case = "an open road: a squad that is no road patrol has no such rule and walks in as before";
        CoXlTestLwSeenWalk(1, 30, start, one, 1, 1, 0, 0, walkIn + 100, -1, 1, s);
        CHECK(s[1] == 0 && s[3] < 60.0f && s[8] > 50 && s[6] == 0);
        g_case = "a road in cover to 400 m: the patrol steps back to the last hidden point when it is seen";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, 2100, 0, walkIn, -1, 1, s);
        CHECK(s[1] == 2 && s[2] <= 2100.0f && s[2] > 2097.0f && s[5] == 1 && s[0] == s[2] && s[6] == 6);
        g_case = "hidden with its next stretch in view: it asks at the band's edge of cover, not before the band";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, 2400, 1, walkIn, -1, 1, s);
        CHECK(s[1] == 1 && P - s[2] < B && P - s[2] > B - 3.0f && s[5] == 0);
        g_case = "a squad off the roads steps back to cover too, in the near band, at half the looks";
        CoXlTestLwSeenWalk(1, 30, start, one, 1, 2260, 2280, 1, walkIn, -1, 1, s);
        CHECK(s[1] == 2 && s[2] <= 2280.0f && s[2] > 2275.0f && s[6] == 2);
        { float r[12]; CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 150, -1, 0, r); CoXlTestLwSeenWalk(1, 30, start, one, 1, 1, 0, 0, 150, -1, 0, s);
          CHECK(r[8] >= 49.0f && r[8] <= 51.0f && s[8] >= 24.0f && s[8] <= 26.0f); }
        g_case = "never online: it keeps asking by the rules, never walks into the ring, and waits at its edge with priority";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn + 20, -1, 0, s);
        CHECK(s[1] == 3 && s[3] >= 155.0f && s[3] < 156.0f && s[4] == 1 && s[9] == 1 && s[6] == 1);
        g_case = "seen inside the online radius with nowhere to step back: its request waits for the next look; hidden, it does not";
        CHECK(s[10] == 1);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, 5000, 0, walkIn - 10, -1, 0, s);
        CHECK(s[10] == 0 && s[1] == 0 && P - s[0] < 250.0f);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, (int)((B + 300.0f - 260.0f) / 2.0f), -1, 0, s);
        CHECK(s[10] == 0 && P - s[0] >= 250.0f); /* seen beyond the online radius: no request of its own to hold back */
        g_case = "a patrol walking away ahead of a player on its road has the priority too";
        CoXlTestLwSeenWalk(0, 30, P + 200.0f, one, 1, 1, 0, 0, 20, -1, 0, s);
        CHECK(s[6] == 1 && s[9] == 1 && s[1] == 0 && s[0] > P + 230.0f);
        { const float off[1][2] = {{P, 90.0f}}; CoXlTestLwSeenWalk(0, 30, P + 200.0f, off, 1, 1, 0, 0, 20, -1, 0, s); CHECK(s[6] == 0 && s[9] == 0); } /* 90 m off its way */
        g_case = "a squad off the roads walking away from the player is not looked at";
        CoXlTestLwSeenWalk(1, 30, P + 200.0f, one, 1, 1, 0, 0, 20, -1, 0, s);
        CHECK(s[8] == 0 && s[6] == 0);
        g_case = "meet 0: no look, no priority, no ask, and the road patrol walks into the ring as it did before these rules";
        CoXlTestLwSeenSwitch(0);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn + 60, -1, 0, s);
        CHECK(s[1] == 0 && s[8] == 0 && s[9] == 0 && s[6] == 0 && s[4] == 0 && s[3] < 100.0f);
        CoXlTestLwSeenSwitch(1);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn + 60, -1, 0, s);
        CHECK(s[1] == 3 && s[3] >= 155.0f);
        g_case = "sent on an errand while it approaches or waits: its patrol state ends at once";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 120, -1, 0, s);
        CHECK(s[6] == 1 && s[9] == 1 && s[7] == 0);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 120, 110, 0, s);
        CHECK(s[6] == 0 && s[9] == 0 && s[7] == 1 && s[4] == 0);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn + 30, walkIn + 20, 0, s);
        CHECK(s[6] == 0 && s[9] == 0 && s[7] == 1 && s[4] == 0 && s[3] >= 155.0f);
        g_case = "two players 400 m apart, the patrol between them: it enters neither ring, and comes online from cover";
        { const float two[2][2] = {{1600, 0}, {2000, 0}};
          CoXlTestLwSeenWalk(0, 30, 1800.0f, two, 2, 1, 0, 0, 400, -1, 0, s);
          CHECK(s[3] >= 155.0f && s[3] < 156.0f && s[9] == 1);
          CoXlTestLwSeenWalk(0, 30, 1800.0f, two, 2, 1790, 1810, 0, 400, -1, 1, s);
          CHECK(s[1] == 2 && s[2] <= 1810.0f && s[2] > 1806.0f && s[3] >= 155.0f); }
        g_case = "a second player nearer than the first decides the distance: beyond the band of one, inside the other's ring";
        { const float two[2][2] = {{P, 0}, {start + 100.0f, 100.0f}};
          CoXlTestLwSeenWalk(0, 30, start, two, 2, 1, 0, 0, 10, -1, 1, s); CHECK(s[1] == 0); }
    }
    /* Room: two players 3000 m apart are two bubbles of 23; squads 0, 10, 20 are road patrols. */
    {
        const float apart[2][2] = {{0, 0}, {3000, 0}}, solo[1][2] = {{0, 0}}; const char* cap = "the cap: bubble 20 online + 8 + 0 held over 23 (1 players)";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 1, 8, 1); CoXlTestLwSeenSquad(20, 250, 0, 1, 8, 1); CoXlTestLwSeenSquad(10, 3200, 0, 1, 8, 1);
        CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0); CoXlTestLwSeenSquad(12, 3100, 0, 1, 7, 0); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(6, 3300, 0, 3, 6, 0);
        g_case = "the cap's text reads as a refusal for units; sight and spacing do not";
        CHECK(CoXlTestLwSeenCapText() == 1);
        g_case = "no swap for a sight or spacing refusal, nor for a squad without priority";
        CHECK(CoXlTestLwSeenWhy(0, "sight: seen", 100000, 5) == 0 && CoXlTestLwSeenWhy(0, "within 60 m of an online squad: squad 5, 40 m away", 100000, 5) == 0);
        CHECK(CoXlTestLwSeenWhy(3, cap, 100000, 5) == 0 && CoXlTestLwSeenHeld(3, 100000) == 0);
        g_case = "no calm hidden squad to leave: no hold";
        CHECK(CoXlTestLwSeenWhy(0, cap, 100000, -1) == 0 && CoXlTestLwSeenHeld(3, 100000) == 0);
        g_case = "refused for room with its point standing: a filler squad leaves and the room is held in its bubble only";
        CHECK(CoXlTestLwSeenWhy(0, cap, 106000, 5) == 1);
        CHECK(CoXlTestLwSeenHeld(3, 106000) == 8 && CoXlTestLwSeenHeld(0, 106000) == 0 && CoXlTestLwSeenHeld(12, 106000) == 0);
        g_case = "while room is made for it the patrol stands where its point stood; another walks on";
        CHECK(CoXlTestLwSeenStands(0, 106000) == 1 && CoXlTestLwSeenStands(20, 106000) == 0 && CoXlTestLwSeenStands(0, 126000) == 0);
        g_case = "another bubble's patrol swaps while that hold runs; the same bubble's does not";
        CHECK(CoXlTestLwSeenWhy(10, cap, 107000, 6) == 1 && CoXlTestLwSeenHeld(12, 107000) == 8);
        CHECK(CoXlTestLwSeenWhy(0, cap, 112000, 5) == 0 && CoXlTestLwSeenWhy(20, cap, 112000, 5) == 0);
        g_case = "the hold lapses after 20 s; then the next patrol of the bubble may swap";
        CHECK(CoXlTestLwSeenHeld(3, 125999) == 8 && CoXlTestLwSeenHeld(3, 126000) == 0);
        CHECK(CoXlTestLwSeenWhy(20, cap, 126000, 5) == 1);
        g_case = "one swap in 5 s a bubble";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 1, 8, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CHECK(CoXlTestLwSeenWhy(0, cap, 200000, -1) == 0 && CoXlTestLwSeenWhy(0, cap, 204999, 5) == 0 && CoXlTestLwSeenWhy(0, cap, 205000, 5) == 1);
        g_case = "at 23 units the road patrols take one squad by swap: with one online the next is refused, and the farther one may be filler";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 3, 8, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 8, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(10, 3200, 0, 3, 8, 0);
        CHECK(CoXlTestLwSeenWhy(20, cap, 300000, 5) == 0 && CoXlTestLwSeenKeeps(0) == 1 && CoXlTestLwSeenKeeps(5) == 0);
        CoXlTestLwSeenSquad(20, 250, 0, 3, 8, 0);
        CHECK(CoXlTestLwSeenKeeps(0) == 0 && CoXlTestLwSeenKeeps(10) == 1); /* 16 of 23 in bubble 0; bubble 1's one squad stays */
        CoXlTestLwSeenSquad(0, 400, 0, 3, 8, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 8, 1);
        CHECK(CoXlTestLwSeenKeeps(0) == 0); /* beyond 250 m: filler like any */
        g_case = "at 72 units two road squads are kept and a third still swaps";
        CoXlTestLwSeenWorld(solo, 1, 72);
        CoXlTestLwSeenSquad(0, 200, 0, 3, 8, 0); CoXlTestLwSeenSquad(10, 0, 200, 3, 8, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 8, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CHECK(CoXlTestLwSeenKeeps(0) == 1 && CoXlTestLwSeenKeeps(10) == 1 && CoXlTestLwSeenWhy(20, cap, 400000, 5) == 1);
        g_case = "the far share of 18: 9 for other far squads, the whole for a patrol on a player's road";
        CHECK(CoXlTestLwSeenFarRoom(3, 0, 2, 0) == 1 && CoXlTestLwSeenFarRoom(3, 0, 3, 0) == 0 && CoXlTestLwSeenFarRoom(3, 2, 3, 0) == 0);
        CHECK(CoXlTestLwSeenFarRoom(20, 3, 10, 0) == 1 && CoXlTestLwSeenFarRoom(20, 3, 11, 0) == 0 && CoXlTestLwSeenFarRoom(3, 0, 10, 8) == 1);
        g_case = "an event squad's hold counts in its own bubble only, and a patrol does not swap beside it there";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 1, 8, 1); CoXlTestLwSeenSquad(10, 3200, 0, 1, 8, 1); CoXlTestLwSeenSquad(7, 3100, 0, 1, 9, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CoXlTestLwSeenSquad(12, 3100, 0, 1, 7, 0); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(6, 3300, 0, 3, 6, 0);
        CoXlTestLwSeenEventHold(7, 9);
        CHECK(CoXlTestLwSeenEventHeld(3) == 0 && CoXlTestLwSeenEventHeld(12) == 9 && CoXlTestLwSeenEventHeld(7) == 0);
        CHECK(CoXlTestLwSeenWhy(10, cap, 500000, 6) == 0 && CoXlTestLwSeenWhy(0, cap, 500000, 5) == 1);
        g_case = "a request counts the event squad's hold and the patrols' holds of its own bubble";
        CHECK(CoXlTestLwSeenEventHeld(3) == 8 && CoXlTestLwSeenEventHeld(12) == 9 && CoXlTestLwSeenEventHeld(0) == 0);
        g_case = "sent to reinforce while it waits with a hold: no wait, no priority, no mark, and its hold ends";
        CHECK(CoXlTestLwSeenErrand(0, 501000) == 0 && CoXlTestLwSeenEventHeld(3) == 0 && CoXlTestLwSeenEventHeld(12) == 9);
        CoXlTestLwSeenEventHold(-1, 0);
        g_case = "a patrol on its way to a player is placed and sight-tested before room is asked";
        { int places, rooms;
          CHECK(CoXlTestLwSeenGates(0, 1, &places, &rooms) == 0 && places == 1 && rooms == 0);
          CHECK(CoXlTestLwSeenGates(1, 0, &places, &rooms) == 0 && places == 1 && rooms == 1);
          CHECK(CoXlTestLwSeenGates(1, 1, &places, &rooms) == 1 && places == 1 && rooms == 1); }
        g_case = "a squad that came online on its way in stays 300 s while within reach of a player";
        { const float B = CoXlTestLwSeenBand();
          CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 2);
          CHECK(CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + 299999) == 1 && CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + 300000) == 0);
          CHECK(CoXlTestLwSeenStays(0, B + 149.0f, 1000, 2000) == 1 && CoXlTestLwSeenStays(0, B + 151.0f, 1000, 2000) == 0);
          CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 0); CHECK(CoXlTestLwSeenStays(0, 600.0f, 1000, 2000) == 0); }
        CoXlTestLwSeenEnd();
    }

    g_case = "a squad off the roads never draws a road node as its destination";
    int failed = 0;
    CHECK(CoXlTestLwPlansRoads(7782.0f, 200.0f, 3000.0f, 3000.0f, 300, &failed) == 0 && failed == 0);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
