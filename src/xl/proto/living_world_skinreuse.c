/* Lever B: far squad members and frozen ring corpses replay their skin palette. 0x512B80 (upload jobs on workers)
 * computes a mesh's palette relative to its root node and writes nothing but those rows and robj+0xC4, so a replay that
 * takes its offset from the same lock xadd, writes the same robj+0xC4 and returns off >> 4 gives the draw the game's
 * layout, and the unit still moves smoothly: only its limb pose steps. A unit's meshes share one due frame (phase by its
 * scene-graph handle robj+0x44) and replay only bytes stored at that frame, so its parts never show two poses. Only
 * handles the game thread published replay (living_world_skinset.c). Slots are taken by a busy exchange; the epoch is
 * bumped only in the kick hook while no world has its jobs out (S+0x184 != 1) and no call is inside.
 * HELLFORGE_LW_SKINREUSE=P / "skinreuse P": 0 off, 1 classes and times only, 2..8 the period in frames. */
#define LW_SKR_SLOTS 4096u
#define LW_SKR_PROBES 8u
#define LW_SKR_ARENA (16u << 20)
#define LW_SKR_SHARDS 16u
#define LW_SKR_CLASSES 5
#define LW_SKR_SET 1024
enum { LW_SKR_OTHER, LW_SKR_FAR, LW_SKR_CORPSE, LW_SKR_PLAYER, LW_SKR_NEAR };
typedef int32_t (*LwSkrObjFn)(uintptr_t robj, uintptr_t S, uintptr_t key);
typedef struct { volatile LONG busy, epoch; uintptr_t robj, key, sg; uint32_t mats, cap, handle, root, frame; uint8_t* data; } LwSkrSlot;
typedef struct { volatile LONG64 meshes, mats, compMats, replMats, tOrig, tReplay, seam, verify[5]; } LwSkrCount;
typedef struct __attribute__((aligned(64)))
{
    volatile LONG inside; volatile LONG64 replays, setTorn, busySkip, noRoom, prevSkip;
    LwSkrCount c[LW_SKR_CLASSES];
} LwSkrShard;
/* One call: the mesh, its unit's handle and class, and what a stored palette must match. */
typedef struct { uintptr_t robj, S, key, sg; uint32_t mats, handle, root, lastDue; int cls; LwSkrShard* sh; } LwSkrCall;
typedef struct { uint32_t handle, cls; } LwSkrSetEntry;
static struct { volatile LONG seq, n; LwSkrSetEntry e[LW_SKR_SET]; } g_lwSkrSet; /* seqlock: seq odd while written */
static struct
{
    int hooked, abP, abSecs; uintptr_t exe; void* orig;
    volatile int period, prev, verify, corpse, corpsePeriod; float farM;
    volatile LONG clearReq, epoch, slotsUsed; volatile LONG64 arenaUsed, clears, clearDeferred; uint8_t* arena;
    ULONGLONG abAt, pubAt; int setN;
    LwSkrSlot slot[LW_SKR_SLOTS];
} g_lwSkr = {.prev = 1, .verify = 16, .corpse = 1, .corpsePeriod = 16, .farM = 150.0f};
static LwSkrShard g_lwSkrShard[LW_SKR_SHARDS];

static LwSkrShard* LwSkrShardOf(void) { return &g_lwSkrShard[(GetCurrentThreadId() >> 2) & (LW_SKR_SHARDS - 1)]; }
static uint32_t LwSkrHashSlot(uintptr_t robj, uintptr_t key) { return (uint32_t)(((uint64_t)(robj ^ (key * 0x9E3779B97F4A7C15ull)) * 0x9E3779B97F4A7C15ull) >> 52); }

