/* On with the living world (HELLFORGE_LW_TREES=0 turns it off; KB xl-destruction-record-map-full): SpeedTree cuts each
 * view's rectangle of 64 m cells to limits (0x954D70, from the forest's extents) only while both axes have a range.
 * When a destroyed tree empties a cell the extents are rebuilt from the cell map without reading the second axis
 * (0x215C68 and 0x216B4F leave it 0..0), so the cut ends and each view walks its whole far rectangle: 120,000-325,000
 * cells against ~20,000 on a 16 km map, a slow frame a second. Trees are only ever added inside the extents a level
 * has had, so in a mission the limits here never shrink. On the ship and in generation nothing is destroyed: the
 * game's limits stand, or the last level's would widen the ship's and the next level's grid. */
#define LW_TREE_LIMITS 0x954D70u   /* (owner, low, high): +0x1B4/+0x1B8 first axis, +0x1BC/+0x1C0 second */
typedef void (*LwTreeFn)(uintptr_t owner, const float* low, const float* high);
static struct { void* orig; volatile LONG calls, widened, level, have[2]; volatile LONG low[2], high[2]; } g_lwTree;

/* A new level's forest starts its own extents. */
static void LwTreeLevel(void)
{
    InterlockedExchange(&g_lwTree.have[0], 0);
    InterlockedExchange(&g_lwTree.have[1], 0);
}

/* Mission state 4 ([[game + 0x3326340] + 0xAC21C], as WorldStreamInMission): the object lives as long as the game. */
static int LwTreeInMission(void)
{
    uintptr_t gameObj = *(volatile uintptr_t*)(g_lw.game + 0x3326340u);
    return gameObj != 0 && *(volatile uint32_t*)(gameObj + 0xAC21Cu) == 4;
}

/* One axis: take the game's range into the level's, then give the level's back if the game's is narrower or empty. */
static void LwTreeAxis(int32_t* pair, int axis)
{
    if (pair[0] < pair[1])
    {
        int had = g_lwTree.have[axis];
        if (!had || pair[0] < g_lwTree.low[axis]) g_lwTree.low[axis] = pair[0];
        if (!had || pair[1] > g_lwTree.high[axis]) g_lwTree.high[axis] = pair[1];
        g_lwTree.have[axis] = 1;
    }
    if (!g_lwTree.have[axis] || (pair[0] == g_lwTree.low[axis] && pair[1] == g_lwTree.high[axis])) return;
    pair[0] = g_lwTree.low[axis]; pair[1] = g_lwTree.high[axis];
    InterlockedIncrement(&g_lwTree.widened);
}

/* The cull jobs call this side by side with the same extents, so the unsynchronised widest range settles at once.
 * The bake views pass no limits (-FLT_MAX..FLT_MAX) and stay that way. */
static void LwTreeLimits(uintptr_t owner, const float* low, const float* high)
{
    ((LwTreeFn)g_lwTree.orig)(owner, low, high);
    if (low[0] < -1e30f || high[0] > 1e30f) return;
    LONG level = (LONG)g_world.levels;
    if (InterlockedExchange(&g_lwTree.level, level) != level) LwTreeLevel();
    if (!LwTreeInMission() || !LwLive()) { LwTreeLevel(); return; } /* the ship's views: the next mission starts from nothing */
    InterlockedIncrement(&g_lwTree.calls);
    LwTreeAxis((int32_t*)(owner + 0x1B4u), 0);
    LwTreeAxis((int32_t*)(owner + 0x1BCu), 1);
}

/* With the 30 s notes, when the game's limits were narrower than the level's. */
static void LwTreeNote(void)
{
    LONG widened = InterlockedExchange(&g_lwTree.widened, 0), calls = InterlockedExchange(&g_lwTree.calls, 0);
    if (widened) Note("living world: tree limits: %ld of %ld axis ranges were narrower than the level's and widened to %ld..%ld and %ld..%ld",
        widened, 2 * calls, g_lwTree.low[0], g_lwTree.high[0], g_lwTree.low[1], g_lwTree.high[1]);
}

/* The pins: the setter, the two rebuilds that skip the second axis, and the cut that needs both ranges (0x954C2D). */
static void LwInstallTrees(void)
{
    static int done;
    char t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_TREES", t, sizeof t) > 0 && t[0] == '0') return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!ApiCodeMatches(exe + LW_TREE_LIMITS, "48 89 5C 24 08 57 48 83 EC 50 0F 29 74 24 40 48 8B D9") ||
        !ApiCodeMatches(exe + 0x215C68u, "41 8B C6 41 0F 4F D3 85 DB 44 8B DA 41 8B D6 0F 48 C3 85 C0 8B D8 0F 4F D0") ||
        !ApiCodeMatches(exe + 0x216B4Fu, "41 8B C6 41 0F 4F D3 85 FF 44 8B DA 41 8B 12 0F 48 C7 85 C0 8B F8 0F 4F F0") ||
        !ApiCodeMatches(exe + 0x954C2Du, "41 8B F0 45 3B EC 0F 8D 80 00 00 00"))
    { Note("living world: the tree cell limits are not the reviewed code; not changed"); return; }
    if (LoaderHook("living world: tree cell limits", exe + LW_TREE_LIMITS, (void*)LwTreeLimits, &g_lwTree.orig) || g_lwTree.orig == NULL)
    { g_lwTree.orig = NULL; Note("living world: the tree cell limits could not be hooked"); return; }
    Note("living world: tree cell limits never shrink within a mission");
}
