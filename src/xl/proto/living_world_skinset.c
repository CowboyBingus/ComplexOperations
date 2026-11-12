/* Lever B, game thread: the unit handles whose meshes may replay, published every 250 ms under a seqlock, and B's
 * switches and notes. Classes by the unit's scene-graph handle ([EvpUnitGraph(unit)+4], what 0x510C19 stores in
 * robj+0x44): far members of online squads (from every player beyond R, back near under R - 10 m), frozen ring
 * corpses, and, never replayed but counted, the Helldivers and near members. Workers read the set inside the seqlock
 * and fall back to the game's compute when it moved. Storage is static and never freed. */
static LwSkrSetEntry g_lwSkrNext[LW_SKR_SET];
static int g_lwSkrNextN;
_Static_assert(sizeof(LwSkrCount) == 12 * sizeof(LONG64), "the class counters are read as one array");

static int LwSkrSetCmp(const void* a, const void* b)
{
    uint32_t x = ((const LwSkrSetEntry*)a)->handle, y = ((const LwSkrSetEntry*)b)->handle;
    return x < y ? -1 : x > y;
}

/* The published class of a handle (the game thread is the set's only writer, so no seqlock here). */
static int LwSkrSetOwn(uint32_t h)
{
    const LwSkrSetEntry* e = bsearch(&(LwSkrSetEntry){h, 0}, g_lwSkrSet.e, (size_t)g_lwSkr.setN, sizeof *e, LwSkrSetCmp);
    return e ? (int)e->cls : 0;
}

static void LwSkrAdd(uint32_t unit, int cls)
{
    uint32_t h = unit ? LwUnitRenderHandle(unit) : 0;
    if (h && g_lwSkrNextN < LW_SKR_SET) { g_lwSkrNext[g_lwSkrNextN].handle = h; g_lwSkrNext[g_lwSkrNextN++].cls = (uint32_t)cls; }
}

static void LwSkrAddSquad(const LwSquad* q, float far2, float leave2)
{
    uint32_t members[32]; int m = LwMembers(q->squad, members);
    for (int k = 0; k < m; k++)
    {
        ActHealth h; float p[3];
        if (!ActHealthOf(g_lw.game, members[k], &h) || !h.unit || !EvpUnitPosition(h.unit, p)) continue;
        float d2 = LwNearestPlayer2(p[0], p[1]);
        int was = d2 >= leave2 && d2 <= far2 && LwSkrSetOwn(LwUnitRenderHandle(h.unit)) == LW_SKR_FAR;
        LwSkrAdd(h.unit, d2 > far2 || was ? LW_SKR_FAR : LW_SKR_NEAR);
    }
}

/* Sorted, one entry a handle: a class that never replays wins. */
static int LwSkrUnique(LwSkrSetEntry* e, int n)
{
    int w = 0;
    qsort(e, (size_t)n, sizeof *e, LwSkrSetCmp);
    for (int i = 0; i < n; i++)
    {
        if (w && e[w - 1].handle == e[i].handle) { if (!LwSkrReplays((int)e[i].cls)) e[w - 1].cls = e[i].cls; continue; }
        e[w++] = e[i];
    }
    return w;
}

/* The list, then written between two odd/even seq steps. */
static void LwSkrPublish(void)
{
    g_lwSkrNextN = 0;
    float r = g_lwSkr.farM, l = r - 10.0f;
    for (int i = 0; i < g_lwWorld.count && g_lwWorld.playersAt > 0; i++)
        if (g_lwSquads[i].state == LW_ONLINE) LwSkrAddSquad(&g_lwSquads[i], r * r, l * l);
    for (int i = 0; i < LW_CORPSE_RING && g_lwSkr.corpse; i++) if (g_lwCorpse.slot[i].done) LwSkrAdd(g_lwCorpse.slot[i].unit, LW_SKR_CORPSE);
    for (int i = 0; i < g_lwWorld.playersAt; i++) LwSkrAdd(g_lwWorld.units[i], LW_SKR_PLAYER);
    int w = LwSkrUnique(g_lwSkrNext, g_lwSkrNextN);
    const LwSkrSetEntry* e = g_lwSkrNext;
    InterlockedIncrement(&g_lwSkrSet.seq);
    memcpy(g_lwSkrSet.e, e, (size_t)w * sizeof *e);
    g_lwSkrSet.n = w;
    InterlockedIncrement(&g_lwSkrSet.seq);
    g_lwSkr.setN = w;
}

static int LwSkrActive(void) { return g_lwSkr.period >= 1; }

