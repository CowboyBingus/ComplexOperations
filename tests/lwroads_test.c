/* The roads for the living world: a point is near a road within the reach and not past it, across cell borders and at
 * the level's edge; a road is cut into stretches that end on its own points, none crowding its last point. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_lw_roads.h"
#include "xl/co_lw_meet.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static CoLwRoads g_r;
int CoXlTestLwRoadPlan(int n, int at, int prev, int refused);
int CoXlTestLwRoadPatrolsOn(void);
float CoXlTestLwMeetWalk(int index, int n, int at, float px, float px2, int moveAt, int seconds, int old, float* out);
int CoXlTestLwPlansRoads(float radius, float step, float x, float y, int plans, int* failed);
float CoXlTestLwSeenBand(void);
float CoXlTestLwSeenBandNoGroup(void);
void CoXlTestLwSeenSetup(int group, int viewOf, int local, float speed, int blind);
float CoXlTestLwSeenRange(int player, int np);
void CoXlTestLwSeenWalk(int index, int n, float from, const float (*pl)[2], int np, float hid0, float hid1, int ahead, int seconds, int errand, int comes, float* out);
void CoXlTestLwSeenWorld(const float (*pl)[2], int np, int budget);
void CoXlTestLwSeenSquad(int idx, float x, float y, int state, int units, int mark);
int CoXlTestLwSeenWhy(int idx, const char* why, uint64_t nowMs, int filler);
uint32_t CoXlTestLwSeenHeld(int requester, uint64_t nowMs);
int CoXlTestLwSeenStands(int idx, uint64_t nowMs);
int CoXlTestLwSeenErrand(int idx, uint64_t nowMs);
int CoXlTestLwSeenGates(int placeOk, int roomOk, int* places, int* rooms);
void CoXlTestLwSeenSwitch(int on);
void CoXlTestLwSeenBusy(int busy);
void CoXlTestLwSeenEventHold(int idx, uint32_t units);
uint32_t CoXlTestLwSeenEventHeld(int requester);
int CoXlTestLwSeenKeeps(int idx);
int CoXlTestLwSeenFarRoom(int idx, int meet, int farUnits, int roadFar);
int CoXlTestLwSeenFarSum(int idx, int units, int stands, int walking, int standing);
int CoXlTestLwSeenStays(int idx, float d, uint64_t sinceMs, uint64_t nowMs);
int CoXlTestLwSeenCapText(void);
void CoXlTestLwSeenEnd(void);
void CoXlTestLwSeenFarNo(int refuse);
int CoXlTestLwRoadTail(int base, int count, int probe, int* out, int max, int* members, int* more);
void CoXlTestLwCallWorld(int n, int fork, const float (*pl)[2], int np, const float* sx, const int* dir, const int* mk, int squads);
int CoXlTestLwCallOne(int i, int turn, int* out);
int CoXlTestLwCallTick(uint64_t nowMs);
int CoXlTestLwCallOver(int idx, int how);
int CoXlTestLwCallOf(int idx);
int CoXlTestLwCallZone(float x, int to, float x2, int turn);
void CoXlTestLwCallEnd(void);
void CoXlTestLwCallGroup(int idx, int group);
void CoXlTestLwCallPlayer(int i, float x, float vx);
int CoXlTestLwCallLost(void);
int CoXlTestLwCallCameOther(void);
uint64_t CoXlTestLwCallSlot(int i);
float CoXlTestLwSeenScopedOf(float radius);

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
        const float B = CoXlTestLwSeenBand(), P = 2500.0f, start = P - B - 300.0f; float s[16];
        const float one[1][2] = {{P, 0}};
        const int walkIn = (int)((B + 145.0f) / 2.0f); /* seconds to the ring's edge */
        g_case = "the band the rules read is the drawn distance and its margin";
        CHECK(B > 400.0f && B < 900.0f && start > 600.0f);
        g_case = "an open road: a patrol on the player's road comes online beyond the band, and holds the kept far share";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn, -1, 1, s);
        CHECK(s[1] == 3 && P - s[2] >= B && P - s[2] < B + 100.0f && P - s[2] > B + 90.0f && s[6] == 6 && s[5] == 0);
        g_case = "beyond the band the look asks every player with a line, the host too";
        CHECK(s[11] > 0);
        g_case = "who may refuse a patrol's point: the host within its band, a player on another machine within the band through a 4x scope";
        const float S = CoXlTestLwSeenRange(1, 2);
        CHECK(CoXlTestLwSeenRange(0, 2) == B && S > B + 150.0f && S < 900.0f);
        g_case = "a squad with no group, or of a group never measured: every player's full sight";
        CoXlTestLwSeenSetup(-1, 1, 0, 0, 0); CHECK(CoXlTestLwSeenRange(0, 2) == 1700.0f && CoXlTestLwSeenRange(1, 2) == 1700.0f);
        CoXlTestLwSeenSetup(0, 1, 0, 0, 0); CHECK(CoXlTestLwSeenRange(0, 2) == 1700.0f && CoXlTestLwSeenRange(1, 2) == 1700.0f);
        g_case = "a squad with no group has the default band, not an empty slot's";
        CHECK(CoXlTestLwSeenBandNoGroup() == B - 50.0f);
        g_case = "the host's short range is for this machine's player while its own view is read: not for a player its camera follows";
        CoXlTestLwSeenSetup(1, 2, 0, 0, 0); CHECK(CoXlTestLwSeenRange(0, 2) == S && CoXlTestLwSeenRange(1, 2) == S);
        CoXlTestLwSeenSetup(1, 0, 0, 0, 0); CHECK(CoXlTestLwSeenRange(0, 1) == S);
        CoXlTestLwSeenSetup(1, 1, 1, 0, 0); CHECK(CoXlTestLwSeenRange(0, 2) == S); /* the view on a player who is not this machine's */
        g_case = "the view not read: the patrol comes online beyond the scoped band, not the host's";
        CoXlTestLwSeenWalk(0, 30, P - S - 300.0f, one, 1, 1, 0, 0, 200, -1, 1, s);
        CHECK(s[1] == 3 && P - s[2] >= S && P - s[2] < S + 100.0f);
        CoXlTestLwSeenSetup(1, 1, 0, 0, 0);
        g_case = "a second player beside the host: the zone starts beyond that player's scoped band, and its line counts in the look";
        { const float two[2][2] = {{P, 0}, {P, 30.0f}}, away[2][2] = {{P, 0}, {P + 1200.0f, 0}};
          CoXlTestLwSeenSetup(1, 1, 0, 0, 1); /* the host has no line: the other player's decides */
          CoXlTestLwSeenWalk(0, 30, P - S - 300.0f, two, 2, 1, 0, 0, 200, -1, 1, s);
          CHECK(s[1] == 3 && hypotf(P - s[2], 30.0f) >= S && P - s[2] < S + 100.0f && s[12] > 0);
          g_case = "inside the host's band a player 1300 m or more away, beyond its scoped band, is not asked";
          CoXlTestLwSeenWalk(0, 30, start, away, 2, 1, 0, 0, walkIn - 10, -1, 0, s);
          CHECK(s[12] == 0 && s[11] > 100 && s[1] == 0);
          CoXlTestLwSeenSetup(1, 1, 0, 0, 0); }
        g_case = "ahead of a vehicle at 20 m/s it asks 200 m farther out";
        CoXlTestLwSeenSetup(1, 1, 0, 20.0f, 0);
        CoXlTestLwSeenWalk(0, 30, start - 300.0f, one, 1, 1, 0, 0, 200, -1, 1, s);
        CHECK(s[1] == 3 && P - s[2] >= B + 200.0f && P - s[2] < B + 300.0f);
        CoXlTestLwSeenSetup(1, 1, 0, 0, 0);
        g_case = "a group never measured: no request in clear sight beyond a guessed band; it waits at the ring";
        CoXlTestLwSeenSetup(0, 1, 0, 0, 0);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, walkIn + 20, -1, 1, s);
        CHECK(s[1] == 0 && s[4] == 1 && s[3] >= 155.0f);
        CoXlTestLwSeenSetup(1, 1, 0, 0, 0);
        g_case = "hidden beyond the band, open inside it: the point beyond is no cover to step back to";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, P - B - 1.0f, 0, walkIn + 20, -1, 0, s);
        CHECK(s[5] == 0 && s[4] == 1);
        g_case = "still 300 m out, its way leading to the player: known for the far share, no priority, no look";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 5, -1, 0, s);
        CHECK(s[6] == 8 && s[9] == 0 && s[8] == 0);
        { const float off[1][2] = {{P, 90.0f}}; CoXlTestLwSeenWalk(0, 30, start, off, 1, 1, 0, 0, 5, -1, 0, s); CHECK(s[6] == 0); }
        CoXlTestLwSeenWalk(0, 30, start - 400.0f, one, 1, 1, 0, 0, 5, -1, 0, s); CHECK(s[6] == 0);
        g_case = "refused the far share beyond the band, the patrol stands there, where nobody draws it, and asks on";
        CoXlTestLwSeenFarNo(1);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 180, -1, 0, s);
        CHECK(s[1] == 3 && P - s[0] >= B && P - s[0] < B + 100.0f && s[13] >= 20 && s[14] == 0 && s[6] == 1);
        g_case = "after 90 s of refusals it walks on into the band, and stands no second time";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 250, -1, 0, s);
        CHECK(P - s[0] < B - 10.0f && P - s[0] > B - 60.0f && s[14] == 1);
        g_case = "refused for the spacing from a squad just requested, or for the bubble's room, it stands there as well; only the far share's refusals are counted as such";
        CoXlTestLwSeenFarNo(2);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 180, -1, 0, s);
        CHECK(s[1] == 3 && P - s[0] >= B && P - s[0] < B + 100.0f && s[13] == 0 && s[14] == 0 && s[6] == 1);
        CoXlTestLwSeenFarNo(3);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 180, -1, 0, s);
        CHECK(s[1] == 3 && P - s[0] >= B && P - s[0] < B + 100.0f && s[13] == 0 && s[14] == 0 && s[6] == 1);
        CoXlTestLwSeenFarNo(0);
        g_case = "with room in the far share it never stands: it comes online at its first request";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 180, -1, 1, s);
        CHECK(s[1] == 3 && s[13] == 0 && s[0] == s[2]);
        g_case = "an open road: a squad that is no road patrol has no such rule and walks in as before";
        CoXlTestLwSeenWalk(1, 30, start, one, 1, 1, 0, 0, walkIn + 100, -1, 1, s);
        CHECK(s[1] == 0 && s[3] < 60.0f && s[8] > 50 && s[6] == 0);
        g_case = "a road in cover to 400 m: the patrol steps back to the last hidden point when it is seen";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, 2100, 0, walkIn, -1, 1, s);
        CHECK(s[1] == 2 && s[2] <= 2100.0f && s[2] > 2097.0f && s[5] == 1 && s[0] == s[2] && s[6] == 6);
        g_case = "refused each time, it steps back to cover 6 times an approach, then walks on";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, -20, 0, walkIn, -1, 0, s); /* 10 m of cover every 20 m */
        CHECK(s[5] == 6 && P - s[0] < 250.0f);
        g_case = "with a request out it takes no look";
        CoXlTestLwSeenBusy(1);
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 150, -1, 0, s);
        CHECK(s[8] == 0 && s[1] == 0 && s[6] == 1);
        CoXlTestLwSeenBusy(0);
        g_case = "hidden with its next stretch in view: it asks at the band's edge of cover, not before the band";
        CoXlTestLwSeenWalk(0, 30, start, one, 1, 0, 2400, 1, walkIn, -1, 1, s);
        CHECK(s[1] == 1 && P - s[2] < B && P - s[2] > B - 3.0f && s[5] == 0);
        g_case = "a squad off the roads steps back to cover too, in the near band, at half the looks";
        CoXlTestLwSeenWalk(1, 30, start, one, 1, 2260, 2280, 1, walkIn, -1, 1, s);
        CHECK(s[1] == 2 && s[2] <= 2280.0f && s[2] > 2275.0f && s[6] == 2);
        { float r[16]; CoXlTestLwSeenWalk(0, 30, start, one, 1, 1, 0, 0, 150, -1, 0, r); CoXlTestLwSeenWalk(1, 30, start, one, 1, 1, 0, 0, 150, -1, 0, s);
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
        CoXlTestLwSeenSquad(0, 200, 0, 1, 4, 1); CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1); CoXlTestLwSeenSquad(10, 3200, 0, 1, 4, 1);
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
        CHECK(CoXlTestLwSeenHeld(3, 106000) == 4 && CoXlTestLwSeenHeld(0, 106000) == 0 && CoXlTestLwSeenHeld(12, 106000) == 0);
        g_case = "while room is made for it the patrol stands where its point stood; another walks on";
        CHECK(CoXlTestLwSeenStands(0, 106000) == 1 && CoXlTestLwSeenStands(20, 106000) == 0 && CoXlTestLwSeenStands(0, 126000) == 0);
        g_case = "another bubble's patrol swaps while that hold runs; the same bubble's does not";
        CHECK(CoXlTestLwSeenWhy(10, cap, 107000, 6) == 1 && CoXlTestLwSeenHeld(12, 107000) == 4);
        CHECK(CoXlTestLwSeenWhy(0, cap, 112000, 5) == 0 && CoXlTestLwSeenWhy(20, cap, 112000, 5) == 0);
        g_case = "the hold lapses after 20 s; then the next patrol of the bubble may swap";
        CHECK(CoXlTestLwSeenHeld(3, 125999) == 4 && CoXlTestLwSeenHeld(3, 126000) == 0);
        CHECK(CoXlTestLwSeenWhy(20, cap, 126000, 5) == 1);
        g_case = "one swap in 5 s a bubble";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 1, 4, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CHECK(CoXlTestLwSeenWhy(0, cap, 200000, -1) == 0 && CoXlTestLwSeenWhy(0, cap, 204999, 5) == 0 && CoXlTestLwSeenWhy(0, cap, 205000, 5) == 1);
        g_case = "at 23 units the road patrols take one squad of 4 by swap: with one online the next is refused, and the farther one may be filler";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 3, 4, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(10, 3200, 0, 3, 4, 0);
        CHECK(CoXlTestLwSeenWhy(20, cap, 300000, 5) == 0 && CoXlTestLwSeenKeeps(0) == 1 && CoXlTestLwSeenKeeps(5) == 0);
        CoXlTestLwSeenSquad(20, 250, 0, 3, 4, 0);
        CHECK(CoXlTestLwSeenKeeps(0) == 0 && CoXlTestLwSeenKeeps(10) == 1); /* 8 over bubble 0's 4; bubble 1's one squad stays */
        CoXlTestLwSeenSquad(0, 400, 0, 3, 4, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1);
        CHECK(CoXlTestLwSeenKeeps(0) == 0); /* beyond 250 m: filler like any */
        g_case = "at 72 units two road squads are kept and a third still swaps; a fourth does not: 13 units are the road patrols'";
        CoXlTestLwSeenWorld(solo, 1, 72);
        CoXlTestLwSeenSquad(0, 200, 0, 3, 4, 0); CoXlTestLwSeenSquad(10, 0, 200, 3, 4, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CHECK(CoXlTestLwSeenKeeps(0) == 1 && CoXlTestLwSeenKeeps(10) == 1 && CoXlTestLwSeenWhy(20, cap, 400000, 5) == 1);
        CoXlTestLwSeenSquad(30, 0, -900, 3, 4, 0);
        CHECK(CoXlTestLwSeenWhy(20, cap, 500000, 5) == 0 && CoXlTestLwSeenKeeps(0) == 1); /* a far road squad counts in the bubble's 13 too */
        g_case = "the landscape's far share of 18 is its own, with or without a road patrol on its way";
        CHECK(CoXlTestLwSeenFarRoom(3, 0, 11, 0) == 1 && CoXlTestLwSeenFarRoom(3, 0, 12, 0) == 0 && CoXlTestLwSeenFarRoom(3, 2, 12, 0) == 0);
        CHECK(CoXlTestLwSeenFarRoom(3, 0, 19, 8) == 1 && CoXlTestLwSeenFarRoom(3, 0, 20, 8) == 0 && CoXlTestLwSeenFarRoom(3, 0, 23, 12) == 1);
        CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 0); CHECK(CoXlTestLwSeenFarRoom(3, 0, 11, 0) == 1 && CoXlTestLwSeenFarRoom(3, 0, 12, 0) == 0);
        CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 8); CHECK(CoXlTestLwSeenFarRoom(3, 0, 11, 0) == 1 && CoXlTestLwSeenFarRoom(3, 0, 12, 0) == 0); /* one still out: no half is kept from the landscape */
        g_case = "a patrol on a player's road asks against its bubble's road patrols: 12 online of 13, no room for 4 more; with 8, room";
        CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1);
        CHECK(CoXlTestLwSeenFarRoom(20, 3, 0, 0) == 0 && CoXlTestLwSeenFarRoom(20, 3, 30, 12) == 0);
        CoXlTestLwSeenSquad(30, 0, -900, 1, 4, 0);
        CHECK(CoXlTestLwSeenFarRoom(20, 3, 0, 0) == 1 && CoXlTestLwSeenFarRoom(20, 3, 30, 8) == 1 && CoXlTestLwSeenFarRoom(20, 3, 32, 8) == 1 && CoXlTestLwSeenFarRoom(20, 3, 33, 8) == 0); /* 36 far units in all */
        CHECK(CoXlTestLwSeenFarRoom(20, 0, 14, 0) == 1 && CoXlTestLwSeenFarRoom(20, 0, 15, 0) == 0); /* on a player's road but asking by the far band's own rule: the landscape's share */
        CHECK(CoXlTestLwSeenFarRoom(3, 3, 11, 0) == 1 && CoXlTestLwSeenFarRoom(3, 3, 12, 0) == 0); /* asking beyond the band but on no player's road: the landscape's share too (7 + 8 road units would pass 13) */
        g_case = "the far sum: a standing far patrol that came online on a player's road counts whole, and is taken off the landscape's standing units";
        CoXlTestLwSeenSquad(20, 600, 0, 3, 4, 4);
        CHECK(CoXlTestLwSeenFarSum(20, 4, 1, 10, 4) == 14 && CoXlTestLwSeenFarSum(20, 4, 0, 14, 0) == 14 && CoXlTestLwSeenFarSum(20, 4, 1, 10, 24) == 16);
        CoXlTestLwSeenSquad(20, 600, 0, 3, 4, 2);
        CHECK(CoXlTestLwSeenFarSum(20, 4, 1, 10, 4) == 10 && CoXlTestLwSeenFarSum(20, 4, 1, 10, 24) == 16); /* online by the bands' own rule: the landscape's */
        CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0); CoXlTestLwSeenSquad(20, 250, 0, 1, 4, 1);
        g_case = "meet 0: a road patrol near a player is filler like any";
        CoXlTestLwSeenSwitch(0); CHECK(CoXlTestLwSeenKeeps(0) == 0);
        CoXlTestLwSeenSwitch(1); CHECK(CoXlTestLwSeenKeeps(0) == 1);
        g_case = "an event squad's hold counts in its own bubble only, and a patrol does not swap beside it there";
        CoXlTestLwSeenWorld(apart, 2, 23);
        CoXlTestLwSeenSquad(0, 200, 0, 1, 4, 1); CoXlTestLwSeenSquad(10, 3200, 0, 1, 4, 1); CoXlTestLwSeenSquad(7, 3100, 0, 1, 9, 0); CoXlTestLwSeenSquad(3, 100, 0, 1, 7, 0);
        CoXlTestLwSeenSquad(12, 3100, 0, 1, 7, 0); CoXlTestLwSeenSquad(5, 300, 0, 3, 6, 0); CoXlTestLwSeenSquad(6, 3300, 0, 3, 6, 0);
        CoXlTestLwSeenEventHold(7, 9);
        CHECK(CoXlTestLwSeenEventHeld(3) == 0 && CoXlTestLwSeenEventHeld(12) == 9 && CoXlTestLwSeenEventHeld(7) == 0);
        CHECK(CoXlTestLwSeenWhy(10, cap, 500000, 6) == 0 && CoXlTestLwSeenWhy(0, cap, 500000, 5) == 1);
        g_case = "a request counts the event squad's hold and the patrols' holds of its own bubble";
        CHECK(CoXlTestLwSeenEventHeld(3) == 4 && CoXlTestLwSeenEventHeld(12) == 9 && CoXlTestLwSeenEventHeld(0) == 0);
        g_case = "sent to reinforce while it waits with a hold: no wait, no priority, no mark, and its hold ends";
        CHECK(CoXlTestLwSeenErrand(0, 501000) == 0 && CoXlTestLwSeenEventHeld(3) == 0 && CoXlTestLwSeenEventHeld(12) == 9);
        CoXlTestLwSeenEventHold(-1, 0);
        g_case = "a patrol on its way to a player is placed and sight-tested before room is asked";
        { int places, rooms;
          CHECK(CoXlTestLwSeenGates(0, 1, &places, &rooms) == 0 && places == 1 && rooms == 0);
          CHECK(CoXlTestLwSeenGates(1, 0, &places, &rooms) == 0 && places == 1 && rooms == 1);
          CHECK(CoXlTestLwSeenGates(1, 1, &places, &rooms) == 1 && places == 1 && rooms == 1); }
        g_case = "a squad that came online on its way in stays for its walk in, within the widest band in play and its zone";
        { const float B = CoXlTestLwSeenBand();
          CoXlTestLwSeenWorld(solo, 1, 72); CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 2);
          const uint64_t stay = (uint64_t)((B + 150.0f - 350.0f) / 1.5f * 1000.0f);
          CHECK(stay > 250000u && stay < 350000u);
          CHECK(CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + stay - 1) == 1 && CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + stay) == 0);
          /* asked for ahead of a vehicle it stands up to the lead's 200 m farther out, and a vehicle that stops keeps it */
          CHECK(CoXlTestLwSeenStays(0, B + 349.0f, 1000, 2000) == 1 && CoXlTestLwSeenStays(0, B + 351.0f, 1000, 2000) == 0);
          CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 0); CHECK(CoXlTestLwSeenStays(0, 600.0f, 1000, 2000) == 0);
          CoXlTestLwSeenWorld(apart, 2, 23); CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 2); /* a second player: its scoped band */
          const float S = CoXlTestLwSeenRange(1, 2);
          CoXlTestLwSeenWorld(apart, 2, 23); CoXlTestLwSeenSquad(0, 700, 0, 3, 8, 2);
          CHECK(CoXlTestLwSeenStays(0, S + 349.0f, 1000, 2000) == 1 && CoXlTestLwSeenStays(0, S + 351.0f, 1000, 2000) == 0);
          CHECK(CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + 430000) == 1 && CoXlTestLwSeenStays(0, 600.0f, 1000, 1000 + 450000) == 0); }
        CoXlTestLwSeenEnd();
    }

    {
        int out[16], members = 0, more = 0;
        g_case = "road patrols are added after the world's squads, as many again, each of 4 units at most";
        CHECK(CoXlTestLwRoadTail(40, 51, 41, out, 16, &members, &more) == 9 && more == 4 && members == 431);
        CHECK(out[0] == 2 && out[1] == 101 && out[2] == 202 && out[3] == 301 && out[4] == 402 && out[5] == 411 && out[6] == 422 && out[7] == 431 && out[8] == 501);
        CHECK(CoXlTestLwRoadTail(40, 51, 39, out, 16, &members, &more) == 9 && members == 930);
        CHECK(CoXlTestLwRoadTail(40, 51, 30, out, 16, &members, &more) == 9 && members == 431);
        g_case = "a world whose squads are no multiple of ten: the added ones follow its last";
        CHECK(CoXlTestLwRoadTail(45, 52, 44, out, 16, &members, &more) == 11 && more == 5 && members == 930 && out[4] == 402 && out[5] == 452 && out[9] == 492 && out[10] == 501);
        g_case = "none added: one squad in ten, as before";
        CHECK(CoXlTestLwRoadTail(0, 51, 41, out, 16, &members, &more) == 6 && more == 0 && members == 930 && out[5] == 501);
    }
    {
        const float B = CoXlTestLwSeenBand(); int o[5];
        const float by[1][2] = {{1500, 20}}, sx[4] = {250, 2350, 1950, 650}; const int dir[4] = {1, 1, 1, -1}, none[4] = {0, 0, 0, 0};
        CoXlTestLwSeenSetup(1, 1, 0, 0, 0);
        g_case = "a 2 m unit's band through a 4x scope is 861 m, whatever the host's view";
        { const float S = CoXlTestLwSeenRange(1, 2); CHECK(S > 840.0f && S < 880.0f && S > B + 150.0f); }
        g_case = "a group measured at 8 m has its own slot's scoped band, wider than a 2 m unit's";
        { const float S2 = CoXlTestLwSeenScopedOf(2.0f), S8 = CoXlTestLwSeenScopedOf(8.0f); printf("scoped 2 m %.0f, 8 m %.0f\n", (double)S2, (double)S8); CHECK(S2 > 780.0f && S2 < 880.0f && S8 > S2 + 100.0f); }
        g_case = "a patrol is called to a player's road: the nearest one beyond the zone on the side whose turn it is, about turn";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 30 && o[0] == 7 && o[1] == 17 && o[2] == 23 && o[3] == 6 && o[4] == 1); /* 800 m past the player */
        g_case = "a called patrol is not called again: the next one out on that side, walking on as it was";
        CHECK(CoXlTestLwCallOne(0, 0, o) == 0 && o[0] == 3 && o[1] == 21 && o[2] == 23 && o[3] == 2);
        g_case = "none left on the side whose turn it is: one from the other side, and its way goes on past the player";
        CHECK(CoXlTestLwCallOne(0, 0, o) == 10 && o[0] == 23 && o[1] == 17 && o[2] == 7 && o[3] == 24);
        g_case = "a patrol inside a player's band is not called: it would be offline in clear view";
        CHECK(CoXlTestLwCallOne(0, 0, o) == -1);
        g_case = "a called patrol's next plan, or an errand, ends its call";
        CHECK(CoXlTestLwCallOf(30) == 1 && CoXlTestLwCallOf(0) == 1 && CoXlTestLwCallOf(10) == 1 && CoXlTestLwCallOf(20) == 0);
        CHECK(CoXlTestLwCallOver(30, 1) == 0 && CoXlTestLwCallOver(0, 0) == 0);
        g_case = "a called patrol that turns back is free again; one that comes online is counted and free";
        CHECK(CoXlTestLwCallOver(10, 2) == 0);
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 30 && CoXlTestLwCallOver(30, 3) == 0);
        g_case = "a patrol standing in the zone it comes online in is left there: only one beyond it is called";
        { const float zx[2] = {250, 800}; const int zd[2] = {1, 1};
          CoXlTestLwCallWorld(30, 0, by, 1, zx, zd, none, 2); CHECK(CoXlTestLwCallOne(0, 0, o) == 0); }
        g_case = "a patrol refused the far share stands in the zone; once the players' bands have moved, or its way no longer leads to one, it stands no more";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallZone(1500.0f - B - 50.0f, 19, 1500.0f - B - 60.0f, 0) == 3);
        CHECK(CoXlTestLwCallZone(1500.0f - B - 50.0f, 19, 1500.0f - B + 30.0f, 0) == 1);
        CHECK(CoXlTestLwCallZone(1500.0f - B - 50.0f, 19, 1500.0f - B - 150.0f, 0) == 1);
        CHECK(CoXlTestLwCallZone(1500.0f - B - 50.0f, 19, 1500.0f - B - 60.0f, 1) == 1);
        g_case = "the other side's turn";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallOne(0, 1, o) == 10);
        g_case = "a patrol already on its way to a player, or waiting at a ring, is left to it";
        { const int mk[4] = {0, 0, 0, 8};
          CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, mk, 4); CHECK(CoXlTestLwCallOne(0, 0, o) == 0); }
        g_case = "a player 79 m from the road is on it; one 81 m off, or with no road node within 150 m, is not";
        { const float near[1][2] = {{1500, 79}}, off[1][2] = {{1500, 81}}, far[1][2] = {{1550, 145}};
          CoXlTestLwCallWorld(30, 0, near, 1, sx, dir, none, 4); CHECK(CoXlTestLwCallOne(0, 0, o) == 30);
          CoXlTestLwCallWorld(30, 0, off, 1, sx, dir, none, 4); CHECK(CoXlTestLwCallOne(0, 0, o) == -2);
          CoXlTestLwCallWorld(30, 0, far, 1, sx, dir, none, 4); CHECK(CoXlTestLwCallOne(0, 0, o) == -2); }
        g_case = "the way past the player: 400 m at least, straight on through a junction, then to the next junction or 800 m";
        { const float at9[1][2] = {{900, 20}}, at13[1][2] = {{1300, 20}}, at11[1][2] = {{1100, 20}}, one[1] = {1850}, two[1] = {2250}, mid[1] = {2050}; const int back[1] = {-1};
          CoXlTestLwCallWorld(30, 7, at9, 1, one, back, none, 1);
          CHECK(CoXlTestLwCallOne(0, 1, o) == 0 && o[0] == 18 && o[1] == 18 && o[2] == 1); /* 200 m to the junction: on along the road, 800 m past in all */
          CoXlTestLwCallWorld(30, 7, at13, 1, two, back, none, 1);
          CHECK(CoXlTestLwCallOne(0, 1, o) == 0 && o[0] == 22 && o[1] == 16 && o[2] == 7); /* 600 m to the junction: it ends there */
          CoXlTestLwCallWorld(30, 7, at11, 1, mid, back, none, 1);
          CHECK(CoXlTestLwCallOne(0, 1, o) == 0 && o[0] == 20 && o[1] == 14 && o[2] == 7); /* a junction just 400 m past: it ends there */ }
        g_case = "a patrol more than 23 road nodes out is not called; one 23 out has a plan of 29 nodes to the road's end";
        { const float at5[1][2] = {{500, 20}}, at6[1][2] = {{600, 20}}, last[1] = {2950}; const int back[1] = {-1};
          CoXlTestLwCallWorld(30, 0, at5, 1, last, back, none, 1); CHECK(CoXlTestLwCallOne(0, 1, o) == -1); /* 24 nodes out, 2450 m */
          CoXlTestLwCallWorld(30, 0, at6, 1, last, back, none, 1); CHECK(CoXlTestLwCallOne(0, 1, o) == 0 && o[0] == 29 && o[1] == 29 && o[2] == 1); }
        g_case = "one call a second at most, and a player's next call a patrol's time online on: 532 s solo where the road patrols' part holds one squad (6 of 36)";
        { const uint32_t period = co_lw_meet_call_ms(6, 4, 1, B + 50.0f + 350.0f, 0);
          CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
          CHECK(period > 525000u && period < 540000u);
          CHECK(CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallTick(1000500u) == 1 && CoXlTestLwCallTick(1002000u) == 1);
          /* the second call is a period on, 15 % of it early or late at most */
          CHECK(CoXlTestLwCallTick(1000000u + period - period * 15u / 100u - 1500u) == 1 && CoXlTestLwCallTick(1000000u + period + period * 15u / 100u + 1500u) == 2);
          CHECK(CoXlTestLwCallOf(30) == 1 && CoXlTestLwCallOf(10) == 1 && CoXlTestLwCallOf(0) == 0); /* the second from the other side */ }
        g_case = "a player off the road has no part in the period; one walking toward the patrol has a shorter one";
        { const float one[2][2] = {{1500, 20}, {1600, 500}};
          const uint32_t period = co_lw_meet_call_ms(6, 4, 1, B + 50.0f + 350.0f, 0), quick = co_lw_meet_call_ms(6, 4, 1, B + 50.0f + 350.0f, 1.5f);
          CoXlTestLwCallWorld(30, 0, one, 2, sx, dir, none, 4);
          CHECK(CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallTick(1000000u + period + period * 15u / 100u + 1500u) == 2);
          CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
          CoXlTestLwCallPlayer(0, 1500.0f, 1.5f);
          CHECK(quick > 290000u && quick < 320000u && CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallTick(1000000u + quick - quick * 15u / 100u - 1500u) == 1 && CoXlTestLwCallTick(1000000u + quick + quick * 15u / 100u + 1500u) == 2);
          CoXlTestLwCallPlayer(0, 1500.0f, 0); }
        g_case = "two players on a road: each has its call in turn, and twice the time to its next";
        { const float pair[2][2] = {{1500, 20}, {1600, -20}}, wide[4] = {250, 450, 550, 2750}; const int d4[4] = {1, 1, -1, -1};
          const uint32_t period = co_lw_meet_call_ms(6, 4, 1, B + 50.0f + 350.0f, 0);
          const uint32_t first = co_lw_meet_call_ms(6, 4, 2, CoXlTestLwSeenScopedOf(2.0f) + 100.0f + 350.0f, 0) / 2u;
          CoXlTestLwCallWorld(30, 0, pair, 2, wide, d4, none, 4);
          /* the second player's first call is half its own period on (a peer's far walk is the scoped band's), from the other side */
          CHECK(first > 620000u && first < 640000u && 2u * period > first + 50000u);
          CHECK(CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallTick(1000500u) == 1 && CoXlTestLwCallTick(1001000u) == 1 && CoXlTestLwCallTick(1000000u + first - 2000u) == 1);
          CHECK(CoXlTestLwCallOf(20) == 1 && CoXlTestLwCallOf(30) == 0);
          CHECK(CoXlTestLwCallTick(1000000u + first + 2000u) == 2 && CoXlTestLwCallOf(30) == 2 && CoXlTestLwCallOf(10) == 0);
          CHECK(CoXlTestLwCallTick(1000000u + 2u * period - 2u * period * 15u / 100u - 2000u) == 2 && CoXlTestLwCallTick(1000000u + 2u * period + 2u * period * 15u / 100u + 2000u) == 3); }
        g_case = "three players all due in one tick: one call a tick, the others in the ticks after";
        { const float three[3][2] = {{1500, 20}, {1600, -20}, {1700, 20}}, wide[4] = {250, 450, 550, 2750}; const int d4[4] = {1, 1, -1, -1};
          CoXlTestLwCallWorld(30, 0, three, 3, wide, d4, none, 4);
          CHECK(CoXlTestLwCallTick(1000000u) == 1);
          CHECK(CoXlTestLwCallTick(3000000u) == 2 && CoXlTestLwCallTick(3000500u) == 2 && CoXlTestLwCallTick(3001000u) == 3 && CoXlTestLwCallTick(3002000u) == 4); }
        g_case = "a patrol of a group never measured is not called: the next one out is";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CoXlTestLwCallGroup(30, 9);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 0 && CoXlTestLwCallOf(30) == 0);
        g_case = "a player walking along the road has the patrol called from ahead, whatever side's turn it is";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CoXlTestLwCallPlayer(0, 1500.0f, 1.5f);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 10 && CoXlTestLwCallOne(0, 0, o) == 30); /* slower than a patrol: with none ahead, one from behind */
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CoXlTestLwCallPlayer(0, 1500.0f, 3.0f);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 10 && CoXlTestLwCallOne(0, 0, o) == -1); /* at 3 m/s one from behind never arrives: none */
        CoXlTestLwCallPlayer(0, 1500.0f, -3.0f);
        CHECK(CoXlTestLwCallOne(0, 1, o) == 30);
        g_case = "a called patrol whose player walks 150 m away from it is let go, and may be called again";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallOf(30) == 1 && CoXlTestLwCallTick(1001000u) == 1);
        CoXlTestLwCallPlayer(0, 1640.0f, 0); CHECK(CoXlTestLwCallTick(1002000u) == 1 && CoXlTestLwCallOf(30) == 1 && CoXlTestLwCallLost() == 0);
        CoXlTestLwCallPlayer(0, 1660.0f, 0); CHECK(CoXlTestLwCallTick(1004000u) == 1 && CoXlTestLwCallOf(30) == 0 && CoXlTestLwCallLost() == 1);
        g_case = "called again from farther out, its nearest so far starts anew: it is not let go at once";
        CoXlTestLwCallPlayer(0, 1900.0f, 0);
        CHECK(CoXlTestLwCallOne(0, 0, o) == 30 && CoXlTestLwCallTick(1006000u) == 2 && CoXlTestLwCallOf(30) == 1 && CoXlTestLwCallLost() == 1);
        CoXlTestLwCallPlayer(0, 1500.0f, 0);
        g_case = "a player whose called patrol was let go is called again 5 s on, not a period on";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CHECK(CoXlTestLwCallTick(1000000u) == 1 && CoXlTestLwCallTick(1001000u) == 1);
        CoXlTestLwCallPlayer(0, 1660.0f, 0); CHECK(CoXlTestLwCallTick(1002000u) == 1 && CoXlTestLwCallLost() == 1);
        CHECK(CoXlTestLwCallTick(1006000u) == 1 && CoXlTestLwCallTick(1007500u) == 2);
        CoXlTestLwCallPlayer(0, 1500.0f, 0);
        g_case = "none to call for a spell: asked again every 5 s; then both players are called a second apart, and each one's even time is back in its own place";
        { const float pair[2][2] = {{1500, 20}, {1600, -20}}, wide[4] = {250, 450, 550, 2750}; const int d4[4] = {1, 1, -1, -1};
          const uint32_t p0 = co_lw_meet_call_ms(6, 4, 2, B + 50.0f + 350.0f, 0), p1 = co_lw_meet_call_ms(6, 4, 2, CoXlTestLwSeenScopedOf(2.0f) + 100.0f + 350.0f, 0);
          const uint64_t t0 = 1000000u, late = t0 + 2u * (uint64_t)p1 + 300000u;
          CoXlTestLwCallWorld(30, 0, pair, 2, wide, d4, none, 4);
          for (int k = 0; k < 4; k++) CoXlTestLwCallGroup(10 * k, 9);
          CHECK(CoXlTestLwCallTick(t0) == 0 && CoXlTestLwCallTick(t0 + 4000u) == 0 && CoXlTestLwCallSlot(0) == t0 && CoXlTestLwCallSlot(1) == t0 + p1 / 2u);
          for (int k = 0; k < 4; k++) CoXlTestLwCallGroup(10 * k, 1);
          CHECK(CoXlTestLwCallTick(late) == 1 && CoXlTestLwCallTick(late + 1000u) == 2);
          CHECK((CoXlTestLwCallSlot(0) - t0) % p0 == 0 && (CoXlTestLwCallSlot(1) - t0 - p1 / 2u) % p1 == 0 && CoXlTestLwCallSlot(0) > late && CoXlTestLwCallSlot(1) > late);
          CHECK(CoXlTestLwCallTick(late + (uint64_t)p0 * 45u / 100u) == 2); /* neither again inside 6/10 of a period less the jitter */ }
        g_case = "two players in line on one road: a patrol called to the inner one that comes online by the outer one is counted as such";
        { const float line[2][2] = {{2400, 20}, {1400, 20}}, west[1] = {350}, east[1] = {3650}; const int in[1] = {1}, back[1] = {-1};
          CoXlTestLwCallWorld(40, 0, line, 2, west, in, none, 1);
          CHECK(CoXlTestLwCallOne(0, 0, o) == 0 && o[4] == 1 && CoXlTestLwCallOver(0, 3) == 0 && CoXlTestLwCallCameOther() == 1);
          CoXlTestLwCallWorld(40, 0, line, 2, east, back, none, 1);
          CHECK(CoXlTestLwCallOne(0, 1, o) == 0 && o[4] == 1 && CoXlTestLwCallOver(0, 3) == 0 && CoXlTestLwCallCameOther() == 0); }
        g_case = "meet 0: no patrol is called";
        CoXlTestLwCallWorld(30, 0, by, 1, sx, dir, none, 4);
        CoXlTestLwSeenSwitch(0); CHECK(CoXlTestLwCallTick(1000000u) == 0); CoXlTestLwSeenSwitch(1);
        CHECK(CoXlTestLwCallTick(1002000u) == 1);
        CoXlTestLwCallEnd();
    }

    g_case = "a squad off the roads never draws a road node as its destination";
    int failed = 0;
    CHECK(CoXlTestLwPlansRoads(7782.0f, 200.0f, 3000.0f, 3000.0f, 300, &failed) == 0 && failed == 0);

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
