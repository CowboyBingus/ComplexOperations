/* ---- Roads: the level's ground is graded along them (extra large complex levels) -----------------------------------
 * Before the level's op 0xB7 (LevelRoutes, 0xFB8A30; the level waits, as at its 0x40): no op writes heights after it
 * but 0x63, and what spawns later stands on the capture ops 0x5D/0x5E take next (lg+0xC0: ops 0x30 and 0x6C read it,
 * 0xF48481 and 0xF47084). In order:
 * - Stations on the arterials' and the spurs' points (difficulty_world_roadfield.c); their ground from the height
 *   capture of ops 0x5B/0x5C (lg+0, mip 0, 1024 x 1024 floats in metres, 16 m a cell on 16 km) through the sampler
 *   op 0x3F's reader uses (0xF4B0D0: [api+0x10]+0x1F0 with the descriptor, uv = metres from the corner / world, y up).
 * - Pins: a station under one of the level's pads (op 0x3F's shapes, kept by TerrainNearPads) or within GRADE_RIM
 *   of the playable radius keeps its ground. Which stations are pinned comes from generation data alone; the
 *   capture only gives heights, and the profile is a continuous function of them (GradeSolve), solved once a level.
 * - The draw: the game's own leveller's material 0x671080F3766141AF through generator [R+0x28] (0xFB8A6A; triangles
 *   as 0xFB88BE feeds them: float3 pixels with the height as z, layer 0, the colour at game+0x23C8540, float2 uv
 *   with the weight in u). Its shader eases u and sets height x a + ground x (1 - a). The field is drawn on a
 *   GRADE_STEP lattice, each cell once, so no ground is set twice. Under a pad and past the rim the weight is 0.
 * HELLFORGE_XL_ROAD_GRADE=0 (research) leaves the ground as it is; the reports are still written. */
#define GRADE_SLOPE 0.0874887f /* tan 5 degrees: the user's sustained grade, held everywhere */
#define GRADE_PASSES 16u       /* easing: 2 degrees a 5 m at stations 5 to 10 m apart (tests/roadgrade_test.c) */
#define GRADE_STEP 4.0f        /* the lattice: the level's heightmap has a sample every 4 m */
#define GRADE_RIM 64.0f
#define GRADE_WATER 1.0f       /* metres a road stays above the highest water */
#define GRADE_NO_HEIGHT 3.402823466e38f /* FLT_MAX: a location without a height of its own */
#define GRADE_MATERIAL 0x671080F3766141AFull
typedef uint64_t (*GradeSampleFn)(void* descriptor, float* uv, void* out, uint64_t unused);
typedef void (*GradeTriFn)(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material,
    const float* uv0, const float* uv1, const float* uv2);
static struct
{
    LONG level, afterLevel;
    int ready, phase, off;
    GradeField f;
    float world, playable;
    uint32_t torn, pinned, cells, next, drawn, batches, waits;
    uint8_t* cell; /* a bit a lattice cell a road's band may reach */
    uint32_t* padStart; uint32_t* padItem; int padGrid;
    GradeSampleFn sample; uintptr_t capture; /* tests set both */
    GradeTriFn tri;                          /* tests: in place of the game's */
} g_grade = {.level = -1, .afterLevel = -1};

static int RoadGradeOn(void)
{
    static int on = -1;
    if (on < 0) { char text[8] = ""; on = !(GetEnvironmentVariableA("HELLFORGE_XL_ROAD_GRADE", text, sizeof text) > 0 && text[0] == '0'); }
    return on;
}

/* A pad's weight at (dx, dy) from its centre, as NearPadRaster weighs it. */
static float RoadGradePadWeight(const NearPad* p, float dx, float dy)
{
    if (p->rect)
    {
        const float lx = fabsf(dx * p->c + dy * p->s), ly = fabsf(-dx * p->s + dy * p->c);
        float ta = (lx - p->a) / p->ba, tb = (ly - p->b) / p->bb;
        ta = ta < 0 ? 0 : ta > 1 ? 1 : ta; tb = tb < 0 ? 0 : tb > 1 ? 1 : tb;
        return p->k * (1 - ta * ta * (3 - 2 * ta)) * (1 - tb * tb * (3 - 2 * tb));
    }
    float t = (sqrtf(dx * dx + dy * dy) - p->a) / p->ba; t = t < 0 ? 0 : t > 1 ? 1 : t;
    return p->k * (1 - t * t * (3 - 2 * t));
}
static float RoadGradePadReach(const NearPad* p) { return p->rect ? sqrtf((p->a + p->ba) * (p->a + p->ba) + (p->b + p->bb) * (p->b + p->bb)) : p->a + p->ba; }

