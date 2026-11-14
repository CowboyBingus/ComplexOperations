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
void CoXlTestLwFightTarget(int f, uint32_t target, const float* at);
void CoXlTestLwReinforceOn(const float* fight, uint32_t sentAgo, int intent);
int CoXlTestLwReinforceTick(const float* at, int engaged, uint32_t later, int* stage, float* target, int* moves);
int CoXlTestLwIntentLate(uint32_t later);
int CoXlTestLwIntentOffline(void);
int CoXlTestLwCluster(const float* pts, const uint32_t* levels, int n, float* pos, float* hot);
int CoXlTestLwFightTake(float x, float y, int n, int hot, int call, uint32_t target, uint64_t now, int read, float* pos, int* engaged, uint64_t* seen);
int CoXlTestLwTeleportUnder(float hit, float plan, float drawn);
void CoXlTestLwFight(int f, float x, float y, uint32_t age, uint32_t seenAgo, int engaged, uint64_t now);
void CoXlTestLwFightsClear(void);
int CoXlTestLwFightHandOver(int f);
int CoXlTestLwFightOfSquad(int i);
void CoXlTestLwHeard(int members);
void CoXlTestLwGoalStub(int on, const float* off, float r);
int CoXlTestLwGoals(float* to);
void CoXlTestLwOfflineAt(uint32_t later);
void CoXlTestLwSendSetup(int online, const float* sq, const float* fight);
int CoXlTestLwSend(float* target, int* moves, int* nextPoint, int* sentAt);
uint32_t CoXlTestLwFightTargetOf(int f);
int CoXlTestLwNoiseGate(uint32_t avatar, uint32_t unit, uint32_t source, uint32_t sent, uint32_t ago, float moved, int fixed, float away);
int CoXlTestLwPlayers(const float* pts, int n);
void CoXlTestLwGoalWhy(int why);
int CoXlTestLwGoalsAsked(void);
int CoXlTestLwGoTo(int state, uint32_t squad, float x, float y);
int CoXlTestLwReinforceState(void);
int CoXlTestLwFightTakeWhom(float x, float y, int n, const uint32_t* ids, const float* tpts, int tn, uint64_t now, float* pos);
uint32_t CoXlTestLwFightFar(void);
void CoXlTestLwFightPoint(int f, float x, float y);
void CoXlTestLwAvatars(const uint32_t* ids, int n);
const char* CoXlTestLwKillerKind(uint64_t res);
int CoXlTestLwFightOver(int f, uint64_t now);
void CoXlTestLwNotSentLines(uint32_t n);
void CoXlTestLwNoiseWorld(const float* pl, int np, const float* sq, int n);
void CoXlTestLwNoiseSquad(int i, int what, float x, float y);
int CoXlTestLwNoise(uint32_t source, float x, float y, uint64_t now, uint32_t of);
uint32_t CoXlTestLwNoiseCount(int what);
void CoXlTestLwFightFought(int f, uint32_t ago);

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
        g_case = "sent 240 s ago, the wait itself seconds old: it waits on (the offline walk is not the wait)";
        CoXlTestLwHoldAge(3, 0);
        if (!end)
        {
            CoXlTestLwHoldAge(0, 240000);
            CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
            CHECK(CoXlTestLwIntent() == 2 && held == 1 && Count(why) == ended && CoXlTestLwIntentLate(0) == 0);
        }
        g_case = "the wait ends with its fight, and after 240 s of waiting";
        CoXlTestLwHoldAge(end ? 2 : 4, 241000);
        if (!end) CHECK(CoXlTestLwIntentLate(0) == 2);
        CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
        CHECK(CoXlTestLwIntent() == 0 && Count(why) == ended + 1);
    }
    g_case = "back at the band's edge 241 s after its first wait there: the end says since its first wait, not that it waited 240 s";
    CoXlTestLwReinforce(far0, sq, point);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CoXlTestLwPlayerAt(in);
    for (int k = 0; k < 5; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(held == 1);
    CoXlTestLwHoldAge(3, 0);
    CoXlTestLwHoldAge(0, 300000);
    CoXlTestLwSlotsChecked(0, 241000); /* not waiting now (it was online), its first wait 241 s ago */
    const float inA[2] = {pos[0] - 140.0f, pos[1]};
    CoXlTestLwPlayerAt(inA);
    const int again = Count("240 s since its first wait at the spawn band"), whole = Count("240 s, waiting at the spawn band");
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(CoXlTestLwIntent() == 0 && Count("240 s since its first wait at the spawn band") == again + 1 && Count("240 s, waiting at the spawn band") == whole);
    g_case = "waiting a second time when the first wait's 240 s pass: the same reason";
    CoXlTestLwReinforce(far0, sq, point);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CoXlTestLwPlayerAt(in);
    for (int k = 0; k < 5; k++) CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CoXlTestLwHoldAge(3, 0);
    CoXlTestLwHoldAge(0, 300000);
    CoXlTestLwSlotsChecked(0, 10000);
    const float inB[2] = {pos[0] - 140.0f, pos[1]};
    CoXlTestLwPlayerAt(inB);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(held == 1 && CoXlTestLwIntent() == 2);
    CoXlTestLwHoldAge(4, 231000);
    const float inC[2] = {pos[0] - 140.0f, pos[1]};
    CoXlTestLwPlayerAt(inC);
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(CoXlTestLwIntent() == 0 && Count("240 s since its first wait at the spawn band") == again + 2 && Count("240 s, waiting at the spawn band") == whole);
    g_case = "a wait that began before this intent is not this intent's";
    CoXlTestLwReinforce(far0, sq, point);
    CoXlTestLwHoldAge(5, 300000);
    CHECK((CoXlTestLwIntentLate(0) & 2) == 0);
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

/* The game's huge explosion entity as a killer is named; any other entity type is not. */
static void KillerKind(void)
{
    g_case = "resource 4d4793e8c8108cfe is a huge explosion with no living attacker; another resource has no name";
    CHECK(strcmp(CoXlTestLwKillerKind(0x4D4793E8C8108CFEull), "a huge explosion with no living attacker, its ") == 0);
    CHECK(CoXlTestLwKillerKind(0x4D4793E8C8108CFFull)[0] == 0 && CoXlTestLwKillerKind(0)[0] == 0);
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
/* The time to reach the point counts online. Squad 5050 of run 5: sent from 588 m, online 252 s later 232 m out. */
static void OnlineLimit(void)
{
    const float fight[2] = {1000, 0}, at[2] = {768, 0};
    int stage, moves; float t[2];
    g_case = "sent 252 s ago from offline, its first online tick 232 m from the point: it keeps reinforcing";
    CoXlTestLwReinforceOn(fight, 252000, 2);
    const int ends = Count("not there in 240 s");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && stage == 0);
    g_case = "239 s online it is still on its way; 241 s online it is not there in 240 s";
    CHECK(CoXlTestLwReinforceTick(at, 0, 239000, &stage, t, &moves) == 2 && Count("not there in 240 s") == ends);
    CHECK(CoXlTestLwReinforceTick(at, 0, 241000, &stage, t, &moves) == 0 && Count("not there in 240 s") == ends + 1);
    g_case = "sent online: 240 s from the send";
    CoXlTestLwReinforceOn(fight, 0, 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 239000, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 241000, &stage, t, &moves) == 0);
    g_case = "offline and online again on its way: its online time starts again";
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwIntentOffline() == 1 && CoXlTestLwIntent() == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 300000, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 539000, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 541000, &stage, t, &moves) == 0);
    g_case = "the limits by intent: 240 s reinforcing, 180 s investigating, from the first online tick";
    CoXlTestLwReinforceOn(fight, 252000, 2);
    CHECK(CoXlTestLwIntentLate(0) == 0 && CoXlTestLwIntentLate(239000) == 0 && CoXlTestLwIntentLate(241000) == 1);
    CoXlTestLwReinforceOn(fight, 252000, 1);
    CHECK(CoXlTestLwIntentLate(0) == 0 && CoXlTestLwIntentLate(179000) == 0 && CoXlTestLwIntentLate(181000) == 1);
}

