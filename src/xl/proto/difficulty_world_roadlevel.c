/* ---- Roads: the level's ground is graded along them (extra large complex levels) -----------------------------------
 * Before the level's op 0xB7 (LevelRoutes, 0xFB8A30; the level waits, as at its 0x40): no op writes heights after it
 * but 0x63, and what spawns later stands on the capture ops 0x5D/0x5E take next (lg+0xC0: ops 0x30 and 0x6C read it,
 * 0xF48481 and 0xF47084). In order:
 * - Stations on the arterials' and the spurs' points (difficulty_world_roadfield.c); their ground from the height
 *   capture of ops 0x5B/0x5C (lg+0, mip 0, 1024 x 1024 floats in metres, 16 m a cell on 16 km) through the sampler
 *   op 0x3F's reader uses (0xF4B0D0: [api+0x10]+0x1F0 with the descriptor, uv = metres from the corner / world, y up).
 * - Pins: a station on the flat ground of one of the level's pads (op 0x3F's shapes, kept by TerrainNearPads, at
 *   GRADE_PIN of their weight) or within GRADE_RIM of the playable radius keeps its ground. A flatten zone's box is
 *   not level ground and pins nothing. Which stations are pinned comes from generation data alone; the capture only
 *   gives heights, and the profile is a continuous function of them (GradeSolveCapped), solved once a level: within
 *   GRADE_SOFT where that moves the ground GRADE_EARTH at most, within GRADE_HARD everywhere. Pins the hard limit
 *   cannot join are left by half the lack each and reported.
 * - The draw: the game's own leveller's material 0x671080F3766141AF through generator [R+0x28] (0xFB8A6A; triangles
 *   as 0xFB88BE feeds them: float3 pixels with the height as z, layer 0, the colour at game+0x23C8540, float2 uv
 *   with the weight in u). Its shader eases u and sets height x a + ground x (1 - a). The field is drawn on a
 *   GRADE_STEP lattice, each cell once, so no ground is set twice. The shader's easing is per vertex (its vertex
 *   stage: saturate, then u u (3 - 2u)); between vertices the eased weight and the height run straight. On a pad's
 *   flat ground and past the rim the weight is 0; beside a pad it falls over the pad's band (RoadGradeShare).
 * HELLFORGE_XL_ROAD_GRADE=0 (research) leaves the ground as it is; the reports are still written. */
#define GRADE_SOFT 0.0787017f  /* tan 4.5 degrees where the cap allows: a sustained 5 with room for the 4 m lattice */
#define GRADE_HARD 0.1227846f  /* tan 7 degrees, held everywhere: nothing over 8 on the 4 m lattice */
#define GRADE_STEEP 0.3249197f /* tan 18 degrees: what a link may take where 7 would stand the road over the cap again */
#define GRADE_EARTH 4.0f       /* metres the gentle profile may cut or fill: a worn path follows the land */
#define GRADE_PASSES 16u       /* easing: 2 degrees a 5 m at stations 5 to 10 m apart (tests/roadgrade_test.c) */
#define GRADE_STEP 4.0f        /* the lattice: the level's heightmap has a sample every 4 m */
#define GRADE_RIM 64.0f
#define GRADE_MISS 0.25f       /* metres: a pin the profile leaves by more is reported */
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
    float (*under)(void*, float, float);     /* RoadGradeClear's surface */
    uint32_t ties, missed; float miss, missAt[2];
    uint32_t landings, landed, past; /* past: stations beyond the playable radius's margin (the rim moved out for them) */
    uint32_t deep; float deepest, deepAt[2]; double deepMetres; /* moved past the cap: by the hard limit or a pin */
    uint32_t steep; float steepest, steepAt[2]; double steepMetres; /* links that may climb over the hard limit */
} g_grade = {.level = -1, .afterLevel = -1};

static int RoadGradeOn(void)
{
    static int on = -1;
    if (on < 0) { char text[8] = ""; on = !(GetEnvironmentVariableA("HELLFORGE_XL_ROAD_GRADE", text, sizeof text) > 0 && text[0] == '0'); }
    return on;
}

/* A pad's weight at (dx, dy) from its centre, as NearPadRaster weighs it; least: its bands taken this wide at least. */
static float RoadGradePadWeight(const NearPad* p, float dx, float dy, float least)
{
    if (p->rect)
    {
        const float lx = fabsf(dx * p->c + dy * p->s), ly = fabsf(-dx * p->s + dy * p->c);
        float ta = (lx - p->a) / fmaxf(p->ba, least), tb = (ly - p->b) / fmaxf(p->bb, least);
        ta = ta < 0 ? 0 : ta > 1 ? 1 : ta; tb = tb < 0 ? 0 : tb > 1 ? 1 : tb;
        return p->k * (1 - ta * ta * (3 - 2 * ta)) * (1 - tb * tb * (3 - 2 * tb));
    }
    float t = (sqrtf(dx * dx + dy * dy) - p->a) / fmaxf(p->ba, least); t = t < 0 ? 0 : t > 1 ? 1 : t;
    return p->k * (1 - t * t * (3 - 2 * t));
}
#define GRADE_FADE 48.0f /* beside a road a pad's band counts this wide at least (RoadGradeShare) */
#define GRADE_FOOT 1.0e-3f /* pad weight: the feet's mean may lack this much of the line's own without a loss of share */
static float RoadGradePadReach(const NearPad* p)
{
    const float ba = fmaxf(p->ba, GRADE_FADE), bb = fmaxf(p->bb, GRADE_FADE);
    return p->rect ? sqrtf((p->a + ba) * (p->a + ba) + (p->b + bb) * (p->b + bb)) : p->a + ba;
}

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

