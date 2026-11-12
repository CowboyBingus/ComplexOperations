/* Far units' animation at a lower rate (HELLFORGE_LW_ANIMRATE=N / command "animrate N"; spec
 * ~/xl-work/static/living/levers-static3/animrate/final.md, KB lever-anim-motion-rate). Host-side enemies far from
 * every player skip the state machine update (exe 0x5530C0) and dynamic bones (0x54DAC0) on most kicks and run them
 * with the summed dt; the clip decoder keeps the root travel, one update late. The root delta getter (0x2029D0) hands
 * that travel out over the frames until the next take, so the motion step still moves the unit every frame. Entries
 * move only in the kick detour (0x555BB0), where no animation task of an earlier kick runs, and the workers find them
 * through hashes it publishes before the kick's tasks are readied. Build 25480438 only. This file: the table, its
 * lookups and the entry tests; living_world_animrate_hooks.c the detours; living_world_animrate_cmd.c the switches. */
#define LW_AR_MAX 512
#define LW_AR_HASH 1024
#define LW_AR_UNIT_VT 0x1676D80u
#define LW_AR_LOOKUP 0x9D8C0u
#define LW_AR_BOX_RET 0x5631CAu
enum { LW_AR_ACTIVE, LW_AR_LEAVE, LW_AR_LAST, LW_AR_HANDBACK, LW_AR_CARRY };
enum { LW_AR_S, LW_AR_V, LW_AR_H };
enum { LW_AR_BY_PLAYER, LW_AR_BY_UNIT, LW_AR_BY_HANDLE };
enum { LW_AR_CAM_NONE, LW_AR_CAM_NOFWD, LW_AR_CAM_VALID };
enum
{
    LW_AR_R_HEALTH, LW_AR_R_ALERT, LW_AR_R_NEAR, LW_AR_R_TIER, LW_AR_R_MOTION, LW_AR_R_MODE, LW_AR_R_AIRBORNE, LW_AR_R_ATTACHED,
    LW_AR_R_SEATED, LW_AR_R_LIFE, LW_AR_R_HELLDIVER, LW_AR_R_AVATAR, LW_AR_R_ROOTMODE, LW_AR_R_CAP, LW_AR_R_GONE, LW_AR_R_SWITCH,
    LW_AR_R_COUNT, LW_AR_R_NONE = -1
};
static const char* const g_lwArReason[LW_AR_R_COUNT] = {"health", "alert", "near", "tier", "motion", "mode", "airborne", "attached",
    "seated", "life", "helldiver", "avatar", "rootmode", "cap", "gone", "switch"};