/* A reinforcement goes to the fight's live target, and one that arrived with nobody engaged follows the fight. */
static void FightAim(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0};
    const float p100[2] = {1100, 0}, p120[2] = {1120, 0}, p122[2] = {1122, 0}, p145[2] = {1145, 0}, p400[2] = {1400, 0};
    int stage, moves; float t[2];
    g_case = "on its way, the fight's target alive 100 m from the fight's point: its point is where the target is";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1100.0f && moves == 0);
    g_case = "the target 45 m on within 10 s of the last new point: the point is kept; after 10 s it follows";
    CoXlTestLwFightTarget(0, 0x400001, p145);
    CHECK(CoXlTestLwReinforceTick(at, 0, 5000, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && t[0] == 1145.0f);
    g_case = "the target dead: the fight's point";
    CoXlTestLwFightTarget(0, 0x400001, NULL);
    CHECK(CoXlTestLwReinforceTick(at, 0, 22000, &stage, t, &moves) == 2 && t[0] == 1000.0f && stage == 0);
    g_case = "a target 20 m from its point: the point is kept; 22 m: the point follows";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves);
    CoXlTestLwFightTarget(0, 0x400001, p120);
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CoXlTestLwFightTarget(0, 0x400001, p122);
    CHECK(CoXlTestLwReinforceTick(at, 0, 12000, &stage, t, &moves) == 2 && t[0] == 1122.0f);
    g_case = "a member engaged on the way: it joins where it is, its point not moved";
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CHECK(CoXlTestLwReinforceTick(at, 1, 33000, &stage, t, &moves) == 2 && stage == 1 && t[0] == 1122.0f);
    (void)p400;

    g_case = "arrived with nobody engaged, the fight 39 m from its leader: it stays";
    const float p39[2] = {1039, 0}, p45[2] = {1045, 0};
    CoXlTestLwReinforceOn(fight, 300000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightTarget(0, 0x400001, p39);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 11000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    g_case = "the fight's target 45 m from its leader, members engaged: it stays";
    CoXlTestLwFightTarget(0, 0x400001, p45);
    CHECK(CoXlTestLwReinforceTick(fight, 2, 12000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    g_case = "nobody engaged: on its way again to where the target is, noted";
    const int notes = Count("it goes there (move 1 of 3)"), ends = Count("not there in 240 s");
    CHECK(CoXlTestLwReinforceTick(fight, 0, 13000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1045.0f);
    CHECK(Count("it goes there (move 1 of 3)") == notes + 1);
    g_case = "its 240 s to get there count from the move";
    CHECK(CoXlTestLwReinforceTick(fight, 0, 13000 + 239000, &stage, t, &moves) == 2 && Count("not there in 240 s") == ends);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 13000 + 241000, &stage, t, &moves) == 0 && Count("not there in 240 s") == ends + 1);

    g_case = "members engaged at its first point, then a move: its end still gives the most engaged at once";
    const int peaks = Count("at most 2 members engaged at once at the fight");
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves);
    CoXlTestLwReinforceTick(fight, 2, 1000, &stage, t, &moves);
    CoXlTestLwFightTarget(0, 0x400001, p45);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 2000, &stage, t, &moves) == 2 && stage == 0 && moves == 1);
    CHECK(CoXlTestLwReinforceTick(p45, 0, 3000, &stage, t, &moves) == 2 && stage == 1);
    CHECK(CoXlTestLwReinforceTick(p45, 0, 3000 + 181000, &stage, t, &moves) == 0 && Count("at most 2 members engaged at once at the fight") == peaks + 1);

    g_case = "a fight that keeps moving 50 m on: three moves an intent, then it stays at its point";
    float pos[2] = {1000, 0}; uint32_t later = 0;
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CoXlTestLwFightTarget(0, 0x400001, pos);
    for (int k = 0; k < 4; k++)
    {
        CHECK(CoXlTestLwReinforceTick(pos, 0, later, &stage, t, &moves) == 2 && stage == 1); /* at its point */
        float next[2] = {pos[0] + 50.0f, 0};
        CoXlTestLwFightTarget(0, 0x400001, next);
        later += 11000;
        CoXlTestLwReinforceTick(pos, 0, later, &stage, t, &moves);
        later += 11000;
        if (k < 3) { CHECK(stage == 0 && moves == k + 1 && t[0] == next[0]); pos[0] = next[0]; }
        else CHECK(stage == 1 && moves == 3 && t[0] == pos[0]);
    }
    CoXlTestLwFightTarget(0, 0, NULL);

    /* squads 6250 and 5806 of run 5: at a point nobody fights at, their fight handed to another 70 m away */
    g_case = "arrived, its fight ends beside another: it is that fight's squad and goes to it";
    const float other[2] = {1070, 0};
    CoXlTestLwReinforceOn(fight, 60000, 2);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFight(1, other[0], other[1], 60000, 0, 5, GetTickCount64());
    CHECK(CoXlTestLwFightHandOver(0) == 1 && CoXlTestLwFightOfSquad(0) == 1);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 1000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1070.0f && moves == 1);
    CHECK(CoXlTestLwReinforceTick(other, 0, 2000, &stage, t, &moves) == 2 && stage == 1); /* there */
    g_case = "on its way, its fight handed over: its point is the other fight's";
    CoXlTestLwReinforceOn(fight, 60000, 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1000.0f);
    CoXlTestLwFight(1, other[0], other[1], 60000, 0, 5, GetTickCount64());
    CHECK(CoXlTestLwFightHandOver(0) == 1);
    CHECK(CoXlTestLwReinforceTick(at, 0, 1000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1070.0f && moves == 0);
}

/* The fight's target on ground with no navmesh (a Helldiver on a rock): the fight's point, where enemies stand. */
static void AimOffNavmesh(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, rock[2] = {1100, 0}, nearBy[2] = {961, 0}, on[2] = {1131, 0};
    int stage, moves; float t[2], to[2];
    g_case = "on its way, no navmesh at the fight's target: its goal and its point are the fight's point, noted";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CoXlTestLwGoalStub(1, rock, 25.0f);
    const int notes = Count("no navmesh at its fight's target");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1000.0f);
    CHECK(CoXlTestLwGoals(to) == 1 && to[0] == 1000.0f && Count("no navmesh at its fight's target") == notes + 1);
    g_case = "the target stays there: no goal again, and the squad arrives within 40 m of the fight's point and stays";
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && stage == 0 && CoXlTestLwGoals(to) == 1);
    CHECK(CoXlTestLwReinforceTick(nearBy, 0, 12000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    CHECK(CoXlTestLwReinforceTick(nearBy, 0, 30000, &stage, t, &moves) == 2 && stage == 1 && moves == 0 && CoXlTestLwGoals(to) == 1);
    g_case = "the target 31 m on, on ground with navmesh: the squad goes to it";
    CoXlTestLwFightTarget(0, 0x400001, on);
    CHECK(CoXlTestLwReinforceTick(nearBy, 0, 41000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1131.0f);
    CHECK(CoXlTestLwGoals(to) == 2 && to[0] == 1131.0f);
    g_case = "no navmesh at the target nor at the fight's point: it ends";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CoXlTestLwGoalStub(1, rock, 200.0f);
    const int ends = Count("no navmesh at the point");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 0 && Count("no navmesh at the point") == ends + 1 && CoXlTestLwGoals(to) == 0);
    g_case = "its fight over before its first goal: it ends with the fight, no goal is tried";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwGoalStub(1, rock, 5000.0f);
    CoXlTestLwHoldAge(2, 0);
    const int over = Count("the fight ended before it got there");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 0 && Count("the fight ended before it got there") == over + 1 && Count("no navmesh at the point") == ends + 1);
    g_case = "an arrived squad's move to a target with no navmesh: its goal is the fight's point again, it is not left with a point it has no goal for";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(1, rock, 25.0f);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1 && CoXlTestLwGoals(to) == 1);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 11000, &stage, t, &moves) == 2 && moves == 1 && t[0] == 1000.0f && CoXlTestLwGoals(to) == 2 && to[0] == 1000.0f);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 22000, &stage, t, &moves) == 2 && stage == 1 && moves == 1 && CoXlTestLwGoals(to) == 2);
    g_case = "a goal after a new point on its way and after a move";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CoXlTestLwGoalStub(1, NULL, 0);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && CoXlTestLwGoals(to) == 1 && to[0] == 1100.0f);
    CoXlTestLwFightTarget(0, 0x400001, on);
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && CoXlTestLwGoals(to) == 2 && to[0] == 1131.0f);
    CHECK(CoXlTestLwReinforceTick(on, 0, 12000, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightTarget(0, 0x400001, fight);
    CHECK(CoXlTestLwReinforceTick(on, 0, 23000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && CoXlTestLwGoals(to) == 3 && to[0] == 1000.0f);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(0, NULL, 0);
}

/* The send (LwReinforce): where the squad is sent, what starts anew, and a squad with no navmesh to go to. */
static void Send(void)
{
    const float sq[2] = {900, 0}, fight[2] = {1000, 0}, p[2] = {1060, 0}, farOff[2] = {1200, 0};
    float t[2], to[2]; int moves, next, sentAt;
    g_case = "an offline squad sent: its point is the fight's target where it is; its moves and its clocks start anew; 10 s to its next new point";
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 2 && t[0] == 1060.0f && moves == 0 && next > 9000 && next <= 11000 && sentAt == 1);
    g_case = "the target 200 m from the fight's point: the squad is sent to the point";
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, farOff);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 2 && t[0] == 1000.0f);
    g_case = "an online squad sent, no navmesh at the target: its goal is the fight's point";
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 25.0f);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 2 && t[0] == 1000.0f && CoXlTestLwGoals(to) == 1 && to[0] == 1000.0f && moves == 0x100);
    g_case = "none at the point either: it stays the patrol it was, noted; it rests, and the fight waits before it sends again";
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 200.0f);
    const int notes = Count("is not sent: no navmesh");
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no navmesh") == notes + 1 && sentAt == 1 && moves == (3 | 0x100) && t[0] == 900.0f);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no navmesh") == notes + 1);
    g_case = "the mission's 24th such line says it is the last; the 25th squad that is not sent has none, and still rests";
    const int lasts = Count("at its point (1000, 0) (the last such line this mission)");
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 200.0f);
    CoXlTestLwNotSentLines(22);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no navmesh") == notes + 2 && Count("at its point (1000, 0) (the last such line this mission)") == lasts);
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 200.0f);
    CoXlTestLwNotSentLines(23);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no navmesh") == notes + 3 && Count("at its point (1000, 0) (the last such line this mission)") == lasts + 1);
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 200.0f);
    CoXlTestLwNotSentLines(24);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no navmesh") == notes + 3 && sentAt == 1 && t[0] == 900.0f);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(0, NULL, 0);
}

