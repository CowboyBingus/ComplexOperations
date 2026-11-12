/* Dead-ragdoll pair filter (HELLFORGE_LW_PAIRS=N on every peer, command "pairs N"; "pairground L"). N is a mask:
 * 1 drops a dead bone against a bone of another dead unit, 2 a dead bone against a static body that is not the height
 * field, 4 two bones of one dead unit, 16 rebuilds the active bones of each new dead unit, 32 counts and drops nothing.
 * The broad phase proposes a pair once, when two boxes start to overlap (exe 0xD3D440), and the group filter 0xCE5A10
 * decides; a dropped pair has no contact until the boxes part. Ground and trigger-material pairs are always kept.
 * Every switch hands dropped pairs back through Havok's own rebuild (command 0x1C, writer slot +0x180), queued in the
 * tick and replayed in this frame's step before its broad phase (0x79EF69). The value must match on every peer:
 * RagdollSync carries the owner's poses (KB fight-ragdoll-constraints). */
#define LW_PF_SET 1024u      /* open-addressed unit set, a power of two */
#define LW_PF_MAP (1u << 20) /* one byte a body slot; world 2 holds 1,048,576 */
#define LW_PF_WALK 1024u     /* active-list entries a frame */
#define LW_PF_HEALTH 8192u   /* health records read a frame at most */
#define LW_PF_FRESH 64u
enum { LW_PF_DEAD = 1, LW_PF_STATIC = 2, LW_PF_SELF = 4, LW_PF_WALKDEATH = 16, LW_PF_COUNT = 32 };
#define LW_PF_DROPS (LW_PF_DEAD | LW_PF_STATIC | LW_PF_SELF)
#define LW_PF_VALID (LW_PF_DROPS | LW_PF_WALKDEATH | LW_PF_COUNT)
enum { PF_DEADSEEN, PF_SELFSEEN, PF_STATICSEEN, PF_GROUND, PF_LIVE, PF_OTHER, PF_TRIGGER, PF_BEYOND, PF_NOMAT,
       PF_DROPDEAD, PF_DROPSELF, PF_DROPSTATIC, PF_C };
typedef int (*LwPfFilterFn)(void* filter, uintptr_t world, uint32_t* pairs, int n);
typedef void (*LwPfQueueFn)(uintptr_t writer, uint32_t body, uint8_t mode);
typedef struct { uint32_t unit[LW_PF_SET], n; } LwPfSet;
static struct
{
    int installed, asked, want, force; volatile int mode; uintptr_t exe; void* orig; LwPfQueueFn queue;
    volatile uintptr_t world; uintptr_t writer;   /* W and D+0x18 of world 2; the map belongs to this pair */
    LwPfSet set[2]; volatile LONG cur;            /* the published set is set[cur & 1] */
    uint32_t avatar[16], avatarN, avatarAt;       /* units seen as Helldivers: never in the set */
    uint8_t keep[16];                             /* filter layers treated as ground */
    uint8_t* map; volatile LONG dirty;            /* bodies whose pair was dropped */
    uint32_t fresh[LW_PF_FRESH], freshN, walk[LW_PF_FRESH], walkN, walkAt; int walking, walkAll, freshOver;
    volatile LONG inFrame, overlap, calls, pairsIn, n[PF_C], layerSeen[128], layerKeep[128];
    uint32_t rebuilt, walks, born, nowHealth, nowRing, nowDisposal, setMax, healthCut, setFull;
    uint32_t gSampled, gOn, gOver, gUnder, guarded, winSampled, winUnder; float worstUnder;
    double walkMs, setMs; ULONGLONG notedAt, groundAt, winAt, gateAt;
} g_lwPf;

static uint32_t LwPfU32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static const uint8_t* LwPfPtr(const uint8_t* p) { const uint8_t* v; memcpy(&v, p, sizeof v); return v; }
static double LwPfMs(LARGE_INTEGER a)
{
    LARGE_INTEGER b, f; QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f);
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}
static int LwPfIn(const uint32_t* list, uint32_t n, uint32_t unit)
{
    for (uint32_t i = 0; i < n; i++) if (list[i] == unit) return 1;
    return 0;
}
/* The unit's slot in the set, or the empty slot where it would go; LW_PF_SET when neither is found. */
static uint32_t LwPfSlot(const LwPfSet* s, uint32_t unit)
{
    uint32_t h = (unit * 0x9E3779B1u) >> 22;
    for (uint32_t k = 0; k < LW_PF_SET; k++, h = (h + 1) & (LW_PF_SET - 1)) if (s->unit[h] == unit || s->unit[h] == 0) return h;
    return LW_PF_SET;
}
static int LwPfDead(const LwPfSet* s, uint32_t unit)
{
    uint32_t h = unit ? LwPfSlot(s, unit) : LW_PF_SET;
    return h < LW_PF_SET && s->unit[h] == unit;
}

/* ---- the detour, on the broad-phase threads ---- */

