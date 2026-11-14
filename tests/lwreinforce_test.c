/* The living world's event squads at the spawn band (an offline reinforcement whose fight is nearer a player than the
 * band), the refusal gates' counts, members lost moments ago, member slots, the status note's limit and the note of an
 * online squad that stands still with a goal. Fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLwReinforce(const float* pl, const float* at, const float* t);
void CoXlTestLwPlayerAt(const float* pl);
void CoXlTestLwWalk(float dt, int* stage, int* held, float* pos, uint64_t* stageAt);
int CoXlTestLwIntent(void);
void CoXlTestLwRefuse(const char* why);
void CoXlTestLwRefuseList(char* out, size_t size);
void CoXlTestLwLostAdd(uint32_t id, int squad);
int CoXlTestLwSquadOf(uint32_t id, uint64_t later, int* dead);
void CoXlTestLwSlot(const float* pos, uint32_t i, uint32_t n, float* to);
int CoXlTestLwBudget(int density, int eff, int b0, int b1);
void CoXlTestLwHeldWhy(const char* why);
uint32_t CoXlTestLwStall(int fresh, const float* at, const float* goal, uint64_t now);
void CoXlTestLwStallLines(uint32_t n);
int CoXlTestLwGates(int held, int placeOk, int roomOk, int* places, int* rooms, int* retryMs);
void CoXlTestLwMissionClear(void);
uint32_t CoXlTestLwStallOther(void);
uint32_t CoXlTestLwStallNoGoal(void);
int CoXlTestLwSlotsChecked(int held, uint32_t waited);
int CoXlTestLwJobSettled(int state, uint32_t waited);
int CoXlTestLwNodeReached(int intent, float nodeX, float goalX, int goalNode, float atX);
int CoXlTestLwGoalNode(void);
void CoXlTestLwStallApply(int intent, int tries, const int* read, float atX, float goalX, float snapX, int* calls, float* to, float* goalNow);
uint32_t CoXlTestLwStallCount(int what);
int CoXlTestLwStallWaits(int set);
void CoXlTestLwHoldAge(int what, uint32_t ms);
void CoXlTestLwStallSet(int rushed, float dist);
int CoXlTestLwIntentEndGoal(void);
void CoXlTestLwStallPath(float left);
int CoXlTestLwHoldLane(int rank);
int CoXlTestLwHoldFree(int a, int b);
float CoXlTestLwHoldTurns(int lane, int n, float other, int waiting);
void CoXlTestLwHoldTarget(float x, float y);
void CoXlTestLwHoldPoint(float a, int k, float* out);
void CoXlTestLwHoldTwo(int idx, float* pos);
int CoXlTestLwPlans(float radius, float step, float x, float y, int plans, int* beyond, int* turned);
int CoXlTestLwPlansAt(float radius, float step, const float* at, const float* home, int plans, int notes, int* inHome, int* turned);
void CoXlTestLwHoldNext(float* pos);
uint64_t CoXlTestLwHeldFrom(void);
int CoXlTestLwStallTries(int set);
int CoXlTestLwStallStep(int intent, int tries, int action, int busy, int snapOff, int* after);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static int Count(const char* text)
{
    int n = 0;
    for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p += strlen(text)) n++;
    return n;
}
static float Dist(const float* a, const float* b) { return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])); }

/* A fight 125 m from the player, inside the 150 m band; the squad waits 158 m out, 34 m from the fight. */
static void BandEdge(void)
{
    const float pl[2] = {0, 0}, edge[2] = {158, 10}, fight[2] = {125, 0};
    int stage = 9, held = 9; float pos[2]; uint64_t at = 0;
    g_case = "within reach of a fight inside the band: it waits at the edge in stage 0, it does not arrive offline";
    CoXlTestLwReinforce(pl, edge, fight);
    for (int k = 0; k < 5; k++)
    {
        CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
        CHECK(stage == 0 && held == 1);
    }
    CHECK(pos[0] == edge[0] && pos[1] == edge[1]); /* the hold keeps it where it waits */
    const uint64_t heldFrom = CoXlTestLwHeldFrom();
    CHECK(heldFrom != 0 && heldFrom + 2000 > GetTickCount64());
    CHECK(Count("reached the fight") == 0);

    g_case = "a player stepping up pushes it out to the edge; still stage 0, no arrival";
    const float closer[2] = {12, 0};
    CoXlTestLwPlayerAt(closer);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && held == 1 && Dist(pos, closer) > 157.0f && Dist(pos, closer) < 159.0f);
    for (int k = 0; k < 3; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && Count("reached the fight") == 0);

    g_case = "the player gone, the point is out of the band: it arrives once, and a second arrival keeps the first's time";
    const float away[2] = {-300, 0};
    CoXlTestLwPlayerAt(away);
    for (int k = 0; k < 400 && stage == 0; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at); /* the last leg to the fight */
    CHECK(stage == 1 && Count("reached the fight") == 1);
    const uint64_t first = at;
    CHECK(first + 2000 > GetTickCount64());
    Sleep(20); /* a second wait and a second arrival come at a later millisecond than the first */
    CoXlTestLwPlayerAt(fight); /* the player walks up to it: inside the band, back to stage 0 and out to the edge */
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && held == 1 && CoXlTestLwHeldFrom() == heldFrom); /* waiting again in the same intent: its wait is not started again */
    CoXlTestLwPlayerAt(away);
    for (int k = 0; k < 4000 && stage == 0; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 1 && at == first && Count("reached the fight") == 1); /* the 120 s wait still counts from the first */
    CHECK(CoXlTestLwIntent() == 2); /* still reinforcing: the fight lasts */

    g_case = "a fight just outside the band (152 m of 150): within reach it arrives offline, no wait at the edge";
    const float out[2] = {190, 0}, justOut[2] = {152, 0};
    const int before = Count("reached the fight");
    CoXlTestLwReinforce(pl, out, justOut);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 1 && held == 0 && Count("reached the fight") == before + 1);

    g_case = "arrived by a point just outside the band, pushed out by a player stepping in: it waits at the edge, no second arrival";
    const float far0[2] = {-300, 0}, sq[2] = {165, 0}, point[2] = {200, 0}, in[2] = {20, 0}, in2[2] = {25, 0};
    const int reached = Count("reached the fight"), inside = Count("is at its point inside the spawn band");
    CoXlTestLwReinforce(far0, sq, point);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 1 && Count("reached the fight") == reached + 1);
    CoXlTestLwPlayerAt(in); /* 145 m: inside the band; the point 180 m out */
    for (int k = 0; k < 5; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && held == 1 && Dist(pos, in) > 157.0f && Count("reached the fight") == reached + 1);
    CoXlTestLwPlayerAt(in2);
    for (int k = 0; k < 5; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && held == 1 && Count("reached the fight") == reached + 1);
    CHECK(Count("is at its point inside the spawn band") == inside + 1);
    g_case = "the player gone, it is at its point again: stage 1, the first arrival's note and time kept";
    CoXlTestLwPlayerAt(far0);
    for (int k = 0; k < 5 && stage == 0; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 1 && Count("reached the fight") == reached + 1);
    g_case = "inside the band again in the same intent: back to the edge, the inside note not repeated";
    const float in3[2] = {pos[0] - 140.0f, 0}; /* 140 m from where it stands */
    CoXlTestLwPlayerAt(in3);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0 && Count("is at its point inside the spawn band") == inside + 1);

    /* arrived by a point outside the band, then a player 145 m away who stands still: squad 2650's wait */
    for (int end = 0; end < 3; end++)
    {
        g_case = "the player standing still, it waits at the edge with its point out of the band";
        CoXlTestLwReinforce(far0, sq, point);
        CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
        CoXlTestLwPlayerAt(in);
        for (int k = 0; k < 5; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
        CHECK(stage == 0 && held == 1 && CoXlTestLwIntent() == 2);
        const char* why = end == 1 ? "the fight ended while it waited at the spawn band" : "240 s, waiting at the spawn band";
        const int moves = Count("along the spawn band's edge"), ended = Count(why);
        if (end == 2)
        {
            g_case = "20 s at a point on its own bearing: it stays in its wait (LwHoldNext moves it on after each refusal)";
            const float was[2] = {pos[0], pos[1]};
            CoXlTestLwHoldAge(1, 21000);
            CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
            CHECK(stage == 0 && held == 1 && Count("along the spawn band's edge") == moves && pos[0] == was[0] && pos[1] == was[1]);
            continue;
        }
        g_case = "the wait ends with its fight, and after 240 s";
        CoXlTestLwHoldAge(3, 0); CoXlTestLwHoldAge(end ? 2 : 0, 240000);
        CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
        CHECK(CoXlTestLwIntent() == 0 && Count(why) == ended + 1);
    }
}