static void LwSkrSet(int P)
{
    if (P < 0 || P > 8) { Note("living world: test: skinreuse: a value outside 0..8"); return; }
    if (P >= 1 && !LwSkrInstall()) return;
    if (P >= 2 && !g_lwSkr.arena && (g_lwSkr.arena = VirtualAlloc(NULL, LW_SKR_ARENA, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) == NULL)
    { Note("living world: skin reuse: no 16 MB for the stored palettes; off"); return; }
    if (P != g_lwSkr.period) InterlockedExchange(&g_lwSkr.clearReq, 1);
    g_lwSkr.period = P;
    Note("living world: skin reuse %d: %s (far beyond %.0f m, corpses %d every %d frames, previous rows %d, verify 1 in %d)", P,
         P == 0 ? "off" : P == 1 ? "classes and times only" : "far members and frozen corpses replay", (double)g_lwSkr.farM,
         g_lwSkr.corpse, g_lwSkr.corpsePeriod, g_lwSkr.prev, g_lwSkr.verify);
}

static void LwSkrAb(int P, int secs)
{
    if (P == 0) { g_lwSkr.abP = 0; Note("living world: skin reuse: A/B stopped"); return; }
    if (P < 2 || P > 8 || secs < 1) { Note("living world: test: skinreuse ab: a period outside 2..8 or under 1 s"); return; }
    LwSkrSet(P);
    if (g_lwSkr.period != P) return;
    g_lwSkr.abP = P; g_lwSkr.abSecs = secs; g_lwSkr.abAt = GetTickCount64();
    InterlockedExchange(&g_lwFr.phase, 2000 + P);
    Note("living world: skin reuse: periods 0 and %d take turns every %d s (recorder phase 2000 / %d)", P, secs, 2000 + P);
}

/* A switch value in [lo, hi] into *at; 0 when out of range. */
static int LwSkrSwitch(volatile int* at, int v, int lo, int hi, const char* what)
{
    if (v < lo || v > hi) { Note("living world: test: skinreuse %s: a value outside %d..%d", what, lo, hi); return 0; }
    *at = v; InterlockedExchange(&g_lwSkr.clearReq, 1);
    Note("living world: skin reuse: %s %d", what, v);
    return 1;
}

static void LwSkrCmd(const char* a)
{
    int x = 0, y = 2, n;
    if ((n = sscanf(a, "ab %d %d", &x, &y)) >= 1) LwSkrAb(x, n == 2 ? y : 2);
    else if (sscanf(a, "m %d", &x) == 1 && x >= 20 && x <= 2000) { g_lwSkr.farM = (float)x; Note("living world: skin reuse: far beyond %d m", x); }
    else if (sscanf(a, "corpseperiod %d", &x) == 1) LwSkrSwitch(&g_lwSkr.corpsePeriod, x, 2, 64, "corpseperiod");
    else if (sscanf(a, "corpse %d", &x) == 1) LwSkrSwitch(&g_lwSkr.corpse, x, 0, 1, "corpse");
    else if (sscanf(a, "prev %d", &x) == 1) LwSkrSwitch(&g_lwSkr.prev, x, 0, 1, "prev");
    else if (sscanf(a, "verify %d", &x) == 1) LwSkrSwitch(&g_lwSkr.verify, x, 0, 1000000, "verify");
    else if (strncmp(a, "anim", 4) == 0) Note("living world: test: skinreuse anim: not built (coupling to the animation lever)");
    else if (sscanf(a, "%d", &x) == 1) { g_lwSkr.abP = 0; LwSkrSet(x); }
    else Note("living world: test: skinreuse %s: not understood", a);
}

/* Game thread, each tick: the A/B flip, a clear when the table or the arena is three quarters full, and the set. */
static void LwSkrFrame(ULONGLONG now)
{
    if (g_lwSkr.abP && now - g_lwSkr.abAt >= (ULONGLONG)g_lwSkr.abSecs * 1000u)
    {
        g_lwSkr.abAt = now;
        g_lwSkr.period = g_lwSkr.period ? 0 : g_lwSkr.abP;
        InterlockedExchange(&g_lwSkr.clearReq, 1);
        InterlockedExchange(&g_lwFr.phase, 2000 + g_lwSkr.period);
    }
    if (g_lwSkr.slotsUsed > (LONG)(LW_SKR_SLOTS * 3 / 4) || g_lwSkr.arenaUsed > (LONG64)(LW_SKR_ARENA / 4 * 3)) InterlockedExchange(&g_lwSkr.clearReq, 1);
    if ((g_lwSkr.period < 1 && !g_lwSkr.abP) || now - g_lwSkr.pubAt < 250) return;
    g_lwSkr.pubAt = now;
    LwSkrPublish();
}

/* The environment's switches, read once with the skin instrument's first tick. */
static void LwSkinEnvLevers(void)
{
    static const struct { const char* name; volatile int* at; int lo, hi; } sw[] = {
        {"HELLFORGE_LW_SKINREUSE_CORPSE", &g_lwSkr.corpse, 0, 1}, {"HELLFORGE_LW_SKINREUSE_PREV", &g_lwSkr.prev, 0, 1},
    };
    char t[16] = "";
    for (size_t i = 0; i < sizeof sw / sizeof sw[0]; i++)
        if (GetEnvironmentVariableA(sw[i].name, t, sizeof t) > 0 && atoi(t) >= sw[i].lo && atoi(t) <= sw[i].hi) *sw[i].at = atoi(t);
    if (GetEnvironmentVariableA("HELLFORGE_LW_SKINREUSE_M", t, sizeof t) > 0 && atoi(t) >= 20 && atoi(t) <= 2000) g_lwSkr.farM = (float)atoi(t);
    if (GetEnvironmentVariableA("HELLFORGE_LW_SKINREUSE_ANIM", t, sizeof t) > 0) Note("living world: skin reuse: HELLFORGE_LW_SKINREUSE_ANIM ignored: not built");
    if (GetEnvironmentVariableA("HELLFORGE_LW_SKINREUSE", t, sizeof t) > 0 && atoi(t) > 0) LwSkrSet(atoi(t));
    if (GetEnvironmentVariableA("HELLFORGE_LW_SKINCOPY", t, sizeof t) > 0 && atoi(t) > 0) LwSkcSet(atoi(t));
}

static const char* const g_lwSkrClassNames[LW_SKR_CLASSES] = {"other", "far member", "corpse", "player", "near member"};

/* One class's sums over the shards, reset. */
static void LwSkrTake30(int k, LONG64* v)
{
    memset(v, 0, 12 * sizeof *v);
    for (uint32_t s = 0; s < LW_SKR_SHARDS; s++)
    {
        volatile LONG64* f = &g_lwSkrShard[s].c[k].meshes;
        for (int i = 0; i < 12; i++) v[i] += InterlockedExchange64(&f[i], 0);
    }
}

static void LwSkrClassNote(int k, double fr, double tpm)
{
    LONG64 v[12]; /* meshes, mats, compMats, replMats, tOrig, tReplay, seam, verify 0..4 */
    LwSkrTake30(k, v);
    if (!v[0]) return;
    Note("living world: skin reuse, %s: %.1f meshes and %.0f matrices a frame; computed %.0f, replayed %.0f matrices a frame; "
         "%.1f ns a computed matrix, %.1f ns a replayed one; verify 0:<1e-4:<1e-3:<1e-2:more %lld:%lld:%lld:%lld:%lld; seam risks %lld",
         g_lwSkrClassNames[k], (double)v[0] / fr, (double)v[1] / fr, (double)v[2] / fr, (double)v[3] / fr,
         v[2] ? (double)v[4] / tpm * 1e6 / (double)v[2] : 0.0, v[3] ? (double)v[5] / tpm * 1e6 / (double)v[3] : 0.0,
         (long long)v[7], (long long)v[8], (long long)v[9], (long long)v[10], (long long)v[11], (long long)v[6]);
}

static void LwSkrNote(double secs, LONG64 frames)
{
    if (g_lwSkr.hooked <= 0) return;
    LONG64 torn = 0, busy = 0, room = 0, prev = 0;
    for (uint32_t s = 0; s < LW_SKR_SHARDS; s++)
    {
        LwSkrShard* h = &g_lwSkrShard[s];
        torn += InterlockedExchange64(&h->setTorn, 0); busy += InterlockedExchange64(&h->busySkip, 0);
        room += InterlockedExchange64(&h->noRoom, 0); prev += InterlockedExchange64(&h->prevSkip, 0);
    }
    Note("living world: skin reuse (%.0f s): period %d (A/B %d), far %.0f m, corpses %d every %d, previous rows %d, verify 1 in %d; set %d handles, "
         "%lld torn reads; %ld slots, %.1f MB stored, epoch %ld, %lld clears (%lld deferred); %lld busy skips, %lld without room, %lld batches outside",
         secs, g_lwSkr.period, g_lwSkr.abP, (double)g_lwSkr.farM, g_lwSkr.corpse, g_lwSkr.corpsePeriod, g_lwSkr.prev, g_lwSkr.verify, g_lwSkr.setN,
         (long long)torn, (long)g_lwSkr.slotsUsed, (double)g_lwSkr.arenaUsed / 1048576.0, (long)g_lwSkr.epoch,
         (long long)InterlockedExchange64(&g_lwSkr.clears, 0), (long long)InterlockedExchange64(&g_lwSkr.clearDeferred, 0),
         (long long)busy, (long long)room, (long long)prev);
    double fr = frames > 0 ? (double)frames : 1, tpm = g_lwSkin.tscPerMs > 0 ? g_lwSkin.tscPerMs : 1;
    for (int k = 0; k < LW_SKR_CLASSES; k++) LwSkrClassNote(k, fr, tpm);
}
