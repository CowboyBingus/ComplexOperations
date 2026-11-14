/* ---- Roads: the worn track in the ground's materials (game-free) --------------------------------------------------
 * A graded road has no surface of its own, so nobody can see where it runs. The track is a share of one material
 * slot mixed into the material maps along the centre lines: whole on a core an FRV's width, fading out over as much
 * again, with an edge that wanders and breaks up, so it reads as ground worn by use and not as a laid strip.
 * Every value is a function of world metres, the road points and integer hash noise, in doubles with + - * / and
 * floor only, so each peer computes the same track. tests/roadtrack_test.c pins it to the design's model. */
#define TRACK_HALF 1.2       /* m: the bare core to either side of the centre line (an FRV is 2.71 m wide) */
#define TRACK_FEATHER 1.5    /* m: the fade from bare to untouched */
#define TRACK_WANDER 0.5     /* m: how far the core's edge wanders */
#define TRACK_WANDER_WL 14.0
#define TRACK_GRAIN 0.35     /* the fine noise's share inside the fade: tufts and scuffs, none on the core */
#define TRACK_GRAIN_WL 1.7
#define TRACK_WEAR 0.12      /* what a less worn stretch loses of the core's weight */
#define TRACK_WEAR_WL 37.0
#define TRACK_REACH (TRACK_HALF + TRACK_WANDER + TRACK_FEATHER) /* 3.2 m: inside the bed the grading flattens (4 m) */
#define TRACK_MEANDER 0.5    /* m an axis: the line itself wanders, so the track meanders and does not only pulse in width */
#define TRACK_MEANDER_WL 25.0
#define TRACK_MEANDER_MOST (TRACK_MEANDER * 1.4142136) /* with TRACK_REACH 3.91 m: still inside the flattened bed */
#define TRACK_FINE 1.0       /* m: a texel wider than this takes the far profile */
#define TRACK_FAR_REACH 3.6  /* m: the far profile's end */
#define TRACK_FAR_FADE 1.6   /* m: its ramp, whole from 2 m in */
#define TRACK_PAD_FADE 8.0   /* m: the track fades in over this from the edge of a place's flat ground */
#define TRACK_DITHER 3.0     /* m: how far a change of slot is pushed about along the road */
#define TRACK_DITHER_WL 6.0
#define TRACK_CELL 64.0
#define TRACK_SLOTS 32

typedef struct TrackNoise { double half, wear, grain; } TrackNoise;
/* The three noises of a point; none depends on the distance. */
static void TrackNoiseAt(double x, double y, TrackNoise* t)
{
#pragma STDC FP_CONTRACT OFF /* a fused multiply-add would differ from another peer's in the last bit */
    t->half = TRACK_HALF + TRACK_WANDER * (0.7 * NearMatNoise(x, y, TRACK_WANDER_WL, 31) + 0.3 * NearMatNoise(x, y, TRACK_WANDER_WL / 3.1, 32));
    t->wear = 1.0 - TRACK_WEAR * (0.5 + 0.5 * NearMatNoise(x, y, TRACK_WEAR_WL, 33));
    t->grain = NearMatNoise(x, y, TRACK_GRAIN_WL, 34);
}
static double TrackWeightWith(double d, const TrackNoise* t)
{
#pragma STDC FP_CONTRACT OFF
    if (d >= TRACK_REACH) return 0.0;
    double s = (d - t->half) / TRACK_FEATHER;
    s = s < 0.0 ? 0.0 : s > 1.0 ? 1.0 : s;
    double w = 1.0 - s * s * (3 - 2 * s);
    w *= t->wear;
    w += TRACK_GRAIN * t->grain * 4.0 * w * (1.0 - w); /* 0 where w is 0 or 1: the grain never draws a line of its own */
    return w < 0.0 ? 0.0 : w > 1.0 ? 1.0 : w;
}
/* The track's share at world point (x, y), d metres from the nearest centre line. */
static double TrackWeight(double d, double x, double y)
{
    if (d >= TRACK_REACH) return 0.0;
    TrackNoise t;
    TrackNoiseAt(x, y, &t);
    return TrackWeightWith(d, &t);
}
/* The point the distance is measured from: moved by a slow noise, so the worn line wanders about the road points'
 * straight chords. A warp of the plane has no side to flip where pieces meet, unlike an offset along a normal. */