typedef struct
{
    /* written only by the kick detour before the kick's tasks are readied; read by the worker detours */
    uintptr_t player, unit, blender; uint32_t handle; float useDt; volatile uint8_t run, pinned;
    /* written by the one thread that runs this player's task in a kick: counters only, summed by the kick detour */
    uint32_t updRun, updSkip, bonesRun, bonesSkip, boxSkip, stale; uint64_t updTicks, bonesTicks, evBytes;
    /* game thread only */
    __attribute__((aligned(64))) uint32_t id, motionIndex, runKick, stagger, enterKick, gaveKick;
    uint8_t state, tier, pending, offPrev, postOff, havePos, rootMotion, haveLast;
    int32_t health; float animAcc, useDtLast, cap, scale;
    float spread[3], spreadT, motionAcc, push[3], lastPos[3], pos[3], prevPos[3];
    double taken[3], given[3];
} LwArEntry;
typedef struct
{
    uint32_t kicks[2], kickFrames[3], offThread, camNone, camNoFwd, remoteCapped, enters, leaves[LW_AR_R_COUNT], drops, blocked;
    uint32_t updRun, updSkip, bonesRun, bonesSkip, stale, boxSkip, kCount; uint64_t updTicks, bonesTicks, evBytes, kTicks, kMax;
    uint32_t capLow, takes, staleTakes, handouts, motionOff, stepSkipped, stepTaken, posDropped, posPassed, postOther;
    uint32_t ctlBracketed, ctlPasses, census, censusDirty[2], censusAll[2];
    float capShortest, capRatioMax, motionAccMax; double consWorst, spreadWorst, travelDisp, travelGiven;
} LwArCounters;
typedef struct { int period, dry, step, post, ctl, pin, census, nearM, pxA, pxB; } LwArSet;
typedef struct { int state; float pos[3], fwd[2], cosHalf, tanHalf; } LwArCam;
typedef struct { int n; float pos[EVT_MAX_PLAYERS][3], fwd[EVT_MAX_PLAYERS][2]; uint8_t aim[EVT_MAX_PLAYERS]; } LwArRemote;
typedef struct { uintptr_t m, descs, input, integ, state; uint32_t local; } LwArMotionMgr;
typedef struct { uintptr_t key; int32_t idx; } LwArSlot;
typedef void (*LwArKickFn)(uintptr_t aw, const uint32_t* deps, uint32_t ndeps, float dt);
typedef void (*LwArUpdFn)(uintptr_t p, float dt, uintptr_t out);
typedef void (*LwArBonesFn)(uintptr_t p, float dt);
typedef void (*LwArMotionFn)(uintptr_t m, uint32_t i, float dt);
typedef float* (*LwArGetFn)(float* out, uint32_t handle);
typedef void (*LwArCtlFn)(uintptr_t aw, float dt, uintptr_t out);
typedef void (*LwArSetPosFn)(uint32_t handle, uint32_t node, const float* p);
typedef uintptr_t (*LwArBoxFn)(uintptr_t u, uint8_t* out, uint8_t flag);
typedef void (*LwArSgFn)(uintptr_t a, uintptr_t b, uint32_t n, uintptr_t d);
static struct
{
    LwArSet want, set; int wantCycle, cycle, lastPeriod, hooked, postIn, pinIn, censusIn, pinFirst, host, count, draining, counting;
    uintptr_t exe; DWORD tid; ULONGLONG hostAt, cycleAt, countFrom; uint32_t kick, stepHandle, frameKicks, phase; int inStep;
    LwArKickFn kickOrig; LwArUpdFn updOrig; LwArBonesFn bonesOrig; LwArMotionFn motionOrig; LwArGetFn getOrig;
    LwArCtlFn ctlOrig; LwArSetPosFn setOrig; LwArBoxFn boxOrig; LwArSgFn sgOrig;
    LwArEntry e[LW_AR_MAX];
    struct { uint32_t handle; ULONGLONG until; } block[64];
    LwArCounters c;
} g_lwAr;
static LwArSlot g_lwArHash[2][3][LW_AR_HASH];
static uint16_t g_lwArUsed[2][3][LW_AR_MAX]; static int g_lwArUsedN[2][3]; /* the slots each copy holds */
static volatile int g_lwArCur;

static DWORD LwArGameThread(void) { return g_lwFlat.gameThread ? g_lwFlat.gameThread : g_lwAr.tid; }
static int LwArOnGameThread(void) { DWORD t = LwArGameThread(); return t != 0 && GetCurrentThreadId() == t; }
static uintptr_t LwArGameWorld(void)
{
    uintptr_t g = 0, w = 0;
    return EvpPtr(g_lw.game + 0x3326340u, &g) && EvpPtr(g + 0x10E8, &w) ? w : 0;
}
static uintptr_t LwArGameAw(void) { uintptr_t w = LwArGameWorld(), aw = 0; return w && EvpPtr(w + 0x230050, &aw) ? aw : 0; }
static float LwArMotionDt(void) { uintptr_t c = 0; float dt = 0; return EvpPtr(g_lw.game + 0x3326348u, &c) && !SafeRead(c + 4, &dt, 4) ? dt : 0.0f; }

