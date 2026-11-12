/* Mission locations' loose entities (pickups, markers, samples, lights) spawned when a player comes near, not at setup
 * (KB living-world-density, "Deferring mission-location entities"). On an extra large map they fill about half of the
 * synchronizer's 2048 entries before any enemy, because the setup pass 0xFDA650 spawns every level's entities map-wide.
 * The patch hands that pass's id allocation and spawn to LwDeferAt, which does both now (as the pass did) or keeps
 * the entry's params, info and list (+0x50, which other spawn sites set) and does both later. Site 1 is for samples.
 * Only when they spawn changes, not which machine spawns them.
 * Included by living_world.c after living_world_density.c. */

#define LW_DEFER_SITE 0xFDAA8Au   /* the allocation and SpawnEntity call of the setup pass, 44 bytes to 0xFDAAB6 */
#define LW_DEFER_SPAWN 0xFDC140u  /* SpawnEntity(state, id, resource, params) */
#define LW_DEFER_API 0x3326308u
#define LW_DEFER_ID_TYPE 0xB04D927AFB137B9Bull
#define LW_DEFER_MAX 4096
#define LW_DEFER_PARAMS 0x58      /* +8 the unit's pose (translation +0x38), +0x48 the info */
#define LW_DEFER_INFO 0x8A0       /* +0xC {table key, level unit}: the entity adopts the level's unit */
#define LW_DEFER_KEEP 800.0f      /* spawned at setup within this of the drop point */
#define LW_DEFER_NEAR 600.0f      /* spawned once a player is within this */
#define LW_DEFER_HARD 2000u       /* never past this many networked entities (the list holds 2048, unchecked) */
#define LW_DEFER_LIST 0x58        /* params +0x50 at other sites: a count, then up to 10 qwords (0x12E5590) */
#define LW_DEFER_STATE 0x346BF98u /* the entity state both passes spawn into */
#define LW_DEFER_SITES 2
#define LW_DEFER_SAMPLE_SITE 0x79D5EFu /* the sample spawner 0x79D180's allocation and SpawnEntity call, 65 bytes to 0x79D630 */

static const char kLwDeferPin[] = "48 8B 90 C0 00 00 00 48 8B 02 48 BA 9B 7B 13 FB 7A 92 4D B0 4C 8B 48 18 41 FF D1 "
                                  "4C 8D 4D B0 4C 8B C3 8B D0 49 8B CC E8 8A 16 00 00";

static const char kLwDeferSamplePin[] = "48 8B 05 12 8D B8 02 4C 8D 45 98 48 8B 3D 97 E9 CC 02 48 8B 90 C0 00 00 00 48 8B 0F 48 8B 02 "
                                        "48 BA 9B 7B 13 FB 7A 92 4D B0 4C 8B 48 18 41 FF D1 4C 8D 4D 90 4C 8B C3 8B D0 48 8B CF E8 10 EB 83 00";

/* Pickups, markers, lights and samples: entities nothing links at setup (kLwDeferResources, sorted). */
#include "living_world_defer_list.h"

typedef struct
{
    uint64_t resource;
    float x, y;
    int site, extra;              /* extra: the params' +0x50 list was set */
    uint8_t params[LW_DEFER_PARAMS], info[LW_DEFER_INFO], list[LW_DEFER_LIST];
} LwDeferred;
typedef struct { uintptr_t state, level; uint32_t seed; float drop[2]; ULONGLONG lastEntry; int kept; } LwDeferPass;
static struct
{
    LwDeferred* list;
    int count, spawned, installed;
    LwDeferPass pass[LW_DEFER_SITES]; /* each patched site's own pass: the order the two run in does not matter */
} g_lwDefer;

typedef uint32_t (*LwDeferIdFn)(uintptr_t owner, uint64_t type, const void* pose);
typedef uint64_t (*LwDeferSpawnFn)(uintptr_t state, uint32_t id, uint64_t resource, const void* params);

