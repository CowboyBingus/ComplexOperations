/* Diagnostic, every build (issue G, terminids rolled on flat ground): what the rotation component's ground alignment
 * reads. Rotation 0x620B60 (mgr [game+0x33264A0]: +0x2C count, +0x38 hash, +0x50 instances, +0x58 records 0x298, +0x60
 * records 0xC with +8 enabled) sets each unit's rotation from R+0x54's mode: 2/3 bone rays, 4 four corner rays, 1 the
 * ground record's normal. A unit is off its ground by the angle between its up and the drawn ground's normal. Twice a
 * second; at most LW_TILT_LINES unit lines a level, one per unit per 20 s, and a summary with the 30 s notes. */
#define LW_TILT_ROT 0x33264A0u
#define LW_TILT_UNITS 0x3326508u     /* unit component: hash +0x40, records +0x68 (0x308: +0x2D0 quaternion, +0x2E0 position) */
#define LW_TILT_PHYS 0x3326328u      /* physics API: +0x68 ray (world, origin, dir, length, filter, hits 0x2C each, max) */
#define LW_TILT_LINES 48
#define LW_TILT_SEEN 64
#define LW_TILT_EXCESS 12.0f         /* degrees off the ground's normal that make a line */

static struct
{
    LONG level; ULONGLONG at; int lines;
    uint32_t scans, units, modes[8], tilted, off30, off30Miss, worstId, worstMode; float worst;
    uint32_t longs, longOver, longId; float longWorst; /* corner rays over 3 m apart (chargers' size) on a detailed tile */
    uint32_t seenId[LW_TILT_SEEN]; ULONGLONG seenAt[LW_TILT_SEEN];
} g_lwTilt = {.level = -1};

typedef uint32_t (*LwTiltRayFn)(uint32_t world, const float* origin, const float* dir, float length, uint32_t filter, void* hits, uint32_t max);

/* v rotated by quaternion q (x, y, z, w) */
static void LwTiltRotate(const float* q, const float* v, float* out)
{
    float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]), tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static float LwTiltDeg(float upZ) { return acosf(upZ > 1.0f ? 1.0f : upZ < -1.0f ? -1.0f : upZ) * 57.29578f; }

/* The ground's normal under xy: the drawn ground on a level with moving tiles (1 m either side), else the world ground
 * query's. 0 when neither reads. */
static int LwTiltGround(float x, float y, float* n)
{
    int tile;
    if (MoveTilesOn())
    {
        float e = MoveDrawnGround(x + 1, y, &tile), w = MoveDrawnGround(x - 1, y, &tile), no = MoveDrawnGround(x, y + 1, &tile), so = MoveDrawnGround(x, y - 1, &tile);
        if (e > -9000.0f && w > -9000.0f && no > -9000.0f && so > -9000.0f)
        {
            float gx = (e - w) / 2.0f, gy = (no - so) / 2.0f, l = sqrtf(1.0f + gx * gx + gy * gy);
            n[0] = -gx / l; n[1] = -gy / l; n[2] = 1.0f / l;
            return 1;
        }
    }
    typedef uint8_t (*GroundFn)(uintptr_t world, const float* in, float* outZ, uint8_t reject, float* normal, uint8_t mode);
    uintptr_t world; float in[3] = {x, y, 0}, z = NAN;
    n[0] = n[1] = n[2] = 0;
    if (!EvpPtr(g_lw.game + 0x346D508u, &world)) return 0;
    ((GroundFn)(g_lw.game + 0x13CA380u))(world, in, &z, 0, n, 0);
    return isfinite(z) && n[2] > 0.0f;
}

/* Degrees between two unit vectors. */
static float LwTiltAngle(const float* a, const float* b) { return LwTiltDeg(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]); }

/* The streamed stamp nearest (x, y) within 32 m that the streamer holds live: its slot, distance and whether the point
 * is inside its footprint (record +0xB0, map metres). -1 when none; a busy lock reads as none. */
static int LwTiltStamp(float x, float y, float* dist, int* inside)
{
    int best = -1; float bd = 32.0f;
    *inside = 0;
    if (!TryAcquireSRWLockShared(&g_streamLock)) return -1;
    for (uint32_t i = 0; i < g_stream.count && i < kStreamMaxLocations; i++)
    {
        if (!g_streamLive[i]) continue;
        float d = hypotf(g_streamAt[i][0] - x, g_streamAt[i][1] - y);
        if (d < bd) { bd = d; best = (int)i; }
    }
    float box[4];
    if (best >= 0 && !SafeRead((uintptr_t)g_stream.array + (size_t)best * kStreamRecord + 0xB0, box, sizeof box))
    {
        float mx = x + g_stream.half, my = y + g_stream.half;
        *inside = mx >= fminf(box[0], box[2]) && mx <= fmaxf(box[0], box[2]) && my >= fminf(box[1], box[3]) && my <= fmaxf(box[1], box[3]);
    }
    ReleaseSRWLockShared(&g_streamLock);
    *dist = bd;
    return best;
}

