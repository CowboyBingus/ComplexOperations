/* On with the living world (HELLFORGE_LW_RENDER_TABLE=0 turns it off; KB xl-living-world-frame-budget): the render
 * device keeps a table of resource id to current version (device +0x13C8) and one of the ids changed this frame
 * (+0x13F0). An insert (0x6F4220) grows a full table through the rehash 0x6F60F0 and nothing shrinks it, so a level's
 * loading leaves 56,114 entries on an extra large map with about 120 in use. Each frame the dispatch (0x6CC130) copies
 * the whole table into every worker context and empties each copy again, about 50 times: 45% of the render thread,
 * which sets the frame's length. The batch end 0x6F0240 runs on the render thread after each dispatch (call at
 * 0x6CEDA8, all of the batch's jobs waited for) and on worker threads from 0x6CEE75; only after the first, where
 * the engine's own inserts may rehash the table, is an oversized table rehashed small with the engine's routine.
 * An insert grows it again when it has to. */
#define LW_RT_END 0x6F0240u
#define LW_RT_REHASH 0x6F60F0u
#define LW_RT_AFTER 0x6CEDADu   /* the return address of the render thread's call, after its dispatch */
#define LW_RT_LONG 4096u   /* shorter tables cost too little to touch; a level in play uses a few hundred entries */

typedef void (*LwRenderEndFn)(uintptr_t state);
typedef void (*LwRenderRehashFn)(uintptr_t map, uint32_t buckets);
static struct { void* endOrig; uintptr_t rehash, after, resize; volatile LONG shrunk, told; uint32_t copiesFreed; uint64_t copyBytesFreed; } g_lwRender;

/* A map: size +0, used +0x18, buckets +0x1C. Under an eighth used leaves room for 4 times the entries before the
 * next growth, so a table near its need is not rehashed back and forth. */
static void LwRenderShrink(uintptr_t map)
{
    if (!LwLive()) return; /* a normal mission's tables are the game's */
    uint32_t size = *(volatile uint32_t*)map, used = *(volatile uint32_t*)(map + 0x18), buckets = *(volatile uint32_t*)(map + 0x1C);
    if (size < LW_RT_LONG || used > buckets / 8) return;
    ((LwRenderRehashFn)g_lwRender.rehash)(map, used * 4 + 19);
    InterlockedIncrement(&g_lwRender.shrunk);
    if (InterlockedExchange(&g_lwRender.told, 1) == 0)
        Note("living world: render version table: %u entries with %u in use, now %u", size, used, *(volatile uint32_t*)map);
}

/* Each pooled job context X (dev+0x700 count, +0x708 array) copies the versions into X+8 for every job, grown and never
 * shrunk (0x6CD17C..0x6CD241): ~1.35 MB in each of ~900 contexts after a load. Past the dispatch's wait (0x6CD5F7) every
 * copy is emptied (0x6CD646: used 0, which the reader 0x6E2230 tests first), so one far over the short table goes
 * (0x6D55E0 to 0); the next job's copy regrows it. In play only: a load's tables grow back and would re-commit them. */
typedef void (*LwRenderResizeFn)(uintptr_t array, uint32_t n);
static int LwRenderInPlay(void)
{
    uintptr_t gameObj = 0, level = g_worldLevelLive; uint32_t state = 0, levelState = 0;
    return g_world.game != 0 && level != 0 && !SafeRead(g_world.game + 0x3326340u, &gameObj, 8) && gameObj != 0 && !SafeRead(gameObj + 0xAC21Cu, &state, 4) &&
        state == 4 && !SafeRead(level + 0x1E4, &levelState, 4) && levelState == 3;
}
static void LwRenderCopiesFree(uintptr_t dev)
{
    if (!LwLive() || g_lwRender.resize == 0 || !LwRenderInPlay()) return;
    uint32_t size = *(volatile uint32_t*)(dev + 0x13C8), count = *(volatile uint32_t*)(dev + 0x700);
    uintptr_t contexts = *(volatile uintptr_t*)(dev + 0x708);
    if (contexts == 0 || count > 0x100000u) return;
    uint32_t freed = 0; uint64_t bytes = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t x = ((volatile uintptr_t*)contexts)[i];
        if (x == 0) continue;
        uint32_t cap = *(volatile uint32_t*)(x + 0xC);
        if (cap <= LW_RT_LONG || cap / 4 <= size || *(volatile uint32_t*)(x + 0x20) != 0) continue;
        ((LwRenderResizeFn)g_lwRender.resize)(x + 8, 0);
        freed++; bytes += (uint64_t)cap * 24;
    }
    if (freed == 0) return;
    g_lwRender.copiesFreed += freed; g_lwRender.copyBytesFreed += bytes;
    Note("living world: render job contexts: %u of %u version copies given back (%.1f MB; table %u entries); %u copies, %.1f MB in all",
        freed, count, (double)bytes / 1048576.0, size, g_lwRender.copiesFreed, (double)g_lwRender.copyBytesFreed / 1048576.0);
}