/* Kind 3 actor id: a ragdoll bone (0x79678B, 0x7967C3; bones pass flags 3 at 0x7A300E). */
static int LwPfBone(const uint8_t* b) { return ((LwPfU32(b + 0x90) >> 28) & 3u) == 3u; }
/* A dead side: not static (+0x44 bit 0), a bone, its unit (+0x94) in the set. */
static int LwPfDeadBone(const LwPfSet* s, const uint8_t* b) { return !(LwPfU32(b + 0x44) & 1u) && LwPfBone(b) && LwPfDead(s, LwPfU32(b + 0x94)); }
/* Any non-zero trigger byte of the body's material (0xDD6EDF: u16 +0x6A, 0x50 a material, byte +0x10). */
static int LwPfTrig(const uint8_t* mt, const uint8_t* b) { uint16_t m; memcpy(&m, b + 0x6A, 2); return mt[(size_t)m * 0x50u + 0x10u] != 0; }
/* Terrain: a height-field shape (0x0D at +0x1A, 0xD12C0E; dispatch 4 at +0x1C, 0x786716), through a LOD wrapper's
 * child 0 (0x13, 0xDD6FB1), or a filter layer the test marked as walkable. */
static int LwPfGround(const uint8_t* body)
{
    const uint8_t* sh = LwPfPtr(body + 0x60);
    if (sh && sh[0x1A] == 0x13) sh = LwPfPtr(sh + 0x40);
    if (sh && (sh[0x1A] == 0x0D || sh[0x1C] == 4)) return 1;
    uint32_t l = LwPfU32(body + 0x6C) & 0x7Fu;
    return (g_lwPf.keep[l >> 3] >> (l & 7)) & 1;
}

/* Both dead sides in the map (0 for a side that is not dead); 0, nothing marked, when one is past it. */
static int LwPfMark(uint32_t ix, uint32_t iy)
{
    if (ix >= LW_PF_MAP || iy >= LW_PF_MAP) return 0;
    if (ix && !g_lwPf.map[ix]) g_lwPf.map[ix] = 1;
    if (iy && !g_lwPf.map[iy]) g_lwPf.map[iy] = 1;
    return 1;
}
static int LwPfDropClass(int drop) { return drop == LW_PF_SELF ? PF_DROPSELF : drop == LW_PF_DEAD ? PF_DROPDEAD : PF_DROPSTATIC; }

typedef struct { const uint8_t* bodies; const uint8_t* mt; const LwPfSet* s; int drops, marked; LONG c[PF_C]; } LwPfCall;

/* The class of a pair with at least one dead side, counted; the drop bit that class asks for. */
static int LwPfClass(LwPfCall* k, const uint8_t* x, const uint8_t* y, int dx, int dy)
{
    if (dx && dy)
    {
        int self = LwPfU32(x + 0x94) == LwPfU32(y + 0x94);
        k->c[self ? PF_SELFSEEN : PF_DEADSEEN]++;
        return self ? LW_PF_SELF : LW_PF_DEAD;
    }
    const uint8_t* o = dx ? y : x;
    uint32_t uo = LwPfU32(o + 0x94), l = LwPfU32(o + 0x6C) & 0x7Fu;
    if (!(LwPfU32(o + 0x44) & 1u)) { k->c[uo && !LwPfDead(k->s, uo) ? PF_LIVE : PF_OTHER]++; return 0; }
    if (LwPfGround(o)) { k->c[PF_GROUND]++; InterlockedIncrement(&g_lwPf.layerKeep[l]); return 0; }
    k->c[PF_STATICSEEN]++; InterlockedIncrement(&g_lwPf.layerSeen[l]);
    return LW_PF_STATIC;
}

/* 1 when the pair is dropped. A dropped dead side goes in the map so a switch can hand it back; past the map, kept. */
static int LwPfPair(LwPfCall* k, const uint32_t* p)
{
    uint32_t ix = p[0] & 0xFFFFFFu, iy = p[1] & 0xFFFFFFu; /* both checked by the original (0xCE5A50-0xCE5AC7) */
    const uint8_t* x = k->bodies + (size_t)ix * 0xA0u; const uint8_t* y = k->bodies + (size_t)iy * 0xA0u;
    int dx = LwPfDeadBone(k->s, x), dy = LwPfDeadBone(k->s, y);
    if (!dx && !dy) return 0;
    int drop = k->drops & LwPfClass(k, x, y, dx, dy);
    if (!drop) return 0;
    if (k->mt == NULL) { k->c[PF_NOMAT]++; return 0; }
    if (LwPfTrig(k->mt, x) || LwPfTrig(k->mt, y)) { k->c[PF_TRIGGER]++; return 0; }
    if (!LwPfMark(dx ? ix : 0, dy ? iy : 0)) { k->c[PF_BEYOND]++; return 0; }
    k->c[LwPfDropClass(drop)]++;
    k->marked = 1;
    return 1;
}

/* hknpGroupCollisionFilter::filterBodyPairs (vtable 0x148B938 slot +0x18): compacts kept pairs in place and returns
 * their count, which every caller stores (0xD3D533, 0xD3BCAE, 0xD3F2D9). The game's decision comes first. */
