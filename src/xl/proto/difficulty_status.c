/* The status-effect volumes' unit registry on larger maps. Included by hellforge_modloader.c after difficulty_maps.c.
 *
 * M = [game + 0x347CF20] (made once at boot, 0x13F2CF6; a sub-object of the gameplay world) maps every flagged unit of
 * every loaded level to its status-effect setting: at mission start (game state 4's enter, 0xAC5D40 -> 0xACF710) each
 * unit is appended to an inline list (M+0x129098, 4096 x {u32 id, u32 setting}, count M+0x131098, no check) and put in
 * an open-addressing hash (ptr M+0x129080 -> inline M+0x121080, cap M+0x129088 = 0x1000, power of two, every reader
 * builds the mask from it at run time); a full hash leaves r8 0 and the insert writes through it (0xACF961 -> 0xACF966).
 * The hit path reads it (0x69C05A; a unit not in it has no status effect on hit) and the destruction callback removes
 * (0x13CC4D0: the last list entry into the hole, 0x13CC56F). An operation's three cities passed 4096 units and the
 * game ended 50 s into the mission (s74h). Scratch m347/m347.md (subagent; sites and bytes re-checked by Claude).
 *
 * Larger maps: the hash gets CO_STATUS_HASH places and the list CO_STATUS_LIST places in the mod's memory, through
 * stubs at every use of the game's list (../co_status.c, which co_complex_start installs: all or none). The append
 * checks the list's room (0xACF8F0; full: the unit is skipped, 0xACFA48, the loop's next item, with its destruction
 * callback, which is what removes list entries) and the insert a null slot (0xACF966: the entry taken back, skipped).
 * CO_STATUS_LIST stays under the destruction-callback map in the same loop ([0x346D528]+0x52AF88, 0x8000 slots, its
 * own unchecked insert). Skips are counted and noted. */

/* The game thread's tick: the hash once the registry exists with the game's own table, and a note when units were skipped. */
static void StatusWatch(uintptr_t game)
{
    static uint32_t noted;
    uint32_t skips = co_status_skips();
    if (skips != noted) { noted = skips; Note("difficulty: the status-effect volumes' unit registry skipped %u units (full)", skips); }
    if (co_status_hash(game))
        Note("difficulty: the status-effect volumes' unit registry holds %u units (list) in a hash of %u (was 4096)", CO_STATUS_LIST, CO_STATUS_HASH);
}

/* Research (2026-11-04, s74h/s74i): on a city map FinalizeVoronoi's structured weld (0xFBBB20, from 0xF6810F) fails and
 * the level restarts. Which of its three failing calls fails, and with what (the first 24 notes):
 *   0xFBBB20(rcx ctx, r8 edges {count, cap, ptr: route edges 0x48}, r9 nodes {count, cap, ptr: route nodes 0x98});
 *     the world is [[ctx+8]+0x2C] (896 m in a terrain tile, the level's otherwise).
 *   0xFB9D90(rcx weld, rdx edges, r8d count): the edges' vertices into the 24 x 24 buckets.
 *   0xFB9B10(rcx weld, rdx vertex: x, y at +0, r8 out): one merged vertex; bucket = min(y/W*23, 23)*24 + min(x/W*23, 23)
 *     over W = [weld+0x1238] (float), each bucket's block {count, vertices}.
 *   0xFBB180(rcx weld, r8 edges, r9 nodes): each edge's endpoints through 0xFBAFF0 into the nodes (count reset to 0). */
