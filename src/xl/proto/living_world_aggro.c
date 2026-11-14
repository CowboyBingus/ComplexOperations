/* Host only: calm enemies close to a player notice one outside their view (LwSense), and a bounded log of enemies near
 * a player that stay calm, with what their eyes see of that player. The game's sight pass (0x887270 -> 0x886490) asks
 * per eye whether the target is in view (0x64CF30 -> 0x64C6F0) and, when it is, casts the eye's ray (0x175CA60, run by
 * 0x175C5C0 through [[game+0x3326328]+0x68]); 0x64D1D0 then reads the hits. The probe repeats both synchronously. */
#define LW_AGP_NEAR 25.0f   /* m from a player */
#define LW_AGP_TIMED 20.0f  /* m: engagement is timed from the first sample this close */
#define LW_AGP_CALM 3000u   /* ms near and below alert 3 before a unit is logged */
#define LW_AGP_UNITS 128
#define LW_AGP_LINES 24     /* unit lines a session */
#define LW_AGP_FIGHTING 6   /* of them, units already at alert 3 or more */
#define LW_AGP_EYE_LINES 160 /* eye lines a session, under the unit lines */
#define LW_AGP_TIME_LINES 40
#define LW_AGP_LOOKS 6      /* units whose own eye is checked per probe (twice a second) */
#define LW_SENSE_NEAR 15.0f /* m from the unit's body at the player's visibility 1.0, which scales it (D) */
#define LW_SENSE_TOLD 4000u /* ms before a unit that is not hostile is checked again; 1000 between checks otherwise */
#define LW_SENSE_AGAIN 20000u /* ms between two notices through one detector: its investigation (+0x5C) must not loop */
#define LW_SENSE_CHECK 1000u /* ms after a notice by which a unit of that detector must have left alert 0 */
#define LW_SENSE_RETRY 2000u /* ms before a notice that changed nothing is given again */
#define LW_SENSE_TRIES 3    /* notices in a row that change nothing before the detector is left for LW_SENSE_AGAIN */
#define LW_SENSE_DETS 64
#define LW_SENSE_HELD 4.0f  /* m across between a player's live place and the held one (pod, last known) */
#define LW_SENSE_CHECKS 4   /* units checked per player per probe */
#define LW_SENSE_FAST 5000u /* ms: a notice followed this soon by alert 3 */
#define LW_SENSE_LINES 8    /* lines a minute of each kind: notices, their results, units not noticed */
#define LW_FOLIAGE_HIT 0x4B8DC02Bu
#define LW_HIT 0x2C         /* a ray hit: +0 x, +4 y, +0x18 metres along the ray, +0x1C unit (0 static), +0x24 type */

/* Why a calm unit close to a player was not noticed (LwSense), and the two ways it is. */
enum { LW_SENSE_YES, LW_SENSE_ALERT, LW_SENSE_FAR, LW_SENSE_STEALTH, LW_SENSE_VIEW, LW_SENSE_BLOCKED, LW_SENSE_EARLY, LW_SENSE_HOSTILE,
    LW_SENSE_LEVEL, LW_SENSE_TARGET, LW_SENSE_STATE, LW_SENSE_FLAGS, LW_SENSE_PLACE, LW_SENSE_DEAF, LW_SENSE_TAGGED, LW_SENSE_NOSLOT, LW_SENSE_N };
static const char* const kLwSenseWhy[LW_SENSE_N] = {"noticed", "not calm", "too far", "the player's visibility", "in its view", "line blocked",
    "its eye ended early (sight cap or no eye)", "not hostile", "threshold at the cap", "not a target", "state 16 or no AI record", "flags +0xE4",
    "the player's place is held", "its detector takes no noise (+0x14 bit 0)", "the player's tag bit 25", "no slot"};
enum { LW_ROUTE_HEAR, LW_ROUTE_FLAG };
enum { LW_CONF_NONE, LW_CONF_LEFT, LW_CONF_RETRY, LW_CONF_GIVEN, LW_CONF_GONE };
enum { LW_EYE_NONE, LW_EYE_VIEW, LW_EYE_SEEN, LW_EYE_DRAWN, LW_EYE_COARSE, LW_EYE_OBJECT, LW_EYE_STATIC, LW_EYE_FOLIAGE, LW_EYE_UNIT, LW_EYE_MATE, LW_EYE_ENEMY, LW_EYE_N };
static const char* const kLwEyeWhat[LW_EYE_N] = {"no eye", "out of view", "seen", "the drawn ground", "the coarse collision over the tile",
    "a static object", "static (no drawn ground here)", "foliage", "another unit", "a squad mate", "another enemy"};

typedef struct { uint32_t unit; ULONGLONG since, seen, closeAt, senseAt, checkedAt, noticedAt; uint8_t logged, timed, route, lastPlayer, lastWhy, calm; } LwAgpUnit;
/* A notice waits and is confirmed per detector: a loader squad's members share one, and whichever updates first takes
 * the flag (0x7EB730 clears it), so the unit that leaves alert 0 need not be the one noticed. */
typedef struct { uint32_t det, unit, leftUnit; ULONGLONG waitAt, checkAt; uint8_t tries, route, pending, present, left; } LwSenseDet;
typedef struct { LwAgpUnit* a; uint32_t unit; float d, radius; ULONGLONG checkedAt; uint8_t player, loader, taken; } LwSenseCand;
static struct
{
    LwAgpUnit u[LW_AGP_UNITS];
    LwSenseDet dets[LW_SENSE_DETS];
    int lines, fighting, eyeLines, timeLines, pins, look, senseLines, skipLines, resultLines, pending;
    ULONGLONG nextAt, noteAt, minuteAt;
    uint32_t mNoticed[2], mLoader, mFast[2], mSkip[LW_SENSE_N]; /* since the last minute line; [route], [reason] */
    uint32_t mLeft[2], mNoEffect[2], mAgain, mGiven, mGone;
    uint32_t nearSum, calmSum, loaderSum, loaderFighting, scans; /* since the last note */
    uint32_t eyes[2][LW_EYE_N], sensed[2], timedN[2]; float timedSum[2], timedMax[2]; /* [0] loader members, [1] other units */
} g_lwAgp;

/* What an eye made of the target: 0x64C6F0's answer (code 0 out of view, 1 near, 2 far; +4 distance, +8 range) and,
 * when it asked for one, the ray's hits as 0x64D1D0 reads them. */
typedef struct
{
    int what, at, clear; uint32_t code, hits, unit, type, shape; float view[3], base, eye[3], dist, hitDist, hit[3], drawn, coarse, top; int tile;
} LwEyeLook;

/* 0x64D1D0's reading of a sight ray's hits, in the order the ray returned them: 1 seen, 0 blocked; *at the hit that
 * decided, -1 for none. Foliage is passed when the target is within `nearR` of the eye across, or the hit is within
 * `folR` of the target or nearer than that to the eye. Another unit blocks; the game's pass for parts of the target
 * (hash [game+0x3326698]) is not repeated. */
static int LwSightVerdict(const uint8_t* hits, uint32_t n, uint32_t target, const float* eye, const float* tpos, float nearR, float folR, int* at)
{
    float ex = tpos[0] - eye[0], ey = tpos[1] - eye[1];
    int nearTarget = nearR * nearR > ex * ex + ey * ey;
    *at = -1;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint8_t* h = hits + LW_HIT * i; float hx, hy; uint32_t unit, type;
        memcpy(&hx, h, 4); memcpy(&hy, h + 4, 4); memcpy(&unit, h + 0x1C, 4); memcpy(&type, h + 0x24, 4);
        if (unit == target) { *at = (int)i; return 1; }
        if (type == LW_FOLIAGE_HIT)
        {
            float tx = tpos[0] - hx, ty = tpos[1] - hy, qx = eye[0] - hx, qy = eye[1] - hy;
            if (nearTarget || folR * folR >= tx * tx + ty * ty || folR * folR > qx * qx + qy * qy) continue;
        }
        *at = (int)i;
        return 0;
    }
    return 1;
}