static int LwPfPairs(void* filter, uintptr_t world, uint32_t* pairs, int n)
{
    int kept = ((LwPfFilterFn)g_lwPf.orig)(filter, world, pairs, n);
    int mode = g_lwPf.mode;
    if (mode == 0 || kept <= 0 || world != g_lwPf.world) return kept;
    if (g_lwPf.inFrame) InterlockedIncrement(&g_lwPf.overlap); /* must stay 0: the tick runs before the step */
    LwPfCall k = {0};
    k.s = &g_lwPf.set[g_lwPf.cur & 1];
    if (k.s->n == 0) return kept;
    k.drops = (mode & LW_PF_COUNT) ? 0 : (mode & LW_PF_DROPS);
    k.bodies = LwPfPtr((const uint8_t*)world + 0x38);
    const uint8_t* mm = LwPfPtr((const uint8_t*)world + 0xB00); /* 0xDD6E2A, 0xDD6E43 */
    k.mt = mm ? LwPfPtr(mm + 0x48) : NULL;
    int out = 0;
    for (int i = 0; i < kept; i++)
        if (!LwPfPair(&k, pairs + 2 * i)) { uint64_t v; memcpy(&v, pairs + 2 * i, 8); memcpy(pairs + 2 * out++, &v, 8); }
    if (k.marked) g_lwPf.dirty = 1;
    InterlockedIncrement(&g_lwPf.calls); InterlockedExchangeAdd(&g_lwPf.pairsIn, kept);
    for (int c = 0; c < PF_C; c++) if (k.c[c]) InterlockedExchangeAdd(&g_lwPf.n[c], k.c[c]);
    return out;
}

/* ---- the set, on the game thread before the step ---- */

static void LwPfAvatar(uint32_t unit)
{
    if (!unit || LwPfIn(g_lwPf.avatar, g_lwPf.avatarN, unit)) return;
    g_lwPf.avatar[g_lwPf.avatarAt++ & 15u] = unit; /* sticky; the oldest goes */
    if (g_lwPf.avatarN < 16) g_lwPf.avatarN++;
}
/* A dead unit into the back buffer; one absent from the front buffer is new, for the death walk. */
static void LwPfAdd(LwPfSet* b, const LwPfSet* front, uint32_t unit)
{
    if (!unit || LwPfIn(g_lwPf.avatar, g_lwPf.avatarN, unit)) return;
    uint32_t h = LwPfSlot(b, unit);
    if (h < LW_PF_SET && b->unit[h] == unit) return;
    if (h >= LW_PF_SET || b->n >= LW_PF_SET * 3 / 4) { g_lwPf.setFull++; return; }
    b->unit[h] = unit; b->n++;
    if (LwPfDead(front, unit)) return;
    g_lwPf.born++;
    if (g_lwPf.freshN < LW_PF_FRESH) g_lwPf.fresh[g_lwPf.freshN++] = unit;
    else g_lwPf.freshOver = 1; /* the next death walk takes every unit of the set */
}
static void LwPfAvatars(void)
{
    EvpPlayer players[EVT_MAX_PLAYERS]; ActHealth h;
    int count = EvpPlayers(g_lw.game, players);
    for (int i = 0; i < count; i++) if (players[i].avatar && ActHealthOf(g_lw.game, players[i].avatar, &h)) LwPfAvatar(h.unit);
}
/* Health records (ActHealthOf's layout): life >= 2 is dead, the dying phase included, before the ring has it. */
static void LwPfHealth(LwPfSet* b, const LwPfSet* front)
{
    uintptr_t m = 0, recs = 0, ptrs = 0;
    if (!EvpPtr(g_lw.game + EVP_HEALTH, &m) || !EvpPtr(m + 0x1058, &recs) || !EvpPtr(m + 0x1048, &ptrs)) return;
    uint32_t live = EvpU32(m + 0x1020, 0), n = live < LW_PF_HEALTH ? live : LW_PF_HEALTH;
    if (live > LW_PF_HEALTH) g_lwPf.healthCut++;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t life = 0, unit; uint64_t res; uintptr_t dsc; uint8_t d[24];
        if (SafeRead(recs + (uintptr_t)i * 0x1B8 + 0x19C, &life, 4) || life < 2) continue;
        if (!EvpPtr(ptrs + (uintptr_t)i * 8, &dsc) || SafeRead(dsc, d, sizeof d)) continue;
        memcpy(&res, d, 8); memcpy(&unit, d + 12, 4);
        if (res == ACT_AVATAR) { LwPfAvatar(unit); continue; }
        g_lwPf.nowHealth++; LwPfAdd(b, front, unit);
    }
}
/* The corpse service's ring (state 1 dying, 2 corpse) and disposal list, as LwPoFrame reads them. */
static void LwPfRing(LwPfSet* b, const LwPfSet* front)
{
    uintptr_t svc = 0; uint32_t e[3], disposal = 0, unit;
    if (SafeRead(g_lw.game + 0x346D500u, &svc, 8) || !svc) return;
    for (int i = 0; i < 32; i++)
        if (!SafeRead(svc + 0x18 + 0x40u * (uintptr_t)i, e, sizeof e) && e[0] && (e[2] == 1 || e[2] == 2)) { g_lwPf.nowRing++; LwPfAdd(b, front, e[0]); }
    if (SafeRead(svc + 0x818, &disposal, 4)) return;
    for (uint32_t i = 0; i < disposal && i < 128; i++)
        if (!SafeRead(svc + 0x820 + 0x40u * (uintptr_t)i, &unit, 4) && unit) { g_lwPf.nowDisposal++; LwPfAdd(b, front, unit); }
}
/* The back buffer, built and published; empty outside a mission, where the detour then keeps the game's result. */
static void LwPfBuildSet(int mission)
{
    LARGE_INTEGER a; QueryPerformanceCounter(&a);
    LwPfSet* b = &g_lwPf.set[(g_lwPf.cur & 1) ^ 1]; const LwPfSet* front = &g_lwPf.set[g_lwPf.cur & 1];
    memset(b, 0, sizeof *b);
    g_lwPf.nowHealth = g_lwPf.nowRing = g_lwPf.nowDisposal = 0;
    if (mission)
    {
        LwPfAvatars();
        LwPfHealth(b, front);
        LwPfRing(b, front);
    }
    if (b->n > g_lwPf.setMax) g_lwPf.setMax = b->n;
    InterlockedExchange(&g_lwPf.cur, g_lwPf.cur ^ 1); /* full barrier: the step's graph is submitted after it */
    g_lwPf.setMs += LwPfMs(a);
}

