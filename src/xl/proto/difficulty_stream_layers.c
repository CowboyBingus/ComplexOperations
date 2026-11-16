/* Extra large maps: the streamed stamps' static bodies in broad phase layers of their own (included after
 * difficulty_stream_window.c). A static add or remove marks its layer (0xD3D1DA) and the next step rebuilds that whole
 * layer (0xD3DAA9); with every static body in layer 4, one stamp rebuilt the level's 330-460k static leaves. Layers
 * 6..9 are layer 4 again for every reader, and each change frame's stamp bodies go to one of them:
 *
 * - The layer count is the config's +0x18 (0xDB9EE0, `mov eax,6`), read once as a broad phase is made (0xD3C672); each
 *   layer's {type, pair mask, flags} is the config's +0x20 (0xDB9DB0, nothing written past 5) into [L+0x1AE0].
 * - Queries pick layers by the type byte against the query's include and exclude bytes, not by index (0xD3E25C,
 *   0xD3EFBC, 0xD3FA0C, 0xD3FD0C, 0xD40038, 0xD41378, 0xD41698, 0xD4190C); pairs are (i, j) when either mask names the
 *   other (0xD40256), so mask 1 pairs with layer 0 alone, as layer 4 does. A layer with no tree ([L+0x20] 0, 0xD3E24B)
 *   or no leaves (0xD4020C, 0xD3DAB9) is skipped.
 * - A body's layer is the low 5 bits of body +0x74 (0xD3D17C, 0xD41B84); no exe code outside the broad phase reads them.
 * - The layer rule 0xDB9E60(cfg, ids, n, bodies, out) runs in the add and in the body update (0xD40674). Every unit's
 *   body is made by the world writer's +0x50, 0xD09080(W+0x18, u32* id, cinfo), and added later (command 02 in the
 *   step). So bodies made on the game thread while a change frame spawns and flushes the spawn ring (0x12D600, as
 *   DestroyUnit does at 0x3389A8) are remembered by index, and the rule's layer 4 for them becomes the frame's layer. */

#define SL_FIRST 6u                 /* the first streamed layer */
#define SL_COUNT 4u                 /* streamed layers: a change frame rebuilds about a quarter of the streamed leaves */
#define SL_BODIES (1u << 20)        /* body indexes remembered (the mission world's store holds 1048576) */
#define SL_COUNT_RVA 0xDB9EE0u
#define SL_INFO_RVA 0xDB9DB0u
#define SL_RULE_RVA 0xDB9E60u
#define SL_CREATE_RVA 0xD09080u
#define SL_FLUSH_RVA 0x12D600u
#define SL_COUNT_SITE 0xD3C66Cu     /* the broad phase constructor 0xD3C450's call of the count; it returns to +9 */
#define SL_COUNT_BACK 0xD3C675u
#define SL_ENGINE 0x3326340u        /* game.dll: the set-up's engine object */
#define SL_ENGINE_ID 0x10F0u        /* its main world's physics id (stored at 0xAB1B31) */
#define SL_RECORDS 0x27BA870u       /* exe: the 4 physics worlds' records of 0xB0 (0x78D3A8), the hknp world at +0x20 (0x78D8D4) */
static const char kSlCountPin[] = "B8 06 00 00 00 C3";
static const char kSlInfoPin[] = "0F B6 C2 83 F8 05 0F 87 8A 00 00 00 48 8D 15";
static const char kSlRulePin[] = "4D 63 D8 4C 8B D2 45 85 C0 7E 6E 48 8B 44 24 28 45 33 C0 41 8B 0A 81 E1";
static const char kSlCreatePin[] = "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 56 41 57 48 8D AC 24 C8 FE FF FF";
static const char kSlFlushPin[] = "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 98 F6 FF FF";
static const char kSlCountSitePin[] = "49 8B 06 49 8B CE FF 50 18 8B 53 1C";

