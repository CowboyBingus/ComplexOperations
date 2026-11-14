/* AI path time budget (on by default; HELLFORGE_LW_AIBUDGET=0 off; command "aibudget US [RAISE_MS]"): the Havok AI
 * step (exe 0xBD1890) lets its async path threads run for H+0x47C seconds (stock 0.002) while the game thread's join
 * (0x7D7930) spins. 500 us took the join from 1.0-1.5 ms to 0.2-0.3 ms at 200 units (KB xl-living-world-frame-budget,
 * m49). Paths arrive later instead, so the stock budget comes back while requests are older than RAISE_MS. */
#define LW_AIB_LOW_US 500
#define LW_AIB_RAISE_MS 500
static struct
{
    int on, failed, verbose; CoAiBudget b; int lowUs, raiseMs;
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

/* Changes queued for the world's next run (section loads and unloads, a cutter made or removed each): the count as
 * exe 0xAD4740 reads it from [H+0x4A0]; -1 when it does not read. One run applies them one by one inside the budget. */
static int LwAibQueued(uintptr_t h)
{
    uintptr_t q = 0, a = 0; int32_t n = 0, m = 0;
    if (SafeRead(h + 0x4A0, &q, 8) || !q || SafeRead(q, &a, 8) || !a || SafeRead(a + 8, &n, 4) || SafeRead(q + 0x1C, &m, 4)) return -1;
    return n + m;
}

#ifdef CO_RESEARCH
/* Stall watchdog (research build): a run in flight or a request waiting over 30 s means navigation stands (no cuts,
 * no paths). The freeze report only fires on a stopped clock, so this writes where every thread is while frames still
 * run: one thread stopped at a time, its stack copied, then read and logged after it runs again (no log or heap call
 * while a thread is stopped). Again every 60 s while it lasts. */
#define LW_AIB_STALL_S 30.0f
#define LW_AIB_STALL_AGAIN_MS 60000
#define LW_AIB_STACK 0x1000
static struct { ULONGLONG at; uint32_t dumps; uint64_t stack[LW_AIB_STACK / 8]; } g_lwAibW;
static void LwAibThread(DWORD id, uintptr_t exe, uintptr_t end)
{
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
    if (!t) return;
    CONTEXT c = {0}; c.ContextFlags = CONTEXT_CONTROL; size_t got = 0;
    if (SuspendThread(t) != (DWORD)-1)
    {
        if (GetThreadContext(t, &c))
            for (; got < LW_AIB_STACK; got += 256)
                if (SafeRead((uintptr_t)c.Rsp + got, (uint8_t*)g_lwAibW.stack + got, 256)) break;
        ResumeThread(t);
    }
    CloseHandle(t);
    if (!c.Rip) return;
    char line[512]; int n = 0, shown = 0;
    for (size_t i = 0; i < got / 8 && shown < 20 && n < (int)sizeof line - 16; i++)
        if (g_lwAibW.stack[i] >= exe && g_lwAibW.stack[i] < end)
        { n += snprintf(line + n, sizeof line - (size_t)n, " x+0x%llx", (unsigned long long)(g_lwAibW.stack[i] - exe)); shown++; }
    if (c.Rip >= exe && c.Rip < end) Note("living world: nav stall: thread %lu at helldivers2.exe+0x%llx; stack:%s", (unsigned long)id, (unsigned long long)(c.Rip - exe), n ? line : " none in the exe");
    else if (shown) Note("living world: nav stall: thread %lu at 0x%llx (outside the exe); stack:%s", (unsigned long)id, (unsigned long long)c.Rip, line);
}
static void LwAibWatch(uintptr_t h, float waitS, float runS)
{
    ULONGLONG now = GetTickCount64();
    if (!(waitS > LW_AIB_STALL_S || runS > LW_AIB_STALL_S)) { g_lwAibW.at = 0; return; }
    if (g_lwAibW.at && now - g_lwAibW.at < LW_AIB_STALL_AGAIN_MS) return;
    g_lwAibW.at = now; g_lwAibW.dumps++;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), task = 0; uint32_t size = 0, pe = 0; uint8_t permit = 0; float budget = 0;
    if (SafeRead(exe + 0x3C, &pe, 4) || SafeRead(exe + pe + 0x50, &size, 4) || !size) return; /* SizeOfImage */
    SafeRead(h + 0x4B0, &task, 8); SafeRead(h + 0x478, &permit, 1); SafeRead(h + 0x47C, &budget, 4);
    Note("living world: nav stall %u: requests wait %.1f s, the run in flight is %.1f s old (task %s, commit permit %u), %d changes queued for the next run, budget %.0f us; game thread %lu; threads:",
        g_lwAibW.dumps, (double)waitS, (double)runS, task ? "set" : "none", permit, LwAibQueued(h), (double)budget * 1e6, (unsigned long)GetCurrentThreadId());
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {sizeof te}; DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) LwAibThread(te.th32ThreadID, exe, exe + size);
    CloseHandle(snap);
    Note("living world: nav stall %u: end of threads", g_lwAibW.dumps);
}
#endif

