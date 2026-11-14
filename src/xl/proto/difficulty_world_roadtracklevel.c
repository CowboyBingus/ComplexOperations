/* ---- Roads: the level's worn track ---------------------------------------------------------------------------------
 * The pieces, the slot and the places' flat ground are made once a level, as its op 0xB7 begins (where the grading
 * reads the same road points): the zones, the spurs and the pads are final then, and no rendered map is needed, so a
 * peer whose read-back fails still has the same track and thins the same plants. Every later map (the level's, each
 * near tile's, also the tiles built in the mission) reads this data. Everything the track depends on is generation
 * data: the road points, the zone types beside the edges (an edge's cells +0x20 / +0x28, the subregion of each, the
 * type at subregion +0xC), the types' painter lists, the pads and the playable radius. No rendered map is read, so
 * every peer draws the same track. It has its own index, not the graded field's, so it is drawn with the grading off.
 * Research switches: HELLFORGE_XL_ROAD_TRACK=0 leaves the maps as they are (the slot is still chosen and written),
 * HELLFORGE_XL_ROAD_TRACK_SLOT=<1..31> gives every road that slot, HELLFORGE_XL_ROAD_TRACK_THIN=0 keeps the plants. */
static struct
{
    LONG level;
    int on;
    uint32_t slot;
    double half, playable;
    TrackIndex ix;
    TrackFlats flats;
} g_track = {.level = -1};

static int RoadTrackSwitch(const char* name)
{
    char text[8] = "";
    return !(GetEnvironmentVariableA(name, text, sizeof text) > 0 && text[0] == '0');
}
static uint32_t RoadTrackForced(void)
{
    char text[8] = "";
    if (GetEnvironmentVariableA("HELLFORGE_XL_ROAD_TRACK_SLOT", text, sizeof text) == 0) return 0;
    const int v = atoi(text);
    return v >= 1 && v < TRACK_SLOTS ? (uint32_t)v : 0;
}

/* A type's painters, read once a level; NULL for no type (a side type can be -1 or -2: no cell, no subregion). */
#define TRACK_TYPES 256
typedef struct TrackTypes { uint64_t table; uint32_t count, bad; uint8_t have[TRACK_TYPES]; TrackPainters p[TRACK_TYPES]; } TrackTypes;
static const TrackPainters* RoadTrackType(TrackTypes* t, int32_t type)
{
    if (type < 0 || type >= TRACK_TYPES || (uint32_t)type >= t->count) return NULL;
    if (!t->have[type])
    {
        uint64_t descriptor = 0;
        t->have[type] = 1;
        if (SafeRead((uintptr_t)t->table + 8ull * (uint32_t)type, &descriptor, 8)) descriptor = 0;
        if (!TrackTypePainters((uintptr_t)descriptor, &t->p[type])) t->bad++;
    }
    return &t->p[type];
}

/* " 1:112 6:122" for the slots that have a count. */
static void RoadTrackBySlot(char* out, size_t size, const uint32_t* count)
{
    size_t at = 0;
    out[0] = 0;
    for (uint32_t s = 1; s < TRACK_SLOTS && at + 16 < size; s++)
        if (count[s] != 0) at += (size_t)snprintf(out + at, size - at, " %u:%u", s, count[s]);
}

/* What the places and the map's rim leave of the track at a point (centred metres). */
static double RoadTrackShare(double x, double y)
{
    double rim = (g_track.playable - sqrt(x * x + y * y)) / (double)GRADE_RIM;
    rim = rim < 0.0 ? 0.0 : rim > 1.0 ? 1.0 : rim;
    return rim * TrackFlatShare(&g_track.flats, x, y);
}

/* The level's pads that are level ground (op 0x3F's; not a stamp's footprint, not a flatten zone), copied: the
 * tiles' rows read them from other threads for the level's life. */
static int RoadTrackFlats(void)
{
    int kept = 1;
    TrackFlatsFree(&g_track.flats);
    for (int q = 0; g_pads.level == g_world.levels && q < g_pads.count; q++)
    {
        const NearPad* p = &g_pads.pad[q];
        if (p->foot || p->zone || !(p->k >= GRADE_PIN)) continue;
        const TrackFlat f = {p->cx, p->cy, p->c, p->s, p->a, p->b, p->rect};
        kept &= TrackFlatsAdd(&g_track.flats, &f);
    }
    return kept && TrackFlatsBuild(&g_track.flats, 2.0 * g_track.half);
}