static void TrackWarp(double x, double y, double* wx, double* wy)
{
#pragma STDC FP_CONTRACT OFF
    *wx = x + TRACK_MEANDER * NearMatNoise(x, y, TRACK_MEANDER_WL, 36);
    *wy = y + TRACK_MEANDER * NearMatNoise(x, y, TRACK_MEANDER_WL, 37);
}
/* The share for a texel of the level's 4 m map. The far terrain mixes four texels and cuts the blend near a half, so
 * a mean of the fine profile over a texel leaves gaps and beads along a road that crosses the lattice at an angle.
 * A ramp that is whole within 2 m keeps the mixed peak over 0.74 at every angle and offset. */
static double TrackCoarse(double d)
{
    if (d >= TRACK_FAR_REACH) return 0.0;
    const double c = (TRACK_FAR_REACH - d) / TRACK_FAR_FADE;
    return c > 1.0 ? 1.0 : c;
}

/* ---- the slot ----
 * The track shows ground the game's own path painter paints in the zones beside the roads, so it fits every biome
 * without a table of the mod's own. A zone type's painters: list pointer +0x60, count u32 +0x68 (game
 * 0xF4FC4E..0xF4FC8E); a painter is 0x18 bytes: material resource +0, variables +8, their count u32 +0x10; a variable
 * is 0x1C bytes: name hash +0, a float +4 for type 0 or +8 for type 1, the type u32 +0x18 (0xF48C76..0xF48D56).
 * The path painter's pixel shader writes weight channel `material`, and the weight merge names channel m as id m in
 * the map, so the variable's value is the slot. */
#define TRACK_PATH_PAINTER 0x8448ABC93DDACF85ull
#define TRACK_MATERIAL 0xEAC0B497u
#define TRACK_LIST_MOST 64u
typedef struct TrackPainters { uint8_t path[16], fill[48], paths, fills, bad; } TrackPainters;

/* A type's path slots and the slots its other painters (fills, slopes, heights, water) cover ground with, in list
 * order. 0 and no slots when the descriptor is not the reviewed shape. */
static int TrackTypePainters(uintptr_t descriptor, TrackPainters* out)
{
    uint64_t list = 0; uint32_t count = 0;
    memset(out, 0, sizeof *out);
    out->bad = 1;
    if (descriptor == 0 || SafeRead(descriptor + 0x60, &list, 8) || SafeRead(descriptor + 0x68, &count, 4) || count > TRACK_LIST_MOST || (count != 0 && list == 0)) return 0;
    TrackPainters found = {{0}, {0}, 0, 0, 0};
    for (uint32_t i = 0; i < count; i++)
    {
        uint64_t resource = 0, variables = 0; uint32_t n = 0;
        if (SafeRead((uintptr_t)list + 0x18ull * i, &resource, 8) || SafeRead((uintptr_t)list + 0x18ull * i + 8, &variables, 8) || SafeRead((uintptr_t)list + 0x18ull * i + 0x10, &n, 4)) return 0;
        if (n > TRACK_LIST_MOST || (n != 0 && variables == 0)) return 0;
        for (uint32_t v = 0; v < n; v++)
        {
            uint8_t var[0x1C]; uint32_t name, type; float value = 0.0f;
            if (SafeRead((uintptr_t)variables + 0x1Cull * v, var, sizeof var)) return 0;
            memcpy(&name, var, 4); memcpy(&type, var + 0x18, 4);
            if (name != TRACK_MATERIAL) continue;
            if (type <= 1) memcpy(&value, var + (type ? 8 : 4), 4);
            if (!(value >= 0.5f && value < (float)TRACK_SLOTS - 0.5f)) break;
            const uint8_t slot = (uint8_t)(value + 0.5f); /* as the shader rounds it */
            if (resource == TRACK_PATH_PAINTER) { if (found.paths < sizeof found.path) found.path[found.paths++] = slot; }
            else if (found.fills < sizeof found.fill) found.fill[found.fills++] = slot;
            break;
        }
    }
    *out = found;
    return 1;
}
static int TrackIsFill(const TrackPainters* p, uint8_t slot)
{
    for (uint32_t k = 0; p != NULL && k < p->fills; k++) if (p->fill[k] == slot) return 1;
    return 0;
}
/* The slot of a road between two zone types (a: the lower type; b: the other, NULL when both sides are one type or a
 * side has none): the first path slot that neither side also fills its ground with, so the track contrasts with
 * both; the first path slot when every one is a fill; 0 when neither side has a path painter. */