/* ---- the published hashes: key -> entry index ---- */
static uint32_t LwArSlotOf(uintptr_t key) { return (uint32_t)(((uint64_t)key * 0x9E3779B97F4A7C15ull) >> 54); }
static uintptr_t LwArKeyOf(const LwArEntry* e, int kind) { return kind == LW_AR_BY_PLAYER ? e->player : kind == LW_AR_BY_UNIT ? e->unit : e->handle; }
static int LwArFind(int kind, uintptr_t key)
{
    const LwArSlot* h = g_lwArHash[__atomic_load_n(&g_lwArCur, __ATOMIC_ACQUIRE)][kind];
    for (uint32_t s = LwArSlotOf(key), n = 0; n < LW_AR_HASH; n++, s = (s + 1) & (LW_AR_HASH - 1))
    {
        if (h[s].idx < 0) return -1;
        if (h[s].key == key) return h[s].idx;
    }
    return -1;
}
static int LwArByPlayer(uintptr_t p) { return g_lwAr.count ? LwArFind(LW_AR_BY_PLAYER, p) : -1; }
static int LwArByUnit(uintptr_t u) { return g_lwAr.count ? LwArFind(LW_AR_BY_UNIT, u) : -1; }
static int LwArByHandle(uint32_t h) { return g_lwAr.count ? LwArFind(LW_AR_BY_HANDLE, h) : -1; }
/* The inactive copy is rebuilt, then one aligned store makes it the one readers use. */
static void LwArRebuild(int copy, int kind)
{
    LwArSlot* h = g_lwArHash[copy][kind];
    uint16_t* used = g_lwArUsed[copy][kind];
    for (int u = 0; u < g_lwArUsedN[copy][kind]; u++) h[used[u]].idx = -1;
    for (int k = 0; k < g_lwAr.count; k++)
    {
        uintptr_t key = LwArKeyOf(&g_lwAr.e[k], kind);
        uint32_t s = LwArSlotOf(key);
        while (h[s].idx >= 0) s = (s + 1) & (LW_AR_HASH - 1);
        h[s].key = key; h[s].idx = k; used[k] = (uint16_t)s;
    }
    g_lwArUsedN[copy][kind] = g_lwAr.count;
}
static void LwArPublishHashes(void)
{
    static int ready;
    if (!ready++) for (size_t s = 0; s < sizeof g_lwArHash / sizeof g_lwArHash[0][0][0]; s++) (&g_lwArHash[0][0][0])[s].idx = -1;
    int next = !g_lwArCur;
    for (int kind = 0; kind < 3; kind++) LwArRebuild(next, kind);
    __atomic_store_n(&g_lwArCur, next, __ATOMIC_RELEASE);
}

/* Worker side: the player handed to the task still is the entry's unit (a new player can sit at a freed address).
 * Stock sm_update reads [[P+0]+0x30] first thing (0x553102), so these reads add no fault. */
static int LwArKeys(const LwArEntry* e, uintptr_t p)
{
    uintptr_t u = *(volatile uintptr_t*)p;
    return u == e->unit && *(volatile uintptr_t*)(p + 0x30) == e->blender && *(volatile uint32_t*)(u + 8) == e->handle;
}

/* ---- the unit behind a handle (game thread) ---- */
typedef struct { uintptr_t unit, blender, player; } LwArUnit;
static int LwArResolve(uint32_t handle, LwArUnit* r)
{
    uintptr_t u = handle ? ((uintptr_t (*)(uint32_t))(g_lwAr.exe + LW_AR_LOOKUP))(handle) : 0, vt = 0, pu = 0, ps = 0;
    uint32_t h = 0;
    if (u < 0x10000 || SafeRead(u, &vt, 8) || vt != g_lwAr.exe + LW_AR_UNIT_VT || SafeRead(u + 8, &h, 4) || h != handle ||
        !EvpPtr(u + 0x168, &r->blender) || !EvpPtr(u + 0x178, &r->player) || SafeRead(r->player, &pu, 8) || pu != u ||
        SafeRead(r->player + 0x30, &ps, 8) || ps != r->blender) return 0;
    r->unit = u;
    return 1;
}
static int LwArResolves(const LwArEntry* e)
{
    LwArUnit r;
    return LwArResolve(e->handle, &r) && r.unit == e->unit && r.blender == e->blender && r.player == e->player;
}