/* ---- the death walk and the restore: Havok's rebuild, queued ---- */

/* Active bones of set units get Havok's rebuild (0xDD0070 via the queue 0x81EA30). walkAll: every unit of the set
 * (a switch, or more new units than walk[] holds); else the units in walk[]. Never static bodies. */
static void LwPfWalkStart(int all)
{
    g_lwPf.walkAll = all || g_lwPf.freshOver; g_lwPf.walkN = all ? 0 : g_lwPf.freshN;
    memcpy(g_lwPf.walk, g_lwPf.fresh, g_lwPf.walkN * 4); g_lwPf.freshN = 0; g_lwPf.freshOver = 0;
    g_lwPf.walkAt = 0; g_lwPf.walking = 1; g_lwPf.walks++;
}
/* One active-list entry: queued when it is a live slot's non-static bone of a unit the walk wants. */
static void LwPfWalkOne(uintptr_t list, uintptr_t bodies, uint32_t count, const LwPfSet* s)
{
    uint32_t id = 0, b[10];
    if (SafeRead(list + 4ull * g_lwPf.walkAt, &id, 4) || (id & 0xFFFFFFu) >= count) return;
    if (SafeRead(bodies + 0xA0ull * (id & 0xFFFFFFu) + 0x70, b, sizeof b) || b[0] != id) return; /* +0x70 id .. +0x94 unit */
    uint32_t flags = 0;
    if (SafeRead(bodies + 0xA0ull * (id & 0xFFFFFFu) + 0x44, &flags, 4) || (flags & 1u) || ((b[8] >> 28) & 3u) != 3u || !b[9]) return;
    if (g_lwPf.walkAll ? !LwPfDead(s, b[9]) : !LwPfIn(g_lwPf.walk, g_lwPf.walkN, b[9])) return;
    g_lwPf.queue(g_lwPf.writer, id, 0); g_lwPf.rebuilt++;
}
/* The active list [W+0xD8] (count [W+0xE0], ids as 0xDD4B00 reads them), LW_PF_WALK entries a frame, in the tick. */
static void LwPfWalkSlice(uintptr_t w)
{
    uintptr_t list = 0, bodies = 0; uint32_t n = 0, count = 0; LARGE_INTEGER a;
    QueryPerformanceCounter(&a);
    if (SafeRead(w + 0xD8, &list, 8) || SafeRead(w + 0xE0, &n, 4) || SafeRead(w + 0x38, &bodies, 8) || SafeRead(w + 0x40, &count, 4))
    { g_lwPf.walking = 0; return; }
    const LwPfSet* s = &g_lwPf.set[g_lwPf.cur & 1];
    for (uint32_t k = 0; k < LW_PF_WALK && g_lwPf.walkAt < n; k++, g_lwPf.walkAt++) LwPfWalkOne(list, bodies, count, s);
    if (g_lwPf.walkAt >= n) { g_lwPf.walking = 0; g_lwPf.walkAll = 0; }
    g_lwPf.walkMs += LwPfMs(a);
}
/* One body of the map: queued when its slot still holds a non-static body. */
static void LwPfRestoreOne(uintptr_t bodies, uint32_t i)
{
    uint32_t id = 0, flags = 0;
    if (SafeRead(bodies + 0xA0ull * i + 0x70, &id, 4) || SafeRead(bodies + 0xA0ull * i + 0x44, &flags, 4)) return;
    if ((id & 0xFFFFFFu) == i && (flags & 2u) && !(flags & 1u)) { g_lwPf.queue(g_lwPf.writer, id, 0); g_lwPf.rebuilt++; }
}
/* Every dead body whose pair was dropped gets the rebuild: its old box is emptied (0xD0DFEA), so this frame's
 * collector proposes each overlap again (0xD3D45C) and the game's filter alone decides. */