/* The level's pads in GRADE_CELL cells (corner metres). */
static void RoadGradePadIndex(void)
{
    free(g_grade.padStart); free(g_grade.padItem); g_grade.padStart = g_grade.padItem = NULL;
    g_grade.padGrid = (int)ceilf(g_grade.world / GRADE_CELL);
    if (g_pads.level != g_world.levels || g_pads.count == 0 || g_grade.padGrid < 1) return;
    const size_t cells = (size_t)g_grade.padGrid * g_grade.padGrid;
    uint32_t* start = (uint32_t*)calloc(cells + 1, 4); uint32_t* item = NULL;
    if (start == NULL) return;
    for (int fill = 0; fill < 2; fill++)
    {
        for (int q = 0; q < g_pads.count; q++)
        {
            const NearPad* p = &g_pads.pad[q];
            const float r = RoadGradePadReach(p), x = p->cx + g_grade.world / 2, y = p->cy + g_grade.world / 2;
            int c0 = (int)floorf((x - r) / GRADE_CELL), c1 = (int)floorf((x + r) / GRADE_CELL), r0 = (int)floorf((y - r) / GRADE_CELL), r1 = (int)floorf((y + r) / GRADE_CELL);
            c0 = c0 < 0 ? 0 : c0; r0 = r0 < 0 ? 0 : r0; c1 = c1 >= g_grade.padGrid ? g_grade.padGrid - 1 : c1; r1 = r1 >= g_grade.padGrid ? g_grade.padGrid - 1 : r1;
            for (int r_ = r0; r_ <= r1; r_++)
                for (int c = c0; c <= c1; c++)
                {
                    const size_t cell = (size_t)r_ * g_grade.padGrid + c;
                    if (fill) item[start[cell]++] = (uint32_t)q; else start[cell + 1]++;
                }
        }
        if (fill) break;
        for (size_t c = 0; c < cells; c++) start[c + 1] += start[c];
        item = (uint32_t*)malloc(4 * (size_t)(start[cells] ? start[cells] : 1));
        if (item == NULL) { free(start); return; }
    }
    for (size_t c = cells; c > 0; c--) start[c] = start[c - 1];
    start[0] = 0;
    g_grade.padStart = start; g_grade.padItem = item;
}

/* How much of the road's height a point may take (corner metres): none past the rim or under a pad.
 * all = 1 (a near tile): every pad at its own weight, footprints too, as the tile takes the level's surface there.
 * all = 0 (the level's heights, the pins): only the pads op 0x3F flattened; a stamp's footprint is no flat ground, its
 * stamp takes its pose on the graded level. A pad's skirt under GRADE_PIN leaves the road whole, and the share eases
 * to none at the pad's full weight, so a road only gives way where the flat ground itself begins. */
#define GRADE_PIN 0.5f
static float RoadGradeFreeOf(float x, float y, int all)
{
    const float half = g_grade.world / 2, r = sqrtf((x - half) * (x - half) + (y - half) * (y - half));
    float rim = (g_grade.playable - r) / GRADE_RIM, pad = 0.0f;
    rim = rim < 0.0f ? 0.0f : rim > 1.0f ? 1.0f : rim;
    if (g_grade.padStart != NULL && x >= 0.0f && y >= 0.0f && (int)(x / GRADE_CELL) < g_grade.padGrid && (int)(y / GRADE_CELL) < g_grade.padGrid)
    {
        const size_t cell = (size_t)(int)(y / GRADE_CELL) * g_grade.padGrid + (int)(x / GRADE_CELL);
        for (uint32_t q = g_grade.padStart[cell]; q < g_grade.padStart[cell + 1]; q++)
        {
            const NearPad* p = &g_pads.pad[g_grade.padItem[q]];
            if (!all && p->foot) continue;
            const float w = RoadGradePadWeight(p, x - half - p->cx, y - half - p->cy);
            if (w > pad) pad = w;
        }
    }
    if (pad > 1.0f) pad = 1.0f;
    if (!all) pad = GradeSmooth((pad - GRADE_PIN) / (1.0f - GRADE_PIN));
    return rim * (1.0f - pad);
}
static float RoadGradeFree(float x, float y) { return RoadGradeFreeOf(x, y, 1); }
static float RoadGradeGround(void* unused, float x, float y);

