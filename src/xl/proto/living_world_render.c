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
static struct { void* endOrig; uintptr_t rehash, after; volatile LONG shrunk, told; } g_lwRender;

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

static void LwRenderEnd(uintptr_t state)
{
    ((LwRenderEndFn)g_lwRender.endOrig)(state);
    if ((uintptr_t)__builtin_return_address(0) != g_lwRender.after) return;   /* a worker's call, or another hook's */
    LwRenderShrink(state + 0x88);
    LwRenderShrink(state + 0xB0);
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
    g_lwRender.after = exe + LW_RT_AFTER;
    if (LoaderHook("living world: render version table", exe + LW_RT_END, (void*)LwRenderEnd, &g_lwRender.endOrig) || g_lwRender.endOrig == NULL)
    { g_lwRender.endOrig = NULL; Note("living world: the render frame end could not be hooked"); return; }
    Note("living world: the render version tables are kept short (%u entries and more, under an eighth used, are rehashed)", LW_RT_LONG);
}
