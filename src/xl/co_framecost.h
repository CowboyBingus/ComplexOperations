/* Wall-clock frame times on a complex mission, every build (issue F: stutter near a terminid nest). The mod's
 * game-thread tick (co_xl.c CoXlTick) runs once a frame, so QPC from one tick to the next is the frame; the game's own
 * dt is clamped and cannot show a stutter. Each 10 s window of play logs the lows, the slow frames, the PC's CPU load
 * against the game's share (a build on the same PC starves the game), the timed parts (CoXlTick's and the level update
 * hook's), the AI count and the nearest garrison with living spawners (bug holes on a terminid map). */
#ifndef CO_FRAMECOST_H
#define CO_FRAMECOST_H

#include <stdint.h>

/* The timed parts: CoXlTick's, in its order, then the level update hook's (difficulty_world_move.c MoveLevelUpdate), then
 * the nav section swaps the game's cell tick runs through the living world's hooks (living_world.c LwNavUnload / LwNavLoad),
 * then the rest of that tick (LwNavSched to its empty pop): the job starts and each completion's load and connect. */
enum
{
    CO_FC_STATUS, CO_FC_STREAM, CO_FC_SPANS, CO_FC_NET, CO_FC_TDIAG, CO_FC_TPROBE, CO_FC_LIVING, CO_FC_DAYLIGHT,
    CO_FC_TAC, CO_FC_DENSITY, CO_FC_SCATTER, CO_FC_MOVE, CO_FC_GROUNDKEEP, CO_FC_FARGRASS, CO_FC_NAVSWAP, CO_FC_NAVTICK, CO_FC_PARTS
};

#define CO_FC_WINDOW_MS 10000.0
#define CO_FC_FRAMES 4096                /* frame times kept a window: 400 fps for 10 s */
#define CO_FC_TARGET_MS 16.7             /* a frame longer than this is under 60 fps: the count the target is judged by */
#define CO_FC_SLOW_MS 25.0               /* a visible stutter at the 60 fps target */
#define CO_FC_HITCH_MS 33.3
#define CO_FC_LONG_MS 1000.0             /* a frame of a second or more in play: a build or a stall */
#define CO_FC_STARVED_IDLE 1.0           /* cores left idle under which the game had to wait for one */
#define CO_FC_SLOW_LINES_WINDOW 2        /* detailed slow-frame lines a window: its slowest, at its close */
#define CO_FC_DETAIL_LINES_SESSION 600   /* detailed slow-frame lines a session */
#define CO_FC_WINDOW_LINES_MISSION 1080  /* window lines a mission: three hours of play */

/* At window close: the AI units now, and the distance from player 1 to the nearest garrison with living spawners
 * (negative when none is known). */
typedef void (*CoFcContextFn)(int* ai, float* nest);
void co_framecost_context(CoFcContextFn fn);

/* At the start of CoXlTick: closes the frame since the last tick when `play` then and now. `mission` changes with
 * each mission; a new one gets a new window-line budget. */
void co_framecost_begin(int play, long mission);
/* After a part of CoXlTick: the time since the last mark goes to `part`. */
void co_framecost_mark(int part);
/* A part timed outside CoXlTick: `t = co_framecost_now(); ...; co_framecost_add(part, t)`. On another thread than
 * CoXlTick's the time is counted apart, as off the game thread. Returns the part's ticks. */
int64_t co_framecost_now(void);
int64_t co_framecost_add(int part, int64_t since);
/* A part timed inside another: its ticks go to `inner` and come off `outer`. */
void co_framecost_nested(int inner, int outer, int64_t since);
/* What a slow frame's line says beside its parts, so a frame the parts do not explain can be told apart: the game
 * thread's waits for the AI step, the engine's world update, and whether a far-grass bake job was out. */
enum { CO_FX_JOIN, CO_FX_WORLD, CO_FX_COUNT };
void co_framecost_extra(int which, int64_t since);
/* Around the engine's world update, on the game thread: its time is CO_FX_WORLD's, and what is timed inside it (a
 * part, a join) comes off it for the causes below. A frame that starts inside it gets its share on each side. */
void co_framecost_world(int enter);
/* What a frame over CO_FC_TARGET_MS is counted under, a window: the timed parts, then the AI joins, the world update
 * less what was timed inside it, and the rest of the frame. Two slow-frame lines a window cannot show a cause that
 * costs the third slowest frame or one of 17 to 25 ms; these counters hold every such frame. */
enum { CO_FC_CAUSE_JOIN = CO_FC_PARTS, CO_FC_CAUSE_WORLD, CO_FC_CAUSE_REST, CO_FC_CAUSES };
#define CO_FC_CAUSE_OTHERS 2             /* parts named in the causes line beside the four it always names */
/* A far-grass bake job was out in this frame: its slow-frame line says so, and a frame without a streamer change is
 * kept out of the quiet frames and their longest of 32 (counted apart, with its world update), so a bake's batch does
 * not hold the change frames' budget at its floor. */
void co_framecost_grass(int out);
/* The streamer added or removed stamp bodies in this frame (its change step, run before the world update). Each window
 * then splits the world update's own time between such frames and the quiet ones (the frame right after a change
 * frame is neither: it is counted apart), once a mission had one, and a slow frame's line says whether it was one:
 * those hold a broad phase rebuild by design. */
void co_framecost_bodies(int changed);
#define CO_FC_BODIES_ROOM 1              /* co_framecost_bodies: a change frame whose budget had room under the target */
#define CO_FC_BODIES_FLOOR 2             /* ... one at its floor: the quiet frames and the rebuild left none */
/* This frame is kept out of the quiet frames and the change frames alike (a moving tile's 0x63 render, about 185 ms,
 * drained in it): one such frame would hold every change frame's budget at its floor for the next 32 quiet frames. */