/* The capture's ground at a point (corner metres); not finite when it cannot be read. */
static float RoadGradeGround(void* unused, float x, float y)
{
    float uv[2] = {x / g_grade.world, y / g_grade.world}, h = 0.0f;
    (void)unused;
    if (g_grade.sample == NULL || g_grade.capture == 0) return NAN;
    g_grade.sample((void*)g_grade.capture, uv, &h, 0);
    return h;
}
/* The roads' own height at a point: the designed surface, where a road reaches. */
static float RoadGradeDesigned(void* unused, float x, float y)
{
    float z = NAN, d;
    (void)unused;
    return GradeFieldAt(&g_grade.f, x, y, &z, &d) > 0.0f ? z : NAN;
}
/* The level's final capture (g_near.global, 4 m) between its four samples about a point. */
static float RoadGradeLevel(void* unused, float x, float y)
{
    (void)unused;
    const RoadsGround g = {g_near.global, g_near.columns, g_near.rows, g_grade.world / (float)g_near.columns, 0.0f};
    return RoadsGroundAt(&g, x, y);
}
/* A near tile's own heights; not finite off its hidden square. */
static float RoadGradeTile(void* unused, float x, float y)
{
    (void)unused;
    const float wx = x - g_grade.world / 2, wy = y - g_grade.world / 2;
    if (NearOutside(wx, wy) > 0.0f) return NAN;
    const float h = NearTileAt(wx, wy);
    return h > -9000.0f ? h : NAN;
}

/* The stations: every arterial edge with points, its ends on the route nodes it names, then the spurs, each from its
 * place to the station where it meets an arterial. 0 when the level's roads cannot be read. */
static int RoadGradeStations(uintptr_t block)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0, nodes = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    const uintptr_t nodesAt = block + g_routeNodes;
    uint32_t* nodeStation = NULL;
    if (n != 0 && pts != NULL && !SafeRead(block + 0x11A71B4u, &nodes, 4) && nodes != 0 && nodes <= ROADS_GRAPH_NODES) nodeStation = (uint32_t*)malloc(4 * (size_t)nodes);
    if (nodeStation == NULL) { RoadsFreeEdges(e, n); if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE); return 0; }
    memset(nodeStation, 0xFF, 4 * (size_t)nodes);
    GradeFieldFree(&g_grade.f);
    for (uint32_t i = 0; i < n; i++)
    {
        if (!((g_arterials.bits[i >> 3] >> (i & 7)) & 1) || !RoadEdgeHasPoints(&e[i], points)) continue;
        const float* p = pts + 2 * (size_t)e[i].first; const uint32_t count = e[i].end - e[i].first;
        uint32_t own[2] = {GRADE_NONE, GRADE_NONE}; uint32_t* end[2] = {&own[0], &own[1]};
        /* each end of the path is the named node it lies on (within GRADE_JOIN), whichever way the path runs */
        for (int q = 0; q < 2; q++)
        {
            const float* at = q ? p + 2 * (size_t)(count - 1) : p;
            for (int v = 0; v < 2; v++)
            {
                float xy[2];
                if (e[i].node[v] < nodesAt || (e[i].node[v] - nodesAt) % 0x98 != 0 || (e[i].node[v] - nodesAt) / 0x98 >= nodes || SafeRead((uintptr_t)e[i].node[v], xy, 8)) continue;
                if (sqrtf((xy[0] - at[0]) * (xy[0] - at[0]) + (xy[1] - at[1]) * (xy[1] - at[1])) < GRADE_JOIN) end[q] = &nodeStation[(e[i].node[v] - nodesAt) / 0x98];
            }
        }
        if (end[0] == end[1]) end[1] = &own[1];
        GradeFieldChain(&g_grade.f, p, count, end[0], end[1]);
    }
    const uint32_t arterial = g_grade.f.segs;
    for (uint32_t k = 0; g_spurs.level == g_world.levels && k < g_spurs.count; k++)
    {
        const RoadSpur* s = &g_spurs.spur[k];
        if (s->end > points || s->end < s->first + 2) continue;
        uint32_t from = GRADE_NONE, to = GradeFieldAttach(&g_grade.f, pts[2 * (size_t)(s->end - 1)], pts[2 * (size_t)(s->end - 1) + 1], arterial);
        GradeFieldChain(&g_grade.f, pts + 2 * (size_t)s->first, s->end - s->first, &from, &to);
    }
    free(nodeStation);
    RoadsFreeEdges(e, n);
    VirtualFree(pts, 0, MEM_RELEASE);
    return g_grade.f.stations >= 2 && g_grade.f.segs >= 1;
}