/* Waiting squads: lanes round the player and the points tried in each. */
static void HoldPoints(void)
{
    float p[9][2], q[9][2], pos[2], pos1[2], next[2];
    int stage, held; uint64_t at;
    g_case = "the n-th waiting squad's lane: 0, +1, -1, +2, -2";
    CHECK(CoXlTestLwHoldLane(0) == 0 && CoXlTestLwHoldLane(1) == 1 && CoXlTestLwHoldLane(2) == -1 && CoXlTestLwHoldLane(3) == 2 && CoXlTestLwHoldLane(4) == -2);
    g_case = "nine different points per lane, 8 to 20 m outside the band, within 10 m of the bearing";
    const float o[2] = {0, 0};
    for (int k = 0; k < 9; k++) { CoXlTestLwHoldPoint(0.0f, k, p[k]); CoXlTestLwHoldPoint(0.56f, k, q[k]); }
    CHECK(p[0][0] == 158.0f && p[0][1] == 0.0f);
    for (int k = 0; k < 9; k++)
    {
        CHECK(Dist(p[k], o) >= 157.9f && Dist(p[k], o) <= 170.1f && p[k][1] > -10.1f && p[k][1] < 10.1f);
        for (int j = 0; j < k; j++) CHECK(Dist(p[k], p[j]) > 5.0f);
        g_case = "points of neighbouring lanes are over 60 m apart";
        for (int j = 0; j < 9; j++) CHECK(Dist(p[k], q[j]) > 65.0f);
    }
    g_case = "two squads waiting at one point move to lanes over 60 m apart, the first on the fight's bearing";
    const float pl[2] = {0, 0}, edge[2] = {158, 10}, fight[2] = {125, 0};
    CoXlTestLwReinforce(pl, edge, fight);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(held == 1);
    CoXlTestLwHoldTwo(1, pos1);
    CoXlTestLwHoldTwo(0, pos);
    CHECK(Dist(pos, pos1) > 60.0f && fabsf(pos[1]) < 1.0f && Dist(pos, pl) > 157.0f && Dist(pos1, pl) > 157.0f);
    g_case = "a refused request moves a waiting squad to its lane's next point, never into the band; others stay";
    CoXlTestLwHoldNext(next);
    CHECK(Dist(next, pos) > 5.0f && Dist(next, pos) < 25.0f && Dist(next, pl) >= 150.0f && Dist(next, pos1) > 60.0f);
    CHECK(fabsf(next[0] - 164.0f) < 0.01f && fabsf(next[1]) < 0.01f); /* 6 m farther out on its lane's bearing */
    float seen[9][2]; int distinct = 1;
    seen[0][0] = next[0]; seen[0][1] = next[1];
    for (int k = 1; k < 8; k++) { CoXlTestLwHoldNext(seen[k]); for (int j = 0; j < k; j++) distinct &= Dist(seen[k], seen[j]) > 5.0f; }
    CHECK(distinct);
    g_case = "all nine refused: the bearing turns 15 degrees, to the side clear of the other waiting squad";
    CoXlTestLwHoldNext(next); /* the ninth refusal: the first point of the new bearing */
    CHECK(fabsf(Dist(next, pl) - 158.0f) < 0.01f && fabsf(atan2f(next[1], next[0]) + 0.2618f) < 0.001f);
    for (int k = 0; k < 8; k++) CoXlTestLwHoldNext(seen[k]);
    CoXlTestLwHoldNext(next);
    CHECK(fabsf(atan2f(next[1], next[0]) + 0.5236f) < 0.001f);
    g_case = "20 s on its bearing: its points go on from where they are; the turned bearing is kept";
    for (int k = 0; k < 3; k++) CoXlTestLwHoldNext(next);
    CoXlTestLwHoldTwo(0, pos);
    CHECK(pos[0] == next[0] && pos[1] == next[1]);
    CoXlTestLwHoldNext(next); /* the fifth point: 6 m farther out, 10 m to the side */
    CHECK(fabsf(Dist(next, pl) - 164.0f) < 0.01f && fabsf(atan2f(next[1], next[0]) - (-0.5236f + 10.0f / 164.0f)) < 0.001f);
    g_case = "20 s with the fight on another bearing: it moves there and starts at that bearing's first point";
    CoXlTestLwHoldTarget(125.0f * cosf(0.1f), 125.0f * sinf(0.1f));
    CoXlTestLwHoldTwo(0, pos);
    CHECK(fabsf(Dist(pos, pl) - 158.0f) < 0.01f && fabsf(atan2f(pos[1], pos[0]) - (-0.5236f + 0.1f)) < 0.001f);
    CoXlTestLwHoldNext(next);
    CHECK(fabsf(Dist(next, pl) - 164.0f) < 0.01f && fabsf(atan2f(next[1], next[0]) - (-0.5236f + 0.1f)) < 0.001f);
    CoXlTestLwHoldTarget(fight[0], fight[1]);
    g_case = "dead ends in turn: 15, 30, 45 degrees one way, then the other side, then its lane again; mirrored in a lower lane";
    const float turns[8] = {0, 0.2618f, 0.5236f, 0.7854f, -0.2618f, -0.5236f, -0.7854f, 0};
    for (int n = 0; n < 8; n++) CHECK(fabsf(CoXlTestLwHoldTurns(0, n, 1.56f, 0) - turns[n]) < 0.0001f);
    for (int n = 0; n < 8; n++) CHECK(fabsf(CoXlTestLwHoldTurns(-1, n, 1.56f, 0) + turns[n]) < 0.0001f);
    CHECK(fabsf(CoXlTestLwHoldTurns(2, 1, 1.56f, 0) - 0.2618f) < 0.0001f);
    g_case = "a turn never comes within 0.5 rad of another waiting squad's bearing";
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 1, 1.56f, 1) + 0.2618f) < 0.0001f); /* +15, +30, +45 degrees are by the squad at +0.56 */
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 1, 1.56f, 2) - 0.2618f) < 0.0001f); /* online: its bearing is free */
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 1, 1.0f + 0.7854f + 0.49f, 1) - 0.2618f) < 0.0001f && fabsf(CoXlTestLwHoldTurns(0, 3, 1.0f + 0.7854f + 0.49f, 1) + 0.2618f) < 0.0001f);
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 3, 1.0f + 0.7854f + 0.51f, 1) - 0.7854f) < 0.0001f);
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 1, 1.0f - 6.2831853f + 0.5236f, 1) + 0.2618f) < 0.0001f); /* the same bearing a turn round */
    CHECK(fabsf(CoXlTestLwHoldTurns(0, 1, 1.0f + 6.2831853f + 0.5236f, 1) + 0.2618f) < 0.0001f);
    g_case = "a newly waiting squad takes the first lane no waiting squad has; a squad that ended frees its lane";
    CHECK(CoXlTestLwHoldFree(99, 99) == 0 && CoXlTestLwHoldFree(0, 99) == 1 && CoXlTestLwHoldFree(0, 1) == -1 && CoXlTestLwHoldFree(99, 1) == 0 && CoXlTestLwHoldFree(1, 0) == -1);
    const float far0[2] = {-300, 0};
    CoXlTestLwReinforce(far0, edge, fight); /* not waiting: on its way, 458 m from the player */
    CoXlTestLwHoldNext(next);
    CHECK(next[0] == edge[0] && next[1] == edge[1]);
}