/* What lies under one point: the world ray's hits from 4 m over z down 12 m (as the rotation's own rays, filter 0),
 * and the drawn ground; a hit on the level terrain's body ([mgr+0x18], 0x61FBBA) or near a live stamp says so. */
static void LwTiltColumn(uintptr_t mgr, float x, float y, float z, char* buf, size_t size)
{
    uintptr_t phys = 0, fn = 0, settings = 0; uint32_t world = 0, coarseId = 0;
    SafeRead(mgr + 0x18, &coarseId, 4);
    uint8_t hits[8 * 0x2C]; uint32_t n = 0;
    const float origin[3] = {x, y, z + 4.0f}, down[3] = {0, 0, -1};
    if (EvpPtr(g_lw.game + LW_TILT_PHYS, &phys) && EvpPtr(phys + 0x68, &fn) && fn && EvpPtr(mgr + 8, &settings) && !SafeRead(settings + 0xD4, &world, 4))
        n = ((LwTiltRayFn)fn)(world, origin, down, 12.0f, 0, hits, 8);
    if (n > 8) n = 8;
    int tile = -1; float drawn = MoveTilesOn() ? MoveDrawnGround(x, y, &tile) : -9999.0f;
    int at = snprintf(buf, size, "drawn %.2f (tile %d), %u hits:", (double)drawn, tile, n);
    for (uint32_t k = 0; k < n && at > 0 && (size_t)at < size; k++)
    {
        const uint8_t* h = hits + 0x2C * k; float f[7]; uint32_t u[4];
        memcpy(f, h, sizeof f); memcpy(u, h + 0x1C, sizeof u);
        at += snprintf(buf + at, size - (size_t)at, " z %.2f n.z %.2f d %.2f id %08X/%08X/%08X", (double)f[2], (double)f[5], (double)f[6], u[0], u[1], u[3]);
        if (at <= 0 || (size_t)at >= size) break;
        if (u[1] == coarseId) at += snprintf(buf + at, size - (size_t)at, " (the level terrain)");
        else if (u[0] != 0)
        {
            float sd = 0; int in = 0, s = LwTiltStamp(f[0], f[1], &sd, &in);
            if (s >= 0) at += snprintf(buf + at, size - (size_t)at, " (stamp %d %s, %.1f m, %s its footprint)", s, g_streamDeferred[s] ? "streamed" : "level's", (double)sd, in ? "in" : "out of");
            else at += snprintf(buf + at, size - (size_t)at, " (no live stamp within 32 m)");
        }
    }
}

/* A ray's hit is a prop's when it names an owner (+0x1C; the ground's name none) and its body (+0x20) is not the level
 * terrain's. */
static int LwTiltIsProp(uint32_t owner, uint32_t body, uint32_t terrain) { return owner != 0 && body != terrain; }

#ifdef CO_RESEARCH
static void LwLpLevel(void); static void LwLpScan(void); static void LwLpNote(void); /* living_world_leancap.c: the lean limit's probe */
static int LwLeanWatch(const wchar_t* path);
static void LwLpUnitScan(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, const float* up, float tilt, float ground, float off, ULONGLONG now);
#endif
#if defined(CO_RESEARCH) || defined(CO_TEST)
/* Research: how often a four-corner unit stands on a prop, leaning or not, to compare levels. The prop rule poses a
 * unit along the ground under a prop, so each sample on a prop also says how far its corners lie under the surface
 * over them: the rule's side effect, which no check against the drawn ground can see. */
#define LW_TILT_SINKS 2048    /* worst corners kept for a line's median */
#define LW_TILT_SUNK_LINES 16 /* unit lines a level and a value of xl-lean.txt */
static struct { uint32_t samples, prop, propLean, bareLean, cornered, sink05, sink10, kept; float sink[LW_TILT_SINKS]; } g_lwTiltProp;
static int g_lwTiltSunkLines;

/* The closest hit of the rotation's own ray type from 4 m over p, 12 m down: 1 a prop, 0 not, -1 no hit; *z the
 * hit's height, ids its owner and body. */
static int LwTiltUnderAt(uintptr_t mgr, const float* p, float* z, uint32_t* ids)
{
    uintptr_t phys = 0, fn = 0, settings = 0; uint32_t type = 0, terrain = 0;
    uint8_t hits[8 * 0x2C];
    const float origin[3] = {p[0], p[1], p[2] + 4.0f}, down[3] = {0, 0, -1};
    if (!EvpPtr(g_lw.game + LW_TILT_PHYS, &phys) || !EvpPtr(phys + 0x68, &fn) || !fn || !EvpPtr(mgr + 8, &settings) ||
        SafeRead(settings + 0xD4, &type, 4) || SafeRead(mgr + 0x18, &terrain, 4)) return -1;
    if (((LwTiltRayFn)fn)(type, origin, down, 12.0f, 0, hits, 8) == 0) return -1;
    memcpy(ids, hits + 0x1C, 8); memcpy(z, hits + 8, 4);
    return LwTiltIsProp(ids[0], ids[1], terrain);
}
static int LwTiltOnProp(uintptr_t mgr, const float* p, float* z) { uint32_t ids[2]; return LwTiltUnderAt(mgr, p, z, ids); }

