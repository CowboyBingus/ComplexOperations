/* The research build's FRV drive, game-free: a leg of road points to follow, the aim along it, the tilt read from the
 * vehicle's rotation, and what one drive adds up to. The game side (proto/living_world_frv.c) spawns the vehicle,
 * feeds these the pose it reads every frame and writes the lines. */
#ifndef CO_FRV_H
#define CO_FRV_H

#include <stddef.h>
#include <stdint.h>

#define CO_FRV_PATH 768u      /* points a leg holds: 6 km of road at the generator's 8 m */
#define CO_FRV_STOP_SPEED 0.5f /* m/s: slower than this under throttle is standing */
#define CO_FRV_STOP_SECONDS 1.0f
#define CO_FRV_STUCK_SECONDS 5.0f
#define CO_FRV_STUCK_METRES 1.0f /* less road than this gained in CO_FRV_STUCK_SECONDS under throttle: stuck */

typedef struct CoFrvPath { float xy[CO_FRV_PATH][2], at[CO_FRV_PATH]; uint32_t count; } CoFrvPath; /* at: metres along */

/* The point of pts[first..end) nearest (x, y) and its distance; `end` when the range is empty. */
uint32_t co_frv_nearest(const float (*pts)[2], uint32_t first, uint32_t end, float x, float y, float* distance);
/* A leg: the road's points from `from`, upwards (step 1) or downwards (step -1) within [first, end), until `metres`
 * of road are in it or the road ends. Points that repeat the one before are left out. Its count (under 2: no leg). */
uint32_t co_frv_leg(const float (*pts)[2], uint32_t first, uint32_t end, uint32_t from, int step, float metres, CoFrvPath* out);

typedef struct CoFrvAim
{
    float target[2]; /* the point `ahead` metres further along the leg */
    float along;     /* metres of the leg behind the vehicle */
    float cross;     /* metres off the leg's line, left of it positive */
    float error;     /* radians from the vehicle's heading to the target, counter-clockwise (left) positive */
    uint32_t station; /* the leg's point the vehicle last passed */
} CoFrvAim;
/* Where the vehicle is on the leg and where to steer. *segment is the search's start and is moved on, never back, so
 * a leg that passes itself is followed in order. heading: a unit vector across the ground. */
void co_frv_aim(const CoFrvPath* p, uint32_t* segment, const float* pos, const float* heading, float ahead, CoFrvAim* out);
/* The point `along` metres down the leg, clamped to its ends (a leg of under 2 points: untouched). */
void co_frv_point(const CoFrvPath* p, float along, float* xy);
/* The steering input for an aim error: the game's sign (-1 turns left, +1 right), full lock at `full` radians. */
float co_frv_steer(float error, float full);
/* The throttle for a wanted speed: full below it, none 1 m/s over it; the wanted speed falls with the aim error. */
float co_frv_throttle(float speed, float want, float error);

/* Whether shift-up is pressed this frame: the game's gears are 0 reverse, 1 neutral, 2 and up forwards, and with the
 * player's gears set to manual nothing shifts for the throttle. One press a frame is one gear, so a press waits
 * CO_FRV_SHIFT_SECONDS for the last one to show in the gear read. */
#define CO_FRV_SHIFT_SECONDS 0.5f
int co_frv_shift(int gear, float throttle, float sincePress);

/* Whether the steering input's sign is the game's, learnt from the drive itself: each frame's input times the turn it
 * got (radians, counter-clockwise positive) is added up while the vehicle moves. 0 undecided, 1 as assumed (a positive
 * input turns right), -1 the other way round: the caller then flips what it writes. */
typedef struct CoFrvSign { float sum; int verdict; } CoFrvSign;
int co_frv_sign(CoFrvSign* v, float steer, float turned, float speed);

/* The vehicle's up vector (its z axis) and its y axis across the ground (unit; 0, 0 when it points up or down) from
 * its rotation quaternion (x, y, z, w). */
void co_frv_axes(const float* q, float* up, float* ahead);
/* Pitch (nose up positive) and roll (right side down positive) in degrees, about the direction of travel `dir` (a
 * unit vector across the ground), from the up vector alone: no model axis but up is assumed. */
void co_frv_tilt(const float* up, const float* dir, float* pitch, float* roll);

typedef struct CoFrvSample
{
    float t, pos[3], speed, gameSpeed, pitch, roll, steer, throttle, along, cross, headingOff;
    int wheels, wheelCount, gear; uint32_t station, stations; float legMetres;
} CoFrvSample;
typedef struct CoFrvStats
{
    uint32_t samples, stops, stuck, airborne, upsideDown;
    int fewestWheels, stopped, stuckNow;
    float t0, t1, distance, topSpeed, pitchLow, pitchHigh, rollLow, rollHigh, farthestOff, along;
    float last[3], slowSince, markT, markAlong, stuckAt[2];
} CoFrvStats;
void co_frv_stats_start(CoFrvStats* s);
/* One sample (every frame the drive runs). Returns 1 when this sample began a stuck event. */
int co_frv_stats_add(CoFrvStats* s, const CoFrvSample* a);

/* The 100 ms pose line and the drive's last line, as the harness script reads them. Their lengths. */
int co_frv_pose_text(char* out, size_t size, const CoFrvSample* a);
int co_frv_summary_text(char* out, size_t size, const CoFrvStats* s, const char* leg, float legMetres, const char* ended, int drove);

#endif
