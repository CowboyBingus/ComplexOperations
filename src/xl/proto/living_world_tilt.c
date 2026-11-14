/* Diagnostic, every build (issue G, terminids rolled on flat ground): what the rotation component's ground alignment
 * reads. Rotation 0x620B60 (mgr [game+0x33264A0]: +0x2C count, +0x38 hash, +0x50 instances, +0x58 records 0x298, +0x60
 * records 0xC with +8 enabled) sets each unit's rotation from R+0x54's mode: 2/3 bone rays, 4 four corner rays, 1 the
 * ground record's normal. A unit is off its ground by the angle between its up and the drawn ground's normal. Twice a
 * second; unit lines only 30 deg or more off (one per unit per 20 s, a few a window, LW_TILT_LINES a level), and a
 * summary with the 30 s notes. A unit on a rock is off the drawn ground and still lies as the game put it, so the
 * four-corner units 30 deg off are also sorted by what their own corner rays read (LwTiltClass). */
#define LW_TILT_ROT 0x33264A0u
#define LW_TILT_UNITS 0x3326508u     /* unit component: hash +0x40, records +0x68 (0x308: +0x2D0 quaternion, +0x2E0 position) */
#define LW_TILT_PHYS 0x3326328u      /* physics API: +0x68 ray (world, origin, dir, length, filter, hits 0x2C each, max) */
#define LW_TILT_NAV 0x3326330u       /* nav API: +0x10 closest face (nav id, p in and out, radius, out, normal, agent hash) */
#define LW_TILT_UNITREC 0x27C5B40u   /* exe: unit physics records, 0x18 each: +0 world << 30 | unit, +4 counts (bit 30 live) */
#define LW_TILT_LINES 96
#define LW_TILT_WINDOW_LINES 4       /* unit lines a 30 s window, so the lines cover the whole mission */
#define LW_TILT_SEEN 64
#define LW_TILT_EXCESS 12.0f         /* degrees off the ground's normal that count as tilted */
#define LW_TILT_LINE_OFF 30.0f       /* degrees off that make a unit line */
#define LW_TILT_CHECKS 8             /* units 30 deg or more off their ground or from upright checked for a prop, a scan */
#define LW_TILT_FIT 20.0f            /* degrees from its corner rays' plane under which a unit lies as they read */
#define LW_TILT_TRACK 1024           /* units followed a level: where each was first seen, its corners' last readings */
#define LW_TILT_STAYED 2.0f          /* metres from where it was first seen under which a unit on a prop was put there */
/* What a miss that started under the ground reads: under 0, so a unit with every corner buried still stands world up
 * as the game has it, and so little that buried corners never outweigh the others' readings. */
#define LW_TILT_SKIN 0.01f
enum { LW_TILT_FITS, LW_TILT_MISSED, LW_TILT_UNDER, LW_TILT_LOOSE, LW_TILT_GROUND, LW_TILT_CLASSES };
typedef struct { uint32_t id; float first[3]; uint8_t state[4]; uint16_t flips; } LwTiltUnit;