typedef char SlLayersMatch[SL_LAYERS == 1u + SL_COUNT ? 1 : -1]; /* difficulty_stream.c's count: layer 4, then these */
static int MoveSpawnRingEmpty(uint64_t world); /* difficulty_world_move.c */
typedef int (*SlCountFn)(void* cfg);
typedef void (*SlInfoFn)(void* cfg, uint8_t layer, uint8_t* out);
typedef void (*SlRuleFn)(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out);
typedef void* (*SlCreateFn)(void* writer, uint32_t* id, void* cinfo, void* r9);
typedef void (*SlFlushFn)(uint64_t world);

static struct
{
    int layers;                 /* the count and info hooks are in: broad phases made since have layers 6..9 */
    int on;                     /* the rule and body hooks too: bodies go to 6..9 */
    uintptr_t exe, game;        /* game.dll, read once: SlWorld runs for every body made */
    int count;                  /* the main world's broad phase's layers, read as the rule and body hooks went in */
    char why[96];               /* SlInstall's refusal with that count */
    int asked;                  /* HELLFORGE_XL_STREAM_LAYERS=1 was set at SlEarly: the layers are off without it */
    void *countOrig, *infoOrig, *ruleOrig, *createOrig;
    volatile LONG thread;       /* the change frame's thread while it spawns and flushes (0: none) */
    volatile LONG current;      /* that frame's layer */
    uintptr_t okBp;             /* the mission world's broad phase, checked to hold layers 6..9 as layer 4 */
    volatile LONG marked, routed, created; /* since the note: bodies remembered, sent to 6..9 by the rule, made in all */
    volatile LONG any;          /* a body was ever made in a change frame: until then the rule has nothing to send */
    uint8_t layerOf[SL_BODIES]; /* a body index's streamed layer, 0: none */
} g_sl;

/* The main world W (made once, in the game's set-up) and its broad phase ([W+0x4C0], stored by the world's
 * constructor at 0xD062BB; vtable +0x38 the update 0xD40550). W is the record of the main world's physics id: the
 * slot the ring gave it (2 in the runs, [exe+0x27BA9F0]), read rather than assumed. 0 when any does not read. Id 0 is
 * a slot too, so an id not yet stored (0 from the engine's publish, 0x4EE564, to 0xAB1B31) would read as slot 0: every
 * caller runs after the set-up (the hooks are gated on `on`, the rest run from the streamer). */
static int SlWorld(uintptr_t exe, uintptr_t* w, uintptr_t* bp)
{
    uintptr_t vt = 0, fn = 0, engine = 0;
    uint32_t id = 0;
    *w = *bp = 0;
    if (g_sl.game == 0) g_sl.game = ApiModuleBase("game.dll", NULL);
    return exe != 0 && g_sl.game != 0 && !SafeRead(g_sl.game + SL_ENGINE, &engine, 8) && !SafeRead(engine + SL_ENGINE_ID, &id, 4) &&
        id < 4 && !SafeRead(exe + SL_RECORDS + id * 0xB0u + 0x20u, w, 8) && *w != 0 && !SafeRead(*w + 0x4C0, bp, 8) && *bp != 0 &&
        !SafeRead(*bp, &vt, 8) && !SafeRead(vt + 0x38, &fn, 8) && fn == exe + 0xD40550u;
}

static int SlLayerCount(void);
static int SlMainLayers(void) { return g_sl.count; } /* as SlInstall read it */

/* The broad phase holds layers 6..9 with layer 4's type and pair mask: only then may the rule send a body there (a
 * broad phase made before the hooks has 6 layers, and an unset layer's type 0 would hide its bodies from every query). */