/* The shrink, then the copies measured against the short table. */
static void LwRenderAfterDispatch(uintptr_t state)
{
    LwRenderShrink(state + 0x88);
    LwRenderShrink(state + 0xB0);
    LwRenderCopiesFree(state - 0x1340);
}

static void LwDelayRender(void); /* living_world_waits.c, test only */
static void LwRenderEnd(uintptr_t state)
{
    ((LwRenderEndFn)g_lwRender.endOrig)(state);
    /* only the dispatch's call (vt+0xF0 0x6CECB0); it runs on the render thread after its drain (0x109540) or in an
     * ordered device job (head 0x528170, removal 0x5280F0), so never two at once; not the uploads' call (0x6CEE75) */
    if ((uintptr_t)__builtin_return_address(0) != g_lwRender.after) return;
#ifdef CO_RESEARCH
    LwDelayRender(); /* the render thread's test delay */
#endif
    if (MemProbeOn()) g_memProbeDevice = state - 0x1340u; /* the memory probe reads the device (0x6CEDA1: lea rcx, [rdi+0x1340]) */
    LwRenderAfterDispatch(state);
}

/* The copies' pool, resize, reader and the dispatch's wait as reviewed: the resize to call, or 0 (the copies stay). */
static uintptr_t LwRenderCopiesPins(uintptr_t exe)
{
    return ApiCodeMatches(exe + 0x6D55E0u, "48 89 5C 24 10 57 48 83 EC 30 48 8B D9 8B FA 3B 51 04 74 68") &&
        ApiCodeMatches(exe + 0x6CD17Cu, "49 8B 9F B0 03 00 00 41 8B B1 C8 13 00 00 3B 73 0C 76 12 8B D6 48") &&
        ApiCodeMatches(exe + 0x6CD637u, "48 8B 46 70 48 8B 88 B0 03 00 00 48 83 C1 08 E8 15 7F 00 00") &&
        ApiCodeMatches(exe + 0x6CC700u, "48 8D 8F F8 06 00 00 E8 D4 44 FF FF 48 8B 8F 08 07 00 00") &&
        ApiCodeMatches(exe + 0x6CC87Bu, "49 8B 83 08 07 00 00 41 0F 10 83 60 07 00 00 48 8B 14 C8") &&
        ApiCodeMatches(exe + 0x6CC8BEu, "48 89 96 B0 03 00 00") && ApiCodeMatches(exe + 0x6D5560u, "48 83 EC 18 44 8B 01 45 33 D2 41 8B C0 44 89 51 18") &&
        ApiCodeMatches(exe + 0x6CD5F7u, "E8 64 9B F5 FF") && ApiCodeMatches(exe + 0x6E2230u, "83 79 20 00 44 8B CA 4C 8B C1") ? exe + 0x6D55E0u : 0;
}

/* The pins: the render thread's call after its dispatch, the batch end reading the changed ids' size (+0xB0) and
 * inserting into the versions (+0x88), the rehash's entry, and the insert's growth calling it with 2n+1 buckets. */
static void LwInstallRender(void)
{
    static int done;
    char t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_RENDER_TABLE", t, sizeof t) > 0 && t[0] == '0') return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!ApiCodeMatches(exe + LW_RT_END, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 30 44 8B B1 B0 00 00 00") ||
        !ApiCodeMatches(exe + 0x6F02A3u, "48 8D 8E 88 00 00 00") ||
        !ApiCodeMatches(exe + 0x6CEDA1u, "48 8D 8F 40 13 00 00 E8 93 14 02 00 48 8B 5C 24 40") ||
        !ApiCodeMatches(exe + LW_RT_REHASH, "40 55 48 8B EC 48 83 EC 50 8B C2 0F 57 C0 48 89 74 24 68 33 F6") ||
        !ApiCodeMatches(exe + 0x6F4260u, "8B 43 1C 48 8B CB 8D 14 45 01 00 00 00 3B D6 0F 42 D6 E8"))
    { Note("living world: the render version table is not the reviewed code; not changed"); return; }
    g_lwRender.rehash = exe + LW_RT_REHASH;
    if ((g_lwRender.resize = LwRenderCopiesPins(exe)) == 0) Note("living world: the render job contexts are not the reviewed code; their version copies stay");
    g_lwRender.after = exe + LW_RT_AFTER;
    if (LoaderHook("living world: render version table", exe + LW_RT_END, (void*)LwRenderEnd, &g_lwRender.endOrig) || g_lwRender.endOrig == NULL)
    { g_lwRender.endOrig = NULL; Note("living world: the render frame end could not be hooked"); return; }
    Note("living world: the render version tables are kept short (%u entries and more, under an eighth used, are rehashed)", LW_RT_LONG);
}
