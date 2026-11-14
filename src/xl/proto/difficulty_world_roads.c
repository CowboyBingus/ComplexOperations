/* ---- Road evidence: log lines only --------------------------------------------------------------------------------
 * The drivable-roads design rests on what these lines give per level: the route edges op 0x0E left without points, the
 * edge widths before and after the ops that set them, the region's road data, how far the major places lie from a drawn
 * path, and the stamp density op 0x2B makes. Nothing here writes game state; every read is a SafeRead, and each line is
 * written once per level, from the generating thread.
 *
 * What the counts follow (game.dll 25480438):
 * - An edge is 0x48 bytes: ends +0 / +8 (generator metres), width +0x30, type u8 +0x34, points [+0x38, +0x3C), flags
 *   u16 +0x44. Op 0x0E gives points to an edge of a type other than 2 and 8 without flag 2 (0xF38AB0..0xF38AC8); with
 *   too little of its pass's capacity (u32 record + 8 + 0x78, 0xF3868A) left, the edge keeps an empty range (0xF38F2F).
 * - Op 0x0F stamps a capsule of half the width per point pair, for an edge of a type other than 1, 2 and 8 without
 *   flag 2 whose first + 1 < end (0xC14EA0..0xC14ECA). The point id is a u16 (0xC14EFF): 65535 points at most.
 * - Op 0x08 gives a type-3 edge the width 2.0 (0xF3648B), before op 0x86; op 0x87 widens its convoy roads to 10 m
 *   with flags 0x18 (0xF8B25F..0xF8B26B).
 * - R = [[context + 8] + 0xB0]: op 0x41 paints an edge of type t with the material u32 [R + 0x5C + 4 t] (0xF50384), and
 *   op 0xB7 levels routes only when the byte R + 0x678 is 2 (0xFB8A59). */
#define ROADS_EDGES_MAX 65536u
#define ROADS_POINTS_READ (1u << 20) /* the most read back; the line names the u16 ids' limit beside the count */
#define ROADS_POINT_IDS 65535u
#define ROADS_GROUP_MAX 512u
#define ROADS_NONE 1.0e30f

typedef struct RoadEdge { float a[2], b[2], width; uint32_t first, end; uint16_t flags; uint8_t type; } RoadEdge;

static void RoadEdgeRead(const uint8_t* rec, RoadEdge* e)
{
    memcpy(e->a, rec, 8); memcpy(e->b, rec + 8, 8); memcpy(&e->width, rec + 0x30, 4);
    memcpy(&e->first, rec + 0x38, 4); memcpy(&e->end, rec + 0x3C, 4); memcpy(&e->flags, rec + 0x44, 2);
    e->type = rec[0x34];
}
static int RoadEdgeDrawable(const RoadEdge* e) { return e->type != 2 && e->type != 8 && !(e->flags & 2); }
/* a point pair inside the points written: op 0x0F's test, and a range that can be read */
static int RoadEdgeHasPoints(const RoadEdge* e, uint32_t points) { return e->end <= points && e->end > e->first && e->end - e->first >= 2; }
static double RoadEdgeKm(const RoadEdge* e) { double dx = e->b[0] - e->a[0], dy = e->b[1] - e->a[1]; return sqrt(dx * dx + dy * dy) / 1000.0; }

typedef struct RoadsPoints { uint32_t edges, drawable, empty, emptySkeleton; double drawableKm, emptyKm; } RoadsPoints;
static void RoadsPointsCount(const RoadEdge* e, uint32_t n, uint32_t points, RoadsPoints* out)
{
    memset(out, 0, sizeof *out);
    out->edges = n;
    for (uint32_t i = 0; i < n; i++)
    {
        if (!RoadEdgeDrawable(&e[i])) continue;
        out->drawable++; out->drawableKm += RoadEdgeKm(&e[i]);
        if (RoadEdgeHasPoints(&e[i], points)) continue;
        out->empty++; out->emptyKm += RoadEdgeKm(&e[i]);
        if (e[i].flags & 8) out->emptySkeleton++;
    }
}