static int SlBroadPhaseReady(uintptr_t bp)
{
    if (bp == g_sl.okBp) return 1;
    int32_t count = 0; uintptr_t layers = 0;
    if (SafeRead(bp + 0x18, &count, 4) || count < (int32_t)(SL_FIRST + SL_COUNT) || count > 32 || SafeRead(bp + 0x10, &layers, 8) || layers == 0) return 0;
    for (uint32_t k = 4; k < SL_FIRST + SL_COUNT; k++)
    {
        uintptr_t l = 0; uint8_t type = 0; uint32_t mask = 0;
        if (k == 5) continue;
        if (SafeRead(layers + k * 8u, &l, 8) || l == 0 || SafeRead(l + 0x1AE0, &type, 1) || SafeRead(l + 0x1AE4, &mask, 4) || type != 1 || mask != 1) return 0;
    }
    g_sl.okBp = bp;
    return 1;
}

/* 10 layers only for the broad phase whose layers grow with the count (0xD3C450, called back at SL_COUNT_BACK): the
 * hybrid broad phase 0xD2DA60 asks the same config (0xD2DC47) and keeps 8 layers in inline arrays (`cmp cl,8`
 * 0xD2DCFA), so a 9th would write over its count at +0x88. */
static int SlCountFrom(void* cfg, uintptr_t back)
{
    const int n = ((SlCountFn)g_sl.countOrig)(cfg);
    return g_sl.layers && n == 6 && back == g_sl.exe + SL_COUNT_BACK ? (int)(SL_FIRST + SL_COUNT) : n;
}
static int SlCount(void* cfg) { return SlCountFrom(cfg, (uintptr_t)__builtin_return_address(0)); }

static void SlInfo(void* cfg, uint8_t layer, uint8_t* out)
{
    if (g_sl.layers && layer >= SL_FIRST && layer < SL_FIRST + SL_COUNT)
    {
        const uint32_t mask = 1; const uint16_t flags = 0;
        out[0] = 1; memcpy(out + 4, &mask, 4); memcpy(out + 8, &flags, 2); /* layer 4's, 0xDB9E1F */
        return;
    }
    ((SlInfoFn)g_sl.infoOrig)(cfg, layer, out);
}

/* The rule's layer 4 answers for the mission world's bodies: the layer the body was made in. Bodies reach the broad
 * phase after they are made (command 02 replayed in the step; 0xD09080 does not touch [writer+0x4A8]), so the index is
 * marked by then; a stale mark only moves a static body between layers that answer every query alike. */
static void SlRoute(const uint32_t* ids, int n, uint8_t* out, const uint8_t* layerOf)
{
    for (int i = 0; i < n; i++)
    {
        if (out[i] != 4) continue;
        const uint32_t index = ids[i] & 0xFFFFFFu;
        const uint32_t to = index < SL_BODIES ? layerOf[index] : 0;
        if (to >= SL_FIRST && to < SL_FIRST + SL_COUNT) { out[i] = (uint8_t)to; InterlockedIncrement(&g_sl.routed); }
    }
}

static void SlRule(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out)
{
    ((SlRuleFn)g_sl.ruleOrig)(cfg, ids, n, bodies, out);
    uintptr_t w = 0, bp = 0, list = 0;
    if (!g_sl.on || !g_sl.any || n <= 0 || !SlWorld(g_sl.exe, &w, &bp) || SafeRead(w + 0x38, &list, 8) || list != (uintptr_t)bodies || !SlBroadPhaseReady(bp)) return;
    SlRoute(ids, n, out, g_sl.layerOf);
}

/* Who makes the mission world's bodies (run 2c: ~38k a 10 s window outside the change frames, ANALYSIS-r2a §2.4): one
 * in SL_SAMPLE_EVERY has its first SL_SAMPLE_DEPTH return addresses outside this module kept, as exe or game.dll RVAs,
 * with how often each chain came, on the game thread, in a change frame and in the frame after one (by the streamer's
 * tick, g_slChangeTick: with the layers off no spawn window is open). The 10 s note names the most frequent. */
