/* Host only: a bounded log of enemies near a player that stay calm, and what their eyes see of that player.
 * The game's sight pass (0x887270 -> 0x886490) asks per eye whether the target is in view (0x64CF30 -> 0x64C6F0) and,
 * when it is, casts the eye's ray (0x175CA60, run by 0x175C5C0 through [[game+0x3326328]+0x68]); 0x64D1D0 then reads
 * the hits. The probe repeats both for the player's target point, synchronously, so the log names what stops a ray. */
#define LW_AGP_NEAR 25.0f   /* m from a player */
#define LW_AGP_TIMED 20.0f  /* m: engagement is timed from the first sample this close */
#define LW_AGP_CALM 3000u   /* ms near and below alert 3 before a unit is logged */
#define LW_AGP_UNITS 64
#define LW_AGP_LINES 24     /* unit lines a session */
#define LW_AGP_FIGHTING 6   /* of them, units already at alert 3 or more */
#define LW_AGP_EYE_LINES 160 /* eye lines a session, under the unit lines */
#define LW_AGP_TIME_LINES 40
#define LW_AGP_LOOKS 6      /* units whose own eye is checked per probe (twice a second) */
#define LW_FOLIAGE_HIT 0x4B8DC02Bu
#define LW_HIT 0x2C         /* a ray hit: +0 x, +4 y, +0x18 metres along the ray, +0x1C unit (0 static), +0x24 type */

enum { LW_EYE_NONE, LW_EYE_VIEW, LW_EYE_SEEN, LW_EYE_DRAWN, LW_EYE_COARSE, LW_EYE_OBJECT, LW_EYE_STATIC, LW_EYE_FOLIAGE, LW_EYE_UNIT, LW_EYE_N };
static const char* const kLwEyeWhat[LW_EYE_N] = {"no eye", "out of view", "seen", "the drawn ground", "the coarse collision over the tile",
    "a static object", "static (no drawn ground here)", "foliage", "another unit"};

typedef struct { uint32_t unit; ULONGLONG since, seen, closeAt; uint8_t logged, timed; } LwAgpUnit;
static struct
{
    LwAgpUnit u[LW_AGP_UNITS];
    int lines, fighting, eyeLines, timeLines, pins, look;
    ULONGLONG nextAt, noteAt;
    uint32_t nearSum, calmSum, loaderSum, loaderFighting, scans; /* since the last note */
    uint32_t eyes[2][LW_EYE_N], timedN[2]; float timedSum[2], timedMax[2]; /* [0] loader members, [1] other units */
} g_lwAgp;

/* What an eye made of the target: 0x64C6F0's answer (code 0 out of view, 1 near, 2 far; +4 distance, +8 range) and,
 * when it asked for one, the ray's hits as 0x64D1D0 reads them. */
typedef struct
{
    int what, at; uint32_t code, hits, unit, type, shape; float view[3], base, eye[3], dist, hitDist, hit[3], drawn, coarse, top; int tile;
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

/* The point an eye looks at on `avatar`: its sight-target registry entry's node ([game+0x3326CB8], as 0x886519). */
static int LwTargetPoint(uint32_t avatar, float* p)
{
    uintptr_t reg = 0, infos = 0, info = 0, nodes = 0; uint32_t t = 0;
    if (!EvpPtr(g_lw.game + 0x3326CB8u, &reg) || !EvpHash(reg + 0x12050, avatar, 8, 4, &t) || t >= 4096 || !EvpPtr(reg + 0x12068, &infos) ||
        !EvpPtr(infos + 8ull * t, &info) || EvpU32(info + 8, 0) != avatar || !EvpPtr(reg + 0x12070, &nodes)) return 0;
    uint32_t unit = EvpU32(info + 0xC, 0), node = EvpU32(nodes + 0x60ull * t, 0);
    if (!g_lwEye.valid(unit)) return 0;
    memcpy(p, g_lwEye.pos(unit, node), 12);
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]);
}

/* One eye (a unit in the eye manager [game+0x3326520]) against `avatar`'s point `tpos`, as a calm unit asks (scale 1.0,
 * flag 0; 0x886635); `ground` adds the ground ray's top under a static hit. */