static void LwAibNote(ULONGLONG now)
{
    if (now - g_lwAib.notedAt < 30000) return;
    if (g_lwAib.frames) /* every build: a request wait that stays high means paths reach squads late (online squads standing) */
        Note("living world: AI path budget %s %d us: stock %.0f us on %u of %u frames, request wait mean %.0f ms, worst %.0f ms; %d changes queued for the next run", g_lwAib.on ? "on" : "off",
            g_lwAib.lowUs, (double)g_lwAib.b.stock * 1e6, g_lwAib.raisedFrames, g_lwAib.frames, 1000.0 * g_lwAib.latSum / g_lwAib.frames, (double)g_lwAib.worst * 1000.0,
            g_lwAib.b.world ? LwAibQueued(g_lwAib.b.world) : -1);
    g_lwAib.frames = g_lwAib.raisedFrames = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = now;
}

/* The budget for this step (co_aibudget.h), written when it differs. */
static void LwAibSteer(uintptr_t h, float cur, float lat)
{
    float want = co_aibudget_want(&g_lwAib.b, lat, g_lwAib.lowUs, g_lwAib.raiseMs);
    if (cur != want) { *(volatile float*)(h + 0x47C) = want; co_aibudget_wrote(&g_lwAib.b, want); }
}

/* Game thread, once a frame. A new level makes a new AI world, whose stock value is read before the first write.
 * Off, it still measures when the command asked for notes, for the A/B. */
static void LwAibFrame(void)
{
    if (g_lwAib.failed || (!g_lwAib.on && !g_lwAib.verbose)) return;
    uintptr_t h = LwAibWorld(); float cur = 0;
    if (!h || SafeRead(h + 0x47C, &cur, 4) || !(cur > 0.00001f && cur < 0.1f)) return;
    co_aibudget_stock(&g_lwAib.b, h, cur);
    float lat = LwAibLatency(h);
    if (lat < 0) return;
#ifdef CO_RESEARCH
    { float t[2] = {0, 0}; if (!SafeRead(h + 0x480, t, 8)) LwAibWatch(h, t[0], t[1]); }
#endif
    if (g_lwAib.on) LwAibSteer(h, cur, lat);
    g_lwAib.frames++; g_lwAib.raisedFrames += (uint32_t)(g_lwAib.b.raised || !g_lwAib.on);
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
static void LwAibLeave(void) { co_aibudget_leave(&g_lwAib.b); }
/* "aibudget 0" gives the stock budget back; "aibudget US [RAISE_MS]" sets the low budget and the wait that raises it. */
static void LwAibCmd(int us, int raiseMs)
{
    g_lwAib.verbose = 1; g_lwAib.frames = g_lwAib.raisedFrames = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = GetTickCount64();
    if (us == 0)
    {
        g_lwAib.on = 0;
        if (g_lwAib.b.world && g_lwAib.b.stock > 0) *(volatile float*)(g_lwAib.b.world + 0x47C) = g_lwAib.b.stock;
        Note("living world: test: AI path budget off (stock %.0f us)", (double)g_lwAib.b.stock * 1e6); return;
    }
    if (us < 50 || us > 2000 || (raiseMs && (raiseMs < 50 || raiseMs > 5000))) { Note("living world: test: aibudget: 50..2000 us, raise 50..5000 ms"); return; }
    g_lwAib.on = 1; g_lwAib.lowUs = us; if (raiseMs) g_lwAib.raiseMs = raiseMs;
    Note("living world: test: AI path budget %d us, stock budget back while requests wait over %d ms", us, g_lwAib.raiseMs);
}
