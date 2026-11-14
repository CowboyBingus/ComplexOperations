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
static int LwTiltFootOnProp(uint32_t i, uint32_t now); static uint32_t LwTiltFootNow(void);
static float LwTiltFlatShare(float n[4][3]);

/* A unit's resource name from the engine's unit registry (helldivers2.exe: object +8 the unit, +0x30 its resource,
 * whose first 8 bytes are the name's hash); 0 when it does not read. For lines only: it tells a rock from a ramp. */
static uint64_t LwTiltUnitName(uint32_t unit)
{
#ifdef CO_TEST
    return unit ? 0xAB00000000000000ull | unit : 0; /* the test's game has no registry */
#else
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), registry, gens, objects, object, res;
    uint32_t index = unit & 0x3FFFFFu, count; uint8_t gen; uint64_t name = 0;
    if (!unit || !EvpPtr(exe + EVP_EXE_UNITS, &registry) || SafeRead(registry + 0x98, &count, 4) || index >= count ||
        !EvpPtr(registry + 0xA0, &gens) || SafeRead(gens + index, &gen, 1) || gen != ((unit >> 22) & 0xFF) ||
        !EvpPtr(registry + 0x88, &objects) || !EvpPtr(objects + (uintptr_t)index * 8, &object) || EvpU32(object + 8, 0) != unit ||
        !EvpPtr(object + 0x30, &res) || SafeRead(res, &name, 8)) return 0;
    return name;
#endif
}

#if defined(CO_RESEARCH) || defined(CO_TEST)
/* Research: how a four-corner unit stands on a prop, to compare levels and the prop rule's states. The rule poses a
 * unit by the ground under a prop, so each sample on a prop is measured against what is really under its corners:
 * the angle to that footing, the deepest and the highest corner, by the footing's height over the drawn ground. */
#define LW_TILT_SUNK_LINES 16  /* unit lines a level and a value of xl-lean.txt */
#define LW_TILT_BANDS 3        /* the share of the game's reading the footing rule gives: none, part, all */
#define LW_TILT_COLUMN_UP 1.0f /* m over a point its column starts: a surface higher is over the unit, not under it */
#define LW_TILT_FOOT_NEAR 0.75f /* m between a corner and the prop under it for the unit to stand on that prop */
#define LW_TILT_TURNS 512
#define LW_TILT_INSIDE_UP 4.0f /* m over a buried corner its second column starts: deeper counts as this deep */
typedef struct LwTiltBucket { uint32_t n, lean, angled, sink05, float05; float angle, sink, air; } LwTiltBucket;
static struct
{
    uint32_t samples, prop, propLean, bareLean, flat, pairs, turned; float turnSum, turnMax; LwTiltBucket b[LW_TILT_BANDS][2];
    uint32_t inside[2][3];                                 /* [all, on a prop][a corner inside a body, over 0.5 m, over 1.0 m] */
    uint32_t changed, changedOver; float changedSum, changedMax; /* units whose lean limit changed kind since the scan before */
} g_lwTiltProp;
typedef struct LwTiltSeenUp { uint32_t id, scan; float up[3]; uint8_t held; } LwTiltSeenUp;
static struct { uint32_t scan; LwTiltSeenUp u[LW_TILT_TURNS], held[LW_TILT_TURNS]; } g_lwTiltTurn;
static int g_lwTiltSunkLines, g_lwTiltTallLines;
static float LwTiltFootShare(float mean, float len, float* top);

/* One ray of the rotation's own type from `from`, 12 m straight down: its closest hit (0x2C B: place +0, normal
 * +0xC, owner +0x1C, body +0x20, +0x28 bit 0 when it started inside a body) and the level terrain's body; 0 when it
 * met nothing. The game's queued corner rays end in this same call (0x175C63C), which fills +0x28 (exe 0x7F4B4A). */
static int LwTiltCast(uintptr_t mgr, const float* from, uint32_t max, uint8_t* hit, uint32_t* terrain)
{
    uintptr_t phys = 0, fn = 0, settings = 0; uint32_t type = 0;
    uint8_t hits[8 * 0x2C]; const float down[3] = {0, 0, -1};
    memset(hits, 0, 0x2C);
    if (!EvpPtr(g_lw.game + LW_TILT_PHYS, &phys) || !EvpPtr(phys + 0x68, &fn) || !fn || !EvpPtr(mgr + 8, &settings) ||
        SafeRead(settings + 0xD4, &type, 4) || SafeRead(mgr + 0x18, terrain, 4)) return 0;
    if (((LwTiltRayFn)fn)(type, from, down, 12.0f, 0, hits, max) == 0) return 0;
    memcpy(hit, hits, 0x2C);
    return 1;
}

/* The closest hit from 1 m over p: 1 a prop, 0 not, -1 no hit; *z the hit's height, ids its owner and body, n its
 * normal (or NULL). A column from higher up would take a roof or an overhang for the footing. */
static int LwTiltUnderAt(uintptr_t mgr, const float* p, float* z, uint32_t* ids, float* n)
{
    uint8_t hit[0x2C]; uint32_t terrain = 0; const float origin[3] = {p[0], p[1], p[2] + LW_TILT_COLUMN_UP};
    if (!LwTiltCast(mgr, origin, 8, hit, &terrain)) return -1;
    memcpy(ids, hit + 0x1C, 8); memcpy(z, hit + 8, 4);
    if (n != NULL) memcpy(n, hit + 0xC, 12);
    return LwTiltIsProp(ids[0], ids[1], terrain);
}
static int LwTiltOnProp(uintptr_t mgr, const float* p, float* z) { uint32_t ids[2]; return LwTiltUnderAt(mgr, p, z, ids, NULL); }

/* Whether p lies inside a body: a ray from p itself says it started inside one (the bit the reader tests, 0x620852).
 * *depth: how far under the surface over it, from a second column 4 m up: its hit's height less p's; 4 when that
 * column starts inside a body too, 0 when it meets nothing over p. The column from 1 m cannot see this: it starts
 * inside the body and passes on to the ground under it. */
static int LwTiltInsideAt(uintptr_t mgr, const float* p, float* depth)
{
    uint8_t hit[0x2C]; uint32_t terrain = 0, flags = 0; float z = 0; const float over[3] = {p[0], p[1], p[2] + LW_TILT_INSIDE_UP};
    if (!LwTiltCast(mgr, p, 1, hit, &terrain)) return 0;
    memcpy(&flags, hit + 0x28, 4);
    if (!(flags & 1)) return 0;
    *depth = 0;
    if (!LwTiltCast(mgr, over, 1, hit, &terrain)) return 1;
    memcpy(&flags, hit + 0x28, 4); memcpy(&z, hit + 8, 4);
    *depth = (flags & 1) ? LW_TILT_INSIDE_UP : z > p[2] ? z - p[2] : 0;
    return 1;
}

/* The plane through the corners' column hits (least squares, three at least): its unit normal; 0 when they do not
 * span one. */
