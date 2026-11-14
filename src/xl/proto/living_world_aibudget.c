/* AI path time budget (on by default; HELLFORGE_LW_AIBUDGET=0 off; command "aibudget US [RAISE_MS]"): the Havok AI
 * step (exe 0xBD1890) lets its async path threads run for H+0x47C seconds (stock 0.002) while the game thread's join
 * (0x7D7930) spins. 500 us took the join from 1.0-1.5 ms to 0.2-0.3 ms at 200 units (KB xl-living-world-frame-budget,
 * m49). Paths arrive later instead, so the stock budget comes back while requests are older than RAISE_MS.
 * The drain (co_aibudget.h; HELLFORGE_LW_AIDRAIN=US[,RUN_MS[,QUEUE]], 0 off; command "aidrain US [RUN_MS] [QUEUE]")
 * gives a run that is seconds old, or a long queue, more than the game's own, with or without the lever. The nav
 * hold's raise (LwNavHoldRaiseUs) is a third value for the same field: the largest of the three is written. */
#define LW_AIB_LOW_US 500
#define LW_AIB_RAISE_MS 500
#define LW_AIB_DRAIN_LINES 16 /* drain start and end lines a level; the 30 s line counts the rest */
#define LW_AIB_APPLYING 60u   /* a run that applied a change within this many frames is applying */
static struct
{
    int on, failed, verbose; CoAiBudget b; int lowUs, raiseMs; CoAiDrain drain;
    uint32_t frames, raisedFrames; float worst; double latSum; ULONGLONG notedAt;
    uint32_t drainFrames, drainStarts, drainRun, drainLines; int drainMost; ULONGLONG drainAt;
    uint32_t applied, appliedFrames; int appliedMost; uintptr_t unknown;
    uint32_t sinceApplied; int runApplied;       /* frames since the run in flight applied a change; its count so far */
    int wasDraining;                             /* the last frame's step ran at the drain's budget */
    uint32_t drainJoins; double drainJoinSum, drainJoinWorst, runJoinSum, runJoinWorst;
} g_lwAib = {.on = 1, .lowUs = LW_AIB_LOW_US, .raiseMs = LW_AIB_RAISE_MS, .drain = {CO_AIB_DRAIN_US, CO_AIB_DRAIN_RUN_MS, CO_AIB_DRAIN_QUEUE}};

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

/* Navmesh changes the run in flight has applied so far, and the run. Its first stage (exe 0xC958E0) lists every
 * generator change it applies, types 7 to 11, in five arrays of its context: counts at +0x1A8, +0x1B8 .. +0x1E8
 * (0xC95974..0xC95A1C). The context is [run+0x68] (0xC3F89F, handed to the stage at 0xC3FE7B), the run [H+0x4C0]. */