#define SL_SAMPLE_EVERY 64u
#define SL_SAMPLE_DEPTH 4
#define SL_SAMPLE_KEEP 48
#define SL_SAMPLE_NAMED 4
typedef struct { uint32_t rva[SL_SAMPLE_DEPTH]; char mod[SL_SAMPLE_DEPTH]; uint32_t count, game, change, after; } SlChain;
static struct
{
    volatile LONG calls;        /* the mission world's bodies made since the note */
    volatile LONG made[3];      /* ... in other frames, in a change frame, in the frame after one */
    SRWLOCK lock;
    SlChain e[SL_SAMPLE_KEEP];
    uint32_t used, missed, sampled;
    uintptr_t selfLo, selfHi;   /* this module's image: its frames are left out */
} g_slSample = {.lock = SRWLOCK_INIT};

/* A return address as a module and RVA: 'e' the exe, 'g' game.dll (images under 256 MB), '?' the low 32 bits. */
static char SlWhere(uintptr_t a, uint32_t* rva)
{
    if (g_sl.exe != 0 && a >= g_sl.exe && a - g_sl.exe < 0x10000000u) { *rva = (uint32_t)(a - g_sl.exe); return 'e'; }
    if (g_sl.game != 0 && a >= g_sl.game && a - g_sl.game < 0x10000000u) { *rva = (uint32_t)(a - g_sl.game); return 'g'; }
    *rva = (uint32_t)a;
    return '?';
}

/* A chain into the table: counted on the one it matches, else a new entry while there is room. */
static void SlChainAdd(const uintptr_t* frames, int n, int game, int mark)
{
    SlChain c;
    memset(&c, 0, sizeof c);
    for (int k = 0; k < n && k < SL_SAMPLE_DEPTH; k++) c.mod[k] = SlWhere(frames[k], &c.rva[k]);
    if (!TryAcquireSRWLockExclusive(&g_slSample.lock)) { g_slSample.missed++; return; } /* another thread's sample: rare, counted */
    g_slSample.sampled++;
    uint32_t k = 0;
    while (k < g_slSample.used && (memcmp(g_slSample.e[k].rva, c.rva, sizeof c.rva) != 0 || memcmp(g_slSample.e[k].mod, c.mod, sizeof c.mod) != 0)) k++;
    if (k == g_slSample.used && k < SL_SAMPLE_KEEP) g_slSample.e[g_slSample.used++] = c;
    if (k < g_slSample.used) { g_slSample.e[k].count++; g_slSample.e[k].game += game != 0; g_slSample.e[k].change += mark == 1; g_slSample.e[k].after += mark == 2; }
    else g_slSample.missed++;
    ReleaseSRWLockExclusive(&g_slSample.lock);
}

/* 1 in a change frame that changed something, 2 in the frame after it, 0 else: a frame runs from one streamer tick to
 * the next, and a change frame's bodies are made after its tick (its flush, the world's flush, the world update). */
static int SlChangeMark(void)
{
    const uint32_t c = g_slChangeTick, t = g_streamTicks;
    return c == 0 ? 0 : t == c ? 1 : t == c + 1u ? 2 : 0;
}

static void SlSample(int mark)
{
    InterlockedIncrement(&g_slSample.made[mark >= 0 && mark < 3 ? mark : 0]);
    if ((uint32_t)InterlockedIncrement(&g_slSample.calls) % SL_SAMPLE_EVERY != 0) return;
    if (g_slSample.selfHi == 0)
    {
        HMODULE self = NULL;
        g_slSample.selfHi = 1;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void*)SlSample, &self) && self != NULL)
        {
            const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)(void*)self;
            const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(void*)((const uint8_t*)self + dos->e_lfanew);
            g_slSample.selfLo = (uintptr_t)self; g_slSample.selfHi = g_slSample.selfLo + nt->OptionalHeader.SizeOfImage;
        }
    }
    void* raw[16];
    uintptr_t keep[SL_SAMPLE_DEPTH];
    const USHORT got = RtlCaptureStackBackTrace(0, 16, raw, NULL);
    int n = 0;
    for (USHORT k = 0; k < got && n < SL_SAMPLE_DEPTH; k++)
    {
        const uintptr_t a = (uintptr_t)raw[k];
        if (a >= g_slSample.selfLo && a < g_slSample.selfHi) continue;
        keep[n++] = a;
    }
    SlChainAdd(keep, n, g_layerGameThread != 0 && (DWORD)g_layerGameThread == GetCurrentThreadId(), mark);
}

