/* The Havok AI step's anatomy and the navmesh queue by kind, two 30 s lines (living_world_step.c). The step 0xBD1890
 * runs one at a time on a job thread or, when the join pops it, on the game thread: its run start 0xBD1380, drive
 * 0xBD04A0, commit 0xBD05E0 and navigators 0xBD0BB0 are timed apart, so a join far over the budget names its part.
 * Ticks are QueryPerformanceCounter's; no game call and no clock here. */
#ifndef CO_STEPSTAT_H
#define CO_STEPSTAT_H

#include <stddef.h>
#include <stdint.h>

/* The step's four calls in its order (P 0xBD1963, 0xBD1997, 0xBD19C9, 0xBD1A38). */
enum { CO_STEP_START, CO_STEP_DRIVE, CO_STEP_COMMIT, CO_STEP_NAV, CO_STEP_PARTS };
/* Where a step ran: the game thread (the join popped its task), another thread, or another AI world's step (not the
 * mission's: timed so its calls are not counted outside a step, not summed). */
enum { CO_STEP_ON_GAME, CO_STEP_ON_OTHER, CO_STEP_ON_FOREIGN, CO_STEP_ONS };
/* The run's navmesh changes by type 7..11: the five arrays ApplyAsyncWorldUpdates (0xC958E0) fills. */
#define CO_STEP_FIRST_TYPE 7
#define CO_STEP_TYPES 5
#define CO_STEP_PERIOD_MS 30000u
/* No frame for this long is no play (the ship, a load): the window starts again, the mission's line count stays. */
#define CO_STEP_GAP_MS 60000u
#define CO_STEP_LINES 400u /* line pairs a mission: over three hours */

typedef struct CoStepOne { int64_t ticks[CO_STEP_PARTS]; uint32_t calls[CO_STEP_PARTS]; int64_t over; } CoStepOne;
typedef struct CoStepSum { uint32_t n; int64_t sum, most; } CoStepSum;
typedef struct CoStepWin
{
    uint32_t on[CO_STEP_ONS], atOnce;
    CoStepSum whole, part[CO_STEP_PARTS], outside[CO_STEP_PARTS];
    int64_t over;    /* the drive's longest time past the budget it was given */
    CoStepOne worst; /* the slowest step's parts; its own time is whole.most */
} CoStepWin;
/* One path request (0xC97D50) on its thread: its yields block until the next drive, so they are not its work. */
typedef struct CoStepReq { int64_t mark, yielded, stretch; } CoStepReq;
typedef struct CoStepPaths { uint32_t n; int64_t own, stretch; } CoStepPaths;
typedef struct CoStepQueue
{
    uint32_t added[2], removed[2], runs, types[CO_STEP_TYPES]; /* [1]: on the game thread */
    int32_t gens, gensMost;                                    /* the AI world's generators [H+0x230], under 0 not read */
} CoStepQueue;
typedef struct CoStepClock { uint64_t last, at; long mission; uint32_t lines; int seen; } CoStepClock;
enum { CO_STEP_IDLE, CO_STEP_FRESH, CO_STEP_DUE, CO_STEP_LAST, CO_STEP_QUIET };

/* One call of `part` inside a step took `ticks`. The drive also gets the time past `seconds`, its budget. */
void co_stepstat_part(CoStepOne* one, int part, int64_t ticks);
void co_stepstat_drive(CoStepOne* one, int64_t ticks, float seconds, int64_t freq);
/* A step of `whole` ticks ended `on` a thread (CO_STEP_ON_*); `atOnce` another step was in flight when it began. */
void co_stepstat_step(CoStepWin* w, const CoStepOne* one, int64_t whole, int on, int atOnce);
/* A call of `part` made outside any step (the mod's nav swaps, the game's own waits): counted apart. */
void co_stepstat_outside(CoStepWin* w, int part, int64_t ticks);

/* A path request from `now`; each yield inside it from `enter` to `leave`; its end gives its own work in ticks and
 * leaves in r->stretch the longest time it ran between two yields (what one request adds past a drive's deadline). */
void co_stepstat_req_begin(CoStepReq* r, int64_t now);
void co_stepstat_req_yield(CoStepReq* r, int64_t enter, int64_t leave);
int64_t co_stepstat_req_end(CoStepReq* r, int64_t start, int64_t now);
void co_stepstat_paths(CoStepPaths* p, int64_t own, int64_t stretch);

/* A cutter add (0xBE8840) or remove (0xBE8D10) pushed; `onGame` from the game thread. */
void co_stepstat_cutter(CoStepQueue* q, int remove, int onGame);
/* One run's ApplyAsyncWorldUpdates: the five counts before and after it; a count that went down adds nothing. */
void co_stepstat_applied(CoStepQueue* q, const int32_t before[CO_STEP_TYPES], const int32_t after[CO_STEP_TYPES]);
void co_stepstat_gens(CoStepQueue* q, int32_t now);

/* Once a frame: CO_STEP_FRESH drops the sums (a new mission, the first frame, or after a gap), CO_STEP_DUE writes the
 * lines, CO_STEP_LAST writes the mission's last pair, CO_STEP_QUIET drops a due window past the cap. */
int co_stepstat_tick(CoStepClock* c, long mission, uint64_t nowMs);
double co_stepstat_ms(int64_t ticks, int64_t freq);
/* The two lines' text after their greppable names; `paths` 0 when path requests are not measured. */
int co_stepstat_line1(char* out, size_t size, const CoStepWin* w, const CoStepPaths* p, int paths, int64_t freq);
int co_stepstat_line2(char* out, size_t size, const CoStepQueue* q, int queued);

#endif