/* The ground, the pins and the water floor at every station, then the profile and the field. 0: not solved. */
static int RoadGradeSolve(void)
{
    GradeField* f = &g_grade.f;
    const float half = g_grade.world / 2;
    const int water = g_nearWater.level == g_world.levels && g_nearWater.count > 0;
    g_grade.pinned = 0;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        const float x = f->xy[2 * i], y = f->xy[2 * i + 1], h = RoadGradeGround(NULL, x, y);
        if (!isfinite(h)) return 0;
        f->s[i].ground = h;
        f->s[i].pinned = RoadGradeFreeOf(x, y, 0) < 1.0f || sqrtf((x - half) * (x - half) + (y - half) * (y - half)) > g_grade.playable - GRADE_RIM;
        f->s[i].floor = water ? g_nearWater.high + GRADE_WATER : -GRADE_FAR;
        g_grade.pinned += f->s[i].pinned;
    }
    GradeFieldLinks(f);
    for (uint32_t i = 0; i < f->stations; i++) f->s[i].cross = GradeFieldCross(f, i, RoadGradeGround, NULL);
    g_grade.torn = GradeSolve(f->s, f->stations, GRADE_SLOPE, GRADE_PASSES);
    return GradeFieldIndex(f, g_grade.world);
}

/* The lattice cells a road's band may reach: each piece's box, grown by its band. */
static void RoadGradeCells(void)
{
    const GradeField* f = &g_grade.f;
    g_grade.cells = (uint32_t)ceilf(g_grade.world / GRADE_STEP);
    free(g_grade.cell);
    g_grade.cell = (uint8_t*)calloc(((size_t)g_grade.cells * g_grade.cells + 7) / 8, 1);
    for (uint32_t k = 0; g_grade.cell != NULL && k < f->segs; k++)
    {
        const GradeSeg* g = &f->seg[k];
        const float r = f->band[g->a] > f->band[g->b] ? f->band[g->a] : f->band[g->b];
        int c0 = (int)floorf((fminf(g->ax, g->bx) - r) / GRADE_STEP), c1 = (int)floorf((fmaxf(g->ax, g->bx) + r) / GRADE_STEP);
        int r0 = (int)floorf((fminf(g->ay, g->by) - r) / GRADE_STEP), r1 = (int)floorf((fmaxf(g->ay, g->by) + r) / GRADE_STEP);
        c0 = c0 < 0 ? 0 : c0; r0 = r0 < 0 ? 0 : r0; c1 = c1 >= (int)g_grade.cells ? (int)g_grade.cells - 1 : c1; r1 = r1 >= (int)g_grade.cells ? (int)g_grade.cells - 1 : r1;
        for (int r_ = r0; r_ <= r1; r_++)
            for (int c = c0; c <= c1; c++) { const size_t bit = (size_t)r_ * g_grade.cells + c; g_grade.cell[bit >> 3] |= (uint8_t)(1u << (bit & 7)); }
    }
}

