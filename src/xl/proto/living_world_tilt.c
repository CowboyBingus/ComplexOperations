/* Diagnostic, every build (issue G, terminids rolled on flat ground): what the rotation component's ground alignment
 * reads. Rotation 0x620B60 (mgr [game+0x33264A0]: +0x2C count, +0x38 hash, +0x50 instances, +0x58 records 0x298, +0x60
 * records 0xC with +8 enabled) sets each unit's rotation from R+0x54's mode: 2/3 bone rays, 4 four corner rays, 1 the
 * ground record's normal. A ray slot's distance turns negative when the level terrain [G+0x8758A0] alone, cast from
 * z 5000 m down (0x175CBD0 -> exe 0x799FF0), is above the slot's origin (0x620810). Twice a second; at most
 * LW_TILT_LINES unit lines a level, one per unit per 20 s, and a summary with the 30 s notes. */
#define LW_TILT_ROT 0x33264A0u
#define LW_TILT_UNITS 0x3326508u     /* unit component: hash +0x40, records +0x68 (0x308: +0x2D0 quaternion, +0x2E0 position) */
#define LW_TILT_PHYS 0x3326328u      /* physics API: +0x68 ray (world, origin, dir, length, filter, hits 0x2C each, max) */
#define LW_TILT_COARSE 0x13CA4F0u    /* the level terrain alone, from z + 500 m down 1000 m */
#define LW_TILT_LINES 48
#define LW_TILT_SEEN 64
#define LW_TILT_EXCESS 12.0f         /* degrees of tilt past the ground's under it that make a line */

static struct
{
    LONG level; ULONGLONG at; int lines;
    uint32_t scans, units, modes[8], tilted, worstId, worstMode; float worst;
    uint32_t longs, longOver, longId; float longWorst; /* corner rays over 3 m apart (chargers' size) on a detailed tile */
    uint32_t seenId[LW_TILT_SEEN]; ULONGLONG seenAt[LW_TILT_SEEN];
} g_lwTilt = {.level = -1};

typedef uint32_t (*LwTiltRayFn)(uint32_t world, const float* origin, const float* dir, float length, uint32_t filter, void* hits, uint32_t max);
typedef uint8_t (*LwTiltCoarseFn)(uint64_t unused, const float* p, float* z, uint8_t offset);

/* v rotated by quaternion q (x, y, z, w) */
static void LwTiltRotate(const float* q, const float* v, float* out)
{
    float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]), tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static float LwTiltDeg(float upZ) { return acosf(upZ > 1.0f ? 1.0f : upZ < -1.0f ? -1.0f : upZ) * 57.29578f; }

/* The ground's own tilt under xy: the drawn ground on a level with moving tiles (1 m either side), else the world
 * ground query's normal. NAN when neither reads. */
static float LwTiltGround(float x, float y)
{
    int tile;
    if (MoveTilesOn())
    {
        float e = MoveDrawnGround(x + 1, y, &tile), w = MoveDrawnGround(x - 1, y, &tile), n = MoveDrawnGround(x, y + 1, &tile), s = MoveDrawnGround(x, y - 1, &tile);
        if (e > -9000.0f && w > -9000.0f && n > -9000.0f && s > -9000.0f)
        {
            float gx = (e - w) / 2.0f, gy = (n - s) / 2.0f;
            return LwTiltDeg(1.0f / sqrtf(1.0f + gx * gx + gy * gy));
        }
    }
    typedef uint8_t (*GroundFn)(uintptr_t world, const float* in, float* outZ, uint8_t reject, float* normal, uint8_t mode);
    uintptr_t world; float in[3] = {x, y, 0}, z = NAN, nrm[3] = {0, 0, 0};
    if (!EvpPtr(g_lw.game + 0x346D508u, &world)) return NAN;
    ((GroundFn)(g_lw.game + 0x13CA380u))(world, in, &z, 0, nrm, 0);
    return isfinite(z) && nrm[2] > 0.0f ? LwTiltDeg(nrm[2]) : NAN;
}

/* What lies under one point: the world ray's hits from 4 m over z down 12 m (as the rotation's own rays, filter 0),
 * the drawn ground and the level terrain alone. Written into buf. */
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