static void Refusals(void)
{
    char list[384];
    g_case = "request refusals are counted by gate, the bubble's budget apart from the old cap";
    CoXlTestLwRefuseList(list, sizeof list); /* cleared */
    CoXlTestLwRefuse("the cap: bubble 62 online + 9 + 9 held over 72 (1 players)");
    CoXlTestLwRefuse("the cap: bubble 54 online + 11 + 9 held over 72 (1 players)");
    CoXlTestLwRefuse("the cap: 40 online + 9 over 62 (ceiling 48)");
    CoXlTestLwRefuse("no player record with bits 4 and 11 clear");
    CoXlTestLwRefuse("something new");
    CoXlTestLwRefuseList(list, sizeof list);
    CHECK(strcmp(list, "no living player 1, the bubble budget 2, the cap 1, other 1") == 0);
    CoXlTestLwRefuseList(list, sizeof list);
    CHECK(list[0] == 0); /* each 30 s note starts again */
    g_case = "a new mission starts its refusal counts at 0";
    CoXlTestLwRefuse("the cap: 40 online + 9 over 62 (ceiling 48)");
    CoXlTestLwMissionClear();
    CoXlTestLwRefuseList(list, sizeof list);
    CHECK(list[0] == 0);
}

static void Gates(void)
{
    int places, rooms, retry;
    g_case = "member slots are sight-checked for every near squad, for a waiting one only in its first 40 s at the edge";
    CHECK(CoXlTestLwSlotsChecked(0, 0) == 1 && CoXlTestLwSlotsChecked(0, 600000) == 1);
    CHECK(CoXlTestLwSlotsChecked(1, 0) == 1 && CoXlTestLwSlotsChecked(1, 39000) == 1);
    CHECK(CoXlTestLwSlotsChecked(1, 40000) == 0 && CoXlTestLwSlotsChecked(1, 200000) == 0);
    g_case = "a waiting squad is placed first; placed and refused room, it asks again in a second, not next tick";
    CHECK(CoXlTestLwGates(1, 1, 0, &places, &rooms, &retry) == 0 && places == 1 && rooms == 1 && retry > 900 && retry <= 1000);
    g_case = "a waiting squad refused placement is not asked about room (the 3 s retry is placement's own)";
    CHECK(CoXlTestLwGates(1, 0, 1, &places, &rooms, &retry) == 0 && places == 1 && rooms == 0);
    g_case = "a waiting squad placed with room: requested, placed once";
    CHECK(CoXlTestLwGates(1, 1, 1, &places, &rooms, &retry) == 1 && places == 1 && rooms == 1 && retry == 0);
    g_case = "any other squad asks room first; refused, no placement and no retry";
    CHECK(CoXlTestLwGates(0, 1, 0, &places, &rooms, &retry) == 0 && places == 0 && rooms == 1 && retry == 0);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1 && places == 1 && rooms == 1);
}