static int LwTiltPlane(float c[4][3], const float* z, const int* on, float* n)
{
    float mx = 0, my = 0, mz = 0, sxx = 0, sxy = 0, syy = 0, sxz = 0, syz = 0; int m = 0;
    for (int k = 0; k < 4; k++) if (on[k] >= 0) { mx += c[k][0]; my += c[k][1]; mz += z[k]; m++; }
    if (m < 3) return 0;
    mx /= (float)m; my /= (float)m; mz /= (float)m;
    for (int k = 0; k < 4; k++)
        if (on[k] >= 0)
        {
            const float x = c[k][0] - mx, y = c[k][1] - my, h = z[k] - mz;
            sxx += x * x; sxy += x * y; syy += y * y; sxz += x * h; syz += y * h;
        }
    const float det = sxx * syy - sxy * sxy;
    if (!(det > 1e-4f)) return 0;
    const float a = (sxz * syy - syz * sxy) / det, b = (syz * sxx - sxz * sxy) / det, l = sqrtf(1.0f + a * a + b * b);
    n[0] = -a / l; n[1] = -b / l; n[2] = 1.0f / l;
    return 1;
}

/* The unit's slot in a table of last scans: its own, else a free or stale one among eight from its hash. */
static LwTiltSeenUp* LwTiltSeenSlot(LwTiltSeenUp* table, uint32_t id)
{
    LwTiltSeenUp* u = NULL;
    for (uint32_t k = 0; k < 8; k++)
    {
        LwTiltSeenUp* s = &table[(id * 2654435761u + k) % LW_TILT_TURNS];
        if (s->id == id) return s;
        if (u == NULL && (s->id == 0 || s->scan + 1 < g_lwTiltTurn.scan)) u = s;
    }
    return u;
}

/* How far a unit on a prop turned since the scan before: a rule that switches readings would show as turning. */
static void LwTiltTurn(uint32_t id, const float* up)
{
    LwTiltSeenUp* u = LwTiltSeenSlot(g_lwTiltTurn.u, id);
    if (u == NULL) return;
    if (u->id == id && u->scan + 1 == g_lwTiltTurn.scan)
    {
        const float turn = LwTiltAngle(up, u->up);
        g_lwTiltProp.pairs++; g_lwTiltProp.turnSum += turn;
        if (turn > g_lwTiltProp.turnMax) g_lwTiltProp.turnMax = turn;
        if (turn > 10.0f) g_lwTiltProp.turned++;
    }
    u->id = id; u->scan = g_lwTiltTurn.scan; memcpy(u->up, up, 12);
}

/* How far a unit turned over a scan in which its lean limit changed kind: `held` is the hook's own flag (a corner
 * on a prop: no slope added). A unit on a hillside that steps on a pebble loses the slope and gets it back 500 ms
 * after: this counts what that does to it. */
static void LwTiltHeldTurn(uint32_t id, const float* up, int held)
{
    LwTiltSeenUp* u = LwTiltSeenSlot(g_lwTiltTurn.held, id);
    if (u == NULL) return;
    if (u->id == id && u->scan + 1 == g_lwTiltTurn.scan && u->held != (uint8_t)held)
    {
        const float turn = LwTiltAngle(up, u->up);
        g_lwTiltProp.changed++; g_lwTiltProp.changedSum += turn;
        if (turn > g_lwTiltProp.changedMax) g_lwTiltProp.changedMax = turn;
        if (turn > 10.0f) g_lwTiltProp.changedOver++;
    }
    u->id = id; u->scan = g_lwTiltTurn.scan; u->held = (uint8_t)held; memcpy(u->up, up, 12);
}

/* A unit on a prop against what is under its corners (the unit's plane at its corner offsets, R+0x60), counted as
 * the footing rule sees it: the mean height of the four corners over the drawn ground, the ground and a hole as 0,
 * gives the share of the game's reading. Footed: three or more corners within 0.75 m of a prop under them; a unit
 * beside a rock is not. A unit line for a corner over 0.5 m under or 1 m over the surface, 30 deg off the footing, or
 * over 0.5 m inside a body (deep: -1 no corner inside). The line gives the faces under the corners and what the
 * normals rule would make of them. */
static void LwTiltFooting(uint32_t id, const float* p, const float* up, float fromUpright, float c[4][3], const float* z, const int* on, uint32_t ids[4][2], float len, float face[4][3], float deep)
{
    float n[3], sink = -1e9f, air = -1e9f, under[4] = {NAN, NAN, NAN, NAN}, sum = 0, top = 0; uint32_t deepest[2] = {0, 0}; int corners = 0, onIt = 0, tile;
    LwTiltFootShare(0, len, &top);
    for (int k = 0; k < 4; k++)
    {
        if (on[k] < 0) { corners++; continue; }
        under[k] = z[k] - c[k][2];
        if (under[k] > sink) { sink = under[k]; memcpy(deepest, ids[k], sizeof deepest); }
        if (-under[k] > air) air = -under[k];
        if (on[k] != 1) { corners++; continue; }
        const float drawn = MoveDrawnGround(c[k][0], c[k][1], &tile), h = z[k] - drawn;
        if (!(drawn > -9000.0f) || tile < 0) continue; /* no ground to measure from: left out, as the rule leaves it */
        sum += h < 0 ? 0 : h > top ? top : h; corners++;
        if (fabsf(under[k]) <= LW_TILT_FOOT_NEAR) onIt++;
    }
    if (!corners) return;
    if (!LwTiltPlane(c, z, on, n)) { g_lwTiltProp.flat++; return; }
    const float mean = sum / (float)corners, share = LwTiltFootShare(mean, len, &top), angle = LwTiltAngle(up, n); const int footed = onIt >= 3;
    LwTiltBucket* b = &g_lwTiltProp.b[share <= 0.0f ? 0 : share < 1.0f ? 1 : 2][footed];
    b->n++; b->angle += angle; b->sink += sink > 0 ? sink : 0; b->air += air > 0 ? air : 0;
    if (fromUpright >= 30.0f) b->lean++;
    if (angle >= 20.0f) b->angled++;
    if (sink > 0.5f) b->sink05++;
    if (air > 0.5f) b->float05++;
    if (footed && share > 0.0f && g_lwTiltTallLines < LW_TILT_SUNK_LINES)
    {
        g_lwTiltTallLines++; /* where a run's Helldiver is to stand: a footing the rule keeps */
        Note("living world: tilt props: tall: unit %08X at (%.1f, %.1f, %.2f) stands on a prop %.2f m over the drawn ground, share %.2f of the game's reading", id, (double)p[0], (double)p[1],
            (double)p[2], (double)mean, (double)share);
    }
    if (!(sink > 0.5f || air > 1.0f || angle >= 30.0f || deep > 0.5f) || g_lwTiltSunkLines >= LW_TILT_SUNK_LINES) return;
    g_lwTiltSunkLines++;
    uint32_t unit = 0; EvpEntityResource(g_lw.game, id, &unit);
    int props = 0;
    for (int k = 0; k < 4; k++) props += on[k] == 1;
    Note("living world: tilt props: sunk: unit %08X (resource %016llX) at (%.1f, %.1f, %.2f): %.1f deg from upright, %.1f deg off its footing, footing %.2f m over the drawn ground (share %.2f, %s); "
        "corners under the surface over them (m, nan: none): %+.2f %+.2f %+.2f %+.2f; the deepest under owner %08X (resource %016llX), body %08X; "
        "faces under the corners (normals, nan: none): (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f), by normals a share of %.2f; a corner inside a body by %.2f m (-1: none)",
        id, (unsigned long long)LwTiltUnitName(unit), (double)p[0], (double)p[1], (double)p[2], (double)fromUpright, (double)angle, (double)mean, (double)share, footed ? "footed" : "not footed",
        (double)under[0], (double)under[1], (double)under[2], (double)under[3], deepest[0], (unsigned long long)LwTiltUnitName(deepest[0]), deepest[1],
        (double)face[0][0], (double)face[0][1], (double)face[0][2], (double)face[1][0], (double)face[1][1], (double)face[1][2], (double)face[2][0], (double)face[2][1], (double)face[2][2],
        (double)face[3][0], (double)face[3][1], (double)face[3][2], props == 4 ? (double)LwTiltFlatShare(face) : 0.0, (double)deep);
}