static void LwPfRestore(void)
{
    uintptr_t w = g_lwPf.world, bodies = 0; uint32_t count = 0;
    if (!g_lwPf.dirty || !w || SafeRead(w + 0x38, &bodies, 8) || SafeRead(w + 0x40, &count, 4)) return;
    for (uint32_t i = 0; i < LW_PF_MAP && i < count; i += 8)
    {
        uint64_t word; memcpy(&word, g_lwPf.map + i, 8);
        if (!word) continue;
        for (uint32_t k = 0; k < 8 && i + k < count; k++) if (g_lwPf.map[i + k]) LwPfRestoreOne(bodies, i + k);
    }
    memset(g_lwPf.map, 0, LW_PF_MAP); g_lwPf.dirty = 0;
}
/* Never queues: 0xDD0070 does not bound an id (0xDD00C0-0xDD00DC), so ids of a world that is gone must not reach it. */
static void LwPfReset(void)
{
    if (g_lwPf.dirty) { memset(g_lwPf.map, 0, LW_PF_MAP); g_lwPf.dirty = 0; }
    if (!g_lwPf.world) return;
    memset(g_lwPf.set, 0, sizeof g_lwPf.set); g_lwPf.freshN = 0; g_lwPf.walking = g_lwPf.walkAll = g_lwPf.freshOver = 0;
    g_lwPf.world = 0; g_lwPf.writer = 0;
}

/* ---- switching and the fall guard ---- */

/* Bit 2 needs the fall guard: moving tiles (its drawn ground) and the corpse freeze's bound routines. */
static int LwPfStaticOk(void) { return MoveTilesOn() && (g_lwCorpse.ready || (!g_lwCorpse.failed && LwCorpseBind())); }

/* Only from the frame step, after world and writer matched this tick, so every queued id is this world's. */
static void LwPfSwitch(int want, int mission)
{
    int was = g_lwPf.mode; uint32_t r0 = g_lwPf.rebuilt, born = g_lwPf.born;
    g_lwPf.mode = 0; /* from here every call returns the game's own result */
    LwPfRestore();
    memset(g_lwPf.set, 0, sizeof g_lwPf.set); g_lwPf.freshN = 0; g_lwPf.walking = g_lwPf.walkAll = g_lwPf.freshOver = 0;
    if ((want & LW_PF_STATIC) && !(want & LW_PF_COUNT) && !LwPfStaticOk())
    { want &= ~LW_PF_STATIC; Note("living world: pair filter: no moving tiles or no corpse freeze binding; bit 2 waits for them"); }
    g_lwPf.want = want; g_lwPf.force = 0;
    g_lwPf.winAt = GetTickCount64(); g_lwPf.winUnder = g_lwPf.winSampled = 0; /* the auto-off window starts here */
    if (want)
    {
        LwPfBuildSet(mission); g_lwPf.freshN = 0; g_lwPf.freshOver = 0; g_lwPf.born = born; /* not new deaths */
        if ((want & LW_PF_DROPS) && !(want & LW_PF_COUNT)) LwPfWalkStart(1); /* existing caches of the dead */
    }
    g_lwPf.mode = want;
    Note("living world: test: pair filter %d -> %d: %u bodies queued for a rebuild", was, want, g_lwPf.rebuilt - r0);
}

/* Once a second: bit 2 follows its conditions. It comes back when an asked bit 2 can run (an environment value set
 * before the mission), and goes when moving tiles end, as the guard then has no ground to compare. */
static void LwPfStaticGate(int mission, ULONGLONG now)
{
    if (!(g_lwPf.asked & LW_PF_STATIC) || (g_lwPf.asked & LW_PF_COUNT) || now - g_lwPf.gateAt < 1000) return;
    g_lwPf.gateAt = now;
    int ok = mission && LwPfStaticOk();
    if (ok && !(g_lwPf.want & LW_PF_STATIC)) g_lwPf.want |= LW_PF_STATIC;
    else if (!ok && (g_lwPf.mode & LW_PF_STATIC)) g_lwPf.want &= ~LW_PF_STATIC;
}

/* One ring corpse against the drawn ground; a converted one (state 2) 10 m under it is frozen as disposal freezes it.
 * State 1 is left alone: its entity may still send RagdollSync poses. */
static void LwPfGroundOne(const uint8_t* e)
{
    uint32_t unit, state; int tile;
    memcpy(&unit, e, 4); memcpy(&state, e + 8, 4);
    if (!unit || (state != 1 && state != 2) || !g_lwCorpse.valid(unit)) return;
    const float* p = g_lwCorpse.pos(unit, 0);
    float ref = MoveDrawnGround(p[0], p[1], &tile);
    if (!isfinite(ref) || ref < -9000.0f) return;
    g_lwPf.gSampled++; g_lwPf.winSampled++;
    if (p[2] < ref - 1.5f)
    { g_lwPf.gUnder++; g_lwPf.winUnder++; if (ref - p[2] > g_lwPf.worstUnder) g_lwPf.worstUnder = ref - p[2]; }
    else if (p[2] > ref + 1.5f) g_lwPf.gOver++;
    else g_lwPf.gOn++;
    if ((g_lwPf.mode & LW_PF_STATIC) && !(g_lwPf.mode & LW_PF_COUNT) && state == 2 && p[2] < ref - 10.0f && LwCorpseFreeze(unit, e[0x38]))
        g_lwPf.guarded++;
}
/* Bit 2 lets a corpse fall through every static mesh and the height field stops it; where the field has a hole the
 * guard catches it. Corpses under the ground while bit 2 drops, in a 10 s window, turn bit 2 off until asked again. */