/* What the patched passes did at this point: an id for the entity, then SpawnEntity. */
static void LwDeferSpawnNow(uintptr_t state, uint64_t resource, const uint8_t* params)
{
    uintptr_t api = *(uintptr_t*)(g_lw.game + LW_DEFER_API), allocator = *(uintptr_t*)(api + 0xC0);
    LwDeferIdFn id = *(LwDeferIdFn*)(*(uintptr_t*)allocator + 0x18);
    uint32_t entity = id(*(uintptr_t*)state, LW_DEFER_ID_TYPE, params + 8);
    ((LwDeferSpawnFn)(g_lw.game + LW_DEFER_SPAWN))(state, entity, resource, params);
}

static int LwDeferListed(uint64_t resource)
{
    size_t lo = 0, hi = sizeof kLwDeferResources / sizeof kLwDeferResources[0];
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (kLwDeferResources[mid] == resource) return 1;
        if (kLwDeferResources[mid] < resource) lo = mid + 1; else hi = mid;
    }
    return 0;
}

/* The squad's drop point from the war board (difficulty_stream.c), else the map's centre. */
static void LwDeferDropPoint(float drop[2])
{
    uint64_t board = 0; uint8_t flag = 0; float at[2] = {0, 0};
    if (!SafeRead(g_lw.game + DIF_STREAM_BOARD, &board, 8) && board != 0 && !SafeRead((uintptr_t)board + DIF_STREAM_BOARD_FLAG, &flag, 1) &&
        flag == 1 && (SafeRead((uintptr_t)board + DIF_STREAM_BOARD_X, &at[0], 4) || SafeRead((uintptr_t)board + DIF_STREAM_BOARD_Y, &at[1], 4)))
        at[0] = at[1] = 0;
    drop[0] = isfinite(at[0]) ? at[0] : 0; drop[1] = isfinite(at[1]) ? at[1] : 0;
}

/* Drops the site's waiting entries and every spawned one, keeping the others in order. */
static void LwDeferForget(int site)
{
    int kept = 0;
    for (int i = 0; i < g_lwDefer.count; i++)
    {
        LwDeferred* d = &g_lwDefer.list[i];
        if (d->resource == 0 || d->site == site) continue;
        if (kept != i) g_lwDefer.list[kept] = *d;
        kept++;
    }
    g_lwDefer.count = kept; g_lwDefer.spawned = 0;
}

/* A new pass at this site: another mission (level or seed), or entries a minute apart (the same mission loaded again).
 * Its earlier entries are forgotten. The sample spawner runs in slices of 10, so a short gap is not a new pass. */
static int LwDeferPassStart(int site, uintptr_t state)
{
    LwDeferPass* p = &g_lwDefer.pass[site];
    ULONGLONG now = GetTickCount64();
    uintptr_t lg = LwLevel(); uint32_t seed = 0;
    if (lg == 0 || SafeRead(lg + 0x8, &seed, 4)) return 0;
    int fresh = p->lastEntry == 0 || now - p->lastEntry > 60000 || lg != p->level || seed != p->seed;
    p->lastEntry = now;
    if (!fresh) return 1;
    if (g_lwDefer.list != NULL) LwDeferForget(site);
    p->state = state; p->level = lg; p->seed = seed; p->kept = 0;
    LwDeferDropPoint(p->drop);
    return 1;
}

/* Only the host's living world ticks (LivingTickBody), so only the host may keep entries back: a client's would never
 * spawn. The game's network context [game+0x347CEF0]: +0xB3A8 the host's peer (0 = this machine), +0xB398 this one's. */
static int LwDeferHostHere(void)
{
    uintptr_t peers = 0; uint64_t host = 1, local = 0;
    if (SafeRead(g_lw.game + 0x347CEF0u, &peers, 8) || peers == 0 || SafeRead(peers + 0xB3A8u, &host, 8) || SafeRead(peers + 0xB398u, &local, 8)) return 0;
    return host == 0 || host == local;
}