/* A static hit against the ground: on the drawn ground (0.3 m), on the coarse rows over a live tile (the coarse
 * collision that should lie lowered there), or an object. */
static int LwStaticKind(float z, float drawn, float coarse, int tile)
{
    if (drawn < -9000.0f) return LW_EYE_STATIC;
    if (fabsf(z - drawn) <= 0.3f) return LW_EYE_DRAWN;
    if (tile >= 0 && coarse > -9000.0f && fabsf(z - coarse) <= 0.3f && z > drawn) return LW_EYE_COARSE;
    return LW_EYE_OBJECT;
}

/* The functions the probe calls, as this build has them (else the eye checks stay off). */
static const ActPin kLwSightPins[] = {
    {0, 0x64C6F0u, "488bc44889581855565741544155415641574881ecf0000000"}, /* an eye's view of a target */
    {0, 0x503BC0u, "4883ec284c8bd14885c97507"},                           /* an eye's settings */
    {0, 0x11D9DF0u, "48895c241048896c241856574154415641574883ec2033ff"},   /* a target's stat modifier */
    {0, 0x888E50u, "40535741574883ec208b3dc1adbf024d"},                   /* the hearing writer (LwSense) */
    {0, 0x510170u, "4885c9750333c0c3488b094885c97507"},                   /* a detector's settings */
    {0, 0x885260u, "41544155" "4883ec38" "443b05dde9bf02"},               /* a detector entry's position and flags */
};
typedef const float* (*LwNodePosFn)(uint32_t unit, uint32_t node);
typedef uint8_t (*LwUnitValidFn)(uint32_t unit);
typedef uint32_t (*LwRayFn)(uint32_t handle, const float* from, const float* dir, float length, uint32_t leaveOut, void* hit, uint32_t max);
static struct { LwNodePosFn pos; LwUnitValidFn valid; LwRayFn ray; } g_lwEye;
static int LwEyeBind(void)
{
    if (g_lwAgp.pins == 0)
    {
        uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), t = 0, s = 0, f[3] = {0};
        int ok = ActPins(g_lw.game, kLwSightPins, sizeof kLwSightPins / sizeof kLwSightPins[0]) && !SafeRead(g_lw.game + 0x3326308u, &t, 8) && t &&
            !SafeRead(t + 0x18, &s, 8) && s && !SafeRead(s + 0x88, &f[0], 8) && !SafeRead(s + 0x720, &f[1], 8) && !SafeRead(g_lw.game + 0x3326328u, &t, 8) && t &&
            !SafeRead(t + 0x68, &f[2], 8) && f[0] == exe + 0x1FD190u && f[1] == exe + 0x1FEEB0u && f[2] == exe + 0x77DEC0u;
        if (ok) { g_lwEye.pos = (LwNodePosFn)f[0]; g_lwEye.valid = (LwUnitValidFn)f[1]; g_lwEye.ray = (LwRayFn)f[2]; }
        else Note("living world: sight probe: the game's functions are not this build's; eye checks off");
        g_lwAgp.pins = ok ? 1 : -1;
    }
    return g_lwAgp.pins > 0;
}

/* The point an eye looks at on `avatar`: its sight-target registry entry's node ([game+0x3326CB8], as 0x886519), and
 * that unit's root now: g_lwWorld.players can be a pod's held place or a last known one. */
static int LwTargetPoint(uint32_t avatar, float* p, float* root)
{
    uintptr_t reg = 0, infos = 0, info = 0, nodes = 0; uint32_t t = 0;
    if (!EvpPtr(g_lw.game + 0x3326CB8u, &reg) || !EvpHash(reg + 0x12050, avatar, 8, 4, &t) || t >= 4096 || !EvpPtr(reg + 0x12068, &infos) ||
        !EvpPtr(infos + 8ull * t, &info) || EvpU32(info + 8, 0) != avatar || !EvpPtr(reg + 0x12070, &nodes)) return 0;
    uint32_t unit = EvpU32(info + 0xC, 0), node = EvpU32(nodes + 0x60ull * t, 0);
    if (!g_lwEye.valid(unit)) return 0;
    memcpy(p, g_lwEye.pos(unit, node), 12);
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && EvpUnitPosition(unit, root);
}

/* One eye (a unit in the eye manager [game+0x3326520]) against `avatar`'s point `tpos`, as a calm unit asks (scale 1.0,
 * flag 0; 0x886635); `ground` adds the ground ray's top under a static hit; `anyView` casts the ray (r->clear) when the
 * view test says out of view too. */
static void LwEyeLookAt(uint32_t eyeUnit, uint32_t avatar, const float* tpos, int ground, int anyView, LwEyeLook* r)
{
    typedef void (*ViewFn)(uintptr_t mgr, void* out, uint32_t eye, const float* pos, uint32_t target, float scale, uint8_t flag);
    typedef uintptr_t (*SettingsFn)(uintptr_t info);
    memset(r, 0, sizeof *r); r->at = -1; r->clear = -1; r->tile = -1; r->drawn = r->coarse = r->top = -9999.0f;
    uintptr_t mgr = 0, infos = 0, recs = 0, info = 0, templates = 0; uint32_t i = 0;
    if (!EvpPtr(g_lw.game + 0x3326520u, &mgr) || !EvpHash(mgr + 0x30, eyeUnit, 8, 4, &i) || i >= 0x100000 || !EvpPtr(mgr + 0x48, &infos) ||
        !EvpPtr(mgr + 0x58, &recs) || !EvpPtr(infos + 8ull * i, &info) || !info || !EvpPtr(mgr + 8, &templates)) return;
    uintptr_t rec = recs + 0x28ull * i;
    uint32_t handle = EvpU32(info + 0xC, 0), node = EvpU32(rec + 4, 0), rayHandle = EvpU32(templates + 0xBC, 0);
    r->shape = EvpU32(rec + 0x24, 0); SafeRead(rec + 0xC, &r->base, 4);
    if (!g_lwEye.valid(handle)) return;
    memcpy(r->eye, g_lwEye.pos(handle, node), 12);
    uint8_t out[16] = {0};
    ((ViewFn)(g_lw.game + 0x64C6F0u))(mgr, out, eyeUnit, tpos, avatar, 1.0f, 0);
    memcpy(&r->code, out, 4); memcpy(r->view, out + 4, 12);
    float d[3] = {tpos[0] - r->eye[0], tpos[1] - r->eye[1], tpos[2] - r->eye[2]};
    r->dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    r->what = LW_EYE_VIEW;
    if ((r->code == 0 && !anyView) || !(r->dist > 1e-6f) || rayHandle == 0) return; /* no ray: the entry is not visible (0x64D0EA) */
    for (int k = 0; k < 3; k++) d[k] /= r->dist;
    uint8_t hits[8 * LW_HIT] = {0};
    r->hits = g_lwEye.ray(rayHandle, r->eye, d, r->dist, handle, hits, 8);
    if (r->hits > 8) r->hits = 8;
    uintptr_t set = ((SettingsFn)(g_lw.game + 0x503BC0u))(info);
    float nearR = 0, folR = 0;
    if (set) { SafeRead(set + 0x24, &nearR, 4); SafeRead(set + 0x28, &folR, 4); }
    r->clear = LwSightVerdict(hits, r->hits, avatar, r->eye, tpos, nearR, folR, &r->at);
    if (r->code == 0) return;
    if (r->clear) { r->what = LW_EYE_SEEN; return; }
    const uint8_t* h = hits + LW_HIT * r->at;
    memcpy(&r->hitDist, h + 0x18, 4); memcpy(&r->unit, h + 0x1C, 4); memcpy(&r->type, h + 0x24, 4);
    for (int k = 0; k < 3; k++) r->hit[k] = r->eye[k] + d[k] * r->hitDist;
    if (r->type == LW_FOLIAGE_HIT) { r->what = LW_EYE_FOLIAGE; return; }
    if (r->unit) { r->what = LW_EYE_UNIT; return; }
    r->drawn = MoveDrawnGround(r->hit[0], r->hit[1], &r->tile);
    r->coarse = MoveCoarseGround(r->hit[0], r->hit[1]);
    if (ground) LwGround(r->hit[0], r->hit[1], &r->top);
    r->what = LwStaticKind(r->hit[2], r->drawn, r->coarse, r->tile);
}