static void LwPfGroundTick(ULONGLONG now)
{
    uintptr_t svc = 0; uint8_t e[0x40];
    if (now - g_lwPf.groundAt < 1000) return;
    g_lwPf.groundAt = now;
    if (!LwPfStaticOk() || SafeRead(g_lw.game + 0x346D500u, &svc, 8) || !svc) return;
    for (int i = 0; i < 32; i++)
    {
        if (SafeRead(svc + 0x18 + 0x40u * (uintptr_t)i, e, sizeof e)) return;
        LwPfGroundOne(e);
    }
    if (now - g_lwPf.winAt < 10000) return;
    if ((g_lwPf.mode & LW_PF_STATIC) && g_lwPf.winUnder >= 3 && g_lwPf.winUnder * 4 > g_lwPf.winSampled)
    {
        Note("living world: pair filter: %u of %u corpses under the ground in 10 s; bit 2 off", g_lwPf.winUnder, g_lwPf.winSampled);
        g_lwPf.want &= ~LW_PF_STATIC; g_lwPf.asked &= ~LW_PF_STATIC;
    }
    g_lwPf.winUnder = g_lwPf.winSampled = 0; g_lwPf.winAt = now;
}

/* ---- the 30 s notes ---- */

/* "layer:count" for each non-zero layer, the counts swapped to 0. */
static void LwPfLayers(volatile LONG* counts, char* out, size_t size)
{
    size_t used = 0; out[0] = 0;
    for (int l = 0; l < 128; l++)
    {
        LONG v = InterlockedExchange(&counts[l], 0);
        if (v && used + 16 < size) used += (size_t)snprintf(out + used, size - used, " %d:%ld", l, (long)v);
    }
    if (!used) snprintf(out, size, " none");
}
static void LwPfNoteSet(void)
{
    char seen[440], kept[440];
    LwPfLayers(g_lwPf.layerSeen, seen, sizeof seen); LwPfLayers(g_lwPf.layerKeep, kept, sizeof kept);
    Note("living world: pair filter set (30 s): dead units now %u (health %u, ring %u, disposal %u), at most %u, new %u, avatars %u; "
         "health cut %u, set full %u; set build %.2f ms; rebuilds queued %u in %u walks, walk %.2f ms",
        g_lwPf.set[g_lwPf.cur & 1].n, g_lwPf.nowHealth, g_lwPf.nowRing, g_lwPf.nowDisposal, g_lwPf.setMax, g_lwPf.born, g_lwPf.avatarN,
        g_lwPf.healthCut, g_lwPf.setFull, g_lwPf.setMs, g_lwPf.rebuilt, g_lwPf.walks, g_lwPf.walkMs);
    Note("living world: pair filter layers (30 s): props seen by filter layer%s; ground kept by layer%s", seen, kept);
    Note("living world: pair filter ground (30 s): %u dead units sampled, on ground %u, over %u, under %u (worst %.1f m), guard froze %u",
        g_lwPf.gSampled, g_lwPf.gOn, g_lwPf.gOver, g_lwPf.gUnder, (double)g_lwPf.worstUnder, g_lwPf.guarded);
}
/* The window's counters back to 0; the layer counts too when no note read them. */
static void LwPfNoteReset(int printed)
{
    for (int l = 0; l < 128 && !printed; l++) { InterlockedExchange(&g_lwPf.layerSeen[l], 0); InterlockedExchange(&g_lwPf.layerKeep[l], 0); }
    g_lwPf.setMax = g_lwPf.born = g_lwPf.healthCut = g_lwPf.setFull = g_lwPf.rebuilt = g_lwPf.walks = 0; g_lwPf.setMs = g_lwPf.walkMs = 0;
    g_lwPf.gSampled = g_lwPf.gOn = g_lwPf.gOver = g_lwPf.gUnder = g_lwPf.guarded = 0; g_lwPf.worstUnder = 0;
}
/* Every 30 s while the mode is not 0 or the test trace is on. */
static void LwPfNote(ULONGLONG now)
{
    if (now - g_lwPf.notedAt < 30000) return;
    g_lwPf.notedAt = now;
    LONG c[PF_C];
    for (int k = 0; k < PF_C; k++) c[k] = InterlockedExchange(&g_lwPf.n[k], 0);
    LONG calls = InterlockedExchange(&g_lwPf.calls, 0), in = InterlockedExchange(&g_lwPf.pairsIn, 0), over = InterlockedExchange(&g_lwPf.overlap, 0);
    int print = g_lwPf.mode != 0 || LwTestTraceOn();
    if (print)
    {
        Note("living world: pair filter %d (30 s): %ld calls, %ld pairs after the game's filter; dead bones paired: %ld with another dead unit, "
             "%ld within one unit, %ld with props, %ld with ground, %ld with living units, %ld with other; dropped: dead %ld, self %ld, props %ld; "
             "kept: triggers %ld, no material table %ld, past the body map %ld; overlap %ld",
            g_lwPf.mode, (long)calls, (long)in, (long)c[PF_DEADSEEN], (long)c[PF_SELFSEEN], (long)c[PF_STATICSEEN], (long)c[PF_GROUND],
            (long)c[PF_LIVE], (long)c[PF_OTHER], (long)c[PF_DROPDEAD], (long)c[PF_DROPSELF], (long)c[PF_DROPSTATIC],
            (long)c[PF_TRIGGER], (long)c[PF_NOMAT], (long)c[PF_BEYOND], (long)over);
        LwPfNoteSet();
    }
    LwPfNoteReset(print);
}