/* One reinforcement's time: 180 s at the fight and 600 s online, each summed over its moves and its online stretches. */
static void OneClock(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, p45[2] = {1045, 0};
    int stage, moves; float t[2];
    g_case = "100 s at its first point, a move, then 79 s at the next: it stays; 81 s: 180 s at the fight";
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightTarget(0, 0x400001, p45);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 100000, &stage, t, &moves) == 2 && stage == 0 && moves == 1);
    CHECK(CoXlTestLwReinforceTick(p45, 0, 101000, &stage, t, &moves) == 2 && stage == 1);
    const int stays = Count("180 s at the fight");
    CHECK(CoXlTestLwReinforceTick(p45, 0, 101000 + 79000, &stage, t, &moves) == 2 && Count("180 s at the fight") == stays);
    CHECK(CoXlTestLwReinforceTick(p45, 0, 101000 + 81000, &stage, t, &moves) == 0 && Count("180 s at the fight") == stays + 1);
    CoXlTestLwFightTarget(0, 0, NULL);
    g_case = "on its way: 200 s online, offline, 239 s online, offline, then 160 s: it goes on; 162 s: 600 s online";
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2);
    CoXlTestLwOfflineAt(200000);
    CHECK(CoXlTestLwReinforceTick(at, 0, 500000, &stage, t, &moves) == 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 500000 + 239000, &stage, t, &moves) == 2);
    CoXlTestLwOfflineAt(500000 + 239000);
    CHECK(CoXlTestLwReinforceTick(at, 0, 800000, &stage, t, &moves) == 2);
    const int caps = Count("600 s online");
    CHECK(CoXlTestLwReinforceTick(at, 0, 800000 + 160000, &stage, t, &moves) == 2 && Count("600 s online") == caps);
    CHECK(CoXlTestLwReinforceTick(at, 0, 800000 + 162000, &stage, t, &moves) == 0 && Count("600 s online") == caps + 1);
    g_case = "at the fight: 460 s online on its way, then 139 s there: it stays; 141 s: 600 s online";
    CoXlTestLwReinforceOn(fight, 1000, 2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2);
    CoXlTestLwOfflineAt(230000);
    CHECK(CoXlTestLwReinforceTick(at, 0, 300000, &stage, t, &moves) == 2);
    CoXlTestLwOfflineAt(530000);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 600000, &stage, t, &moves) == 2 && stage == 1);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 600000 + 139000, &stage, t, &moves) == 2 && Count("600 s online") == caps + 1);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 600000 + 141000, &stage, t, &moves) == 0 && Count("600 s online") == caps + 2);
}

