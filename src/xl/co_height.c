#include "co_height.h"

#include <math.h>
#include <stdio.h>

/* The kept layer heightfield's layout, as the drop point and the near terrain read it: cell = (w + world / 2) / step,
 * row 0 at +y. Checked against the user's logs: a landed vehicle's z against the dumped field (0.2 m apart). */
static int Cell(const CoHeightField* f, float x, float y, int64_t* c, int64_t* r)
{
    if (f == NULL || f->h == NULL || f->columns == 0 || f->rows == 0 || !(f->world > 0)) return 0;
    float step = f->world / (float)f->columns, half = f->world * 0.5f;
    float fc = floorf((x + half) / step), fr = floorf((y + half) / step);
    if (!(fc >= 0) || !(fr >= 0) || fc >= (float)f->columns || fr >= (float)f->rows) return 0;
    *c = (int64_t)fc;
    *r = (int64_t)f->rows - 1 - (int64_t)fr;
    return 1;
}

float co_height_at(const CoHeightField* f, float x, float y)
{
    int64_t c, r;
    if (!Cell(f, x, y, &c, &r)) return NAN;
    float v = f->h[(size_t)r * f->columns + (size_t)c];
    return isfinite(v) ? v : NAN;
}

int co_height_scan(const CoHeightField* f, float x, float y, float radius, uint32_t stride, uint32_t fieldStride, CoHeightScan* out)
{
    int64_t pc, pr;
    if (out == NULL || !Cell(f, x, y, &pc, &pr)) return 0;
    if (stride == 0) stride = 1;
    if (fieldStride == 0) fieldStride = 1;
    float lo = INFINITY, hi = -INFINITY;
    for (uint32_t r = 0; r < f->rows; r += fieldStride)
        for (uint32_t c = 0; c < f->columns; c += fieldStride)
        {
            float v = f->h[(size_t)r * f->columns + c];
            if (!isfinite(v)) continue;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    if (!(lo <= hi)) return 0;
    float step = f->world / (float)f->columns, half = f->world * 0.5f;
    int64_t span = (int64_t)ceilf(radius / step);
    float top = -INFINITY, topX = x, topY = y;
    for (int64_t dr = -span; dr <= span; dr += stride)
        for (int64_t dc = -span; dc <= span; dc += stride)
        {
            int64_t r = pr + dr, c = pc + dc;
            if (r < 0 || c < 0 || r >= (int64_t)f->rows || c >= (int64_t)f->columns) continue;
            float wx = ((float)c + 0.5f) * step - half, wy = ((float)(f->rows - 1 - r) + 0.5f) * step - half;
            if ((wx - x) * (wx - x) + (wy - y) * (wy - y) > radius * radius) continue;
            float v = f->h[(size_t)r * f->columns + (size_t)c];
            if (isfinite(v) && v > top) { top = v; topX = wx; topY = wy; }
        }
    out->lo = lo;
    out->hi = hi;
    out->here = co_height_at(f, x, y);
    out->top = isfinite(top) ? top : out->here;
    out->topX = topX;
    out->topY = topY;
    return 1;
}

int co_height_line(char* buf, size_t len, long level, const CoHeightLimits* lim, const CoHeightScan* s, float x, float y, float radius)
{
    if (buf == NULL || len == 0 || lim == NULL || s == NULL) return -1;
    /* The lowest height anything is stopped at from above: the game sets no other (the zone kill height is a floor). */
    float ceiling = lim->physicsHalf < lim->netHalf ? lim->physicsHalf : lim->netHalf;
    int n = snprintf(buf, len,
        "difficulty: world height (level %ld): the game's limits: units die below %.0f m, the physics world ends at +-%.0f m, "
        "network positions at +-%.0f m: no ceiling under %.0f m; the ground %.1f .. %.1f m (%.0f m under that ceiling); "
        "at the player (%.0f, %.0f) %.1f m, within %.0f m it rises to %.1f m (%.1f m above) at (%.0f, %.0f)",
        level, (double)lim->killFloor, (double)lim->physicsHalf, (double)lim->netHalf, (double)ceiling, (double)s->lo, (double)s->hi,
        (double)(ceiling - s->hi), (double)x, (double)y, (double)s->here, (double)radius, (double)s->top, (double)(s->top - s->here),
        (double)s->topX, (double)s->topY);
    return n < 0 || (size_t)n >= len ? -1 : n;
}