/* One four-corner unit of this scan (rotation slot i): on a prop when the column under its centre or under any corner
 * meets one first (a unit half on a low rock has its centre on the ground). Sunk: a corner point inside a body, by
 * its depth under the surface over it. */
static void LwTiltPropSample(uintptr_t mgr, uintptr_t R, uint32_t i, uint32_t id, const float* q, const float* p, float fromUpright)
{
    const float lx[3] = {1, 0, 0}, ly[3] = {0, 1, 0}, lz[3] = {0, 0, 1}; float ax[3], ay[3], up[3], c[4][3], z[4] = {0, 0, 0, 0}, cz = 0, face[4][3];
    uint32_t ids[4][2], centreIds[2]; int on[4] = {-1, -1, -1, -1}, read[4] = {0, 0, 0, 0}, hits = 0, props = 0; float len = 0, deep = -1.0f, depth = 0;
    const int centre = LwTiltUnderAt(mgr, p, &cz, centreIds, NULL);
    LwTiltRotate(q, lx, ax); LwTiltRotate(q, ly, ay); LwTiltRotate(q, lz, up);
    for (int k = 0; k < 4; k++)
    {
        float o[2];
        face[k][0] = face[k][1] = face[k][2] = NAN;
        if (SafeRead(R + 0x58 + 0x18u * k + 8, o, sizeof o)) continue;
        for (int j = 0; j < 3; j++) c[k][j] = p[j] + ax[j] * o[0] + ay[j] * o[1];
        read[k] = 1;
        on[k] = LwTiltUnderAt(mgr, c[k], &z[k], ids[k], face[k]);
        hits += on[k] >= 0; props += on[k] == 1;
    }
    if (centre < 0 && hits == 0) return;
    g_lwTiltProp.samples++;
    for (int k = 0; k < 4; k++)
        if (read[k] && LwTiltInsideAt(mgr, c[k], &depth) && depth > deep) deep = depth;
    const int prop = centre == 1 || props != 0;
    for (int j = 0; j <= prop; j++)
    {
        if (deep >= 0.0f) g_lwTiltProp.inside[j][0]++;
        if (deep > 0.5f) g_lwTiltProp.inside[j][1]++;
        if (deep > 1.0f) g_lwTiltProp.inside[j][2]++;
    }
    LwTiltHeldTurn(id, up, LwTiltFootOnProp(i, LwTiltFootNow()));
    if (!prop) { if (fromUpright >= 30.0f) g_lwTiltProp.bareLean++; return; }
    g_lwTiltProp.prop++;
    if (fromUpright >= 30.0f) g_lwTiltProp.propLean++;
    LwTiltTurn(id, up);
    if (!SafeRead(R + 0x48, &len, 4)) LwTiltFooting(id, p, up, fromUpright, c, z, on, ids, len, face, deep);
}

static void LwTiltPropNote(void)
{
    static const char* const kName[LW_TILT_BANDS] = {"with none", "with part", "with all"};
    if (g_lwTiltProp.samples)
        Note("living world: tilt props (30 s): %u four-corner samples with a hit under them, %u on a prop (%.2f %%); 30 deg or more from upright: %u of those on a prop, %u of the others; "
            "on a prop two scans running %u: up turned %.1f deg on average, %.1f at most, %u over 10 deg; %u with corners that span no plane; "
            "%u changed between a lean limit with their ground's slope and one without since the scan before: up turned %.1f deg on average, %.1f at most, %u over 10 deg",
            g_lwTiltProp.samples, g_lwTiltProp.prop, 100.0 * g_lwTiltProp.prop / g_lwTiltProp.samples, g_lwTiltProp.propLean, g_lwTiltProp.bareLean, g_lwTiltProp.pairs,
            g_lwTiltProp.pairs ? (double)(g_lwTiltProp.turnSum / (float)g_lwTiltProp.pairs) : 0.0, (double)g_lwTiltProp.turnMax, g_lwTiltProp.turned, g_lwTiltProp.flat,
            g_lwTiltProp.changed, g_lwTiltProp.changed ? (double)(g_lwTiltProp.changedSum / (float)g_lwTiltProp.changed) : 0.0, (double)g_lwTiltProp.changedMax, g_lwTiltProp.changedOver);
    if (g_lwTiltProp.samples)
        Note("living world: tilt inside (30 s): of %u four-corner samples %u with a corner inside a body, %u over 0.5 m under its top, %u over 1.0 m; of the %u on a prop: %u, %u, %u",
            g_lwTiltProp.samples, g_lwTiltProp.inside[0][0], g_lwTiltProp.inside[0][1], g_lwTiltProp.inside[0][2], g_lwTiltProp.prop, g_lwTiltProp.inside[1][0], g_lwTiltProp.inside[1][1],
            g_lwTiltProp.inside[1][2]);
    for (int k = 0; k < LW_TILT_BANDS * 2; k++)
    {
        const LwTiltBucket* b = &g_lwTiltProp.b[k / 2][k % 2];
        if (b->n)
            Note("living world: tilt footing (30 s): footing %s of the game's reading, %s: %u samples, %u of them 30 deg or more from upright; off their footing %.1f deg on average, %u 20 deg or more; "
                "the deepest corner %.2f m under the surface on average, %u over 0.5 m; the highest corner %.2f m over it on average, %u over 0.5 m",
                kName[k / 2], k % 2 ? "footed" : "not footed", b->n, b->lean, (double)(b->angle / (float)b->n), b->angled, (double)(b->sink / (float)b->n), b->sink05, (double)(b->air / (float)b->n), b->float05);
    }
    memset(&g_lwTiltProp, 0, sizeof g_lwTiltProp);
}

/* Research: which units carry which rotation mode, once a resource and mode a level: the prop rule acts on mode 4,
 * whatever the unit is. An enemy is a unit the host's AI holds ([game+0x33266C8] hash +0x2330). */