/* The note's part: the bodies made, the samples, and the SL_SAMPLE_NAMED most frequent chains; the table starts again. */
static void SlSampleText(char* out, size_t room)
{
    size_t o = 0;
    AcquireSRWLockExclusive(&g_slSample.lock);
    const long change = InterlockedExchange(&g_slSample.made[1], 0), after = InterlockedExchange(&g_slSample.made[2], 0), other = InterlockedExchange(&g_slSample.made[0], 0);
    o += (size_t)snprintf(out + o, room - o, "%ld bodies made (%ld in change frames, %ld in the frames after them, %ld in others), %u sampled (1 in %u), %u not kept",
        (long)InterlockedExchange(&g_slSample.calls, 0), change, after, other, g_slSample.sampled, SL_SAMPLE_EVERY, g_slSample.missed);
    uint8_t named[SL_SAMPLE_KEEP] = {0};
    for (int n = 0; n < SL_SAMPLE_NAMED && o < room; n++)
    {
        int best = -1;
        for (uint32_t k = 0; k < g_slSample.used; k++) if (!named[k] && (best < 0 || g_slSample.e[k].count > g_slSample.e[best].count)) best = (int)k;
        if (best < 0) break;
        named[best] = 1;
        const SlChain* c = &g_slSample.e[best];
        o += (size_t)snprintf(out + o, room - o, "; %u from", c->count);
        for (int d = 0; d < SL_SAMPLE_DEPTH && o < room && c->mod[d]; d++) o += (size_t)snprintf(out + o, room - o, "%s%c+0x%X", d ? " < " : " ", c->mod[d], c->rva[d]);
        if (o < room) o += (size_t)snprintf(out + o, room - o, " (%u on the game thread, %u in a change frame, %u in the frame after)", c->game, c->change, c->after);
    }
    memset(g_slSample.e, 0, sizeof g_slSample.e); g_slSample.used = g_slSample.missed = g_slSample.sampled = 0;
    ReleaseSRWLockExclusive(&g_slSample.lock);
}

/* A body of the mission world takes the change frame's layer when made inside it on its thread, else none: an index
 * used again by another body forgets the stamp's. */
static void SlMade(uint32_t id, uint32_t inFrame)
{
    const uint32_t index = id & 0xFFFFFFu;
    if (index >= SL_BODIES) return;
    g_sl.layerOf[index] = (uint8_t)inFrame;
    InterlockedIncrement(&g_sl.created);
    if (inFrame) { InterlockedIncrement(&g_sl.marked); g_sl.any = 1; }
}

static void* SlCreate(void* writer, uint32_t* id, void* cinfo, void* r9)
{
    void* r = ((SlCreateFn)g_sl.createOrig)(writer, id, cinfo, r9);
    uintptr_t w = 0, bp = 0;
    if (id == NULL || g_sl.exe == 0 || !SlWorld(g_sl.exe, &w, &bp) || (uintptr_t)writer != w + 0x18) return r;
    const int frame = g_sl.thread != 0 && g_sl.thread == (LONG)GetCurrentThreadId();
    if (g_sl.on) SlMade(*id, frame ? (uint32_t)g_sl.current : 0);
    else InterlockedIncrement(&g_sl.created);
    SlSample(SlChangeMark());
    return r;
}