/* The class published for a unit handle; 0 (the game's compute) when the set was being written meanwhile. */
static int LwSkrSetFind(uint32_t h, LwSkrShard* sh)
{
    LONG s1 = __atomic_load_n(&g_lwSkrSet.seq, __ATOMIC_ACQUIRE), n = g_lwSkrSet.n, lo = 0, hi, cls = 0;
    if (s1 & 1) { InterlockedIncrement64(&sh->setTorn); return 0; }
    hi = (n < 0 ? 0 : n > LW_SKR_SET ? LW_SKR_SET : n) - 1;
    while (lo <= hi)
    {
        LONG mid = (lo + hi) / 2; uint32_t v = ((volatile LwSkrSetEntry*)g_lwSkrSet.e)[mid].handle;
        if (v == h) { cls = (int)((volatile LwSkrSetEntry*)g_lwSkrSet.e)[mid].cls; break; }
        if (v < h) lo = mid + 1; else hi = mid - 1;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&g_lwSkrSet.seq, __ATOMIC_RELAXED) != s1) { InterlockedIncrement64(&sh->setTorn); return 0; }
    return cls < LW_SKR_CLASSES ? cls : 0;
}

/* Rows of batch i as 0x512F80..0x513049 reads them: current at c, previous at p, n matrices. 1 when inside `bytes`,
 * 0 when the batch has no bone table (the game writes none), -1 when outside. */
typedef struct { uint64_t c, p, n; } LwSkrRows;
static int LwSkrRowsOf(uintptr_t robj, uint32_t i, uint32_t bytes, LwSkrRows* r)
{
    uintptr_t b = *(volatile uintptr_t*)(robj + 0x98) + (uintptr_t)i * 0x28, k = *(volatile uintptr_t*)(robj + 0x70);
    uint32_t tab = *(volatile uint32_t*)(k + 0xC);
    if (!tab) return 0;
    r->n = *(volatile uint32_t*)(k + tab + (uintptr_t)*(volatile uint32_t*)(b + 0x18) * 8 + 8);
    r->c = (uint64_t)(*(volatile uint32_t*)(b + 0x1C) / 3u) * 48u;
    r->p = (uint64_t)(*(volatile uint32_t*)(b + 0x20) / 3u) * 48u;
    return r->c + r->n * 48u <= bytes && r->p + r->n * 48u <= bytes ? 1 : -1;
}

/* 1 when batch i's previous rows meet no batch's current rows but its own identical ones: how the batches share the
 * block is data, so a copy that could overwrite shown rows is not made. */
static int LwSkrPrevApart(uintptr_t robj, uint32_t i, uint32_t nb, uint32_t bytes, const LwSkrRows* r)
{
    LwSkrRows o;
    for (uint32_t j = 0; j < nb; j++)
    {
        int ok = LwSkrRowsOf(robj, j, bytes, &o);
        if (ok < 0 || (ok > 0 && r->p < o.c + o.n * 48u && o.c < r->p + r->n * 48u && !(j == i && r->p == r->c))) return 0;
    }
    return 1;
}

/* So motion vectors see no limb motion on a replay frame: the shown pose is also the previous one. */
static void LwSkrPrevIsCurrent(LwSkrShard* sh, uintptr_t robj, uint8_t* dst, uint32_t bytes)
{
    uint32_t nb = *(volatile uint32_t*)(robj + 0x90); LwSkrRows r;
    for (uint32_t i = 0; i < nb; i++)
    {
        int ok = LwSkrRowsOf(robj, i, bytes, &r);
        if (ok > 0 && r.n && r.p != r.c && !LwSkrPrevApart(robj, i, nb, bytes, &r)) ok = -1;
        if (ok < 0) InterlockedIncrement64(&sh->prevSkip);
        if (ok > 0 && r.n && r.p != r.c) memcpy(dst + r.p, dst + r.c, (size_t)r.n * 48u);
    }
}

static float LwSkrWorst(const float* a, const float* b, uint64_t n, float worst)
{
    for (uint64_t j = 0; j < n; j++) { float d = fabsf(a[j] - b[j]); if (!(d <= worst)) worst = isnan(d) ? INFINITY : d; }
    return worst;
}

/* Largest float difference over the current rows, bucketed 0, <1e-4, <1e-3, <1e-2, more; previous rows differ by design. */
static void LwSkrCompare(const LwSkrCall* c, const uint8_t* stored, const uint8_t* now)
{
    uint32_t nb = *(volatile uint32_t*)(c->robj + 0x90), bytes = c->mats * 48u; LwSkrRows r; float worst = 0;
    for (uint32_t i = 0; i < nb; i++)
        if (LwSkrRowsOf(c->robj, i, bytes, &r) > 0) worst = LwSkrWorst((const float*)(stored + r.c), (const float*)(now + r.c), r.n * 12u, worst);
    int k = worst == 0 ? 0 : worst < 1e-4f ? 1 : worst < 1e-3f ? 2 : worst < 1e-2f ? 3 : 4;
    InterlockedIncrement64(&c->sh->c[c->cls].verify[k]);
}