/* A unit on a prop: each corner's depth under the surface over it (the unit's plane at its corner offsets, R+0x60),
 * the deepest counted; a unit line for one over 1 m. */
static void LwTiltPropSink(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, float fromUpright)
{
    const float lx[3] = {1, 0, 0}, ly[3] = {0, 1, 0}; float ax[3], ay[3], under[4] = {NAN, NAN, NAN, NAN}, worst = -1e9f; uint32_t worstIds[2] = {0, 0};
    LwTiltRotate(q, lx, ax); LwTiltRotate(q, ly, ay);
    for (int k = 0; k < 4; k++)
    {
        float o[2], c[3], cz; uint32_t ids[2];
        if (SafeRead(R + 0x58 + 0x18u * k + 8, o, sizeof o)) return;
        for (int j = 0; j < 3; j++) c[j] = p[j] + ax[j] * o[0] + ay[j] * o[1];
        if (LwTiltUnderAt(mgr, c, &cz, ids) < 0) continue;
        under[k] = cz - c[2];
        if (under[k] > worst) { worst = under[k]; memcpy(worstIds, ids, sizeof ids); }
    }
    if (worst < -1e8f) return;
    g_lwTiltProp.cornered++;
    if (g_lwTiltProp.kept < LW_TILT_SINKS) g_lwTiltProp.sink[g_lwTiltProp.kept++] = worst;
    if (worst > 0.5f) g_lwTiltProp.sink05++;
    if (!(worst > 1.0f)) return;
    g_lwTiltProp.sink10++;
    if (g_lwTiltSunkLines >= LW_TILT_SUNK_LINES) return;
    g_lwTiltSunkLines++;
    Note("living world: tilt props: sunk: unit %08X at (%.1f, %.1f, %.2f): %.1f deg from upright; corners under the surface over them (m, nan: none): %+.2f %+.2f %+.2f %+.2f; "
        "the deepest under owner %08X, body %08X", id, (double)p[0], (double)p[1], (double)p[2], (double)fromUpright, (double)under[0], (double)under[1], (double)under[2],
        (double)under[3], worstIds[0], worstIds[1]);
}

static void LwTiltPropSample(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, float fromUpright)
{
    float z;
    const int on = LwTiltOnProp(mgr, p, &z);
    if (on < 0) return;
    g_lwTiltProp.samples++;
    if (!on) { if (fromUpright >= 30.0f) g_lwTiltProp.bareLean++; return; }
    g_lwTiltProp.prop++;
    if (fromUpright >= 30.0f) g_lwTiltProp.propLean++;
    LwTiltPropSink(mgr, R, id, q, p, fromUpright);
}

static int LwTiltFloatUp(const void* a, const void* b) { const float x = *(const float*)a, y = *(const float*)b; return (x > y) - (x < y); }

static void LwTiltPropNote(void)
{
    if (g_lwTiltProp.samples)
    {
        qsort(g_lwTiltProp.sink, g_lwTiltProp.kept, sizeof(float), LwTiltFloatUp);
        Note("living world: tilt props (30 s): %u four-corner samples with a hit under them, %u on a prop (%.2f %%); 30 deg or more from upright: %u of those on a prop, %u of the others; "
            "corners read for %u of those on a prop: %u with a corner over 0.5 m under the surface over it, %u over 1.0 m, the deepest corner's median %.2f m (of the first %u)",
            g_lwTiltProp.samples, g_lwTiltProp.prop, 100.0 * g_lwTiltProp.prop / g_lwTiltProp.samples, g_lwTiltProp.propLean, g_lwTiltProp.bareLean,
            g_lwTiltProp.cornered, g_lwTiltProp.sink05, g_lwTiltProp.sink10, g_lwTiltProp.kept ? (double)g_lwTiltProp.sink[g_lwTiltProp.kept / 2] : 0.0, g_lwTiltProp.kept);
    }
    memset(&g_lwTiltProp, 0, sizeof g_lwTiltProp);
}