/* What kind of unit stopped a ray: a member of squad `sq` (-1 none), another enemy the host's AI holds
 * ([game+0x33266C8] hash +0x2330), or anything else (a prop, a pod, a vehicle). */
static int LwBlocker(uint32_t unit, int sq)
{
    uintptr_t ai = 0; uint32_t ii = 0; int other = -1;
    if (sq >= 0 && LwOnlineMember(unit, &other) && other == sq) return LW_EYE_MATE;
    return EvpPtr(g_lw.game + 0x33266C8u, &ai) && EvpHash(ai + 0x2330, unit, 8, 4, &ii) ? LW_EYE_ENEMY : LW_EYE_UNIT;
}

static void LwEyeLine(uint32_t unit, uint32_t eyeUnit, uint32_t bit, const LwEyeLook* r, const float* tpos)
{
    char ray[340] = "";
    uint32_t u = 0; uint64_t res = r->unit ? EvpEntityResource(g_lw.game, r->unit, &u) : 0;
    int sq = -1; LwOnlineMember(unit, &sq);
    if (r->code && r->what != LW_EYE_SEEN && r->at >= 0)
        snprintf(ray, sizeof ray, "; ray %u hits, hit %d stops it at %.2f m of %.2f: unit %08X (%s, resource %016llX), type %08X, at z %.2f (drawn ground %.2f, tile %d, coarse rows %.2f, ground ray top %.2f)",
            r->hits, r->at, (double)r->hitDist, (double)r->dist, r->unit, r->unit ? kLwEyeWhat[LwBlocker(r->unit, sq)] : "static", (unsigned long long)res, r->type,
            (double)r->hit[2], (double)r->drawn, r->tile, (double)r->coarse, (double)r->top);
    else if (r->code) snprintf(ray, sizeof ray, "; ray %u hits", r->hits);
    Note("living world: sight: %08X's detector, eye %08X%s (bit %u, shape %u, base range %.1f): %s; view code %u, %.2f m (eye z %.2f, target z %.2f), range %.2f, factor %.2f%s",
        unit, eyeUnit, eyeUnit == unit ? " (its own)" : "", bit, r->shape, (double)r->base, kLwEyeWhat[r->what], r->code, (double)r->dist, (double)r->eye[2],
        (double)tpos[2], (double)r->view[1], (double)r->view[2], ray);
}

/* The hostile or friendly entry for `target` in a detector record: 1 with its fields. */
static int LwAgpEntry(uintptr_t r, uint32_t target, int friendly, uint8_t* e)
{
    uint32_t count = EvpU32(r + (friendly ? 0x818u : 0x310u), 0);
    for (uint32_t i = 0; i < count && i < 16; i++)
    {
        uintptr_t at = r + (friendly ? 0x820u : 0x318u) + 0x50ull * i;
        if (EvpU32(at, 0) == target) return !SafeRead(at, e, 0x50);
    }
    return 0;
}

static void LwAgpLine(uint32_t unit, float d, int player)
{
    uintptr_t game = g_lw.game, det = 0, ents = 0, recs = 0, ent = 0, r = 0, ai = 0, aiRecs = 0, st = 0, clock = 0;
    uint32_t avatar = g_lwWorld.avatars[player], detE = LwAwareDetector(unit), di = 0, ii = 0, flags = 0xFFFFFFFFu, synced = 0xFFFFFFFFu;
    uint32_t u = 0; uint64_t res = EvpEntityResource(game, unit, &u), now = 0;
    if (EvpPtr(game + 0x3326348u, &clock)) SafeRead(clock + 0x18, &now, 8);
    if (EvpPtr(game + 0x33266C8u, &ai) && EvpHash(ai + 0x2330, unit, 8, 4, &ii) && EvpPtr(ai + 0x2350, &aiRecs))
    { flags = EvpU32(aiRecs + 0xA50ull * ii + 0xE4, 0xFFFFFFFFu); if (EvpPtr(ai + 0x2360, &st)) synced = EvpU32(st + 20ull * ii, 0xFFFFFFFFu); }
    int sq = -1; LwOnlineMember(unit, &sq);
    char dline[300] = "no awareness detector", hline[160] = "no hostile entry for the player", fline[40] = "", mline[80] = "";
    if (detE && EvpPtr(game + 0x3326548u, &det) && EvpHash(det + 0x30, detE, 8, 4, &di) && EvpPtr(det + 0x50, &recs) && EvpPtr(det + 0x48, &ents) &&
        EvpPtr(ents + 8ull * di, &ent))
    {
        r = recs + 0x13F8ull * di;
        uint64_t next = 0; SafeRead(r + 0x13D8, &next, 8);
        uintptr_t set = ((uintptr_t (*)(uintptr_t))(game + 0x510170u))(ent); /* the detector's settings (0x8877FC) */
        uint8_t always = 0xFF, skip = 0xFF; if (set) { SafeRead(set + 0x14, &always, 1); SafeRead(set + 0x15, &skip, 1); }
        uint8_t on = 0; SafeRead(r + 0x13F0, &on, 1);
        snprintf(dline, sizeof dline, "detector %08X (owned %s, entity +0x14 %X, %s): faction %X, lists %X, on %u, next scan %+.2f s, pending %u, hostile %u, friendly %u, eyes %u, ears %u, danger %u; avatar_always_enemy %u, +0x15 %u",
            detE, di < EvpU32(det + 0x20, 0) ? "yes" : "NO", EvpU32(ent + 0x14, 0), detE == unit ? "its own" : "shared", EvpU32(r + 0x13E8, 0), EvpU32(r + 0x13EC, 0), on,
            now && next ? ((double)(int64_t)(next - now)) / 1e6 : 0.0, EvpU32(r + 0x1368, 0), EvpU32(r + 0x310, 0), EvpU32(r + 0x818, 0),
            EvpU32(r, 0), EvpU32(r + 0x104, 0), EvpU32(r + 0x188, 0), always, skip);
        uint8_t e[0x50];
        if (LwAgpEntry(r, avatar, 0, e))
        {
            uint64_t seen; float hear, danger, aware; uint32_t ef, eyes; memcpy(&seen, e + 0x10, 8); memcpy(&hear, e + 0x38, 4); memcpy(&danger, e + 0x3C, 4);
            memcpy(&aware, e + 0x40, 4); memcpy(&ef, e + 0x48, 4); memcpy(&eyes, e + 0x4C, 4);
            snprintf(hline, sizeof hline, "hostile entry: flags %X, visible %u, awareness %.2f, eyes %X, seen %.1f s ago, hearing %.1f, danger %.1f",
                ef, e[0x30], (double)aware, eyes, seen && now >= seen ? (double)(now - seen) / 1e6 : -1.0, (double)hear, (double)danger);
        }
        if (LwAgpEntry(r, avatar, 1, e)) snprintf(fline, sizeof fline, "; the player is in its FRIENDLY list");
    }
    if (g_lwAgp.pins > 0)
    {
        typedef uintptr_t (*ModFn)(uint32_t entity, uint32_t hash, uint32_t kind);
        uintptr_t m = ((ModFn)(game + 0x11D9DF0u))(avatar, 0x14ECCE15u, 3); /* enemy_sight_range, as 0x64C9D9 reads it */
        float v = 0; uint32_t op = 0;
        if (m) { SafeRead(m + 4, &op, 4); SafeRead(m + 8, &v, 4); snprintf(mline, sizeof mline, "; the player's enemy_sight_range modifier: op %u, %.2f", op, (double)v); }
        else snprintf(mline, sizeof mline, "; no enemy_sight_range modifier on the player");
    }
    Note("living world: aggro probe: %s %08X (%s%d, squad entity %08X, resource %016llX) %.1f m from player %d (%08X): alert %u, AiEnemy flags %X, synced state %u; %s; %s%s%s",
        LwAlert(unit) >= 3 ? "fighting" : "calm", unit, sq >= 0 ? "squad " : "not a loader squad's ", sq, sq >= 0 ? g_lwSquads[sq].squad : 0, (unsigned long long)res, (double)d, player, avatar,
        LwAlert(unit), flags, synced, dline, hline, fline, mline);
    /* every eye of that detector against the player's point */
    float tpos[3], root[3];
    if (!r || g_lwAgp.pins <= 0 || !LwTargetPoint(avatar, tpos, root)) return;
    uint32_t count = EvpU32(r, 0);
    for (uint32_t k = 0; k < count && k < 16 && g_lwAgp.eyeLines < LW_AGP_EYE_LINES; k++)
    {
        uint32_t eyeUnit = EvpU32(r + 4 + 8ull * k, 0), bit = EvpU32(r + 8 + 8ull * k, 0);
        LwEyeLook look; LwEyeLookAt(eyeUnit, avatar, tpos, 1, 0, &look);
        LwEyeLine(unit, eyeUnit, bit, &look, tpos); g_lwAgp.eyeLines++;
    }
}