/* Arrival is against where the fight is now; members who heard something are not given a new point; the hand-over. */
static void AimRules(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, p100[2] = {1100, 0}, p120[2] = {1120, 0}, p45[2] = {1045, 0}, p150[2] = {1150, 0}, p151[2] = {1151, 0};
    const float m41[2] = {1079, 0}, m39[2] = {1081, 0}, other[2] = {1070, 0};
    int stage, moves; float t[2];
    g_case = "its point 20 m behind the fight's target: 41 m from the target it has not arrived; 39 m it has, and it stays";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves);
    CoXlTestLwFightTarget(0, 0x400001, p120);
    CHECK(CoXlTestLwReinforceTick(m41, 0, 11000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1100.0f);
    CHECK(CoXlTestLwReinforceTick(m39, 0, 12000, &stage, t, &moves) == 2 && stage == 1);
    CHECK(CoXlTestLwReinforceTick(m39, 0, 23000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    g_case = "the fight's target 150 m from its point is where its squads go; 151 m: the point, and no tell of that target";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p150);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1150.0f);
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p151);
    const int tells = Count("told of its target");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1000.0f);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 1000, &stage, t, &moves) == 2 && stage == 1 && moves == 0 && Count("told of its target") == tells);
    CoXlTestLwFightTarget(0, 0x400001, p45);
    CHECK(CoXlTestLwReinforceTick(fight, 2, 2000, &stage, t, &moves) == 2 && Count("told of its target") == tells);
    CoXlTestLwFightTarget(0, 0x400001, fight);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 3000, &stage, t, &moves) == 2 && Count("told of its target") == tells + 1);

    g_case = "a member at alert 1 or 2 on its way: no new point; calm again: the new point";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwHeard(1);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1000.0f);
    CoXlTestLwHeard(0);
    CHECK(CoXlTestLwReinforceTick(at, 0, 1000, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    g_case = "arrived, a member at alert 2, the fight 45 m off: no move before a tell nor 16 s after one; told 1 s ago, it moves";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightTarget(0, 0x400001, p45);
    CoXlTestLwHeard(1);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 1000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);  /* tell 1 follows in this tick */
    CHECK(CoXlTestLwReinforceTick(fight, 0, 17000, &stage, t, &moves) == 2 && stage == 1 && moves == 0); /* tell 2 follows */
    CHECK(CoXlTestLwReinforceTick(fight, 0, 18000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1045.0f);
    CoXlTestLwHeard(0);

    g_case = "on its way, a new point 2 s ago, its fight handed over: the other fight's place on the next tick";
    CoXlTestLwReinforceOn(fight, 60000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwFight(1, other[0], other[1], 60000, 0, 5, GetTickCount64());
    CHECK(CoXlTestLwFightHandOver(0) == 1);
    CHECK(CoXlTestLwReinforceTick(at, 0, 2000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1070.0f);
}

/* A fight is where a player, or whom its units fight, is within 150 m: units that walk off take no fight along. */
static void FightNear(void)
{
    float pos[2] = {0, 0}; int eng; uint64_t seen;
    const uint64_t now = GetTickCount64() + 5000000;
    const float pl[2] = {0, 0};
    CoXlTestLwPlayerAt(pl);
    g_case = "units engaged 151 m from the only player, fighting nobody known, start no fight, noted; 150 m: one starts";
    CoXlTestLwFightsClear();
    const int lines = Count("no fight is placed there"); const uint32_t farN = CoXlTestLwFightFar();
    CHECK(CoXlTestLwFightTake(151, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == -1 && seen == 0);
    CHECK(Count("no fight is placed there") == lines + 1 && CoXlTestLwFightFar() == farN + 1);
    CHECK(CoXlTestLwFightTake(150, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0 && pos[0] == 150.0f);
    g_case = "a call for reinforcements 151 m from every player starts none either";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(151, 0, 8, 0, 1, 0, now, 0, pos, &eng, &seen) == -1);
    g_case = "the line comes once in 30 s and counts the clusters since";
    CHECK(Count("no fight is placed there") == lines + 2);
    CHECK(CoXlTestLwFightTake(151, 0, 6, 6, 0, 0, now + 29000, 0, pos, &eng, &seen) == -1 && Count("no fight is placed there") == lines + 2);
    CHECK(CoXlTestLwFightTake(151, 0, 6, 6, 0, 0, now + 31000, 0, pos, &eng, &seen) == -1 && Count("no fight is placed there") == lines + 3);
    CHECK(Count("no fight is placed there; its units name nobody they fight (2 such clusters since the last such line)") == 1);

    g_case = "a fight's point follows its units while a player is near the new place; over 200 m from every player it stays and the fight is not seen";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(100, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwFightTake(140, 0, 6, 6, 0, 0, now + 1000, 0, pos, &eng, &seen) == 0 && pos[0] == 140.0f && seen == now + 1000);
    CHECK(CoXlTestLwFightTake(190, 0, 6, 6, 0, 0, now + 2000, 0, pos, &eng, &seen) == 0 && pos[0] == 190.0f && seen == now + 2000);
    CHECK(CoXlTestLwFightTake(245, 0, 6, 6, 0, 0, now + 3000, 0, pos, &eng, &seen) == -1 && pos[0] == 190.0f && seen == now + 2000);
    CHECK(Count("fight 0's point stays at (190, 0)") == 1);
    g_case = "units that lost sight of their target (level 7) over 200 m from every player keep no fight seen";
    CHECK(CoXlTestLwFightTake(205, 0, 4, 0, 0, 0, now + 4000, 0, pos, &eng, &seen) == -1 && seen == now + 2000);
    CHECK(CoXlTestLwFightTake(195, 0, 4, 0, 0, 0, now + 5000, 0, pos, &eng, &seen) == 0 && seen == now + 5000 && pos[0] == 190.0f);

    g_case = "no player within 150 m, but one the units fight is, with a player 300 m from it: the fight is there and its point follows them";
    const uint32_t a[1] = {0xA}; const float a1100[2] = {1100, 0}, a1300[2] = {1300, 0}, p1400[2] = {1400, 0}, p1401[2] = {1401, 0};
    CoXlTestLwPlayerAt(p1400);
    CoXlTestLwFightsClear();
    const int named = Count("; its units fight 0000000A, no player (resource 0000000000000000), 100 m away and 300 m from the nearest player");
    CHECK((CoXlTestLwFightTakeWhom(1000, 0, 6, a, a1100, 1, now, pos) & 0xFF) == 0 && pos[0] == 1000.0f && CoXlTestLwFightTargetOf(0) == 0xA);
    CHECK(Count("; its units fight 0000000A, no player (resource 0000000000000000), 100 m away and 300 m from the nearest player") == named + 1);
    CoXlTestLwFightTarget(0, 0xA, a1100);
    CHECK((CoXlTestLwFightTakeWhom(1040, 0, 6, a, a1100, 1, now + 1000, pos) & 0xFF) == 0 && pos[0] == 1040.0f);
    g_case = "units that name nobody: within 150 m of the fight's living target they still count; with it dead they do not";
    CHECK(CoXlTestLwFightTake(1050, 0, 6, 6, 0, 0, now + 2000, 0, pos, &eng, &seen) == 0 && pos[0] == 1050.0f);
    g_case = "the nearest player 301 m from that target: neither the cluster's target nor the fight's holds the fight";
    CoXlTestLwPlayerAt(p1401);
    CHECK(CoXlTestLwFightTakeWhom(1060, 0, 6, a, a1100, 1, now + 3000, pos) == -1);
    CHECK(CoXlTestLwFightTake(1060, 0, 6, 6, 0, 0, now + 3000, 0, pos, &eng, &seen) == -1 && pos[0] == 1050.0f);
    CoXlTestLwPlayerAt(p1400);
    CHECK(CoXlTestLwFightTake(1060, 0, 6, 6, 0, 0, now + 4000, 0, pos, &eng, &seen) == 0 && pos[0] == 1060.0f);
    CoXlTestLwFightTarget(0, 0xA, NULL);
    CHECK(CoXlTestLwFightTake(1070, 0, 6, 6, 0, 0, now + 5000, 0, pos, &eng, &seen) == -1 && pos[0] == 1060.0f);
    g_case = "one they fight 200 m from them, a player beside it: no fight there";
    CoXlTestLwFightsClear();
    CoXlTestLwPlayerAt(p1400);
    CHECK(CoXlTestLwFightTakeWhom(1100, 0, 6, a, a1300, 1, now, pos) == -1);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwPlayerAt(pl);
    g_case = "a new fight's line says when its units name nobody";
    CoXlTestLwFightsClear();
    const int nobody = Count("m from the nearest player; its units name nobody they fight");
    CHECK(CoXlTestLwFightTake(100, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0 && Count("m from the nearest player; its units name nobody they fight") == nobody + 1);

    g_case = "eight players 400 m apart: units 100 m from the last one make a fight; 200 m from the two nearest make none";
    float pts[16]; for (int k = 0; k < 8; k++) { pts[2 * k] = 400.0f * (float)k; pts[2 * k + 1] = 0; }
    const int np = CoXlTestLwPlayers(pts, 8);
    const float last = 400.0f * (float)(np - 1);
    CHECK(np == 4 || np == 8);
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(last + 100.0f, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0 && pos[0] == last + 100.0f);
    CHECK(CoXlTestLwFightTake(last - 200.0f, 0, 6, 6, 0, 0, now, 1, pos, &eng, &seen) == -1);
    CHECK(CoXlTestLwFightTake(last + 151.0f, 60, 6, 6, 0, 0, now, 1, pos, &eng, &seen) == -1);
    CHECK(CoXlTestLwPlayers(pl, 1) == 1);
    CoXlTestLwFightsClear();
}

/* Players who are shot at keep their fight: units that fight a player hold one at any range, its squads go to that
 * player, and a fight that is on ends 200 m from the players, not at the 150 m where one starts. */
static void ShotAt(void)
{
    float pos[2] = {0, 0}, t[2], to[2]; int eng, stage, moves, next, sentAt; uint64_t seen;
    const uint64_t now = GetTickCount64() + 9000000;
    const float pl[2] = {0, 0}, at0[2] = {0, 0};
    const uint32_t av[1] = {0x502}, other[1] = {0xA};
    CoXlTestLwPlayerAt(pl); CoXlTestLwAvatars(av, 1);
    g_case = "units 400 m from the only player that fight that player make a fight where they stand, the player its target";
    CoXlTestLwFightsClear();
    CHECK((CoXlTestLwFightTakeWhom(400, 0, 6, av, at0, 1, now, pos) & 0xFF) == 0 && pos[0] == 400.0f && CoXlTestLwFightTargetOf(0) == 0x502);
    CHECK(Count("400 m from the nearest player; its units fight player 00000502 400 m away") == 1);
    g_case = "and keep it seen, scan after scan";
    CHECK(CoXlTestLwFightTake(410, 0, 6, 6, 0, 0x502, now + 1000, 0, pos, &eng, &seen) == 0 && seen == now + 1000 && pos[0] == 410.0f);
    g_case = "units as far that fight one that is no player make none";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTakeWhom(400, 0, 6, other, at0, 1, now, pos) == -1);

    g_case = "in at 150 m, out at 200 m: no fight starts at 160 m; one that is on is kept and moved to 200 m, not seen at 201 m, seen again at 190 m";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(160, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == -1);
    CHECK(CoXlTestLwFightTake(150, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwFightTake(200, 0, 6, 6, 0, 0, now + 1000, 0, pos, &eng, &seen) == 0 && pos[0] == 200.0f && seen == now + 1000);
    CHECK(CoXlTestLwFightTake(201, 0, 6, 6, 0, 0, now + 2000, 0, pos, &eng, &seen) == -1 && pos[0] == 200.0f && seen == now + 1000);
    CHECK(CoXlTestLwFightTake(190, 0, 6, 6, 0, 0, now + 3000, 0, pos, &eng, &seen) == 0 && pos[0] == 190.0f && seen == now + 3000);

    g_case = "a fight unseen for 30 s is over; its line names the place rule when its units fought on over 200 m from every player";
    const int ruled = Count("squads sent); the place rule ended it: its units were engaged to 1 s ago, over 200 m from every player and fighting none");
    const int over = Count("is over after");
    CHECK(CoXlTestLwFightOver(0, now + 33000) == 0); /* seen at 3 s */
    for (int k = 4; k <= 33; k++) CHECK(CoXlTestLwFightTake(210, 0, 6, 6, 0, 0, now + 1000u * (uint64_t)k, 0, pos, &eng, &seen) == -1);
    CHECK(CoXlTestLwFightOver(0, now + 34000) == 1 && Count("is over after") == over + 1);
    CHECK(Count("squads sent); the place rule ended it: its units were engaged to 1 s ago, over 200 m from every player and fighting none") == ruled + 1);
    CHECK(CoXlTestLwFightTake(150, 0, 6, 6, 0, 0, now + 40000, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwFightOver(0, now + 71000) == 1 && Count("is over after") == over + 2);
    CHECK(Count("the place rule ended it") == ruled + 1); /* its units calmed down: no such words */
    g_case = "eight players: units far from all that fight the last player make a fight; an avatar of nobody in the session does not";
    float pts[16]; uint32_t ids[8];
    for (int k = 0; k < 8; k++) { pts[2 * k] = 0; pts[2 * k + 1] = 400.0f * (float)k; ids[k] = 0x600u + (uint32_t)k; }
    const int np = CoXlTestLwPlayers(pts, 8);
    CoXlTestLwAvatars(ids, np);
    CoXlTestLwFightsClear();
    CHECK((CoXlTestLwFightTakeWhom(900, 0, 6, &ids[np - 1], &pts[2 * (np - 1)], 1, now, pos) & 0xFF) == 0);
    CHECK(CoXlTestLwPlayers(pl, 1) == 1);
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTakeWhom(900, 0, 6, &ids[np - 1], &pts[2 * (np - 1)], 1, now, pos) == -1);
    CoXlTestLwAvatars(av, 1);

    g_case = "a squad's aim is the player its fight's units fight, 400 m from the fight's point, while they fought that player in the last 5 s";
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, p600[2] = {600, 0}, p820[2] = {820, 0}, p840[2] = {840, 0}, sq[2] = {900, 0};
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwPlayerAt(p600); CoXlTestLwAvatars(av, 1);
    CoXlTestLwFightTarget(0, 0x502, p600); CoXlTestLwFightFought(0, 0);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 600.0f);
    g_case = "fought no more, the player is not followed: the squad keeps the point it has";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwPlayerAt(p600); CoXlTestLwAvatars(av, 1);
    CoXlTestLwFightTarget(0, 0x502, p600); CoXlTestLwFightFought(0, 6000);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1000.0f);
    g_case = "the fight's point 30 m on: no new point with the nearest player 210 m from it, a new one with a player 190 m from it";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwFightPoint(0, 1030, 0);
    CoXlTestLwPlayerAt(p820);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1000.0f);
    CoXlTestLwPlayerAt(p840);
    CHECK(CoXlTestLwReinforceTick(at, 0, 1000, &stage, t, &moves) == 2 && t[0] == 1030.0f);

    g_case = "such a fight sends its squad to that player; fought no more, with nobody within 150 m of its point, it sends none";
    const int none = Count("sends no squad: no player and no target of it within 150 m");
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(p600); CoXlTestLwAvatars(av, 1);
    CoXlTestLwFightTarget(0, 0x502, p600); CoXlTestLwFightFought(0, 0);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 2 && to[0] == 600.0f && Count("sends no squad: no player and no target of it within 150 m") == none);
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(p600); CoXlTestLwAvatars(av, 1);
    CoXlTestLwFightTarget(0, 0x502, p600); CoXlTestLwFightFought(0, 6000);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 0 && Count("sends no squad: no player and no target of it within 150 m") == none + 1);
    g_case = "one that is no player, 400 m from the point and fought now, is no aim";
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(p600); CoXlTestLwAvatars(av, 1);
    CoXlTestLwFightTarget(0, 0xA, p600); CoXlTestLwFightFought(0, 0);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 0);
    CoXlTestLwFightTarget(0, 0, NULL);
    const uint32_t nobody[1] = {0};
    CoXlTestLwAvatars(nobody, 1); CoXlTestLwPlayerAt(pl);
    CoXlTestLwFightsClear();
}