/* Research: what the prop rule's check costs on the rotation's job threads, every hit it is asked about. */
static struct { volatile LONG64 ticks, worst; volatile LONG calls; LONG frames; } g_lwTiltCost;
static void LwTiltCost(LONG64 ticks)
{
    InterlockedExchangeAdd64(&g_lwTiltCost.ticks, ticks); InterlockedIncrement(&g_lwTiltCost.calls);
    for (LONG64 w = g_lwTiltCost.worst; ticks > w;)
    {
        const LONG64 was = InterlockedCompareExchange64(&g_lwTiltCost.worst, ticks, w);
        if (was == w) break;
        w = was;
    }
}
static void LwTiltCostNote(LONG64 perSecond)
{
    const LONG64 ticks = InterlockedExchange64(&g_lwTiltCost.ticks, 0), worst = InterlockedExchange64(&g_lwTiltCost.worst, 0);
    const LONG calls = InterlockedExchange(&g_lwTiltCost.calls, 0), frames = g_lwTiltCost.frames;
    g_lwTiltCost.frames = 0;
    if (calls == 0 || perSecond <= 0) return;
    const double ms = 1000.0 * (double)ticks / (double)perSecond;
    Note("living world: tilt rays (30 s): the prop rule's check took %.3f ms in %ld calls over %ld frames (%.4f ms a frame), %.1f us at worst",
        ms, (long)calls, (long)frames, frames > 0 ? ms / frames : 0.0, 1e6 * (double)worst / (double)perSecond);
}
#endif

/* ---- The fix: a ray that starts under a live tile's ground and misses reads that ground above it ---------------------
 * The reader 0x620810(mgr, i, rays, request, coarse request) returns R+0x48, the full length, whenever the ray's count
 * (request + 0x30, records of 0x40 B, start at +0x10) is 0 (0x620933): a hole. A heightfield is one-sided, so a corner
 * pushed under the ground misses it, reads a hole and tilts the unit further; it stays rolled. On a live tile such a
 * miss now reads minus its depth, the sign the game gives a hit under its level terrain. Hits on the ground stay the
 * game's (craters are not in the tile's CPU heights). Runs on the rotation's job threads: the tiles are read under g_tilesLock. */
#define LW_TILT_READ 0x620810u
typedef float (*LwTiltReadFn)(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse);
static LwTiltReadFn g_lwTiltRead;
static volatile LONG g_lwTiltUnder, g_lwTiltReads;
#ifdef CO_RESEARCH
static volatile LONG g_lwTiltInside;
#endif
static void LwLeanAt(const float* origin); /* living_world_leancap.c: where this thread's unit stands, for its limit */

/* A miss that started under the ground: minus its depth; anything else as the game read it. */
static float LwTiltUnder(float d, uint32_t count, float originZ, float ground) { return count == 0 && ground > originZ ? originZ - ground : d; }

/* The live tile's ground at (x, y); -9999 outside every live tile or when a writer holds the tiles. */
static float LwTiltTileGround(float x, float y)
{
    float z = -9999.0f;
    if (!TryAcquireSRWLockShared(&g_tilesLock)) return z;
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].ground.v != NULL &&
            fabsf(x - g_tiles[k].centre[0]) < NEAR_HIDE / 2 && fabsf(y - g_tiles[k].centre[1]) < NEAR_HIDE / 2)
        { z = NearGroundAt(&g_tiles[k].ground, x, y); break; }
    ReleaseSRWLockShared(&g_tilesLock);
    return z;
}

/* ---- On complex levels a four-corner ray that meets a prop reads the drawn ground ------------------------------------
 * The pose (0x6226F1) turns a unit until its four corner readings are alike, so a corner on a rock lifts that side.
 * The game's navmesh keeps units off faces over 60 deg and off what its terrain covers; an extra large level's terrain
 * is rasterised in 4 m cells (exe 0x89A744) and lies over low rocks, so units cross them and lay along their sides.
 * A prop's hit (the level terrain's body is [mgr+0x18]) then reads the ray's length to the drawn ground: the unit lies
 * along its ground, at the height the game gave it. Mode 4 only (R+0x54); where no drawn ground reads the hit stays
 * the game's. The pose stands a unit upright when its readings average the full length or under 0 (0x622CA0). */
static volatile LONG g_lwTiltPropsOn = 1, g_lwTiltPropGround, g_lwTiltPropKept;

/* The length from o along dir to the live tiles' ground, negative when o is under it. A corner ray runs along the
 * unit's own down turned up to 60 deg toward its motion (0x62015D), so the steps below grow where the ground is
 * steeper than the ray: 0 then, as for a ray over 60 deg from straight down or a point with no live tile. */
#define LW_TILT_SETTLED 0.05f
static int LwTiltGroundAlong(const float* o, const float* dir, float* t)
{
    const float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 1e-4f) || !(dir[2] / n < -0.5f)) return 0;
    float at = 0, step = 1.0f;
    for (int k = 0; k < 8 && step > 0.01f; k++)
    {
        const float g = LwTiltTileGround(o[0] + dir[0] / n * at, o[1] + dir[1] / n * at);
        if (!(g > -9000.0f)) return 0;
        const float next = (o[2] - g) / (-dir[2] / n);
        step = fabsf(next - at); at = next;
    }
    if (!(step <= LW_TILT_SETTLED)) return 0;
    *t = at;
    return 1;
}

/* A prop's hit read as the ground t away: at most the full length, the game's answer to a hole, and at least minus
 * it, so one corner deep under the ground cannot outweigh the other three (a corner weighs 1 - reading / length). */
static float LwTiltPropRead(float t, float len) { return t > len ? len : t < -len ? -len : t; }