static int LwDeferWanted(int site, uintptr_t state, uint64_t resource, const uint8_t* params, float* at)
{
    if (!LwDeferListed(resource) || !LwDeferHostHere() || !(g_world.live > 2.0f) || LwDensityOn() != 1 || !LwDeferPassStart(site, state)) return 0;
    if (g_lwDefer.list == NULL) g_lwDefer.list = VirtualAlloc(NULL, sizeof(LwDeferred) * LW_DEFER_MAX, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lwDefer.list == NULL || g_lwDefer.count >= LW_DEFER_MAX) return 0;
    memcpy(at, params + 0x38, 8);
    if (!isfinite(at[0]) || !isfinite(at[1])) return 0;
    const LwDeferPass* p = &g_lwDefer.pass[site];
    float dx = at[0] - p->drop[0], dy = at[1] - p->drop[1];
    return dx * dx + dy * dy > LW_DEFER_KEEP * LW_DEFER_KEEP;
}

/* Called from a patched pass for every entry, on the game thread. The info (+0x48) and the list (+0x50) are the pass's
 * stack, so both are copied; the setup pass leaves +0x50 at 0. */
static void LwDeferAt(int site, uintptr_t state, uint64_t resource, uint8_t* params)
{
    float at[2];
    if (!LwDeferWanted(site, state, resource, params, at)) { LwDeferSpawnNow(state, resource, params); return; }
    LwDeferred* d = &g_lwDefer.list[g_lwDefer.count++];
    uintptr_t info, list;
    memcpy(&info, params + 0x48, 8); memcpy(&list, params + 0x50, 8);
    d->resource = resource; d->x = at[0]; d->y = at[1]; d->site = site; d->extra = list != 0;
    memcpy(d->params, params, LW_DEFER_PARAMS);
    memcpy(d->info, (const void*)info, LW_DEFER_INFO);
    if (list != 0) memcpy(d->list, (const void*)list, LW_DEFER_LIST);
    g_lwDefer.pass[site].kept++;
}

static void LwDeferEntry(uintptr_t state, uint64_t resource, uint8_t* params) { LwDeferAt(0, state, resource, params); }

/* The sample spawner reads the state itself ([game+0x346BF98], as rdi there). */
static void LwDeferSampleEntry(uint64_t resource, uint8_t* params) { LwDeferAt(1, *(uintptr_t*)(g_lw.game + LW_DEFER_STATE), resource, params); }

/* An entry whose pass belongs to another mission, or to another state, is never spawned. */
static int LwDeferCurrent(const LwDeferred* d, uintptr_t lg, uint32_t seed, uintptr_t state)
{
    const LwDeferPass* p = &g_lwDefer.pass[d->site];
    return p->level == lg && p->seed == seed && p->state == state;
}

/* Spawns the kept entries a player has come near: 16 a tick at most, never past the list's bound. */
static void LwDeferTick(uintptr_t lg, uint32_t seed)
{
    if (g_lwDefer.count == 0 || g_lwDefer.spawned == g_lwDefer.count || g_lwWorld.playersAt == 0) return;
    uintptr_t state;
    if (!EvpPtr(g_lw.game + LW_DEFER_STATE, &state) || LwSyncCount() >= LW_DEFER_HARD) return;
    int budget = 16;
    for (int i = 0; i < g_lwDefer.count && budget > 0; i++)
    {
        LwDeferred* d = &g_lwDefer.list[i];
        if (d->resource == 0 || LwNearestPlayer2(d->x, d->y) > LW_DEFER_NEAR * LW_DEFER_NEAR) continue;
        if (!LwDeferCurrent(d, lg, seed, state)) { d->resource = 0; g_lwDefer.spawned++; continue; } /* another mission */
        uintptr_t info = (uintptr_t)d->info, list = d->extra ? (uintptr_t)d->list : 0;
        memcpy(d->params + 0x48, &info, 8); memcpy(d->params + 0x50, &list, 8);
        LwDeferSpawnNow(state, d->resource, d->params);
        d->resource = 0; g_lwDefer.spawned++; budget--;
    }
}