static void Lost(void)
{
    int dead = 7;
    g_case = "a member lost a moment ago still names its squad, as a dead one";
    CoXlTestLwLostAdd(0x78A, 2009);
    CHECK(CoXlTestLwSquadOf(0x78A, 0, &dead) == 2009 && dead == 1);
    CHECK(CoXlTestLwSquadOf(0x78A, 9000, &dead) == 2009);
    g_case = "after 10 s, or never lost, it is no squad's";
    CHECK(CoXlTestLwSquadOf(0x78A, 11000, &dead) == -1 && dead == 0);
    CHECK(CoXlTestLwSquadOf(0x999, 0, &dead) == -1);
    CHECK(CoXlTestLwSquadOf(0, 0, &dead) == -1);
}

static void Slots(void)
{
    const float p[3] = {100, 200, 5};
    float t[3];
    g_case = "member slots as the squad create places them: r = 5 sqrt((i + 0.5) / (n - 0.5)), a = (i + 1) 2 pi / 2.618034";
    CoXlTestLwSlot(p, 0, 9, t);
    CHECK(fabsf(Dist(t, p) - 1.2127f) < 0.001f && fabsf(atan2f(t[1] - p[1], t[0] - p[0]) - 2.39996f) < 0.001f && t[2] == 5);
    CoXlTestLwSlot(p, 8, 9, t);
    CHECK(fabsf(Dist(t, p) - 5.0f * sqrtf(8.5f / 8.5f)) < 0.001f);
}

static void Budget(void)
{
    g_case = "the status note's limit: the bubbles' budgets under the density rules, the effective cap otherwise";
    CHECK(CoXlTestLwBudget(1, 62, 72, 23) == 95);
    CHECK(CoXlTestLwBudget(0, 62, 72, 23) == 62);
}

static void HeldWhy(void)
{
    g_case = "why a waiting squad is not online: noted once per reason, again when it changes";
    CoXlTestLwHeldWhy("placement (LwPlace)");
    CoXlTestLwHeldWhy("placement (LwPlace)");
    CHECK(Count("not online: placement (LwPlace)") == 1);
    CoXlTestLwHeldWhy("sight: seen");
    CHECK(Count("not online: sight: seen") == 1);
    g_case = "the same gate with other live counts is the same reason";
    CoXlTestLwHeldWhy("the cap: bubble 62 online + 9 + 9 held over 72 (1 players)");
    CoXlTestLwHeldWhy("the cap: bubble 54 online + 11 + 9 held over 72 (1 players)");
    CHECK(Count("not online: the cap: bubble") == 1);
    CoXlTestLwHeldWhy("the far share: 40 far units + 9 over 47");
    CoXlTestLwHeldWhy("the far share: 41 far units + 9 over 47");
    CHECK(Count("not online: the far share") == 1);
    g_case = "at most 80 such lines a mission";
    char why[32];
    for (int k = 0; k < 200; k++) { snprintf(why, sizeof why, "reason %c%c", 'a' + k % 26, 'a' + k / 26); CoXlTestLwHeldWhy(why); }
    CHECK(Count("waiting at the spawn band's edge,") == 80 && Count("(the last such line this mission)") == 1);
}

/* LW_STALL_*: 1 left to a brain action, 2 waits, 3 goal again, 4 goal moved onto the leader's layer, 5 new goal;
 * intents: 0 patrol, 1 investigate, 2 reinforce, 3 retake. */