/* Unit i's reading d of the ray record rec that started at o and hit. */
static float LwTiltOverProp(uintptr_t mgr, uint32_t i, uintptr_t rec, const float* o, float d)
{
    uintptr_t hit = 0, recs = 0; uint32_t ids[2], terrain = 0, mode = 0; float dir[3], len = 0, t;
    if (!g_lwTiltPropsOn || !LwLive() || !EvpPtr(rec + 8, &hit) || SafeRead(hit + 0x1C, ids, sizeof ids) || SafeRead(mgr + 0x18, &terrain, 4) ||
        !LwTiltIsProp(ids[0], ids[1], terrain) || !EvpPtr(mgr + 0x58, &recs) || SafeRead(recs + 0x298ull * i + 0x54, &mode, 4) || mode != 4 ||
        SafeRead(recs + 0x298ull * i + 0x48, &len, 4) || SafeRead(rec + 0x1C, dir, sizeof dir)) return d;
    if (!LwTiltGroundAlong(o, dir, &t)) { InterlockedIncrement(&g_lwTiltPropKept); return d; }
    InterlockedIncrement(&g_lwTiltPropGround);
    return LwTiltPropRead(t, len);
}

static float LwTiltRead(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse)
{
    float o[3]; uint32_t count = 1;
    const int have = request != 0 && !SafeRead(rays + 0x40ull * (request - 1) + 0x10, o, sizeof o);
    LwLeanAt(have ? o : NULL);
#ifdef CO_RESEARCH
    uintptr_t hit = 0; uint32_t flags = 0; /* before the game's call, which casts back from inside a body (0x620852) */
    if (have && EvpPtr(rays + 0x40ull * (request - 1) + 8, &hit) && !SafeRead(hit + 0x28, &flags, 4) && (flags & 1)) InterlockedIncrement(&g_lwTiltInside);
#endif
    float d = g_lwTiltRead(mgr, i, rays, request, coarse);
    if (!have || !MoveTilesOn() || SafeRead(rays + 0x40ull * (request - 1) + 0x30, &count, 4)) return d;
    if (count != 0)
    {
#if defined(CO_RESEARCH) || defined(CO_TEST)
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        const float read = LwTiltOverProp(mgr, i, rays + 0x40ull * (request - 1), o, d);
        QueryPerformanceCounter(&b);
        LwTiltCost(b.QuadPart - a.QuadPart);
        return read;
#else
        return LwTiltOverProp(mgr, i, rays + 0x40ull * (request - 1), o, d);
#endif
    }
    float ground = LwTiltTileGround(o[0], o[1]);
    if (!(ground > -9000.0f)) return d;
    InterlockedIncrement(&g_lwTiltReads);
    if (ground > o[2]) InterlockedIncrement(&g_lwTiltUnder);
    return LwTiltUnder(d, count, o[2], ground);
}

/* The reader's entry, its record arithmetic, the count it stores and tests, its full-length default, and its two
 * callers in the rotation update. */
static int g_lwTiltDone;
static void LwInstallTilt(void)
{
    if (g_lwTiltDone++) return;
    uintptr_t g = g_lw.game;
    if (!ApiCodeMatches(g + LW_TILT_READ, "40 53 55 56 57 48 83 EC 78 0F 29 74 24 60") || !ApiCodeMatches(g + 0x620835u, "41 8D 71 FF 48 C1 E6 06 49 8B D8") ||
        !ApiCodeMatches(g + 0x6208D4u, "89 44 1E 30") || !ApiCodeMatches(g + 0x6208ECu, "F3 0F 10 45 48") ||
        !ApiCodeMatches(g + 0x620933u, "83 7C 1E 30 00 76 05 F3 0F 10 47 18") ||
        !ApiCodeMatches(g + 0x6215DDu, "E8 2E F2 FF FF") || !ApiCodeMatches(g + 0x6227A8u, "E8 63 E0 FF FF"))
    { Note("living world: the rotation's ray reader is not the reviewed code; units may stay rolled on detailed tiles"); return; }
    if (LoaderHook("living world: rays under a detailed tile's ground", g + LW_TILT_READ, (void*)LwTiltRead, (void**)&g_lwTiltRead) || g_lwTiltRead == NULL)
    { g_lwTiltRead = NULL; Note("living world: the rotation's ray reader could not be hooked"); }
}

static int LwTiltSeen(uint32_t id, ULONGLONG now)
{
    int free = -1;
    for (int k = 0; k < LW_TILT_SEEN; k++)
    {
        if (g_lwTilt.seenId[k] == id) { if (now - g_lwTilt.seenAt[k] < 20000) return 1; g_lwTilt.seenAt[k] = now; return 0; }
        if (free < 0 && (g_lwTilt.seenId[k] == 0 || now - g_lwTilt.seenAt[k] > 20000)) free = k;
    }
    if (free < 0) return 1;
    g_lwTilt.seenId[free] = id; g_lwTilt.seenAt[free] = now;
    return 0;
}