/* A squad is never turned to a fight's point that every player and the fight's target have left. */
static void AimNear(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, p100[2] = {1100, 0}, gone[2] = {0, 0}, back[2] = {900, 0}, by[2] = {1060, 0};
    int stage, moves; float t[2];
    g_case = "on its way to the target; the target dies and no player is within 150 m of the fight's point: it keeps its point";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CoXlTestLwFightTarget(0, 0x400001, NULL);
    CoXlTestLwPlayerAt(gone);
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1100.0f);
    g_case = "a player back within 150 m of the fight's point: the point is its point again";
    CoXlTestLwPlayerAt(back);
    CHECK(CoXlTestLwReinforceTick(at, 0, 12000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1000.0f);
    g_case = "it arrives at its own point when that is all it has";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves);
    CoXlTestLwFightTarget(0, 0x400001, NULL);
    CoXlTestLwPlayerAt(gone);
    CHECK(CoXlTestLwReinforceTick(by, 0, 1000, &stage, t, &moves) == 2 && stage == 1 && t[0] == 1100.0f); /* 40 m from its point, 60 m from the fight's */
    g_case = "arrived, the fight's point 50 m on and no player within 150 m of it: no move; a player there: it moves";
    const int notes = Count("it goes there (move 1 of 3)");
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightPoint(0, 1050, 0);
    CoXlTestLwPlayerAt(gone);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 11000, &stage, t, &moves) == 2 && stage == 1 && moves == 0 && t[0] == 1000.0f);
    CHECK(Count("it goes there (move 1 of 3)") == notes);
    CoXlTestLwPlayerAt(by);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 12000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1050.0f);

    g_case = "a fight with no player and no living target within 150 m of its point sends no squad, noted, and waits";
    const float sq[2] = {900, 0}, p[2] = {1060, 0}; float to[2]; int next, sentAt;
    const int none = Count("sends no squad: no player and no target of it within 150 m");
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(gone);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 0 && sentAt == 1 && Count("sends no squad: no player and no target of it within 150 m") == none + 1);
    g_case = "its living target 60 m from its point, a player 300 m from the target: the squad is sent to the target; 301 m: none is sent";
    const float p1360[2] = {1360, 0}, p1361[2] = {1361, 0};
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(p1360);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 2 && to[0] == 1060.0f && Count("sends no squad: no player and no target of it within 150 m") == none + 1);
    CoXlTestLwSendSetup(0, sq, fight);
    CoXlTestLwPlayerAt(p1361);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CHECK(CoXlTestLwSend(to, &moves, &next, &sentAt) == 0 && Count("sends no squad: no player and no target of it within 150 m") == none + 2);
    CoXlTestLwFightTarget(0, 0, NULL);
}

/* A fight's target is one its units fight: kept 5 s after the last of them fought it, not for as long as it lives near. */
static void TargetFought(void)
{
    float pos[2];
    const uint64_t now = GetTickCount64() + 7000000;
    const float pl[2] = {0, 0}, p50[2] = {50, 0};
    const uint32_t a[1] = {0xA}, b[1] = {0xB}, cd[2] = {0xC, 0xD}; const float at50[2] = {50, 0}, at30[2] = {30, 0}, at100and20[4] = {100, 0, 20, 0};
    CoXlTestLwPlayerAt(pl);
    CoXlTestLwFightsClear();
    g_case = "the units fight another player now: the fight's target stays for 5 s, then it is the one they fight";
    CHECK((CoXlTestLwFightTakeWhom(0, 0, 6, a, at50, 1, now, pos) & 0xFF) == 0 && CoXlTestLwFightTargetOf(0) == 0xA);
    CoXlTestLwFightTarget(0, 0xA, p50);
    for (int s = 1; s <= 5; s++) { CoXlTestLwFightTakeWhom(0, 0, 6, b, at30, 1, now + 1000u * (uint64_t)s, pos); CHECK(CoXlTestLwFightTargetOf(0) == 0xA); }
    CoXlTestLwFightTakeWhom(0, 0, 6, b, at30, 1, now + 6000, pos);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xB);
    g_case = "two clusters of one fight that fight two players: the target does not change from scan to scan";
    for (int s = 7; s <= 20; s++)
    {
        CoXlTestLwFightTakeWhom(0, 0, 6, b, at30, 1, now + 1000u * (uint64_t)s, pos);
        CoXlTestLwFightTakeWhom(10, 0, 6, a, at50, 1, now + 1000u * (uint64_t)s, pos);
        CHECK(CoXlTestLwFightTargetOf(0) == 0xB);
    }
    g_case = "scans that name nobody change nothing, however long";
    int eng; uint64_t seen;
    for (int s = 21; s <= 40; s++) CoXlTestLwFightTake(0, 0, 6, 6, 0, 0, now + 1000u * (uint64_t)s, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xB);
    g_case = "of several the units fight, the one nearest the fight's point; all of them count as fought";
    CHECK((CoXlTestLwFightTakeWhom(0, 0, 6, cd, at100and20, 2, now + 41000, pos) >> 8) == 2 && CoXlTestLwFightTargetOf(0) == 0xD);
    CoXlTestLwFightTakeWhom(0, 0, 6, cd, at100and20, 2, now + 50000, pos);
    CoXlTestLwFightTakeWhom(0, 0, 6, a, at50, 1, now + 55000, pos);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xD); /* fought 5 s ago */
    g_case = "of two that are no players, the one with a player within 300 m of it is the target, though the other is nearer the point";
    const uint32_t ef[2] = {0xE, 0xF}; const float e350f200[4] = {350, 0, -200, 0}; float p100[2];
    CoXlTestLwFightsClear();
    CHECK((CoXlTestLwFightTakeWhom(100, 0, 6, ef, e350f200, 2, now + 51000, p100) >> 8) == 2 && CoXlTestLwFightTargetOf(0) == 0xF);
    CoXlTestLwFightsClear();
    CoXlTestLwFightTakeWhom(0, 0, 6, cd, at100and20, 2, now + 50000, pos);
    g_case = "a target the units go on fighting stays, though they fight one nearer the point as well";
    const uint32_t c[1] = {0xC}; const float at100[2] = {100, 0};
    CoXlTestLwFightTakeWhom(0, 0, 6, c, at100, 1, now + 56000, pos);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xC);
    for (int s = 57; s <= 70; s++) { CoXlTestLwFightTakeWhom(0, 0, 6, cd, at100and20, 2, now + 1000u * (uint64_t)s, pos); CHECK(CoXlTestLwFightTargetOf(0) == 0xC); }
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwFightsClear();
}

