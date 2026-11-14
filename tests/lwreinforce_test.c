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
    CoXlTestLwPlayerAt(fight); /* the player walks up to it: inside the band, back to stage 0 and out to the edge */
    CoXlTestLwWalk(0.1f, &stage, &held, pos, &at);
    CHECK(stage == 0);
    Sleep(20); /* a second arrival comes at a later millisecond than the first */
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

static void Stall(void)
{
    const float goal[2] = {500, 0}, at[2] = {0, 0}, byGoal[2] = {490, 0};
    float walk[2] = {0, 0};
    CoXlTestLwStallLines(0);
    g_case = "an online squad still for 30 s with a goal 500 m away: one line, counted";
    CoXlTestLwStall(1, at, goal, 1000);
    CoXlTestLwStall(0, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 30000) == 0 && Count("still for 30 s") == 0);
    CHECK(CoXlTestLwStall(0, at, goal, 31001) == 1 && Count("still for 30 s") == 1);
    CHECK(Count("still for 30 s (calm), 500 m from its goal (500, 0, 0.0)") == 1);
    g_case = "still for another 30 s: counted again, no second line in the same stretch";
    CHECK(CoXlTestLwStall(0, at, goal, 61001) == 2 && Count("still for 30 s") == 1);
    g_case = "a walking squad (4 m per 10 s) is never still";
    CoXlTestLwStall(1, walk, goal, 1000);
    for (int k = 1; k <= 12; k++) { walk[0] = 4.0f * k; CoXlTestLwStall(0, walk, goal, 1000 + 10000u * k); }
    CHECK(CoXlTestLwStall(0, walk, goal, 1000 + 10000u * 12) == 0 && Count("still for 30 s") == 1);
    g_case = "within 15 m of its goal it has arrived, not stalled";
    CoXlTestLwStall(1, byGoal, goal, 1000);
    CoXlTestLwStall(0, byGoal, goal, 1000);
    CHECK(CoXlTestLwStall(0, byGoal, goal, 40000) == 0 && Count("still for 30 s") == 1);
    g_case = "goal-less and rushing squads standing still are noted apart, not counted as calm stalls";
    CoXlTestLwStall(2, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 32000) == 0 && CoXlTestLwStallOther() == 1 && Count("still for 30 s (calm, no goal sent), no goal sent") == 1);
    CoXlTestLwStall(3, at, goal, 1000);
    CHECK(CoXlTestLwStall(0, at, goal, 32000) == 0 && CoXlTestLwStallOther() == 1 && Count("still for 30 s (rushing)") == 1);
    g_case = "at most 20 such lines a mission, apart from the calm ones' 40";
    for (int k = 0; k < 30; k++) { CoXlTestLwStall(2, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000); CoXlTestLwStall(0, at, goal, 32000); }
    CHECK(Count("still for 30 s (calm, no goal sent)") + Count("still for 30 s (rushing)") == 20);
    g_case = "at most 40 lines a mission";
    for (int k = 0; k < 60; k++) { CoXlTestLwStall(1, at, goal, 1000); CoXlTestLwStall(0, at, goal, 1000); CoXlTestLwStall(0, at, goal, 32000); }
    CHECK(Count("still for 30 s (calm)") == 40);
    CHECK(Count("(the last such line this mission)") == 3); /* the held-why's, the other stalls' and this one */
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
    Stall();
    if (g_failed) { printf("lwreinforce: %d checks failed\n", g_failed); return 1; }
    printf("lwreinforce: all cases passed\n");
    return 0;
}