static void StallStep(void)
{
    int after = -1;
    g_case = "a brain action on the leader: nothing is done and no try is used, each window counted";
    CoXlTestLwStallWaits(0);
    CHECK(CoXlTestLwStallStep(0, 0, 1, 0, 1, &after) == 1 && after == 0 && CoXlTestLwStallWaits(-1) == 1);
    CoXlTestLwStallWaits(7);
    CHECK(CoXlTestLwStallStep(0, 3, 1, 1, 0, &after) == 1 && after == 3 && CoXlTestLwStallWaits(-1) == 8);
    g_case = "a path request in flight: two windows are waited, the third acts and uses a try";
    CoXlTestLwStallWaits(0);
    CHECK(CoXlTestLwStallStep(2, 1, 0, 1, 1, &after) == 2 && after == 1 && CoXlTestLwStallWaits(-1) == 1);
    CHECK(CoXlTestLwStallStep(2, 1, 0, 1, 1, &after) == 2 && after == 1 && CoXlTestLwStallWaits(-1) == 2);
    CHECK(CoXlTestLwStallStep(2, 1, 0, 1, 1, &after) == 4 && after == 2 && CoXlTestLwStallWaits(-1) == 0);
    g_case = "a step that acts starts the waits again";
    CoXlTestLwStallWaits(1);
    CHECK(CoXlTestLwStallStep(0, 0, 0, 0, 0, &after) == 3 && CoXlTestLwStallWaits(-1) == 0);
    g_case = "the first try sends the goal again, whatever the snap says";
    CHECK(CoXlTestLwStallStep(0, 0, 0, 0, 0, &after) == 3 && after == 1);
    CHECK(CoXlTestLwStallStep(2, 0, 0, 0, 1, &after) == 3 && after == 1);
    g_case = "later tries: an event squad's goal moves onto the leader's layer when that is off it, else a new goal";
    CHECK(CoXlTestLwStallStep(2, 1, 0, 0, 1, &after) == 4 && after == 2);
    CHECK(CoXlTestLwStallStep(1, 2, 0, 0, 1, &after) == 4 && after == 3);
    CHECK(CoXlTestLwStallStep(2, 1, 0, 0, 0, &after) == 5 && after == 2);
    g_case = "a patrol never takes the moved goal (its node is reached at the node): a new goal";
    CHECK(CoXlTestLwStallStep(0, 1, 0, 0, 1, &after) == 5 && after == 2);
    g_case = "the tries stop counting at 255";
    CHECK(CoXlTestLwStallStep(0, 255, 0, 0, 0, &after) == 5 && after == 255);
}

/* read[] = {ok, action, busy, held, nav record, leader-layer hit}; calls[] = goal sends, path requests, resumes. */
static void StallApply(void)
{
    int calls[3]; float to[2], goalNow = 0;
    const int none[6] = {0, 0, 0, 1, 1, 1}, plain[6] = {1, 0, 0, 0, 1, 1}, noNav[6] = {1, 0, 0, 0, 0, 1}, held[6] = {1, 0, 0, 1, 1, 0};
    const int action[6] = {1, 1, 0, 1, 1, 1}, busy[6] = {1, 0, 1, 1, 1, 1}, miss[6] = {1, 0, 0, 0, 1, 0};
    g_case = "a squad without its records is left alone";
    CoXlTestLwStallWaits(0);
    CoXlTestLwStallApply(2, 0, none, 0, 500, 500, calls, to, &goalNow);
    CHECK(calls[0] == 0 && calls[1] == 0 && calls[2] == 0 && CoXlTestLwStallTries(-1) == 0);
    g_case = "the first try: the job position and a path request to the goal itself, not to the snap";
    uint32_t resent = CoXlTestLwStallCount(0);
    CoXlTestLwStallApply(2, 0, plain, 0, 500, 480, calls, to, &goalNow);
    CHECK(calls[0] == 1 && calls[1] == 1 && calls[2] == 0 && to[0] == 500 && to[1] == 500 && goalNow == 500);
    CHECK(CoXlTestLwStallCount(0) == resent + 1 && CoXlTestLwStallTries(-1) == 1 && Count("stalled (try 1, 0 waits): its goal sent again") == 1);
    g_case = "without a nav record no path is asked for (0x5B36C0 tests for none)";
    CoXlTestLwStallApply(2, 0, noNav, 0, 500, 480, calls, to, &goalNow);
    CHECK(calls[0] == 1 && calls[1] == 0);
    g_case = "a held squad is resumed before its goal is sent again";
    uint32_t resumed = CoXlTestLwStallCount(3);
    CoXlTestLwStallApply(0, 0, held, 0, 500, 500, calls, to, &goalNow);
    CHECK(calls[2] == 1 && calls[0] == 1 && calls[1] == 1 && CoXlTestLwStallCount(3) == resumed + 1 && Count("it was held (AiSquad +0x18): resumed") == 1);
    g_case = "a brain action or a request in flight: no call, not even the resume; each writes its line and is counted";
    uint32_t acts = CoXlTestLwStallCount(4), waits = CoXlTestLwStallCount(5);
    CoXlTestLwStallApply(0, 0, action, 0, 500, 500, calls, to, &goalNow);
    CHECK(calls[0] == 0 && calls[1] == 0 && calls[2] == 0 && CoXlTestLwStallCount(4) == acts + 1 && Count("a brain action drives the leader") == 1);
    CoXlTestLwStallWaits(0);
    CoXlTestLwStallApply(0, 0, busy, 0, 500, 500, calls, to, &goalNow);
    CHECK(calls[0] == 0 && calls[1] == 0 && calls[2] == 0 && CoXlTestLwStallCount(5) == waits + 1 && Count("its path request is in flight or cooling down") == 1);
    g_case = "a later try of an event squad whose goal is 5 m or more off the leader's layer: the goal moves there";
    uint32_t snapped = CoXlTestLwStallCount(1);
    CoXlTestLwStallApply(2, 1, plain, 0, 500, 494, calls, to, &goalNow);
    CHECK(calls[0] == 1 && calls[1] == 1 && to[0] == 494 && to[1] == 494 && goalNow == 494 && CoXlTestLwStallCount(1) == snapped + 1);
    g_case = "under 5 m off, or with no face on the leader's layer: a new goal, for an event squad its intent's end";
    uint32_t fresh = CoXlTestLwStallCount(2);
    CoXlTestLwHoldAge(3, 0);
    CoXlTestLwStallApply(2, 1, plain, 0, 500, 496, calls, to, &goalNow);
    CHECK(CoXlTestLwStallCount(2) == fresh + 1 && CoXlTestLwStallCount(1) == snapped + 1 && CoXlTestLwIntent() == 0 && calls[0] == 0);
    CHECK(CoXlTestLwStallTries(-1) == 2); /* the intent's end keeps the tries */
    CoXlTestLwStallApply(1, 1, miss, 0, 500, 300, calls, to, &goalNow);
    CHECK(CoXlTestLwStallCount(2) == fresh + 2 && CoXlTestLwIntent() == 0 && Count("no way to its point online: the intent ends") == 2);
    g_case = "an intent that ends leaves no goal behind when no patrol goal could be set";
    CHECK(CoXlTestLwIntentEndGoal() == 0);
}

