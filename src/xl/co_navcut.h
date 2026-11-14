/* Havok's navmesh face cutter (exe 0x133B950..0x1343940) cuts one face by all the silhouettes on it in a plane sweep.
 * A half-edge names its twin in 14 bits (written 0x13408B8, read signed at 0x1341D3B and 0x1341F3C), so a cut of more
 * than 8192 half-edges writes before its edge array (0x1341D45) and reads before the output table (0x1341F5B: crash
 * exe+0x1341F5B, and the garbage edge of exe+0xC87FB6). An extra large map's faces are large enough for that under
 * a dense forest. A face takes silhouettes up to a segment budget; a cut that still passes the limit is done again
 * with the face alone, which leaves the face whole (uncut), as for a face no silhouette touches. */
#ifndef CO_NAVCUT_H
#define CO_NAVCUT_H

#include "co.h"

#define CO_NAVCUT_FACE 0x133C650u    /* exe: (state, instance, up*, u8 flip, u32 key, hkArray* vertices): resets, adds the face */
#define CO_NAVCUT_KEY 0x133C620u     /* exe: (state, u32 key): resets, no face; a cut whose face has no generator (0xCB1C21, 0xC24481) */
#define CO_NAVCUT_ADD 0x133B9A0u     /* exe: (state, points, i32 count, i32 material) -> 1: one silhouette's segments */
#define CO_NAVCUT_STEP 0x1341710u    /* exe: (state) -> 1: one event of the sweep; the loop 0x133BB30 runs it until the queue is 1 */
#define CO_NAVCUT_HEAP 0x133CD50u    /* exe: (queue): builds the heap over the queue's events (0x133BA8C before the loop) */
#define CO_NAVCUT_RESET 0x133D450u   /* exe: (state): the four counts and the materials 0, the queue 1 (finished) */
#define CO_NAVCUT_QUEUE 0x90u        /* state: the event queue (hkArray; size 1 is empty) */
/* The sweep's status: the list of segments under the sweep line, linked in the segment records (+0x16, +0x18). Only
 * the constructor (0x133D135) and the step's link (0x1341940) write it, and a whole sweep leaves it empty. */
#define CO_NAVCUT_STATUS 0xA0u       /* state: u16 first and u16 last segment of the status, 0xFFFF for none */
#define CO_NAVCUT_ORIGIN 0x80u       /* state: the face frame's origin in the world (0x133BD43) */
#define CO_NAVCUT_SEGMENTS 0xD8u     /* state: segments (0x30 bytes each, index i16) */
#define CO_NAVCUT_FACES 0xE8u        /* state: output polygons */
#define CO_NAVCUT_EDGES 0xF8u        /* state: half-edges (8 bytes: u16 vertex, u16 next, 2 bits kind, 14 bits index) */
#define CO_NAVCUT_VERTICES 0x108u    /* state: vertices */
#define CO_NAVCUT_MAX 8192u          /* the most half-edges whose indexes fit the 14 signed bits (0..8191) */
/* Run 4's overflow made 10223 half-edges of 5599 segments (1.83 each): 3584 segments leave a fifth of the limit for
 * the crossings of overlapping silhouettes. */
#define CO_NAVCUT_BUDGET 3584u

typedef struct CoNavCutSeen
{
    uint64_t uncut;       /* cuts over the limit, done again with the face alone */
    uint64_t hidden;      /* cuts over the limit with no face to add again: the face is cut away whole */
    uint64_t partial;     /* faces that took silhouettes only up to the budget */
    uint64_t leftOut;     /* silhouettes left out of those faces */
    uint32_t largest;     /* the most half-edges any cut held after a step, the stopped ones too */
    uint32_t worstEdges, worstSegments; /* the last stopped cut: its half-edges and segments when it was stopped */
    float worstAt[2];     /* and its face frame's origin */
    /* A face alone should end its sweep as 1 polygon (counted before the clean-up 0x1341BF0, which only removes).
     * The faces no silhouette was added to are the engine's own measure of that. */
    uint64_t againEnded, againOdd; /* cuts done again whose sweep ended, and those not with 1 polygon */
    uint64_t aloneEnded, aloneOdd; /* the same for faces that took no silhouette and were never stopped */
    uint32_t oddFaces, oddEdges;   /* the last cut done again that ended otherwise: its polygons and half-edges */
} CoNavCutSeen;

/* Hooks the two starts of a cut, the silhouette add and the sweep's step. NULL, or why the step is not hooked. */
const char* co_navcut_install(uintptr_t exe);
void co_navcut_seen(CoNavCutSeen* out);
#if defined(CO_RESEARCH) || defined(CO_TEST)
/* A research build's lower limits, so a run reaches both paths on any face: clamped to [16, CO_NAVCUT_MAX]. */
void co_navcut_limits(uint32_t edges, uint32_t segments);
#endif

#ifdef CO_TEST
void co_navcut_test_reset(void);
/* The engine's heap build and reset, which the hooks call by address. */
void co_navcut_test_calls(void (*heap)(void* queue), void (*reset)(void* state));
#endif

#endif