/* A unit came to alert 3: the time since it was first within LW_AGP_TIMED of a player, calm. */
static void LwAgpTimed(LwAgpUnit* a, int loader, float d, ULONGLONG now)
{
    float s = (float)(now - a->closeAt) / 1000.0f;
    g_lwAgp.timedN[!loader]++; g_lwAgp.timedSum[!loader] += s; if (s > g_lwAgp.timedMax[!loader]) g_lwAgp.timedMax[!loader] = s;
    if (g_lwAgp.timeLines++ < LW_AGP_TIME_LINES)
        Note("living world: sight: %s %08X reached alert 3 %.1f s after it was first within %.0f m of a player, calm (now %.1f m)",
            loader ? "loader member" : "unit", a->unit, (double)s, (double)LW_AGP_TIMED, (double)d);
}

/* Terminid eyes see 40 m ahead but about 1 m to the side and behind (shape 1: +0x10, +0x14), times the player's
 * visibility (0x720530), and a walking player is heard within 10 m (noise type 5). So a calm unit passes a player
 * beside or behind it unseen. A calm unit whose body is within the reach and whose vision ray reaches the player is
 * told through the game's own detector entry for the player; LwSenseRoute picks how. */
static int LwSenseRule(uint32_t alert, float body, float reach, uint32_t code, int clear)
{
    if (alert != 0) return LW_SENSE_ALERT;
    if (body > LW_SENSE_NEAR) return LW_SENSE_FAR;
    if (code != 0) return LW_SENSE_VIEW; /* in view: the game's own sight */
    if (body > reach) return LW_SENSE_STEALTH;
    return clear == 1 ? LW_SENSE_YES : LW_SENSE_BLOCKED;
}
/* The player's enemy_sight_range modifier on an eye's range, as 0x64C9DE..0x64CA2B: only a value over 0 counts; op 0
 * replaces, 1 or 3 adds, 2 multiplies. */
static float LwSightMod(float range, uint32_t op, float value)
{
    if (!(value > 0.0f)) return range;
    if (op == 0) return value;
    if (op == 1 || op == 3) return range + value;
    return op == 2 ? range * value : range;
}
/* The reach: LW_SENSE_NEAR cut by the share the modifier leaves of the eye's range ahead `base` (never lengthened: the
 * reach is the mod's own), times the square root of the visibility. The game scales an eye's range by the visibility
 * itself (0x64CE30); at dusk that is 0.12 to 0.6, which left a player standing 5 m behind a unit unnoticed. */
static float LwSenseReach(float visibility, float base, uint32_t op, float value)
{
    float share = base > 0.0f ? LwSightMod(base, op, value) / base : 1.0f;
    if (!(share < 1.0f)) share = 1.0f;
    return LW_SENSE_NEAR * share * (visibility > 0.0f ? sqrtf(visibility) : 0.0f);
}
/* The distance from a unit's body, not its root: a large unit's root is metres inside it. `radius` is the mover's
 * ([game+0x3326418] record +0x98, which 0x59C21D copies to the motion record the mover step 0x134CF99 reads). */
static float LwBodyDist(float d, float radius)
{
    if (!(radius > 0.0f)) radius = 0.0f;
    if (radius > 8.0f) radius = 8.0f;
    return d > radius ? d - radius : 0.0f;
}
/* 0x888E50 writes nothing when the detector entity's +0x14 bit 0 is clear (0x888EF2) or the source's tags have bit 25
 * (0x888F0E). 0x885260 has neither exit; the flag route keeps both so the two routes notice the same players. */
static int LwSenseGate(uint8_t entity, uint64_t tags)
{
    if (!(entity & 1u)) return LW_SENSE_DEAF;
    return (tags >> 25) & 1u ? LW_SENSE_TAGGED : LW_SENSE_YES;
}
static int LwPlaceHeld(const float* live, const float* held)
{
    float dx = live[0] - held[0], dy = live[1] - held[1];
    return dx * dx + dy * dy > LW_SENSE_HELD * LW_SENSE_HELD;
}
/* The detector's slot: its own, else (with `make`) one whose wait is over, the oldest first (a notice being checked
 * is inside its wait). NULL when none. */
static LwSenseDet* LwSenseDetOf(LwSenseDet* t, int n, uint32_t det, ULONGLONG now, int make)
{
    LwSenseDet* spare = NULL;
    for (int i = 0; i < n; i++)
    {
        if (t[i].det == det) return &t[i];
        if (now >= t[i].waitAt && (!spare || t[i].waitAt < spare->waitAt)) spare = &t[i];
    }
    if (!make || !spare) return NULL;
    *spare = (LwSenseDet){.det = det};
    return spare;
}
static void LwSenseNoticed(LwSenseDet* e, uint32_t unit, int route, ULONGLONG now)
{
    e->unit = unit; e->route = (uint8_t)route; e->pending = 1; e->present = e->left = 0; e->tries++;
    e->checkAt = now + LW_SENSE_CHECK; e->waitAt = now + LW_SENSE_AGAIN;
}
/* A notice is one bit or one number in an entry the game may clear, overtake or never read (a mate takes the flag,
 * awareness crosses 1 before the update), so its effect is checked: a unit of the detector left alert 0 by checkAt, or
 * the notice is given again after LW_SENSE_RETRY, LW_SENSE_TRIES times at most. */