static void Goals(void)
{
    g_case = "goals wait for the job's state entry: state 10, or 2 s when the state stays another or unread";
    CHECK(CoXlTestLwJobSettled(10, 0) == 1 && CoXlTestLwJobSettled(9, 0) == 0 && CoXlTestLwJobSettled(11, 1999) == 0);
    CHECK(CoXlTestLwJobSettled(-1, 1999) == 0 && CoXlTestLwJobSettled(-1, 2000) == 1 && CoXlTestLwJobSettled(4, 2000) == 1);
    g_case = "a patrol's node is reached within 15 m of it, or of the goal sent for it";
    CHECK(CoXlTestLwNodeReached(0, 500, 480, 0, 486) == 1 && CoXlTestLwNodeReached(0, 500, 480, 0, 514) == 1);
    CHECK(CoXlTestLwNodeReached(0, 500, 480, 0, 484) == 1 && CoXlTestLwNodeReached(0, 500, 480, 0, 466) == 1); /* by the goal only */
    CHECK(CoXlTestLwNodeReached(0, 500, 480, 0, 464) == 0 && CoXlTestLwNodeReached(0, 500, 480, 0, 516) == 0);
    g_case = "a goal sent for another node, for none, or none at all does not reach this node";
    CHECK(CoXlTestLwNodeReached(0, 500, 480, 3, 470) == 0 && CoXlTestLwNodeReached(0, 500, 480, -1, 470) == 0);
    CHECK(CoXlTestLwNodeReached(0, 500, 0, -2, 5) == 0 && CoXlTestLwNodeReached(0, 500, 0, -2, 490) == 1);
    g_case = "an event squad reaches no patrol node; a goal sent names no node until the patrol send sets it";
    CHECK(CoXlTestLwNodeReached(2, 500, 480, 0, 500) == 0 && CoXlTestLwNodeReached(1, 500, 480, 0, 480) == 0);
    CHECK(CoXlTestLwGoalNode() == -1);
}
static void Stall(void)
{
    const float goal[2] = {500, 0}, at[2] = {0, 0}, byGoal[2] = {490, 0};
    float walk[2] = {0, 0};
    CoXlTestLwStallLines(0);
    g_case = "an online squad still for 30 s with a goal 500 m away: one line, counted";
    CoXlTestLwStall(1, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 30000) == 0 && Count("made no way for 30 s") == 0);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 1 && Count("made no way for 30 s") == 1);
    CHECK(Count("made no way for 30 s (calm), 500 m from its goal (500, 0, 0.0)") == 1);
    g_case = "still for another 30 s: counted again, no second line in the same stretch";
    CHECK(CoXlTestLwStall(0, at, goal, 61001) == 2 && Count("made no way for 30 s") == 1);
    g_case = "a walking squad (15 m per 10 s toward its goal) is never stalled";
    CoXlTestLwStall(1, walk, goal, 1000);
    for (int k = 1; k <= 12; k++) { walk[0] = 15.0f * k; CoXlTestLwStall(0, walk, goal, 1000 + 10000u * k); }
    CHECK(CoXlTestLwStall(0, walk, goal, 1000 + 10000u * 12) == 0 && Count("made no way for 30 s") == 1);
    g_case = "19 m of way in 30 s is a stall, 20 m is not";
    const float short19[2] = {19, 0}, way20[2] = {20, 0};
    CoXlTestLwStall(1, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, short19, goal, 31001) == 1);
    CoXlTestLwStall(1, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, way20, goal, 31001) == 0);
    g_case = "a detour: no nearer its goal, but 20 m less of its path left, is way made; 19 m is not";
    const float aside[2] = {0, 40};
    CoXlTestLwStallPath(300); CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000);
    CoXlTestLwStallPath(280);
    CHECK(CoXlTestLwStall(0, aside, goal, 31001) == 0);
    CoXlTestLwStallPath(300); CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000);
    CoXlTestLwStallPath(281);
    CHECK(CoXlTestLwStall(0, aside, goal, 31001) == 1);
    g_case = "a path that grew, or is unknown at either end of the window, is no way made";
    CoXlTestLwStallPath(300); CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000);
    CoXlTestLwStallPath(340);
    CHECK(CoXlTestLwStall(0, aside, goal, 31001) == 1);
    CoXlTestLwStallPath(-1); CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000);
    CoXlTestLwStallPath(100);
    CHECK(CoXlTestLwStall(0, aside, goal, 31001) == 1);
    CoXlTestLwStallPath(300); CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000);
    CoXlTestLwStallPath(-1);
    CHECK(CoXlTestLwStall(0, aside, goal, 31001) == 1);
    g_case = "a squad milling 8 m to and fro across its way for 30 s moves, but is stalled";
    const float mill[3][2] = {{0, 0}, {0, 8}, {0, -8}};
    const int millLines = Count("made no way for 30 s");
    CoXlTestLwStall(1, mill[0], goal, 1000);
    for (int k = 1; k <= 30; k++) CoXlTestLwStall(0, mill[k % 3], goal, 1000 + 1000u * k);
    CHECK(CoXlTestLwStall(0, mill[1], goal, 31001) == 1 && Count("made no way for 30 s") == millLines + 1);
    g_case = "a stalled squad's tries start again once it makes 20 m of way, and are kept while it mills";
    CoXlTestLwStall(1, mill[0], goal, 1000);
    CoXlTestLwStall(0, mill[0], goal, 1000);
    CoXlTestLwStallTries(2); CoXlTestLwStallSet(0, 500);
    CoXlTestLwStall(0, mill[1], goal, 11000);
    CHECK(CoXlTestLwStallTries(-1) == 2);
    const float way[2] = {21, 0}, more[2] = {59, 0}, clear[2] = {81, 0};
    CoXlTestLwStall(0, way, goal, 12000); /* 20 m of way is no stall, but a milling squad makes that too */
    CHECK(CoXlTestLwStallTries(-1) == 2);
    CoXlTestLwStall(0, more, goal, 13000);
    CHECK(CoXlTestLwStallTries(-1) == 2);
    CoXlTestLwStall(0, clear, goal, 14000); /* 60 m nearer than at its last recovery */
    CHECK(CoXlTestLwStallTries(-1) == 0);
    g_case = "another goal starts the tries again";
    CoXlTestLwStallTries(3);
    CHECK(CoXlTestLwGoalNode() == -1 && CoXlTestLwStallTries(-1) == 0);
    g_case = "a squad noted calm gets its own line when it later stands rushing";
    CoXlTestLwStall(1, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    int lines = Count("made no way for 30 s");
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 1 && Count("made no way for 30 s") == lines + 1);
    CoXlTestLwStallSet(1, 0);
    CoXlTestLwStall(0, at, goal, 61002);
    CHECK(Count("made no way for 30 s") == lines + 2);
    CoXlTestLwStall(0, at, goal, 91003);
    CHECK(Count("made no way for 30 s") == lines + 2);
    g_case = "an event squad at its point (stage 1) stands there: never stalled; on its way (stage 0) it is";
    CoXlTestLwStall(6, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 0 && CoXlTestLwStall(0, at, goal, 61002) == 0 && CoXlTestLwStallOther() == 0);
    CoXlTestLwStall(7, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 1);
    g_case = "within 15 m of its goal it has arrived, not stalled";
    CoXlTestLwStall(1, byGoal, goal, 1000);
    CoXlTestLwStall(0, byGoal, goal, 1000);
    lines = Count("made no way for 30 s");
    CHECK(CoXlTestLwStall(0, byGoal, goal, 40000) == 0 && Count("made no way for 30 s") == lines);
    CoXlTestLwStallTries(3);
    CHECK(CoXlTestLwStall(0, byGoal, goal, 41000) == 0 && CoXlTestLwStallTries(-1) == 0); /* there: its tries start again */
    g_case = "goal-less and rushing squads standing still are noted apart, not counted as calm stalls";
    CoXlTestLwStall(2, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 32000) == 0 && CoXlTestLwStallOther() == 1 && Count("made no way for 30 s (calm, no goal sent), no goal sent") == 1);
    CoXlTestLwStall(3, at, goal, 1000);
    lines = Count("made no way for 30 s (rushing)");
    CHECK(CoXlTestLwStall(0, at, goal, 32000) == 0 && CoXlTestLwStallOther() == 1 && Count("made no way for 30 s (rushing)") == lines + 1);
    g_case = "a patrol left goal-less by its last send is asked for a goal again once per 30 s still; other goal-less squads are not";
    CHECK(CoXlTestLwStallNoGoal() == 0);
    CoXlTestLwStall(4, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 30000) == 0 && CoXlTestLwStallNoGoal() == 0);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 0 && CoXlTestLwStallNoGoal() == 1);
    CHECK(CoXlTestLwStall(0, at, goal, 61002) == 0 && CoXlTestLwStallNoGoal() == 2);
    const float wander[2] = {40, 0};
    const uint32_t other = CoXlTestLwStallOther();
    CHECK(CoXlTestLwStall(0, wander, goal, 80000) == 0 && CoXlTestLwStallNoGoal() == 2);
    CHECK(CoXlTestLwStall(0, wander, goal, 91003) == 0 && CoXlTestLwStallNoGoal() == 3 && CoXlTestLwStallOther() == other); /* wandering goal-less: asked too, no line */
    CoXlTestLwStall(5, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 0 && CoXlTestLwStallNoGoal() == 3); /* an event squad's goal is its intent's */
    CoXlTestLwStall(8, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 0 && CoXlTestLwStallNoGoal() == 3); /* on its way too */
    g_case = "at most 20 such lines a mission, apart from the calm ones' 40";
    for (int k = 0; k < 30; k++) { CoXlTestLwStall(2, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000); CoXlTestLwStall(0, at, goal, 32000); }
    CHECK(Count("made no way for 30 s (calm, no goal sent)") + Count("made no way for 30 s (rushing)") == 20);
    g_case = "at most 40 lines a mission";
    for (int k = 0; k < 60; k++) { CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000); CoXlTestLwStall(0, at, goal, 32000); }
    CHECK(Count("made no way for 30 s (calm)") == 40);
    CHECK(Count("(the last such line this mission)") == 3); /* the held-why's, the other stalls' and this one */
}