/* ---- the motion manager's records (game thread) ---- */
static int LwArMotionRead(LwArMotionMgr* mm)
{
    memset(mm, 0, sizeof *mm);
    uint32_t all = 0;
    if (!EvpPtr(g_lw.game + 0x3326558u, &mm->m) || SafeRead(mm->m + 0x4890, &mm->local, 4) || SafeRead(mm->m + 0x488C, &all, 4) ||
        !EvpPtr(mm->m + 0x48B8, &mm->descs) || !EvpPtr(mm->m + 0x48C0, &mm->input) || !EvpPtr(mm->m + 0x48C8, &mm->integ) ||
        !EvpPtr(mm->m + 0x48D8, &mm->state)) return 0;
    if (mm->local > all) mm->local = all;
    return 1;
}
static int LwArInstanceIs(const LwArMotionMgr* mm, uint32_t i, uint32_t id, uint32_t handle)
{
    uintptr_t d = 0;
    return i < mm->local && EvpPtr(mm->descs + 8ull * i, &d) && EvpU32(d + 8, 0) == id && EvpU32(d + 0xC, 0) == handle;
}
/* The instance index moves when the manager compacts; found again through its hash by entity. */
static int LwArInstance(const LwArMotionMgr* mm, LwArEntry* e)
{
    if (LwArInstanceIs(mm, e->motionIndex, e->id, e->handle)) return 1;
    uint32_t i = 0;
    if (!EvpHash(mm->m + 0x48A0, e->id, 8, 4, &i) || !LwArInstanceIs(mm, i, e->id, e->handle)) return 0;
    e->motionIndex = i;
    return 1;
}
static int LwArPosition(const LwArMotionMgr* mm, uint32_t i, float* p)
{
    if (SafeRead(mm->state + 0x2Cull * i, p, 12)) return 0;
    for (int a = 0; a < 3; a++) if (!isfinite(p[a]) || fabsf(p[a]) > 100000.0f) return 0;
    return 1;
}

/* Half of the shortest looping, used clip: one decode handles one wrap (0x5AEA2D), so a step longer than a lap loses
 * that lap's root travel. One-shot and pose clips do not wrap and are left out. */
static float LwArCap(uintptr_t s)
{
    uint32_t n = 0; uintptr_t cs = 0; float shortest = 1e9f;
    if (SafeRead(s + 0x98, &n, 4) || !EvpPtr(s + 0xA0, &cs)) return 0.0f;
    if (n > 64) n = 64;
    for (uint32_t k = 0; k < n; k++)
    {
        uint8_t r[0x38];
        if (SafeRead(cs + 0x58ull * k, r, sizeof r)) return 0.0f;
        uint32_t users; float len; memcpy(&users, r, 4); memcpy(&len, r + 0x30, 4);
        if (users >= 1 && r[0x34] != 0 && len > 0.001f && len < shortest) shortest = len;
    }
    if (shortest < 1e9f && shortest < g_lwAr.c.capShortest) g_lwAr.c.capShortest = shortest;
    return fminf(0.1f, shortest < 1e9f ? 0.5f * shortest : 0.1f);
}

/* ---- units that are never slowed ---- */
static int LwArAttached(uint32_t id)
{
    uintptr_t m = 0, recs = 0; uint32_t i = 0;
    return EvpPtr(g_lw.game + 0x3326698u, &m) && EvpHash(m + 0x18, id, 8, 4, &i) && EvpPtr(m + 0x38, &recs) && EvpU32(recs + 0xACull * i, 0) != 0;
}
static int LwArAvatar(uint32_t id)
{
    uintptr_t m = 0; uint32_t slot = 0;
    return EvpPtr(g_lw.game + 0x3326D20u, &m) && EvpHash(m + 0xF8, id, 8, 4, &slot);
}
static int LwArHelldiver(uint32_t handle)
{
    for (int i = 0; i < g_lwWorld.playersAt; i++) if (g_lwWorld.units[i] == handle) return 1;
    return 0;
}
/* Motion mode 4 and enabled; not launched, airborne, height blending (a float, 0x5A0E58) or waiting for a deferred
 * enable (byte +0x80, 0x59FE70). */
