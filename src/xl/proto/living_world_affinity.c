/* Game and render threads on cores of their own (test: command "affinity 0|1"). The game runs 14 workers, the game
 * thread and the render thread on 16 hardware threads, so the two frame threads always share a core with a worker. In
 * slow frames every game function took longer by the same share (KB prof1) with no extra run-queue wait: the shape of
 * a core shared with a busy sibling. On: the game thread gets one hardware thread of core A, the render thread one of
 * core B, and every other thread of the process the rest, both cores left out. A thread of ours re-applies it every
 * 2 s, so threads made later follow. Cores come from GetLogicalProcessorInformationEx, group 0. */
static struct
{
    volatile LONG on; int ready; volatile DWORD gameTid, renderTid; KAFFINITY gameMask, renderMask, restMask, allMask;
    volatile LONG applied, failed; HANDLE thread; ULONGLONG notedAt; int verbose;
} g_lwAff;

/* Two cores with two hardware threads each, from the processor's own topology; 0 when there are fewer. */
static int LwAffTopology(void)
{
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &size);
    uint8_t* buf = size ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    if (!buf || !GetLogicalProcessorInformationEx(RelationProcessorCore, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf, &size))
    { if (buf) HeapFree(GetProcessHeap(), 0, buf); return 0; }
    KAFFINITY cores[2] = {0, 0}, all = 0; int found = 0;
    for (DWORD at = 0; at < size;)
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf + at);
        KAFFINITY m = info->Processor.GroupMask[0].Group == 0 ? info->Processor.GroupMask[0].Mask : 0;
        all |= m;
        if (found < 2 && __builtin_popcountll(m) == 2) cores[found++] = m;
        at += info->Size;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (found < 2) return 0;
    g_lwAff.gameMask = cores[0] & (~cores[0] + 1); g_lwAff.renderMask = cores[1] & (~cores[1] + 1);
    g_lwAff.restMask = all & ~cores[0] & ~cores[1]; g_lwAff.allMask = all;
    return g_lwAff.restMask != 0;
}

static KAFFINITY LwAffMask(DWORD tid, int on)
{
    if (!on) return g_lwAff.allMask;
    return tid == g_lwAff.gameTid ? g_lwAff.gameMask : tid == g_lwAff.renderTid ? g_lwAff.renderMask : g_lwAff.restMask;
}

/* Every thread of the process: its mask for the current state. */
static void LwAffApply(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {.dwSize = sizeof te}; DWORD pid = GetCurrentProcessId(); LONG ok = 0, bad = 0; int on = g_lwAff.on;
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != pid) continue;
        KAFFINITY m = LwAffMask(te.th32ThreadID, on);
        HANDLE t = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (t && SetThreadAffinityMask(t, m)) ok++; else bad++;
        if (t) CloseHandle(t);
    }
    CloseHandle(snap);
    g_lwAff.applied = ok; g_lwAff.failed = bad;
}

static DWORD WINAPI LwAffLoop(void* unused)
{
    for (LONG was = 0;; Sleep(2000))
    {
        LONG on = g_lwAff.on;
        if (on || was) LwAffApply();
        was = on;
    }
    return 0;
}

/* Game thread, once a frame: its id, and every 30 s after the command a note. The render id comes from LwRhNext. */
static void LwAffFrame(void)
{
    g_lwAff.gameTid = GetCurrentThreadId();
    if (!g_lwAff.verbose || GetTickCount64() - g_lwAff.notedAt < 30000) return;
    g_lwAff.notedAt = GetTickCount64();
    Note("living world: test: affinity %s: game %lu on %llx, render %lu on %llx, %ld threads set, %ld failed", g_lwAff.on ? "on" : "off",
        (unsigned long)g_lwAff.gameTid, (unsigned long long)g_lwAff.gameMask, (unsigned long)g_lwAff.renderTid,
        (unsigned long long)g_lwAff.renderMask, (long)g_lwAff.applied, (long)g_lwAff.failed);
}

static void LwAffinityCmd(int on)
{
    if (!g_lwAff.ready) g_lwAff.ready = LwAffTopology() ? 1 : -1;
    if (g_lwAff.ready < 0) { Note("living world: test: affinity: fewer than two cores with two hardware threads"); return; }
    if (on && !g_lwAff.renderTid) { Note("living world: test: affinity: the render thread is not known yet"); return; }
    if (!g_lwAff.thread) g_lwAff.thread = CreateThread(NULL, 0, LwAffLoop, NULL, 0, NULL);
    g_lwAff.on = on != 0; g_lwAff.verbose = 1; g_lwAff.notedAt = GetTickCount64();
    Note("living world: test: affinity %s (game %llx, render %llx, rest %llx)", on ? "on" : "off", (unsigned long long)g_lwAff.gameMask,
        (unsigned long long)g_lwAff.renderMask, (unsigned long long)g_lwAff.restMask);
}
