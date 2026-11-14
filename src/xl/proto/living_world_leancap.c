/* On complex levels a four-corner unit leans at most its ground's slope and LW_LEAN_CAP degrees from upright (issue
 * G). The rotation's four-corner pose (mode 4, 0x6226F1) turns a unit until its corner rays read alike, so it lies
 * along whatever they meet, rock faces too. The game then limits the up it found: 0x624750(mgr, out, up) under 0.7 from
 * the world's up (0x6247CA) gives that up turned 45.57 deg toward it (0x6248CE). The limit is hooked, first in the
 * chain so its return address is the game's caller, and what it leaves is tightened for the four-corner call (back to
 * 0x622D70) only; the bone-ray modes' call (0x621EAF) and every other level keep the game's. The pose reads one corner
 * ray on the same thread just before (0x6227A8): its start is where the unit stands. The world update calls the
 * rotation update on the game thread (0x573E16); the slot is still the thread's own, the counts atomic and the tiles
 * tried for, never waited on, so a build that hands the update to its jobs (0x571100) stays safe.
 * Research builds: HELLFORGE_XL_LEAN_CAP=<degrees> over the slope, 0 none; xl-lean.txt in the logs changes it in play. */
#define LW_LEAN_LIMIT 0x624750u
#define LW_LEAN_FOUR 0x622D70u    /* where the four-corner pose's call returns */
#define LW_LEAN_BONE 0x621EB4u    /* and the bone-ray modes' */
#define LW_LEAN_UP 0x328CED0u     /* the world's up, three floats */
#define LW_LEAN_CAP 25.0f
#define LW_LEAN_GAME 0.7f         /* the game's cos, for the count only: its own constant is never written */
#define LW_LEAN_GAME_DEG 45.573f

typedef float* (*LwLeanFn)(uintptr_t mgr, float* out, float* up);
static struct
{
    LwLeanFn orig; uintptr_t four, bone; const float* world;
    volatile LONG deg; /* a float's bits: research builds change it in play */
    volatile LONG poses, game, ours, blind, boneCalls, otherCalls;
} g_lwLean;
static _Thread_local struct { float x, y; int set; } t_lwLean;

static float LwLeanDeg(void) { LONG b = g_lwLean.deg; float f; memcpy(&f, &b, 4); return f; }
static float LwLeanDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* From the ray reader's hook: the corner ray this thread's unit just read starts here; NULL when it did not read. */
static void LwLeanAt(const float* origin)
{
    t_lwLean.set = origin != NULL;
    if (origin != NULL) { t_lwLean.x = origin[0]; t_lwLean.y = origin[1]; }
}

/* The degrees over the slope: 0 is none, and one at or past the game's own limit would never act. */
static void LwLeanSet(float deg)
{
    if (!(deg > 0.0f)) deg = 0.0f;
    else if (deg < 5.0f) deg = 5.0f;
    else if (deg > 45.0f) deg = 45.0f;
    LONG b; memcpy(&b, &deg, 4);
    InterlockedExchange(&g_lwLean.deg, b);
}

/* The drawn ground's slope at (x, y) in degrees, from a live tile's heights 1 m either side. 0 when the tiles are
 * off, a writer holds them, or no live tile reads there: the caller then leaves the game's answer. */
static int LwLeanSlope(float x, float y, float* deg)
{
    int ok = 0;
    if (!MoveTilesOn() || !TryAcquireSRWLockShared(&g_tilesLock)) return 0;
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].ground.v != NULL &&
            fabsf(x - g_tiles[k].centre[0]) < NEAR_HIDE / 2 && fabsf(y - g_tiles[k].centre[1]) < NEAR_HIDE / 2)
        {
            const NearGround* g = &g_tiles[k].ground;
            const float e = NearGroundAt(g, x + 1, y), w = NearGroundAt(g, x - 1, y), n = NearGroundAt(g, x, y + 1), s = NearGroundAt(g, x, y - 1);
            if (e > -9000.0f && w > -9000.0f && n > -9000.0f && s > -9000.0f) { *deg = atanf(hypotf((e - w) / 2.0f, (n - s) / 2.0f)) * 57.29578f; ok = 1; }
            break;
        }
    ReleaseSRWLockShared(&g_tilesLock);
    return ok;
}