/* The change frame's spawns into `layer` (6..9): bodies made on this thread from here to SlFrameEnd take it. */
static void SlFrameBegin(uint32_t layer)
{
    if (!g_sl.on) return;
    InterlockedExchange(&g_sl.current, (LONG)layer);
    InterlockedExchange(&g_sl.thread, (LONG)GetCurrentThreadId());
}

/* The spawns queued their units on the async spawn ring: they are made inside the frame, as DestroyUnit makes them
 * before it destroys (0x338974..0x3389A8: the ring not empty, or either destroy list not, then 0x12D600(world)). */
static int SlFlushDue(uint64_t world)
{
    uint32_t a = 0, b = 0;
    if (world == 0) return 0;
    const int ring = MoveSpawnRingEmpty(world);
    return ring == 0 || (ring == 1 && ((!SafeRead(world + 0x170038, &a, 4) && a != 0) || (!SafeRead(world + 0x10030, &b, 4) && b != 0)));
}

/* The frame's end: its units made (the caller waited for the animation jobs, as before a destroy), then bodies made
 * on this thread take no streamed layer. `world` 0: nothing to make. */
static void SlFrameEnd(uint64_t world)
{
    if (!g_sl.on) return;
    if (SlFlushDue(world)) ((SlFlushFn)(g_sl.exe + SL_FLUSH_RVA))(world);
    InterlockedExchange(&g_sl.thread, 0);
}

/* The change frame's one layer (leaves and queued removals of layer 4, then 6..9), so its step rebuilds one: the one
 * with the most queued removals, ties to the lower, and its spawns go there too. Layer 4 takes no spawns, nor does a
 * layer over the smallest streamed one by half a streamed layer's mean: with spawns due, frames then take turns
 * (`turn`) between its removals and the smallest layer's spawns, so the layers stay near a quarter each. `spawn` 0:
 * no spawns this frame. */
static void SlPick(const uint32_t leaves[SL_LAYERS], const uint32_t queued[SL_LAYERS], int due, int* turn, int* drain, uint32_t* spawn)
{
    uint32_t small = 1, most = 0, total = 0;
    for (uint32_t k = 1; k < SL_LAYERS; k++) { total += leaves[k]; if (leaves[k] < leaves[small]) small = k; }
    for (uint32_t k = 1; k < SL_LAYERS; k++) if (queued[k] > queued[most]) most = k;
    uint32_t to = queued[most] > 0 ? most : small;
    int spawns = to != 0 && leaves[to] <= leaves[small] + total / (2u * SL_COUNT);
    if (!spawns && due && (*turn ^= 1)) { to = small; spawns = 1; }
    *drain = to == 0 ? 4 : (int)(SL_FIRST + to - 1);
    *spawn = spawns ? SL_FIRST + to - 1 : 0;
}

/* The layer count and info, before the game's set-up: the main world is made once there (0x4EE160 -> 0xAB0EE0 ->
 * 0x78D330 -> 0xD05870) and its broad phase reads the count once, so hooks put in later never reach it. The info is
 * hooked first, so no broad phase gets layers 6..9 without it. NULL when in; a refused count leaves the info passing
 * through. */