static void LwEyeLookAt(uint32_t eyeUnit, uint32_t avatar, const float* tpos, int ground, LwEyeLook* r)
{
    typedef void (*ViewFn)(uintptr_t mgr, void* out, uint32_t eye, const float* pos, uint32_t target, float scale, uint8_t flag);
    typedef uintptr_t (*SettingsFn)(uintptr_t info);
    memset(r, 0, sizeof *r); r->at = -1; r->tile = -1; r->drawn = r->coarse = r->top = -9999.0f;
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
    if (r->code == 0 || !(r->dist > 1e-6f) || rayHandle == 0) return; /* no ray: the entry is not visible (0x64D0EA) */
    for (int k = 0; k < 3; k++) d[k] /= r->dist;
    uint8_t hits[8 * LW_HIT] = {0};
    r->hits = g_lwEye.ray(rayHandle, r->eye, d, r->dist, handle, hits, 8);
    if (r->hits > 8) r->hits = 8;
    uintptr_t set = ((SettingsFn)(g_lw.game + 0x503BC0u))(info);
    float nearR = 0, folR = 0;
    if (set) { SafeRead(set + 0x24, &nearR, 4); SafeRead(set + 0x28, &folR, 4); }
    if (LwSightVerdict(hits, r->hits, avatar, r->eye, tpos, nearR, folR, &r->at)) { r->what = LW_EYE_SEEN; return; }
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

static void LwEyeLine(uint32_t unit, uint32_t eyeUnit, uint32_t bit, const LwEyeLook* r, const float* tpos)
{
    char ray[300] = "";
    if (r->code && r->what != LW_EYE_SEEN && r->at >= 0)
        snprintf(ray, sizeof ray, "; ray %u hits, hit %d stops it at %.2f m of %.2f: unit %08X, type %08X, at z %.2f (drawn ground %.2f, tile %d, coarse rows %.2f, ground ray top %.2f)",
            r->hits, r->at, (double)r->hitDist, (double)r->dist, r->unit, r->type, (double)r->hit[2], (double)r->drawn, r->tile, (double)r->coarse, (double)r->top);
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
    float tpos[3];
    if (!r || g_lwAgp.pins <= 0 || !LwTargetPoint(avatar, tpos)) return;
    uint32_t count = EvpU32(r, 0);
    for (uint32_t k = 0; k < count && k < 16 && g_lwAgp.eyeLines < LW_AGP_EYE_LINES; k++)
    {
        uint32_t eyeUnit = EvpU32(r + 4 + 8ull * k, 0), bit = EvpU32(r + 8 + 8ull * k, 0);
        LwEyeLook look; LwEyeLookAt(eyeUnit, avatar, tpos, 1, &look);
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

/* Twice a second: the AI units the host owns ([game+0x33266C8]: owned +0x2324, entity pointers +0x2348) near a player. */
static void LwAggroProbe(void)
{
    ULONGLONG now = GetTickCount64();
    if (now < g_lwAgp.nextAt || g_lwWorld.playersAt <= 0) return;
    g_lwAgp.nextAt = now + 500;
    uintptr_t ai = 0, ents = 0;
    if (!EvpPtr(g_lw.game + 0x33266C8u, &ai) || !EvpPtr(ai + 0x2348, &ents)) return;
    int eyesOn = LwEyeBind(), looks = 0;
    float tpos[EVT_MAX_PLAYERS][3]; uint8_t tposOk[EVT_MAX_PLAYERS] = {0};
    for (int k = 0; eyesOn && k < g_lwWorld.playersAt && k < EVT_MAX_PLAYERS; k++) tposOk[k] = (uint8_t)LwTargetPoint(g_lwWorld.avatars[k], tpos[k]);
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
        if (slot < 0) { if (empty < 0) continue; slot = empty; g_lwAgp.u[slot] = (LwAgpUnit){.unit = unit, .since = now, .timed = (uint8_t)fighting}; }
        LwAgpUnit* a = &g_lwAgp.u[slot];
        if (!fighting && !a->closeAt && best <= LW_AGP_TIMED * LW_AGP_TIMED) a->closeAt = now;
        if (fighting && !a->timed) { a->timed = 1; if (a->closeAt) LwAgpTimed(a, loader, sqrtf(best), now); }
        /* its own eye, a few units a probe in turn: what stops a calm unit seeing the player */
        if (!fighting && eyesOn && tposOk[player] && looks < LW_AGP_LOOKS && (i + (uint32_t)g_lwAgp.look) % 3 == 0)
        {
            LwEyeLook look; LwEyeLookAt(unit, g_lwWorld.avatars[player], tpos[player], 0, &look);
            g_lwAgp.eyes[!loader][look.what]++; looks++;
        }
        if (fighting && a->logged != 2) a->since = now; /* calm time counts from its last fight */
        a->seen = now;
        if (g_lwAgp.lines >= LW_AGP_LINES || a->logged) continue;
        if (fighting ? g_lwAgp.fighting >= LW_AGP_FIGHTING : now - a->since < LW_AGP_CALM) continue;
        a->logged = fighting ? 2 : 1; g_lwAgp.lines++; g_lwAgp.fighting += fighting;
        LwAgpLine(unit, sqrtf(best), player);
    }
    g_lwAgp.look++;
    g_lwAgp.nearSum += nearN; g_lwAgp.calmSum += calmN; g_lwAgp.loaderSum += loaderN; g_lwAgp.loaderFighting += loaderFightN; g_lwAgp.scans++;
    if (now - g_lwAgp.noteAt < 30000) return;
    double scans = g_lwAgp.scans ? (double)g_lwAgp.scans : 1.0;
    if (g_lwAgp.nearSum)
    {
        uintptr_t s = 0; uint8_t capOn = 0; float cap = 0;
        if (EvpPtr(g_lw.game + 0x3326340u, &s) && s) { SafeRead(s + 0x78AF7B0u, &capOn, 1); SafeRead(s + 0x78AF9DCu, &cap, 4); } /* 0x64C8A9 */
        char eyes[2][300];
        for (int c = 0; c < 2; c++)
        {
            const uint32_t* e = g_lwAgp.eyes[c];
            snprintf(eyes[c], sizeof eyes[c], "out of view %u, seen %u, blocked by the drawn ground %u, by the coarse collision over the tile %u, by a static object %u, static with no drawn ground %u, by foliage %u, by another unit %u, no eye %u",
                e[LW_EYE_VIEW], e[LW_EYE_SEEN], e[LW_EYE_DRAWN], e[LW_EYE_COARSE], e[LW_EYE_OBJECT], e[LW_EYE_STATIC], e[LW_EYE_FOLIAGE], e[LW_EYE_UNIT], e[LW_EYE_NONE]);
        }
        Note("living world: aggro (30 s): %.1f enemies within %.0f m of a player on average, %.1f of them below alert 3; loader members %.1f, %.1f of them at alert 3+; %d units logged so far; sight cap %s %.1f m",
            g_lwAgp.nearSum / scans, (double)LW_AGP_NEAR, g_lwAgp.calmSum / scans, g_lwAgp.loaderSum / scans, g_lwAgp.loaderFighting / scans, g_lwAgp.lines, capOn ? "on" : "off", (double)cap);
        for (int c = 0; c < 2; c++)
            Note("living world: sight (30 s): %s, calm, own eye on the player: %s; reached alert 3 from within %.0f m: %u, mean %.1f s, longest %.1f s",
                c ? "other units" : "loader members", eyes[c], (double)LW_AGP_TIMED, g_lwAgp.timedN[c], g_lwAgp.timedN[c] ? (double)(g_lwAgp.timedSum[c] / (float)g_lwAgp.timedN[c]) : 0.0,
                (double)g_lwAgp.timedMax[c]);
    }
    g_lwAgp.nearSum = g_lwAgp.calmSum = g_lwAgp.loaderSum = g_lwAgp.loaderFighting = g_lwAgp.scans = 0; g_lwAgp.noteAt = now;
    memset(g_lwAgp.eyes, 0, sizeof g_lwAgp.eyes); memset(g_lwAgp.timedN, 0, sizeof g_lwAgp.timedN);
    memset(g_lwAgp.timedSum, 0, sizeof g_lwAgp.timedSum); memset(g_lwAgp.timedMax, 0, sizeof g_lwAgp.timedMax);
}