#define LW_TILT_KINDS 64
static struct { int n; uint64_t name[LW_TILT_KINDS]; uint32_t mode[LW_TILT_KINDS]; } g_lwTiltKind;
static void LwTiltKind(uint32_t id, uint32_t mode)
{
    uintptr_t ai = 0; uint32_t unit = 0, at = 0; const uint64_t res = EvpEntityResource(g_lw.game, id, &unit), name = LwTiltUnitName(unit);
    for (int k = 0; k < g_lwTiltKind.n; k++) if (g_lwTiltKind.name[k] == name && g_lwTiltKind.mode[k] == mode) return;
    if (g_lwTiltKind.n >= LW_TILT_KINDS) return;
    g_lwTiltKind.name[g_lwTiltKind.n] = name; g_lwTiltKind.mode[g_lwTiltKind.n++] = mode;
    Note("living world: tilt: kind: resource %016llX (entity resource %016llX) has rotation mode %u: unit %08X, %s", (unsigned long long)name, (unsigned long long)res, mode, id,
        EvpPtr(g_lw.game + 0x33266C8u, &ai) && EvpHash(ai + 0x2330, id, 8, 4, &at) ? "an enemy the host's AI holds" : "not an enemy the host's AI holds");
}

/* Research: what the prop rule's check costs in the rotation update, every hit it is asked about. */
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
/* Research: what a scan costs its frame, the columns under every four-corner unit included; frame-time evidence
 * of the same run is to be read with it. */
static struct { LONG64 ticks, worst; LONG scans; } g_lwTiltScanCost;
static void LwTiltScanCost(LONG64 ticks)
{
    g_lwTiltScanCost.ticks += ticks; g_lwTiltScanCost.scans++;
    if (ticks > g_lwTiltScanCost.worst) g_lwTiltScanCost.worst = ticks;
}
static void LwTiltScanCostNote(LONG64 perSecond)
{
    if (g_lwTiltScanCost.scans && perSecond > 0)
        Note("living world: tilt scan (30 s): %ld scans took %.2f ms, %.3f ms a scan on average, %.3f ms at worst (each in one frame, the research columns in it)", (long)g_lwTiltScanCost.scans,
            1000.0 * (double)g_lwTiltScanCost.ticks / (double)perSecond, 1000.0 * (double)g_lwTiltScanCost.ticks / (double)perSecond / g_lwTiltScanCost.scans,
            1000.0 * (double)g_lwTiltScanCost.worst / (double)perSecond);
    memset(&g_lwTiltScanCost, 0, sizeof g_lwTiltScanCost);
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
 * game's (craters are not in the tile's CPU heights). The tiles are read under g_tilesLock, tried and never waited on. */
#define LW_TILT_READ 0x620810u
typedef float (*LwTiltReadFn)(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse);
static LwTiltReadFn g_lwTiltRead;
static volatile LONG g_lwTiltUnder, g_lwTiltReads;
#ifdef CO_RESEARCH
static volatile LONG g_lwTiltInside;
#endif
static void LwLeanAt(const float* origin); /* living_world_leancap.c: where this thread's unit stands, for its limit */
static void LwLeanOnProp(int on);
static void LwLeanLevel(float deg);

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

/* Whether a live tile's square covers (x, y): 1, 0, or -1 while a writer holds the tiles. Under a live tile the level
 * terrain's collision is lowered to -500 m, so its hit is the ground only where this is 0. */
static int LwTiltTileCovers(float x, float y)
{
    int on = 0;
    if (!TryAcquireSRWLockShared(&g_tilesLock)) return -1;
    for (int k = 0; k < NEAR_TILES_MAX && !on; k++)
        on = g_tiles[k].live && g_tiles[k].level == g_world.levels && fabsf(x - g_tiles[k].centre[0]) < NEAR_HIDE / 2 && fabsf(y - g_tiles[k].centre[1]) < NEAR_HIDE / 2;
    ReleaseSRWLockShared(&g_tilesLock);
    return on;
}

/* ---- Where no detailed tile is live the ground is the level's coarse terrain, and the game already holds its hit --------
 * With each mode 4 corner ray the game queues a second one (0x62038E -> 0x175CBD0): from the corner ray's start at
 * z 5000 straight down 10000 m against the level terrain's body [mgr+0x18] alone. Its index is the reader's fifth
 * argument; the record (rays + (index - 1) x 0x40) holds the hit at +8, the start at +0x10, the count at +0x30, the
 * body at +0x3C and 1 at +0x40 (a body ray); the hit holds the place at +0 and the normal at +0xC, in world metres
 * (the reader compares its z with the corner ray's start, 0x62091A). Units eight players spread over more squares
 * than there are tiles, and a client without the tile, stand on this ground. */
#define LW_TILT_LEVEL_LOW -499.0f /* the lowest height a terrain holds: the collision lowered under a tile */
#define LW_TILT_LEVEL_FACE 0.2f   /* a normal flatter than this is the lowered square's wall, or no ground to lie along */
typedef struct LwTiltLevel { float z, n[3]; } LwTiltLevel;
static volatile LONG g_lwTiltPropLevel;

static int LwTiltLevelHit(uintptr_t rays, uint32_t coarse, uint32_t terrain, const float* o, LwTiltLevel* c)
{
    uint8_t rec[0x3C]; uintptr_t hit = 0; uint32_t count = 0, body = 0, kind = 0; float start[2], h[6];
    if (coarse == 0 || terrain == 0xFFFFFFFFu || SafeRead(rays + 0x40ull * (coarse - 1) + 8, rec, sizeof rec)) return 0;
    memcpy(&hit, rec, 8); memcpy(start, rec + 8, 8); memcpy(&count, rec + 0x28, 4); memcpy(&body, rec + 0x34, 4); memcpy(&kind, rec + 0x38, 4);
    if (count == 0 || kind != 1 || body != terrain || fabsf(start[0] - o[0]) > 0.01f || fabsf(start[1] - o[1]) > 0.01f || hit == 0 || SafeRead(hit, h, sizeof h)) return 0;
    if (!(h[2] > LW_TILT_LEVEL_LOW) || !(h[5] > LW_TILT_LEVEL_FACE) || !(fabsf(h[3] * h[3] + h[4] * h[4] + h[5] * h[5] - 1.0f) < 0.02f)) return 0;
    c->z = h[2]; memcpy(c->n, h + 3, 12);
    return 1;
}

/* The length from o along dir to the level terrain under o, taken as the plane of its hit; negative when o is under
 * it. 0 for a ray over 60 deg from straight down, as on a tile, or one that runs along the plane. */
static int LwTiltLevelAlong(const LwTiltLevel* c, const float* o, const float* dir, float* t)
{
    const float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 1e-4f) || !(dir[2] / n < -0.5f)) return 0;
    const float into = -(dir[0] * c->n[0] + dir[1] * c->n[1] + dir[2] * c->n[2]) / n;
    if (!(into > LW_TILT_LEVEL_FACE)) return 0;
    *t = (o[2] - c->z) * c->n[2] / into;
    return 1;
}

/* That plane's height at (x, y). */
static float LwTiltLevelGround(const LwTiltLevel* c, const float* o, float x, float y) { return c->z - (c->n[0] * (x - o[0]) + c->n[1] * (y - o[1])) / c->n[2]; }

