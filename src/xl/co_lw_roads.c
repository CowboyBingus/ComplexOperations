/* The level's roads for the living world (co_lw_roads.h). */
#include <math.h>
#include <string.h>

#include "co_lw_roads.h"

static int RoadsCell(const CoLwRoads* r, float v)
{
    const float c = floorf((v - r->low) / CO_LW_ROADS_CELL);
    return c < 0 ? 0 : c >= (float)CO_LW_ROADS_GRID ? CO_LW_ROADS_GRID - 1 : (int)c;
}

void co_lw_roads_index(CoLwRoads* r, uint32_t count, float low)
{
    if (count > CO_LW_ROADS_POINTS) count = CO_LW_ROADS_POINTS;
    r->count = count; r->low = low;
    memset(r->start, 0, sizeof r->start);
    for (uint32_t i = 0; i < count; i++) r->start[RoadsCell(r, r->pts[i][1]) * CO_LW_ROADS_GRID + RoadsCell(r, r->pts[i][0]) + 1]++;
    for (uint32_t c = 0; c < CO_LW_ROADS_GRID * CO_LW_ROADS_GRID; c++) r->start[c + 1] += r->start[c];
    /* a counting sort: each cell's points end up in order[start[c] .. start[c + 1]) */
    static uint32_t fill[CO_LW_ROADS_GRID * CO_LW_ROADS_GRID];
    memcpy(fill, r->start, sizeof fill);
    for (uint32_t i = 0; i < count; i++) r->order[fill[RoadsCell(r, r->pts[i][1]) * CO_LW_ROADS_GRID + RoadsCell(r, r->pts[i][0])]++] = i;
}

int co_lw_roads_near(const CoLwRoads* r, float x, float y, float reach)
{
    if (r->count == 0 || !isfinite(x) || !isfinite(y)) return 0;
    if (reach > CO_LW_ROADS_CELL) reach = CO_LW_ROADS_CELL;
    const int cx = RoadsCell(r, x), cy = RoadsCell(r, y);
    for (int gy = cy - 1; gy <= cy + 1; gy++) for (int gx = cx - 1; gx <= cx + 1; gx++)
    {
        if (gx < 0 || gy < 0 || gx >= CO_LW_ROADS_GRID || gy >= CO_LW_ROADS_GRID) continue;
        const uint32_t c = (uint32_t)(gy * CO_LW_ROADS_GRID + gx);
        for (uint32_t k = r->start[c]; k < r->start[c + 1]; k++)
        {
            const float dx = r->pts[r->order[k]][0] - x, dy = r->pts[r->order[k]][1] - y;
            if (dx * dx + dy * dy <= reach * reach) return 1;
        }
    }
    return 0;
}

uint32_t co_lw_roads_cut(const float (*pts)[2], uint32_t first, uint32_t end, float stretch, uint32_t* out, uint32_t max)
{
    if (end <= first + 1 || !(stretch > 0)) return 0;
    float total = 0;
    for (uint32_t i = first + 1; i < end; i++) total += hypotf(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
    uint32_t n = 0; float run = 0, done = 0;
    for (uint32_t i = first + 1; i + 1 < end && n < max; i++)
    {
        const float step = hypotf(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
        run += step; done += step;
        if (run >= stretch && total - done >= 0.5f * stretch) { out[n++] = i; run = 0; }
    }
    return n;
}

int co_lw_roads_step(const uint16_t* next, int degree, const uint8_t* kind, int from, uint32_t rnd)
{
    int ways = 0;
    for (int k = 0; k < degree; k++) if (kind[next[k]] && next[k] != from) ways++;
    if (ways == 0) return -1;
    int pick = (int)(rnd % (uint32_t)ways);
    for (int k = 0; k < degree; k++) if (kind[next[k]] && next[k] != from && pick-- == 0) return next[k];
    return -1;
}