static int LwArMotionCalm(const LwArMotionMgr* mm, uint32_t i, uint8_t* rootMotion, float* scale)
{
    uint8_t st[0x2C], in[0x84]; uint32_t mode; float blend;
    if (SafeRead(mm->state + 0x2Cull * i, st, sizeof st) || SafeRead(mm->integ + 0x84ull * i, in, sizeof in)) return LW_AR_R_MOTION;
    memcpy(&mode, st + 0x1C, 4); memcpy(&blend, in + 0x10, 4); memcpy(scale, in + 0x74, 4);
    *rootMotion = st[0x21];
    if (mode != 4 || st[0x20] == 0) return LW_AR_R_MODE;
    return in[0xD] == 0 && in[0xF] == 0 && !(blend > 0.0f) && in[0x80] == 0 ? LW_AR_R_NONE : LW_AR_R_AIRBORNE;
}
/* The reason a unit may not be held, LW_AR_R_NONE when none. Health is tested every kick by the caller. */
static int LwArNeverSlow(const LwArMotionMgr* mm, LwArEntry* e)
{
    if (!LwArInstance(mm, e)) return LW_AR_R_MOTION;
    int calm = LwArMotionCalm(mm, e->motionIndex, &e->rootMotion, &e->scale);
    if (calm != LW_AR_R_NONE) return calm;
    if (LwArAttached(e->id)) return LW_AR_R_ATTACHED;
    if (LwSeated(e->id)) return LW_AR_R_SEATED;
    ActHealth h;
    if (!ActHealthOf(g_lw.game, e->id, &h) || h.life != 0) return LW_AR_R_LIFE;
    if (LwArHelldiver(e->handle)) return LW_AR_R_HELLDIVER;
    if (LwArAvatar(e->id)) return LW_AR_R_AVATAR;
    uint32_t mode = 1;
    if (SafeRead(e->blender + 0x280, &mode, 4) || mode != 0) return LW_AR_R_ROOTMODE; /* the getter returns 0 there (0x202A02) */
    return LW_AR_R_NONE;
}
static int32_t LwArHealth(uint32_t id) { ActHealth h; return ActHealthOf(g_lw.game, id, &h) ? h.health : INT32_MIN; }

/* ---- the views: the host camera read every kick (the loader's chain, LwViewRead, without its cache) ---- */
static void LwArCamera(LwArCam* c)
{
    uintptr_t cm = 0, cam = 0; float pose[12], fov = 0;
    memset(c, 0, sizeof *c);
    if (!EvpPtr(g_lw.game + 0x346D560u, &cm) || SafeRead(cm + 0xC, pose, sizeof pose) || !EvpPtr(cm, &cam) || SafeRead(cam + 0x34, &fov, 4) ||
        !(fov > 0.1f && fov < 3.0f) || SafeRead(cm + 0x3C, c->pos, 12)) { g_lwAr.c.camNone++; return; }
    c->state = LW_AR_CAM_NOFWD; c->tanHalf = tanf(0.5f * fov);
    float fx = pose[4], fy = pose[5], fl = sqrtf(fx * fx + fy * fy);
    int owner = -1; float best = 15.0f * 15.0f;
    for (int i = 0; i < g_lwWorld.playersAt; i++) { float d = LwDist2(g_lwWorld.players[i], c->pos[0], c->pos[1]); if (d < best) { best = d; owner = i; } }
    if (fl > 0.2f && owner >= 0)
    {
        float px = g_lwWorld.players[owner][0] - c->pos[0], py = g_lwWorld.players[owner][1] - c->pos[1], pl = sqrtf(px * px + py * py);
        fx /= fl; fy /= fl;
        if (pl > 0.2f && (px * fx + py * fy) / pl >= cosf(30.0f * 3.14159265f / 180.0f))
        {
            c->state = LW_AR_CAM_VALID; c->fwd[0] = fx; c->fwd[1] = fy;
            c->cosHalf = cosf(atanf(c->tanHalf * 2.4f) + 10.0f * 3.14159265f / 180.0f);
        }
    }
    if (c->state != LW_AR_CAM_VALID) g_lwAr.c.camNoFwd++;
}
/* Remote players' aims, once a kick: LwOffScreen's test for another machine's player (vertical FOV 90 deg, 2.4
 * aspect, 10 deg), with the aim read once instead of once an entry. */