static int LwAibApplied(uintptr_t h, uintptr_t* run)
{
    uintptr_t task = 0, ctx = 0; int sum = 0;
    *run = 0;
    if (SafeRead(h + 0x4B0, &task, 8) || !task || SafeRead(h + 0x4C0, run, 8) || !*run || SafeRead(*run + 0x68, &ctx, 8) || !ctx) { *run = 0; return 0; }
    for (int k = 0; k < 5; k++)
    {
        int32_t n = 0;
        if (SafeRead(ctx + 0x1A8u + 0x10u * (uintptr_t)k, &n, 4) || n < 0) { *run = 0; return 0; }
        sum += n;
    }
    return sum;
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
/* The thread's TEB, 0 when it does not read. Its stack bounds (StackBase +8, StackLimit +0x10) are read once the
 * thread is stopped: a parked thread's Rsp is within 4 KiB of the top, an Rsp on another stack is outside them, and
 * a copy that faults runs fault dispatch on this thread, which can wait for a lock the stopped thread holds. */
static uintptr_t LwAibTeb(HANDLE t)
{
    typedef LONG (WINAPI *Query)(HANDLE, int, void*, ULONG, ULONG*);
    static Query query; static int looked;
    if (!looked++) { HMODULE nt = GetModuleHandleA("ntdll.dll"); if (nt) query = (Query)(void*)GetProcAddress(nt, "NtQueryInformationThread"); }
    struct { uintptr_t exitStatus, teb, pid, tid, affinity, priority; } info = {0};
    return query && query(t, 0, &info, (ULONG)sizeof info, NULL) >= 0 ? info.teb : 0;
}
static void LwAibThread(DWORD id, uintptr_t exe, uintptr_t end)
{
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    if (!t) return;
    CONTEXT c = {0}; c.ContextFlags = CONTEXT_CONTROL; size_t got = 0; uintptr_t teb = LwAibTeb(t), bound[2] = {0, 0};
    if (teb && SuspendThread(t) != (DWORD)-1)
    {
        size_t want = 0;
        if (GetThreadContext(t, &c) && !SafeRead(teb + 8, bound, 16) && co_aibudget_stack((uintptr_t)c.Rsp, bound[1], bound[0], LW_AIB_STACK, &want))
            for (size_t n; got < want; got += n)
            { n = want - got < 256 ? want - got : 256; if (SafeRead((uintptr_t)c.Rsp + got, (uint8_t*)g_lwAibW.stack + got, n)) break; }
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
        Note("living world: AI path budget %s %d us: stock %.0f us on %u of %u frames, request wait mean %.0f ms, worst %.0f ms; %d changes queued for the next run; drain %d us on %u frames, %u start(s), most queued %d; runs applied %u generator changes on %u frames, most %d a frame; joins on %u drain frames mean %.2f ms, worst %.2f ms; AI world %p",
            g_lwAib.on ? "on" : "off", g_lwAib.lowUs, (double)g_lwAib.b.stock * 1e6, g_lwAib.raisedFrames, g_lwAib.frames, 1000.0 * g_lwAib.latSum / g_lwAib.frames,
            (double)g_lwAib.worst * 1000.0, g_lwAib.b.world ? LwAibQueued(g_lwAib.b.world) : -1, g_lwAib.drain.us, g_lwAib.drainFrames, g_lwAib.drainStarts, g_lwAib.drainMost,
            g_lwAib.applied, g_lwAib.appliedFrames, g_lwAib.appliedMost, g_lwAib.drainJoins, g_lwAib.drainJoins ? g_lwAib.drainJoinSum / g_lwAib.drainJoins : 0.0, g_lwAib.drainJoinWorst,
            (void*)g_lwAib.b.world);
    g_lwAib.frames = g_lwAib.raisedFrames = g_lwAib.drainFrames = g_lwAib.drainStarts = g_lwAib.applied = g_lwAib.appliedFrames = 0;
    g_lwAib.drainMost = g_lwAib.appliedMost = 0; g_lwAib.drainJoins = 0; g_lwAib.drainJoinSum = g_lwAib.drainJoinWorst = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = now;
}

/* The drain's counts and lines after a step's choice: frames at the raised budget are what it costs (the game
 * thread's join waits about as long as the step gives), so its starts and ends are written out. */
static void LwAibDrainNote(int was, int inFlight, float runS, int queued)
{
    if (queued > g_lwAib.drainMost) g_lwAib.drainMost = queued;
    if (g_lwAib.b.draining) { g_lwAib.drainFrames++; g_lwAib.drainRun++; }
    if (g_lwAib.b.draining == was) return;
    ULONGLONG now = GetTickCount64();
    if (!was) { g_lwAib.drainStarts++; g_lwAib.drainRun = 1; g_lwAib.drainAt = now; g_lwAib.runJoinSum = g_lwAib.runJoinWorst = 0; }
    if (g_lwAib.drainLines >= LW_AIB_DRAIN_LINES) return;
    g_lwAib.drainLines++;
    /* an old run that applies nothing is old by its path requests or clearance, not by navmesh changes */
    int applying = inFlight && g_lwAib.runApplied > 0 && g_lwAib.sinceApplied <= LW_AIB_APPLYING;
    if (!was) Note("living world: AI drain on: the run in flight is %.1f s old and %s (%d generator changes so far, the last %u frames ago), %d changes queued for the next; %d us a step until the run is young and the queue short",
        inFlight ? (double)runS : 0.0, !inFlight ? "none is in flight" : applying ? "applying navmesh changes" : "not applying navmesh changes", g_lwAib.runApplied, g_lwAib.sinceApplied, queued, g_lwAib.drain.us);
    else Note("living world: AI drain off after %u frames (%.1f s), joins mean %.2f ms, worst %.2f ms: %d changes queued%s", g_lwAib.drainRun, (double)(now - g_lwAib.drainAt) / 1000.0,
        g_lwAib.drainRun ? g_lwAib.runJoinSum / g_lwAib.drainRun : 0.0, g_lwAib.runJoinWorst, queued, g_lwAib.drainLines >= LW_AIB_DRAIN_LINES ? "; no more drain lines this level" : "");
}

/* The budget for this step (co_aibudget.h), written when it differs. Every write is told to co_aibudget_wrote, so no
 * value of the mod's is read back as the game's. The low budget makes a run, and so a held nav completion's wait,
 * several times longer: not while one waits, a swap is unapplied or the queue is long. */
static void LwAibSteer(uintptr_t h, float cur, float lat, float runS, ULONGLONG now)
{
    uintptr_t task = 0; int was = g_lwAib.b.draining, queued = LwAibQueued(h);
    if (SafeRead(h + 0x4B0, &task, 8)) task = 0;
    int lever = g_lwAib.on && !(LwNavHoldBusy() || LwNavQueueLong(h));
    float more = (float)LwNavHoldRaiseUs(h, now) / 1000000.0f;
    float want = co_aibudget_step(&g_lwAib.b, &g_lwAib.drain, lever, lat, g_lwAib.lowUs, g_lwAib.raiseMs, task != 0, runS, queued, more);
    LwAibDrainNote(was, task != 0, runS, queued);
    if (want > 0 && cur != want) { *(volatile float*)(h + 0x47C) = want; co_aibudget_wrote(&g_lwAib.b, want); }
}

/* Game thread, once a frame. A new level makes a new AI world, whose stock value is read before the first write.
 * Off, the drain and the nav hold's raise still apply over the game's own budget, and the lever measures for the A/B. */
static void LwAibFrame(void)
{
    if (g_lwAib.failed || (!g_lwAib.on && !g_lwAib.verbose && g_lwAib.drain.us <= 0 && !g_lwNavHold.worlds)) return;
    uintptr_t h = LwAibWorld(); float cur = 0;
    if (!h || SafeRead(h + 0x47C, &cur, 4) || !(cur > 0.00001f && cur < 0.1f)) return;
    if (!(co_aibudget_stock(&g_lwAib.b, h, cur) > 0))
    {
        /* another instance of the mod lowered this world (a DLL swapped into a held session): no stock to return to */
        if (g_lwAib.unknown != h) Note("living world: AI path budget: AI world %p holds %.0f us, not a game value; left alone until it reads %.0f to %.0f us", (void*)h, (double)cur * 1e6, (double)CO_AIB_STOCK_LEAST * 1e6, (double)CO_AIB_STOCK_MOST * 1e6);
        g_lwAib.unknown = h; return;
    }
    /* the step the last frame's budget ran in was joined before this tick: its wait is what a drain frame, or one
     * of the nav hold's raise, costs. One hook times the join (living_world_fparts.c); both statistics get the value. */
    double join = LwFpJoinTake();
    LwNavHoldJoin(join);
    if (g_lwAib.wasDraining)
    {
        g_lwAib.drainJoins++; g_lwAib.drainJoinSum += join; g_lwAib.runJoinSum += join;
        if (join > g_lwAib.drainJoinWorst) g_lwAib.drainJoinWorst = join;
        if (join > g_lwAib.runJoinWorst) g_lwAib.runJoinWorst = join;
    }
    uintptr_t run = 0; int sum = LwAibApplied(h, &run), did = co_aibudget_applied(&g_lwAib.b, run, sum);
    if (did) { g_lwAib.applied += (uint32_t)did; g_lwAib.appliedFrames++; if (did > g_lwAib.appliedMost) g_lwAib.appliedMost = did; }
    g_lwAib.runApplied = run ? sum : 0; g_lwAib.sinceApplied = did ? 0 : g_lwAib.sinceApplied + 1;
    float lat = LwAibLatency(h), t[2] = {0, 0};
    if (lat < 0 || SafeRead(h + 0x480, t, 8)) return;
#ifdef CO_RESEARCH
    LwAibWatch(h, t[0], t[1]);
#endif
    LwAibSteer(h, cur, lat, t[1], GetTickCount64());
    g_lwAib.wasDraining = g_lwAib.b.draining;
    g_lwAib.frames++; g_lwAib.raisedFrames += (uint32_t)(g_lwAib.b.raised || !g_lwAib.on);
    if (lat > g_lwAib.worst) g_lwAib.worst = lat;
    g_lwAib.latSum += lat;
    LwAibNote(GetTickCount64());
}

/* "aidrain 0" is no drain; "aidrain US [RUN_MS] [QUEUE]" sets its budget, the run's age and the queue that start it.
 * 20 ms is the most: the step holds the frame that long. */
static void LwAibDrainCmd(const char* text)
{
    int us = -1, runMs = 0, queue = 0;
    sscanf(text, "%d%*[ ,]%d%*[ ,]%d", &us, &runMs, &queue);
    if (us < 0 || us > 20000 || runMs < 0 || runMs > 600000 || queue < 0 || queue > 1000000) { Note("living world: test: aidrain: 0..20000 us, run 0..600000 ms, queue 0..1000000"); return; }
    g_lwAib.drain.us = us; if (runMs) g_lwAib.drain.runMs = runMs; if (queue) g_lwAib.drain.queue = queue;
    g_lwAib.drainLines = 0;
    Note("living world: test: AI drain %d us a step for a run over %d ms old or %d changes queued", us, g_lwAib.drain.runMs, g_lwAib.drain.queue);
}

static void LwAibInit(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_AIBUDGET", t, sizeof t) && t[0] == '0') g_lwAib.on = 0;
    char d[48] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_AIDRAIN", d, sizeof d) && d[0]) LwAibDrainCmd(d);
    g_lwAib.notedAt = GetTickCount64();
}

/* Leaving a complex level: the game's budget goes back. One AI world serves the session (its destroy 0x7D83A0 has
 * one caller, 0x78F184, the world's release), so a value left here would hold on the ship and in every later
 * mission, where the lever does not run. The record is read anew: destroy zeroes it (0x7D85A3). */
static void LwAibLeave(void)
{
    uintptr_t h = g_lwAib.failed || !g_lwAib.b.world ? 0 : LwAibWorld(); float cur = 0;
    if (h && h == g_lwAib.b.world && g_lwAib.b.stock > 0 && !SafeRead(h + 0x47C, &cur, 4) && cur != g_lwAib.b.stock)
    {
        *(volatile float*)(h + 0x47C) = g_lwAib.b.stock; co_aibudget_wrote(&g_lwAib.b, g_lwAib.b.stock);
        Note("living world: AI path budget: the game's %.0f us is back in AI world %p at the level's end (it held %.0f us)", (double)g_lwAib.b.stock * 1e6, (void*)h, (double)cur * 1e6);
    }
    co_aibudget_leave(&g_lwAib.b); g_lwAib.drainLines = 0; g_lwAib.unknown = 0; g_lwAib.wasDraining = 0;
}
/* "aibudget 0" gives the stock budget back; "aibudget US [RAISE_MS]" sets the low budget and the wait that raises it. */
static void LwAibCmd(int us, int raiseMs)
{
    g_lwAib.verbose = 1; g_lwAib.frames = g_lwAib.raisedFrames = 0; g_lwAib.worst = 0; g_lwAib.latSum = 0; g_lwAib.notedAt = GetTickCount64();
    if (us == 0)
    {
        g_lwAib.on = 0;
        if (g_lwAib.b.world && g_lwAib.b.stock > 0) { *(volatile float*)(g_lwAib.b.world + 0x47C) = g_lwAib.b.stock; co_aibudget_wrote(&g_lwAib.b, g_lwAib.b.stock); }
        Note("living world: test: AI path budget off (stock %.0f us)", (double)g_lwAib.b.stock * 1e6); return;
    }
    if (us < 50 || us > 2000 || (raiseMs && (raiseMs < 50 || raiseMs > 5000))) { Note("living world: test: aibudget: 50..2000 us, raise 50..5000 ms"); return; }
    g_lwAib.on = 1; g_lwAib.lowUs = us; if (raiseMs) g_lwAib.raiseMs = raiseMs;
    Note("living world: test: AI path budget %d us, stock budget back while requests wait over %d ms", us, g_lwAib.raiseMs);
}