/* The largest weight of the pads op 0x3F flattened at a point (corner metres), their bands `least` wide at least. A
 * flatten zone's box is not level and never counts. A stamp's footprint is no flat ground and pins nothing (least
 * 0), but it counts for the fade: the place's own terrain is drawn there, so a road's share and cap fall to none
 * at its edge like beside a pad. */
#define GRADE_PIN 0.98f
#define GRADE_SKIRT 0.9f
static float RoadGradePadWide(float x, float y, float least)
{
    const float half = g_grade.world / 2;
    float pad = 0.0f;
    if (g_grade.padStart != NULL && x >= 0.0f && y >= 0.0f && (int)(x / GRADE_CELL) < g_grade.padGrid && (int)(y / GRADE_CELL) < g_grade.padGrid)
    {
        const size_t cell = (size_t)(int)(y / GRADE_CELL) * g_grade.padGrid + (int)(x / GRADE_CELL);
        for (uint32_t q = g_grade.padStart[cell]; q < g_grade.padStart[cell + 1]; q++)
        {
            const NearPad* p = &g_pads.pad[g_grade.padItem[q]];
            if (p->zone || (p->foot && !(least > 0.0f))) continue;
            float w = RoadGradePadWeight(p, x - half - p->cx, y - half - p->cy, least);
            if (p->foot)
            {
                /* a place's own ground: the fade's width from its edge, whatever band a tile blends it over */
                float t = (sqrtf((x - half - p->cx) * (x - half - p->cx) + (y - half - p->cy) * (y - half - p->cy)) - p->a) / least;
                t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
                w = 1.0f - t * t * (3.0f - 2.0f * t);
            }
            if (w > pad) pad = w;
        }
    }
    return pad;
}
static float RoadGradePadAt(float x, float y) { return RoadGradePadWide(x, y, 0.0f); }
static float RoadGradeFade(float x, float y) { return RoadGradePadWide(x, y, GRADE_FADE); }
/* 1 inside a stamp's own footprint (corner metres): the place's ground, where its own terrain rules. */
static int RoadGradeInPlace(float x, float y)
{
    const float half = g_grade.world / 2;
    if (g_grade.padStart == NULL || !(x >= 0.0f) || !(y >= 0.0f) || (int)(x / GRADE_CELL) >= g_grade.padGrid || (int)(y / GRADE_CELL) >= g_grade.padGrid) return 0;
    const size_t cell = (size_t)(int)(y / GRADE_CELL) * g_grade.padGrid + (int)(x / GRADE_CELL);
    for (uint32_t q = g_grade.padStart[cell]; q < g_grade.padStart[cell + 1]; q++)
    {
        const NearPad* p = &g_pads.pad[g_grade.padItem[q]];
        const float dx = x - half - p->cx, dy = y - half - p->cy;
        if (p->foot && dx * dx + dy * dy <= p->a * p->a) return 1;
    }
    return 0;
}
static float RoadGradeRim(float x, float y)
{
    const float half = g_grade.world / 2, r = sqrtf((x - half) * (x - half) + (y - half) * (y - half));
    const float rim = (g_grade.playable - r) / GRADE_RIM;
    /* a road that had to cross the rim is graded like any other: the rim then holds nothing on this level */
    return g_grade.past != 0 ? 1.0f : rim < 0.0f ? 0.0f : rim > 1.0f ? 1.0f : rim;
}
/* A station that keeps its ground: on a pad's flat part or within GRADE_RIM of the playable radius. */
static int RoadGradePinned(float x, float y) { return RoadGradePadAt(x, y) >= GRADE_PIN || RoadGradeRim(x, y) < 1.0f; }
/* The cap at a station: whole on open ground, none at the rim and where a pad's flat part lies within the bed's half
 * width (the largest pad weight on two rings about the station, bands as RoadGradeShare takes them), eased between:
 * a cut on the line would stand as a wall against flat ground the bed touches, and the cap falls slowly enough
 * towards such a pad for the limit to follow it. */