/* ---- On complex levels a four-corner ray that meets a prop reads the drawn ground ------------------------------------
 * The pose (0x6226F1) turns a unit until its four corner readings are alike, so a corner on a rock lifts that side.
 * The game's navmesh keeps units off faces over 60 deg and off what its terrain covers; an extra large level's terrain
 * is rasterised in 4 m cells (exe 0x89A744) and lies over low rocks, so units cross them and lay along their sides.
 * A prop's hit (the level terrain's body is [mgr+0x18]) then reads the ray's length to the drawn ground: the unit lies
 * along its ground, at the height the game gave it. Mode 4 only (R+0x54); where no drawn ground reads the hit stays
 * the game's. The pose stands a unit upright when its readings average the full length or under 0 (0x622CA0). */
enum { LW_KEPT_TILE = 1, LW_KEPT_FLAT, LW_KEPT_SWING, LW_KEPT_KINDS }; /* why a prop's hit stayed the game's */
static volatile LONG g_lwTiltPropsOn = 1, g_lwTiltPropGround, g_lwTiltPropFooted, g_lwTiltPropPart, g_lwTiltPropKept[LW_KEPT_KINDS];

/* The length from o along dir to the live tiles' ground, negative when o is under it; 0 when it reads. A corner ray
 * runs along the unit's own down turned up to 60 deg toward its motion (0x62015D), so the steps below grow where the
 * ground is steeper than the ray: no reading then, as for a ray over 60 deg from straight down or with no live tile. */
#define LW_TILT_SETTLED 0.05f
static int LwTiltGroundAlong(const float* o, const float* dir, float* t)
{
    const float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 1e-4f) || !(dir[2] / n < -0.5f)) return LW_KEPT_FLAT;
    float at = 0, step = 1.0f;
    for (int k = 0; k < 8 && step > 0.01f; k++)
    {
        const float g = LwTiltTileGround(o[0] + dir[0] / n * at, o[1] + dir[1] / n * at);
        if (!(g > -9000.0f)) return LW_KEPT_TILE;
        const float next = (o[2] - g) / (-dir[2] / n);
        step = fabsf(next - at); at = next;
    }
    if (!(step <= LW_TILT_SETTLED)) return LW_KEPT_SWING;
    *t = at;
    return 0;
}

/* A prop's hit read as the ground t away: at most the full length, the game's answer to a hole, and at least minus
 * it, so one corner deep under the ground cannot outweigh the other three (a corner weighs 1 - reading / length). */
static float LwTiltPropRead(float t, float len) { return t > len ? len : t < -len ? -len : t; }

/* ---- Footing: a unit that stands on a tall prop keeps the game's reading ---------------------------------------------
 * The rule above lays a unit along the ground under a low rock. On a ramp, a roof or a big rock's top its feet are
 * all on the prop, and the ground under it is the wrong thing to lie along. The pose reads one corner a frame (corner
 * [mgr] & 3, 0x6226F1), so each corner's last hit is kept per unit and ONE weight goes to all four: a weight per
 * corner would make the footing the pose sees up to three times steeper than the prop where the weight changes. */
#define LW_TILT_FOOTING 2         /* 0 the rule alone, 1 one balanced body keeps the game's, 2 by height, 3 by height or by normals */
#define LW_TILT_FOOT_UNITS 1024
#define LW_TILT_FOOT_FRESH 500u   /* ms a corner's hit counts: four frames at 8 fps */
#define LW_TILT_FOOT_FAR 10.0f    /* m from the slot's last ray: another unit took the slot */
#define LW_TILT_FOOT_LOW 2.0f     /* m over the drawn ground where the game's reading starts to count, at most */
#define LW_TILT_FOOT_LEAST 1.5f   /* and at least: lower rocks are the ones units are laid along the ground on */
#define LW_TILT_FOOT_BAND 1.5f    /* m from there to all of the game's reading, less where the reach is nearer */
#define LW_TILT_FOOT_EVEN 0.25f   /* m between the four raw readings of a unit that lies along its prop */
#define LW_TILT_FLAT_AGREE 10.0f  /* deg between the four faces under a unit for them to be one footing */
#define LW_TILT_FLAT_UP 35.0f     /* deg from upright a face may lie: a boulder's side is no footing */
#define LW_TILT_FLAT_BAND 5.0f    /* deg past either over which the share goes from all to none */
/* A corner's last hit. KEPT: a prop whose hit stayed the game's (no ground read for it): the unit is on a prop, and
 * there is no height to count. */
enum { LW_FOOT_NONE, LW_FOOT_GROUND, LW_FOOT_PROP, LW_FOOT_MISS, LW_FOOT_KEPT };
typedef struct LwTiltFoot { float h, d, n[3]; uint32_t body, at; uint8_t kind; } LwTiltFoot;
typedef struct LwTiltFeet { float x, y; uint32_t id; LwTiltFoot c[4]; } LwTiltFeet;
static LwTiltFeet g_lwTiltFoot[LW_TILT_FOOT_UNITS];
static volatile LONG g_lwTiltFooting = LW_TILT_FOOTING;
#ifdef CO_TEST
static uint32_t g_lwTiltTestNow;
#endif

static uint32_t LwTiltFootNow(void)
{
#ifdef CO_TEST
    if (g_lwTiltTestNow) return g_lwTiltTestNow;
#endif
    return (uint32_t)GetTickCount64();
}

/* The entity in the rotation's slot i ([mgr+0x50] instances, +8 its id); 0 when it does not read. */
static uint32_t LwTiltSlotId(uintptr_t mgr, uint32_t i)
{
    uintptr_t inst = 0, e = 0; uint32_t id = 0;
    return EvpPtr(mgr + 0x50, &inst) && EvpPtr(inst + 8ull * i, &e) && !SafeRead(e + 8, &id, 4) ? id : 0;
}

/* Unit i's corner (frame & 3) met this: a prop h over the drawn ground with the game's reading d, the ground, or
 * nothing. Another entity in the slot, or a ray far from the slot's last one, is another unit: its corners start
 * again, so it never stands on what a dead neighbour stood on. */
static void LwTiltFootNote(uint32_t i, uint32_t id, uint32_t frame, uint8_t kind, float h, float d, uint32_t body, const float* n, const float* o, uint32_t now)
{
    if (i >= LW_TILT_FOOT_UNITS) return;
    LwTiltFeet* f = &g_lwTiltFoot[i];
    if (id != f->id || fabsf(o[0] - f->x) > LW_TILT_FOOT_FAR || fabsf(o[1] - f->y) > LW_TILT_FOOT_FAR) memset(f->c, 0, sizeof f->c);
    f->x = o[0]; f->y = o[1]; f->id = id;
    f->c[frame & 3] = (LwTiltFoot){h > 0 ? h : 0, d, {n ? n[0] : 0, n ? n[1] : 0, n ? n[2] : 0}, body, now, kind};
}

static float LwTiltSmooth(float x) { x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; return x * x * (3.0f - 2.0f * x); }

/* The share of the game's reading for corners `mean` m over the drawn ground on average, by the unit's ray length:
 * none up to `low`, all of it from *top. The reach is the length less the ray's 0.5 m head start; past it the ground
 * reads as a hole anyway. Lengths in the units' data are 2, 3, 4, 10 and 15 m: a short unit has no band (it stands upright from
 * its reach), a long one's ends 1.5 m over its start, not at a reach of 14.5 m. */