/* A squad at its route's end gets a new destination on an extra large disc: 200 m nodes over 7782 m (the walking grid
 * of a real mission), from the middle and from 400 m inside the rim; and on a vanilla disc. */
static void Plans(void)
{
    int beyond = 0, turned = 0;
    g_case = "a new destination on the extra large disc";
    CHECK(CoXlTestLwPlans(7782.0f, 200.0f, 3000.0f, 3000.0f, 300, &beyond, &turned) == 0);
    CHECK(beyond == 0);         /* every destination within the roam range of where the squad stood */
    CHECK(turned < 300 / 10);  /* an about-turn only when the tries run low */
    g_case = "a new destination near the disc's rim";
    CHECK(CoXlTestLwPlans(7782.0f, 200.0f, 5200.0f, 5200.0f, 300, &beyond, &turned) == 0);
    CHECK(beyond == 0);
    g_case = "a new destination on a vanilla disc";
    CHECK(CoXlTestLwPlans(1000.0f, 100.0f, 0.0f, 0.0f, 300, &beyond, &turned) == 0);
    /* three nodes in an L: from either end the only way starts back where it came from, and it is taken */
    g_case = "a new destination from a dead end";
    CHECK(CoXlTestLwPlans(1000.0f, 900.0f, 800.0f, -100.0f, 300, &beyond, &turned) < 300 / 20);
    CHECK(turned > 300 / 4);
}