static void LwDeferNote(void)
{
    if (g_lwDefer.count == 0) return;
    int pick = -1; float best = 0;
    for (int i = 0; i < g_lwDefer.count; i++)
    {
        float d2 = g_lwDefer.list[i].resource ? LwNearestPlayer2(g_lwDefer.list[i].x, g_lwDefer.list[i].y) : 0;
        if (g_lwDefer.list[i].resource && (pick < 0 || d2 < best)) { pick = i; best = d2; }
    }
    Note("living world: deferred entities: %d kept (setup pass %d, sample pass %d), %d spawned near players since; the nearest waiting %016llx "
        "at (%.0f, %.0f), %.0f m", g_lwDefer.count, g_lwDefer.pass[0].kept, g_lwDefer.pass[1].kept, g_lwDefer.spawned,
        pick < 0 ? 0ull : (unsigned long long)g_lwDefer.list[pick].resource, pick < 0 ? 0.0 : (double)g_lwDefer.list[pick].x,
        pick < 0 ? 0.0 : (double)g_lwDefer.list[pick].y, (double)sqrtf(best));
}

/* Writes a site's bytes: the call to the entry, a jmp to resume, the rest int3. */
static int LwDeferPatch(uintptr_t site, size_t size, const uint8_t* head, size_t headSize, void* entry)
{
    uint8_t patch[96];
    if (size > sizeof patch || size < headSize + 12) return 1;
    memset(patch, 0xCC, sizeof patch);
    memcpy(patch, head, headSize);
    uint64_t fn = (uint64_t)(uintptr_t)entry;
    memcpy(patch + headSize, &fn, 8);
    size_t at = headSize + 8;
    patch[at] = 0xFF; patch[at + 1] = 0xD0; patch[at + 2] = 0xEB; patch[at + 3] = (uint8_t)(size - (at + 4));
    return ApiWrite(site, patch, size);
}

/* The setup pass's 44 bytes become: mov rcx,r12; mov rdx,rbx; lea r8,[rbp-0x50]; mov rax,LwDeferEntry; call rax;
 * jmp 0xFDAAB6. The loop after 0xFDAAB6 reads only nonvolatile registers (rsi, r15, rbp, r12, r13).
 * The sample spawner's 65 become: mov rcx,rbx; lea rdx,[rbp-0x70]; mov rax,LwDeferSampleEntry; call rax; jmp 0x79D630.
 * Its params are rbp-0x70, the info rbp-0x10 and the list (+0x50) rbp+0x890; 0x79D630 on sets edi and r8 again. */