static uint32_t TrackSlotChoose(const TrackPainters* a, const TrackPainters* b)
{
    uint32_t first = 0;
    for (int side = 0; side < 2; side++)
    {
        const TrackPainters* p = side ? b : a;
        for (uint32_t k = 0; p != NULL && k < p->paths; k++)
        {
            if (first == 0) first = p->path[k];
            if (!TrackIsFill(a, p->path[k]) && !TrackIsFill(b, p->path[k])) return p->path[k];
        }
    }
    return first;
}
/* One slot a level, so a road is one surface from end to end: of the path slots beside the roads, the one that is
 * ground on neither side along the most road length (then the one that is a path slot along the most, then the
 * lower). a[i] / b[i] / metres[i]: a road's sides (b NULL as for TrackSlotChoose) and its length. 0: no path slot. */
static uint32_t TrackLevelSlot(const TrackPainters* const* a, const TrackPainters* const* b, const double* metres, uint32_t n, double* clear)
{
    double free_[TRACK_SLOTS] = {0}, own[TRACK_SLOTS] = {0}; uint8_t seen[TRACK_SLOTS] = {0};
    for (uint32_t i = 0; i < n; i++)
    {
        uint8_t path[TRACK_SLOTS] = {0};
        for (int side = 0; side < 2; side++)
        {
            const TrackPainters* p = side ? b[i] : a[i];
            for (uint32_t k = 0; p != NULL && k < p->paths; k++) path[p->path[k]] = seen[p->path[k]] = 1;
        }
        for (uint32_t s = 1; s < TRACK_SLOTS; s++)
        {
            if (path[s]) own[s] += metres[i];
            if (!TrackIsFill(a[i], (uint8_t)s) && !TrackIsFill(b[i], (uint8_t)s)) free_[s] += metres[i];
        }
    }
    uint32_t best = 0;
    for (uint32_t s = 1; s < TRACK_SLOTS; s++)
        if (seen[s] && (best == 0 || free_[s] > free_[best] || (free_[s] == free_[best] && own[s] > own[best]))) best = s;
    *clear = best ? free_[best] : 0.0;
    return best;
}
/* A road's slot under the level's: the level's, but where that is ground on both sides the road's own choice, so a
 * track through a zone that fills its ground with the level's slot can still be told from it. */
static uint32_t TrackRoadSlot(const TrackPainters* a, const TrackPainters* b, uint32_t level)
{
    if (level == 0 || a == NULL) return level;
    if (!TrackIsFill(a, (uint8_t)level) || (b != NULL && !TrackIsFill(b, (uint8_t)level))) return level;
    const uint32_t own = TrackSlotChoose(a, b);
    return own != 0 ? own : level;
}

/* ---- the mix ----
 * The ground gives up the track's share and the slot takes it; the caller packs the three largest as the game's
 * texel. room: the places id[] and w[] have. 0 when nothing was written. */
static int TrackMix(uint32_t* id, double* w, int* n, int room, uint32_t slot, double c)
{
    if (!(c > 0.0) || slot == 0 || slot >= TRACK_SLOTS) return 0;
    int q = 0;
    while (q < *n && id[q] != slot) q++;
    if (q == *n) { if (*n >= room) return 0; id[q] = slot; w[q] = 0.0; (*n)++; }
    if (c > 1.0) c = 1.0;
    for (int k = 0; k < *n; k++) w[k] *= 1.0 - c;
    w[q] += c;
    return 1;
}

/* ---- the pieces ----
 * Every pair of road points with its slot, in TRACK_CELL cells of centred metres. A piece is listed in each cell it
 * can matter in: the widest reach, the meander and the slot change's dither. */
#define TRACK_PAD (TRACK_FAR_REACH + TRACK_MEANDER_MOST + TRACK_DITHER)
typedef struct TrackSeg { float ax, ay, bx, by; uint32_t slot; } TrackSeg;
typedef struct TrackIndex { TrackSeg* seg; uint32_t n, room; uint32_t* start; uint32_t* item; int grid; double half; } TrackIndex;