static void LwArRemotes(LwArRemote* r)
{
    r->n = 0;
    for (int i = 0; i < g_lwWorld.playersAt && r->n < EVT_MAX_PLAYERS; i++)
    {
        if (g_lwWorld.local[i]) continue;
        memcpy(r->pos[r->n], g_lwWorld.players[i], 12);
        r->aim[r->n] = (uint8_t)LwAimForward(i, r->fwd[r->n]);
        r->n++;
    }
}
static int LwArInCone(const float* from, const float* fwd, float cosHalf, const float* p)
{
    float dx = p[0] - from[0], dy = p[1] - from[1], d = sqrtf(dx * dx + dy * dy);
    return !(d > 1.0f) || (dx * fwd[0] + dy * fwd[1]) / d >= cosHalf;
}
static int LwArRemoteSees(const LwArRemote* r, const float* p)
{
    static float cosHalf = 2.0f;
    if (cosHalf > 1.5f) cosHalf = cosf(atanf(tanf(0.5f * 1.5707963f) * 2.4f) + 10.0f * 3.14159265f / 180.0f);
    for (int i = 0; i < r->n; i++)
        if (LwDist2(r->pos[i], p[0], p[1]) < 200.0f * 200.0f && (!r->aim[i] || LwArInCone(r->pos[i], r->fwd[i], cosHalf, p))) return 1;
    return 0;
}

/* The tier at p, starting from tier `cur` (S for a candidate). Slower needs 0.85 of the threshold and 10 m past the
 * floor; faster at once. *why is the leave reason when the result is S. */
static float LwArNearest(const float* p)
{
    float dmin = 1e9f;
    for (int i = 0; i < g_lwWorld.playersAt; i++) dmin = fminf(dmin, sqrtf(LwDist2(g_lwWorld.players[i], p[0], p[1])));
    return dmin;
}
/* Projected height of a 2 m unit at 1440 px; 0 off screen or with an unreadable camera. An unvalidated forward
 * (first person, scope, death view) counts every unit as on screen. */