static void LwInstallSpawnSources(void);
static void LwInstallRender(void); /* living_world_render.c */
static void LwInstallDefer(void)
{
    if (!LwEnabled() || g_lwDefer.installed) return;
    LwInstallAlloc(); /* the extra large world's own guards go in with the complex part (../co_xl.c) */
    uintptr_t game = g_lw.game;
    /* entries are kept back only with the density guards in the game's code; without them the patch would only
     * reroute every mission's setup pass */
    if (LwDensityOn() != 1)
    { Note("living world: no density guards: mission locations' loose entities spawn at setup"); LwInstallSpawnSources(); LwInstallPassTimes(); return; }
    if (!ApiCodeMatches(game + LW_DEFER_SITE, kLwDeferPin) || !ApiCodeMatches(game + LW_DEFER_SPAWN, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18"))
    { Note("living world: the level entities' setup pass is not the reviewed code; its entities spawn at setup"); return; }
    static const uint8_t setup[] = {0x49, 0x8B, 0xCC, 0x48, 0x8B, 0xD3, 0x4C, 0x8D, 0x45, 0xB0, 0x48, 0xB8};
    if (LwDeferPatch(game + LW_DEFER_SITE, 44, setup, sizeof setup, (void*)LwDeferEntry))
    { Note("living world: the level entities' setup pass could not be written"); return; }
    g_lwDefer.installed = 1;
    Note("living world: mission locations' loose entities wait until a player is within %.0f m (extra large maps, density on)", (double)LW_DEFER_NEAR);
    static const uint8_t sample[] = {0x48, 0x8B, 0xCB, 0x48, 0x8D, 0x55, 0x90, 0x48, 0xB8};
    if (!ApiCodeMatches(game + LW_DEFER_SAMPLE_SITE, kLwDeferSamplePin) ||
        LwDeferPatch(game + LW_DEFER_SAMPLE_SITE, 65, sample, sizeof sample, (void*)LwDeferSampleEntry))
        Note("living world: the sample spawner is not the reviewed code; samples spawn at setup");
    else Note("living world: samples wait as well (the sample spawner, game.dll 0x%X)", LW_DEFER_SAMPLE_SITE);
    LwInstallSpawnSources();
    LwInstallPassTimes();
}

/* Test trace only: which SpawnEntity call site spawns which resource, so a deferral patch goes where the entities
 * really come from (113 sites call 0xFDC140, several through handler tables). Printed with the census. */
typedef struct { uint64_t resource; uint32_t caller, count; } LwSpawnSource;
static LwSpawnSource g_lwSpawnSrc[1024];
static volatile LONG g_lwSpawnSrcLock, g_lwSpawnSrcUsed, g_lwSpawnSrcLost;
static LwDeferSpawnFn g_lwSpawnOrig;

static void LwSpawnSourceAdd(uint64_t resource, uintptr_t ret)
{
    uintptr_t game = g_lw.game;
    uint32_t caller = ret >= game && ret - game < 0x4000000u ? (uint32_t)(ret - game) : 0xFFFFFFFFu; /* not game.dll: the loader */
    while (InterlockedCompareExchange(&g_lwSpawnSrcLock, 1, 0) != 0) YieldProcessor();
    LONG i = 0;
    while (i < g_lwSpawnSrcUsed && (g_lwSpawnSrc[i].resource != resource || g_lwSpawnSrc[i].caller != caller)) i++;
    if (i == g_lwSpawnSrcUsed && i < (LONG)(sizeof g_lwSpawnSrc / sizeof g_lwSpawnSrc[0]))
    { g_lwSpawnSrc[i].resource = resource; g_lwSpawnSrc[i].caller = caller; g_lwSpawnSrcUsed++; }
    if (i < g_lwSpawnSrcUsed) g_lwSpawnSrc[i].count++; else g_lwSpawnSrcLost++;
    InterlockedExchange(&g_lwSpawnSrcLock, 0);
}

static uint64_t LwSpawnSourceDetour(uintptr_t state, uint32_t id, uint64_t resource, const void* params)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (ret - g_lw.game >= 0x4000000u) ret = g_spawnCaller; /* behind the spawn event's hook (event_sources.c) */
    LwSpawnSourceAdd(resource, ret);
    return g_lwSpawnOrig(state, id, resource, params);
}

static void LwSpawnSourcesNote(int seq)
{
    while (InterlockedCompareExchange(&g_lwSpawnSrcLock, 1, 0) != 0) YieldProcessor();
    for (LONG i = 0; i < g_lwSpawnSrcUsed; i++)
        Note("living world: spawn source %d: %016llx from 0x%X x%u", seq, (unsigned long long)g_lwSpawnSrc[i].resource, g_lwSpawnSrc[i].caller,
            g_lwSpawnSrc[i].count);
    Note("living world: spawn sources %d: %ld pairs, %ld calls not counted (table full)", seq, (long)g_lwSpawnSrcUsed, (long)g_lwSpawnSrcLost);
    InterlockedExchange(&g_lwSpawnSrcLock, 0);
}

static void LwInstallSpawnSources(void)
{
    if (!LwTestTraceOn() || g_lwSpawnOrig != NULL) return;
    if (LoaderHook("living world: spawn sources (test trace)", g_lw.game + LW_DEFER_SPAWN, (void*)LwSpawnSourceDetour, (void**)&g_lwSpawnOrig) ||
        g_lwSpawnOrig == NULL)
    { g_lwSpawnOrig = NULL; Note("living world: the spawn source trace could not hook 0x%X", LW_DEFER_SPAWN); return; }
    Note("living world: test: spawn sources are counted (SpawnEntity, game.dll 0x%X)", LW_DEFER_SPAWN);
}