static int LwSenseConfirm(LwSenseDet* e, ULONGLONG now)
{
    if (!e->pending) return LW_CONF_NONE;
    if (e->left) { e->pending = 0; e->tries = 0; return LW_CONF_LEFT; }
    if (now < e->checkAt) { e->present = 0; return LW_CONF_NONE; }
    e->pending = 0;
    if (!e->present) { e->tries = 0; return LW_CONF_GONE; }
    if (e->tries < LW_SENSE_TRIES) { e->waitAt = now + LW_SENSE_RETRY; return LW_CONF_RETRY; }
    e->tries = 0;
    return LW_CONF_GIVEN;
}
/* The next unit to check against `player`: the one checked longest ago (never: first), the nearer of two. In list
 * order the first few took every check and the rest were never tested. */
static int LwSensePick(const LwSenseCand* c, int n, int player)
{
    int best = -1;
    for (int i = 0; i < n; i++)
    {
        if (c[i].taken || c[i].player != player) continue;
        if (best < 0 || c[i].checkedAt < c[best].checkedAt || (c[i].checkedAt == c[best].checkedAt && c[i].d < c[best].d)) best = i;
    }
    return best;
}
/* The player a unit is checked against: the next one in reach after the last, so the nearest is not the only one. */
static int LwSenseNextPlayer(int last, const uint8_t* ok, int n)
{
    for (int k = 1; k <= n; k++) if (ok[(last + k) % n]) return (last + k) % n;
    return -1;
}
/* At alert 0 the game compares hearing (+0x38 > settings +0x2BC, 0x7EB694) only while the entry's awareness +0x40 is
 * under 1 (0x7EB5A4). From 1 up it asks the unit's own eye, and out of view (code 0) ends there (0x7EB62C). That case
 * takes flag 0x10 instead, which the game's own alerts set (0x885260 from 0x7E3F15, 0x7F27B2): alert 3 (0x7EB506). */
static int LwSenseRoute(float awareness)
{
    return awareness >= 1.0f ? LW_ROUTE_FLAG : LW_ROUTE_HEAR;
}
/* The awareness update skips a unit in state 16 ([AI +0x2360] stride 20, 0x7EB136) or with +0xE4 bit 1, 2, 3 or 5
 * (0x7EB1EE..0x7EB23F): an entry written then changes nothing. */
static int LwSenseLost(uint32_t flags, uint32_t state)
{
    if (state == 0x10) return LW_SENSE_STATE;
    return (flags & 0x2Eu) ? LW_SENSE_FLAGS : LW_SENSE_YES;
}
/* The game's hostility test (0x8877E2..0x887A1C): none when the detector entity's +0x14 bit 1 is set, its faction
 * +0x13E8 is 0 or its +0x13F0 is off. Then hostile when its settings +0x14 make every avatar an enemy and the target
 * is one, or its lists +0x13EC bit 0 is set and its faction shares no bit with the target's mask, which is not 0. */
static int LwHostileRule(uint8_t entity, uint8_t on, uint8_t always, int avatar, uint32_t lists, uint32_t faction, uint32_t mask)
{
    if ((entity & 2u) || faction == 0 || on == 0) return 0;
    return (always && avatar) || ((lists & 1u) && (faction & mask) == 0 && mask != 0);
}
/* A detector's record ([game+0x3326548]: hash +0x30, records +0x50 of 0x13F8, entities +0x48). 0 when not found. */
static uintptr_t LwDetRecord(uint32_t detector, uintptr_t* entity)
{
    uintptr_t det = 0, recs = 0, ents = 0; uint32_t di = 0;
    if (!EvpPtr(g_lw.game + 0x3326548u, &det) || !EvpHash(det + 0x30, detector, 8, 4, &di) || !EvpPtr(det + 0x50, &recs) ||
        !EvpPtr(det + 0x48, &ents) || !EvpPtr(ents + 8ull * di, entity) || !*entity) return 0;
    return recs + 0x13F8ull * di;
}
static int LwHostileTo(uintptr_t r, uintptr_t ent, uint32_t avatar)
{
    uintptr_t game = g_lw.game, reg = 0, masks = 0, avs = 0; uint32_t t = 0, slot = 0;
    if (!EvpPtr(game + 0x3326CB8u, &reg) || !EvpHash(reg + 0x12050, avatar, 8, 4, &t) || !EvpPtr(reg + 0x12078, &masks)) return 0;
    uintptr_t set = ((uintptr_t (*)(uintptr_t))(game + 0x510170u))(ent);
    uint8_t always = 0, flags = 0, on = 0; if (set) SafeRead(set + 0x14, &always, 1);
    SafeRead(ent + 0x14, &flags, 1); SafeRead(r + 0x13F0, &on, 1);
    int isAvatar = EvpPtr(game + 0x3326D20u, &avs) && EvpHash(avs + 0xF8, avatar, 8, 4, &slot);
    return LwHostileRule(flags, on, always, isAvatar, EvpU32(r + 0x13EC, 0), EvpU32(r + 0x13E8, 0), EvpU32(masks + 4ull * t, 0));
}
/* The record's entry for a target (count +0x310, entries +0x318 of 0x50, key +0, at most 16: 0x884D30): its awareness. */
static float LwDetAwareness(uintptr_t r, uint32_t target)
{
    uint32_t n = EvpU32(r + 0x310, 0);
    for (uint32_t i = 0; i < n && i < 16; i++)
    {
        float aw = 0; uintptr_t e = r + 0x318 + 0x50ull * i;
        if (EvpU32(e, 0) == target) return SafeRead(e + 0x40, &aw, 4) ? 0.0f : aw;
    }
    return 0.0f;
}
static int LwSenseLostFor(uint32_t unit)
{
    uintptr_t ai = 0, recs = 0, st = 0; uint32_t ii = 0;
    if (!EvpPtr(g_lw.game + 0x33266C8u, &ai) || !EvpHash(ai + 0x2330, unit, 8, 4, &ii) || !EvpPtr(ai + 0x2350, &recs) || !EvpPtr(ai + 0x2360, &st))
        return LW_SENSE_STATE;
    return LwSenseLost(EvpU32(recs + 0xA50ull * ii + 0xE4, 0), EvpU32(st + 20ull * ii, 0x10));
}
/* The eye the awareness update asks at alert 0: [Behavior record + 0x6C] ([rsi+0x64] at 0x7EB5CB, rsi from 0x7ECF70),
 * which need not be the unit. */