/* ---- the frame step: game thread, every peer, every frame, before this frame's physics step ---- */

/* World 2 and its deferred writer, checked against the reviewed vtable; a new pair clears everything of the old. */
static uintptr_t LwPfBindWorld(void)
{
    uintptr_t w = 0, d = 0, vt = 0, fn = 0, e = g_lwPf.exe;
    if (SafeRead(e + 0x27BA9F0u, &w, 8) || !w || SafeRead(e + 0x27BA9F8u, &d, 8) || !d || SafeRead(d + 0x18, &vt, 8) ||
        vt != e + 0x169FA48u || SafeRead(vt + 0x180, &fn, 8) || fn != e + 0x81EA30u)
    { LwPfReset(); return 0; }
    if (w != g_lwPf.world || d + 0x18 != g_lwPf.writer) { LwPfReset(); g_lwPf.world = w; g_lwPf.writer = d + 0x18; }
    return w;
}
static void LwPfStep(uintptr_t w, int mission, ULONGLONG now)
{
    LwPfStaticGate(mission, now);
    if (g_lwPf.want != g_lwPf.mode || g_lwPf.force) LwPfSwitch(g_lwPf.want, mission);
    if (g_lwPf.mode == 0) return;
    LwPfBuildSet(mission);
    int walkDeaths = (g_lwPf.mode & LW_PF_WALKDEATH) && !(g_lwPf.mode & LW_PF_COUNT);
    if (!walkDeaths) g_lwPf.freshN = g_lwPf.freshOver = 0;
    else if (!g_lwPf.walking && (g_lwPf.freshN || g_lwPf.freshOver)) LwPfWalkStart(0);
    if (g_lwPf.walking) LwPfWalkSlice(w);
    if (mission) LwPfGroundTick(now);
}
static void LwPfFrame(int mission)
{
    if (!g_lwPf.installed) return;
    ULONGLONG now = GetTickCount64();
    InterlockedExchange(&g_lwPf.inFrame, 1);
    uintptr_t w = LwPfBindWorld();
    if (w) LwPfStep(w, mission, now);
    LwPfNote(now);
    InterlockedExchange(&g_lwPf.inFrame, 0);
}

static void LwPfCmd(int mask)
{
    if (!g_lwPf.installed || mask < 0 || (mask & ~LW_PF_VALID)) { Note("living world: test: pairs: not installed, or bits outside 1|2|4|16|32"); return; }
    g_lwPf.asked = g_lwPf.want = mask;
}
/* The ground mask changes which pairs are kept, so the switch runs again and re-filters the existing pairs. */
static void LwPfGroundCmd(int layer)
{
    if (layer == -1) memset(g_lwPf.keep, 0, sizeof g_lwPf.keep);
    else if (layer >= 0 && layer < 128) g_lwPf.keep[layer >> 3] ^= (uint8_t)(1u << (layer & 7));
    else { Note("living world: test: pairground: a layer outside -1..127"); return; }
    g_lwPf.force = 1;
}

/* "pairs N" and "pairground L", from the host's test commands. */
static void LwPfTestCmd(const char* cmd)
{
    int v = 0;
    if (sscanf(cmd, "pairs %d", &v) == 1) LwPfCmd(v);
    else if (sscanf(cmd, "pairground %d", &v) == 1) LwPfGroundCmd(v);
    else Note("living world: test: pair filter: not understood: %s", cmd);
}