/* The most a unit may lean on ground of this slope. */
static float LwLeanAllowed(float slope, float over) { return slope + over < LW_LEAN_GAME_DEG ? slope + over : LW_LEAN_GAME_DEG; }

/* up held within `deg` of w (a unit vector): w turned that far toward up, as the game's own limit does. 1 when it
 * changed. An up that is not a unit vector is left (the game's answer to no up at all is 0.7 of the world's, 0x622D43),
 * and so is one with no side to lean to. */
static int LwLeanHold(float* up, const float* w, float deg)
{
    const float n = sqrtf(LwLeanDot(up, up)), d = LwLeanDot(up, w), c = cosf(deg / 57.29578f);
    if (!(fabsf(n - 1.0f) < 0.01f) || !(d < c)) return 0;
    float h[3] = {up[0] - d * w[0], up[1] - d * w[1], up[2] - d * w[2]};
    const float l = sqrtf(LwLeanDot(h, h)), s = sinf(deg / 57.29578f);
    if (!(l > 1e-4f)) return 0;
    for (int k = 0; k < 3; k++) up[k] = w[k] * c + h[k] / l * s;
    return 1;
}

static float* LwLeanLimit(uintptr_t mgr, float* out, float* up)
{
    const uintptr_t back = (uintptr_t)__builtin_return_address(0);
    const int four = back == g_lwLean.four, set = t_lwLean.set;
    t_lwLean.set = 0; /* one pose, one reading: never another unit's */
    if (!four) InterlockedIncrement(back == g_lwLean.bone ? &g_lwLean.boneCalls : &g_lwLean.otherCalls);
    else
    {
        InterlockedIncrement(&g_lwLean.poses);
        if (LwLeanDot(up, g_lwLean.world) < LW_LEAN_GAME) InterlockedIncrement(&g_lwLean.game);
    }
    const float deg = LwLeanDeg(); /* read once: a change while this pose is under way never gives it two values */
    float* held = g_lwLean.orig(mgr, out, up);
    float slope;
    if (!four || deg == 0.0f || !LwLive()) return held;
    if (!set || !LwLeanSlope(t_lwLean.x, t_lwLean.y, &slope)) { InterlockedIncrement(&g_lwLean.blind); return held; }
    /* the game writes its answer to both, and its caller reads the one returned (0x622D7A) */
    if (LwLeanHold(out, g_lwLean.world, LwLeanAllowed(slope, deg)))
    {
        memcpy(up, out, 12);
        InterlockedIncrement(&g_lwLean.ours);
    }
    return held;
}

static void LwLeanNote(void)
{
    if (g_lwLean.orig == NULL) return;
    const LONG poses = InterlockedExchange(&g_lwLean.poses, 0), game = InterlockedExchange(&g_lwLean.game, 0), ours = InterlockedExchange(&g_lwLean.ours, 0),
        blind = InterlockedExchange(&g_lwLean.blind, 0), bone = InterlockedExchange(&g_lwLean.boneCalls, 0), other = InterlockedExchange(&g_lwLean.otherCalls, 0);
    if (poses || bone || other)
        Note("living world: lean (30 s): %ld four-corner poses, %ld held at the game's 45.6 deg, %ld at their ground's slope and %.0f deg (0: none), "
            "%ld left with no drawn ground read; the limit's other calls: %ld bone-ray, %ld from elsewhere",
            (long)poses, (long)game, (long)ours, (double)(LwLive() ? LwLeanDeg() : 0.0f), (long)blind, (long)bone, (long)other);
}

/* The pins: the limit's entry, its test against 0.7 and that float, the one reference to the angle's double and the
 * double, where it writes its answer and returns it, its two callers, the four-corner one's read of the answer, the
 * read of the corner ray before it, and the mode table's entries that lead to them. */