/* A new point or a move whose goal goes nowhere leaves the squad where it is in its fight; only its first goal ends it. */
static void MoveKeeps(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, rock[2] = {1100, 0}, p100[2] = {1100, 0}, p145[2] = {1145, 0}, by[2] = {1070, 0};
    int stage, moves; float t[2], to[2];
    g_case = "an arrived squad's move, no navmesh at the target nor at the fight's point: it stays at the fight, the move not counted, noted";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(1, NULL, 0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwGoalStub(1, rock, 200.0f);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    const int ends = Count("no navmesh at the point"), kept = Count("it keeps its point at (1000, 0), at the fight, the move not counted");
    CHECK(CoXlTestLwReinforceTick(fight, 0, 11000, &stage, t, &moves) == 2 && stage == 1 && moves == 0 && t[0] == 1000.0f);
    CHECK(Count("no navmesh at the point") == ends && Count("it keeps its point at (1000, 0), at the fight, the move not counted") == kept + 1);
    CHECK(CoXlTestLwGoalsAsked() == 2 && CoXlTestLwGoals(to) == 0 && (CoXlTestLwReinforceState() & 3) == 2);
    g_case = "it asks again 30 s later, not before";
    CHECK(CoXlTestLwReinforceTick(fight, 0, 40000, &stage, t, &moves) == 2 && CoXlTestLwGoalsAsked() == 2 && stage == 1);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 42000, &stage, t, &moves) == 2 && CoXlTestLwGoalsAsked() == 4 && stage == 1 && moves == 0);
    g_case = "navmesh at the target again: the move is made and counted";
    CoXlTestLwGoalStub(1, NULL, 0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 73000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1100.0f && CoXlTestLwGoals(to) == 1);
    g_case = "its 180 s at the fight go on through a move that was not made";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(1, NULL, 0);
    CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves);
    CoXlTestLwGoalStub(1, rock, 200.0f);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    const int stays = Count("180 s at the fight");
    CHECK(CoXlTestLwReinforceTick(fight, 0, 100000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 179000, &stage, t, &moves) == 2 && Count("180 s at the fight") == stays);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 181000, &stage, t, &moves) == 0 && Count("180 s at the fight") == stays + 1);

    g_case = "on its way, a new point with no navmesh there nor at the fight's point: it keeps its point and arrives there";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwGoalStub(1, NULL, 0);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1100.0f && CoXlTestLwGoals(to) == 1);
    const float off[2] = {1130, 0};
    CoXlTestLwGoalStub(1, off, 135.0f); /* the new target and the fight's point; not its point at (1100, 0)... */
    CoXlTestLwFightTarget(0, 0x400001, p145);
    const int way = Count("it keeps its point at (1100, 0), on its way");
    CHECK(CoXlTestLwReinforceTick(at, 0, 11000, &stage, t, &moves) == 2 && stage == 0 && t[0] == 1100.0f && moves == 0);
    CHECK(Count("it keeps its point at (1100, 0), on its way") == way + 1 && Count("no navmesh at the point") == ends);
    CHECK(CoXlTestLwReinforceTick(by, 0, 12000, &stage, t, &moves) == 2 && stage == 1); /* 30 m from its point, 75 m from the target */

    g_case = "a first goal refused for a missing record: it ends in that word, and the fight's point is not tried";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CoXlTestLwGoalStub(1, rock, 25.0f);
    CoXlTestLwGoalWhy(2);
    const int word = Count("ends (no goal at the point: its squad has no job record)"), fall = Count("no navmesh at its fight's target");
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 0 && CoXlTestLwGoalsAsked() == 1);
    CHECK(Count("ends (no goal at the point: its squad has no job record)") == word + 1 && Count("no navmesh at its fight's target") == fall && Count("no navmesh at the point") == ends);
    g_case = "no ground at the target is a miss of the place: the fight's point is tried";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, rock);
    CoXlTestLwGoalStub(1, rock, 25.0f);
    CoXlTestLwGoalWhy(4);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && CoXlTestLwGoalsAsked() == 2 && CoXlTestLwGoals(to) == 1 && to[0] == 1000.0f);
    g_case = "an online squad not sent for a missing record: the line says so";
    const float sq[2] = {900, 0}, p[2] = {1060, 0}; int next, sentAt;
    const int rec = Count("is not sent: no goal for it: its squad has no squad record"), nav = Count("is not sent: no navmesh");
    CoXlTestLwSendSetup(1, sq, fight);
    CoXlTestLwFightTarget(0, 0x400001, p);
    CoXlTestLwGoalStub(1, p, 200.0f);
    CoXlTestLwGoalWhy(3);
    CHECK(CoXlTestLwSend(t, &moves, &next, &sentAt) == 0 && Count("is not sent: no goal for it: its squad has no squad record") == rec + 1 && Count("is not sent: no navmesh") == nav);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwGoalStub(0, NULL, 0);

    g_case = "the game's goal call says why it set no goal: not online, no squad entity, no job record; 12 lines a word";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    const int last = Count("its squad has no job record (the last line with this reason this mission)");
    const int off0 = Count("goal at (10, 20) not sent: the squad is not online"), ent = Count("goal at (10, 20) not sent: it has no squad entity"), job = Count("goal at (10, 20) not sent: its squad has no job record");
    CHECK(CoXlTestLwGoTo(1, 5, 10, 20) == 0 && Count("goal at (10, 20) not sent: the squad is not online") == off0 + 1);
    CHECK(CoXlTestLwGoTo(3, 0, 10, 20) == -1 && Count("goal at (10, 20) not sent: it has no squad entity") == ent + 1);
    CHECK(CoXlTestLwGoTo(3, 5, 10, 20) == -2 && Count("goal at (10, 20) not sent: its squad has no job record") == job + 1);
    for (int k = 0; k < 14; k++) CHECK(CoXlTestLwGoTo(3, 5, 10, 20) == -2);
    CHECK(Count("goal at (10, 20) not sent: its squad has no job record") == job + 12 && Count("goal at (10, 20) not sent: it has no squad entity") == ent + 1);
    CHECK(Count("its squad has no job record (the last line with this reason this mission)") == last + 1);
}

/* Members at alert 1 or 2 keep a squad from a new point for 30 s in a row, not for as long as someone hears something. */
static void HeardHold(void)
{
    const float fight[2] = {1000, 0}, at[2] = {800, 0}, p100[2] = {1100, 0}, p145[2] = {1145, 0}, p120[2] = {1120, 0};
    int stage, moves; float t[2];
    g_case = "members at alert 1 or 2 for 29 s: no new point; 31 s: the new point; the next one waits its 30 s again";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0x400001, p100);
    CoXlTestLwHeard(1);
    CHECK(CoXlTestLwReinforceTick(at, 0, 0, &stage, t, &moves) == 2 && t[0] == 1000.0f && (CoXlTestLwReinforceState() & 8));
    CHECK(CoXlTestLwReinforceTick(at, 0, 29000, &stage, t, &moves) == 2 && t[0] == 1000.0f);
    CHECK(CoXlTestLwReinforceTick(at, 0, 31000, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CoXlTestLwFightTarget(0, 0x400001, p145);
    CHECK(CoXlTestLwReinforceTick(at, 0, 60000, &stage, t, &moves) == 2 && t[0] == 1100.0f);
    CHECK(CoXlTestLwReinforceTick(at, 0, 62000, &stage, t, &moves) == 2 && t[0] == 1145.0f);
    g_case = "calm in between: the 30 s start again with the next member that hears something";
    CoXlTestLwHeard(0);
    CHECK(CoXlTestLwReinforceTick(at, 0, 80000, &stage, t, &moves) == 2 && !(CoXlTestLwReinforceState() & 8));
    CoXlTestLwFightTarget(0, 0x400001, p120);
    CoXlTestLwHeard(2);
    CHECK(CoXlTestLwReinforceTick(at, 0, 100000, &stage, t, &moves) == 2 && t[0] == 1145.0f);
    CHECK(CoXlTestLwReinforceTick(at, 0, 129000, &stage, t, &moves) == 2 && t[0] == 1145.0f);
    CHECK(CoXlTestLwReinforceTick(at, 0, 131000, &stage, t, &moves) == 2 && t[0] == 1120.0f);
    g_case = "arrived, members at alert 2 and no tell left: after 30 s it moves to where its fight is";
    CoXlTestLwReinforceOn(fight, 20000, 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwHeard(0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 0, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwHeard(2);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 1000, &stage, t, &moves) == 2 && stage == 1);
    CoXlTestLwFightPoint(0, 1050, 0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 30000, &stage, t, &moves) == 2 && stage == 1 && moves == 0);
    CHECK(CoXlTestLwReinforceTick(fight, 0, 32000, &stage, t, &moves) == 2 && stage == 0 && moves == 1 && t[0] == 1050.0f);
    CoXlTestLwHeard(0);
}

/* A player's body is told by its avatar entity: its noises are not under the 120 s rule of a source in place. */
static void NoiseBody(void)
{
    g_case = "a noise from a player's avatar entity 30 s after it drew squads there may draw again; a unit handle is no entity";
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x502, 0x502, 30000, 0.0f, 0, 100.0f) == 0);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x9001, 0x9001, 30000, 0.0f, 0, 100.0f) == 2);
    g_case = "another source in place: once per 120 s within 50 m";
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x1D5, 0x1D5, 119000, 0.0f, 0, 100.0f) == 2);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x1D5, 0x1D5, 121000, 0.0f, 0, 100.0f) == 0);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x1D5, 0x1D5, 30000, 51.0f, 0, 100.0f) == 0);
    g_case = "the test's noise counted as a source in place: the avatar under the 120 s rule";
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x502, 0x502, 30000, 0.0f, 1, 100.0f) == 2);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x502, 0x502, 121000, 0.0f, 1, 100.0f) == 0);
    g_case = "a source noted for no player near is still noted for the 120 s rule";
    const int farOff = Count("draws no squad: the nearest player is"), rule = Count("draws no squad: this source drew squads");
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x777, 0, 0, 0.0f, 0, 900.0f) == 1 && Count("draws no squad: the nearest player is") == farOff + 1);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x777, 0x777, 30000, 0.0f, 0, 100.0f) == 2 && Count("draws no squad: this source drew squads") == rule + 1);
    CHECK(CoXlTestLwNoiseGate(0x502, 0x9001, 0x777, 0x777, 31000, 0.0f, 0, 100.0f) == 2 && Count("draws no squad: this source drew squads") == rule + 1);
}