/* Start-up: the filter hooked for good (mode 0 returns the original's result) when every pin is this build's. */
static void LwInstallPairs(void)
{
    static int done; char t[8] = ""; uintptr_t v1 = 0, v2 = 0, v3 = 0;
    if (done++) return;
    uintptr_t e = (uintptr_t)GetModuleHandleA(NULL), g = g_lw.game;
    g_lwPf.exe = e;
    if (strcmp(g_build, "25480438") || !g ||
        /* the hook and the filter's layout */
        !ApiCodeMatches(e + 0xCE5A10u, "48 89 5C 24 18 48 89 6C 24 20 41 56 49 8B E8 48 8B DA 4C 8B F1 4D 8B D0 4D 8B D8 45 85 C9 0F 8E A4 01 00 00") ||
        !ApiCodeMatches(e + 0xCE5A66u, "3B 43 40 0F 83 51 01 00 00 48 8D 0C 80 48 C1 E1 05 48 03 4B 38 F6 41 44 03") ||
        !ApiCodeMatches(e + 0xCE5BB6u, "49 8B 0B 49 89 0A 49 83 C2 08") ||
        !ApiCodeMatches(e + 0xCE5BDDu, "4C 2B D5 48 8B 6C 24 28 49 C1 FA 03 41 8B C2 41 5E C3") ||
        SafeRead(e + 0x148B938u + 0x18, &v1, 8) || v1 != e + 0xCE5A10u ||
        !ApiCodeMatches(e + 0xD3D50Fu, "48 8B 43 28 48 8B 50 08 48 8B 8A B8 04 00 00 48 85 C9 74 16 48 8B 01 4C 8D 43 30 41 B9 DC 01 00 00 FF 50 18") ||
        /* the deferred rebuild and its replay */
        !ApiCodeMatches(e + 0x81EA30u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 BE 10 00 00 00 89 54 24 28 48 8D 59 E8 66 89 74 24 20 48 8B CB") ||
        SafeRead(e + 0x169FA48u + 0x180, &v2, 8) || v2 != e + 0x81EA30u ||
        !ApiCodeMatches(e + 0x81B88Fu, "48 8B 03 48 8B CB 45 0F B6 43 1C 41 8B 53 18 FF 90 80 01 00 00") ||
        SafeRead(e + 0x14B3168u + 0x180, &v3, 8) || v3 != e + 0xDD0070u ||
        !ApiCodeMatches(e + 0xDD00DCu, "8B 41 74 83 C0 01") ||
        !ApiCodeMatches(e + 0xD0DFB0u, "48 89 5C 24 20 89 54 24 10 57 48 83 EC 20 8B C2 48 8B F9 25 FF FF FF 00 48 8D 1C 80 8B C2 25 FF FF FF 00 "
                                       "48 C1 E3 05 48 03 59 38 48 C1 E0 04 48 03 41 70 48 B9 FF FF 7F FF 7F FF FF 7F 66 83 4B 48 01") ||
        !ApiCodeMatches(e + 0x79EF4Fu, "8B C3 8B CB 48 69 D0 B0 00 00 00 48 8D 05 9F 10 86 FF 48 8B 94 02 98 A8 7B 02 E8 A2 F9 FF FF") ||
        /* the ground test's shape layout */
        !ApiCodeMatches(e + 0xDC11A0u, "33 C0 48 C7 41 10 01 00 00 00 41 B8 FF FF FF FF 4C 89 41 08 66 89 41 18 88 51 1A") ||
        !ApiCodeMatches(e + 0xD12C69u, "66 83 4F 18 08 40 88 77 1C") ||
        !ApiCodeMatches(e + 0xDD6FADu, "48 8B 57 60 80 7A 1A 13 75 0C 0F B6 C8 48 C1 E9 05 48 8B 54 CA 40") ||
        /* the trigger rule's material table */
        !ApiCodeMatches(e + 0xDD6E2Au, "49 8B 8D 00 0B 00 00") || !ApiCodeMatches(e + 0xDD6E43u, "4C 8B 41 48") ||
        !ApiCodeMatches(e + 0xDD6EDFu, "0F B7 47 6A 48 8D 0C 80 48 03 C9 41 80 7C C8 10 01") ||
        /* the bone rule's actor kind bits, and the active list the walk reads */
        !ApiCodeMatches(e + 0x79678Bu, "3C 03 75 05 8D 59 02") || !ApiCodeMatches(e + 0x7967C3u, "C1 E2 1C") ||
        !ApiCodeMatches(e + 0xDEBEF0u, "45 8B 87 E0 00 00 00 45 85 C0 74 0F 49 8B 97 D8 00 00 00") ||
        /* the frame order the race argument rests on */
        !ApiCodeMatches(g + 0xAB611Au, "45 38 BE B4 10 00 00") || !ApiCodeMatches(g + 0xAB612Du, "E8 6E 20 94 00") ||
        !ApiCodeMatches(g + 0x13F8938u, "E8 73 E7 F2 FF") || !ApiCodeMatches(g + 0xAB6235u, "FF 92 20 04 00 00") ||
        !ApiCodeMatches(e + 0x1B1124u, "E8 A7 17 00 00") || !ApiCodeMatches(e + 0x1B2AFAu, "E8 C1 B5 5D 00") ||
        !ApiCodeMatches(e + 0x78E1F2u, "E8 19 09 01 00") ||
        (g_lwPf.map = VirtualAlloc(NULL, LW_PF_MAP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) == NULL ||
        LoaderHook("living world: pair filter", e + 0xCE5A10u, (void*)LwPfPairs, &g_lwPf.orig) || g_lwPf.orig == NULL)
    { Note("living world: pair filter: not the reviewed code; off"); return; }
    g_lwPf.queue = (LwPfQueueFn)(e + 0x81EA30u); g_lwPf.installed = 1; g_lwPf.notedAt = GetTickCount64();
    DWORD got = GetEnvironmentVariableA("HELLFORGE_LW_PAIRS", t, sizeof t);
    if (got == 0) return;
    int m = got < sizeof t ? atoi(t) : -1;
    if (m < 0 || (m & ~LW_PF_VALID)) { Note("living world: pair filter: HELLFORGE_LW_PAIRS=%s has bits outside 1|2|4|16|32; 0", t); return; }
    g_lwPf.asked = g_lwPf.want = m;
}