static float LwTiltFootShare(float mean, float len, float* top)
{
    const float reach = len - 0.5f, low = fminf(LW_TILT_FOOT_LOW, fmaxf(LW_TILT_FOOT_LEAST, 0.6f * reach));
    *top = fminf(reach, low + LW_TILT_FOOT_BAND);
    return *top - low > 0.01f ? LwTiltSmooth((mean - low) / (*top - low)) : 0.0f;
}

/* By height: the share of the game's reading for unit i, from the mean height of its corners' hits over the drawn
 * ground (a corner on the ground or over a hole counts 0). A corner counts at most the band's top, so one corner
 * over a pit in the ground hidden under a rock cannot stand for the other three. A unit half on a tall rock stays
 * with the ground. */
static float LwTiltFootHigh(uint32_t i, float len, uint32_t now)
{
    float sum = 0, top = 0; int n = 0;
    if (i >= LW_TILT_FOOT_UNITS) return 0.0f;
    LwTiltFootShare(0, len, &top);
    for (int k = 0; k < 4; k++)
    {
        const LwTiltFoot* c = &g_lwTiltFoot[i].c[k];
        if (c->kind == LW_FOOT_NONE || c->kind == LW_FOOT_KEPT || now - c->at > LW_TILT_FOOT_FRESH) continue;
        n++;
        if (c->kind == LW_FOOT_PROP) sum += c->h > top ? top : c->h;
    }
    return n ? LwTiltFootShare(sum / (float)n, len, &top) : 0.0f;
}

/* Whether unit i has a corner on a prop now: its lean is then held without the ground's slope (the ground hidden
 * under a prop is broken, slopes to 79 deg were read there, and would lift the limit to the game's own). */
static int LwTiltFootOnProp(uint32_t i, uint32_t now)
{
    if (i >= LW_TILT_FOOT_UNITS) return 0;
    for (int k = 0; k < 4; k++)
        if ((g_lwTiltFoot[i].c[k].kind == LW_FOOT_PROP || g_lwTiltFoot[i].c[k].kind == LW_FOOT_KEPT) && now - g_lwTiltFoot[i].c[k].at <= LW_TILT_FOOT_FRESH) return 1;
    return 0;
}

/* By normals: the share of the game's reading for four faces under a unit that agree and lie near level, as a ramp,
 * a floor or a flat rock top do: all of it within 10 deg of each other and 35 of upright, none from 15 and 40. The
 * faces do not depend on the unit's pose, so the share cannot hunt with it. */
static float LwTiltFlatShare(float n[4][3])
{
    float apart = 0, lean = 0;
    for (int k = 0; k < 4; k++)
    {
        if (!(fabsf(n[k][0] * n[k][0] + n[k][1] * n[k][1] + n[k][2] * n[k][2] - 1.0f) < 0.02f)) return 0.0f;
        lean = fmaxf(lean, LwTiltDeg(n[k][2]));
        for (int j = 0; j < k; j++) apart = fmaxf(apart, LwTiltAngle(n[k], n[j]));
    }
    return (1.0f - LwTiltSmooth((apart - LW_TILT_FLAT_AGREE) / LW_TILT_FLAT_BAND)) * (1.0f - LwTiltSmooth((lean - LW_TILT_FLAT_UP) / LW_TILT_FLAT_BAND));
}

/* That share for unit i: only when all four corners met a prop now. */
static float LwTiltFootFlat(uint32_t i, uint32_t now)
{
    if (i >= LW_TILT_FOOT_UNITS) return 0.0f;
    const LwTiltFoot* c = g_lwTiltFoot[i].c; float n[4][3];
    for (int k = 0; k < 4; k++)
    {
        if ((c[k].kind != LW_FOOT_PROP && c[k].kind != LW_FOOT_KEPT) || now - c[k].at > LW_TILT_FOOT_FRESH) return 0.0f;
        memcpy(n[k], c[k].n, 12);
    }
    return LwTiltFlatShare(n);
}

/* One body: the share of the game's reading for unit i when all four corners met the same body and the game's four
 * readings are alike, as they are for a unit that lies along what it stands on: all of it within 0.25 m, none from
 * 0.5 m. */
static float LwTiltFootSame(uint32_t i, uint32_t now)
{
    if (i >= LW_TILT_FOOT_UNITS) return 0.0f;
    const LwTiltFoot* c = g_lwTiltFoot[i].c;
    float lo = c[0].d, hi = c[0].d;
    for (int k = 0; k < 4; k++)
    {
        if (c[k].kind != LW_FOOT_PROP || now - c[k].at > LW_TILT_FOOT_FRESH || c[k].body != c[0].body) return 0.0f;
        if (c[k].d < lo) lo = c[k].d;
        if (c[k].d > hi) hi = c[k].d;
    }
    return 1.0f - LwTiltSmooth((hi - lo - LW_TILT_FOOT_EVEN) / LW_TILT_FOOT_EVEN);
}

/* Unit i's reading d of the ray record rec that started at o and hit; level: the level terrain's hit under o where
 * no tile is live, else NULL. */
static float LwTiltOverProp(uintptr_t mgr, uint32_t i, uintptr_t rec, const float* o, float d, const LwTiltLevel* level)
{
    uintptr_t hit = 0, recs = 0; uint8_t h[0x24]; uint32_t ids[2], terrain = 0, mode = 0, frame = 0; float dir[3], at[3], face[3], len = 0, t = 0;
    if (!g_lwTiltPropsOn || !LwLive() || !EvpPtr(rec + 8, &hit) || SafeRead(hit, h, sizeof h) || SafeRead(mgr + 0x18, &terrain, 4) ||
        !EvpPtr(mgr + 0x58, &recs) || SafeRead(recs + 0x298ull * i + 0x54, &mode, 4) || mode != 4 ||
        SafeRead(recs + 0x298ull * i + 0x48, &len, 4) || SafeRead(rec + 0x1C, dir, sizeof dir) || SafeRead(mgr, &frame, 4)) return d;
    memcpy(at, h, sizeof at); memcpy(face, h + 0xC, sizeof face); memcpy(ids, h + 0x1C, sizeof ids);
    const uint32_t now = LwTiltFootNow(), id = LwTiltSlotId(mgr, i);
    if (!LwTiltIsProp(ids[0], ids[1], terrain)) { LwTiltFootNote(i, id, frame, LW_FOOT_GROUND, 0, d, ids[1], face, o, now); return d; }
    int why = LwTiltGroundAlong(o, dir, &t);
    float under = why ? -9999.0f : LwTiltTileGround(at[0], at[1]); /* the hit's own place (hit +0): d is not its distance under the level terrain */
    if (!why && !(under > -9000.0f)) why = LW_KEPT_TILE;
    if (why == LW_KEPT_TILE && level != NULL && LwTiltLevelAlong(level, o, dir, &t))
    {
        under = LwTiltLevelGround(level, o, at[0], at[1]); why = 0;
        InterlockedIncrement(&g_lwTiltPropLevel);
    }
    if (why) { InterlockedIncrement(&g_lwTiltPropKept[why]); LwTiltFootNote(i, id, frame, LW_FOOT_KEPT, 0, d, ids[1], face, o, now); return d; }
    LwTiltFootNote(i, id, frame, LW_FOOT_PROP, at[2] - under, d, ids[1], face, o, now);
    const float plain = LwTiltPropRead(t, len);
    const LONG footing = g_lwTiltFooting;
    const float w = footing == 1 ? LwTiltFootSame(i, now) : footing == 2 ? LwTiltFootHigh(i, len, now) :
        footing == 3 ? fmaxf(LwTiltFootHigh(i, len, now), LwTiltFootFlat(i, now)) : 0.0f;
    InterlockedIncrement(w >= 1.0f ? &g_lwTiltPropFooted : w > 0.0f ? &g_lwTiltPropPart : &g_lwTiltPropGround);
    return w >= 1.0f ? d : w > 0.0f ? plain + (d - plain) * w : plain;
}