static void TrackIndexFree(TrackIndex* t)
{
    free(t->seg); free(t->start); free(t->item);
    memset(t, 0, sizeof *t);
}
static int TrackIndexAdd(TrackIndex* t, float ax, float ay, float bx, float by, uint32_t slot)
{
    if (slot == 0 || slot >= TRACK_SLOTS) return 0;
    if (t->n == t->room)
    {
        const uint32_t room = t->room ? 2 * t->room : 4096;
        TrackSeg* seg = (TrackSeg*)realloc(t->seg, sizeof(TrackSeg) * (size_t)room);
        if (seg == NULL) return 0;
        t->seg = seg; t->room = room;
    }
    t->seg[t->n++] = (TrackSeg){ax, ay, bx, by, slot};
    return 1;
}
static int TrackCellOf(double v, double half, int grid)
{
    const int c = (int)floor((v + half) / TRACK_CELL);
    return c < 0 ? 0 : c >= grid ? grid - 1 : c;
}
/* The cells' lists, pieces in their own order within a cell. 0 without memory (the index is then empty). */
static int TrackIndexBuild(TrackIndex* t, double world)
{
    free(t->start); free(t->item); t->start = t->item = NULL;
    t->half = world / 2; t->grid = (int)ceil(world / TRACK_CELL);
    if (t->grid < 1 || t->grid > 1024 || t->n == 0) { t->grid = 0; return 0; }
    const size_t cells = (size_t)t->grid * (size_t)t->grid;
    uint32_t* start = (uint32_t*)calloc(cells + 1, 4); uint32_t* item = NULL;
    if (start == NULL) { t->grid = 0; return 0; }
    for (int fill = 0; fill < 2; fill++)
    {
        for (uint32_t k = 0; k < t->n; k++)
        {
            const TrackSeg* s = &t->seg[k];
            const int x0 = TrackCellOf(fmin(s->ax, s->bx) - TRACK_PAD, t->half, t->grid), x1 = TrackCellOf(fmax(s->ax, s->bx) + TRACK_PAD, t->half, t->grid);
            const int y0 = TrackCellOf(fmin(s->ay, s->by) - TRACK_PAD, t->half, t->grid), y1 = TrackCellOf(fmax(s->ay, s->by) + TRACK_PAD, t->half, t->grid);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    const size_t c = (size_t)y * (size_t)t->grid + (size_t)x;
                    if (fill) item[start[c]++] = k; else start[c + 1]++;
                }
        }
        if (fill) break;
        for (size_t c = 0; c < cells; c++) start[c + 1] += start[c];
        item = (uint32_t*)malloc(4 * (size_t)(start[cells] ? start[cells] : 1));
        if (item == NULL) { free(start); t->grid = 0; return 0; }
    }
    for (size_t c = cells; c > 0; c--) start[c] = start[c - 1];
    start[0] = 0;
    t->start = start; t->item = item;
    return 1;
}
static double TrackSegDistance(const TrackSeg* s, double x, double y)
{
#pragma STDC FP_CONTRACT OFF
    const double dx = (double)s->bx - s->ax, dy = (double)s->by - s->ay, l2 = dx * dx + dy * dy;
    double u = 0.0;
    if (l2 > 1e-9) { u = ((x - s->ax) * dx + (y - s->ay) * dy) / l2; u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u; }
    const double qx = s->ax + u * dx - x, qy = s->ay + u * dy - y;
    return sqrt(qx * qx + qy * qy);
}
/* The nearest centre line within `reach` of (x, y): 1 with its distance and the slot there. Where pieces of two
 * slots are near, the lower slot's distance is moved by a noise before the two are compared: the slot then changes
 * in a ragged patch along the road, never on a line across it. tests (or NULL) counts the pieces measured. */
static int TrackNearest(const TrackIndex* t, double x, double y, double reach, double* d, uint32_t* slot, uint32_t* tests)
{
    if (t->grid == 0 || x < -t->half || y < -t->half || x >= t->half || y >= t->half) return 0;
    const size_t c = (size_t)TrackCellOf(y, t->half, t->grid) * (size_t)t->grid + (size_t)TrackCellOf(x, t->half, t->grid);
    double d1 = 1e30, d2 = 1e30; uint32_t s1 = 0, s2 = 0;
    for (uint32_t k = t->start[c]; k < t->start[c + 1]; k++)
    {
        const TrackSeg* s = &t->seg[t->item[k]];
        const double e = TrackSegDistance(s, x, y);
        if (tests != NULL) (*tests)++;
        if (s->slot == s1) { if (e < d1) d1 = e; }
        else if (e < d1) { d2 = d1; s2 = s1; d1 = e; s1 = s->slot; }
        else if (e < d2) { d2 = e; s2 = s->slot; }
    }
    if (!(d1 < reach)) return 0;
    *d = d1; *slot = s1;
    if (s2 != 0 && d2 < d1 + TRACK_DITHER)
    {
        const double push = TRACK_DITHER * NearMatNoise(x, y, TRACK_DITHER_WL, 35);
        const double low = (s1 < s2 ? d1 : d2) + push, high = s1 < s2 ? d2 : d1;
        *slot = low < high ? (s1 < s2 ? s1 : s2) : (s1 < s2 ? s2 : s1);
    }
    return 1;
}