/* "2.00 m x 547, 10.00 m x 3": the widths to the centimetre, ascending; past 8 widths the rest are counted together */
static void RoadsWidths(const RoadEdge* e, uint32_t n, char* out, size_t size)
{
    int32_t cm[8]; uint32_t count[8], other = 0; int kinds = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const float w = e[i].width;
        const int32_t c = w >= -1.0e6f && w <= 1.0e6f ? (int32_t)lroundf(w * 100.0f) : INT32_MIN; /* NaN and huge: one bin */
        int k = 0;
        while (k < kinds && cm[k] != c) k++;
        if (k < kinds) { count[k]++; continue; }
        if (kinds == 8) { other++; continue; }
        int at = kinds++;
        while (at > 0 && cm[at - 1] > c) { cm[at] = cm[at - 1]; count[at] = count[at - 1]; at--; }
        cm[at] = c; count[at] = 1;
    }
    size_t len = 0;
    out[0] = 0;
    for (int k = 0; k < kinds && len < size; k++)
        len += (size_t)(cm[k] == INT32_MIN ? snprintf(out + len, size - len, "%sunreadable x %u", k ? ", " : "", count[k])
                                           : snprintf(out + len, size - len, "%s%.2f m x %u", k ? ", " : "", cm[k] / 100.0, count[k]));
    if (other && len < size) len += (size_t)snprintf(out + len, size - len, ", other widths x %u", other);
    if (kinds == 0) snprintf(out, size, "none");
}

static float RoadsSegment(float x, float y, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (l2 > 1.0e-9f) { t = ((x - ax) * dx + (y - ay) * dy) / l2; t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t; }
    const float ex = x - ax - t * dx, ey = y - ay - t * dy;
    return sqrtf(ex * ex + ey * ey);
}
/* The nearer of `best` and the edge's drawn path: its points' segments, or with pts NULL the line between its ends. */
static float RoadsEdgeDistance(float x, float y, const RoadEdge* e, const float* pts, float best)
{
    if (pts == NULL) { const float d = RoadsSegment(x, y, e->a[0], e->a[1], e->b[0], e->b[1]); return d < best ? d : best; }
    for (uint32_t p = e->first + 1; p < e->end; p++)
    {
        const float d = RoadsSegment(x, y, pts[2 * (p - 1)], pts[2 * (p - 1) + 1], pts[2 * p], pts[2 * p + 1]);
        if (d < best) best = d;
    }
    return best;
}
typedef struct RoadsBox { float lo[2], hi[2]; } RoadsBox;
static float RoadsBoxDistance(float x, float y, const RoadsBox* b)
{
    const float dx = x < b->lo[0] ? b->lo[0] - x : x > b->hi[0] ? x - b->hi[0] : 0.0f;
    const float dy = y < b->lo[1] ? b->lo[1] - y : y > b->hi[1] ? y - b->hi[1] : 0.0f;
    return sqrtf(dx * dx + dy * dy);
}

typedef struct RoadsReach { uint32_t n; float pathMedian, pathMax, skelMedian, skelMax, lineMedian, lineMax; } RoadsReach;
static int RoadsFloatCompare(const void* a, const void* b) { const float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
/* The median (the upper of two) and the maximum of the distances found; -1 when there are none. */
static void RoadsSummary(float* d, uint32_t n, float* median, float* most)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) if (d[i] < ROADS_NONE) d[m++] = d[i];
    if (m == 0) { *median = -1.0f; *most = -1.0f; return; }
    qsort(d, m, sizeof *d, RoadsFloatCompare);
    *median = d[m / 2]; *most = d[m - 1];
}
/* xy: n places in generator metres (the first ROADS_GROUP_MAX count). The paths: edges with points. The skeleton: edges
 * with flag 8, along their points and as the straight lines op 0xB7 draws, points or not. */