static float RoadGradeEarth(float x, float y)
{
    float pad = RoadGradeFade(x, y);
    for (int q = 0; q < 16; q++)
    {
        const float r = q < 8 ? GRADE_FULL : GRADE_FULL / 2, a = 0.7853982f * (float)q, w = RoadGradeFade(x + r * cosf(a), y + r * sinf(a));
        if (w > pad) pad = w;
    }
    return GRADE_EARTH * RoadGradeRim(x, y) * (1.0f - GradeSmooth(pad / GRADE_PIN));
}
/* The field with the road's share at a point (corner metres): the weight the draw and a near tile take, *z and *d
 * as GradeFieldAt gives them. The share is whole but for three things, each continuous and from positions alone:
 * - past the rim: none;
 * - where the road's stations stand on a pad's flat part, they and their neighbours: none, the pad's ground holds
 *   and the pins already put the road at its height;
 * - a pad beside the road: where the pad counts more here than on the line, the share falls with the pad's weight
 *   over its whole band (GRADE_FADE wide at least), so flat ground in a road's band gets no wall at its edge. On
 *   the line itself the pad counts the same, so a road onto a pad is drawn whole to its first station there and
 *   no step is left where its profile comes down to the pad. */
static float RoadGradeShare(float x, float y, float* z, float* d)
{
    const float fade = RoadGradeFade(x, y);
    GradeFoot on = {fade > 0.0f ? RoadGradeFade : NULL, 0.0f, 0.0f};
    const float w = GradeFieldAt(&g_grade.f, x, y, z, d, &on);
    if (!(w > 0.0f)) return 0.0f;
    const float beside = GradeSmooth((1.0f - fade + GRADE_FOOT) / (1.0f - on.foot + GRADE_FOOT)); /* 1 on the line itself */
    return w * RoadGradeRim(x, y) * (1.0f - GradeSmooth(on.held)) * beside;
}
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
/* What the grading adds at a point: the eased weight times the cut or fill, 0 where no road reaches. Its side slope
 * is the bank the blend makes, apart from the ground's own. */
static float RoadGradeMove(void* unused, float x, float y)
{
    float z = NAN, d;
    (void)unused;
    const float w = RoadGradeShare(x, y, &z, &d), h = RoadGradeGround(NULL, x, y);
    return w > 0.0f && isfinite(z) && isfinite(h) ? GradeSmooth(w) * (z - h) : isfinite(h) ? 0.0f : NAN;
}
/* The roads' own height at a point: the designed surface, where a road reaches. */
static float RoadGradeDesigned(void* unused, float x, float y)
{
    float z = NAN, d;
    (void)unused;
    return GradeFieldAt(&g_grade.f, x, y, &z, &d, NULL) > 0.0f ? z : NAN;
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
                float moved[2] = {xy[0], xy[1]};
                RoadsKeepNode(moved); /* a node inside a place: its roads meet at the place's edge */
                if (sqrtf((xy[0] - at[0]) * (xy[0] - at[0]) + (xy[1] - at[1]) * (xy[1] - at[1])) < GRADE_JOIN ||
                    sqrtf((moved[0] - at[0]) * (moved[0] - at[0]) + (moved[1] - at[1]) * (moved[1] - at[1])) < GRADE_JOIN) end[q] = &nodeStation[(e[i].node[v] - nodesAt) / 0x98];
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
        /* the station on the arterial is the arterial's; the spur's own are solved after it, against its height */
        const uint32_t own = g_grade.f.stations;
        GradeFieldChain(&g_grade.f, pts + 2 * (size_t)s->first, s->end - s->first, &from, &to);
        for (uint32_t i = own; to != GRADE_NONE && i < g_grade.f.stations; i++) g_grade.f.s[i].later = 1;
        if (s->own != ROADS_NO_NODE && from != GRADE_NONE) g_grade.f.s[from].edge = 1;
    }
    free(nodeStation);
    RoadsFreeEdges(e, n);
    VirtualFree(pts, 0, MEM_RELEASE);
    /* from positions alone: the same on every machine */
    g_grade.past = 0;
    for (uint32_t i = 0; i < g_grade.f.stations; i++)
    {
        const float dx = g_grade.f.xy[2 * i] - g_grade.world / 2, dy = g_grade.f.xy[2 * i + 1] - g_grade.world / 2;
        g_grade.past += sqrtf(dx * dx + dy * dy) > g_grade.playable - GRADE_RIM;
    }
    return g_grade.f.stations >= 2 && g_grade.f.segs >= 1;
}