#define SL_OFF_DEFAULT "off by default (HELLFORGE_XL_STREAM_LAYERS=1 turns them on): a rebuild of 100-230k leaves costs nothing measurable (run 1 against run 2a)"
#ifdef CO_TEST
static int g_slAskTest; /* a test's switch: 1 on, else off (a test build reads no environment switch, co_shim.h) */
#endif
/* A research switch: a package build reads none (co_shim.h), so there the layers are always off. */
static int SlAsked(void)
{
    char t[4] = "";
#ifdef CO_TEST
    if (g_slAskTest) return g_slAskTest > 0;
#endif
    return GetEnvironmentVariableA("HELLFORGE_XL_STREAM_LAYERS", t, sizeof t) > 0 && t[0] == '1';
}
static const char* SlEarly(uintptr_t exe)
{
    char t[4] = "";
    if (g_sl.layers) return NULL;
    /* off unless asked: no hook goes in before the set-up, so the race with it (adv REPORT-r9 §2) is not run */
    if (!(g_sl.asked = SlAsked())) return SL_OFF_DEFAULT;
    if (exe == 0) return "the exe is not loaded";
    if (GetEnvironmentVariableA("HELLFORGE_LW_BP", t, sizeof t) > 0 && t[0] == '1') return "the living world's layer research (HELLFORGE_LW_BP) hooks the same layer rule";
    if (!ApiCodeMatches(exe + SL_COUNT_RVA, kSlCountPin) || !ApiCodeMatches(exe + SL_INFO_RVA, kSlInfoPin) || !ApiCodeMatches(exe + SL_COUNT_SITE, kSlCountSitePin))
        return "the broad phase's layer config is not the reviewed code";
    g_sl.exe = exe;
    /* Set, fenced, before the count's hook is written: the set-up's thread can only reach SlCount after that write, so
     * it reads 1. Set after it, the set-up's store at 0xAB18B7 and this one could each miss the other (a store-buffer
     * litmus): LayersEarly would say "in time" for a world made with 6. */
    InterlockedExchange((volatile LONG*)&g_sl.layers, 1);
    if (LoaderHook("extra large maps: streamed layers' info", exe + SL_INFO_RVA, (void*)SlInfo, &g_sl.infoOrig) || g_sl.infoOrig == NULL ||
        LoaderHook("extra large maps: streamed layers' count", exe + SL_COUNT_RVA, (void*)SlCount, &g_sl.countOrig) || g_sl.countOrig == NULL)
    {
        InterlockedExchange((volatile LONG*)&g_sl.layers, 0);
        return "a layer config hook was refused";
    }
    return NULL;
}

/* The rule and body hooks, both or none, once the streamer goes in; 0 when on. Only after SlEarly: without its layers
 * the main world's broad phase has none to send bodies to. Hooks the loader took stay in and pass through while off. */
static const char* SlInstall(uintptr_t exe)
{
    if (g_sl.on) return NULL;
    if (!g_sl.asked && !SlAsked()) return SL_OFF_DEFAULT;
    if (!g_sl.layers) return "the layer count was not hooked before the game's set-up made the main world (its broad phase keeps 6 layers)";
    if (exe == 0 || exe != g_sl.exe) return "the exe is not the one the layer count was hooked in";
    /* the main world exists by now (the streamer goes in from a game update): its own count says whether it was in time */
    if ((g_sl.count = SlLayerCount()) < 0) return "no main world to read (its broad phase's layer count is unknown)"; /* not a lost race */
    if (g_sl.count < (int)(SL_FIRST + SL_COUNT))
    {
        snprintf(g_sl.why, sizeof g_sl.why, "the main world's broad phase has %d layers (made before the layer count was hooked)", g_sl.count);
        return g_sl.why;
    }
    if (!ApiCodeMatches(exe + SL_RULE_RVA, kSlRulePin) || !ApiCodeMatches(exe + SL_CREATE_RVA, kSlCreatePin) || !ApiCodeMatches(exe + SL_FLUSH_RVA, kSlFlushPin))
        return "the broad phase's layer rule, the body maker or the spawn flush is not the reviewed code";
    /* the body maker may be in already, as the sampler (SlSamplerInstall): the same detour, not hooked twice */
    if ((g_sl.ruleOrig == NULL && (LoaderHook("extra large maps: streamed layers' rule", exe + SL_RULE_RVA, (void*)SlRule, &g_sl.ruleOrig) || g_sl.ruleOrig == NULL)) ||
        (g_sl.createOrig == NULL && (LoaderHook("extra large maps: streamed layers' bodies", exe + SL_CREATE_RVA, (void*)SlCreate, &g_sl.createOrig) || g_sl.createOrig == NULL)))
        return "a hook was refused";
    g_sl.on = 1;
    return NULL;
}

