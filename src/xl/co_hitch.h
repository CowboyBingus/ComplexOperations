/* The game thread's frames while a reinforcement hellpod comes in. A respawn sweeps the camera up to 1 km in about a
 * second; what the world does on the way (stamps out and in, far shadows redrawn, squads) shows as frame time only in
 * the 10 s note's slowest frame. From each pod's start for CO_HITCH_WATCH_MS this keeps every frame's time and the
 * mod's own share of it, and the caller logs the slowest frames once the watch is over. */
#ifndef CO_HITCH_H
#define CO_HITCH_H

#include <stdint.h>

#define CO_HITCH_WATCH_MS 8000u /* a pod's sweep, its landing and the first seconds on the ground */
#define CO_HITCH_KEEP 8         /* the slowest frames kept per watch */
#define CO_HITCH_WATCHES 12     /* watches logged per level: a bounded log however often a player dies */

/* The mod's own work in one game frame (ms), and what it did. */
typedef struct CoHitchCost
{
    float stream, window, spawn, living, tiles; /* streamer scan with its removals, stamp window scan, spawn pass, living world, moving tiles */
    uint16_t removed, entered, sunWhole;        /* stamp units destroyed, window entries, far-shadow slices drawn whole */
} CoHitchCost;

typedef struct CoHitchFrame { float ms; uint32_t atMs; CoHitchCost cost; } CoHitchFrame;

typedef struct CoHitch
{
    long level;
    uint32_t watches;              /* begun this level */
    int on;
    uint64_t startMs;
    uint32_t frames, over16, over25, over33, over50, removed, sunWhole;
    float sum, own;                /* frame time and the mod's share over the watch (ms) */
    CoHitchFrame top[CO_HITCH_KEEP]; /* slowest first */
} CoHitch;

/* A level's start: no watch, the count of watches from 0 (level numbers as g_world.levels counts them). */
void co_hitch_level(CoHitch* h, long level);
/* A pod started at `nowMs`: a new watch, unless one is on or the level had its CO_HITCH_WATCHES. 1 when it began. */
int co_hitch_arm(CoHitch* h, uint64_t nowMs);
/* One game frame of `ms` ending at `nowMs` with the mod's `cost`. 1 when the watch is over with this frame: its figures
 * stay in `h` for the caller's note until the next arm. */
int co_hitch_frame(CoHitch* h, uint64_t nowMs, float ms, const CoHitchCost* cost);
/* The mod's share of one frame (ms). */
float co_hitch_own(const CoHitchCost* cost);

#endif