/* Up to `most` cells from the cursor on as two triangles each (px: pixels a metre). Returns the triangles given. */
static uint32_t RoadGradeBatch(GradeTriFn tri, uint64_t gui, uint64_t material, const float* colour, float px, uint32_t most)
{
    const uint32_t total = g_grade.cells * g_grade.cells;
    uint32_t given = 0;
    for (; g_grade.next < total && given + 2 <= most; g_grade.next++)
    {
        const uint32_t k = g_grade.next;
        if ((k & 7) == 0 && g_grade.cell[k >> 3] == 0) { g_grade.next += 7; continue; }
        if (!((g_grade.cell[k >> 3] >> (k & 7)) & 1)) continue;
        float v[4][3], uv[4][2], most_ = 0.0f, seen = NAN;
        for (int q = 0; q < 4; q++)
        {
            const float x = (float)(k % g_grade.cells + (q == 1 || q == 2)) * GRADE_STEP, y = (float)(k / g_grade.cells + (q >= 2)) * GRADE_STEP;
            float z = NAN, d;
            const float w = GradeFieldAt(&g_grade.f, x, y, &z, &d) * RoadGradeFreeOf(x, y, 0);
            v[q][0] = x * px; v[q][1] = y * px; v[q][2] = z; uv[q][0] = w; uv[q][1] = 0.0f;
            if (w > most_) most_ = w;
            if (isfinite(z)) seen = z;
        }
        if (!(most_ > 0.0f)) continue;
        for (int q = 0; q < 4; q++) if (!isfinite(v[q][2])) v[q][2] = seen; /* a corner no road is listed at has weight 0 */
        tri(gui, v[0], v[1], v[2], 0, colour, material, uv[0], uv[1], uv[2]);
        tri(gui, v[0], v[2], v[3], 0, colour, material, uv[0], uv[2], uv[3]);
        given += 2; g_grade.drawn++;
    }
    return given;
}

/* ---- the reports ---- */
static void RoadGradeLine(char* out, size_t size, const GradeRoad* g)
{
    snprintf(out, size, "%.1f km, steepest %.1f deg, 95 %% under %.2f deg (10 m baseline), %.0f / %.0f / %.0f / %.0f m over 5 / 8 / 12 / 18 deg, %.1f m climbed and descended a km, "
        "cross slope at most %.1f deg", g->metres / 1000.0, (double)g->steepest, (double)GradeRoad95(g), g->over[0], g->over[1], g->over[2], g->over[3],
        g->metres > 0.0 ? g->rise / (g->metres / 1000.0) : 0.0, (double)g->cross);
}

/* Every arterial edge and spur on a surface: a line a class and <file>-<level>.tsv (class, index, edge, metres,
 * steepest, 95th percentile, metres over 5, 8, 12 and 18 degrees, climb and descent a km, worst cross slope, and with
 * `moved` the deepest cut and highest fill against the capture). */