static int g_lwLeanDone;
static void LwInstallLeanCap(void)
{
    if (g_lwLeanDone++) return;
    uintptr_t g = g_lw.game;
    LwLeanSet(LW_LEAN_CAP);
#ifdef CO_RESEARCH
    char t[16] = "";
    if (GetEnvironmentVariableA("HELLFORGE_XL_LEAN_CAP", t, sizeof t) > 0) LwLeanSet((float)atof(t));
#endif
    if (!ApiCodeMatches(g + LW_LEAN_LIMIT, "48 8B C4 48 89 58 08 57 48 81 EC B0 00 00 00 F3 41 0F 10 58 04 49 8B D8") ||
        !ApiCodeMatches(g + 0x6247CAu, "F3 0F 10 05 06 24 DA 01 F3 0F 58 D1 0F 2F C2") || !ApiCodeMatches(g + 0x23C6BD8u, "33 33 33 3F") ||
        !ApiCodeMatches(g + 0x6248CEu, "F2 0F 10 05 4A 27 DA 01") || !ApiCodeMatches(g + 0x23C7020u, "00 00 00 60 66 66 E6 3F") ||
        !ApiCodeMatches(g + 0x6249AFu, "F3 44 0F 11 0B F3 0F 11 6B 04 F3 0F 11 53 08") ||
        !ApiCodeMatches(g + 0x6249DDu, "F2 0F 10 03 4C 8D 9C 24 B0 00 00 00 8B 4B 08 48 8B C7") ||
        !ApiCodeMatches(g + 0x622D6Bu, "E8 E0 19 00 00") || !ApiCodeMatches(g + 0x621EAFu, "E8 9C 28 00 00") ||
        !ApiCodeMatches(g + 0x622D7Au, "F2 44 0F 10 00 8B 58 08") || !ApiCodeMatches(g + 0x6227A8u, "E8 63 E0 FF FF") ||
        !ApiCodeMatches(g + 0x624738u, "DD 14 62 00 DD 14 62 00 F1 26 62 00"))
    { Note("living world: the rotation's lean limit is not the reviewed code; four-corner units keep the game's 45.6 deg"); return; }
    g_lwLean.four = g + LW_LEAN_FOUR; g_lwLean.bone = g + LW_LEAN_BONE; g_lwLean.world = (const float*)(g + LW_LEAN_UP);
    const int old = g_coApi->size < offsetof(HfApi, hook_ex) + sizeof(void*) || g_coApi->hook_ex == NULL;
    if ((old ? LoaderHook("living world: four-corner units' lean", g + LW_LEAN_LIMIT, (void*)LwLeanLimit, (void**)&g_lwLean.orig)
             : g_coApi->hook_ex(g_coMod, g + LW_LEAN_LIMIT, (void*)LwLeanLimit, (void**)&g_lwLean.orig, 0x7FFFFFFF)) || g_lwLean.orig == NULL)
    { g_lwLean.orig = NULL; Note("living world: the rotation's lean limit could not be hooked; four-corner units keep the game's 45.6 deg"); return; }
    Note("living world: four-corner units lean at most their ground's slope and %.0f deg from upright on complex levels (0: the game's 45.6 deg), the game's 45.6 deg on any other",
        (double)LwLeanDeg());
}

#if defined(CO_RESEARCH) || defined(CO_TEST)
/* Research, from the tilt scan (game thread, twice a second): how the limit holds, and whether units at it jitter. A
 * switch of 0 measures against the default, so a control run counts the same units. */
#define LW_LP_UNITS 256
#define LW_LP_LINES 16
#define LW_LP_STEEP 48  /* lines a level (and a value of xl-lean.txt) for samples 30 deg off ground of 30 deg */
typedef struct LwLpUnit { uint32_t id, scan; float up[3]; uint8_t kind[4]; ULONGLONG lineAt; } LwLpUnit;
static struct
{
    uint32_t scan, samples, over, at, off30, off30Steep, pairs, flips; float turnSum, turnMax; int lines, steepLines;
    LwLpUnit units[LW_LP_UNITS];
} g_lwLp;

static void LwLpLevel(void) { memset(&g_lwLp, 0, sizeof g_lwLp); }
static void LwLpScan(void) { g_lwLp.scan++; }
static float LwLpOver(void) { return LwLeanDeg() > 0.0f ? LwLeanDeg() : LW_LEAN_CAP; }