/* The ground, the pins and the water floor at every station, then the profile and the field. 0: not solved. */
static int RoadGradeSolve(void)
{
    GradeField* f = &g_grade.f;
    const int water = g_nearWater.level == g_world.levels && g_nearWater.count > 0;
    g_grade.pinned = 0;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        const float x = f->xy[2 * i], y = f->xy[2 * i + 1], h = RoadGradeGround(NULL, x, y);
        if (!isfinite(h)) return 0;
        f->s[i].ground = h;
        f->s[i].pinned = (uint8_t)(RoadGradePinned(x, y) || f->s[i].edge);
        f->s[i].flat = (uint8_t)(RoadGradePadAt(x, y) >= GRADE_PIN);
        f->s[i].earth = RoadGradeEarth(x, y);
        f->s[i].floor = water ? g_nearWater.high + GRADE_WATER : -GRADE_FAR;
        g_grade.pinned += f->s[i].pinned;
    }
    GradeFieldLinks(f);
    g_grade.landings = GradeFieldLand(f, &g_grade.landed);
    GradeFieldTies(f);
    for (uint32_t i = 0; i < f->stations; i++)
    {
        f->s[i].cross = GradeFieldCross(f, i, RoadGradeGround, NULL);
        f->s[i].held = f->s[i].flat;
        for (uint32_t k = 0; k < f->s[i].links; k++) f->s[i].held &= f->s[f->s[i].link[k]].flat;
    }
    g_grade.torn = GradeSolveStaged(f->s, f->stations, GRADE_SOFT, GRADE_HARD, GRADE_STEEP, GRADE_EARTH, GRADE_PASSES);
    uint32_t at = 0;
    g_grade.steep = GradeRelaxed(f->s, f->stations, GRADE_HARD, &g_grade.steepMetres, &g_grade.steepest, &at);
    g_grade.steepAt[0] = f->stations ? f->xy[2 * at] : 0.0f; g_grade.steepAt[1] = f->stations ? f->xy[2 * at + 1] : 0.0f;
    g_grade.deep = GradeOverCap(f->s, f->stations, GRADE_MISS, &g_grade.deepest, &at, &g_grade.deepMetres);
    g_grade.deepAt[0] = f->stations ? f->xy[2 * at] : 0.0f; g_grade.deepAt[1] = f->stations ? f->xy[2 * at + 1] : 0.0f;
    g_grade.ties = f->ties;
    g_grade.missed = GradeMissed(f->s, f->stations, GRADE_MISS, &g_grade.miss, &at);
    g_grade.missAt[0] = f->stations ? f->xy[2 * at] : 0.0f; g_grade.missAt[1] = f->stations ? f->xy[2 * at + 1] : 0.0f;
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
        float v[4][3], uv[4][2], most_ = 0.0f; double seen = 0.0, shares = 0.0;
        for (int q = 0; q < 4; q++)
        {
            const float x = (float)(k % g_grade.cells + (q == 1 || q == 2)) * GRADE_STEP, y = (float)(k / g_grade.cells + (q >= 2)) * GRADE_STEP;
            float z = NAN, d;
            const float w = RoadGradeShare(x, y, &z, &d);
            v[q][0] = x * px; v[q][1] = y * px; v[q][2] = z; uv[q][0] = w; uv[q][1] = 0.0f;
            if (w > most_) most_ = w;
            if (w > 0.0f) { seen += (double)w * z; shares += w; }
        }
        if (!(most_ > 0.0f)) continue;
        /* a corner without a share has no height of its own: the cell's, by the shares of the corners that have */
        for (int q = 0; q < 4; q++) if (!(uv[q][0] > 0.0f)) v[q][2] = (float)(seen / shares);
        tri(gui, v[0], v[1], v[2], 0, colour, material, uv[0], uv[1], uv[2]);
        tri(gui, v[0], v[2], v[3], 0, colour, material, uv[0], uv[2], uv[3]);
        given += 2; g_grade.drawn++;
    }
    return given;
}

/* ---- the reports ---- */
/* A surface only where a road is drawn whole: off the pads' flat ground and its skirt and off the rim. */
static float RoadGradeClear(void* ctx, float x, float y) { return RoadGradeRim(x, y) < 1.0f || RoadGradePadAt(x, y) > GRADE_SKIRT ? NAN : g_grade.under(ctx, x, y); }
/* A surface up to the places' edges: inside a stamp's footprint the place's own terrain is not the road's. */
static float RoadGradeOpen(void* ctx, float x, float y) { return RoadGradeInPlace(x, y) ? NAN : g_grade.under(ctx, x, y); }
/* "x\ty" from the world's centre, or two empty fields when the measure has no sample. */
static void RoadGradePlace(char* out, size_t size, const float at[2], int any)
{
    const float half = g_grade.world / 2;
    if (any) snprintf(out, size, "%.0f\t%.0f", (double)(at[0] - half), (double)(at[1] - half)); else snprintf(out, size, "\t");
}
static void RoadGradeLine(char* out, size_t size, const GradeRoad* g)
{
    const float half = g_grade.world / 2;
    snprintf(out, size, "%.1f km, steepest %.1f deg at (%.0f, %.0f), 95 %% under %.2f deg (10 m baseline), %.0f / %.0f / %.0f / %.0f m over 5 / 8 / 12 / 18 deg, %.1f m climbed and "
        "descended a km, cross slope at most %.1f deg at (%.0f, %.0f); shoulders at most %.1f deg at (%.0f, %.0f), their sharpest crease %.1f deg at (%.0f, %.0f)", g->metres / 1000.0, (double)g->steepest, (double)(g->steepAt[0] - half), (double)(g->steepAt[1] - half),
        (double)GradeRoad95(g), g->over[0], g->over[1], g->over[2], g->over[3], g->metres > 0.0 ? g->rise / (g->metres / 1000.0) : 0.0, (double)g->cross,
        (double)(g->crossAt[0] - half), (double)(g->crossAt[1] - half), (double)g->side, (double)(g->sideAt[0] - half), (double)(g->sideAt[1] - half), (double)g->crease,
        (double)(g->creaseAt[0] - half), (double)(g->creaseAt[1] - half));
}