static uint32_t LwAwareEye(uint32_t unit)
{
    uintptr_t beh = 0, recs = 0; uint32_t bi = 0;
    if (!EvpPtr(g_lw.game + 0x3326740u, &beh) || !EvpHash(beh + 0x40, unit, 8, 4, &bi) || !EvpPtr(beh + 0x60, &recs)) return 0;
    return EvpU32(recs + 0x1F8ull * bi + 0x6C, 0);
}
/* The mover's radius of a unit ([game+0x3326418]: hash +0x30, records +0x50 of 0xA8, +0x98; 0x59C093..0x59C18B). */
static float LwBodyRadius(uint32_t unit)
{
    uintptr_t nav = 0, recs = 0; uint32_t ni = 0; float r = 0;
    if (!EvpPtr(g_lw.game + 0x3326418u, &nav) || !EvpHash(nav + 0x30, unit, 8, 4, &ni) || !EvpPtr(nav + 0x50, &recs) ||
        SafeRead(recs + 0xA8ull * ni + 0x98, &r, 4) || !isfinite(r)) return 0.0f;
    return r;
}
/* An entity's tags, as 0x4A9710: [game+0x3326430] hash +0x18, 8 bytes each at [+0x38]; 0 with none. */
static uint64_t LwTags(uint32_t entity)
{
    uintptr_t tags = 0, values = 0; uint32_t ti = 0; uint64_t v = 0;
    if (!EvpPtr(g_lw.game + 0x3326430u, &tags) || !EvpHash(tags + 0x18, entity, 8, 4, &ti) || !EvpPtr(tags + 0x38, &values) ||
        SafeRead(values + 8ull * ti, &v, 8)) return 0;
    return v;
}
static void LwSense(LwAgpUnit* a, uint32_t unit, int player, const float* tpos, const float* feet, int loader, float d, float radius, ULONGLONG now)
{
    typedef void (*HearFn)(void*, uint32_t, uint32_t, const float*, float);
    typedef void (*EntryFn)(void*, uint32_t, uint32_t, const float*, uint8_t, uint8_t);
    typedef uintptr_t (*ModFn)(uint32_t entity, uint32_t hash, uint32_t kind);
    uintptr_t ent = 0, r = 0, targets = 0; uint32_t avatar = g_lwWorld.avatars[player], det = LwAwareDetector(unit), slot = 0;
    a->senseAt = now + 1000; a->checkedAt = now; a->lastPlayer = (uint8_t)player;
    if (!det || !(r = LwDetRecord(det, &ent))) return;
    LwSenseDet* e = LwSenseDetOf(g_lwAgp.dets, LW_SENSE_DETS, det, now, 0);
    /* this detector was told: one squad, one investigation */
    if (e && e->pending) return; /* its mates stay in turn: a notice with no effect is given again through any of them */
    if (e && now < e->waitAt) { if (e->waitAt > a->senseAt) a->senseAt = e->waitAt; return; }
    if (!LwHostileTo(r, ent, avatar)) { g_lwAgp.mSkip[LW_SENSE_HOSTILE]++; a->senseAt = now + LW_SENSE_TOLD; return; }
    uint32_t eye = LwAwareEye(unit), op = 0; float value = 0; uint8_t entity = 0;
    LwEyeLook look; LwEyeLookAt(eye, avatar, tpos, 0, 1, &look);
    uintptr_t m = ((ModFn)(g_lw.game + 0x11D9DF0u))(avatar, 0x14ECCE15u, 3); /* enemy_sight_range, as 0x64CC3C */
    if (m) { SafeRead(m + 4, &op, 4); SafeRead(m + 8, &value, 4); }
    /* view[2]: the visibility 0x64C6F0 used (0x64CEA1). An early exit (the global sight cap 0x64C919, no eye) writes
     * neither it nor the distance view[0]. */
    float body = LwBodyDist(d, radius), reach = LwSenseReach(look.view[2], look.base, op, value);
    int why = look.code == 0 && look.view[0] == 0.0f ? LW_SENSE_EARLY : LwSenseRule(LwAlert(unit), body, reach, look.code, look.clear);
    float threshold = 0, investigate = 0, aw = LwDetAwareness(r, avatar);
    int route = LwSenseRoute(aw);
    if (why == LW_SENSE_YES) why = LwSenseLostFor(unit);
    if (why == LW_SENSE_YES && route == LW_ROUTE_HEAR && (!LwHearSettings(unit, &threshold, &investigate) || !(threshold >= 0.0f && threshold + 1.0f < 40.0f)))
        why = LW_SENSE_LEVEL; /* 40: the cap 0x888FCA */
    /* 0x885260 needs the player in [game+0x3326508] +0x40, else it does nothing. */
    if (why == LW_SENSE_YES && !(EvpPtr(g_lw.game + 0x3326508u, &targets) && EvpHash(targets + 0x40, avatar, 8, 4, &slot))) why = LW_SENSE_TARGET;
    if (why == LW_SENSE_YES) { SafeRead(ent + 0x14, &entity, 1); why = LwSenseGate(entity, LwTags(avatar)); }
    if (why == LW_SENSE_YES && !e && !(e = LwSenseDetOf(g_lwAgp.dets, LW_SENSE_DETS, det, now, 1))) why = LW_SENSE_NOSLOT;
    if (why != LW_SENSE_YES)
    {
        if (why < LW_SENSE_STEALTH || why == LW_SENSE_VIEW) { a->lastWhy = (uint8_t)why; return; }
        g_lwAgp.mSkip[why]++;
        if (a->lastWhy != why && g_lwAgp.skipLines++ < LW_SENSE_LINES)
            Note("living world: sight: %s %08X did not notice player %d at %.1f m (body %.1f m, radius %.1f): %s; reach %.1f m (visibility %.2f, sight modifier op %u %.2f), eye %08X range %.2f there, awareness %.2f, detector %08X",
                loader ? "loader member" : "unit", unit, player, (double)d, (double)body, (double)radius, kLwSenseWhy[why], (double)reach, (double)look.view[2], op, (double)value,
                eye, (double)look.view[1], (double)aw, det);
        a->lastWhy = (uint8_t)why;
        return;
    }
    /* 0x888E50 moves the entry's point only when its flags are then exactly 4 (0x889015), so the point is set first:
     * 0x885260 writes a nonzero position always (0x8853C2) and, with flag 0x10, the target's own (0x88545C). */
    ((EntryFn)(g_lw.game + 0x885260u))(NULL, det, avatar, feet, 0, (uint8_t)(route == LW_ROUTE_FLAG));
    if (route == LW_ROUTE_HEAR) ((HearFn)(g_lw.game + 0x888E50u))(NULL, det, avatar, feet, threshold + 1.0f);
    g_lwAgp.mAgain += (uint32_t)(e->tries != 0);
    LwSenseNoticed(e, unit, route, now); g_lwAgp.pending++;
    a->noticedAt = now; a->route = (uint8_t)route; a->lastWhy = LW_SENSE_YES;
    g_lwAgp.sensed[!loader]++; g_lwAgp.mNoticed[route]++; g_lwAgp.mLoader += (uint32_t)loader;
    if (g_lwAgp.senseLines++ < LW_SENSE_LINES)
        Note("living world: sight: %s %08X noticed player %d at %.1f m (body %.1f m, radius %.1f) outside its view (eye %08X, range %.2f there; reach %.1f m: visibility %.2f, sight modifier op %u %.2f; awareness %.2f): %s, detector %08X (%s), notice %u",
            loader ? "loader member" : "unit", unit, player, (double)d, (double)body, (double)radius, eye, (double)look.view[1], (double)reach, (double)look.view[2], op, (double)value, (double)aw,
            route == LW_ROUTE_FLAG ? "alerted (flag 0x10)" : "heard just over its threshold", det, det == unit ? "its own" : "shared", e->tries);
}
/* The notices due a check: what became of each, counted per route. */
static void LwSenseResults(ULONGLONG now)
{
    static const char* const what[] = {"", "a unit of it left alert 0", "no effect: noticed again after a wait", "no effect: left alone", "its units were not seen again"};
    g_lwAgp.pending = 0;
    for (int i = 0; i < LW_SENSE_DETS; i++)
    {
        LwSenseDet* e = &g_lwAgp.dets[i]; int route = e->route & 1, tries = e->tries, c = LwSenseConfirm(e, now);
        if (c == LW_CONF_NONE) { g_lwAgp.pending += e->pending; continue; }
        if (c == LW_CONF_LEFT) g_lwAgp.mLeft[route]++;
        else if (c == LW_CONF_GONE) g_lwAgp.mGone++;
        else { g_lwAgp.mNoEffect[route]++; g_lwAgp.mGiven += (uint32_t)(c == LW_CONF_GIVEN); }
        char who[64] = "";
        if (c == LW_CONF_LEFT) snprintf(who, sizeof who, ": %08X, alert %u (%s)", e->leftUnit, LwAlert(e->leftUnit), e->leftUnit == e->unit ? "the unit noticed" : "a mate");
        if (g_lwAgp.resultLines++ < LW_SENSE_LINES)
            Note("living world: sight: detector %08X, noticed through %08X (%s, notice %d): %s%s", e->det, e->unit,
                route == LW_ROUTE_FLAG ? "flag 0x10" : "hearing", tries, what[c], who);
    }
}