typedef uint64_t (*WeldFn4)(uint64_t, uint64_t, uint64_t, uint64_t);
static WeldFn4 g_weldFinal, g_weldCells, g_weldVertex, g_weldEdges;
static volatile LONG g_weldNotes;
static float WeldSide(uint64_t weld) { float w = 0; SafeRead(weld + 0x1238, &w, 4); return w; }
static uint64_t WeldFinal(uint64_t ctx, uint64_t b, uint64_t edges, uint64_t nodes)
{
    uint32_t e[2] = {0, 0}, n[2] = {0, 0}, w = 0; uint64_t s = 0;
    SafeRead(edges, e, 8); SafeRead(nodes, n, 8);
    if (!SafeRead(ctx + 8, &s, 8) && s != 0) SafeRead(s + 0x2C, &w, 4);
    uint64_t r = g_weldFinal(ctx, b, edges, nodes);
    if ((uint8_t)r != 0) return r;
    TileGuard* guard = TileGuardSelf();
    int tile = guard->thread != 0 && guard->thread == GetCurrentThreadId();
    if (InterlockedIncrement(&g_weldNotes) <= 24)
        TerrainBuildNote("difficulty: weld research: FinalizeVoronoi's weld failed (%s, world %u m): edges %u (cap %u), nodes %u before (cap %u)",
            tile ? "a terrain tile: abandoned, no level restart" : "the level", w, e[0], e[1], n[0], n[1]);
    /* a tile's failure: done for 0xF68114 (it writes the counts back into the tile's own copy of the block), the tile's
     * runner abandons the tile (its guard's weldFailed) */
    if (tile) { InterlockedIncrement(&guard->weldFailed); return (r & ~0xFFull) | 1u; }
    return r;
}
static uint64_t WeldCells(uint64_t weld, uint64_t list, uint64_t count, uint64_t d)
{
    uint64_t r = g_weldCells(weld, list, count, d);
    if ((uint8_t)r == 0 && InterlockedIncrement(&g_weldNotes) <= 24)
    {
        uint32_t used = 0; SafeRead(weld + DIF_WELD_COUNT, &used, 4);
        TerrainBuildNote("difficulty: weld research: the edges' vertices did not fit (0xFB9D90; side %.0f m, %u edges, %u of 576 bucket blocks)", (double)WeldSide(weld), (uint32_t)count, used);
    }
    return r;
}
static uint64_t WeldVertex(uint64_t weld, uint64_t vertex, uint64_t out, uint64_t d)
{
    uint64_t r = g_weldVertex(weld, vertex, out, d);
    if ((uint8_t)r == 0 && InterlockedIncrement(&g_weldNotes) <= 24)
    {
        float p[2] = {0, 0}, w = WeldSide(weld); SafeRead(vertex, p, 8);
        uint32_t bx = 0, by = 0, held = 0; uint64_t block = 0;
        if (w > 0.0f) { int64_t x = (int64_t)(p[0] / w * 23.0f), y = (int64_t)(p[1] / w * 23.0f); bx = (uint32_t)x > 23u ? 23u : (uint32_t)x; by = (uint32_t)y > 23u ? 23u : (uint32_t)y; }
        if (!SafeRead(weld + 8ull * (by * 24u + bx), &block, 8) && block != 0) SafeRead(block, &held, 4);
        TerrainBuildNote("difficulty: weld research: a vertex did not fit (0xFB9B10; side %.0f m, at %.1f, %.1f: bucket %u, %u holds %u)", (double)w, (double)p[0], (double)p[1], bx, by, held);
    }
    return r;
}
static uint64_t WeldEdges(uint64_t weld, uint64_t b, uint64_t edges, uint64_t nodes)
{
    uint64_t r = g_weldEdges(weld, b, edges, nodes);
    if ((uint8_t)r == 0 && InterlockedIncrement(&g_weldNotes) <= 24)
    {
        uint32_t e[2] = {0, 0}, n[2] = {0, 0}, used = 0;
        SafeRead(edges, e, 8); SafeRead(nodes, n, 8); SafeRead(weld + DIF_WELD_COUNT, &used, 4);
        TerrainBuildNote("difficulty: weld research: the edges' endpoints failed (0xFBB180; side %.0f m): edges %u, nodes %u (cap %u), %u of 576 bucket blocks", (double)WeldSide(weld), e[0], n[0], n[1], used);
    }
    return r;
}
static void WeldResearchInstall(uintptr_t game)
{
    static const struct { uint32_t rva; const char* pin; const char* what; void* detour; WeldFn4* original; } k[4] = {
        {0xFBBB20u, "40 55 53 56 57 41 54 41 55 48", "weld research (FinalizeVoronoi's weld)", (void*)WeldFinal, &g_weldFinal},
        {0xFB9D90u, "48 89 5C 24 18 48 89 6C 24 20", "weld research (the edges' vertices)", (void*)WeldCells, &g_weldCells},
        {0xFB9B10u, "48 89 5C 24 10 48 89 74 24 18", "weld research (one vertex)", (void*)WeldVertex, &g_weldVertex},
        {0xFBB180u, "48 89 5C 24 08 48 89 6C 24 10", "weld research (the endpoints)", (void*)WeldEdges, &g_weldEdges}};
    if (!LargerMaps()) return;
    for (int i = 0; i < 4; i++)
        if (ApiCodeMatches(game + k[i].rva, k[i].pin)) LoaderHook(k[i].what, game + k[i].rva, k[i].detour, (void**)k[i].original);
        else Note("difficulty: %s: not the reviewed code", k[i].what);
}