static void LwTiltLine(uintptr_t mgr, uintptr_t R, uint32_t id, uint32_t mode, const float* q, const float* p, float tilt, float ground, float off)
{
    float f[4]; uint32_t hitId = 0;
    SafeRead(R + 0x48, f, 12); SafeRead(R + 0x28C, &hitId, 4);
    float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
    char col[512];
    LwTiltColumn(mgr, p[0], p[1], p[2], col, sizeof col);
    Note("living world: tilt: unit %08X mode %u at (%.1f, %.1f, %.2f): %.0f deg, its ground %.0f deg, %.0f deg off its ground; up (%.2f, %.2f, %.2f); R+0x48 %.2f, +0x4C %.2f, +0x50 %.2f, +0x28C %08X; under it: %s",
        id, mode, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)ground, (double)off, (double)up[0], (double)up[1], (double)up[2], (double)f[0], (double)f[1], (double)f[2], hitId, col);
    if (mode == 4)
        for (int k = 0; k < 4; k++)
        {
            uint8_t s[0x18]; float o[2], last; uint32_t req[2];
            if (SafeRead(R + 0x58 + 0x18u * k, s, sizeof s)) break;
            memcpy(req, s, 8); memcpy(o, s + 8, 8); memcpy(&last, s + 0x10, 4);
            const float lx[3] = {1, 0, 0}, ly[3] = {0, 1, 0}; float ax[3], ay[3];
            LwTiltRotate(q, lx, ax); LwTiltRotate(q, ly, ay);
            float cx = p[0] + ax[0] * o[0] + ay[0] * o[1], cy = p[1] + ax[1] * o[0] + ay[1] * o[1];
            LwTiltColumn(mgr, cx, cy, p[2], col, sizeof col);
            Note("living world: tilt: unit %08X corner %d offset (%.2f, %.2f) last %.2f (requests %u/%u, links %u %u) at (%.1f, %.1f): %s", id, k, (double)o[0], (double)o[1],
                (double)last, req[0], req[1], s[0x14], s[0x15], (double)cx, (double)cy, col);
        }
    else if (mode == 2 || mode == 3)
    {
        uint32_t count = EvpU32(R + 0xB8, 0);
        for (uint32_t k = 0; k < count && k < 6; k++)
        {
            uint32_t w[14];
            if (SafeRead(R + 0xBC + 0x38u * k, w, sizeof w)) break;
            float v[14]; memcpy(v, w, sizeof v);
            Note("living world: tilt: unit %08X slot %u: %08X %08X %08X | %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f", id, k, w[0], w[1], w[2],
                (double)v[3], (double)v[4], (double)v[5], (double)v[6], (double)v[7], (double)v[8], (double)v[9], (double)v[10], (double)v[11], (double)v[12], (double)v[13]);
        }
    }
}

/* Whether a four-corner unit holds a corner that read the full length (R+0x48): a miss, read as a hole. */
static int LwTiltMissed(uintptr_t R)
{
    float len = 0, last = 0;
    if (SafeRead(R + 0x48, &len, 4)) return 0;
    for (int k = 0; k < 4; k++)
        if (!SafeRead(R + 0x58 + 0x18u * k + 0x10, &last, 4) && fabsf(last - len) < 0.001f) return 1;
    return 0;
}

/* Whether a four-corner unit's corners lie over 3 m apart (chargers' size). */
static int LwTiltLong(uintptr_t R)
{
    float lo = 0, hi = 0;
    for (int k = 0; k < 4; k++)
    {
        float o[2];
        if (SafeRead(R + 0x58 + 0x18u * k + 8, o, sizeof o)) return 0;
        if (k == 0 || o[1] < lo) lo = o[1];
        if (k == 0 || o[1] > hi) hi = o[1];
    }
    return hi - lo >= 3.0f;
}