/* Twice a second: the AI units the host owns ([game+0x33266C8]: owned +0x2324, entity pointers +0x2348) near a player. */
static void LwAggroProbe(void)
{
    ULONGLONG now = GetTickCount64();
    if (now < g_lwAgp.nextAt || g_lwWorld.playersAt <= 0) return;
    g_lwAgp.nextAt = now + 500;
    uintptr_t ai = 0, ents = 0;
    if (!EvpPtr(g_lw.game + 0x33266C8u, &ai) || !EvpPtr(ai + 0x2348, &ents)) return;
    int eyesOn = LwEyeBind(), looks = 0, candN = 0;
    static LwSenseCand cands[LW_AGP_UNITS];
    float tpos[EVT_MAX_PLAYERS][3], feet[EVT_MAX_PLAYERS][3]; uint8_t tposOk[EVT_MAX_PLAYERS] = {0}, held[EVT_MAX_PLAYERS] = {0};
    for (int k = 0; eyesOn && k < g_lwWorld.playersAt && k < EVT_MAX_PLAYERS; k++)
    {
        tposOk[k] = (uint8_t)LwTargetPoint(g_lwWorld.avatars[k], tpos[k], feet[k]);
        held[k] = (uint8_t)(tposOk[k] && LwPlaceHeld(feet[k], g_lwWorld.players[k]));
    }
    uint32_t owned = EvpU32(ai + 0x2324, 0), nearN = 0, calmN = 0, loaderN = 0, loaderFightN = 0;
    for (uint32_t i = 0; i < owned && i < 4096; i++)
    {
        uintptr_t ent = 0; uint32_t unit, handle = 0; float p[3];
        if (!EvpPtr(ents + 8ull * i, &ent) || (unit = EvpU32(ent + 8, 0)) == 0 || !EvpEntityResource(g_lw.game, unit, &handle) || !handle ||
            !EvpUnitPosition(handle, p)) continue;
        int player = -1; float best = LW_AGP_NEAR * LW_AGP_NEAR;
        for (int k = 0; k < g_lwWorld.playersAt; k++)
        {
            float dx = p[0] - g_lwWorld.players[k][0], dy = p[1] - g_lwWorld.players[k][1], dz = p[2] - g_lwWorld.players[k][2], d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; player = k; }
        }
        if (player < 0) continue;
        nearN++;
        int fighting = LwAlert(unit) >= 3, slot = -1, empty = -1, sq = -1, loader = LwOnlineMember(unit, &sq) && sq >= 0;
        calmN += (uint32_t)!fighting;
        if (loader) { loaderN++; loaderFightN += (uint32_t)fighting; }
        for (int k = 0; k < LW_AGP_UNITS && slot < 0; k++) { if (g_lwAgp.u[k].unit == unit) slot = k; else if (empty < 0 && now - g_lwAgp.u[k].seen > 5000) empty = k; }
        if (slot < 0 && empty < 0) { g_lwAgp.mSkip[LW_SENSE_NOSLOT] += (uint32_t)(LwAlert(unit) == 0 && best <= LW_SENSE_NEAR * LW_SENSE_NEAR); continue; }
        if (slot < 0) { slot = empty; g_lwAgp.u[slot] = (LwAgpUnit){.unit = unit, .since = now, .timed = (uint8_t)fighting}; }
        LwAgpUnit* a = &g_lwAgp.u[slot];
        if (g_lwAgp.pending > 0)
        {
            /* a unit that was calm at the last probe and is not now: one already fighting proves nothing */
            LwSenseDet* e = LwSenseDetOf(g_lwAgp.dets, LW_SENSE_DETS, LwAwareDetector(unit), now, 0);
            if (e && e->pending) { e->present = 1; if (a->calm && LwAlert(unit) != 0 && !e->left) { e->left = 1; e->leftUnit = unit; } }
        }
        a->calm = (uint8_t)(LwAlert(unit) == 0);
        if (!fighting && !a->closeAt && best <= LW_AGP_TIMED * LW_AGP_TIMED) a->closeAt = now;
        if (fighting && !a->timed) { a->timed = 1; if (a->closeAt) LwAgpTimed(a, loader, sqrtf(best), now); }
        /* its own eye, a few units a probe in turn: what stops a calm unit seeing the player */
        if (!fighting && eyesOn && tposOk[player] && looks < LW_AGP_LOOKS && (i + (uint32_t)g_lwAgp.look) % 3 == 0)
        {
            LwEyeLook look; LwEyeLookAt(LwAwareEye(unit), g_lwWorld.avatars[player], tpos[player], 0, 0, &look);
            if (look.what == LW_EYE_UNIT) look.what = LwBlocker(look.unit, sq);
            g_lwAgp.eyes[!loader][look.what]++; looks++;
        }
        if (a->noticedAt && (fighting || now - a->noticedAt > LW_SENSE_FAST)) { g_lwAgp.mFast[a->route & 1] += (uint32_t)(fighting && now - a->noticedAt <= LW_SENSE_FAST); a->noticedAt = 0; }
        if (eyesOn && LwAlert(unit) == 0 && now >= a->senseAt)
        {
            /* against each player's live place, from the unit's body */
            float radius = LwBodyRadius(unit), dk[EVT_MAX_PLAYERS]; uint8_t ok[EVT_MAX_PLAYERS] = {0}; int heldNear = 0, who;
            for (int k = 0; k < g_lwWorld.playersAt; k++)
            {
                if (!tposOk[k]) continue;
                float dx = p[0] - feet[k][0], dy = p[1] - feet[k][1], dz = p[2] - feet[k][2];
                if (LwBodyDist(dk[k] = sqrtf(dx * dx + dy * dy + dz * dz), radius) > LW_SENSE_NEAR) continue;
                if (held[k]) heldNear = 1; else ok[k] = 1;
            }
            if ((who = LwSenseNextPlayer(a->lastPlayer, ok, g_lwWorld.playersAt)) >= 0 && candN < LW_AGP_UNITS)
                cands[candN++] = (LwSenseCand){.a = a, .unit = unit, .d = dk[who], .radius = radius, .checkedAt = a->checkedAt, .player = (uint8_t)who, .loader = (uint8_t)loader};
            else if (heldNear) { g_lwAgp.mSkip[LW_SENSE_PLACE]++; a->senseAt = now + 1000; }
        }
        if (fighting && a->logged != 2) a->since = now; /* calm time counts from its last fight */
        a->seen = now;
        if (g_lwAgp.lines >= LW_AGP_LINES || a->logged) continue;
        if (fighting ? g_lwAgp.fighting >= LW_AGP_FIGHTING : now - a->since < LW_AGP_CALM) continue;
        a->logged = fighting ? 2 : 1; g_lwAgp.lines++; g_lwAgp.fighting += fighting;
        LwAgpLine(unit, sqrtf(best), player);
    }
    g_lwAgp.look++;
    LwSenseResults(now); /* before this probe's notices: theirs are checked from the next probe on */
    for (int k = 0; k < g_lwWorld.playersAt; k++)
        for (int c = 0, i; c < LW_SENSE_CHECKS && (i = LwSensePick(cands, candN, k)) >= 0; c++)
        { cands[i].taken = 1; LwSense(cands[i].a, cands[i].unit, k, tpos[k], feet[k], cands[i].loader, cands[i].d, cands[i].radius, now); }
    if (now - g_lwAgp.minuteAt >= 60000)
    {
        const uint32_t *m = g_lwAgp.mNoticed, *f = g_lwAgp.mFast, *k = g_lwAgp.mSkip, *l = g_lwAgp.mLeft, *n = g_lwAgp.mNoEffect; uint32_t any = m[0] + m[1] + l[0] + l[1] + n[0] + n[1] + g_lwAgp.mGone;
        for (int i = 0; i < LW_SENSE_N; i++) any += k[i];
        if (any)
            Note("living world: sight (60 s): %u notices of a player outside a calm unit's view (%u through loader members, %u given again after no effect); "
                "heard at awareness under 1: %u, left alert 0: %u, no effect: %u, at alert 3 within %.0f s: %u; alerted at awareness 1 or more: %u, left alert 0: %u, no effect: %u, at alert 3: %u; "
                "left alone after %d notices: %u, not seen again: %u; "
                "not noticed: not hostile %u, the player's visibility %u, line blocked %u, eye ended early (sight cap or no eye) %u, threshold at the cap %u, not a target %u, state 16 or no AI record %u, flags +0xE4 %u, "
                "the player's place held %u, detector takes no noise %u, the player's tag bit 25 %u, no slot %u",
                m[0] + m[1], g_lwAgp.mLoader, g_lwAgp.mAgain, m[LW_ROUTE_HEAR], l[LW_ROUTE_HEAR], n[LW_ROUTE_HEAR], LW_SENSE_FAST / 1000.0, f[LW_ROUTE_HEAR],
                m[LW_ROUTE_FLAG], l[LW_ROUTE_FLAG], n[LW_ROUTE_FLAG], f[LW_ROUTE_FLAG], LW_SENSE_TRIES, g_lwAgp.mGiven, g_lwAgp.mGone,
                k[LW_SENSE_HOSTILE], k[LW_SENSE_STEALTH], k[LW_SENSE_BLOCKED], k[LW_SENSE_EARLY], k[LW_SENSE_LEVEL], k[LW_SENSE_TARGET], k[LW_SENSE_STATE], k[LW_SENSE_FLAGS],
                k[LW_SENSE_PLACE], k[LW_SENSE_DEAF], k[LW_SENSE_TAGGED], k[LW_SENSE_NOSLOT]);
        memset(g_lwAgp.mNoticed, 0, sizeof g_lwAgp.mNoticed); memset(g_lwAgp.mFast, 0, sizeof g_lwAgp.mFast); memset(g_lwAgp.mSkip, 0, sizeof g_lwAgp.mSkip);
        memset(g_lwAgp.mLeft, 0, sizeof g_lwAgp.mLeft); memset(g_lwAgp.mNoEffect, 0, sizeof g_lwAgp.mNoEffect);
        g_lwAgp.mLoader = g_lwAgp.mAgain = g_lwAgp.mGiven = g_lwAgp.mGone = 0; g_lwAgp.minuteAt = now;
        g_lwAgp.senseLines = g_lwAgp.skipLines = g_lwAgp.resultLines = 0;
    }
    g_lwAgp.nearSum += nearN; g_lwAgp.calmSum += calmN; g_lwAgp.loaderSum += loaderN; g_lwAgp.loaderFighting += loaderFightN; g_lwAgp.scans++;
    if (now - g_lwAgp.noteAt < 30000) return;
    double scans = g_lwAgp.scans ? (double)g_lwAgp.scans : 1.0;
    if (g_lwAgp.nearSum)
    {
        uintptr_t s = 0; uint8_t capOn = 0; float cap = 0;
        if (EvpPtr(g_lw.game + 0x3326340u, &s) && s) { SafeRead(s + 0x78AF7B0u, &capOn, 1); SafeRead(s + 0x78AF9DCu, &cap, 4); } /* 0x64C8A9 */
        char eyes[2][400];
        for (int c = 0; c < 2; c++)
        {
            const uint32_t* e = g_lwAgp.eyes[c];
            snprintf(eyes[c], sizeof eyes[c], "out of view %u, seen %u, blocked by the drawn ground %u, by the coarse collision over the tile %u, by a static object %u, static with no drawn ground %u, by foliage %u, by a squad mate %u, by another enemy %u, by another unit %u, no eye %u",
                e[LW_EYE_VIEW], e[LW_EYE_SEEN], e[LW_EYE_DRAWN], e[LW_EYE_COARSE], e[LW_EYE_OBJECT], e[LW_EYE_STATIC], e[LW_EYE_FOLIAGE], e[LW_EYE_MATE], e[LW_EYE_ENEMY],
                e[LW_EYE_UNIT], e[LW_EYE_NONE]);
        }
        Note("living world: aggro (30 s): %.1f enemies within %.0f m of a player on average, %.1f of them below alert 3; loader members %.1f, %.1f of them at alert 3+; %d units logged so far; sight cap %s %.1f m",
            g_lwAgp.nearSum / scans, (double)LW_AGP_NEAR, g_lwAgp.calmSum / scans, g_lwAgp.loaderSum / scans, g_lwAgp.loaderFighting / scans, g_lwAgp.lines, capOn ? "on" : "off", (double)cap);
        for (int c = 0; c < 2; c++)
            Note("living world: sight (30 s): %s, calm, own eye on the player: %s; noticed outside their view within %.0f m: %u; reached alert 3 from within %.0f m: %u, mean %.1f s, longest %.1f s",
                c ? "other units" : "loader members", eyes[c], (double)LW_SENSE_NEAR, g_lwAgp.sensed[c], (double)LW_AGP_TIMED, g_lwAgp.timedN[c], g_lwAgp.timedN[c] ? (double)(g_lwAgp.timedSum[c] / (float)g_lwAgp.timedN[c]) : 0.0,
                (double)g_lwAgp.timedMax[c]);
    }
    g_lwAgp.nearSum = g_lwAgp.calmSum = g_lwAgp.loaderSum = g_lwAgp.loaderFighting = g_lwAgp.scans = 0; g_lwAgp.noteAt = now;
    memset(g_lwAgp.eyes, 0, sizeof g_lwAgp.eyes); memset(g_lwAgp.sensed, 0, sizeof g_lwAgp.sensed); memset(g_lwAgp.timedN, 0, sizeof g_lwAgp.timedN);
    memset(g_lwAgp.timedSum, 0, sizeof g_lwAgp.timedSum); memset(g_lwAgp.timedMax, 0, sizeof g_lwAgp.timedMax);
}