/* A corner's reading by kind: 0 a hit, 1 under a surface, 2 a miss (the full length). */
static uint8_t LwLpKind(float d, float len) { return d < 0.0f ? 1 : fabsf(d - len) < 0.001f ? 2 : 0; }

/* The unit's slot: its own, else a free or stale one among eight from its hash; NULL when all are in use. */
static LwLpUnit* LwLpSlot(uint32_t id)
{
    LwLpUnit* spare = NULL;
    for (uint32_t k = 0; k < 8; k++)
    {
        LwLpUnit* u = &g_lwLp.units[(id * 2654435761u + k) % LW_LP_UNITS];
        if (u->id == id) return u;
        if (spare == NULL && (u->id == 0 || u->scan + 1 < g_lwLp.scan)) spare = u;
    }
    if (spare != NULL) { memset(spare, 0, sizeof *spare); spare->id = id; }
    return spare;
}

/* One four-corner unit of this scan: tilt from upright, its ground's slope, the angle between the two, its corner
 * readings. The unit's slot when it is at its limit (within 1 deg under it, or past it), else NULL. */
static LwLpUnit* LwLpSample(uint32_t id, const float* up, float tilt, float ground, float off, const float* last, float len)
{
    const float allowed = LwLeanAllowed(ground, LwLpOver());
    g_lwLp.samples++;
    if (tilt > allowed + 2.0f) g_lwLp.over++;
    if (off >= 30.0f) { g_lwLp.off30++; if (ground >= 30.0f) g_lwLp.off30Steep++; }
    LwLpUnit* u = LwLpSlot(id);
    if (u == NULL) return NULL;
    if (tilt < allowed - 1.0f) return NULL;
    g_lwLp.at++;
    if (u->scan != 0 && u->scan + 1 == g_lwLp.scan)
    {
        float c = LwLeanDot(up, u->up), turn = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c) * 57.29578f;
        g_lwLp.pairs++; g_lwLp.turnSum += turn;
        if (turn > g_lwLp.turnMax) g_lwLp.turnMax = turn;
        for (int k = 0; k < 4; k++) if (LwLpKind(last[k], len) != u->kind[k]) g_lwLp.flips++;
    }
    u->scan = g_lwLp.scan; memcpy(u->up, up, 12);
    for (int k = 0; k < 4; k++) u->kind[k] = LwLpKind(last[k], len);
    return u;
}

static void LwLpNote(void)
{
    if (g_lwLp.samples)
        Note("living world: lean probe (30 s): %u four-corner samples on read ground, %u over their limit by 2 deg or more (ground slope + %.0f, at most 45.6), %u at it; "
            "30 deg or more off their ground %u, %u of them on ground of 30 deg or more; at their limit two scans running %u: up turned %.1f deg on average, %.1f at most, %u corner readings changed kind",
            g_lwLp.samples, g_lwLp.over, (double)LwLpOver(), g_lwLp.at, g_lwLp.off30, g_lwLp.off30Steep, g_lwLp.pairs,
            g_lwLp.pairs ? (double)(g_lwLp.turnSum / (float)g_lwLp.pairs) : 0.0, (double)g_lwLp.turnMax, g_lwLp.flips);
    g_lwLp.samples = g_lwLp.over = g_lwLp.at = g_lwLp.off30 = g_lwLp.off30Steep = g_lwLp.pairs = g_lwLp.flips = 0; g_lwLp.turnSum = g_lwLp.turnMax = 0;
}

/* The degrees from a file, read when it changes, so one mission measures the same units with and without the limit.
 * The 30 s lines are written first: no count spans two values. 1 when the degrees were taken. */