/* The home rule on the extra large disc: 7 of 10 plans keep to the home region when it has nodes in range (about a
 * quarter of them here), and a squad far from its home still gets a route that does not start back the way it came. */
static void HomePlans(void)
{
    const float at[2] = {3018.0f, 3018.0f}, homeNear[3] = {3400.0f, 3000.0f, 600.0f}, homeAway[3] = {-3000.0f, -3000.0f, 600.0f};
    int inHome = 0, turned = 0;
    g_fake.log[0] = 0;
    g_case = "most destinations in the home region";
    CHECK(CoXlTestLwPlansAt(7782.0f, 200.0f, at, homeNear, 300, 40, &inHome, &turned) == 0);
    CHECK(inHome > 300 * 6 / 10); /* without the rule: the region's share of the nodes in range, about 75 */
    CHECK(inHome < 300 * 9 / 10); /* 3 of 10 plans draw freely */
    CHECK(turned < 300 / 20);
    g_case = "a new route's line names the home rule";
    CHECK(Count("squad 0 takes a new route: to node ") == 40 && Count(" nodes; home rule kept; its goal is sent") + Count(" nodes; no home rule (the free draw); its goal is sent") == 40);
    CHECK(Count("home rule kept") > 15 && Count("no home rule (the free draw)") > 3);
    g_case = "a home region out of range";
    CHECK(CoXlTestLwPlansAt(7782.0f, 200.0f, at, homeAway, 300, 20, &inHome, &turned) == 0);
    CHECK(inHome == 0);
    CHECK(turned < 300 / 20); /* the tries the home rule used up do not end the about-turn rule */
    CHECK(Count("home rule gave way (no home node in its tries)") > 5);
    g_case = "no home region";
    CHECK(CoXlTestLwPlansAt(7782.0f, 200.0f, at, NULL, 300, 5, &inHome, &turned) == 0);
    CHECK(Count("no home rule (the squad has no home region)") == 5);
}

/* No node in range (the squad 1500 m outside the grid): every plan ends without a route after its capped draws. Run on
 * a thread with a time limit, so a planner that draws without end fails here instead of hanging the suite. */
static struct { int failed, inHome, turned; } g_far;
static DWORD WINAPI FarPlans(LPVOID unused)
{
    const float at[2] = {0.0f, 3500.0f};
    (void)unused;
    g_far.failed = CoXlTestLwPlansAt(2000.0f, 400.0f, at, NULL, 200, 0, &g_far.inHome, &g_far.turned);
    return 0;
}
static void PlanCap(void)
{
    g_case = "no node in range: the draws end";
    HANDLE thread = CreateThread(NULL, 0, FarPlans, NULL, 0, NULL);
    int ended = thread != NULL && WaitForSingleObject(thread, 20000) == WAIT_OBJECT_0;
    CHECK(ended);
    if (thread != NULL && !ended) TerminateThread(thread, 1);
    if (thread != NULL) CloseHandle(thread);
    CHECK(ended && g_far.failed == 200);
}
int main(void)
{
    g_case = "set-up";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    BandEdge();
    Refusals();
    Gates();
    Lost();
    Slots();
    Budget();
    HeldWhy();
    HoldPoints();
    Goals();
    StallApply();
    StallStep();
    Stall();
    Plans();
    HomePlans();
    PlanCap();
    if (g_failed) { printf("lwreinforce: %d checks failed\n", g_failed); return 1; }
    printf("lwreinforce: all cases passed\n");
    return 0;
}