static struct
{
    LONG level; ULONGLONG at; int lines;
    uint32_t scans, units, modes[8], tilted, off30, off30Miss, worstId, worstMode; float worst;
    uint32_t longs, longOver, longId; float longWorst; /* corner rays over 3 m apart (chargers' size) on a detailed tile */
    int windowLines;
    /* units 30 deg or more off: on a prop (a body with a unit), navmesh under it or on its top, the prop's modifiers */
    uint32_t checked, onProp, navUnder, navTop, propStatic, propDynamic, propNone, propNoRecord;
    /* four-corner units 30 deg or more off, by what their own corner rays read; the fitting ones found on a prop */
    uint32_t four, cls[LW_TILT_CLASSES], fitsOnProp; int windowFits;
    /* the props: the nav objects over the prop's point (by the first of carver, geometry, painter), a face at its column
     * for any agent and for the unit's own type, and the stamp whose footprint holds it */
    uint32_t propCarver, propGeometry, propPainter, propBare, propUnread, faceAny, agentAsked, agentFace, inStreamed, inLevel, noStamp;
    /* four-corner samples 30 deg or more from upright (the same measure on any level): a window's, then the level's */
    uint32_t up30, upChecked, upOnProp, lvFour, lvOff30, lvUp30, lvUpChecked, lvUpOnProp;
    /* the props' carvers against the face in the unit's column: the face over them, under them, met by them, no face;
     * the same column asked for the unit's own agent type; units still where they were first seen, and those that moved */
    uint32_t carverOver, carverUnder, carverMeets, carverNoFace, agentColumn, propStayed, propWalked;
    /* corners that went from a start under a surface to a miss or back between two scans */
    uint32_t flips, flipSamples, flipMax, flipId;
    uint32_t seenId[LW_TILT_SEEN]; ULONGLONG seenAt[LW_TILT_SEEN];
    LwTiltUnit track[LW_TILT_TRACK];
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

/* The physics record of `unit` (exe 0x7871F0 / 0x7E18C0 pack it): its counts word, 0 when the slot holds another unit.
 * Bits 15-18 hold twice the static navmesh modifiers (0x7870B0 returns r14 * 2), bits 19-22 the dynamic ones. */
static uint32_t LwTiltRecord(uint32_t unit)
{
    uintptr_t table = 0; uint32_t r[2];
    if (unit == 0 || g_navExe == 0 || !EvpPtr(g_navExe + LW_TILT_UNITREC, &table) || table == 0 ||
        SafeRead(table + 0x18ull * (unit & 0x3FFFFFu), r, sizeof r) || !(r[1] & 0x40000000u) || (r[0] & 0x3FFFFFFFu) != unit) return 0;
    return r[1];
}

/* The navmesh face nearest (x, y, z) within 10 m, as LwPlace asks it (no agent filter): 1 with the face point. */
static int LwTiltFace(float x, float y, float z, float* f) { return LwTnFace(x, y, z, 0, f); }

/* The world ray's closest hit from 4 m over z down 12 m (filter 0, as the rotation's rays): 1 with its hit record. */
static uint32_t LwTiltCast(uintptr_t mgr, float x, float y, float z, uint8_t* hits, uint32_t max)
{
    uintptr_t phys = 0, fn = 0, settings = 0; uint32_t world = 0, n = 0;
    const float origin[3] = {x, y, z + 4.0f}, down[3] = {0, 0, -1};
    if (EvpPtr(g_lw.game + LW_TILT_PHYS, &phys) && EvpPtr(phys + 0x68, &fn) && fn && EvpPtr(mgr + 8, &settings) && !SafeRead(settings + 0xD4, &world, 4))
        n = ((LwTiltRayFn)fn)(world, origin, down, 12.0f, 0, hits, max);
    return n > max ? max : n;
}

/* What lies under one point: the world ray's hits from 4 m over z down 12 m (as the rotation's own rays, filter 0),
 * and the drawn ground; a hit on the level terrain's body ([mgr+0x18], 0x61FBBA) or near a live stamp says so. */
static void LwTiltColumn(uintptr_t mgr, float x, float y, float z, char* buf, size_t size)
{
    uint32_t coarseId = 0;
    SafeRead(mgr + 0x18, &coarseId, 4);
    uint8_t hits[8 * 0x2C];
    uint32_t n = LwTiltCast(mgr, x, y, z, hits, 8);
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
            if (s >= 0) at += snprintf(buf + at, size - (size_t)at, " (stamp %d %s, resource %016llX, %.1f m, %s its footprint)", s, g_streamDeferred[s] ? "streamed" : "level's",
                (unsigned long long)g_streamResource[s], (double)sd, in ? "in" : "out of");
            else at += snprintf(buf + at, size - (size_t)at, " (no live stamp within 32 m)");
            if (at <= 0 || (size_t)at >= size) break;
            uint32_t rec = LwTiltRecord(u[0]); float fg[3], ft[3];
            int g = drawn > -9000.0f && LwTiltFace(x, y, drawn, fg), t = LwTiltFace(x, y, f[2], ft);
            at += snprintf(buf + at, size - (size_t)at, " [record %08X: %u static, %u dynamic modifiers; face from the ground %s%.2f m across, z %.2f; from its top %s%.2f m across, z %.2f]",
                rec, ((rec >> 15) & 0xFu) / 2u, (rec >> 19) & 0xFu, g ? "" : "none ", g ? (double)hypotf(fg[0] - x, fg[1] - y) : 0.0, g ? (double)fg[2] : 0.0,
                t ? "" : "none ", t ? (double)hypotf(ft[0] - x, ft[1] - y) : 0.0, t ? (double)ft[2] : 0.0);
        }
    }
}

/* How far a four-corner unit lies from what its corner rays read: the slope, in degrees, of the least-squares plane
 * through each corner's offset (x, y) and the distance its ray read. The rays start in the unit's own frame, so a unit
 * settled on what is under it reads about the same distance at every corner, on a rock as on the ground. -1 when the
 * corners span no area. */
static float LwTiltResidual(float c[4][3])
{
    float mx = 0, my = 0, md = 0, xx = 0, yy = 0, xy = 0, xd = 0, yd = 0;
    for (int k = 0; k < 4; k++) { mx += c[k][0] / 4; my += c[k][1] / 4; md += c[k][2] / 4; }
    for (int k = 0; k < 4; k++)
    {
        float x = c[k][0] - mx, y = c[k][1] - my, d = c[k][2] - md;
        xx += x * x; yy += y * y; xy += x * y; xd += x * d; yd += y * d;
    }
    float det = xx * yy - xy * xy;
    if (!(det > 1e-6f)) return -1.0f;
    return atanf(hypotf((xd * yy - yd * xy) / det, (yd * xx - xd * xy) / det)) * 57.29578f;
}

/* A four-corner unit 30 deg or more off the drawn ground. A corner that reads the ground at its start (LwTiltUnder's
 * value) comes first: that reading is this mod's, and the plane through it says nothing of how the unit lies. Then it
 * fits its rays' plane (it stands as the game put it, on whatever is under it), or it does not and a corner missed
 * (read the full length, a hole), or a corner read a negative distance (its ray started inside a body), or neither. */