static int LwLeanWatch(const wchar_t* path)
{
    static FILETIME stamp;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a) || CompareFileTime(&a.ftLastWriteTime, &stamp) == 0) return 0;
    char text[32] = "", *end; DWORD got = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0; /* still being written: next time */
    stamp = a.ftLastWriteTime;
    ReadFile(f, text, sizeof text - 1, &got, NULL); CloseHandle(f);
    text[got] = 0;
    const float deg = strtof(text, &end);
    if (end == text || !(deg >= 0.0f)) { Note("living world: lean: xl-lean.txt \"%.20s\" is not degrees; still %.0f", text, (double)LwLeanDeg()); return 0; }
    char* rest = end; const long props = strtol(end, &rest, 10); /* a second number: 0 leaves prop hits the game's */
    char* more = rest; const long footing = strtol(rest, &more, 10); /* a third: how a footing on a prop is kept (0 to 2) */
    LwTiltNote(); LwLeanNote();
    LwLeanSet(deg);
    if (rest != end) InterlockedExchange(&g_lwTiltPropsOn, props != 0);
    if (more != rest) InterlockedExchange(&g_lwTiltFooting, footing >= 0 && footing <= 2 ? footing : LW_TILT_FOOTING);
    g_lwLp.lines = g_lwLp.steepLines = 0; g_lwTiltSunkLines = 0;
    Note("living world: lean: now %.0f deg over the slope (0: none), prop hits read %s, footing %ld (0: the ground under any prop, 1: one balanced body keeps the game's, "
        "2: by height), from xl-lean.txt", (double)LwLeanDeg(), g_lwTiltPropsOn ? "the drawn ground" : "as the game reads them", (long)g_lwTiltFooting);
    return 1;
}
#endif

#ifdef CO_RESEARCH
/* A unit at its limit, at most LW_LP_LINES a level and once in 20 s a unit: where it is (for a screenshot), what it
 * stands on, and each corner's height over the surface under it. */
static void LwLpUnitScan(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, const float* up, float tilt, float ground, float off, ULONGLONG now)
{
    float last[4] = {0, 0, 0, 0}, len = 0, z = 0;
    SafeRead(R + 0x48, &len, 4);
    for (int k = 0; k < 4; k++) SafeRead(R + 0x58 + 0x18u * k + 0x10, &last[k], 4);
    LwLpUnit* u = LwLpSample(id, up, tilt, ground, off, last, len);
    if (off >= 30.0f && ground >= 30.0f && g_lwLp.steepLines < LW_LP_STEEP)
    {
        float drawn = -1.0f; const int tile = LwLeanSlope(p[0], p[1], &drawn), under = LwTiltOnProp(mgr, p, &z);
        g_lwLp.steepLines++;
        Note("living world: lean probe: steep: unit %08X at (%.1f, %.1f, %.2f): %.1f deg from upright, %.1f off its ground; the ground's slope %.1f deg by the game's read, "
            "%.1f by the drawn tile (-1: none read); limit %.1f; under it %s; readings %.2f %.2f %.2f %.2f of %.2f",
            id, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)off, (double)ground, tile ? (double)drawn : -1.0,
            (double)LwLeanAllowed(tile ? drawn : ground, LwLpOver()), under < 0 ? "nothing" : under ? "a prop" : "the ground",
            (double)last[0], (double)last[1], (double)last[2], (double)last[3], (double)len);
    }
    if (u == NULL || g_lwLp.lines >= LW_LP_LINES || (u->lineAt != 0 && now - u->lineAt < 20000)) return;
    u->lineAt = now; g_lwLp.lines++;
    char corners[160] = ""; int at = 0;
    const float lx[3] = {1, 0, 0}, ly[3] = {0, 1, 0}; float ax[3], ay[3];
    LwTiltRotate(q, lx, ax); LwTiltRotate(q, ly, ay);
    for (int k = 0; k < 4 && at >= 0 && (size_t)at < sizeof corners; k++)
    {
        float o[2], c[3], cz;
        if (SafeRead(R + 0x58 + 0x18u * k + 8, o, sizeof o)) break;
        for (int j = 0; j < 3; j++) c[j] = p[j] + ax[j] * o[0] + ay[j] * o[1];
        const int on = LwTiltOnProp(mgr, c, &cz);
        if (on < 0) at += snprintf(corners + at, sizeof corners - (size_t)at, " none");
        else at += snprintf(corners + at, sizeof corners - (size_t)at, " %+.2f%s", (double)(c[2] - cz), on ? " (prop)" : "");
    }
    const int on = LwTiltOnProp(mgr, p, &z);
    Note("living world: lean probe: unit %08X at (%.1f, %.1f, %.2f): %.1f deg from upright, its ground %.1f deg, limit %.1f; under it %s; corners %s 3 m apart, their heights over what is under them (m):%s",
        id, (double)p[0], (double)p[1], (double)p[2], (double)tilt, (double)ground, (double)LwLeanAllowed(ground, LwLpOver()),
        on < 0 ? "nothing" : on ? "a prop" : "the ground", LwTiltLong(R) ? "over" : "under", corners);
}
#endif