static int32_t LwSkrOrig(const LwSkrCall* c)
{
    uint64_t t0 = __builtin_ia32_rdtsc();
    int32_t r = ((LwSkrObjFn)g_lwSkr.orig)(c->robj, c->S, c->key);
    LwSkrCount* n = &c->sh->c[c->cls];
    InterlockedAdd64(&n->tOrig, (LONG64)(__builtin_ia32_rdtsc() - t0)); InterlockedAdd64(&n->compMats, c->mats);
    return r;
}

/* A slot of this epoch for (robj, key); its fields are checked again under its busy flag. */
static LwSkrSlot* LwSkrFind(uintptr_t robj, uintptr_t key)
{
    uint32_t h = LwSkrHashSlot(robj, key); LONG ep = g_lwSkr.epoch;
    for (uint32_t i = 0; i < LW_SKR_PROBES; i++)
    {
        LwSkrSlot* s = &g_lwSkr.slot[(h + i) & (LW_SKR_SLOTS - 1)];
        if (s->robj == robj && s->key == key && s->epoch == ep) return s;
    }
    return NULL;
}

/* A free slot (empty or of an older epoch) taken with busy held; its data is of an older epoch, so it is dropped. */
static LwSkrSlot* LwSkrClaimFree(uintptr_t robj, uintptr_t key)
{
    uint32_t h = LwSkrHashSlot(robj, key); LONG ep = g_lwSkr.epoch;
    for (uint32_t i = 0; i < LW_SKR_PROBES; i++)
    {
        LwSkrSlot* s = &g_lwSkr.slot[(h + i) & (LW_SKR_SLOTS - 1)];
        if ((s->robj && s->epoch == ep) || InterlockedCompareExchange(&s->busy, 1, 0)) continue;
        if (!s->robj || s->epoch != ep) { s->robj = 0; s->epoch = ep; s->data = NULL; s->cap = 0; return s; }
        InterlockedExchange(&s->busy, 0);
    }
    return NULL;
}

static uint8_t* LwSkrArena(uint32_t bytes)
{
    LONG64 size = ((LONG64)bytes + 63) & ~(LONG64)63, at = g_lwSkr.arena ? InterlockedExchangeAdd64(&g_lwSkr.arenaUsed, size) : LW_SKR_ARENA;
    return at + size <= (LONG64)LW_SKR_ARENA ? g_lwSkr.arena + at : NULL;
}

/* After the game's compute on a due frame: the rows it wrote, kept for the unit's next frames. robj is written last. */
static void LwSkrStore(const LwSkrCall* c, int32_t r)
{
    LwSkrSlot* s = LwSkrFind(c->robj, c->key);
    if (s && InterlockedCompareExchange(&s->busy, 1, 0)) { InterlockedIncrement64(&c->sh->busySkip); return; }
    if (s && (s->robj != c->robj || s->key != c->key)) { InterlockedExchange(&s->busy, 0); InterlockedIncrement64(&c->sh->busySkip); return; }
    int fresh = !s;
    if (fresh && (s = LwSkrClaimFree(c->robj, c->key)) == NULL) { InterlockedIncrement64(&c->sh->noRoom); return; }
    uint32_t bytes = c->mats * 48u;
    if (s->cap < c->mats) { uint8_t* d = LwSkrArena(bytes); if (d) { s->data = d; s->cap = c->mats; } }
    if (s->cap < c->mats) { s->mats = 0; InterlockedExchange(&s->busy, 0); InterlockedIncrement64(&c->sh->noRoom); return; }
    memcpy(s->data, (const uint8_t*)*(volatile uintptr_t*)(c->S + 0x170) + ((uint32_t)r << 4), bytes);
    s->mats = c->mats; s->sg = c->sg; s->handle = c->handle; s->root = c->root; s->frame = c->lastDue; s->key = c->key;
    if (fresh) InterlockedIncrement(&g_lwSkr.slotsUsed);
    __atomic_store_n(&s->robj, c->robj, __ATOMIC_RELEASE);
    InterlockedExchange(&s->busy, 0);
}