static float LwTiltRead(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse)
{
    float o[3]; uint32_t count = 1, frame = 0;
    const int have = request != 0 && !SafeRead(rays + 0x40ull * (request - 1) + 0x10, o, sizeof o);
    LwLeanAt(have ? o : NULL);
#ifdef CO_RESEARCH
    uintptr_t hit = 0; uint32_t flags = 0; /* before the game's call, which casts back from inside a body (0x620852) */
    if (have && EvpPtr(rays + 0x40ull * (request - 1) + 8, &hit) && !SafeRead(hit + 0x28, &flags, 4) && (flags & 1)) InterlockedIncrement(&g_lwTiltInside);
#endif
    float d = g_lwTiltRead(mgr, i, rays, request, coarse);
    if (!have || !MoveTilesOn() || SafeRead(rays + 0x40ull * (request - 1) + 0x30, &count, 4)) return d;
    LwTiltLevel level; uint32_t terrain = 0xFFFFFFFFu;
    const int coarseGround = !SafeRead(mgr + 0x18, &terrain, 4) && LwTiltTileCovers(o[0], o[1]) == 0 && LwTiltLevelHit(rays, coarse, terrain, o, &level);
    if (coarseGround) LwLeanLevel(LwTiltDeg(level.n[2])); /* the slope a unit off every tile is held to */
    if (count != 0)
    {
#if defined(CO_RESEARCH) || defined(CO_TEST)
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
#endif
        d = LwTiltOverProp(mgr, i, rays + 0x40ull * (request - 1), o, d, coarseGround ? &level : NULL);
#if defined(CO_RESEARCH) || defined(CO_TEST)
        QueryPerformanceCounter(&b);
        LwTiltCost(b.QuadPart - a.QuadPart);
#endif
        LwLeanOnProp(LwTiltFootOnProp(i, LwTiltFootNow()));
        return d;
    }
    if (!SafeRead(mgr, &frame, 4)) LwTiltFootNote(i, LwTiltSlotId(mgr, i), frame, LW_FOOT_MISS, 0, d, 0, NULL, o, LwTiltFootNow()); /* a corner over a hole stands on nothing */
    LwLeanOnProp(LwTiltFootOnProp(i, LwTiltFootNow()));
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
    char col[512]; uint32_t unit = 0; const uint64_t res = EvpEntityResource(g_lw.game, id, &unit);
    LwTiltColumn(mgr, p[0], p[1], p[2], col, sizeof col);
    Note("living world: tilt: unit %08X (resource %016llX, entity resource %016llX) mode %u at (%.1f, %.1f, %.2f): %.0f deg, its ground %.0f deg, %.0f deg off its ground; up (%.2f, %.2f, %.2f); R+0x48 %.2f, +0x4C %.2f, +0x50 %.2f, +0x28C %08X; under it: %s",
        id, (unsigned long long)LwTiltUnitName(unit), (unsigned long long)res, mode, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)ground, (double)off, (double)up[0], (double)up[1], (double)up[2], (double)f[0], (double)f[1], (double)f[2], hitId, col);
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

/* 1 when this call scanned. */
static int LwTiltScanBody(void)
{
    ULONGLONG now = GetTickCount64();
#if defined(CO_RESEARCH) || defined(CO_TEST)
    g_lwTiltCost.frames++;
#endif
    if (now - g_lwTilt.at < 500) return 0;
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
        LwLpLevel(); g_lwTiltSunkLines = g_lwTiltTallLines = 0; g_lwTiltKind.n = 0;
#endif
    }
    uintptr_t mgr = 0, inst = 0, recs = 0, ctl = 0, units = 0, urecs = 0;
    if (!EvpPtr(g_lw.game + LW_TILT_ROT, &mgr) || !EvpPtr(mgr + 0x50, &inst) || !EvpPtr(mgr + 0x58, &recs) || !EvpPtr(mgr + 0x60, &ctl) ||
        !EvpPtr(g_lw.game + LW_TILT_UNITS, &units) || !EvpPtr(units + 0x68, &urecs)) return 0;
    uint32_t n = EvpU32(mgr + 0x2C, 0);
    if (n > 1024) n = 1024;
    g_lwTilt.scans++;
#ifdef CO_RESEARCH
    LwLpScan(); g_lwTiltTurn.scan++;
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
        LwTiltKind(id, mode);
        if (mode == 4) LwTiltPropSample(mgr, R, i, id, q, p, LwTiltDeg(up[2]));
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
    return 1;
}

static void LwTiltScan(void)
{
#ifdef CO_RESEARCH
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    if (!LwTiltScanBody()) return;
    QueryPerformanceCounter(&b);
    LwTiltScanCost(b.QuadPart - a.QuadPart);
#else
    LwTiltScanBody();
#endif
}