void co_framecost_apart(void);
/* Quiet frames the streamer's change-frame budget looks back over: a frame is over the longest of the 32 before it
 * 1 time in 33 when frames are alike, about 0.3 s at 100 fps */
#define CO_FC_RECENT 32
/* The frame just closed, for the streamer's change-frame budget (CoXlTick's thread): the longest whole time of the last
 * CO_FC_RECENT frames without a change (0 until that many in this play), their world update's own time (a running
 * mean, 0 until one), and that frame's own world update time. Its co_framecost_bodies value. */
int co_framecost_recent(double* quietHi, double* quietWorld, double* lastWorld);
/* What a change frame did (co_framecost_bodies says it was one): each window splits the change frames into removal
 * only, spawn only and both. */
void co_framecost_change(int removed, int spawned);
/* All of the above at once: quietHi without the frames a far-grass bake job was out in (they are kept out of the quiet
 * frames, as co_framecost_apart's), quietHiGrass with them (the rule before), and the frame just closed: its whole
 * time (0 until one closed in this play), co_framecost_bodies value, change kind (1 removal, 2 spawn, 3 both), and
 * whether a far-grass job was out in it or it was kept apart.
 * A frame without a change right after a change frame is an after-change frame (lastAfter, with that frame's kind in
 * lastAfterKind): its world update runs 2.5-4.8 ms over the change frame's (run 2c), so it is kept out of the quiet
 * frames, their longest of 32 and their world update, as the change frames are, and counted apart by that kind.
 * pairWorld is the two frames' world update together, pairClean that neither had a far-grass job or was kept apart. */
typedef struct
{
    double quietHi, quietHiGrass, quietWorld, lastWorld, lastWhole, pairWorld;
    int lastBodies, lastKind, lastGrass, lastApart, lastAfter, lastAfterKind, pairClean;
} CoFcRecent;
void co_framecost_recent_all(CoFcRecent* r);

#ifdef CO_TEST
/* The same on a given clock (ticks of `freq` a second) and given CPU times (100 ns units). */
void co_framecost_test_reset(int64_t freq);
void co_framecost_test_cpu(uint64_t systemBusy, uint64_t systemAll, uint64_t process, int cores);
void co_framecost_test_begin(int64_t now, int play, long mission);
void co_framecost_test_mark(int part, int64_t now);
int64_t co_framecost_test_add(int part, int64_t since, int64_t now);
void co_framecost_test_nested(int inner, int outer, int64_t since, int64_t now);
void co_framecost_test_extra(int which, int64_t since, int64_t now);
void co_framecost_test_world(int enter, int64_t now);
/* The last closed window: frames over 16.7 ms, how many each cause led (the largest), and how many were over only
 * with it (16.7 ms or less without it; not kept for the rest). */
void co_framecost_test_causes(uint32_t* over, uint32_t led[CO_FC_CAUSES], uint32_t only[CO_FC_CAUSES]);
/* The window being filled, its slow frame `k` (0 the slowest): its ms beside the AI joins, the world update and the far-grass flag. */
double co_framecost_test_slow(int k, double* join, double* world, int* grass);
/* Whether that frame was a streamer change frame. */
int co_framecost_test_slow_bodies(int k);
/* The last window's change frames' whole time: mean, the longest, and how many were over the target. */
void co_framecost_test_bodies_whole(double* mean, double* most, uint32_t* over);
/* The last window's change frames whose budget had room, and how many of those were over the target. */
void co_framecost_test_bodies_room(uint32_t* room, uint32_t* over);
/* The last window's frames kept apart. */
uint32_t co_framecost_test_apart(void);
/* The last window's change frames at their budget's floor: how many, their whole mean and the longest. */
void co_framecost_test_bodies_floor(uint32_t* floors, double* mean, double* most);
int co_framecost_test_window_lines(void);
/* The last closed window's split: frames with a change, their mean and longest world update (own time), the same for
 * the frames without, and how many of those were over CO_FC_TARGET_MS. 0 when the window had no split line. */
int co_framecost_test_bodies(uint32_t* changed, double* changedMean, double* changedMost, uint32_t* quiet, double* quietMean, double* quietMost, uint32_t* quietOver);
int co_framecost_test_detail_lines(void);
/* The last closed window: change frames by kind (index 1 removal only, 2 spawn only, 3 both) and their world update
 * mean; frames without a change with a far-grass job out and their world update mean; change frames with one out. */
void co_framecost_test_kinds(uint32_t frames[4], double world[4], uint32_t* grass, double* grassWorld, uint32_t* bodyGrass);
/* The last closed window: after-change frames by the change frame's kind (index 0 a change frame of no kind), their
 * world update mean and longest, and how many were over the target; the change frames by kind and budget (index 1
 * with room or, a removal frame, chosen; 2 at the floor or forced) and how many of those were over the target. */
void co_framecost_test_after(uint32_t frames[4], double world[4], double most[4], uint32_t over[4]);
void co_framecost_test_kind_over(uint32_t frames[4][3], uint32_t over[4][3]);
/* The last closed window: 1% and 0.1% lows (fps), frames over 25 ms, 33 ms and 1 s, the CPU verdict. */
void co_framecost_test_last(double* low1, double* low01, uint32_t* over25, uint32_t* over33, uint32_t* over1s, int* starved);
#endif

#endif
