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
static const char kSlCountPin[] = "B8 06 00 00 00 C3";
static const char kSlInfoPin[] = "0F B6 C2 83 F8 05 0F 87 8A 00 00 00 48 8D 15";
static const char kSlRulePin[] = "4D 63 D8 4C 8B D2 45 85 C0 7E 6E 48 8B 44 24 28 45 33 C0 41 8B 0A 81 E1";
static const char kSlCreatePin[] = "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 56 41 57 48 8D AC 24 C8 FE FF FF";
static const char kSlFlushPin[] = "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 98 F6 FF FF";

typedef char SlLayersMatch[SL_LAYERS == 1u + SL_COUNT ? 1 : -1]; /* difficulty_stream.c's count: layer 4, then these */
static int MoveSpawnRingEmpty(uint64_t world); /* difficulty_world_move.c */
typedef int (*SlCountFn)(void* cfg);
typedef void (*SlInfoFn)(void* cfg, uint8_t layer, uint8_t* out);
typedef void (*SlRuleFn)(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out);
typedef void* (*SlCreateFn)(void* writer, uint32_t* id, void* cinfo, void* r9);
typedef void (*SlFlushFn)(uint64_t world);

static struct
{
    int on;                     /* all four hooks are in: new broad phases have layers 6..9 */
    uintptr_t exe;
    void *countOrig, *infoOrig, *ruleOrig, *createOrig;
    volatile LONG thread;       /* the change frame's thread while it spawns and flushes (0: none) */
    volatile LONG current;      /* that frame's layer */
    uintptr_t okBp;             /* the mission world's broad phase, checked to hold layers 6..9 as layer 4 */
    volatile LONG marked, routed, created; /* since the note: bodies remembered, sent to 6..9 by the rule, made in all */
    volatile LONG any;          /* a body was ever made in a change frame: until then the rule has nothing to send */
    uint8_t layerOf[SL_BODIES]; /* a body index's streamed layer, 0: none */
} g_sl;

/* The mission world (W = [exe+0x27BA9F0]) and its broad phase ([W+0x4C0], stored by the world's constructor at
 * 0xD062BB; vtable +0x38 the update 0xD40550). 0 when either does not read. */
static int SlWorld(uintptr_t exe, uintptr_t* w, uintptr_t* bp)
{
    uintptr_t vt = 0, fn = 0;
    *w = *bp = 0;
    return exe != 0 && !SafeRead(exe + 0x27BA9F0u, w, 8) && *w != 0 && !SafeRead(*w + 0x4C0, bp, 8) && *bp != 0 && !SafeRead(*bp, &vt, 8) &&
        !SafeRead(vt + 0x38, &fn, 8) && fn == exe + 0xD40550u;
}

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

static int SlCount(void* cfg)
{
    const int n = ((SlCountFn)g_sl.countOrig)(cfg);
    return g_sl.on && n == 6 ? (int)(SL_FIRST + SL_COUNT) : n;
}

static void SlInfo(void* cfg, uint8_t layer, uint8_t* out)
{
    if (g_sl.on && layer >= SL_FIRST && layer < SL_FIRST + SL_COUNT)
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
    if (g_sl.on && id != NULL && SlWorld(g_sl.exe, &w, &bp) && (uintptr_t)writer == w + 0x18)
        SlMade(*id, g_sl.thread != 0 && g_sl.thread == (LONG)GetCurrentThreadId() ? (uint32_t)g_sl.current : 0);
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

/* The change frame's layers (leaves and queued removals of layer 4, then 6..9): the drain takes the layer with the most
 * queued, ties to the lower; the spawns go there too unless it holds more than the smallest streamed layer by half a
 * streamed layer's mean, then to the smallest, so the layers stay near a quarter of the streamed leaves each. */
static void SlPick(const uint32_t leaves[SL_LAYERS], const uint32_t queued[SL_LAYERS], int* drain, uint32_t* spawn)
{
    uint32_t small = 1, most = 0, total = 0;
    for (uint32_t k = 1; k < SL_LAYERS; k++) { total += leaves[k]; if (leaves[k] < leaves[small]) small = k; }
    for (uint32_t k = 1; k < SL_LAYERS; k++) if (queued[k] > queued[most]) most = k;
    const uint32_t to = queued[most] > 0 ? most : small;
    *drain = to == 0 ? 4 : (int)(SL_FIRST + to - 1);
    *spawn = SL_FIRST + (to != 0 && leaves[to] <= leaves[small] + total / (2u * SL_COUNT) ? to : small) - 1;
}

/* The four hooks, all or none; 0 when on. The count is hooked last, so no broad phase gets layers 6..9 without their
 * info. Hooks the loader took stay in and pass through while off. */
static const char* SlInstall(uintptr_t exe)
{
    char t[4] = "";
    if (g_sl.on) return NULL;
    if (exe == 0) return "the exe is not loaded";
    if (GetEnvironmentVariableA("HELLFORGE_LW_BP", t, sizeof t) > 0 && t[0] == '1') return "the living world's layer research (HELLFORGE_LW_BP) hooks the same layer rule";
    if (!ApiCodeMatches(exe + SL_COUNT_RVA, kSlCountPin) || !ApiCodeMatches(exe + SL_INFO_RVA, kSlInfoPin) || !ApiCodeMatches(exe + SL_RULE_RVA, kSlRulePin) ||
        !ApiCodeMatches(exe + SL_CREATE_RVA, kSlCreatePin) || !ApiCodeMatches(exe + SL_FLUSH_RVA, kSlFlushPin))
        return "the broad phase's layer config, the body maker or the spawn flush is not the reviewed code";
    g_sl.exe = exe;
    if (LoaderHook("extra large maps: streamed layers' info", exe + SL_INFO_RVA, (void*)SlInfo, &g_sl.infoOrig) || g_sl.infoOrig == NULL ||
        LoaderHook("extra large maps: streamed layers' rule", exe + SL_RULE_RVA, (void*)SlRule, &g_sl.ruleOrig) || g_sl.ruleOrig == NULL ||
        LoaderHook("extra large maps: streamed layers' bodies", exe + SL_CREATE_RVA, (void*)SlCreate, &g_sl.createOrig) || g_sl.createOrig == NULL ||
        LoaderHook("extra large maps: streamed layers' count", exe + SL_COUNT_RVA, (void*)SlCount, &g_sl.countOrig) || g_sl.countOrig == NULL)
        return "a hook was refused";
    g_sl.on = 1;
    return NULL;
}

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

static void SlNote(void)
{
    uint32_t leaves[SL_LAYERS] = {0};
    const int read = SlLeaves(leaves);
    Note("difficulty: extra large maps: streamed layers %s: static leaves layer 4 %u, layers 6..9 %u %u %u %u%s; in 10 s %ld bodies made, %ld of them in a change frame, %ld sent to 6..9 by the rule",
        g_sl.on ? "on" : "off", leaves[0], leaves[1], leaves[2], leaves[3], leaves[4], read ? "" : " (not read: a broad phase made before the hooks, or none)",
        (long)InterlockedExchange(&g_sl.created, 0), (long)InterlockedExchange(&g_sl.marked, 0), (long)InterlockedExchange(&g_sl.routed, 0));
}