static void RoadsReachOf(const float* xy, uint32_t n, const RoadEdge* e, uint32_t edges, const float* pts, uint32_t points, RoadsReach* out)
{
    static float path[ROADS_GROUP_MAX], skel[ROADS_GROUP_MAX], line[ROADS_GROUP_MAX];
    if (n > ROADS_GROUP_MAX) n = ROADS_GROUP_MAX;
    RoadsBox* box = edges ? (RoadsBox*)VirtualAlloc(NULL, (size_t)edges * sizeof *box, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : NULL;
    for (uint32_t k = 0; box != NULL && k < edges; k++)
    {
        RoadsBox* b = &box[k];
        b->lo[0] = b->lo[1] = ROADS_NONE; b->hi[0] = b->hi[1] = -ROADS_NONE;
        if (pts == NULL || !RoadEdgeHasPoints(&e[k], points)) continue;
        for (uint32_t p = e[k].first; p < e[k].end; p++)
            for (int c = 0; c < 2; c++) { const float v = pts[2 * p + c]; if (v < b->lo[c]) b->lo[c] = v; if (v > b->hi[c]) b->hi[c] = v; }
    }
    for (uint32_t i = 0; i < n; i++)
    {
        const float x = xy[2 * i], y = xy[2 * i + 1];
        float bp = ROADS_NONE, bs = ROADS_NONE, bl = ROADS_NONE;
        for (uint32_t k = 0; k < edges; k++)
        {
            if (e[k].flags & 8) bl = RoadsEdgeDistance(x, y, &e[k], NULL, bl);
            if (pts == NULL || !RoadEdgeHasPoints(&e[k], points)) continue;
            const float boxAway = box != NULL ? RoadsBoxDistance(x, y, &box[k]) : 0.0f; /* no point of the edge is nearer */
            if (boxAway < bp) bp = RoadsEdgeDistance(x, y, &e[k], pts, bp);
            if ((e[k].flags & 8) && boxAway < bs) bs = RoadsEdgeDistance(x, y, &e[k], pts, bs);
        }
        path[i] = bp; skel[i] = bs; line[i] = bl;
    }
    if (box != NULL) VirtualFree(box, 0, MEM_RELEASE);
    out->n = n;
    RoadsSummary(path, n, &out->pathMedian, &out->pathMax);
    RoadsSummary(skel, n, &out->skelMedian, &out->skelMax);
    RoadsSummary(line, n, &out->lineMedian, &out->lineMax);
}

/* The playable disc in km2: radius = world (u32 S + 0x2C) x the fraction (f32 S + 0x50) / 2, as LwPlayRadius reads it.
 * 0 when the values are not a world's. */
static double RoadsDiscKm2(uint32_t world, float fraction, double* radius)
{
    *radius = 0;
    if (world == 0 || world > 65536u || !(fraction > 0.0f) || fraction > 2.0f) return 0;
    *radius = world * 0.5 * fraction;
    return 3.14159265358979 * *radius * *radius / 1.0e6;
}

/* ---- what the level's generation block and context hold ---- */
typedef struct RoadsLayout { uint32_t edgesAt, pointsAt, locationsAt; } RoadsLayout;
static struct { LONG level; uint32_t done; } g_roads = {.level = -1};

/* The edges, parsed, in one allocation after their raw records; 0 when unreadable. */
static uint32_t RoadsReadEdges(uintptr_t block, const RoadsLayout* L, RoadEdge** out)
{
    uint32_t n = 0;
    *out = NULL;
    if (L->edgesAt == 0 || SafeRead(block + 0x11A71CCu, &n, 4) || n == 0 || n > ROADS_EDGES_MAX) return 0;
    uint8_t* raw = (uint8_t*)VirtualAlloc(NULL, (size_t)n * (0x48 + sizeof(RoadEdge)), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (raw == NULL) return 0;
    if (SafeRead(block + L->edgesAt, raw, (size_t)n * 0x48)) { VirtualFree(raw, 0, MEM_RELEASE); return 0; }
    RoadEdge* e = (RoadEdge*)(raw + (size_t)n * 0x48);
    for (uint32_t i = 0; i < n; i++) RoadEdgeRead(raw + (size_t)i * 0x48, &e[i]);
    *out = e;
    return n;
}
static void RoadsFreeEdges(RoadEdge* e, uint32_t n) { if (e != NULL) VirtualFree((uint8_t*)e - (size_t)n * 0x48, 0, MEM_RELEASE); }

static float* RoadsReadPoints(uintptr_t block, const RoadsLayout* L, uint32_t* count)
{
    uint32_t n = 0;
    *count = 0;
    if (L->pointsAt == 0 || SafeRead(block + 0x11A71D4u, &n, 4) || n == 0 || n > ROADS_POINTS_READ) return NULL;
    float* p = (float*)VirtualAlloc(NULL, (size_t)n * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL) return NULL;
    if (SafeRead(block + L->pointsAt, p, (size_t)n * 8)) { VirtualFree(p, 0, MEM_RELEASE); return NULL; }
    *count = n;
    return p;
}

static void RoadsWidthNote(uintptr_t block, const RoadsLayout* L, uint32_t op)
{
    RoadEdge* e = NULL;
    const uint32_t n = RoadsReadEdges(block, L, &e);
    if (n == 0) { Note("roads: level %ld after op 0x%02X: the route edges are not readable", (long)g_world.levels, op); return; }
    uint32_t skeleton = 0;
    for (uint32_t i = 0; i < n; i++) skeleton += (e[i].flags & 8) != 0;
    char widths[320];
    RoadsWidths(e, n, widths, sizeof widths);
    Note("roads: level %ld after op 0x%02X: edge widths %s (%u edges, %u with flag 0x8)", (long)g_world.levels, op, widths, n, skeleton);
    RoadsFreeEdges(e, n);
}

static void RoadsPointsNote(uintptr_t block, const RoadsLayout* L, uint64_t record)
{
    RoadEdge* e = NULL;
    const uint32_t n = RoadsReadEdges(block, L, &e);
    uint32_t points = 0, capacity = 0;
    if (n == 0 || SafeRead(block + 0x11A71D4u, &points, 4))
    { Note("roads: level %ld after op 0x0E: the route edges are not readable", (long)g_world.levels); RoadsFreeEdges(e, n); return; }
    SafeRead(record + 8 + 0x78, &capacity, 4);
    RoadsPoints c;
    RoadsPointsCount(e, n, points, &c);
    Note("roads: level %ld after op 0x0E: %u road points (the pass's capacity %u, the block's room %u, point ids reach %u); %u edges, %u drawable "
        "(%.1f km), %u of them without points (%.1f km; %u with flag 0x8)", (long)g_world.levels, points, capacity, co_gen_places(HF_GEN_ROUTE_POINTS),
        ROADS_POINT_IDS, c.edges, c.drawable, c.drawableKm, c.empty, c.emptyKm, c.emptySkeleton);
    RoadsFreeEdges(e, n);
}

static void RoadsRegionNote(uint64_t context)
{
    uint64_t settings = 0, region = 0;
    uint8_t levels = 0; float narrow = 0; uint32_t mat[9];
    if (SafeRead(context + 8, &settings, 8) || settings == 0 || SafeRead(settings + 0xB0, &region, 8) || region == 0 ||
        SafeRead(region + 0x678, &levels, 1) || SafeRead(region + 0x5C, mat, sizeof mat) || SafeRead(region + 0x938, &narrow, 4))
    { Note("roads: level %ld region: not readable", (long)g_world.levels); return; }
    Note("roads: level %ld region: +0x678 = %u (op 0xB7 levels routes at 2), +0x938 = %.3f; ribbon material by edge type: "
        "0:%08X 1:%08X 2:%08X 3:%08X 4:%08X 5:%08X 6:%08X 7:%08X 8:%08X", (long)g_world.levels, levels, (double)narrow,
        mat[0], mat[1], mat[2], mat[3], mat[4], mat[5], mat[6], mat[7], mat[8]);
}

/* ops[i]: the op that appended location i (g_locationOp), for the `total` locations */
static void RoadsReachNote(uintptr_t block, const RoadsLayout* L, const uint8_t* ops, uint32_t total)
{
    static const struct { uint8_t op; const char* name; } kGroups[3] = {{0x16, "objectives (op 0x16)"}, {0x82, "outposts (op 0x82)"},
        {0x17, "important locations (op 0x17)"}};
    static float xy[2 * ROADS_GROUP_MAX];
    RoadEdge* e = NULL;
    const uint32_t n = RoadsReadEdges(block, L, &e);
    uint32_t points = 0;
    float* pts = RoadsReadPoints(block, L, &points);
    if (n == 0 || L->locationsAt == 0)
        Note("roads: level %ld reach: the route edges or the locations are not readable", (long)g_world.levels);
    for (int g = 0; n != 0 && L->locationsAt != 0 && g < 3; g++)
    {
        uint32_t m = 0;
        for (uint32_t i = 0; i < total && m < ROADS_GROUP_MAX; i++)
            if (ops[i] == kGroups[g].op && !SafeRead(block + L->locationsAt + (uint64_t)i * 0xE0, &xy[2 * m], 8)) m++;
        RoadsReach r;
        RoadsReachOf(xy, m, e, n, pts, points, &r);
        Note("roads: level %ld reach: %u %s: nearest edge with points median %.0f m, max %.0f m; flag-0x8 edges along points median %.0f m, "
            "max %.0f m, as straight lines median %.0f m, max %.0f m", (long)g_world.levels, m, kGroups[g].name, (double)r.pathMedian, (double)r.pathMax,
            (double)r.skelMedian, (double)r.skelMax, (double)r.lineMedian, (double)r.lineMax);
    }
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
}

/* plan: the stamps an extra large level keeps beyond its room (TerrainStamps), op 0x2B's too */
static void RoadsDensityNote(uint64_t context, uint32_t before, uint32_t after, uint32_t plan)
{
    uint64_t settings = 0; uint32_t world = 0; float fraction = 0; double radius = 0;
    if (SafeRead(context + 8, &settings, 8) || settings == 0 || SafeRead(settings + 0x2C, &world, 4) || SafeRead(settings + 0x50, &fraction, 4)) world = 0;
    const double km2 = RoadsDiscKm2(world, fraction, &radius);
    const uint32_t placed = after >= before ? after - before : 0;
    if (km2 <= 0)
    {
        Note("roads: level %ld density: op 0x2B placed %u locations (%u -> %u) and %u in the stamp plan; the playable disc is not readable",
            (long)g_world.levels, placed, before, after, plan);
        return;
    }
    Note("roads: level %ld density: op 0x2B placed %u locations (%u -> %u) and %u in the stamp plan over the playable disc (world %u m, radius %.0f m, "
        "%.2f km2): %.1f per km2", (long)g_world.levels, placed, before, after, plan, world, radius, km2, (placed + (double)plan) / km2);
}

/* After a level's op ran to its end (the index moved on): the lines that op gives, each once per level. ops NULL: no
 * reach lines (this level's locations have no op recorded). */
static void RoadsAfterOpAt(uintptr_t block, const RoadsLayout* L, uint64_t context, uint64_t record, uint32_t op, uint32_t before, uint32_t after,
    const uint8_t* ops, uint32_t total, uint32_t plan)
{
    if (g_roads.level != g_world.levels) { g_roads.level = g_world.levels; g_roads.done = 0; }
    const uint32_t bit = op == 0x86 ? 1u : op == 0x0E ? 2u : op == 0x0F ? 4u : op == 0x2B ? 8u : 0u;
    if (bit == 0 || (g_roads.done & bit)) return;
    g_roads.done |= bit;
    if (op == 0x86 || op == 0x0F) RoadsWidthNote(block, L, op);
    if (op == 0x0E) RoadsPointsNote(block, L, record);
    if (op == 0x2B)
    {
        RoadsRegionNote(context);
        if (ops != NULL) RoadsReachNote(block, L, ops, total);
        RoadsDensityNote(context, before, after, plan);
    }
}

static void RoadsAfterOp(uintptr_t block, uint64_t context, uint64_t record, uint32_t op, uint32_t before, int counted)
{
    if (op != 0x86 && op != 0x0E && op != 0x0F && op != 0x2B) return;
    RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    uint32_t offset = 0, after = 0;
    /* the location table where the hash pass finds it (add rbx, A + 0x90 at 0xC176A7), as WorldLocationsNote reads it */
    if (!SafeRead(g_world.game + 0xC176A7 + 3, &offset, 4) && offset >= 0x90) L.locationsAt = offset - 0x90;
    SafeRead(block + 0x11A7108u, &after, 4);
    const uint32_t total = after < DIF_LOCATIONS ? after : DIF_LOCATIONS;
    const uint32_t plan = g_plan.level == g_world.levels ? g_plan.count : 0;
    RoadsAfterOpAt(block, &L, context, record, op, before, after, counted ? g_locationOp : NULL, total, plan);
}