static void LwTiltScan(void)
{
    ULONGLONG now = GetTickCount64();
#if defined(CO_RESEARCH) || defined(CO_TEST)
    g_lwTiltCost.frames++;
#endif
    if (now - g_lwTilt.at < 500) return;
    g_lwTilt.at = now;
#ifdef CO_RESEARCH
    static wchar_t lean[MAX_PATH];
    if (!lean[0] && g_logs[0]) swprintf(lean, MAX_PATH, L"%ls\\xl-lean.txt", g_logs);
    if (lean[0]) LwLeanWatch(lean);
#endif
    if (g_lwTilt.level != g_world.levels)
    {
        memset(&g_lwTilt, 0, sizeof g_lwTilt); g_lwTilt.level = g_world.levels; g_lwTilt.at = now;
#ifdef CO_RESEARCH
        LwLpLevel(); g_lwTiltSunkLines = 0;
#endif
    }
    uintptr_t mgr = 0, inst = 0, recs = 0, ctl = 0, units = 0, urecs = 0;
    if (!EvpPtr(g_lw.game + LW_TILT_ROT, &mgr) || !EvpPtr(mgr + 0x50, &inst) || !EvpPtr(mgr + 0x58, &recs) || !EvpPtr(mgr + 0x60, &ctl) ||
        !EvpPtr(g_lw.game + LW_TILT_UNITS, &units) || !EvpPtr(units + 0x68, &urecs)) return;
    uint32_t n = EvpU32(mgr + 0x2C, 0);
    if (n > 1024) n = 1024;
    g_lwTilt.scans++;
#ifdef CO_RESEARCH
    LwLpScan();
#endif
    for (uint32_t i = 0; i < n; i++)
    {
        uintptr_t e = 0, R = recs + 0x298ull * i; uint8_t enabled = 0; uint32_t id = 0, mode = 0, ui = 0; float q[4], p[3], nrm[3];
        if (!EvpPtr(inst + 8ull * i, &e) || SafeRead(e + 8, &id, 4) || SafeRead(ctl + 0xCull * i + 8, &enabled, 1) || SafeRead(R + 0x54, &mode, 4)) continue;
        if (!enabled || mode > 7 || !EvpHash(units + 0x40, id, 8, 4, &ui) || ui > 0x100000 ||
            SafeRead(urecs + 0x308ull * ui + 0x2D0, q, sizeof q) || SafeRead(urecs + 0x308ull * ui + 0x2E0, p, sizeof p)) continue;
        g_lwTilt.units++; g_lwTilt.modes[mode]++;
        float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
#ifdef CO_RESEARCH
        if (mode == 4) LwTiltPropSample(mgr, R, id, q, p, LwTiltDeg(up[2]));
#endif
        if (!LwTiltGround(p[0], p[1], nrm)) continue;
        float tilt = LwTiltDeg(up[2]), ground = LwTiltDeg(nrm[2]), off = LwTiltAngle(up, nrm);
#ifdef CO_RESEARCH
        if (mode == 4) LwLpUnitScan(mgr, R, id, q, p, up, tilt, ground, off, now);
#endif
        int tile = -1;
        if (mode == 4 && LwTiltLong(R) && MoveTilesOn() && MoveDrawnGround(p[0], p[1], &tile) > -9000.0f && tile >= 0)
        {
            g_lwTilt.longs++;
            if (off > 15.0f) g_lwTilt.longOver++;
            if (off > g_lwTilt.longWorst) { g_lwTilt.longWorst = off; g_lwTilt.longId = id; }
        }
        if (off < LW_TILT_EXCESS) continue;
        g_lwTilt.tilted++;
        if (off >= 30.0f) { g_lwTilt.off30++; if (mode == 4 && LwTiltMissed(R)) g_lwTilt.off30Miss++; }
        if (off > g_lwTilt.worst) { g_lwTilt.worst = off; g_lwTilt.worstId = id; g_lwTilt.worstMode = mode; }
        if (g_lwTilt.lines < LW_TILT_LINES && !LwTiltSeen(id, now)) { g_lwTilt.lines++; LwTiltLine(mgr, R, id, mode, q, p, tilt, ground, off); }
    }
}

/* With the 30 s notes: the units scanned by mode and those off their ground; the counts start again. */
static void LwTiltNote(void)
{
    LONG reads = InterlockedExchange(&g_lwTiltReads, 0), under = InterlockedExchange(&g_lwTiltUnder, 0);
    if (reads) Note("living world: tilt rays (30 s): %ld missed on detailed tiles, %ld of them started under the ground and read it above them", (long)reads, (long)under);
    LONG propGround = InterlockedExchange(&g_lwTiltPropGround, 0), propKept = InterlockedExchange(&g_lwTiltPropKept, 0);
    if (propGround || propKept)
        Note("living world: tilt rays (30 s): %ld four-corner hits on a prop read the drawn ground, %ld stayed the game's with no drawn ground there", (long)propGround, (long)propKept);
#ifdef CO_RESEARCH
    LARGE_INTEGER perSecond;
    QueryPerformanceFrequency(&perSecond);
    LwTiltCostNote(perSecond.QuadPart);
    LwTiltPropNote(); LwLpNote();
    Note("living world: tilt rays (30 s): %ld corner readings started inside a body", (long)InterlockedExchange(&g_lwTiltInside, 0));
#endif
    if (g_lwTilt.scans == 0) return;
    Note("living world: tilt (30 s): %u scans, %u unit samples (by rotation mode 0-7: %u %u %u %u %u %u %u %u), %u %.0f deg or more off their ground, %u 30 deg or more (%u of them with a corner that missed), worst %.0f deg (unit %08X, mode %u)",
        g_lwTilt.scans, g_lwTilt.units, g_lwTilt.modes[0], g_lwTilt.modes[1], g_lwTilt.modes[2], g_lwTilt.modes[3], g_lwTilt.modes[4], g_lwTilt.modes[5], g_lwTilt.modes[6],
        g_lwTilt.modes[7], g_lwTilt.tilted, (double)LW_TILT_EXCESS, g_lwTilt.off30, g_lwTilt.off30Miss, (double)g_lwTilt.worst, g_lwTilt.worstId, g_lwTilt.worstMode);
    if (g_lwTilt.longs)
        Note("living world: tilt (30 s): units with corners over 3 m apart on detailed tiles: %u samples, %u over 15 deg off their ground, worst %.0f deg off (unit %08X)",
            g_lwTilt.longs, g_lwTilt.longOver, (double)g_lwTilt.longWorst, g_lwTilt.longId);
    g_lwTilt.longs = g_lwTilt.longOver = g_lwTilt.longId = 0; g_lwTilt.longWorst = 0;
    g_lwTilt.scans = g_lwTilt.units = g_lwTilt.tilted = g_lwTilt.off30 = g_lwTilt.off30Miss = 0; memset(g_lwTilt.modes, 0, sizeof g_lwTilt.modes); g_lwTilt.worst = 0; g_lwTilt.worstId = g_lwTilt.worstMode = 0;
}

