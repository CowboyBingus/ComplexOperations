/* The level's roads for the living world. The generator draws each road as a line of points about 8 m apart (the route
 * edges' point ranges); the squads' route copy knew only the edges' ends, so a squad "on a road" walked the straight
 * chord between two junctions, hundreds of metres from the road it stood for. Here: the points gathered for a nearness
 * test, and each road cut into stretches for route nodes, so squads walk the road itself. */
#ifndef CO_LW_ROADS_H
#define CO_LW_ROADS_H

#include <stdint.h>

#define CO_LW_ROADS_POINTS 131072u /* twice the generator's u16 point ids */
#define CO_LW_ROADS_CELL 64.0f     /* m: the nearness grid's cell; a reach up to this is exact */
#define CO_LW_ROADS_GRID 256       /* cells across: 16384 m */
#define CO_LW_ROADS_STRETCH 100.0f /* m of road between two route nodes */

typedef struct CoLwRoads
{
    float pts[CO_LW_ROADS_POINTS][2], low; uint32_t count;
    uint32_t start[CO_LW_ROADS_GRID * CO_LW_ROADS_GRID + 1], order[CO_LW_ROADS_POINTS];
} CoLwRoads;

/* The nearness grid over the `count` points already in r->pts, for a square level from `low` up (both axes). */
void co_lw_roads_index(CoLwRoads* r, uint32_t count, float low);
/* 1 when a road point lies within `reach` metres of (x, y); reach at most CO_LW_ROADS_CELL. */
int co_lw_roads_near(const CoLwRoads* r, float x, float y, float reach);
/* A road's points first..end (exclusive) cut into stretches of about `stretch` metres: the points where a stretch
 * ends, in order, into out (at most max); none nearer than half a stretch along the road to its last point. Its count. */
uint32_t co_lw_roads_cut(const float (*pts)[2], uint32_t first, uint32_t end, float stretch, uint32_t* out, uint32_t max);

#endif