/* ---- the places ----
 * A place's flat ground keeps its own surface: the track fades in over TRACK_PAD_FADE from the flat part's edge, so
 * it reaches every place. The fade is on the distance to the flat part, not on the pad's weight: a pad's band is 30
 * to 136 m wide and would end a spur's track 50 m short of the place it leads to. */
typedef struct TrackFlat { float cx, cy, c, s, a, b; int rect; } TrackFlat; /* centred metres; a circle's radius is a */
typedef struct TrackFlats { TrackFlat* flat; uint32_t n, room; uint32_t* start; uint32_t* item; int grid; double half; } TrackFlats;
static void TrackFlatsFree(TrackFlats* t)
{
    free(t->flat); free(t->start); free(t->item);
    memset(t, 0, sizeof *t);
}
static int TrackFlatsAdd(TrackFlats* t, const TrackFlat* f)
{
    if (t->n == t->room)
    {
        const uint32_t room = t->room ? 2 * t->room : 1024;
        TrackFlat* flat = (TrackFlat*)realloc(t->flat, sizeof(TrackFlat) * (size_t)room);
        if (flat == NULL) return 0;
        t->flat = flat; t->room = room;
    }
    t->flat[t->n++] = *f;
    return 1;
}
static double TrackFlatDistance(const TrackFlat* f, double x, double y)
{
#pragma STDC FP_CONTRACT OFF
    const double dx = x - f->cx, dy = y - f->cy;
    if (!f->rect) { const double r = sqrt(dx * dx + dy * dy) - f->a; return r > 0.0 ? r : 0.0; }
    double lx = fabs(dx * f->c + dy * f->s) - f->a, ly = fabs(-dx * f->s + dy * f->c) - f->b;
    lx = lx > 0.0 ? lx : 0.0; ly = ly > 0.0 ? ly : 0.0;
    return sqrt(lx * lx + ly * ly);
}
/* 1 with no flat ground: the share is 1 everywhere. 0 without memory. */
static int TrackFlatsBuild(TrackFlats* t, double world)
{
    free(t->start); free(t->item); t->start = t->item = NULL;
    t->half = world / 2; t->grid = (int)ceil(world / TRACK_CELL);
    if (t->grid < 1 || t->grid > 1024 || t->n == 0) { t->grid = 0; return 1; }
    const size_t cells = (size_t)t->grid * (size_t)t->grid;
    uint32_t* start = (uint32_t*)calloc(cells + 1, 4); uint32_t* item = NULL;
    if (start == NULL) { t->grid = 0; return 0; }
    for (int fill = 0; fill < 2; fill++)
    {
        for (uint32_t k = 0; k < t->n; k++)
        {
            const TrackFlat* f = &t->flat[k];
            const double r = (f->rect ? sqrt((double)f->a * f->a + (double)f->b * f->b) : f->a) + TRACK_PAD_FADE;
            const int x0 = TrackCellOf(f->cx - r, t->half, t->grid), x1 = TrackCellOf(f->cx + r, t->half, t->grid);
            const int y0 = TrackCellOf(f->cy - r, t->half, t->grid), y1 = TrackCellOf(f->cy + r, t->half, t->grid);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    const size_t c = (size_t)y * (size_t)t->grid + (size_t)x;
                    if (fill) item[start[c]++] = k; else start[c + 1]++;
                }
        }
        if (fill) break;
        for (size_t c = 0; c < cells; c++) start[c + 1] += start[c];
        item = (uint32_t*)malloc(4 * (size_t)(start[cells] ? start[cells] : 1));
        if (item == NULL) { free(start); t->grid = 0; return 0; }
    }
    for (size_t c = cells; c > 0; c--) start[c] = start[c - 1];
    start[0] = 0;
    t->start = start; t->item = item;
    return 1;
}
/* What the places leave of the track at a point: 0 on flat ground, 1 from TRACK_PAD_FADE off every flat part. */
static double TrackFlatShare(const TrackFlats* t, double x, double y)
{
    if (t->grid == 0 || x < -t->half || y < -t->half || x >= t->half || y >= t->half) return 1.0;
    const size_t c = (size_t)TrackCellOf(y, t->half, t->grid) * (size_t)t->grid + (size_t)TrackCellOf(x, t->half, t->grid);
    double least = TRACK_PAD_FADE;
    for (uint32_t k = t->start[c]; k < t->start[c + 1]; k++)
    {
        const double d = TrackFlatDistance(&t->flat[t->item[k]], x, y);
        if (d < least) least = d;
    }
    const double u = least / TRACK_PAD_FADE;
    return u * u * (3 - 2 * u);
}