#if defined(CO_RESEARCH) || defined(CO_TEST)
/* With the layers off, the body maker alone, so the note can say who makes the mission world's bodies (SlSample): a
 * pass-through on 0xD09080 (hooked as when the layers are on, runs 2a and 2c). NULL when in. A research instrument: a
 * package build has no such hook. */
static const char* SlSamplerInstall(uintptr_t exe)
{
    if (g_sl.createOrig != NULL) return NULL;
    if (exe == 0) return "the exe is not loaded";
    if (!ApiCodeMatches(exe + SL_CREATE_RVA, kSlCreatePin)) return "the body maker is not the reviewed code";
    g_sl.exe = exe;
    if (LoaderHook("extra large maps: the bodies' makers", exe + SL_CREATE_RVA, (void*)SlCreate, &g_sl.createOrig) || g_sl.createOrig == NULL)
    { g_sl.createOrig = NULL; return "the hook was refused"; }
    return NULL;
}
#endif

/* The static layers' leaves (4, then 6..9) of the mission world's broad phase; 0 when the hooks are off, or it does not
 * read or holds no streamed layers. */
static int SlLeaves(uint32_t leaves[SL_LAYERS])
{
    uintptr_t w = 0, bp = 0, layers = 0;
    int32_t count = 0;
    if (!g_sl.on || !SlWorld(ApiModuleBase("helldivers2.exe", NULL), &w, &bp) || SafeRead(bp + 0x18, &count, 4) || count < (int32_t)(SL_FIRST + SL_COUNT) || count > 32 ||
        SafeRead(bp + 0x10, &layers, 8) || layers == 0) return 0;
    for (uint32_t k = 0; k <= SL_COUNT; k++)
    {
        uintptr_t l = 0;
        if (SafeRead(layers + (k == 0 ? 4u : SL_FIRST + k - 1) * 8u, &l, 8) || l == 0 || SafeRead(l + 0x50, &leaves[k], 4)) return 0;
    }
    return 1;
}

/* The main world's broad phase's layer count; -1 when there is none to read. */
static int SlLayerCount(void)
{
    uintptr_t w = 0, bp = 0;
    int32_t count = 0;
    return SlWorld(ApiModuleBase("helldivers2.exe", NULL), &w, &bp) && !SafeRead(bp + 0x18, &count, 4) ? (int)count : -1;
}

static void SlNote(void)
{
    uint32_t leaves[SL_LAYERS] = {0};
    char why[160] = "", made[800];
    const int read = SlLeaves(leaves), count = read ? (int)(SL_FIRST + SL_COUNT) : SlLayerCount();
    if (!read && count < 0) snprintf(why, sizeof why, " (not read: no main world)");
    else if (!read && count < (int)(SL_FIRST + SL_COUNT)) snprintf(why, sizeof why, " (not read: the main world's broad phase has %d layers, made before the layer count was hooked)", count);
    else if (!read) snprintf(why, sizeof why, " (not read: the main world's broad phase has %d layers, but they do not read)", count);
    Note("difficulty: extra large maps: streamed layers %s: static leaves layer 4 %u, layers 6..9 %u %u %u %u%s; in 10 s %ld bodies made, %ld of them in a change frame, %ld sent to 6..9 by the rule",
        g_sl.on ? "on" : "off", leaves[0], leaves[1], leaves[2], leaves[3], leaves[4], why,
        (long)InterlockedExchange(&g_sl.created, 0), (long)InterlockedExchange(&g_sl.marked, 0), (long)InterlockedExchange(&g_sl.routed, 0));
    if (g_sl.createOrig == NULL) return;
    SlSampleText(made, sizeof made);
    Note("difficulty: extra large maps: the mission world's bodies' makers in 10 s: %s", made);
}