/* Every arterial edge and spur on a surface: whole, clear of the pads and the rim (where the road is drawn whole) and
 * up to the places' edges. Three lines a class and <file>-<level>.tsv: class, index, edge, metres, steepest, 95th
 * percentile, metres over 5, 8, 12 and 18 degrees, climb and descent a km, worst cross slope, the deepest cut and
 * highest fill against the capture (`moved`); clear of pads: metres, steepest, metres over 5 and 8, cross slope, the
 * two worst places; the whole road's two worst places, each with the pad weight there and 1 inside a place; the
 * shoulders' steepest span and sharpest crease with places; up to the places' edges: metres, steepest, metres over 5
 * and 8, cross slope, the two places, shoulder and crease; then the steepest between neighbouring samples (whole, up
 * to the places' edges, its place) and the steepest across the bed 3 to 6 m off the line (the same three). Places
 * are metres from the world's centre, empty when the measure has no sample. base: GradeRoadShoulder's. */
static void RoadGradeReport(uintptr_t block, const char* what, const wchar_t* file, GradeHeightFn height, int moved, GradeHeightFn base)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    const uint32_t spurs = g_spurs.level == g_world.levels ? g_spurs.count : 0;
    wchar_t name[64];
    swprintf(name, 64, L"%ls-%ld.tsv", file, (long)g_world.levels);
    HANDLE out = pts != NULL ? OpenLog(name) : INVALID_HANDLE_VALUE;
    static GradeRoad sum[2], sumClear[2], sumOpen[2], one, clear, open;
    memset(sum, 0, sizeof sum); memset(sumClear, 0, sizeof sumClear); memset(sumOpen, 0, sizeof sumOpen);
    uint32_t parts[2] = {0, 0}, deep[2] = {0, 0}; float cut[2] = {0, 0}, fill[2] = {0, 0};
    char list[160] = ""; size_t listed = 0;
    g_grade.under = height;
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
        memset(&one, 0, sizeof one); memset(&clear, 0, sizeof clear); memset(&open, 0, sizeof open);
        GradeRoadAlong(height, base, NULL, pts + 2 * (size_t)first, end - first, &one);
        GradeRoadAlong(RoadGradeClear, base, NULL, pts + 2 * (size_t)first, end - first, &clear);
        GradeRoadAlong(RoadGradeOpen, base, NULL, pts + 2 * (size_t)first, end - first, &open);
        float low = 0.0f, high = 0.0f;
        for (uint32_t p = first; moved && p < end; p++)
        {
            const float z = RoadGradeDesigned(NULL, pts[2 * p], pts[2 * p + 1]), h = RoadGradeGround(NULL, pts[2 * p], pts[2 * p + 1]);
            if (!isfinite(z) || !isfinite(h)) continue;
            if (h - z > low) low = h - z;
            if (z - h > high) high = z - h;
        }
        if (one.metres == 0.0 && !moved) continue;
        GradeRoadAdd(&sum[spur], &one); GradeRoadAdd(&sumClear[spur], &clear); GradeRoadAdd(&sumOpen[spur], &open); parts[spur]++;
        if (low > cut[spur]) cut[spur] = low;
        if (high > fill[spur]) fill[spur] = high;
        if (low > 15.0f || high > 15.0f)
        {
            deep[spur]++;
            if (listed + 24 < sizeof list) listed += (size_t)snprintf(list + listed, sizeof list - listed, " %s %u", spur ? "spur" : "edge", spur ? k - n : k);
        }
        char line[1100], at[10][40]; DWORD wrote;
        RoadGradePlace(at[0], 40, clear.steepAt, clear.steepest > 0.0f); RoadGradePlace(at[1], 40, clear.crossAt, clear.cross > 0.0f);
        RoadGradePlace(at[2], 40, one.steepAt, one.steepest > 0.0f); RoadGradePlace(at[3], 40, one.crossAt, one.cross > 0.0f);
        RoadGradePlace(at[4], 40, one.sideAt, one.side > 0.0f); RoadGradePlace(at[5], 40, one.creaseAt, one.crease > 0.0f);
        RoadGradePlace(at[6], 40, open.steepAt, open.steepest > 0.0f); RoadGradePlace(at[7], 40, open.crossAt, open.cross > 0.0f);
        RoadGradePlace(at[8], 40, open.briefAt, open.brief > 0.0f); RoadGradePlace(at[9], 40, open.edgeAt, open.edge > 0.0f);
        const int len = snprintf(line, sizeof line, "%s\t%u\t%u\t%.1f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.0f\t%.1f\t%.2f\t%.2f\t%.2f\t%.1f\t%.2f\t%.0f\t%.0f\t%.2f\t%s\t%s"
            "\t%s\t%.2f\t%d\t%s\t%.2f\t%d\t%.2f\t%s\t%.2f\t%s\t%.1f\t%.2f\t%.0f\t%.0f\t%.2f\t%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%s\t%.2f\t%.2f\t%s\n",
            spur ? "spur" : "arterial", spur ? k - n : k, edge, one.metres, (double)one.steepest, (double)GradeRoad95(&one), one.over[0], one.over[1], one.over[2], one.over[3],
            one.metres > 0.0 ? one.rise / (one.metres / 1000.0) : 0.0, (double)one.cross, (double)low, (double)high, clear.metres, (double)clear.steepest, clear.over[0],
            clear.over[1], (double)clear.cross, at[0], at[1],
            at[2], (double)RoadGradePadAt(one.steepAt[0], one.steepAt[1]), RoadGradeInPlace(one.steepAt[0], one.steepAt[1]),
            at[3], (double)RoadGradePadAt(one.crossAt[0], one.crossAt[1]), RoadGradeInPlace(one.crossAt[0], one.crossAt[1]),
            (double)one.side, at[4], (double)one.crease, at[5], open.metres, (double)open.steepest, open.over[0], open.over[1], (double)open.cross, at[6], at[7],
            (double)open.side, (double)open.crease, (double)one.brief, (double)open.brief, at[8], (double)one.edge, (double)open.edge, at[9]);
        if (out != INVALID_HANDLE_VALUE) WriteFile(out, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    for (int c = 0; c < 2; c++)
    {
        char text[400];
        RoadGradeLine(text, sizeof text, &sum[c]);
        if (moved)
            Note("roads: level %ld grade, %s: %u %s: %s; deepest cut %.1f m, highest fill %.1f m, %u with over 15 m", (long)g_world.levels, what, parts[c],
                c ? "spurs" : "arterial edges", text, (double)cut[c], (double)fill[c], deep[c]);
        else Note("roads: level %ld grade, %s: %u %s: %s", (long)g_world.levels, what, parts[c], c ? "spurs" : "arterial edges", text);
        RoadGradeLine(text, sizeof text, &sumClear[c]);
        Note("roads: level %ld grade, %s, clear of pads and the rim: %u %s: %s", (long)g_world.levels, what, parts[c], c ? "spurs" : "arterial edges", text);
        RoadGradeLine(text, sizeof text, &sumOpen[c]);
        Note("roads: level %ld grade, %s, up to the places' edges: %u %s: %s", (long)g_world.levels, what, parts[c], c ? "spurs" : "arterial edges", text);
    }
    if (moved && listed != 0) Note("roads: level %ld grade: cut or fill over 15 m on:%s", (long)g_world.levels, list);
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
}