/* The slot with busy held when it holds this mesh's bytes of its unit's last due frame; *seam when a slot of another
 * frame or shape was there. */
static LwSkrSlot* LwSkrTake(const LwSkrCall* c, int* seam)
{
    LwSkrSlot* s = LwSkrFind(c->robj, c->key);
    *seam = s != NULL;
    if (!s || InterlockedCompareExchange(&s->busy, 1, 0)) return NULL;
    if (s->robj == c->robj && s->key == c->key && s->epoch == g_lwSkr.epoch && s->data && s->mats == c->mats && s->sg == c->sg &&
        s->handle == c->handle && s->root == c->root && s->frame == c->lastDue) { *seam = 0; return s; }
    InterlockedExchange(&s->busy, 0);
    return NULL;
}

/* The game's 0x512C1B..0x512C44 and 0x513069 with the stored rows in place of the compute. */
static int32_t LwSkrReplay(const LwSkrCall* c, LwSkrSlot* s)
{
    uint64_t t0 = __builtin_ia32_rdtsc();
    uint32_t bytes = c->mats * 48u;
    uint32_t off = (uint32_t)InterlockedExchangeAdd((volatile LONG*)(c->S + 0x178), (LONG)bytes);
    uint8_t* dst = (uint8_t*)*(volatile uintptr_t*)(c->S + 0x170) + off;
    memcpy(dst, s->data, bytes);
    if (g_lwSkr.prev) LwSkrPrevIsCurrent(c->sh, c->robj, dst, bytes);
    InterlockedExchange(&s->busy, 0);
    if (!c->key) *(volatile uint32_t*)(c->robj + 0xC4) = off >> 4;
    LwSkrCount* n = &c->sh->c[c->cls];
    InterlockedAdd64(&n->tReplay, (LONG64)(__builtin_ia32_rdtsc() - t0)); InterlockedAdd64(&n->replMats, c->mats);
    return (int32_t)(off >> 4);
}

/* Due frame: compute and keep. Else replay, or compute and compare one replay in `verify`. */
static int32_t LwSkrReuse(LwSkrCall* c, int P)
{
    uint32_t f = LwSkinFrameOf(c->S), per = c->cls == LW_SKR_CORPSE ? (uint32_t)g_lwSkr.corpsePeriod : (uint32_t)P;
    uint32_t phase = ((uint32_t)(c->handle * 2654435761u) >> 16) % per;
    c->lastDue = f - (f + phase) % per;
    c->sg = c->key ? c->key : *(volatile uintptr_t*)(c->robj + 0x38);
    c->root = *(volatile uint32_t*)(c->robj + 0x30);
    if (f == c->lastDue) { int32_t r = LwSkrOrig(c); if (r >= 0) LwSkrStore(c, r); return r; }
    int seam; LwSkrSlot* s = LwSkrTake(c, &seam);
    if (!s) { if (seam) InterlockedIncrement64(&c->sh->c[c->cls].seam); return LwSkrOrig(c); }
    int K = g_lwSkr.verify;
    if (K <= 0 || InterlockedIncrement64(&c->sh->replays) % K) return LwSkrReplay(c, s);
    int32_t r = LwSkrOrig(c);
    if (r >= 0) LwSkrCompare(c, s->data, (const uint8_t*)*(volatile uintptr_t*)(c->S + 0x170) + ((uint32_t)r << 4));
    InterlockedExchange(&s->busy, 0);
    return r;
}

static int LwSkrReplays(int cls) { return cls == LW_SKR_FAR || (cls == LW_SKR_CORPSE && g_lwSkr.corpse); }

