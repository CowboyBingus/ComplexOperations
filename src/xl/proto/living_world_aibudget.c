/* AI path time budget (on by default; HELLFORGE_LW_AIBUDGET=0 off; command "aibudget US [RAISE_MS]"): the Havok AI
 * step (exe 0xBD1890) lets its async path threads run for H+0x47C seconds (stock 0.002) while the game thread's join
 * (0x7D7930) spins. 500 us took the join from 1.0-1.5 ms to 0.2-0.3 ms at 200 units (KB xl-living-world-frame-budget,
 * m49). Paths arrive later instead, so the stock budget comes back while requests are older than RAISE_MS. */
#define LW_AIB_LOW_US 500
#define LW_AIB_RAISE_MS 500
static struct
{
    int on, failed, raised, verbose; uintptr_t h; float stock; int lowUs, raiseMs;
    uint32_t frames, raisedFrames; float worst; double latSum; ULONGLONG notedAt;
} g_lwAib = {.on = 1, .lowUs = LW_AIB_LOW_US, .raiseMs = LW_AIB_RAISE_MS};

/* H = [exe+0x190D4A0 + 2*0x80] (KB world-update-exe-ai-step); the step reads the budget once at 0xBD198C. */
static uintptr_t LwAibWorld(void)
{
    static int checked;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), h = 0;
    if (!checked++ && (!ApiCodeMatches(exe + 0xBD198Cu, "F3 0F 10 8B 7C 04 00 00 48 8B CB E8") || !ApiCodeMatches(exe + 0xC40060u, "F3 0F 10 01 F3 0F 10 49 04")))
    { g_lwAib.failed = 1; Note("living world: AI path budget: this build's AI step is not what the lever expects; off"); return 0; }
    return SafeRead(exe + 0x190D4A0u + 2u * 0x80u, &h, 8) ? 0 : h;
}

/* Request latency as 0xC40060 reads it: the larger of the queued requests' wait (+0x480) and the age of the run in
 * flight (+0x484), both sums of the step's time in seconds (0xC40030; a run's start moves the wait to +0x484 at
 * 0xC400B0, its commit clears it at 0xC400A0). */
static float LwAibLatency(uintptr_t h)
{
    float t[2] = {0, 0};
    if (SafeRead(h + 0x480, t, 8) || !isfinite(t[0]) || !isfinite(t[1])) return -1.0f;
    return t[0] > t[1] ? t[0] : t[1];
}

static void LwAibNote(ULONGLONG now)
{
    if (now - g_lwAib.notedAt < 30000) return;
    if ((g_lwAib.verbose || LwTestTraceOn()) && g_lwAib.frames)
        Note("living world: AI path budget %s %d us: stock %.0f us on %u of %u frames, request wait mean %.0f ms, worst %.0f ms", g_lwAib.on ? "on" : "off",
            g_lwAib.lowUs, (double)g_lwAib.stock * 1e6, g_lwAib.raisedFrames, g_lwAib.frames, 1000.0 * g_lwAib.latSum / g_lwAib.frames, (double)g_lwAib.worst * 1000.0);
    g_lwAib.frames = g_lwAib.raisedFrames = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = now;
}

/* The budget for this step: the low one, or the stock one from a wait over RAISE_MS until it is under half that. The
 * stock one too while a nav completion waits for the world's step, a swap is not applied yet (LwNavHoldBusy) or the
 * queue is long (LwNavQueueLong): the low budget makes that run, and so the wait, several times longer. Only a
 * completion held behind a run in flight gets more than the game's own (LwNavHoldRaiseUs). */
static float LwAibWant(uintptr_t h, int low, ULONGLONG now)
{
    float want = low ? (float)g_lwAib.lowUs / 1000000.0f : g_lwAib.stock;
    if (want > g_lwAib.stock) want = g_lwAib.stock;
    float up = (float)LwNavHoldRaiseUs(h, now) / 1000000.0f;
    return up > want ? up : want;
}
static void LwAibSteer(uintptr_t h, float cur, float lat, ULONGLONG now)
{
    if (lat * 1000.0f > (float)g_lwAib.raiseMs) g_lwAib.raised = 1;
    else if (lat * 1000.0f < 0.5f * (float)g_lwAib.raiseMs) g_lwAib.raised = 0;
    float want = LwAibWant(h, !(g_lwAib.raised || LwNavHoldBusy() || LwNavQueueLong(h)), now);
    if (cur != want) *(volatile float*)(h + 0x47C) = want;
}

/* Game thread, once a frame. A new level makes a new AI world, whose stock value is read before the first write.
 * Off, the held completion's raise still applies, and it still measures when the command asked for notes, for the A/B. */
static void LwAibFrame(void)
{
    if (g_lwAib.failed) return;
    uintptr_t h = LwAibWorld(); float cur = 0;
    if (!h || SafeRead(h + 0x47C, &cur, 4) || !(cur > 0.00001f && cur < 0.1f)) return;
    if (h != g_lwAib.h) { g_lwAib.h = h; g_lwAib.stock = cur; g_lwAib.raised = 0; }
    if (!g_lwAib.on)
    {
        float want = LwAibWant(h, 0, GetTickCount64());
        if (cur != want) *(volatile float*)(h + 0x47C) = want;
        if (!g_lwAib.verbose) return;
    }
    float lat = LwAibLatency(h);
    if (lat < 0) return;
    if (g_lwAib.on) LwAibSteer(h, cur, lat, GetTickCount64());
    g_lwAib.frames++; g_lwAib.raisedFrames += (uint32_t)(g_lwAib.raised || !g_lwAib.on);
    if (lat > g_lwAib.worst) g_lwAib.worst = lat;
    g_lwAib.latSum += lat;
    LwAibNote(GetTickCount64());
}

static void LwAibInit(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_AIBUDGET", t, sizeof t) && t[0] == '0') g_lwAib.on = 0;
    g_lwAib.notedAt = GetTickCount64();
}

/* Leaving a complex level: nothing to put back. The lowered budget is the AI world's, which the level end destroys
 * (exe 0x7D83BF), and the next level's is made from the game's config (KB world-update-exe-ai-step). Writing it here
 * could reach a freed world. */
static void LwAibLeave(void) { g_lwAib.h = 0; g_lwAib.raised = 0; }
/* "aibudget 0" gives the stock budget back; "aibudget US [RAISE_MS]" sets the low budget and the wait that raises it. */
static void LwAibCmd(int us, int raiseMs)
{
    g_lwAib.verbose = 1; g_lwAib.frames = g_lwAib.raisedFrames = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = GetTickCount64();
    if (us == 0)
    {
        g_lwAib.on = 0;
        if (g_lwAib.h && g_lwAib.stock > 0) *(volatile float*)(g_lwAib.h + 0x47C) = g_lwAib.stock;
        Note("living world: test: AI path budget off (stock %.0f us)", (double)g_lwAib.stock * 1e6); return;
    }
    if (us < 50 || us > 2000 || (raiseMs && (raiseMs < 50 || raiseMs > 5000))) { Note("living world: test: aibudget: 50..2000 us, raise 50..5000 ms"); return; }
    g_lwAib.on = 1; g_lwAib.lowUs = us; if (raiseMs) g_lwAib.raiseMs = raiseMs;
    Note("living world: test: AI path budget %d us, stock budget back while requests wait over %d ms", us, g_lwAib.raiseMs);
}