/* With the 30 s notes: the units scanned by mode and those off their ground; the counts start again. */
static void LwTiltNote(void)
{
    LONG reads = InterlockedExchange(&g_lwTiltReads, 0), under = InterlockedExchange(&g_lwTiltUnder, 0);
    if (reads) Note("living world: tilt rays (30 s): %ld missed on detailed tiles, %ld of them started under the ground and read it above them", (long)reads, (long)under);
    const LONG propGround = InterlockedExchange(&g_lwTiltPropGround, 0), footed = InterlockedExchange(&g_lwTiltPropFooted, 0), part = InterlockedExchange(&g_lwTiltPropPart, 0),
        flat = InterlockedExchange(&g_lwTiltPropKept[LW_KEPT_FLAT], 0), tile = InterlockedExchange(&g_lwTiltPropKept[LW_KEPT_TILE], 0), swing = InterlockedExchange(&g_lwTiltPropKept[LW_KEPT_SWING], 0),
        level = InterlockedExchange(&g_lwTiltPropLevel, 0);
    if (propGround || footed || part || flat || tile || swing)
        Note("living world: tilt rays (30 s): %ld four-corner hits on a prop read the drawn ground, %ld kept the game's reading for a footing on the prop and %ld part of it (footing %ld); "
            "%ld stayed the game's: %ld with no live tile there, %ld on a ray over 60 deg from straight down, %ld where the length to the ground did not settle; "
            "of the hits answered %ld read the level's coarse terrain, where no detailed tile is live",
            (long)propGround, (long)footed, (long)part, (long)g_lwTiltFooting, (long)(flat + tile + swing), (long)tile, (long)flat, (long)swing, (long)level);
#ifdef CO_RESEARCH
    LARGE_INTEGER perSecond;
    QueryPerformanceFrequency(&perSecond);
    LwTiltCostNote(perSecond.QuadPart); LwTiltScanCostNote(perSecond.QuadPart);
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

/* On a client the rules run as on the host (neither hook asks who hosts), but the host's tick with their 30 s
 * lines stops before it: the same counts here, behind a line that says whose they are. No scan: the host's units. */
static void LwTiltClient(void)
{
    static ULONGLONG noted;
    const ULONGLONG now = GetTickCount64();
    if (now - noted < 30000) return;
    noted = now;
    Note("living world: tilt: on a client: the corner and lean rules run here on the units this game poses; their counts follow, or none when it posed none");
    LwTiltNote(); LwLeanNote();
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
void CoXlTestTiltPropSample(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, float fromUpright) { LwTiltPropSample(mgr, R, 5, id, q, p, fromUpright); }
int CoXlTestTiltInsideAt(uintptr_t game, uintptr_t mgr, const float* p, float* depth) { g_lw.game = game; return LwTiltInsideAt(mgr, p, depth); }
void CoXlTestTiltHeldTurn(uint32_t id, const float* up, int held) { LwTiltHeldTurn(id, up, held); }
void CoXlTestTiltTurnScan(void) { g_lwTiltTurn.scan++; }
void CoXlTestTiltKind(uint32_t id, uint32_t mode) { LwTiltKind(id, mode); }
void CoXlTestTiltFooting(int mode) { InterlockedExchange(&g_lwTiltFooting, mode); }
int CoXlTestTiltFootingNow(void) { return (int)g_lwTiltFooting; }
void CoXlTestTiltNow(uint32_t ms) { g_lwTiltTestNow = ms; }
void CoXlTestTiltFeetClear(void) { memset(g_lwTiltFoot, 0, sizeof g_lwTiltFoot); }
float CoXlTestTiltFootHigh(uint32_t i, float len, uint32_t now) { return LwTiltFootHigh(i, len, now); }
float CoXlTestTiltFootSame(uint32_t i, uint32_t now) { return LwTiltFootSame(i, now); }
void CoXlTestTiltFootNote(uint32_t i, uint32_t frame, int kind, float h, float d, uint32_t body, const float* o, uint32_t now) { LwTiltFootNote(i, 0, frame, (uint8_t)kind, h, d, body, NULL, o, now); }
void CoXlTestTiltFootNoteOf(uint32_t i, uint32_t id, uint32_t frame, int kind, float h, const float* o, uint32_t now) { LwTiltFootNote(i, id, frame, (uint8_t)kind, h, 0.5f, 7, NULL, o, now); }
void CoXlTestTiltFootFace(uint32_t i, uint32_t frame, int kind, const float* n, const float* o, uint32_t now) { LwTiltFootNote(i, 0, frame, (uint8_t)kind, 1.0f, 0.5f, 7, n, o, now); }
float CoXlTestTiltFootFlat(uint32_t i, uint32_t now) { return LwTiltFootFlat(i, now); }
int CoXlTestTiltTileCovers(float x, float y) { return LwTiltTileCovers(x, y); }
int CoXlTestTiltLevelHit(uintptr_t rays, uint32_t coarse, uint32_t terrain, const float* o, float* zn) { LwTiltLevel c = {0, {0, 0, 0}}; const int ok = LwTiltLevelHit(rays, coarse, terrain, o, &c); zn[0] = c.z; memcpy(zn + 1, c.n, 12); return ok; }
int CoXlTestTiltLevelAlong(const float* zn, const float* o, const float* dir, float* t) { LwTiltLevel c; c.z = zn[0]; memcpy(c.n, zn + 1, 12); return LwTiltLevelAlong(&c, o, dir, t); }
float CoXlTestTiltLevelGround(const float* zn, const float* o, float x, float y) { LwTiltLevel c; c.z = zn[0]; memcpy(c.n, zn + 1, 12); return LwTiltLevelGround(&c, o, x, y); }
long CoXlTestTiltLevelCount(void) { return InterlockedExchange(&g_lwTiltPropLevel, 0); }
int CoXlTestTiltFootOnProp(uint32_t i, uint32_t now) { return LwTiltFootOnProp(i, now); }
float CoXlTestTiltFootShare(float mean, float len, float* top) { return LwTiltFootShare(mean, len, top); }
void CoXlTestTiltScanCost(long long ticks) { LwTiltScanCost(ticks); }
void CoXlTestTiltScanCostNote(long long perSecond) { LwTiltScanCostNote(perSecond); }
int CoXlTestTiltTallLines(void) { return g_lwTiltTallLines; }
void CoXlTestTiltKept(long* by) { for (int k = 1; k < LW_KEPT_KINDS; k++) by[k - 1] = g_lwTiltPropKept[k]; }
void CoXlTestTiltFooted(long* footed, long* part) { *footed = InterlockedExchange(&g_lwTiltPropFooted, 0); *part = InterlockedExchange(&g_lwTiltPropPart, 0); }
void CoXlTestTiltSunkLines(int n) { g_lwTiltSunkLines = n; g_lwTiltTallLines = 0; }
int CoXlTestTiltSunkLinesNow(void) { return g_lwTiltSunkLines; }
void CoXlTestTiltCost(long long ticks) { LwTiltCost(ticks); }
void CoXlTestTiltCostNote(long long perSecond, long frames) { g_lwTiltCost.frames = frames; LwTiltCostNote(perSecond); }
long CoXlTestTiltCostCalls(void) { return g_lwTiltCost.calls; }
void CoXlTestTiltPropNote(void) { LwTiltPropNote(); }
int CoXlTestTiltPropsOn(void) { return g_lwTiltPropsOn != 0; }
void CoXlTestTiltNote(void) { LwTiltNote(); }
void CoXlTestTiltClient(void) { LwTiltClient(); }
float CoXlTestTiltPropRead(float t, float len) { return LwTiltPropRead(t, len); }
int CoXlTestTiltGroundAlong(const float* o, const float* dir, float* t) { return LwTiltGroundAlong(o, dir, t) == 0; }
int CoXlTestTiltWhy(const float* o, const float* dir) { float t; return LwTiltGroundAlong(o, dir, &t); }
void CoXlTestTiltProps(int on) { InterlockedExchange(&g_lwTiltPropsOn, on); }
void CoXlTestTiltPropCounts(long* ground, long* kept)
{
    *ground = InterlockedExchange(&g_lwTiltPropGround, 0); *kept = 0;
    for (int k = 1; k < LW_KEPT_KINDS; k++) *kept += InterlockedExchange(&g_lwTiltPropKept[k], 0);
}
void CoXlTestTiltCounts(long* reads, long* under) { *reads = InterlockedExchange(&g_lwTiltReads, 0); *under = InterlockedExchange(&g_lwTiltUnder, 0); }
#endif