/* The pieces of the level's arterials and spurs with their slots, and roadtrack-<level>.tsv for an audit: a row an
 * arterial edge ("a", edge, the two side types (-1: none), slot, 1 when it is not the level's, pieces, metres) and a
 * row a spur ("s", spur, the edge it joins, -, slot, 0, pieces, metres). */
static void RoadTrackBuild(void)
{
    static TrackTypes types; static uint64_t cellOf[8192]; static int32_t typeOf[8192];
    static const TrackPainters* sideA[ROADS_GRAPH_EDGES]; static const TrackPainters* sideB[ROADS_GRAPH_EDGES];
    static double metres[ROADS_GRAPH_EDGES]; static int32_t side[ROADS_GRAPH_EDGES][2];
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0, subs = 0, size = 0; float fraction = 0.0f;
    uintptr_t gen = 0; uint64_t ts = 0;
    if (g_track.level == g_world.levels) return; /* once a level */
    g_track.on = 0; g_track.slot = 0; g_track.level = g_world.levels;
    TrackIndexFree(&g_track.ix);
    if (g_world.current != 8.0f || g_arterials.level != g_world.levels || g_arterials.count == 0 || g_worldLevel == 0 || SafeRead(g_worldLevel + 0x2C8, &gen, 8) || gen == 0) return;
    const uint32_t n = RoadsReadEdges(gen, &L, &e);
    float* pts = RoadsReadPoints(gen, &L, &points);
    uint32_t* slot = n != 0 ? (uint32_t*)calloc(n, 2 * sizeof(uint32_t)) : NULL; /* per edge: the slot, then its place among the roads */
    memset(&types, 0, sizeof types);
    /* the settings are the level's own block (level + 0x2D20): the world's size +0x2C, the playable fraction +0x50 */
    if (slot == NULL || pts == NULL || SafeRead(gen + 0x11A71C4u, &subs, 4) || subs > 8192 || SafeRead(g_worldLevel + 0x2D0, &ts, 8) || ts == 0 ||
        SafeRead((uintptr_t)ts, &types.table, 8) || types.table == 0 || SafeRead((uintptr_t)ts + 0xA0, &types.count, 4) ||
        SafeRead(g_worldLevel + 0x2D20 + 0x2C, &size, 4) || SafeRead(g_worldLevel + 0x2D20 + 0x50, &fraction, 4) || (float)size != 2048.0f * g_world.current ||
        RoadsDiscKm2(size, fraction, &g_track.playable) == 0)
    {
        Note("roads: level %ld track: the roads, the subregions, the zone types or the world's size are not readable; no track is drawn", (long)g_world.levels);
        free(slot); RoadsFreeEdges(e, n); if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
        return;
    }
    g_track.half = (double)size / 2;
    for (uint32_t i = 0; i < subs; i++)
    {
        cellOf[i] = 0; typeOf[i] = -1;
        SafeRead(gen + 0x92F328u + 0x20ull * i + 0x18, &cellOf[i], 8); SafeRead(gen + 0x92F328u + 0x20ull * i + 0xC, &typeOf[i], 4);
    }
    const uint8_t* raw = (const uint8_t*)e - (size_t)n * 0x48; /* RoadsReadEdges keeps the records before the parsed edges */
    uint32_t* place = slot + n;
    memset(place, 0xFF, sizeof(uint32_t) * (size_t)n);
    uint32_t roads = 0;
    for (uint32_t i = 0; i < n && roads < ROADS_GRAPH_EDGES; i++)
    {
        if (!((g_arterials.bits[i >> 3] >> (i & 7)) & 1) || !RoadEdgeHasPoints(&e[i], points)) continue;
        uint64_t cell[2]; int32_t t[2] = {-1, -1};
        memcpy(cell, raw + (size_t)i * 0x48 + 0x20, 16);
        for (int q = 0; q < 2; q++)
            for (uint32_t k = 0; cell[q] != 0 && k < subs; k++) if (cellOf[k] == cell[q]) { t[q] = typeOf[k]; break; }
        const int32_t low = t[0] < 0 ? t[1] : t[1] < 0 ? t[0] : t[0] < t[1] ? t[0] : t[1], high = t[0] < 0 || t[1] < 0 || t[0] == t[1] ? -1 : t[0] < t[1] ? t[1] : t[0];
        sideA[roads] = RoadTrackType(&types, low); sideB[roads] = sideA[roads] != NULL ? RoadTrackType(&types, high) : NULL;
        metres[roads] = RoadEdgeAlong(&e[i], pts);
        side[roads][0] = t[0]; side[roads][1] = t[1];
        place[i] = roads++;
    }
    double clear = 0.0, all = 0.0, hidden = 0.0;
    const uint32_t forced = RoadTrackForced();
    const uint32_t chosen = TrackLevelSlot(sideA, sideB, metres, roads, &clear), level = forced ? forced : chosen;
    uint32_t bySlot[TRACK_SLOTS] = {0}, spurs = 0, lost = 0, other = 0, typeless = 0;
    const float half = (float)g_track.half;
    const int flats = RoadTrackFlats();
    wchar_t name[48]; char line[112]; DWORD wrote;
    swprintf(name, 48, L"roadtrack-%ld.tsv", (long)g_world.levels);
    HANDLE file = OpenLog(name);
    for (uint32_t k = 0; k < n + (g_spurs.level == g_world.levels ? g_spurs.count : 0); k++)
    {
        const int spur = k >= n;
        uint32_t first, end, edge = k;
        if (!spur)
        {
            if (place[k] >= roads) continue;
            first = e[k].first; end = e[k].end;
            slot[k] = forced ? forced : TrackRoadSlot(sideA[place[k]], sideB[place[k]], level);
            if (slot[k] != 0) bySlot[slot[k]]++;
            other += slot[k] != level; typeless += sideA[place[k]] == NULL; all += metres[place[k]];
        }
        else
        {
            const RoadSpur* s = &g_spurs.spur[k - n];
            if (s->end > points || s->end < s->first + 2 || s->edge >= n) continue;
            first = s->first; end = s->end; edge = s->edge; spurs++;
            if (slot[edge] == 0) slot[edge] = level; /* a spur is a road whatever the edge it joins is */
        }
        double length = 0.0;
        for (uint32_t p = first + 1; p < end; p++)
        {
            const float ax = pts[2 * (p - 1)] - half, ay = pts[2 * (p - 1) + 1] - half, bx = pts[2 * p] - half, by = pts[2 * p + 1] - half;
            const double piece = hypot((double)bx - ax, (double)by - ay);
            length += piece;
            if (flats && RoadTrackShare(((double)ax + bx) / 2, ((double)ay + by) / 2) < 0.5) hidden += piece;
            if (slot[edge] != 0) lost += !TrackIndexAdd(&g_track.ix, ax, ay, bx, by, slot[edge]);
        }
        if (file == INVALID_HANDLE_VALUE) continue;
        const int len = spur ? snprintf(line, sizeof line, "s\t%u\t%u\t-\t%u\t0\t%u\t%.1f\n", k - n, edge, slot[edge], end - first - 1, length) :
            snprintf(line, sizeof line, "a\t%u\t%d\t%d\t%u\t%u\t%u\t%.1f\n", k, side[place[k]][0], side[place[k]][1], slot[k], slot[k] != level, end - first - 1, length);
        WriteFile(file, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    const int wrote_ = file != INVALID_HANDLE_VALUE;
    if (wrote_) CloseHandle(file);
    free(slot); RoadsFreeEdges(e, n); VirtualFree(pts, 0, MEM_RELEASE);
    const int indexed = level != 0 && lost == 0 && TrackIndexBuild(&g_track.ix, 2.0 * g_track.half);
    char by[256];
    RoadTrackBySlot(by, sizeof by, bySlot);
    Note("roads: level %ld track: slot %u%s (ground on neither side along %.1f of %.1f km of arterials); %u pieces on %u arterial edges and %u spurs; edges by slot:%s; "
        "%u edges took their own slot (the level's is ground on both sides), %u edges without a zone type, %u zone types not the reviewed shape; %u flat pads, "
        "%.0f m of track left to their ground or the rim; roadtrack-%ld.tsv %s", (long)g_world.levels, level,
        forced ? " (HELLFORGE_XL_ROAD_TRACK_SLOT, research)" : "", clear / 1000.0, all / 1000.0, g_track.ix.n, roads, spurs, by, other, typeless, types.bad,
        g_track.flats.n, hidden, (long)g_world.levels, wrote_ ? "written" : "not written (the file did not open)");
    if (level == 0) Note("roads: level %ld track: no zone of the biome beside a road has a path painter; no track is drawn", (long)g_world.levels);
    else if (!indexed || !flats) Note("roads: level %ld track: no memory for the pieces or the pads; no track is drawn", (long)g_world.levels);
    else if (!RoadTrackSwitch("HELLFORGE_XL_ROAD_TRACK")) Note("roads: level %ld track: off (HELLFORGE_XL_ROAD_TRACK=0, research): the material maps stay as they are", (long)g_world.levels);
    else { g_track.on = 1; g_track.slot = level; }
    if (g_track.on && !NearCoarseMatOn())
        Note("roads: level %ld track: the coarse map is off (HELLFORGE_XL_COARSE_MAT=0): no far track and no plants thinned by the slot; near tiles only", (long)g_world.levels);
}

static void RoadTrackAt(double x, double y, double texel, uint32_t* id, double* w, int* n, int room, uint32_t* tally)
{
    if (!g_track.on || g_track.level != g_world.levels) return;
    double d = 0.0, c; uint32_t slot = 0;
    if (texel > TRACK_FINE)
    {
        if (!TrackNearest(&g_track.ix, x, y, TRACK_FAR_REACH, &d, &slot, NULL)) return;
        c = TrackCoarse(d);
    }
    else
    {
        double qx, qy;
        if (TrackNoneNear(&g_track.ix, x, y)) return;
        TrackWarp(x, y, &qx, &qy);
        if (!TrackNearest(&g_track.ix, qx, qy, TRACK_REACH, &d, &slot, NULL)) return;
        c = TrackWeight(d, x, y);
    }
    if (!(c > 0.0)) return;
    c *= RoadTrackShare(x, y);
    if (!(c > 0.0)) { tally[0]++; return; }
    if (TrackMix(id, w, n, room, slot, c)) tally[slot]++;
}

/* 1 when the track can differ between (x, y) and a point of the 4 m cell that starts there. */
static int RoadTrackNear(double x, double y) { return g_track.on && g_track.level == g_world.levels && !TrackNoneNear(&g_track.ix, x, y); }

/* A map's count after its rows: the texels the track was mixed into, by slot. */
static void RoadTrackNote(const char* what, const volatile LONG* tally)
{
    if (!g_track.on || g_track.level != g_world.levels) return;
    uint32_t count[TRACK_SLOTS], total = 0; char by[256];
    for (uint32_t s = 0; s < TRACK_SLOTS; s++) { count[s] = (uint32_t)tally[s]; if (s != 0) total += count[s]; }
    RoadTrackBySlot(by, sizeof by, count);
    Note("roads: level %ld track: %s: %u texels written (by slot:%s), %u more left to a place's flat ground or the rim", (long)g_world.levels, what, total, by, count[0]);
}

/* ---- the plants ----
 * The slot alone does not clear the line: its scatter records still let 3 to 68 % of the candidates grow, by biome,
 * and grass or rocks on the line hide a road from a driver. The scatter callback rejects a candidate whose 7-bit draw exceeds the density in
 * bits 0-6 of its ground-id texel (lg+0x108: w +0, h +4, data +0x10; nearest texel, column trunc((w - 1) u), row
 * h - 1 - trunc((h - 1) v), u and v the point over the world + 0.5; game 0x137A695..0x137A731). So the density is
 * scaled by what the far profile's ramp leaves: every brush thins alike, to 1 in 128 within 2 m of the line. After the level's op
 * 0xBB (it hands the image to the callback's globals) and before op 0x95 registers the callback. The id fields are
 * not touched. */
static uint32_t RoadTrackThinImage(volatile uint32_t* image, uint32_t side, uint32_t* bare, uint64_t* before, uint64_t* after)
{
    const double world = 2.0 * g_track.half;
    uint32_t thinned = 0;
    for (uint32_t r = 0; r < side; r++)
        for (uint32_t c = 0; c < side; c++)
        {
            const double x = ((c + 0.5) / (side - 1) - 0.5) * world, y = ((side - 1 - r + 0.5) / (side - 1) - 0.5) * world;
            double dist = 0.0; uint32_t slot = 0;
            if (!TrackNearest(&g_track.ix, x, y, TRACK_FAR_REACH, &dist, &slot, NULL)) continue;
            const double keep = 1.0 - TrackCoarse(dist) * RoadTrackShare(x, y);
            const uint32_t texel = image[(size_t)r * side + c], density = texel & 127u;
            const uint32_t now = (uint32_t)floor(density * keep);
            if (now == density) continue;
            image[(size_t)r * side + c] = (texel & ~127u) | now;
            thinned++; *bare += now == 0; *before += density; *after += now;
        }
    return thinned;
}
#define TRACK_GROUND_FORMAT 0x2Au
/* The level's ground-id image when it is the one this was written for: the image op 0xBB has just handed to the
 * callback (its globals +0x20), square, of 32-bit texels (format 0x2A in the engine's table of bits a texel, exe
 * 0x1658990), and wholly in writable memory. 0, else 1 (not the callback's) or 2 (not the shape); d: w, h, format. */
static int RoadTrackGround(uintptr_t block, uint32_t d[4], uint64_t* data)
{
    uint64_t handed = 0; MEMORY_BASIC_INFORMATION mi;
    memset(d, 0, 16); *data = 0;
    if (SafeRead(g_world.game + 0x3328F60u + 0x20, &handed, 8) || handed != (uint64_t)(block + 0x8758A8u + 0x108)) return 1;
    if (SafeRead(block + 0x8758A8u + 0x108, d, 16) || SafeRead(block + 0x8758A8u + 0x108 + 0x10, data, 8) || *data == 0 || d[0] < 256 || d[0] > 8192 || d[1] != d[0] ||
        d[2] != TRACK_GROUND_FORMAT || VirtualQuery((void*)(uintptr_t)*data, &mi, sizeof mi) == 0 || mi.State != MEM_COMMIT || !(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
        (uintptr_t)mi.BaseAddress + mi.RegionSize < (uintptr_t)*data + (size_t)d[0] * d[1] * 4) return 2;
    return 0;
}
static void RoadTrackThin(uintptr_t block)
{
    static LONG done = -1;
    if (g_world.current != 8.0f || g_arterials.level != g_world.levels || g_arterials.count == 0 || done == g_world.levels) return;
    done = g_world.levels;
    if (!g_track.on || g_track.level != g_world.levels)
    { Note("roads: level %ld track: plants not thinned: the track is not made or off at op 0xBB", (long)g_world.levels); return; }
    if (!RoadTrackSwitch("HELLFORGE_XL_ROAD_TRACK_THIN"))
    { Note("roads: level %ld track: plants not thinned (HELLFORGE_XL_ROAD_TRACK_THIN=0, research)", (long)g_world.levels); return; }
    uint32_t d[4]; uint64_t data;
    const int why = RoadTrackGround(block, d, &data);
    if (why == 1) { Note("roads: level %ld track: plants not thinned: op 0xBB did not hand the level's ground-id image to the scatter callback", (long)g_world.levels); return; }
    if (why != 0) { Note("roads: level %ld track: plants not thinned: the ground-id image is not the reviewed one (%u x %u format %u)", (long)g_world.levels, d[0], d[1], d[2]); return; }
    uint32_t bare = 0; uint64_t before = 0, after = 0;
    const uint32_t side = d[0], thinned = RoadTrackThinImage((volatile uint32_t*)(uintptr_t)data, side, &bare, &before, &after);
    Note("roads: level %ld track: plants thinned on %u texels of the %u x %u ground-id image (%u to none; mean density %.1f -> %.1f of 127)", (long)g_world.levels,
        thinned, side, side, bare, thinned ? (double)before / thinned : 0.0, thinned ? (double)after / thinned : 0.0);
}