static int LwTiltClass(float c[4][3], float len, float* residual)
{
    *residual = LwTiltResidual(c);
    for (int k = 0; k < 4; k++) if (fabsf(c[k][2] + LW_TILT_SKIN) < 0.0005f) return LW_TILT_GROUND;
    if (*residual >= 0.0f && *residual < LW_TILT_FIT) return LW_TILT_FITS;
    for (int k = 0; k < 4; k++) if (fabsf(c[k][2] - len) < 0.001f) return LW_TILT_MISSED;
    for (int k = 0; k < 4; k++) if (c[k][2] < 0.0f) return LW_TILT_UNDER;
    return LW_TILT_LOOSE;
}

/* A corner's reading: 1 under 0 (pushes the corner up), 2 the full length (a miss, lets it down), 0 a hit under it. */
static int LwTiltCornerState(float d, float len) { return d < 0.0f ? 1 : fabsf(d - len) < 0.001f ? 2 : 0; }

/* The corners that went from under 0 to a miss or back since `prev`, which then holds the new readings. A corner that
 * hovers at the ground line inside a rock does this: under the ground it reads the ground, over it the game's hole. */
static uint32_t LwTiltFlips(uint8_t* prev, float c[4][3], float len)
{
    uint32_t n = 0;
    for (int k = 0; k < 4; k++)
    {
        uint8_t s = (uint8_t)LwTiltCornerState(c[k][2], len);
        n += prev[k] != 0 && s != 0 && prev[k] != s;
        prev[k] = s;
    }
    return n;
}

/* The unit's entry among those followed this level, made at p when it is new; NULL when its places are taken. */
static LwTiltUnit* LwTiltTrack(LwTiltUnit* table, uint32_t id, const float* p)
{
    for (uint32_t k = 0; k < 16; k++)
    {
        LwTiltUnit* u = &table[(id * 2654435761u + k) % LW_TILT_TRACK];
        if (u->id == id) return u;
        if (u->id == 0) { u->id = id; memcpy(u->first, p, 12); return u; }
    }
    return NULL;
}

/* A four-corner unit's corners (R+0x58, 0x18 each: offset at +8, the last distance read at +0x10) and its ray length
 * (R+0x48): 0 when they read. */
static int LwTiltCorners(uintptr_t R, float c[4][3], float* len)
{
    if (SafeRead(R + 0x48, len, 4)) return 1;
    for (int k = 0; k < 4; k++)
        if (SafeRead(R + 0x58 + 0x18u * k + 8, c[k], 12)) return 1;
    return 0;
}

/* ---- The fix: a ray that starts under a live tile's ground and misses reads that ground at its start --------------
 * The reader 0x620810(mgr, i, rays, request, coarse request) returns R+0x48, the full length, whenever the ray's count
 * (request + 0x30, records of 0x40 B, start at +0x10) is 0 (0x620933): a hole. A heightfield is one-sided, so a corner
 * pushed under the ground misses it, reads a hole and tilts the unit further; it stays rolled. On a live tile such a
 * miss reads just under 0: the ground is at the ray's start. Its depth is not returned: the four-corner mode puts a
 * unit world up when its corners' mean 1 - h / length is over 1 (0x622CBB), and the depths of two rear corners of a
 * long unit standing level on a slope are enough for that, which keeps it level. Hits stay the game's (craters are not
 * in the tile's CPU heights). Runs on the rotation's job threads: the tiles are read under g_tilesLock. */
#define LW_TILT_READ 0x620810u
typedef float (*LwTiltReadFn)(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse);
static LwTiltReadFn g_lwTiltRead;
static volatile LONG g_lwTiltUnder, g_lwTiltReads;

/* A miss that started under the ground: the ground at its start; anything else as the game read it. */
static float LwTiltUnder(float d, uint32_t count, float originZ, float ground) { return count == 0 && ground > originZ ? -LW_TILT_SKIN : d; }

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

