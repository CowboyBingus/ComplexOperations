/* A moving tile's build frames (proto/difficulty_world_move.c): the slow-frame line splits a frame over 40 ms at the
 * mod's marks (CoXlTick, the level update hook, t0, MoveTick's end, the job consumer, the next CoXlTick) into parts that
 * add up to the frame, at most 8 a build; the stall watch samples a stretch only once, past 40 ms, while a build runs, 4 a
 * build; the 0x63 step's wall frames are told apart by the split's state; the two long tile jobs go to queue 14 unless a
 * research build is switched to 10. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMoveFrameMark(int mark, double ms);
void CoXlTestMoveFrameState(int building, int mv, int ns, int op, int shape, int tile);
void CoXlTestMoveFrameConsumer(double in, double out);
int CoXlTestMoveFrameParts(double next, double* part);
uint32_t CoXlTestMoveFrameLines(int reset);
void CoXlTestMoveCostNote(void);
int CoXlTestMoveStallDue(int phase, double since, double now, LONG seq, LONG sampled, LONG samples, int building);
void CoXlTestMoveStallPhase(int phase, double ms, LONG* seq, int64_t* since);
LONG CoXlTestMoveStallSamples(LONG set);
int CoXlTestSplit63WallKind(int sp0, int sp1);
void CoXlTestSplit63WallAdd(double ms, int sp0, int sp1);
void CoXlTestSplit63WallNote(void);
void CoXlTestSplit63WallAddFence(double ms, double fence, int sp0, int sp1);
void CoXlTestMoveFrameFence(double in, double out);
void CoXlTestMoveFrameFenceTimed(int on);
void CoXlTestMoveFenceAdd(double wall, double fence, int mv, int op);
double CoXlTestMoveFrameFenceMs(void);
void CoXlTestNearBgQueue(int on, uint32_t* queue, uint32_t* priority);
void CoXlTestNearJobQueue(uint32_t* queue, uint32_t* priority);
void CoXlTestMoveJobEnd(int shape, int tile);
void CoXlTestNearAutoReset(void);
uint32_t CoXlTestNearAutoBuild(int shaped, int zero, long samples);
const char* CoXlTestMoveStallWait(uintptr_t a, uintptr_t exe, size_t exeSize, uintptr_t game, size_t gameSize);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { MF_TICK, MF_LEVEL, MF_T0, MF_T1, MF_TICK_END };
enum { MS_NONE, MS_EARLY, MS_LATE };
enum { SP_NONE, SP_WHOLE, SP_HIDING, SP_READY, SP_QUEUED, SP_RANGES, SP_SHOWING, SP_DONE, SP_RESTORE };
enum { S63W_BEFORE, S63W_OP, S63W_RANGES, S63W_SHOWING, S63W_LAST, S63W_OTHER };
#define MV_REPLAY 6
#define MV_READY 10

static int Near(double a, double b) { return fabs(a - b) < 0.01; }

/* One frame from `t`: tick, level update entry, t0, MoveTick's end at the given offsets (ms). */
static void Frame(double t, double level, double t0, double t1)
{
    CoXlTestMoveFrameMark(MF_TICK, t);
    CoXlTestMoveFrameMark(MF_LEVEL, t + level);
    CoXlTestMoveFrameMark(MF_T0, t + t0);
    CoXlTestMoveFrameMark(MF_T1, t + t1);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    double p[9];

    g_case = "the parts add up to the frame; after the mover ends at the consumer's last return";
    Frame(1000.0, 130.0, 131.0, 139.0);
    CoXlTestMoveFrameConsumer(1000.0 + 140.0, 1000.0 + 141.5);
    CoXlTestMoveFrameConsumer(1000.0 + 150.0, 1000.0 + 151.0);
    CHECK(CoXlTestMoveFrameParts(1000.0 + 165.5, p));
    CHECK(Near(p[0], 165.5) && Near(p[1], 130.0) && Near(p[2], 1.0) && Near(p[3], 8.0) && Near(p[4], 12.0) && Near(p[5], 2.5) && Near(p[6], 14.5));
    CHECK(Near(p[1] + p[2] + p[3] + p[4] + p[6], p[0]));
    CHECK(Near(p[7], 0.0)); /* no CoXlTick end mark */

    g_case = "CoXlTick's own time: from the tick to its end mark, inside the early update";
    CoXlTestMoveFrameMark(MF_TICK, 1500.0);
    CoXlTestMoveFrameMark(MF_TICK_END, 1500.0 + 3.5);
    CoXlTestMoveFrameMark(MF_LEVEL, 1500.0 + 20.0);
    CoXlTestMoveFrameMark(MF_T0, 1500.0 + 21.0);
    CoXlTestMoveFrameMark(MF_T1, 1500.0 + 22.0);
    CHECK(CoXlTestMoveFrameParts(1500.0 + 50.0, p));
    CHECK(Near(p[7], 3.5) && Near(p[1], 20.0));

    g_case = "a consumer call before MoveTick's end is not after the mover; no consumer: the rest is to the next tick";
    Frame(2000.0, 2.0, 3.0, 9.0);
    CoXlTestMoveFrameConsumer(2000.0 + 4.0, 2000.0 + 5.0);
    CHECK(CoXlTestMoveFrameParts(2000.0 + 50.0, p));
    CHECK(Near(p[4], 0.0) && Near(p[5], 0.0) && Near(p[6], 41.0) && Near(p[1] + p[2] + p[3] + p[4] + p[6], 50.0));

    g_case = "a frame without the level update has no parts";
    CoXlTestMoveFrameMark(MF_TICK, 3000.0);
    CHECK(!CoXlTestMoveFrameParts(3020.0, p));

    g_case = "a build frame over 40 ms is noted with its state, op and jobs at the next tick; a shape job that ended between the tick and t0 was out";
    g_fake.log[0] = 0;
    CoXlTestMoveFrameLines(1);
    CoXlTestMoveFrameMark(MF_TICK, 4000.0);
    CoXlTestMoveFrameMark(MF_TICK_END, 4000.0 + 2.0);
    CoXlTestMoveJobEnd(1, 0); /* the job's end before the level update: nothing in the frame sees it running */
    CoXlTestMoveFrameMark(MF_LEVEL, 4000.0 + 130.0);
    CoXlTestMoveFrameMark(MF_T0, 4000.0 + 131.0);
    CoXlTestMoveFrameMark(MF_T1, 4000.0 + 139.0);
    CoXlTestMoveFrameState(1, MV_READY, 6, 0x64, 0, 0);
    CoXlTestMoveFrameConsumer(4000.0 + 140.0, 4000.0 + 141.5);
    CoXlTestMoveFrameMark(MF_TICK, 4000.0 + 165.5);
    CHECK(fake_host_logged("difficulty: moving tiles: slow frame 165.5 ms (ready/near 6/op 0x64): early update 130.0 ms (CoXlTick 2.0, frame fence untimed), level update 1.0, mover 8.0, "
        "after the mover 2.5 (consumer 1.5), to the next tick 24.0; jobs out: shape 1, tile 0"));
    CHECK(CoXlTestMoveFrameLines(0) == 1);

    g_case = "a tile job that ends inside the frame was out; one that ended in the frame before was not";
    g_fake.log[0] = 0;
    CoXlTestMoveJobEnd(0, 1); /* the last frame's */
    Frame(4200.0, 50.0, 51.0, 52.0);
    CoXlTestMoveFrameState(1, MV_REPLAY, 2, 0x3C, 0, 0);
    CoXlTestMoveFrameMark(MF_TICK, 4200.0 + 60.0);
    CHECK(fake_host_logged("jobs out: shape 0, tile 0"));
    g_fake.log[0] = 0;
    Frame(4300.0, 50.0, 51.0, 52.0);
    CoXlTestMoveFrameState(1, MV_REPLAY, 2, 0x3C, 0, 0);
    CoXlTestMoveJobEnd(0, 1); /* after MoveTick, before the next tick */
    CoXlTestMoveFrameMark(MF_TICK, 4300.0 + 60.0);
    CHECK(fake_host_logged("jobs out: shape 0, tile 1"));

    g_case = "40 ms or less, or no build: no line";
    g_fake.log[0] = 0;
    Frame(5000.0, 10.0, 11.0, 12.0);
    CoXlTestMoveFrameState(1, MV_REPLAY, 2, 0x63, 0, 1);
    Frame(5039.0, 10.0, 11.0, 12.0); /* closes a 39 ms frame */
    CoXlTestMoveFrameState(0, 0, 7, -1, 0, 0);
    CoXlTestMoveFrameMark(MF_TICK, 5140.0); /* a 100 ms frame outside a build */
    CHECK(strstr(g_fake.log, "slow frame") == NULL && CoXlTestMoveFrameLines(0) == 3); /* the three above */

    g_case = "at most 8 a build; the build's end lets the next build note again";
    g_fake.log[0] = 0;
    for (int k = 0; k < 12; k++) { Frame(6000.0 + 100.0 * k, 50.0, 51.0, 52.0); CoXlTestMoveFrameState(1, MV_REPLAY, 2, 0x3C, 0, 1); }
    CoXlTestMoveFrameMark(MF_TICK, 6000.0 + 1200.0);
    CHECK(CoXlTestMoveFrameLines(0) == 8 && fake_host_logged("(replay/near 2/op 0x3C): early update 50.0 ms"));
    CoXlTestMoveCostNote();
    CHECK(CoXlTestMoveFrameLines(0) == 0);
    Frame(8000.0, 50.0, 51.0, 52.0); CoXlTestMoveFrameState(1, MV_REPLAY, 2, -1, 0, 0);
    CoXlTestMoveFrameMark(MF_TICK, 8100.0);
    CHECK(CoXlTestMoveFrameLines(0) == 1 && fake_host_logged("(replay/near 2/op 0xFF)"));

    g_case = "the frame fence (exe 0x8A7C4): its waits between the tick and the level update are the early update's; others not";
    g_fake.log[0] = 0;
    CoXlTestMoveFrameLines(1);
    CoXlTestMoveFrameFenceTimed(1);
    CoXlTestMoveFrameMark(MF_TICK, 9000.0);
    CHECK(Near(CoXlTestMoveFrameFenceMs(), 0.0));
    CoXlTestMoveFrameMark(MF_TICK_END, 9000.0 + 1.0);
    CoXlTestMoveFrameFence(9000.0 + 10.0, 9000.0 + 40.0);
    CoXlTestMoveFrameFence(9000.0 + 41.0, 9000.0 + 43.5);
    CHECK(Near(CoXlTestMoveFrameFenceMs(), 32.5));
    CoXlTestMoveFrameMark(MF_LEVEL, 9000.0 + 45.0);
    CoXlTestMoveFrameFence(9000.0 + 46.0, 9000.0 + 49.0); /* after the level update: not counted */
    CoXlTestMoveFrameMark(MF_T0, 9000.0 + 50.0);
    CoXlTestMoveFrameMark(MF_T1, 9000.0 + 52.0);
    CoXlTestMoveFrameState(1, MV_REPLAY, 2, 0x63, 0, 0);
    CHECK(CoXlTestMoveFrameParts(9000.0 + 60.0, p) && Near(p[8], 32.5) && Near(p[1], 45.0));
    CoXlTestMoveFrameMark(MF_TICK, 9000.0 + 60.0);
    CHECK(fake_host_logged("difficulty: moving tiles: slow frame 60.0 ms (replay/near 2/op 0x63): early update 45.0 ms (CoXlTick 1.0, frame fence 32.5), level update 5.0"));
    CHECK(Near(CoXlTestMoveFrameFenceMs(), 0.0)); /* the tick starts the next frame's */
    CoXlTestMoveFrameFenceTimed(0);
    CHECK(CoXlTestMoveFrameFenceMs() == -1.0);

    g_case = "the build's frame fence line: wall frames timed, the longest wait with its state, the slow frames' share; untimed frames left out";
    g_fake.log[0] = 0;
    CoXlTestMoveCostNote();
    CHECK(strstr(g_fake.log, "frame fence waits") == NULL); /* none timed */
    CoXlTestMoveFenceAdd(12.0, 3.0, MV_REPLAY, 0x3C);
    CoXlTestMoveFenceAdd(30.0, 22.0, MV_REPLAY, 0x63);
    CoXlTestMoveFenceAdd(20.0, 2.0, MV_READY, -1);
    CoXlTestMoveFenceAdd(90.0, -1.0, MV_READY, -1); /* untimed */
    g_fake.log[0] = 0;
    CoXlTestMoveCostNote();
    CHECK(fake_host_logged("difficulty: moving tiles: the build's frame fence waits (exe 0x8A7C4): 3 wall frames, 27 ms in all, the longest 22.0 ms (replay/op 0x63); "
        "the 2 wall frames over 16.7 ms: 24 of their 50 ms in the fence;"));
    g_fake.log[0] = 0;
    CoXlTestMoveCostNote();
    CHECK(strstr(g_fake.log, "frame fence waits") == NULL); /* cleared */

    g_case = "the stall watch: only the two stretches, past 40 ms, once a stretch, 4 a build, while a build runs";
    CHECK(CoXlTestMoveStallDue(MS_EARLY, 100.0, 141.0, 5, 4, 0, 1));
    CHECK(CoXlTestMoveStallDue(MS_LATE, 100.0, 141.0, 5, 4, 3, 1));
    CHECK(!CoXlTestMoveStallDue(MS_EARLY, 100.0, 139.0, 5, 4, 0, 1)); /* not yet 40 ms */
    CHECK(!CoXlTestMoveStallDue(MS_NONE, 100.0, 300.0, 5, 4, 0, 1));  /* inside the mod's marks */
    CHECK(!CoXlTestMoveStallDue(MS_EARLY, 100.0, 300.0, 5, 5, 0, 1)); /* this stretch sampled already */
    CHECK(!CoXlTestMoveStallDue(MS_EARLY, 100.0, 300.0, 5, 4, 4, 1)); /* the build's four taken */
    CHECK(!CoXlTestMoveStallDue(MS_EARLY, 100.0, 300.0, 5, 4, 0, 0)); /* no build */

    g_case = "each phase change moves the stretch's sequence and start";
    {
        LONG s0 = 0, s1 = 0; int64_t a = 0, b = 0;
        CoXlTestMoveStallPhase(MS_EARLY, 10.0, &s0, &a);
        CoXlTestMoveStallPhase(MS_NONE, 20.0, &s1, &b);
        CHECK(s1 == s0 + 1 && b > a);
        CoXlTestMoveStallSamples(3);
        CoXlTestMoveCostNote();
        CHECK(CoXlTestMoveStallSamples(-1) == 0);
    }

    g_case = "the 0x63 step's frames by the split's state at t0 and after MoveTick";
    CHECK(CoXlTestSplit63WallKind(SP_NONE, SP_HIDING) == S63W_BEFORE && CoXlTestSplit63WallKind(SP_HIDING, SP_HIDING) == S63W_BEFORE);
    CHECK(CoXlTestSplit63WallKind(SP_HIDING, SP_QUEUED) == S63W_OP && CoXlTestSplit63WallKind(SP_READY, SP_QUEUED) == S63W_OP);
    CHECK(CoXlTestSplit63WallKind(SP_NONE, SP_WHOLE) == S63W_OP);
    CHECK(CoXlTestSplit63WallKind(SP_QUEUED, SP_RANGES) == S63W_RANGES && CoXlTestSplit63WallKind(SP_RANGES, SP_RANGES) == S63W_RANGES);
    CHECK(CoXlTestSplit63WallKind(SP_RANGES, SP_SHOWING) == S63W_SHOWING && CoXlTestSplit63WallKind(SP_SHOWING, SP_SHOWING) == S63W_SHOWING);
    CHECK(CoXlTestSplit63WallKind(SP_SHOWING, SP_DONE) == S63W_LAST && CoXlTestSplit63WallKind(SP_RANGES, SP_DONE) == S63W_LAST);
    CHECK(CoXlTestSplit63WallKind(SP_WHOLE, SP_WHOLE) == S63W_OTHER && CoXlTestSplit63WallKind(SP_RESTORE, SP_RESTORE) == S63W_OTHER);

    g_case = "the 0x63 step's line: counts, the slowest of each kind, frames over 16.7 ms; then cleared";
    g_fake.log[0] = 0;
    CoXlTestSplit63WallAdd(35.0, SP_NONE, SP_HIDING);
    CoXlTestSplit63WallAdd(18.0, SP_HIDING, SP_HIDING);
    CoXlTestSplit63WallAdd(20.0, SP_HIDING, SP_QUEUED);
    for (int k = 0; k < 4; k++) CoXlTestSplit63WallAdd(14.0 + k, SP_RANGES, SP_RANGES);
    CoXlTestSplit63WallAdd(16.0, SP_RANGES, SP_SHOWING);
    CoXlTestSplit63WallAdd(28.0, SP_SHOWING, SP_DONE);
    CoXlTestSplit63WallNote();
    CHECK(fake_host_logged("difficulty: moving tiles: 0x63 step's wall-clock frames: 2 before the op (max 35.0 ms), the op's 1 (max 20.0), 4 of the ranges "
        "(max 17.0), 1 showing all (max 16.0), the last 1 (max 28.0), 0 other (max 0.0); 5 over 16.7 ms"));
    CHECK(strstr(g_fake.log, "frame fence") == NULL); /* none timed */
    g_fake.log[0] = 0;
    CoXlTestSplit63WallNote();
    CHECK(strstr(g_fake.log, "0x63 step's") == NULL);

    g_case = "the 0x63 step's fence line: each kind's longest frame with its own fence wait";
    g_fake.log[0] = 0;
    CoXlTestSplit63WallAddFence(28.0, 20.5, SP_NONE, SP_HIDING);
    CoXlTestSplit63WallAddFence(15.0, 1.0, SP_HIDING, SP_HIDING); /* not the longest: its fence is not shown */
    CoXlTestSplit63WallAddFence(17.0, 2.0, SP_HIDING, SP_QUEUED);
    CoXlTestSplit63WallAddFence(27.0, 0.5, SP_RANGES, SP_RANGES);
    CoXlTestSplit63WallNote();
    CHECK(fake_host_logged("difficulty: moving tiles: 0x63 step's longest wall-clock frames' frame fence (exe 0x8A7C4): before the op 20.5 of 28.0 ms, the op's 2.0 of 17.0, "
        "the ranges' 0.5 of 27.0, showing all 0.0 of 0.0, the last 0.0 of 0.0, other 0.0 of 0.0"));

    g_case = "the tile jobs' queue: 14 (2, 4) by default, noted once; a build without CO_RESEARCH reads no switch; 10 (1, 3) off";
    {
        uint32_t q = 0, pr = 0;
        CoXlTestNearBgQueue(0, &q, &pr);
        CHECK(q == 1 && pr == 3);
        CoXlTestNearBgQueue(1, &q, &pr);
        CHECK(q == 2 && pr == 4 && q + 3 * pr == 14);
        g_fake.log[0] = 0;
        SetEnvironmentVariableA("HELLFORGE_XL_NEAR_BGQUEUE", "0");
        CoXlTestNearBgQueue(-1, &q, &pr); /* read again: the research switch says 10 */
#ifdef CO_RESEARCH
        CHECK(q == 1 && pr == 3 && fake_host_logged("jobs go to queue 10, the regular workers' (HELLFORGE_XL_NEAR_BGQUEUE=0, research)"));
        SetEnvironmentVariableA("HELLFORGE_XL_NEAR_BGQUEUE", "1");
        CoXlTestNearBgQueue(-1, &q, &pr);
        CHECK(q == 2 && pr == 4);
#else
        CHECK(q == 2 && pr == 4 && fake_host_logged("difficulty: moving tiles: the composite tile and collision shape jobs go to queue 14, the LP workers'"));
        CHECK(strstr(g_fake.log, "queue 10") == NULL);
#endif
        g_fake.log[0] = 0;
        CoXlTestNearJobQueue(&q, &pr); /* read once a process: the second job's call notes nothing */
        CHECK(q == 2 && pr == 4 && g_fake.log[0] == 0);
        SetEnvironmentVariableA("HELLFORGE_XL_NEAR_BGQUEUE", NULL);
    }

    g_case = "auto: queue 10 until two builds stalled (0 frames) with samples, then 14 for good; a build keeps its queue";
    CoXlTestNearAutoReset();
    g_fake.log[0] = 0;
    CHECK(CoXlTestNearAutoBuild(1, 1, 0) == 10);  /* stalled, no sample */
    CHECK(CoXlTestNearAutoBuild(0, 0, 3) == 10);  /* no shape job: not counted */
    CHECK(CoXlTestNearAutoBuild(1, 1, 2) == 10);  /* 1 */
    CHECK(CoXlTestNearAutoBuild(1, 0, 1) == 10);  /* samples, no stall */
    CHECK(strstr(g_fake.log, "go to queue 14") == NULL);
    CHECK(CoXlTestNearAutoBuild(1, 1, 1) == 10);  /* 2: switches after this build */
    CHECK(fake_host_logged("the tile jobs go to queue 14 from the next build on (2 stalled): 4 builds on queue 10, 2 stalled (\"0 frames\") with stall samples, 0 counted since the last stall"));
    CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 14 && CoXlTestNearAutoBuild(1, 1, 4) == 14);

    g_case = "auto: four builds in a row with a shape job and no stall with samples: 14 from the fifth";
    CoXlTestNearAutoReset();
    for (int k = 0; k < 4; k++) CHECK(CoXlTestNearAutoBuild(1, 0, k) == 10);
    CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 14);

    g_case = "auto: the four count from the last stall: one stall, then four clean builds, switches; a stall restarts the count";
    CoXlTestNearAutoReset();
    CHECK(CoXlTestNearAutoBuild(1, 1, 1) == 10);                          /* stall 1 */
    for (int k = 0; k < 3; k++) CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 10);
    CHECK(CoXlTestNearAutoBuild(0, 0, 0) == 10);                          /* no shape job: not counted */
    CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 10);                          /* the 4th clean one */
    CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 14);
    CHECK(fake_host_logged("go to queue 14 from the next build on (4 clean since the last stall): 5 builds on queue 10, 1 stalled (\"0 frames\") with stall samples, 4 counted since the last stall"));
    CoXlTestNearAutoReset();
    for (int k = 0; k < 3; k++) CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 10);
    CHECK(CoXlTestNearAutoBuild(1, 1, 0) == 10);                          /* "0 frames" without samples: clean */
    CoXlTestNearAutoReset();
    for (int k = 0; k < 3; k++) CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 10);
    CHECK(CoXlTestNearAutoBuild(1, 1, 2) == 10);                          /* stall 1 restarts the count */
    for (int k = 0; k < 4; k++) CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 10);
    CHECK(CoXlTestNearAutoBuild(1, 0, 0) == 14);
    {
        uint32_t q = 0, pr = 0;
        CoXlTestNearBgQueue(0, &q, &pr); /* back to off for the cases after */
    }

    g_case = "the stall sample's wait: the frame loop's fence, a resource fence, a terrain destroy's, the game's generator job; other places none";
    {
        static uint8_t exe[0x200000], game[0x1000000];
        const uintptr_t e = (uintptr_t)exe, g = (uintptr_t)game;
        CHECK(strstr(CoXlTestMoveStallWait(e + 0x8A7C9u, e, sizeof exe, g, sizeof game), "frame loop's fence (exe 0x8A7C4)") != NULL);
        CHECK(strstr(CoXlTestMoveStallWait(e + 0x1A8D75u, e, sizeof exe, g, sizeof game), "release's fence") != NULL);
        CHECK(strstr(CoXlTestMoveStallWait(e + 0x1A844Eu, e, sizeof exe, g, sizeof game), "create's fence") != NULL);
        CHECK(strstr(CoXlTestMoveStallWait(e + 0x1ADFB8u, e, sizeof exe, g, sizeof game), "a terrain destroy's fence (exe 0x1ADFB3)") != NULL);
        CHECK(strstr(CoXlTestMoveStallWait(g + 0xF6E6E6u, e, sizeof exe, g, sizeof game), "generator job (game 0xF6E6E6)") != NULL);
        CHECK(CoXlTestMoveStallWait(g + 0xF6E6E0u, e, sizeof exe, g, sizeof game)[0] == 0 && CoXlTestMoveStallWait(e + 0x8A7C4u, e, sizeof exe, g, sizeof game)[0] == 0);
    }

    if (g_failed) { printf("slowframe: %d check(s) failed\n", g_failed); return 1; }
    printf("slowframe: all checks passed\n");
    return 0;
}
