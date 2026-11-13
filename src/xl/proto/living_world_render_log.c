/* Research build only, logging only (included by living_world_render.c): per dispatch with a version copy over
 * LW_RT_LONG entries, the versions table's size and use as the dispatch leaves it, and the most contexts that held
 * such a copy at once. The table's use tells a burst's own growth from a long table carried into a quiet frame. */
static struct { volatile LONG big, largest, held, live; volatile LONG64 bytes, heldBytes; } g_lwRenderLog;

/* At a long copy's job end, before its free: the pool's contexts with a long copy now, and those not yet emptied.
 * SafeRead, as the dispatch may move the pool's array while it makes contexts (0x6C0BE0). */
static void LwRenderJobLog(uintptr_t x, uint32_t cap)
{
    InterlockedIncrement(&g_lwRenderLog.big); InterlockedAdd64(&g_lwRenderLog.bytes, (LONG64)cap * 24);
    LwMaxLong(&g_lwRenderLog.largest, (LONG)cap);
    uintptr_t pool = 0, contexts = 0;
    uint32_t count = 0, held = 0, live = 0;
    uint64_t bytes = 0;
    if (SafeRead(x, &pool, 8) || pool < 0x1340 || SafeRead(pool - 0x1340 + 0x700, &count, 4) || SafeRead(pool - 0x1340 + 0x708, &contexts, 8) ||
        contexts == 0 || count > 0x100000u) return;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t c = 0;
        uint32_t ccap = 0, used = 0;
        if (SafeRead(contexts + (uintptr_t)i * 8, &c, 8) || c == 0 || SafeRead(c + 0xC, &ccap, 4) || ccap <= LW_RT_LONG) continue;
        held++; bytes += (uint64_t)ccap * 24;
        if (!SafeRead(c + 0x20, &used, 4) && used != 0) live++;
    }
    LONG64 old;
    while ((old = g_lwRenderLog.heldBytes) < (LONG64)bytes)
        if (InterlockedCompareExchange64(&g_lwRenderLog.heldBytes, (LONG64)bytes, old) == old) { g_lwRenderLog.held = (LONG)held; g_lwRenderLog.live = (LONG)live; break; }
}

/* After the dispatch's wait (no job of it runs), before the shrink: one line, then the counts start again. */
static void LwRenderPassLog(uintptr_t state)
{
    if (g_lwRenderLog.big == 0) return;
    const volatile uint32_t* m = (const volatile uint32_t*)(state + 0x88);
    const volatile uint32_t* c = (const volatile uint32_t*)(state + 0xB0);
    Note("living world: render dispatch: table %u entries, %u used, %u buckets; changed ids %u entries, %u used; %u job copies over %u entries "
         "(%.1f MB, largest %u); at most %u contexts held one at once (%.1f MB), %u of them not yet emptied",
        m[0], m[6], m[7], c[0], c[6], (unsigned)g_lwRenderLog.big, LW_RT_LONG, (double)g_lwRenderLog.bytes / 1048576.0, (unsigned)g_lwRenderLog.largest,
        (unsigned)g_lwRenderLog.held, (double)g_lwRenderLog.heldBytes / 1048576.0, (unsigned)g_lwRenderLog.live);
    g_lwRenderLog.big = g_lwRenderLog.largest = g_lwRenderLog.held = g_lwRenderLog.live = 0;
    g_lwRenderLog.bytes = g_lwRenderLog.heldBytes = 0;
}