static float LwArHeight(const LwArCam* cam, const float* p)
{
    if (cam->state == LW_AR_CAM_NONE || (cam->state == LW_AR_CAM_VALID && !LwArInCone(cam->pos, cam->fwd, cam->cosHalf, p))) return 0.0f;
    float dx = p[0] - cam->pos[0], dy = p[1] - cam->pos[1], dz = p[2] - cam->pos[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
    return d > 0.5f ? 1440.0f / (d * cam->tanHalf) : 1e9f;
}
static int LwArSlower(int t, int cur, float h, float dmin)
{
    if (t <= cur || !(dmin > (float)g_lwAr.set.nearM + 10.0f)) return t < cur ? t : cur;
    int allowed = cur;
    if (allowed == LW_AR_S && h < 0.85f * (float)g_lwAr.set.pxA) allowed = LW_AR_V;
    if (allowed == LW_AR_V && h < 0.85f * (float)g_lwAr.set.pxB) allowed = LW_AR_H;
    return t < allowed ? t : allowed;
}
static int LwArTierAt(const LwArCam* cam, const LwArRemote* rv, const float* p, int cur, int* why)
{
    float dmin = LwArNearest(p), h = LwArHeight(cam, p);
    *why = LW_AR_R_NEAR;
    if (dmin < (float)g_lwAr.set.nearM) return LW_AR_S;
    int t = h >= (float)g_lwAr.set.pxA ? LW_AR_S : h >= (float)g_lwAr.set.pxB ? LW_AR_V : LW_AR_H;
    if (cam->state == LW_AR_CAM_NONE && t == LW_AR_H) t = LW_AR_V;
    t = LwArSlower(t, cur, h, dmin);
    if (t == LW_AR_H && LwArRemoteSees(rv, p)) { t = LW_AR_V; g_lwAr.c.remoteCapped++; }
    *why = LW_AR_R_TIER;
    return t;
}

/* ---- candidates: an eighth of the local instances a kick ---- */
static int LwArBlocked(uint32_t handle)
{
    ULONGLONG now = GetTickCount64();
    for (int b = 0; b < 64; b++) if (g_lwAr.block[b].handle == handle && g_lwAr.block[b].until > now) return 1;
    return 0;
}
static void LwArBlock(uint32_t handle)
{
    ULONGLONG now = GetTickCount64(); int at = 0;
    for (int b = 0; b < 64; b++) if (g_lwAr.block[b].until <= now || g_lwAr.block[b].handle == handle) { at = b; break; }
    g_lwAr.block[at].handle = handle; g_lwAr.block[at].until = now + 2000;
}
static int LwArHeld(uint32_t handle, int from)
{
    if (LwArByHandle(handle) >= 0) return 1;
    for (int k = from; k < g_lwAr.count; k++) if (g_lwAr.e[k].handle == handle) return 1;
    return 0;
}
typedef struct { const LwArCam* cam; const LwArRemote* rv; const LwArMotionMgr* mm; float dt; int late, from; } LwArPick;
/* Every test an entry gets, cheapest first. 1 with the entry filled and added. */
static int LwArTry(const LwArPick* pk, uint32_t i, uint32_t id, uint32_t handle)
{
    LwArEntry n; int why;
    memset(&n, 0, sizeof n);
    n.id = id; n.handle = handle; n.motionIndex = i;
    if (!LwArPosition(pk->mm, i, n.pos) || (n.tier = (uint8_t)LwArTierAt(pk->cam, pk->rv, n.pos, LW_AR_S, &why)) == LW_AR_S) return 0;
    LwArUnit r;
    if (!LwArResolve(handle, &r)) return 0;
    n.unit = r.unit; n.blender = r.blender; n.player = r.player;
    if (LwAlert(id) || LwArNeverSlow(pk->mm, &n) != LW_AR_R_NONE || (n.health = LwArHealth(id)) == INT32_MIN) return 0;
    n.cap = LwArCap(r.blender);
    if (n.cap < 2.5f * pk->dt) { g_lwAr.c.capLow++; return 0; }
    n.state = LW_AR_ACTIVE; n.pending = (uint8_t)!pk->late; n.runKick = g_lwAr.kick - 1; n.enterKick = g_lwAr.kick;
    n.useDt = n.useDtLast = pk->dt; n.stagger = handle & 7; memcpy(n.prevPos, n.pos, 12);
    g_lwAr.e[g_lwAr.count++] = n;
    g_lwAr.c.enters++;
    return 1;
}
static void LwArFindCandidates(const LwArPick* pk)
{
    for (uint32_t i = g_lwAr.kick & 7; i < pk->mm->local && g_lwAr.count < LW_AR_MAX; i += 8)
    {
        uintptr_t d = 0; uint32_t rec[2];
        if (!EvpPtr(pk->mm->descs + 8ull * i, &d) || SafeRead(d + 8, rec, 8) || !rec[0] || !rec[1] || LwArHeld(rec[1], pk->from)) continue;
        if (LwArBlocked(rec[1])) { g_lwAr.c.blocked++; continue; }
        LwArTry(pk, i, rec[0], rec[1]);
    }
}

#include "living_world_animrate_hooks.c" /* the detours */
#include "living_world_animrate_cmd.c"   /* switches, hooks, notes */