static void RoadGradeReport(uintptr_t block, const char* what, const wchar_t* file, GradeHeightFn height, int moved)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    const uint32_t spurs = g_spurs.level == g_world.levels ? g_spurs.count : 0;
    wchar_t name[48];
    swprintf(name, 48, L"%ls-%ld.tsv", file, (long)g_world.levels);
    HANDLE out = pts != NULL ? OpenLog(name) : INVALID_HANDLE_VALUE;
    static GradeRoad sum[2], one;
    memset(sum, 0, sizeof sum);
    uint32_t parts[2] = {0, 0}, deep[2] = {0, 0}; float cut[2] = {0, 0}, fill[2] = {0, 0};
    char list[160] = ""; size_t listed = 0;
    for (uint32_t k = 0; pts != NULL && k < n + spurs; k++)
    {
        const int spur = k >= n;
        uint32_t first, end, edge;
        if (!spur)
        {
            if (!((g_arterials.bits[k >> 3] >> (k & 7)) & 1) || !RoadEdgeHasPoints(&e[k], points)) continue;
            first = e[k].first; end = e[k].end; edge = k;
        }
        else { first = g_spurs.spur[k - n].first; end = g_spurs.spur[k - n].end; edge = g_spurs.spur[k - n].edge; }
        if (end > points || end <= first) continue;
        memset(&one, 0, sizeof one);
        GradeRoadAlong(height, NULL, pts + 2 * (size_t)first, end - first, &one);
        float low = 0.0f, high = 0.0f;
        for (uint32_t p = first; moved && p < end; p++)
        {
            const float z = RoadGradeDesigned(NULL, pts[2 * p], pts[2 * p + 1]), h = RoadGradeGround(NULL, pts[2 * p], pts[2 * p + 1]);
            if (!isfinite(z) || !isfinite(h)) continue;
            if (h - z > low) low = h - z;
            if (z - h > high) high = z - h;
        }
        if (one.metres == 0.0 && !moved) continue;
        GradeRoadAdd(&sum[spur], &one); parts[spur]++;
        if (low > cut[spur]) cut[spur] = low;
        if (high > fill[spur]) fill[spur] = high;
        if (low > 15.0f || high > 15.0f)
        {
            deep[spur]++;
            if (listed + 24 < sizeof list) listed += (size_t)snprintf(list + listed, sizeof list - listed, " %s %u", spur ? "spur" : "edge", spur ? k - n : k);
        }
        char line[224]; DWORD wrote;
        const int len = snprintf(line, sizeof line, "%s\t%u\t%u\t%.1f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.0f\t%.1f\t%.2f\t%.2f\t%.2f\n", spur ? "spur" : "arterial", spur ? k - n : k, edge,
            one.metres, (double)one.steepest, (double)GradeRoad95(&one), one.over[0], one.over[1], one.over[2], one.over[3],
            one.metres > 0.0 ? one.rise / (one.metres / 1000.0) : 0.0, (double)one.cross, (double)low, (double)high);
        if (out != INVALID_HANDLE_VALUE) WriteFile(out, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    for (int c = 0; c < 2; c++)
    {
        char text[320];
        RoadGradeLine(text, sizeof text, &sum[c]);
        if (moved)
            Note("roads: level %ld grade, %s: %u %s: %s; deepest cut %.1f m, highest fill %.1f m, %u with over 15 m", (long)g_world.levels, what, parts[c],
                c ? "spurs" : "arterial edges", text, (double)cut[c], (double)fill[c], deep[c]);
        else Note("roads: level %ld grade, %s: %u %s: %s", (long)g_world.levels, what, parts[c], c ? "spurs" : "arterial edges", text);
    }
    if (moved && listed != 0) Note("roads: level %ld grade: cut or fill over 15 m on:%s", (long)g_world.levels, list);
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
}

/* Once a level: the planet's relief values (the region resource's dwords +0x0C..+0x34, as floats) and its leveller
 * byte, and the locations in a road's band op 0x6C stands at a height of their own (location +0xC is not GRADE_NO_HEIGHT:
 * 0xF47041 reads it in place of the capture), with how far that is from the road. */
static void RoadGradeCensus(uintptr_t block, uint64_t context)
{
    uint64_t settings = 0, region = 0; float relief[11]; uint8_t levels = 0; uint32_t placed = 0, offset = 0;
    if (!SafeRead(context + 8, &settings, 8) && settings != 0 && !SafeRead(settings + 0xB0, &region, 8) && region != 0 && !SafeRead(region + 0x0C, relief, sizeof relief) &&
        !SafeRead(region + 0x678, &levels, 1))
        Note("roads: level %ld relief values (region +0x0C..+0x34): %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f; leveller byte %u", (long)g_world.levels,
            (double)relief[0], (double)relief[1], (double)relief[2], (double)relief[3], (double)relief[4], (double)relief[5], (double)relief[6], (double)relief[7],
            (double)relief[8], (double)relief[9], (double)relief[10], levels);
    if (SafeRead(block + 0x11A7108u, &placed, 4) || SafeRead(g_world.game + 0xC176A7 + 3, &offset, 4) || offset < 0x90) return;
    if (placed > DIF_LOCATIONS) placed = DIF_LOCATIONS;
    uint32_t fixed = 0, inBand = 0; float worst = 0.0f;
    for (uint32_t i = 0; i < placed; i++)
    {
        float xy[2], own = GRADE_NO_HEIGHT, z = NAN, d;
        if (SafeRead(block + (offset - 0x90) + 0xE0ull * i, xy, 8) || SafeRead(block + (offset - 0x90) + 0xE0ull * i + 0xC, &own, 4) || own == GRADE_NO_HEIGHT) continue;
        fixed++;
        if (!(GradeFieldAt(&g_grade.f, xy[0], xy[1], &z, &d) * RoadGradeFree(xy[0], xy[1]) > 0.0f) || !isfinite(z)) continue;
        inBand++;
        if (fabsf(own - z) > worst) worst = fabsf(own - z);
    }
    Note("roads: level %ld grade: %u of %u locations stand at a height of their own, %u of them in a road's band (at most %.1f m from the road's height)", (long)g_world.levels,
        fixed, placed, inBand, (double)worst);
}

/* Before the level's op 0xB8 (0xFB76D0): its planned mesh count [pass+0xC4] (0xFB769F). Its meshes take their height
 * from the capture before the roads are graded. */
static void RoadGradeEmbankNote(uint64_t record)
{
    static LONG noted = -1;
    uint32_t count = 0;
    if (noted == g_world.levels || g_arterials.level != g_world.levels || SafeRead(record + 8 + 0xC4, &count, 4)) return;
    noted = g_world.levels;
    Note("roads: level %ld op 0xB8 plans %u embankment meshes (at the ground's height before grading)", (long)g_world.levels, count);
}

/* The dispatcher's hook at the level's op 0xB7: 1 while the level waits. */
static int RoadGradeStep(uint64_t context)
{
    if (g_world.current != 8.0f || g_worldLevel == 0 || g_arterials.level != g_world.levels || g_arterials.count == 0) return 0;
    uintptr_t gen = 0;
    if (SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0) return 0;
    if (g_grade.level != g_world.levels)
    {
        g_grade.level = g_world.levels; g_grade.ready = 0; g_grade.phase = 0; g_grade.next = 0; g_grade.drawn = 0; g_grade.batches = 0; g_grade.waits = 0;
        uint64_t settings = 0; uint32_t size = 0, dims[2] = {0, 0}; float fraction = 0; uint64_t data = 0; double radius = 0;
        const uintptr_t lg = gen + 0x8758A8u;
        g_grade.world = 2048.0f * g_world.current;
        if (SafeRead(context + 8, &settings, 8) || settings == 0 || SafeRead(settings + 0x2C, &size, 4) || SafeRead(settings + 0x50, &fraction, 4) ||
            (float)size != g_grade.world || RoadsDiscKm2(size, fraction, &radius) == 0 || SafeRead(lg, dims, 8) || SafeRead(lg + 0x10, &data, 8) || data == 0 ||
            dims[0] < 256 || dims[0] > 4096 || dims[1] != dims[0])
        { Note("roads: level %ld grade: the height capture or the world's size is not the reviewed one; the ground stays as it is", (long)g_world.levels); return 0; }
        g_grade.playable = (float)radius;
        g_grade.sample = (GradeSampleFn)NearApi(0x10, 0x1F0); g_grade.capture = lg;
        LARGE_INTEGER hz, t0, t1;
        QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
        RoadGradePadIndex();
        if (g_grade.sample == NULL || !RoadGradeStations(gen) || !RoadGradeSolve())
        { Note("roads: level %ld grade: the roads' stations could not be made or solved; the ground stays as it is", (long)g_world.levels); return 0; }
        RoadGradeCells();
        QueryPerformanceCounter(&t1);
        GradeStats g; GradeMeasure(g_grade.f.s, g_grade.f.stations, &g);
        Note("roads: level %ld grade: %u stations on %u pieces (%u pinned under pads or at the rim, %u pieces unlinked), the %ux%u capture at %.0f m; profile: steepest %.2f deg, "
            "sharpest bend %.2f deg, deepest cut %.1f m, highest fill %.1f m, %.0f m moved over 8 m and %.0f m over 15 m; %u stations between pins the limit cannot join, %u bands at "
            "the widest (%.0f m); %.0f ms", (long)g_world.levels, g_grade.f.stations, g_grade.f.segs, g_grade.pinned, g_grade.f.unlinked, dims[0], dims[1],
            (double)(g_grade.world / (float)dims[0]), (double)g.steepest, (double)g.bend, (double)g.cut, (double)g.fill, g.cut8, g.cut15, g_grade.torn, g_grade.f.capped,
            (double)(GRADE_FULL + GRADE_BAND_MOST), (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
        g_grade.ready = 1;
        RoadGradeReport(gen, "the ground before (the capture)", L"grade-before", RoadGradeGround, 0);
        RoadGradeReport(gen, "the roads as designed", L"grade-designed", RoadGradeDesigned, 1);
        RoadGradeCensus(gen, context);
        g_grade.off = !RoadGradeOn() || g_grade.cell == NULL;
        if (g_grade.off) { Note("roads: level %ld grade: off (HELLFORGE_XL_ROAD_GRADE=0, research): the ground stays as it is", (long)g_world.levels); return 0; }
        g_grade.phase = 1;
    }
    if (g_grade.phase != 1) return 0;
    const uint32_t queued = NearQueued(context);
    if (queued != 0 && queued != 0xFFFFFFFFu && ++g_grade.waits < 2400) return 1;
    g_grade.waits = 0;
    typedef uint64_t (*MatFn)(uint64_t gui, uint64_t hash);
    const MatFn mat = (MatFn)NearApi(0xD0, 0x230);
    const GradeTriFn tri = g_grade.tri != NULL ? g_grade.tri : (GradeTriFn)NearApi(0xD0, 0x0);
    uint64_t R = 0, gui = 0, world = 0, settings = 0, queue = 0, camera = 0, generator = 0, r108 = 0;
    float samples = 0, colour[4] = {0};
    if (g_grade.next >= g_grade.cells * g_grade.cells || mat == NULL || tri == NULL || SafeRead(gen, &R, 8) || R == 0 || SafeRead(R + 0x110, &gui, 8) || gui == 0 ||
        SafeRead(context + 0x40, &world, 8) || world == 0 || SafeRead(context + 8, &settings, 8) || settings == 0 || SafeRead(gen + 0x8758A8u + 0x120, &samples, 4) ||
        !(samples >= 1024.0f && samples <= 8192.0f) || SafeRead(context + 0x50, &queue, 8) || queue == 0 || SafeRead(R, &camera, 8) || SafeRead(R + 0x28, &generator, 8) ||
        generator == 0 || SafeRead(R + 0x108, &r108, 8) || SafeRead(g_world.game + 0x23C8540u, colour, 16))
    {
        g_grade.phase = 2;
        Note("roads: level %ld grade: %u lattice cells drawn into the level's heights in %u batches%s", (long)g_world.levels, g_grade.drawn, g_grade.batches,
            g_grade.next >= g_grade.cells * g_grade.cells ? "" : " (the merge was not readable: the rest is left out)");
        return 0;
    }
    if (RoadGradeBatch(tri, gui, mat(gui, GRADE_MATERIAL), colour, samples / g_grade.world, NEAR_BATCH) != 0)
    {
        NearGuiJob(queue, world, camera, generator, r108, settings);
        g_grade.batches++;
    }
    return 1;
}

/* Once the level's final capture is kept (g_near.global): the roads on it, after grading. */
static void RoadGradeAfterNote(uintptr_t block)
{
    if (g_grade.afterLevel == g_world.levels || g_grade.level != g_world.levels || !g_grade.ready || g_near.level != g_world.levels || g_near.global == NULL ||
        g_near.columns < 2 || g_near.rows < 2) return;
    g_grade.afterLevel = g_world.levels;
    RoadGradeReport(block, g_grade.off ? "the level's 4 m heights (not graded)" : "the level's 4 m heights after grading", L"grade-after", RoadGradeLevel, 0);
}

/* ---- a near tile ----
 * The whole band takes the level's surface, as under a pad (NearTileShare): the weight for NearPadRaster's cell. */
static float RoadGradeTileWeight(float wx, float wy)
{
    if (!g_grade.ready || g_grade.off || g_grade.level != g_world.levels) return 0.0f;
    float z, d;
    const float x = wx + g_grade.world / 2, y = wy + g_grade.world / 2, w = GradeFieldAt(&g_grade.f, x, y, &z, &d);
    return w > 0.0f ? GradeSmooth(w * RoadGradeFree(x, y)) : 0.0f;
}
/* The level's surface at a tile texel, sharpened on the core: the level holds the road at 4 m a sample, the tile
 * has 0.56 m, so within half the core it takes the roads' own height and eases to the level's surface at the core's edge. */
static float RoadGradeSharpen(float wx, float wy, float coarse)
{
    if (!g_grade.ready || g_grade.off || g_grade.level != g_world.levels) return coarse;
    float z = NAN, d;
    const float x = wx + g_grade.world / 2, y = wy + g_grade.world / 2;
    if (!(GradeFieldAt(&g_grade.f, x, y, &z, &d) > 0.0f) || !(d < GRADE_CORE) || !isfinite(z)) return coarse;
    float s = (GRADE_CORE - d) / (GRADE_CORE / 2);
    s = (s > 1.0f ? 1.0f : s) * RoadGradeFree(x, y);
    return coarse + s * (z - coarse);
}
/* When a tile's heights are in: the roads on them, where they cross its hidden square. */
static void RoadGradeTileNote(void)
{
    uintptr_t gen = 0;
    if (!g_grade.ready || g_grade.level != g_world.levels || SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0) return;
    char what[96];
    snprintf(what, sizeof what, "the near tile at (%.0f, %.0f), 0.56 m heights", (double)g_near.centre[0], (double)g_near.centre[1]);
    RoadGradeReport(gen, what, L"grade-tile", RoadGradeTile, 0);
}