/* What places a fight. Run 5: four units at level 7 walked 1 km east at 1.7 m/s and took the fight's point along. */
/* The noise path: an event that drew nobody is asked again while the firing goes on; one source draws two squads
 * per 120 s over all its events and both passes; a noise a fight answers draws none. */
static void NoisePath(void)
{
    const uint64_t t0 = GetTickCount64() + 11000000;
    const float pl[2] = {0, 0}, out[2] = {2000, 0};
    g_case = "no calm squad in reach at the first shot, the firing goes on: asked again after 10 s, and the squad that came in reach is sent";
    CoXlTestLwNoiseWorld(pl, 1, out, 1);
    const int tries = Count(", asked again (try 1): 0 (offline, 200 m)");
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 0 && CoXlTestLwNoiseCount(0) == 1);
    CoXlTestLwNoiseSquad(0, 0, 200, 0);
    for (int s = 1; s <= 9; s++) CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0) == 0);
    CHECK(CoXlTestLwNoise(0, 0, 0, t0 + 9999, 0) == 0 && CoXlTestLwNoiseCount(2) == 0);
    CHECK(CoXlTestLwNoise(0, 0, 0, t0 + 10000, 0) == 1 && CoXlTestLwNoiseCount(2) == 1 && CoXlTestLwNoiseCount(0) == 1);
    CHECK(Count(", asked again (try 1): 0 (offline, 200 m)") == tries + 1);
    g_case = "an event that drew a squad is not asked again";
    for (int s = 11; s <= 40; s++) CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0);
    CHECK(CoXlTestLwNoiseCount(2) == 1 && CoXlTestLwNoiseCount(1) == 1);
    g_case = "one shot and no more: the firing does not go on, nobody is asked again";
    CoXlTestLwNoiseWorld(pl, 1, out, 1);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 0);
    CoXlTestLwNoiseSquad(0, 0, 200, 0);
    for (int s = 10; s <= 40; s += 10) CHECK(CoXlTestLwNoise(0, 0, 0, t0 + 1000u * (uint64_t)s, 0) == 0);
    CHECK(CoXlTestLwNoiseCount(2) == 0);
    g_case = "an online squad that came in reach is asked 1.5 s after the try, not with it";
    CoXlTestLwNoiseWorld(pl, 1, out, 1);
    CoXlTestLwNoiseSquad(0, 1, 0, 0);
    CoXlTestLwNoise(0x500, 0, 0, t0, 0);
    for (int s = 1; s <= 9; s++) { CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0); if (s == 2) CoXlTestLwNoiseSquad(0, 0, 200, 0); }
    CHECK(CoXlTestLwNoise(0, 0, 0, t0 + 10000, 0) == 0 && CoXlTestLwNoiseCount(2) == 1);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 11499, 0) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 11500, 0) == 1);
    g_case = "two minutes of firing with nobody in reach: twelve tries, a line, then no more";
    CoXlTestLwNoiseWorld(pl, 1, out, 1);
    const int gave = Count("no calm squad in reach in 12 tries over 120 s of firing");
    for (int s = 0; s <= 200; s++) CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0);
    CHECK(CoXlTestLwNoiseCount(2) == 12 && CoXlTestLwNoiseCount(0) == 1 && Count("no calm squad in reach in 12 tries over 120 s of firing") == gave + 1);

    g_case = "six squads in reach, bursts 15 s apart for two minutes: two squads are drawn, the rest stay; noted once";
    const float six[12] = {100, 0, 120, 0, 140, 0, 160, 0, 180, 0, 200, 0};
    CoXlTestLwNoiseWorld(pl, 1, six, 6);
    const int capped = Count("draws no squad: this source drew 2 squads in the last 120 s or has as many on their way (at most 2)");
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 2 && CoXlTestLwNoiseCount(1) == 2);
    for (int s = 15; s < 120; s += 15) CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0) == 2);
    CHECK(CoXlTestLwNoiseCount(0) == 1 && CoXlTestLwNoiseCount(3) == 7);
    CHECK(Count("draws no squad: this source drew 2 squads in the last 120 s or has as many on their way (at most 2)") == capped + 1);
    g_case = "their investigations over: still none within 120 s of the draw, two more after it";
    CoXlTestLwNoiseSquad(0, 2, 0, 0); CoXlTestLwNoiseSquad(1, 2, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 119999, 0) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 131000, 0) == 2 && CoXlTestLwNoiseCount(1) == 4);
    g_case = "one of the two drawn 60 s later: the next is drawn 120 s after the first, and no second one until 120 s after that one";
    CoXlTestLwNoiseWorld(pl, 1, six, 1);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 1);
    CoXlTestLwNoiseSquad(0, 2, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 60000, 0) == 1 && CoXlTestLwNoiseCount(1) == 2);
    CoXlTestLwNoiseSquad(0, 2, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 119000, 0) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 131000, 0) == 1 && CoXlTestLwNoiseCount(1) == 3);
    CoXlTestLwNoiseSquad(0, 2, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 179000, 0) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 191000, 0) == 1 && CoXlTestLwNoiseCount(1) == 4);
    g_case = "squads still investigating the source after 120 s count against it";
    CoXlTestLwNoiseWorld(pl, 1, six, 6);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 2);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 130000, 0) == 2);
    CoXlTestLwNoiseSquad(0, 2, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 145000, 0) == 2 && CoXlTestLwNoiseCount(1) == 3);
    g_case = "one offline and three online squads in reach: the offline one at once, one online one 1.5 s later, two in all";
    CoXlTestLwNoiseWorld(pl, 1, six, 4);
    for (int i = 1; i < 4; i++) CoXlTestLwNoiseSquad(i, 1, 0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 1);
    CHECK(CoXlTestLwNoise(0, 0, 0, t0 + 1500, 0) == 2);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0 + 15000, 0) == 2 && CoXlTestLwNoise(0, 0, 0, t0 + 16500, 0) == 2);

    g_case = "an event asked again goes through the gate: its source drew its two squads elsewhere meanwhile, so it is not passed again";
    const float pl2[4] = {0, 0, 700, 0}, at700[4] = {800, 0, 820, 0};
    CoXlTestLwNoiseWorld(pl2, 2, at700, 2);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 0);
    CHECK(CoXlTestLwNoise(0x500, 700, 0, t0 + 500, 0) == 2);
    for (int s = 1; s <= 10; s++) CoXlTestLwNoise(0x500, 0, 0, t0 + 1000u * (uint64_t)s, 0);
    CHECK(CoXlTestLwNoiseCount(2) == 0 && CoXlTestLwNoiseCount(3) == 1 && CoXlTestLwNoiseCount(0) == 2);

    g_case = "eight players firing from eight places 1 km apart, four squads in reach of each: two squads each";
    float pls[16], sqs[64];
    for (int k = 0; k < 8; k++) { pls[2 * k] = 1000.0f * (float)k; pls[2 * k + 1] = 0; for (int j = 0; j < 4; j++) { sqs[8 * k + 2 * j] = 1000.0f * (float)k + 100.0f + 20.0f * (float)j; sqs[8 * k + 2 * j + 1] = 0; } }
    const int np = CoXlTestLwPlayers(pls, 8);
    CoXlTestLwNoiseWorld(pls, np, sqs, 4 * np);
    for (int round = 0; round < 8; round++)
        for (int k = 0; k < np; k++) CHECK(CoXlTestLwNoise(0x500u + (uint32_t)k, 1000.0f * (float)k, 0, t0 + 15000u * (uint64_t)round, 0x500u + (uint32_t)k) == 2);
    CHECK(CoXlTestLwNoiseCount(1) == 2u * (uint32_t)np && CoXlTestLwNoise(0, 0, 0, t0 + 106500, 0) == 2 * np);

    g_case = "a shooter 120 m from a live fight's point gets no noise squads; 150 m from it, two";
    uint64_t seen; int eng; float pos[2];
    CoXlTestLwNoiseWorld(pl, 1, six, 6);
    CHECK(CoXlTestLwFightTake(-120, 0, 6, 6, 0, 0, t0, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 0 && CoXlTestLwNoiseCount(0) == 1);
    CoXlTestLwNoiseWorld(pl, 1, six, 6);
    CHECK(CoXlTestLwFightTake(-150, 0, 6, 6, 0, 0, t0, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, t0, 0) == 2);
    g_case = "a shooter its fight's units fight from 400 m gets the fight's squads, not the noise's; another player there gets them";
    const float two[4] = {0, 0, 0, 30}; const uint32_t av[1] = {0x500}; const float at0[2] = {0, 0};
    CoXlTestLwNoiseWorld(two, 2, six, 6);
    CHECK((CoXlTestLwFightTakeWhom(-400, 0, 6, av, at0, 1, t0, pos) & 0xFF) == 0);
    CoXlTestLwFightTarget(0, 0x500, at0); CoXlTestLwFightFought(0, 0);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, GetTickCount64(), 0) == 0);
    CHECK(CoXlTestLwNoise(0x501, 0, 30, GetTickCount64(), 0) == 0); /* 30 m from where the fight's squads go */
    CHECK(CoXlTestLwNoise(0x501, 0, 150, GetTickCount64(), 0) == 2);
    g_case = "that player fought no more: its own noise draws squads";
    CoXlTestLwNoiseWorld(two, 2, six, 6);
    CHECK((CoXlTestLwFightTakeWhom(-400, 0, 6, av, at0, 1, t0, pos) & 0xFF) == 0);
    CoXlTestLwFightTarget(0, 0x500, at0); CoXlTestLwFightFought(0, 6000);
    CHECK(CoXlTestLwNoise(0x500, 0, 0, GetTickCount64(), 0) == 2);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwFightsClear();
    CoXlTestLwPlayers(pl, 1);
}