/* grade-stations-<level>.tsv: every station's x, y (metres from the world's centre), ground, target, the pins' low and
 * high, pinned, links, cross tangent, band, cap, pad weight and 1 inside a place: what the profile was solved from;
 * then the steepest its links may take in degrees (0: all between pins) and 1 for a spur's end at its place's edge. */
static void RoadGradeStationsFile(void)
{
    const GradeField* f = &g_grade.f;
    const float half = g_grade.world / 2;
    wchar_t name[48];
    swprintf(name, 48, L"grade-stations-%ld.tsv", (long)g_world.levels);
    HANDLE out = OpenLog(name);
    if (out == INVALID_HANDLE_VALUE) return;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        char line[240]; DWORD wrote;
        float most = 0.0f;
        for (uint32_t k = 0; k < f->s[i].links; k++)
        {
            const float lim = f->s[i].lim[k] > GRADE_HARD ? f->s[i].lim[k] : GRADE_HARD;
            if (lim < GRADE_FREE && lim > most) most = lim;
        }
        const int len = snprintf(line, sizeof line, "%u\t%.1f\t%.1f\t%.2f\t%.2f\t%.2f\t%.2f\t%u\t%u\t%.3f\t%.1f\t%.2f\t%.2f\t%d\t%.2f\t%u\n", i, (double)(f->xy[2 * i] - half), (double)(f->xy[2 * i + 1] - half),
            (double)f->s[i].ground, (double)f->s[i].target, (double)(f->s[i].low > -1.0e29f ? f->s[i].low : -9999.0f), (double)(f->s[i].high < 1.0e29f ? f->s[i].high : 9999.0f),
            f->s[i].pinned, f->s[i].links, (double)f->s[i].cross, (double)f->band[i], (double)f->s[i].earth, (double)RoadGradePadAt(f->xy[2 * i], f->xy[2 * i + 1]),
            RoadGradeInPlace(f->xy[2 * i], f->xy[2 * i + 1]), (double)(atanf(most) * 57.29578f), f->s[i].edge);
        WriteFile(out, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    CloseHandle(out);
}

/* pads-<level>.tsv (research): every pad shape as the roads see it: kind (pad, zone, foot), circle or rect, centre
 * (metres from the world's centre), a, b, the bands, strength and the rectangle's axis. */
static void RoadGradePadsFile(void)
{
    wchar_t name[40];
    swprintf(name, 40, L"pads-%ld.tsv", (long)g_world.levels);
    HANDLE out = g_pads.level == g_world.levels && g_pads.count > 0 ? OpenLog(name) : INVALID_HANDLE_VALUE;
    if (out == INVALID_HANDLE_VALUE) return;
    for (int q = 0; q < g_pads.count; q++)
    {
        const NearPad* p = &g_pads.pad[q];
        char line[160]; DWORD wrote;
        const int len = snprintf(line, sizeof line, "%s\t%s\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.3f\t%.4f\t%.4f\n", p->foot ? "foot" : p->zone ? "zone" : "pad", p->rect ? "rect" : "circle",
            (double)p->cx, (double)p->cy, (double)p->a, (double)p->b, (double)p->ba, (double)p->bb, (double)p->k, (double)p->c, (double)p->s);
        WriteFile(out, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    CloseHandle(out);
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
        if (!(RoadGradeShare(xy[0], xy[1], &z, &d) > 0.0f) || !isfinite(z)) continue;
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

/* The dispatcher's hook at the level's op 0x38, the merge's first: once the level's roads were solved, a second merge
 * on the same level is the generation begun anew. Its ground is ungraded, so the roads are solved and drawn again. */
static void RoadGradeBegin(void)
{
    if (g_grade.level != g_world.levels || g_grade.level < 0) return;
    Note("roads: level %ld grade: the merge began again after the roads were graded (the generation restarted): they are graded again", (long)g_world.levels);
    g_grade.level = -1; g_grade.afterLevel = -1; g_grade.ready = 0; g_grade.phase = 0;
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
        Note("roads: level %ld grade: %u stations on %u pieces (%u pinned on pads' flat ground or at the rim, %u pieces unlinked, %u ties between roads side by side, %u without "
            "room), the %ux%u capture at %.0f m; profile: steepest %.2f deg, sharpest bend %.2f deg, deepest cut %.1f m, highest fill %.1f m, %.0f m moved over 8 m and %.0f m "
            "over 15 m; %u stations between pins the limit cannot join, %u pins left by over %.2f m (the furthest %.2f m at (%.0f, %.0f)), %u bands at the widest (%.0f m: a bank over 12 deg on the ground's side slope there); %.0f ms",
            (long)g_world.levels, g_grade.f.stations, g_grade.f.segs, g_grade.pinned, g_grade.f.unlinked, g_grade.ties, g_grade.f.untied, dims[0], dims[1],
            (double)(g_grade.world / (float)dims[0]), (double)g.steepest, (double)g.bend, (double)g.cut, (double)g.fill, g.cut8, g.cut15, g_grade.torn, g_grade.missed,
            (double)GRADE_MISS, (double)g_grade.miss, (double)(g_grade.missAt[0] - g_grade.world / 2), (double)(g_grade.missAt[1] - g_grade.world / 2), g_grade.f.capped,
            (double)(GRADE_FULL + GRADE_BAND_MOST), (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
        Note("roads: level %ld grade: the cap: %.1f deg where the road stays within %.1f m of the ground, %.1f deg everywhere; %u stations (%.0f m of road) moved past the cap "
            "by the %.1f deg limit or a pad's height, the furthest %.2f m past it at (%.0f, %.0f)", (long)g_world.levels, (double)(atanf(GRADE_SOFT) * 57.29578f), (double)GRADE_EARTH,
            (double)(atanf(GRADE_HARD) * 57.29578f), g_grade.deep, g_grade.deepMetres, (double)(atanf(GRADE_HARD) * 57.29578f), (double)g_grade.deepest,
            (double)(g_grade.deepAt[0] - g_grade.world / 2), (double)(g_grade.deepAt[1] - g_grade.world / 2));
        Note("roads: level %ld grade: steep ground: %u links (%.0f m of road) may take up to %.1f deg, where %.1f deg would stand the road over %.1f m off the ground; "
            "the steepest taken %.1f deg at (%.0f, %.0f)", (long)g_world.levels, g_grade.steep, g_grade.steepMetres, (double)(atanf(GRADE_STEEP) * 57.29578f),
            (double)(atanf(GRADE_HARD) * 57.29578f), (double)(2.0f * GRADE_EARTH), (double)(atanf(g_grade.steepest) * 57.29578f),
            (double)(g_grade.steepAt[0] - g_grade.world / 2), (double)(g_grade.steepAt[1] - g_grade.world / 2));
        Note("roads: level %ld grade: junctions: %u stations where roads meet or a line turns over 30 deg, %u links within %.0f m of them held to %.1f deg; the rim: %u "
            "stations within %.0f m of the playable radius (%.0f m) or past it%s; relaxed: %.2f %% of the roads' length may take over %.1f deg", (long)g_world.levels,
            g_grade.landings, g_grade.landed, (double)GRADE_LAND, (double)(atanf(GRADE_LAND_SLOPE) * 57.29578f), g_grade.past, (double)GRADE_RIM, (double)g_grade.playable,
            g_grade.past != 0 ? " (graded like the rest: the rim holds nothing on this level)" : "", g.metres > 0.0 ? 100.0 * g_grade.steepMetres / g.metres : 0.0,
            (double)(atanf(GRADE_HARD) * 57.29578f));
        RoadGradeStationsFile();
        RoadGradePadsFile();
        g_grade.ready = 1;
        RoadGradeReport(gen, "the ground before (the capture)", L"grade-before", RoadGradeGround, 0, NULL);
        RoadGradeReport(gen, "the roads as designed", L"grade-designed", RoadGradeDesigned, 1, NULL);
        RoadGradeReport(gen, "what the grading adds (the eased weight times the cut or fill; shoulders: over the ground's own side slope)", L"grade-move", RoadGradeMove, 0, RoadGradeGround);
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
    RoadGradeReport(block, g_grade.off ? "the level's 4 m heights (not graded)" : "the level's 4 m heights after grading", L"grade-after", RoadGradeLevel, 0, NULL);
}

/* ---- a near tile ----
 * The whole band takes the level's surface, as under a pad (NearTileShare): the weight for NearPadRaster's cell. */
static float RoadGradeTileWeight(float wx, float wy)
{
    if (!g_grade.ready || g_grade.off || g_grade.level != g_world.levels) return 0.0f;
    float z, d;
    return GradeSmooth(RoadGradeShare(wx + g_grade.world / 2, wy + g_grade.world / 2, &z, &d));
}
/* The level's surface at a tile texel, sharpened on the core: the level holds the road at 4 m a sample, the tile
 * has 0.56 m, so within half the core it takes the roads' own height and eases to the level's surface at the core's edge.
 * Where the level stands GRADE_SHARP off the roads' height something later than the roads shaped it (a stamp's terrain
 * at op 0x63): the share eases to none there, so the tile keeps what the level shows. */
#define GRADE_SHARP 1.0f
static float RoadGradeSharpen(float wx, float wy, float coarse)
{
    if (!g_grade.ready || g_grade.off || g_grade.level != g_world.levels) return coarse;
    float z = NAN, d;
    const float x = wx + g_grade.world / 2, y = wy + g_grade.world / 2;
    const float share = RoadGradeShare(x, y, &z, &d); /* within the core the field's weight is 1: the share alone */
    if (!(share > 0.0f) || !(d < GRADE_CORE) || !isfinite(z)) return coarse;
    float s = (GRADE_CORE - d) / (GRADE_CORE / 2);
    s = (s > 1.0f ? 1.0f : s) * share * (1.0f - GradeSmooth(fabsf(z - coarse) / GRADE_SHARP));
    return coarse + s * (z - coarse);
}
/* How much of a tile's own fine relief a graded road keeps at a point: none on the worn core (GRADE_FLAT to either
 * side: an FRV's 2.6 m track with room to pass another), rising without a crease to GRADE_ROUGH of it at the bed's
 * edge, and from there with the tile's share to all of it at the band's end. So the bed is no bare bench and no line
 * shows where the detail stops. 0 off the roads. */
#define GRADE_FLAT 4.0f
#define GRADE_ROUGH 0.35f
static float RoadGradeDetail(float wx, float wy)
{
    if (!g_grade.ready || g_grade.off || g_grade.level != g_world.levels) return 0.0f;
    float z, d;
    const float share = GradeSmooth(RoadGradeShare(wx + g_grade.world / 2, wy + g_grade.world / 2, &z, &d));
    return share * GRADE_ROUGH * GradeSmooth((d - GRADE_FLAT) / (GRADE_FULL - GRADE_FLAT));
}
/* When a tile's heights are in: the roads on them, where they cross its hidden square. */
static void RoadGradeTileNote(void)
{
    uintptr_t gen = 0;
    if (!g_grade.ready || g_grade.level != g_world.levels || SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0) return;
    /* a tile that comes in again has the same heights: its rows are written once */
    static struct { LONG level; float at[2]; } seen[64]; static uint32_t count;
    for (uint32_t k = 0; k < count && k < 64; k++) if (seen[k].level == g_world.levels && seen[k].at[0] == g_near.centre[0] && seen[k].at[1] == g_near.centre[1]) return;
    seen[count % 64].level = g_world.levels; seen[count % 64].at[0] = g_near.centre[0]; seen[count % 64].at[1] = g_near.centre[1]; count++;
    char what[96];
    snprintf(what, sizeof what, "the near tile at (%.0f, %.0f), 0.56 m heights", (double)g_near.centre[0], (double)g_near.centre[1]);
    wchar_t file[40]; /* a file a tile: grade-tile_<x>_<y>-<level>.tsv */
    swprintf(file, 40, L"grade-tile_%.0f_%.0f", (double)g_near.centre[0], (double)g_near.centre[1]);
    RoadGradeReport(gen, what, file, RoadGradeTile, 0, NULL);
}