/* The hook on 0x512B80 (upload job workers; int32(robj, S, key)). */
static int32_t LwSkrObj(uintptr_t robj, uintptr_t S, uintptr_t key)
{
    int P = g_lwSkr.period; /* read once: every value is a valid mode */
    if (P < 1) return ((LwSkrObjFn)g_lwSkr.orig)(robj, S, key);
    LwSkrCall c = {robj, S, key, 0, *(volatile uint32_t*)(robj + 0xC8), *(volatile uint32_t*)(robj + 0x44), 0, 0, 0, LwSkrShardOf()};
    InterlockedIncrement(&c.sh->inside);
    if (*(volatile uintptr_t*)(robj + 0x70) && c.mats && c.mats <= 4096) c.cls = LwSkrSetFind(c.handle, c.sh);
    InterlockedIncrement64(&c.sh->c[c.cls].meshes); InterlockedAdd64(&c.sh->c[c.cls].mats, c.mats);
    int32_t r = P >= 2 && LwSkrReplays(c.cls) ? LwSkrReuse(&c, P) : LwSkrOrig(&c);
    InterlockedDecrement(&c.sh->inside);
    return r;
}

/* Render thread, in the kick hook before its original: no world has upload jobs out, so no LwSkrObj runs (inside is
 * the second guard), and none starts before this thread kicks again. */
static void LwSkrServeClear(void)
{
    if (!g_lwSkr.clearReq) return;
    for (uint32_t i = 0; i < LW_SKR_SHARDS; i++) if (g_lwSkrShard[i].inside) { InterlockedIncrement64(&g_lwSkr.clearDeferred); return; }
    for (LONG i = 0; i < g_lwSkin.nWorlds; i++)
    {
        uint32_t st = 0;
        if (LwSkinLive(&g_lwSkin.w[i]) && !SafeRead(g_lwSkin.w[i].S + 0x184, &st, 4) && st == 1) { InterlockedIncrement64(&g_lwSkr.clearDeferred); return; }
    }
    InterlockedIncrement(&g_lwSkr.epoch);
    InterlockedExchange64(&g_lwSkr.arenaUsed, 0); InterlockedExchange(&g_lwSkr.slotsUsed, 0);
    InterlockedExchange(&g_lwSkr.clearReq, 0); InterlockedIncrement64(&g_lwSkr.clears);
}

static int LwSkrInstall(void)
{
    if (g_lwSkr.hooked) return g_lwSkr.hooked > 0;
    g_lwSkr.hooked = -1;
    uintptr_t exe = g_lwSkr.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!LwSkinHook()) return 0;
    static const struct { uint32_t rva; const char* bytes; } pins[] = {
        {0x512B80u, "40 55 57 48 8D 6C 24 A8 48 81 EC 58 01 00 00 48 83 79 70 00"},
        {0x512C1Bu, "8B 81 C8 00 00 00 8D 0C 40 C1 E1 04 F0 0F C1 8A 78 01 00 00 44 8B E1 8B F1 48 03 B2 70 01 00 00 41 C1 EC 04"},
        {0x512C3Fu, "4D 85 C0 75 07 44 89 A7 C4 00 00 00"}, {0x513061u, "4C 8B BC 24 40 01 00 00 41 8B C4"},
        {0x512F1Du, "48 8B 9F 98 00 00 00 8B 87 90 00 00 00"}, {0x512F80u, "4C 8B 4F 70 B8 AB AA AA AA F7 63 1C 41 8B 41 0C"},
        {0x512FA2u, "49 8D 0C 01 8B 43 18 44 8B 44 C1 04 44 8B 5C C1 08 4C 03 C1"}, {0x512FEAu, "4C 8B 4F 70 B8 AB AA AA AA F7 63 20"},
        {0x513054u, "48 83 C3 28"}, {0x510C19u, "49 8B 80 C0 00 00 00 41 8B 88 C8 00 00 00 89 4D B7 48 85 C0 74 08 8B 40 04 89 45 BB"},
        {0x25A038u, "48 8D 05 01 EE FF FF"},
    };
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++)
        if (!ApiCodeMatches(exe + pins[i].rva, pins[i].bytes)) { Note("living world: skin reuse: exe 0x%X is not the reviewed code; off", pins[i].rva); return 0; }
    if (LoaderHook("living world skin palette reuse", exe + 0x512B80u, (void*)LwSkrObj, &g_lwSkr.orig) || !g_lwSkr.orig)
    { Note("living world: skin reuse: the hook on 0x512B80 failed; off"); return 0; }
    g_lwSkr.hooked = 1;
    return 1;
}
