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

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    return 0;
}
