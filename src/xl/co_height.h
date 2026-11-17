/* Where an extra large level's ground stands against the heights the game enforces. The game has no ceiling of its own
 * below the physics world's edge, but flight mods that cap their height relative to where they took off meet the
 * 16 km relief: one line per level gives the limits, the ground's range and how far it rises around the player. */
#ifndef CO_HEIGHT_H
#define CO_HEIGHT_H

#include <stddef.h>
#include <stdint.h>

/* A level heightfield: columns x rows floats over a square world of `world` metres centred on (0, 0), row 0 at +y. */
typedef struct CoHeightField
{
    const float* h;
    uint32_t columns, rows;
    float world;
} CoHeightField;

/* The heights the game enforces on the level (metres, world z). */
typedef struct CoHeightLimits
{
    float killFloor;   /* units below it die (zone type +0x12C, lowered on an extra large level) */
    float physicsHalf; /* the broadphase is a cube of this half size centred on (0, 0, 0): bodies past it leave the world */
    float netHalf;     /* network positions are clamped to +-this on each axis */
} CoHeightLimits;

typedef struct CoHeightScan
{
    float lo, hi;     /* the whole field */
    float here;       /* at the point */
    float top;        /* the highest ground within the radius */
    float topX, topY; /* where it is */
} CoHeightScan;

/* The sample under (x, y), or NAN outside the field or on a non-finite sample. */
float co_height_at(const CoHeightField* f, float x, float y);
/* The field's range (every `fieldStride`-th sample each way), the ground at (x, y) and the highest ground within
 * `radius` of it (every `stride`-th sample). 0 when the field is empty or the point lies outside it. */
int co_height_scan(const CoHeightField* f, float x, float y, float radius, uint32_t stride, uint32_t fieldStride, CoHeightScan* out);
/* The level's note: limits, the ground's range and the rise around the player. The length written, or -1 when it does
 * not fit. */
int co_height_line(char* buf, size_t len, long level, const CoHeightLimits* lim, const CoHeightScan* s, float x, float y, float radius);

#endif