static float LwTiltRead(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse)
{
    float d = g_lwTiltRead(mgr, i, rays, request, coarse), o[3]; uint32_t count = 1;
    if (request == 0 || !MoveTilesOn() || SafeRead(rays + 0x40ull * (request - 1) + 0x10, o, sizeof o) ||
        SafeRead(rays + 0x40ull * (request - 1) + 0x30, &count, 4) || count != 0) return d;
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

static void LwTiltPropLine(uintptr_t mgr, uint32_t id, const float* p);

static void LwTiltLine(uintptr_t mgr, uintptr_t R, uint32_t id, uint32_t mode, const float* q, const float* p, float tilt, float ground, float off, int cls, float residual)
{
    static const char* const kClass[LW_TILT_CLASSES] = {"it lies as they read", "a corner missed", "a corner started under a surface", "no miss, no start under a surface",
        "a corner read the ground at its start"};
    float f[4]; uint32_t hitId = 0;
    SafeRead(R + 0x48, f, 12); SafeRead(R + 0x28C, &hitId, 4);
    float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
    char col[512];
    LwTiltColumn(mgr, p[0], p[1], p[2], col, sizeof col);
    Note("living world: tilt: unit %08X mode %u at (%.1f, %.1f, %.2f): %.0f deg, its ground %.0f deg, %.0f deg off its ground; up (%.2f, %.2f, %.2f); R+0x48 %.2f, +0x4C %.2f, +0x50 %.2f, +0x28C %08X; under it: %s",
        id, mode, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)ground, (double)off, (double)up[0], (double)up[1], (double)up[2], (double)f[0], (double)f[1], (double)f[2], hitId, col);
    if (cls >= 0)
        Note("living world: tilt: unit %08X is %.0f deg from the plane its corner rays read: %s", id, (double)residual, kClass[cls]);
    LwTiltPropLine(mgr, id, p);
    if (mode == 4)
        for (int k = 0; k < 4; k++)
        {
            uint8_t s[0x18]; float o[2], last; uint32_t req[2];
            if (SafeRead(R + 0x58 + 0x18u * k, s, sizeof s)) break;
            memcpy(req, s, 8); memcpy(o, s + 8, 8); memcpy(&last, s + 0x10, 4);
            const float lx[3] = {1, 0, 0}, ly[3] = {0, 1, 0}; float ax[3], ay[3];
            LwTiltRotate(q, lx, ax); LwTiltRotate(q, ly, ay);
            float cx = p[0] + ax[0] * o[0] + ay[0] * o[1], cy = p[1] + ax[1] * o[0] + ay[1] * o[1];
            float cz = p[2] + ax[2] * o[0] + ay[2] * o[1] + up[2] * 0.5f; /* the ray's start: the corner, 0.5 m up the unit's up */
            LwTiltColumn(mgr, cx, cy, p[2], col, sizeof col);
            Note("living world: tilt: unit %08X corner %d offset (%.2f, %.2f) last %.2f (requests %u/%u, links %u %u) at (%.1f, %.1f), ray start z %.2f: %s", id, k, (double)o[0],
                (double)o[1], (double)last, req[0], req[1], s[0x14], s[0x15], (double)cx, (double)cy, (double)cz, col);
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

/* A unit 30 deg or more off: whether it stands on a prop (the closest hit under it has a unit, at its feet), whether the
 * navmesh runs under that prop (a face within 0.5 m across from the drawn ground) or on its top, and the prop's
 * navmesh modifiers. A carved prop leaves no face under it; the face is asked for no agent, so a painted one does.
 * 1 when the unit stands on a prop. */
static int LwTiltCheck(uintptr_t mgr, uint32_t id, const float* p, const float* first)
{
    uint8_t h[0x2C]; float f[7], fg[3], ft[3]; uint32_t u[4], navId; int tile;
    g_lwTilt.checked++;
    if (LwTiltCast(mgr, p[0], p[1], p[2], h, 1) == 0) return 0;
    memcpy(f, h, sizeof f); memcpy(u, h + 0x1C, sizeof u);
    if (u[0] == 0 || fabsf(f[2] - p[2]) > 0.3f) return 0;
    g_lwTilt.onProp++;
    float drawn = MoveTilesOn() ? MoveDrawnGround(p[0], p[1], &tile) : -9999.0f;
    if (drawn > -9000.0f && LwTiltFace(p[0], p[1], drawn, fg) && hypotf(fg[0] - p[0], fg[1] - p[1]) < 0.5f) g_lwTilt.navUnder++;
    int any = LwTiltFace(p[0], p[1], f[2], ft) && hypotf(ft[0] - p[0], ft[1] - p[1]) < 0.5f;
    if (any && fabsf(ft[2] - f[2]) < 0.5f) g_lwTilt.navTop++;
    g_lwTilt.faceAny += (uint32_t)any;
    uint32_t rec = LwTiltRecord(u[0]);
    if (rec == 0) g_lwTilt.propNoRecord++;
    else
    {
        if ((rec >> 15) & 0xFu) g_lwTilt.propStatic++;
        if ((rec >> 19) & 0xFu) g_lwTilt.propDynamic++;
        if (((rec >> 15) & 0xFFu) == 0) g_lwTilt.propNone++;
    }
    LwTnAt at; LwTnObjects(LwTnNav(&navId), f, &at);
    if (!at.read) g_lwTilt.propUnread++;
    else if (at.n[LW_TN_CARVER]) g_lwTilt.propCarver++;
    else if (at.n[LW_TN_GEOMETRY]) g_lwTilt.propGeometry++;
    else if (at.n[LW_TN_PAINTER]) g_lwTilt.propPainter++;
    else g_lwTilt.propBare++;
    uint32_t agent = LwTnAgent(id); float fz;
    if (at.n[LW_TN_CARVER])
    {
        if (!LwTnColumnFace(p[0], p[1], f[2], 0, &fz)) g_lwTilt.carverNoFace++;
        else
        {
            int miss = LwTnMiss(fz, at.carverLo, at.carverHi, g_lwTn.height);
            if (miss > 0) g_lwTilt.carverOver++; else if (miss < 0) g_lwTilt.carverUnder++; else g_lwTilt.carverMeets++;
        }
    }
    if (LwTnAgentIndex(g_navExe, agent) > 0) /* place 0 and no type ask with no filter: nothing new to read */
    {
        g_lwTilt.agentAsked++;
        if (LwTnFace(p[0], p[1], f[2], agent, ft) && hypotf(ft[0] - p[0], ft[1] - p[1]) < 0.5f) g_lwTilt.agentFace++;
        g_lwTilt.agentColumn += (uint32_t)LwTnColumnFace(p[0], p[1], f[2], agent, &fz);
    }
    if (first != NULL)
    {
        if (sqrtf((p[0] - first[0]) * (p[0] - first[0]) + (p[1] - first[1]) * (p[1] - first[1]) + (p[2] - first[2]) * (p[2] - first[2])) < LW_TILT_STAYED) g_lwTilt.propStayed++;
        else g_lwTilt.propWalked++;
    }
    float sd = 0; int in = 0, s = LwTiltStamp(f[0], f[1], &sd, &in);
    if (s < 0 || !in) g_lwTilt.noStamp++;
    else if (g_streamDeferred[s]) g_lwTilt.inStreamed++;
    else g_lwTilt.inLevel++;
    return 1;
}

/* A unit line's prop: the nav objects over the point the unit stands on, their carvers' heights against the face in
 * that column, with and without the unit's own agent type, and how far the unit is from where it was first seen. */
static void LwTiltPropLine(uintptr_t mgr, uint32_t id, const float* p)
{
    uint8_t h[0x2C]; float f[7], any = 0, own = 0; uint32_t u[4], navId;
    if (LwTiltCast(mgr, p[0], p[1], p[2], h, 1) == 0) return;
    memcpy(f, h, sizeof f); memcpy(u, h + 0x1C, sizeof u);
    if (u[0] == 0) return;
    LwTnAt at; LwTnObjects(LwTnNav(&navId), f, &at);
    uint32_t agent = LwTnAgent(id); int place = LwTnAgentIndex(g_navExe, agent);
    int a = LwTnColumnFace(p[0], p[1], f[2], 0, &any), o = place > 0 && LwTnColumnFace(p[0], p[1], f[2], agent, &own);
    int miss = a && at.n[LW_TN_CARVER] ? LwTnMiss(any, at.carverLo, at.carverHi, g_lwTn.height) : 0;
    const LwTiltUnit* t = LwTiltTrack(g_lwTilt.track, id, p);
    float moved = t ? sqrtf((p[0] - t->first[0]) * (p[0] - t->first[0]) + (p[1] - t->first[1]) * (p[1] - t->first[1]) + (p[2] - t->first[2]) * (p[2] - t->first[2])) : -1.0f;
    Note("living world: tilt: unit %08X on prop %08X (surface z %.2f), %.1f m from where it was first seen, %u corner flips: nav objects over that point%s: %u carvers, %u geometry, "
        "%u painters, %u inert, %u with a painter set; the first: flags %X, material %08X, resource %016llX, z %.2f to %.2f; the carvers span z %.2f to %.2f; agent type %08X (place %d); "
        "the face in its column for any agent: %sz %.2f%s; for its own type: %sz %.2f",
        id, u[0], (double)f[2], (double)moved, t ? t->flips : 0u, at.read ? "" : " (not read)", at.n[LW_TN_CARVER], at.n[LW_TN_GEOMETRY], at.n[LW_TN_PAINTER], at.n[LW_TN_INERT],
        at.painters, at.flags, at.material, (unsigned long long)at.owner, (double)at.lo, (double)at.hi, (double)at.carverLo, (double)at.carverHi, agent, place,
        a ? "" : "none, ", (double)any, !a || !at.n[LW_TN_CARVER] ? "" : miss > 0 ? " (over the carvers: not cut)" : miss < 0 ? " (under the carvers' reach: not cut)" : " (the carvers reach it)",
        o ? "" : place > 0 ? "none, " : "not asked, ", (double)own);
}

/* Live carvers in the nav grid (nav + 0x6710), walking its free list from the head as the insert pops it (co_nav.c's
 * count): -1 when the list does not end within the slots. */
static int64_t LwTiltCarvers(uintptr_t nav, uint32_t* slots)
{
    const uintptr_t grid = nav + 0x6710u;
    uintptr_t pool = 0; uint32_t n = 0, spare = 0; uint16_t stride = 0; int32_t at = -1;
    if (SafeRead(grid + 0x1C, &n, 4) || SafeRead(grid + 0x24, &stride, 2) || SafeRead(grid + 0x2C, &at, 4) || SafeRead(grid + 0x30, &pool, 8) || pool == 0 || n == 0) return -1;
    *slots = n;
    for (; at != -1; spare++)
        if (at <= 0 || (uint32_t)at >= n || spare >= n - 1u || SafeRead(pool + (uintptr_t)stride * (uint32_t)at, &at, 4)) return -1;
    return (int64_t)n - 1 - spare;
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
    if (now - g_lwTilt.at < 500) return;
    g_lwTilt.at = now;
    if (g_lwTilt.level != g_world.levels) { memset(&g_lwTilt, 0, sizeof g_lwTilt); g_lwTilt.level = g_world.levels; g_lwTilt.at = now; }
    uintptr_t mgr = 0, inst = 0, recs = 0, ctl = 0, units = 0, urecs = 0;
    if (!EvpPtr(g_lw.game + LW_TILT_ROT, &mgr) || !EvpPtr(mgr + 0x50, &inst) || !EvpPtr(mgr + 0x58, &recs) || !EvpPtr(mgr + 0x60, &ctl) ||
        !EvpPtr(g_lw.game + LW_TILT_UNITS, &units) || !EvpPtr(units + 0x68, &urecs)) return;
    uint32_t n = EvpU32(mgr + 0x2C, 0);
    if (n > 1024) n = 1024;
    g_lwTilt.scans++;
    int checks = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uintptr_t e = 0, R = recs + 0x298ull * i; uint8_t enabled = 0; uint32_t id = 0, mode = 0, ui = 0; float q[4], p[3], nrm[3];
        if (!EvpPtr(inst + 8ull * i, &e) || SafeRead(e + 8, &id, 4) || SafeRead(ctl + 0xCull * i + 8, &enabled, 1) || SafeRead(R + 0x54, &mode, 4)) continue;
        if (!enabled || mode > 7 || !EvpHash(units + 0x40, id, 8, 4, &ui) || ui > 0x100000 ||
            SafeRead(urecs + 0x308ull * ui + 0x2D0, q, sizeof q) || SafeRead(urecs + 0x308ull * ui + 0x2E0, p, sizeof p)) continue;
        g_lwTilt.units++; g_lwTilt.modes[mode]++;
        LwTiltUnit* track = LwTiltTrack(g_lwTilt.track, id, p);
        const float* first = track ? track->first : NULL;
        float c[4][3], len; int corners = mode == 4 && !LwTiltCorners(R, c, &len);
        if (corners && track)
        {
            uint32_t flips = LwTiltFlips(track->state, c, len);
            if (flips)
            {
                track->flips = (uint16_t)(track->flips + flips > 0xFFFFu ? 0xFFFFu : track->flips + flips);
                g_lwTilt.flips += flips; g_lwTilt.flipSamples++;
                if (track->flips > g_lwTilt.flipMax) { g_lwTilt.flipMax = track->flips; g_lwTilt.flipId = id; }
            }
        }
        float up[3]; const float z1[3] = {0, 0, 1}; LwTiltRotate(q, z1, up);
        if (!LwTiltGround(p[0], p[1], nrm)) continue;
        float tilt = LwTiltDeg(up[2]), ground = LwTiltDeg(nrm[2]), off = LwTiltAngle(up, nrm);
        int tile = -1;
        if (mode == 4 && LwTiltLong(R) && MoveTilesOn() && MoveDrawnGround(p[0], p[1], &tile) > -9000.0f && tile >= 0)
        {
            g_lwTilt.longs++;
            if (off > 15.0f) g_lwTilt.longOver++;
            if (off > g_lwTilt.longWorst) { g_lwTilt.longWorst = off; g_lwTilt.longId = id; }
        }
        int prop = -1; /* -1: not checked this scan */
        if (mode == 4)
        {
            g_lwTilt.lvFour++;
            if (tilt >= 30.0f)
            {
                g_lwTilt.up30++; g_lwTilt.lvUp30++;
                if (checks < LW_TILT_CHECKS)
                {
                    checks++; prop = LwTiltCheck(mgr, id, p, first);
                    g_lwTilt.upChecked++; g_lwTilt.lvUpChecked++; g_lwTilt.upOnProp += (uint32_t)prop; g_lwTilt.lvUpOnProp += (uint32_t)prop;
                }
            }
        }
        if (off < LW_TILT_EXCESS) continue;
        g_lwTilt.tilted++;
        int cls = -1; float residual = -1.0f;
        if (off >= 30.0f)
        {
            g_lwTilt.off30++;
            if (mode == 4 && LwTiltMissed(R)) g_lwTilt.off30Miss++;
            if (corners) { cls = LwTiltClass(c, len, &residual); g_lwTilt.four++; g_lwTilt.lvOff30++; g_lwTilt.cls[cls]++; }
            if (prop < 0 && checks < LW_TILT_CHECKS) { checks++; prop = LwTiltCheck(mgr, id, p, first); }
            if (prop > 0 && cls == LW_TILT_FITS) g_lwTilt.fitsOnProp++;
        }
        if (off > g_lwTilt.worst) { g_lwTilt.worst = off; g_lwTilt.worstId = id; g_lwTilt.worstMode = mode; }
        /* one fitting unit a window is example enough: the lines are for the units that do not lie as their rays read */
        if (off >= LW_TILT_LINE_OFF && g_lwTilt.lines < LW_TILT_LINES && g_lwTilt.windowLines < LW_TILT_WINDOW_LINES &&
            (cls != LW_TILT_FITS || g_lwTilt.windowFits < 1) && !LwTiltSeen(id, now))
        {
            g_lwTilt.lines++; g_lwTilt.windowLines++; g_lwTilt.windowFits += cls == LW_TILT_FITS;
            LwTiltLine(mgr, R, id, mode, q, p, tilt, ground, off, cls, residual);
        }
    }
}

/* With the 30 s notes: the units scanned by mode and those off their ground; the counts start again. */
static void LwTiltNote(void)
{
    LONG reads = InterlockedExchange(&g_lwTiltReads, 0), under = InterlockedExchange(&g_lwTiltUnder, 0);
    if (reads) Note("living world: tilt rays (30 s): %ld missed on detailed tiles, %ld of them started under the ground and read it above them", (long)reads, (long)under);
    if (g_lwTilt.scans == 0) return;
    LwTnNote();
    Note("living world: tilt baseline (%s, 30 s): %u four-corner samples, %u of them 30 deg or more from upright, %u of those checked, %u on a prop; this level: %u samples, "
        "%u 30 deg or more off their ground, %u 30 deg or more from upright (%.2f %%), %u checked, %u on a prop",
        LwLive() ? "extra large" : "normal mission", g_lwTilt.modes[4], g_lwTilt.up30, g_lwTilt.upChecked, g_lwTilt.upOnProp, g_lwTilt.lvFour, g_lwTilt.lvOff30, g_lwTilt.lvUp30,
        g_lwTilt.lvFour ? 100.0 * g_lwTilt.lvUp30 / g_lwTilt.lvFour : 0.0, g_lwTilt.lvUpChecked, g_lwTilt.lvUpOnProp);
    Note("living world: tilt (30 s): %u scans, %u unit samples (by rotation mode 0-7: %u %u %u %u %u %u %u %u), %u %.0f deg or more off their ground, %u 30 deg or more (%u of them with a corner that missed), worst %.0f deg (unit %08X, mode %u)",
        g_lwTilt.scans, g_lwTilt.units, g_lwTilt.modes[0], g_lwTilt.modes[1], g_lwTilt.modes[2], g_lwTilt.modes[3], g_lwTilt.modes[4], g_lwTilt.modes[5], g_lwTilt.modes[6],
        g_lwTilt.modes[7], g_lwTilt.tilted, (double)LW_TILT_EXCESS, g_lwTilt.off30, g_lwTilt.off30Miss, (double)g_lwTilt.worst, g_lwTilt.worstId, g_lwTilt.worstMode);
    if (g_lwTilt.four)
        Note("living world: tilt (30 s): of the %u 30 deg or more off, %u four-corner units: %u lie as their own corner rays read (within %.0f deg; %u of them found standing on a prop), %u with a corner that read the ground at its start, %u others that do not: %u with a corner that missed, %u with a corner that started under a surface, %u neither",
            g_lwTilt.off30, g_lwTilt.four, g_lwTilt.cls[LW_TILT_FITS], (double)LW_TILT_FIT, g_lwTilt.fitsOnProp, g_lwTilt.cls[LW_TILT_GROUND],
            g_lwTilt.four - g_lwTilt.cls[LW_TILT_FITS] - g_lwTilt.cls[LW_TILT_GROUND], g_lwTilt.cls[LW_TILT_MISSED], g_lwTilt.cls[LW_TILT_UNDER], g_lwTilt.cls[LW_TILT_LOOSE]);
    if (g_lwTilt.flips)
        Note("living world: tilt (30 s): %u corner readings went from under 0 to a miss or back between two scans, in %u unit samples; the most this level: %u (unit %08X)",
            g_lwTilt.flips, g_lwTilt.flipSamples, g_lwTilt.flipMax, g_lwTilt.flipId);
    if (g_lwTilt.longs)
        Note("living world: tilt (30 s): units with corners over 3 m apart on detailed tiles: %u samples, %u over 15 deg off their ground, worst %.0f deg off (unit %08X)",
            g_lwTilt.longs, g_lwTilt.longOver, (double)g_lwTilt.longWorst, g_lwTilt.longId);
    if (g_lwTilt.checked)
    {
        uintptr_t dir = 0, nav = 0, H = 0; uint32_t id = 0, slots = 0; int64_t carvers = -2; uint32_t live = 0;
        if (EvpPtr(g_lw.game + LW_DIRECTOR, &dir) && LwNavWorld(dir, &nav, &id, &H)) carvers = LwTiltCarvers(nav, &slots);
        if (TryAcquireSRWLockShared(&g_streamLock))
        {
            for (uint32_t k = 0; k < g_stream.count && k < kStreamMaxLocations; k++) live += g_streamLive[k] != 0;
            ReleaseSRWLockShared(&g_streamLock);
        }
        if (g_lwTilt.onProp)
            Note("living world: tilt on props (30 s): of the %u with a carver over the prop's point: the face in the unit's column lies over the carvers %u, under their reach %u, "
                "within it %u, no face in the column %u (character height %.2f m); a face in the column for the unit's own agent type %u of %u asked; "
                "units within %.0f m of where they were first seen %u, further %u",
                g_lwTilt.propCarver, g_lwTilt.carverOver, g_lwTilt.carverUnder, g_lwTilt.carverMeets, g_lwTilt.carverNoFace, (double)g_lwTn.height, g_lwTilt.agentColumn,
                g_lwTilt.agentAsked, (double)LW_TILT_STAYED, g_lwTilt.propStayed, g_lwTilt.propWalked);
        if (g_lwTilt.onProp)
            Note("living world: tilt on props (30 s): of the %u on a prop: nav objects over the prop's point: a carver %u, geometry and no carver %u, a painter only %u, none %u, not read %u; "
                "a face at its column for any agent %u; asked for the unit's own agent type %u, a face %u; in a streamed stamp's footprint %u, a level-made stamp's %u, no stamp's %u",
                g_lwTilt.onProp, g_lwTilt.propCarver, g_lwTilt.propGeometry, g_lwTilt.propPainter, g_lwTilt.propBare, g_lwTilt.propUnread, g_lwTilt.faceAny, g_lwTilt.agentAsked,
                g_lwTilt.agentFace, g_lwTilt.inStreamed, g_lwTilt.inLevel, g_lwTilt.noStamp);
        Note("living world: tilt on props (30 s): %u units 30 deg or more off their ground or from upright checked, %u standing on a prop; of those a navmesh face under the prop %u, on its top %u; props with static modifiers %u, dynamic %u, none %u, no record %u; nav world carvers live %lld of %u; stamps live %u",
            g_lwTilt.checked, g_lwTilt.onProp, g_lwTilt.navUnder, g_lwTilt.navTop, g_lwTilt.propStatic, g_lwTilt.propDynamic, g_lwTilt.propNone, g_lwTilt.propNoRecord,
            (long long)carvers, slots, live);
    }
    g_lwTilt.checked = g_lwTilt.onProp = g_lwTilt.navUnder = g_lwTilt.navTop = g_lwTilt.propStatic = g_lwTilt.propDynamic = g_lwTilt.propNone = g_lwTilt.propNoRecord = 0;
    g_lwTilt.propCarver = g_lwTilt.propGeometry = g_lwTilt.propPainter = g_lwTilt.propBare = g_lwTilt.propUnread = g_lwTilt.faceAny = g_lwTilt.agentAsked = g_lwTilt.agentFace = 0;
    g_lwTilt.inStreamed = g_lwTilt.inLevel = g_lwTilt.noStamp = g_lwTilt.up30 = g_lwTilt.upChecked = g_lwTilt.upOnProp = 0;
    g_lwTilt.carverOver = g_lwTilt.carverUnder = g_lwTilt.carverMeets = g_lwTilt.carverNoFace = g_lwTilt.agentColumn = g_lwTilt.propStayed = g_lwTilt.propWalked = 0;
    g_lwTilt.flips = g_lwTilt.flipSamples = 0;
    g_lwTilt.windowLines = g_lwTilt.windowFits = 0;
    g_lwTilt.four = g_lwTilt.fitsOnProp = 0; memset(g_lwTilt.cls, 0, sizeof g_lwTilt.cls);
    g_lwTilt.longs = g_lwTilt.longOver = g_lwTilt.longId = 0; g_lwTilt.longWorst = 0;
    g_lwTilt.scans = g_lwTilt.units = g_lwTilt.tilted = g_lwTilt.off30 = g_lwTilt.off30Miss = 0; memset(g_lwTilt.modes, 0, sizeof g_lwTilt.modes); g_lwTilt.worst = 0; g_lwTilt.worstId = g_lwTilt.worstMode = 0;
}

/* Research builds: the same scan and notes on a normal mission's host, so one session holds a baseline for the extra
 * large level's shares. */
static void LwTiltBaseline(void)
{
#ifdef CO_RESEARCH
    static ULONGLONG noted;
    int host = 0;
    if (!ActMission(g_lw.game, &host, NULL) || !host) return;
    LwTiltScan();
    ULONGLONG now = GetTickCount64();
    if (now - noted >= 30000) { noted = now; LwTiltNote(); }
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
/* corners: 4 x (offset x, offset y, distance read); the class, with the residual in degrees */
int CoXlTestTiltClass(const float* corners, float len, float* residual)
{
    float c[4][3];
    memcpy(c, corners, sizeof c);
    return LwTiltClass(c, len, residual);
}
/* readings: 4 distances; prev: the 4 last states, updated */
uint32_t CoXlTestTiltFlips(uint8_t* prev, const float* readings, float len)
{
    float c[4][3] = {{0}};
    for (int k = 0; k < 4; k++) c[k][2] = readings[k];
    return LwTiltFlips(prev, c, len);
}
/* The place of `id` in a table of LW_TILT_TRACK units the test owns (-1: none), and where it was first seen. */
int CoXlTestTiltTrack(void* table, uint32_t id, const float* p, float* first)
{
    LwTiltUnit* u = LwTiltTrack((LwTiltUnit*)table, id, p);
    if (u == NULL) return -1;
    memcpy(first, u->first, 12);
    return (int)(u - (LwTiltUnit*)table);
}
void CoXlTestTiltHold(int on) { if (on) AcquireSRWLockExclusive(&g_tilesLock); else ReleaseSRWLockExclusive(&g_tilesLock); }
void CoXlTestTiltCounts(long* reads, long* under) { *reads = InterlockedExchange(&g_lwTiltReads, 0); *under = InterlockedExchange(&g_lwTiltUnder, 0); }
#endif