static void LwTiltColumn(uintptr_t mgr, float x, float y, float z, char* buf, size_t size)
{
    uintptr_t phys = 0, fn = 0, settings = 0; uint32_t world = 0, coarseId = 0;
    SafeRead(mgr + 0x18, &coarseId, 4);
    uint8_t hits[8 * 0x2C]; uint32_t n = 0;
    const float origin[3] = {x, y, z + 4.0f}, down[3] = {0, 0, -1};
    if (EvpPtr(g_lw.game + LW_TILT_PHYS, &phys) && EvpPtr(phys + 0x68, &fn) && fn && EvpPtr(mgr + 8, &settings) && !SafeRead(settings + 0xD4, &world, 4))
        n = ((LwTiltRayFn)fn)(world, origin, down, 12.0f, 0, hits, 8);
    if (n > 8) n = 8;
    int tile = -1; float drawn = MoveTilesOn() ? MoveDrawnGround(x, y, &tile) : -9999.0f, coarse = NAN;
    float p[3] = {x, y, z};
    if (!((LwTiltCoarseFn)(g_lw.game + LW_TILT_COARSE))(0, p, &coarse, 0)) coarse = NAN;
    int at = snprintf(buf, size, "drawn %.2f (tile %d), level terrain %.2f, %u hits:", (double)drawn, tile, (double)coarse, n);
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

/* ---- The fix: a ray that starts under a live tile's ground reads that ground above it --------------------------------
 * The reader 0x620810(mgr, i, rays, request, coarse request) returns a ray slot's distance: R+0x48, the full length,
 * when the ray met nothing; a negative one when the level terrain alone (the coarse request) lies above the ray's start
 * (request + 0x10, 0x40 B each). A heightfield is one-sided, so a corner pushed under the ground misses it and reads a
 * hole, which tilts the unit further: the unit stays rolled (lines with "last 4.00"). Under a live tile the coarse is
 * lowered to -500 m (or raised by a crater), so the game's own check cannot see it; the tile's ground takes its place. */
#define LW_TILT_READ 0x620810u
typedef float (*LwTiltReadFn)(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse);
static LwTiltReadFn g_lwTiltRead;
static volatile LONG g_lwTiltUnder, g_lwTiltReads;

/* The game's rule for a ray that starts under its terrain, with `ground` as the terrain: minus the depth, else d. */
static float LwTiltUnder(float d, float originZ, float ground) { return ground > originZ ? originZ - ground : d; }

static float LwTiltRead(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse)
{
    float o[3]; int tile = -1;
    if (request == 0 || !MoveTilesOn() || SafeRead(rays + 0x40ull * (request - 1) + 0x10, o, sizeof o)) return g_lwTiltRead(mgr, i, rays, request, coarse);
    float ground = MoveDrawnGround(o[0], o[1], &tile);
    if (tile < 0 || !(ground > -9000.0f)) return g_lwTiltRead(mgr, i, rays, request, coarse);
    float d = g_lwTiltRead(mgr, i, rays, request, 0); /* the coarse there is not the ground */
    g_lwTiltReads++;
    if (ground > o[2]) g_lwTiltUnder++;
    return LwTiltUnder(d, o[2], ground);
}

/* The reader's entry, its request arithmetic, its coarse test (request 0 skips it; coarse z against the start's z at
 * +0x18) and its two callers in the rotation update. */
static int g_lwTiltDone;
static void LwInstallTilt(void)
{
    if (g_lwTiltDone++) return;
    uintptr_t g = g_lw.game;
    if (!ApiCodeMatches(g + LW_TILT_READ, "40 53 55 56 57 48 83 EC 78 0F 29 74 24 60") || !ApiCodeMatches(g + 0x620835u, "41 8D 71 FF 48 C1 E6 06 49 8B D8") ||
        !ApiCodeMatches(g + 0x6208F7u, "8B 84 24 C0 00 00 00 85 C0 74 31") ||
        !ApiCodeMatches(g + 0x620914u, "F3 0F 10 4C 1E 18 F3 0F 10 50 08 0F 2F D1 76 0F") ||
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

static void LwTiltLine(uintptr_t mgr, uintptr_t R, uint32_t id, uint32_t mode, const float* q, const float* p, float tilt, float ground)
{
    float f[4]; uint32_t hitId = 0;
    SafeRead(R + 0x48, f, 12); SafeRead(R + 0x28C, &hitId, 4);
    float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
    char col[512];
    LwTiltColumn(mgr, p[0], p[1], p[2], col, sizeof col);
    Note("living world: tilt: unit %08X mode %u at (%.1f, %.1f, %.2f): %.0f deg, its ground %.0f deg; up (%.2f, %.2f, %.2f); R+0x48 %.2f, +0x4C %.2f, +0x50 %.2f, +0x28C %08X; under it: %s",
        id, mode, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)ground, (double)up[0], (double)up[1], (double)up[2], (double)f[0], (double)f[1], (double)f[2], hitId, col);
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

/* A four-corner unit whose corners lie over 3 m apart, on a detailed tile: its tilt past the ground's, counted. */
static void LwTiltLong(uintptr_t R, uint32_t id, const float* p, float tilt)
{
    float lo = 0, hi = 0; int tile = -1;
    for (int k = 0; k < 4; k++)
    {
        float o[2];
        if (SafeRead(R + 0x58 + 0x18u * k + 8, o, sizeof o)) return;
        if (k == 0 || o[1] < lo) lo = o[1];
        if (k == 0 || o[1] > hi) hi = o[1];
    }
    if (hi - lo < 3.0f || !MoveTilesOn() || MoveDrawnGround(p[0], p[1], &tile) < -9000.0f || tile < 0) return;
    float ground = LwTiltGround(p[0], p[1]);
    if (!isfinite(ground)) return;
    g_lwTilt.longs++;
    if (tilt - ground > 15.0f) g_lwTilt.longOver++;
    if (tilt - ground > g_lwTilt.longWorst) { g_lwTilt.longWorst = tilt - ground; g_lwTilt.longId = id; }
}

static void LwTiltScan(void)
{
    ULONGLONG now = GetTickCount64();
    if (now - g_lwTilt.at < 500) return;
    g_lwTilt.at = now;
    if (g_lwTilt.level != g_world.levels) { memset(&g_lwTilt, 0, sizeof g_lwTilt); g_lwTilt.level = g_world.levels; g_lwTilt.at = now; }
    uintptr_t mgr = 0, inst = 0, recs = 0, ctl = 0, units = 0, urecs = 0;
    if (!EvpPtr(g_lw.game + LW_TILT_ROT, &mgr) || !EvpPtr(mgr + 0x50, &inst) || !EvpPtr(mgr + 0x58, &recs) || !EvpPtr(mgr + 0x60, &ctl) ||
        !EvpPtr(g_lw.game + LW_TILT_UNITS, &units) || !EvpPtr(units + 0x68, &urecs)) return;
    uint32_t n = EvpU32(mgr + 0x2C, 0);
    if (n > 1024) n = 1024;
    g_lwTilt.scans++;
    for (uint32_t i = 0; i < n; i++)
    {
        uintptr_t e = 0, R = recs + 0x298ull * i; uint8_t enabled = 0; uint32_t id = 0, mode = 0, ui = 0; float q[4], p[3];
        if (!EvpPtr(inst + 8ull * i, &e) || SafeRead(e + 8, &id, 4) || SafeRead(ctl + 0xCull * i + 8, &enabled, 1) || SafeRead(R + 0x54, &mode, 4)) continue;
        if (!enabled || mode > 7 || !EvpHash(units + 0x40, id, 8, 4, &ui) || ui > 0x100000 ||
            SafeRead(urecs + 0x308ull * ui + 0x2D0, q, sizeof q) || SafeRead(urecs + 0x308ull * ui + 0x2E0, p, sizeof p)) continue;
        g_lwTilt.units++; g_lwTilt.modes[mode]++;
        float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
        float tilt = LwTiltDeg(up[2]);
        if (mode == 4) LwTiltLong(R, id, p, tilt);
        if (tilt < LW_TILT_EXCESS) continue;
        float ground = LwTiltGround(p[0], p[1]);
        if (!isfinite(ground) || tilt < ground + LW_TILT_EXCESS) continue;
        g_lwTilt.tilted++;
        if (tilt > g_lwTilt.worst) { g_lwTilt.worst = tilt; g_lwTilt.worstId = id; g_lwTilt.worstMode = mode; }
        if (g_lwTilt.lines < LW_TILT_LINES && !LwTiltSeen(id, now)) { g_lwTilt.lines++; LwTiltLine(mgr, R, id, mode, q, p, tilt, ground); }
    }
}

/* With the 30 s notes: the units scanned by mode and those tilted past their ground; the counts start again. */
static void LwTiltNote(void)
{
    LONG reads = InterlockedExchange(&g_lwTiltReads, 0), under = InterlockedExchange(&g_lwTiltUnder, 0);
    if (reads) Note("living world: tilt rays (30 s): %ld read on detailed tiles, %ld started under the ground and read it above them", (long)reads, (long)under);
    if (g_lwTilt.scans == 0) return;
    Note("living world: tilt (30 s): %u scans, %u unit samples (by rotation mode 0-7: %u %u %u %u %u %u %u %u), %u tilted %.0f deg past their ground, worst %.0f deg (unit %08X, mode %u)",
        g_lwTilt.scans, g_lwTilt.units, g_lwTilt.modes[0], g_lwTilt.modes[1], g_lwTilt.modes[2], g_lwTilt.modes[3], g_lwTilt.modes[4], g_lwTilt.modes[5], g_lwTilt.modes[6],
        g_lwTilt.modes[7], g_lwTilt.tilted, (double)LW_TILT_EXCESS, (double)g_lwTilt.worst, g_lwTilt.worstId, g_lwTilt.worstMode);
    if (g_lwTilt.longs)
        Note("living world: tilt (30 s): units with corners over 3 m apart on detailed tiles: %u samples, %u over 15 deg past their ground, worst %.0f deg past it (unit %08X)",
            g_lwTilt.longs, g_lwTilt.longOver, (double)g_lwTilt.longWorst, g_lwTilt.longId);
    g_lwTilt.longs = g_lwTilt.longOver = g_lwTilt.longId = 0; g_lwTilt.longWorst = 0;
    g_lwTilt.scans = g_lwTilt.units = g_lwTilt.tilted = 0; memset(g_lwTilt.modes, 0, sizeof g_lwTilt.modes); g_lwTilt.worst = 0; g_lwTilt.worstId = g_lwTilt.worstMode = 0;
}

#ifdef CO_TEST
static uint16_t g_lwTiltTestHeights[9];
/* A live 3 x 3 tile at (cx, cy) over the whole hidden square, flat at z, with moving tiles on; z NAN: tiles off. */
void CoXlTestTiltTile(float cx, float cy, float z)
{
    memset(g_tiles, 0, sizeof g_tiles);
    g_moveInstalled = 0; g_move.on = 0;
    if (isnan(z)) return;
    uint16_t v = (uint16_t)lroundf((z + 500.0f) / 1000.0f * 65535.0f);
    for (int k = 0; k < 9; k++) g_lwTiltTestHeights[k] = v;
    g_moveInstalled = 1; g_move.on = 1; g_move.level = g_world.levels;
    g_tiles[0].live = 1; g_tiles[0].level = g_world.levels; g_tiles[0].centre[0] = cx; g_tiles[0].centre[1] = cy;
    g_tiles[0].ground = (NearGround){g_lwTiltTestHeights, 3, NEAR_HIDE, cx, cy};
}
int CoXlTestTiltInstall(uintptr_t game) { g_lw.game = game; g_lwTiltDone = 0; g_lwTiltRead = NULL; LwInstallTilt(); return g_lwTiltRead != NULL; }
float CoXlTestTiltUnder(float d, float originZ, float ground) { return LwTiltUnder(d, originZ, ground); }
void CoXlTestTiltCounts(long* reads, long* under) { *reads = InterlockedExchange(&g_lwTiltReads, 0); *under = InterlockedExchange(&g_lwTiltUnder, 0); }
#endif
