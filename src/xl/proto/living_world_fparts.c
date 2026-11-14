/* What a slow frame's line says beside the mod's timed parts (xl/co_framecost.h): the game thread's waits for the Havok
 * AI step (0x7D7930: the join at the frame's start through plugin slot +0x60, called at 0x6009F, and the AI update's
 * own) and the engine's world update (0x1B0B10). A raised AI budget (LwNavHoldRaiseUs) lengthens that wait, and a frame
 * no timed part explains is told apart by these. Both hooks only time the game's call. */
typedef uintptr_t (*LwFpJoinFn)(uintptr_t a);
typedef uintptr_t (*LwFpWorldFn)(uintptr_t w, float a, float b);
static struct { void *join, *world; int64_t joinTicks; DWORD taker; int quiet; } g_lwFp;

static uintptr_t LwFpJoin(uintptr_t a)
{
    /* a nav drain's own join is that drain's time (nav swaps), not an AI join of the frame */
    if (g_lwFp.quiet && GetCurrentThreadId() == g_lwFp.taker) return ((LwFpJoinFn)g_lwFp.join)(a);
    int64_t t = co_framecost_now();
    uintptr_t out = ((LwFpJoinFn)g_lwFp.join)(a);
    co_framecost_extra(CO_FX_JOIN, t);
    if (GetCurrentThreadId() == g_lwFp.taker) g_lwFp.joinTicks += co_framecost_now() - t;
    return out;
}
static uintptr_t LwFpWorld(uintptr_t w, float a, float b)
{
    co_framecost_world(1);
    uintptr_t out = ((LwFpWorldFn)g_lwFp.world)(w, a, b);
    co_framecost_world(0);
    return out;
}
/* The joins' time on the calling thread since its last call, in ms: the budget lever asks once a frame. */
static double LwFpJoinTake(void)
{
    int64_t ticks = g_lwFp.joinTicks;
    g_lwFp.joinTicks = 0; g_lwFp.taker = GetCurrentThreadId();
    return LwNavMs(0, ticks);
}
static void LwFpJoinQuiet(int on) { g_lwFp.quiet = on; }
static void LwInstallFrameParts(void)
{
    static int done;
    if (done++) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    int join = ApiCodeMatches(exe + 0x7D7930u, "48 89 5C 24 10 48 89 6C 24 18") && ApiCodeMatches(exe + 0x6009Fu, "FF 51 60") &&
        !LoaderHook("frame times: the AI step's joins", exe + 0x7D7930u, (void*)LwFpJoin, &g_lwFp.join) && g_lwFp.join;
    int world = ApiCodeMatches(exe + 0x1B0B10u, "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55") &&
        !LoaderHook("frame times: the engine's world update", exe + 0x1B0B10u, (void*)LwFpWorld, &g_lwFp.world) && g_lwFp.world;
    if (!join || !world)
        Note("living world: frame times: %s not timed (not the code the timer expects); a slow frame's line shows 0 for it",
            !join && !world ? "the AI joins and the world update are" : !join ? "the AI joins are" : "the world update is");
}