/* Research: the scan and its notes on a normal mission too, to compare with (the living world's tick stops there). */
static void LwTiltBaseline(void)
{
#ifdef CO_RESEARCH
    static ULONGLONG noted;
    int host = 0;
    if (!ActMission(g_lw.game, &host, NULL) || !host) return;
    LwTiltScan();
    ULONGLONG now = GetTickCount64();
    if (now - noted >= 30000) { noted = now; LwTiltNote(); LwLeanNote(); }
#endif
}

#ifdef CO_TEST
static uint16_t g_lwTiltTestHeights[9];
/* A live 3 x 3 tile at (cx, cy), flat at z, with moving tiles on; z NAN: tiles off. */
void CoXlTestTiltTile(float cx, float cy, float z)
{
    memset(g_tiles, 0, sizeof g_tiles);
    g_moveInstalled = 0; g_move.on = 0;
    if (isnan(z)) return;
    uint16_t v = (uint16_t)lroundf((z + 500.0f) / 1000.0f * 65535.0f);
    for (int k = 0; k < 9; k++) g_lwTiltTestHeights[k] = v;
    g_moveInstalled = 1; g_move.on = 1; g_move.level = g_world.levels;
    g_tiles[0].live = 1; g_tiles[0].level = g_world.levels; g_tiles[0].centre[0] = cx; g_tiles[0].centre[1] = cy;
    g_tiles[0].ground = (NearGround){g_lwTiltTestHeights, 3, NEAR_SPAN, cx, cy}; /* the terrain reaches past its hidden square */
}
int CoXlTestTiltInstall(uintptr_t game) { g_lw.game = game; g_lwTiltDone = 0; g_lwTiltRead = NULL; LwInstallTilt(); return g_lwTiltRead != NULL; }
float CoXlTestTiltUnder(float d, uint32_t count, float originZ, float ground) { return LwTiltUnder(d, count, originZ, ground); }
void CoXlTestTiltHold(int on) { if (on) AcquireSRWLockExclusive(&g_tilesLock); else ReleaseSRWLockExclusive(&g_tilesLock); }
int CoXlTestTiltIsProp(uint32_t owner, uint32_t body, uint32_t terrain) { return LwTiltIsProp(owner, body, terrain); }
int CoXlTestTiltOnProp(uintptr_t game, uintptr_t mgr, const float* p, float* z) { g_lw.game = game; return LwTiltOnProp(mgr, p, z); }
void CoXlTestTiltPropSample(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, float fromUpright) { LwTiltPropSample(mgr, R, id, q, p, fromUpright); }
void CoXlTestTiltSunkLines(int n) { g_lwTiltSunkLines = n; }
int CoXlTestTiltSunkLinesNow(void) { return g_lwTiltSunkLines; }
void CoXlTestTiltCost(long long ticks) { LwTiltCost(ticks); }
void CoXlTestTiltCostNote(long long perSecond, long frames) { g_lwTiltCost.frames = frames; LwTiltCostNote(perSecond); }
long CoXlTestTiltCostCalls(void) { return g_lwTiltCost.calls; }
void CoXlTestTiltPropNote(void) { LwTiltPropNote(); }
int CoXlTestTiltPropsOn(void) { return g_lwTiltPropsOn != 0; }
void CoXlTestTiltNote(void) { LwTiltNote(); }
float CoXlTestTiltPropRead(float t, float len) { return LwTiltPropRead(t, len); }
int CoXlTestTiltGroundAlong(const float* o, const float* dir, float* t) { return LwTiltGroundAlong(o, dir, t); }
void CoXlTestTiltProps(int on) { InterlockedExchange(&g_lwTiltPropsOn, on); }
void CoXlTestTiltPropCounts(long* ground, long* kept) { *ground = InterlockedExchange(&g_lwTiltPropGround, 0); *kept = InterlockedExchange(&g_lwTiltPropKept, 0); }
void CoXlTestTiltCounts(long* reads, long* under) { *reads = InterlockedExchange(&g_lwTiltReads, 0); *under = InterlockedExchange(&g_lwTiltUnder, 0); }
#endif