static void FightPlace(void)
{
    float pos[2], hot[2]; int eng; uint64_t seen;
    const uint64_t now = GetTickCount64();
    const float pl[2] = {0, 0};
    CoXlTestLwPlayerAt(pl);
    g_case = "levels 3, 5 and 6 place a fight; level 7 counts in it; other levels are no part of it";
    const float pts[12] = {0, 0, 10, 0, 30, 0, 34, 0, 20, 0, 20, 0};
    const uint32_t mixed[6] = {3, 5, 7, 7, 2, 4}, chasing[6] = {7, 7, 7, 7, 0, 1}, six[6] = {6, 6, 6, 6, 6, 6};
    CHECK(CoXlTestLwCluster(pts, mixed, 6, pos, hot) == (4 | 2 << 8) && fabsf(pos[0] - 18.5f) < 0.01f && fabsf(hot[0] - 5.0f) < 0.01f);
    CHECK(CoXlTestLwCluster(pts, chasing, 6, pos, hot) == 4 && fabsf(pos[0] - 18.5f) < 0.01f);
    CHECK(CoXlTestLwCluster(pts, six, 6, pos, hot) == (6 | 6 << 8));

    g_case = "four units at level 7 start no fight";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(100, 0, 4, 0, 0, 0, now, 0, pos, &eng, &seen) == -1 && seen == 0);
    g_case = "a call starts one; units at level 3 start one where they stand";
    CHECK(CoXlTestLwFightTake(100, 0, 8, 0, 1, 0, now, 0, pos, &eng, &seen) == 0 && pos[0] == 100.0f && eng == 8);
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(0, 0, 6, 6, 0, 0, now, 0, pos, &eng, &seen) == 0 && pos[0] == 0.0f && eng == 6 && seen == now);

    g_case = "level 7 units walking off at 1.7 m/s: part of the fight within 60 m, its point stays; beyond, they keep and start no fight";
    for (int s = 1; s <= 60; s++)
    {
        const uint64_t t = now + 1000u * (uint64_t)s; const float x = 1.7f * (float)s;
        const int f = CoXlTestLwFightTake(x, 0, 4, 0, 0, 0, t, 0, pos, &eng, &seen);
        CHECK(pos[0] == 0.0f && pos[1] == 0.0f);
        if (x < 59.9f) CHECK(f == 0 && seen == t && eng == 4);
        else if (x > 60.1f) CHECK(f == -1 && seen < t);
    }
    g_case = "one scan: the units that see their target place the fight, the level 7 units count, in either order";
    const uint64_t t1 = now + 100000, t2 = now + 101000, t3 = now + 102000, t4 = now + 103000;
    CoXlTestLwFightsClear();
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0, t1, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTake(30, 0, 4, 0, 0, 0, t1, 0, pos, &eng, &seen) == 0 && pos[0] == 0.0f && eng == 10);
    CoXlTestLwFightTake(30, 0, 4, 0, 0, 0, t2, 0, pos, &eng, &seen);
    CHECK(pos[0] == 0.0f && eng == 4);
    CHECK(CoXlTestLwFightTake(10, 0, 6, 6, 0, 0, t2, 0, pos, &eng, &seen) == 0 && pos[0] == 10.0f && eng == 10);
    g_case = "two clusters that see their targets in one scan: one centre, weighted by units";
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0, t3, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTake(40, 0, 2, 2, 0, 0, t3, 0, pos, &eng, &seen) == 0 && fabsf(pos[0] - 10.0f) < 0.01f && eng == 8);
    g_case = "the next scan's units 20 m on: the point follows them";
    CHECK(CoXlTestLwFightTake(30, 0, 3, 3, 0, 0, t4, 0, pos, &eng, &seen) == 0 && pos[0] == 30.0f && eng == 3);
    g_case = "level 7 units standing 10 m from the point keep the fight seen 60 s after its last placing unit, no longer";
    const uint64_t t5 = now + 200000;
    CoXlTestLwFightsClear();
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0, t5, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTake(10, 0, 4, 0, 0, 0, t5 + 60000, 0, pos, &eng, &seen) == 0 && seen == t5 + 60000);
    CHECK(CoXlTestLwFightTake(10, 0, 4, 0, 0, 0xB, t5 + 61000, 0, pos, &eng, &seen) == -1 && seen == t5 + 60000 && eng == 4 && CoXlTestLwFightTargetOf(0) == 0);
    g_case = "a unit that sees its target again: the fight is seen and the 60 s start again";
    CHECK(CoXlTestLwFightTake(0, 0, 5, 1, 0, 0, t5 + 62000, 0, pos, &eng, &seen) == 0 && seen == t5 + 62000);
    CHECK(CoXlTestLwFightTake(10, 0, 4, 0, 0, 0, t5 + 122000, 0, pos, &eng, &seen) == 0 && seen == t5 + 122000);
    g_case = "a call counts as a placing unit";
    CoXlTestLwFightsClear();
    CHECK(CoXlTestLwFightTake(100, 0, 8, 0, 1, 0, t5, 0, pos, &eng, &seen) == 0);
    CHECK(CoXlTestLwFightTake(100, 0, 4, 0, 0, 0, t5 + 60000, 0, pos, &eng, &seen) == 0 && seen == t5 + 60000);
    CHECK(CoXlTestLwFightTake(100, 0, 4, 0, 0, 0, t5 + 61000, 0, pos, &eng, &seen) == -1);
    g_case = "a fight keeps its target while it lives within 150 m of the point; one that left or died gives way to the scan's";
    const float p50[2] = {50, 0}, p151[2] = {151, 0};
    CoXlTestLwFightsClear();
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0xA, t5, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xA);
    CoXlTestLwFightTarget(0, 0xA, p50);
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0xB, t5 + 1000, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xA);
    CoXlTestLwFightTarget(0, 0xA, p151);
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0xB, t5 + 2000, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xB);
    CoXlTestLwFightTarget(0, 0xB, NULL);
    CoXlTestLwFightTake(0, 0, 6, 6, 0, 0xC, t5 + 3000, 0, pos, &eng, &seen);
    CHECK(CoXlTestLwFightTargetOf(0) == 0xC);
    CoXlTestLwFightTarget(0, 0, NULL);
    CoXlTestLwFightsClear();
}

/* The test teleport's ground check on an extra large level. */
static void TeleportGround(void)
{
    g_case = "5 m under the height plan on the drawn ground (a stamp's road, run 5's (-55, 3)): taken";
    CHECK(CoXlTestLwTeleportUnder(60.8f, 65.8f, 60.8f) == 0 && CoXlTestLwTeleportUnder(60.8f, 65.8f, 61.2f) == 0);
    g_case = "under the plan and off the drawn ground, or the drawn ground unread: refused";
    CHECK(CoXlTestLwTeleportUnder(60.8f, 65.8f, 65.6f) == 1 && CoXlTestLwTeleportUnder(60.8f, 65.8f, 61.4f) == 1);
    CHECK(CoXlTestLwTeleportUnder(-36.7f, 60.0f, 60.0f) == 1 && CoXlTestLwTeleportUnder(60.8f, 65.8f, -9999.0f) == 1);
    g_case = "within 3 m of the plan, or no plan here: taken";
    CHECK(CoXlTestLwTeleportUnder(63.0f, 65.8f, -9999.0f) == 0 && CoXlTestLwTeleportUnder(10.0f, -9999.0f, -9999.0f) == 0);
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
    OnlineLimit();
    FightAim();
    AimOffNavmesh();
    Send();
    OneClock();
    AimRules();
    KillerKind();
    FightNear();
    ShotAt();
    NoisePath();
    AimNear();
    TargetFought();
    MoveKeeps();
    HeardHold();
    NoiseBody();
    FightPlace();
    TeleportGround();
    if (g_failed) { printf("lwreinforce: %d checks failed\n", g_failed); return 1; }
    printf("lwreinforce: all cases passed\n");
    return 0;
}