#ifdef CO_TEST
static uint16_t g_lwLeanTestHeights[65 * 65];
/* A live tile about (cx, cy), 64 m across in 1 m steps, at z in its middle and rising gx east and gy north; moving
 * tiles on. */
void CoXlTestLeanTile(float cx, float cy, float z, float gx, float gy)
{
    memset(g_tiles, 0, sizeof g_tiles);
    for (int r = 0; r < 65; r++)
        for (int c = 0; c < 65; c++)
            g_lwLeanTestHeights[r * 65 + c] = (uint16_t)lroundf((z + gx * (float)(c - 32) + gy * (float)(32 - r) + 500.0f) / 1000.0f * 65535.0f);
    g_moveInstalled = 1; g_move.on = 1; g_move.level = g_world.levels;
    g_tiles[0].live = 1; g_tiles[0].level = g_world.levels; g_tiles[0].centre[0] = cx; g_tiles[0].centre[1] = cy;
    g_tiles[0].ground = (NearGround){g_lwLeanTestHeights, 65, 64.0f, cx, cy};
}
/* That tile moved to the second slot, behind a live flat one about (cx, cy). */
void CoXlTestLeanBehind(float cx, float cy)
{
    static uint16_t flat[9];
    g_tiles[1] = g_tiles[0];
    for (int k = 0; k < 9; k++) flat[k] = 32768;
    g_tiles[0].centre[0] = cx; g_tiles[0].centre[1] = cy;
    g_tiles[0].ground = (NearGround){flat, 3, NEAR_SPAN, cx, cy};
}
/* The moving tiles off with the tiles still listed. */
void CoXlTestLeanTilesOff(void) { g_move.on = 0; }
int CoXlTestLeanInstall(uintptr_t game) { g_lw.game = game; g_lwLeanDone = 0; memset(&g_lwLean, 0, sizeof g_lwLean); LwInstallLeanCap(); return g_lwLean.orig != NULL; }
void CoXlTestLeanSet(float deg) { LwLeanSet(deg); }
float CoXlTestLeanDeg(void) { return LwLeanDeg(); }
int CoXlTestLeanHold(float* up, const float* w, float deg) { return LwLeanHold(up, w, deg); }
float CoXlTestLeanAllowed(float slope, float over) { return LwLeanAllowed(slope, over); }
int CoXlTestLeanSlope(float x, float y, float* deg) { return LwLeanSlope(x, y, deg); }
void CoXlTestLeanAt(const float* origin) { LwLeanAt(origin); }
int CoXlTestLeanSlot(float* xy) { xy[0] = t_lwLean.x; xy[1] = t_lwLean.y; return t_lwLean.set; }
void CoXlTestLeanCounts(long* c)
{
    c[0] = InterlockedExchange(&g_lwLean.poses, 0); c[1] = InterlockedExchange(&g_lwLean.game, 0); c[2] = InterlockedExchange(&g_lwLean.ours, 0);
    c[3] = InterlockedExchange(&g_lwLean.blind, 0); c[4] = InterlockedExchange(&g_lwLean.boneCalls, 0); c[5] = InterlockedExchange(&g_lwLean.otherCalls, 0);
}
void CoXlTestLeanNote(void) { LwLeanNote(); }
int CoXlTestLeanWatch(const wchar_t* path) { return LwLeanWatch(path); }
void CoXlTestLpLevel(void) { LwLpLevel(); }
void CoXlTestLpScan(void) { LwLpScan(); }
int CoXlTestLpSample(uint32_t id, const float* up, float tilt, float ground, float off, const float* last, float len) { return LwLpSample(id, up, tilt, ground, off, last, len) != NULL; }
void CoXlTestLpNote(void) { LwLpNote(); }
#endif
