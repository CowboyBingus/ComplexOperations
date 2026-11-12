/* Worker wake tokens capped (test: HELLFORGE_LW_SEMCAP=1 or command "semcap 0|1"). A job kick (0x625400) adds one
 * token per ready job to both worker semaphores (split mode, byte 0x1B135E9), so kicks of hundreds of jobs leave
 * hundreds of tokens for 14 workers: live, the counts held 50-60 on average and up to 840. A worker takes one token per
 * pass and a pass polls until its channels are empty (0x625380..0x6253C5), so tokens past the worker count only buy
 * empty passes. After the game's own release, each count is brought down to the worker count; every worker can
 * still make one pass after any kick. KB world-update-exe-topc. */
static struct
{
    int on, installed; void* orig; int cap;
    volatile LONG kicks, cut; volatile LONG64 cutTokens; ULONGLONG notedAt; int verbose;
} g_lwSem;
typedef void (*LwSemWakeFn)(uintptr_t cb, uint32_t channel, uint32_t count);

/* Bring the count at sem+0x10 down to the cap; the workers' own decrements may race, so compare-exchange. */
static void LwSemClamp(uintptr_t sem)
{
    volatile LONG* c = (volatile LONG*)(sem + 0x10);
    for (LONG v = *c; v > g_lwSem.cap; v = *c)
        if (InterlockedCompareExchange(c, g_lwSem.cap, v) == v) { InterlockedIncrement(&g_lwSem.cut); InterlockedAdd64(&g_lwSem.cutTokens, v - g_lwSem.cap); return; }
}

static void LwSemWake(uintptr_t cb, uint32_t channel, uint32_t count)
{
    ((LwSemWakeFn)g_lwSem.orig)(cb, channel, count);
    if (!g_lwSem.on) return;
    InterlockedIncrement(&g_lwSem.kicks);
    uintptr_t s = *(volatile uintptr_t*)(cb + 8);
    if ((channel & 0xFF) / 3 == 4 || !s) return; /* channels 12..14 wake the two extra threads, not the workers */
    uintptr_t a = *(volatile uintptr_t*)(s + 0xD0), b = *(volatile uintptr_t*)(s + 0xD8);
    if (a) LwSemClamp(a);
    if (b) LwSemClamp(b);
}

/* Game thread, once a frame: every 30 s after the command, the kicks and the tokens taken away. */
static void LwSemFrame(void)
{
    ULONGLONG now = GetTickCount64();
    if (!g_lwSem.installed || now - g_lwSem.notedAt < 30000) return;
    if (g_lwSem.verbose)
        Note("living world: test: worker tokens capped at %d (%s): %ld kicks, %ld counts cut, %lld tokens taken in 30 s", g_lwSem.cap,
            g_lwSem.on ? "on" : "off", (long)g_lwSem.kicks, (long)g_lwSem.cut, (long long)g_lwSem.cutTokens);
    g_lwSem.kicks = g_lwSem.cut = 0; g_lwSem.cutTokens = 0; g_lwSem.notedAt = now;
}

static void LwInstallSemCap(void)
{
    static int done; char t[4] = "";
    if (done++) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL); int w = 0;
    if (!ApiCodeMatches(exe + 0x625400u, "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 20 0F B6 F2 B8 AB") ||
        !ApiCodeMatches(exe + 0x625380u, "48 8B 4B 20 41 8B C7 F0 0F C1 41 10 83 F8 01 7D 0F") ||
        SafeRead(exe + 0x23608E4u, &w, 4) || w < 1 || w > 64 ||
        LoaderHook("worker wake tokens capped (test)", exe + 0x625400u, (void*)LwSemWake, &g_lwSem.orig) || !g_lwSem.orig)
    { Note("living world: test: worker token cap: this build's job system is not what it expects; off"); return; }
    g_lwSem.cap = w; g_lwSem.installed = 1; g_lwSem.notedAt = GetTickCount64();
    g_lwSem.on = GetEnvironmentVariableA("HELLFORGE_LW_SEMCAP", t, sizeof t) > 0 && t[0] == '1';
}

static void LwSemCapCmd(int on)
{
    if (!g_lwSem.installed) { Note("living world: test: worker token cap is not installed"); return; }
    g_lwSem.on = on != 0; g_lwSem.verbose = 1; g_lwSem.kicks = g_lwSem.cut = 0; g_lwSem.cutTokens = 0; g_lwSem.notedAt = GetTickCount64();
    Note("living world: test: worker token cap %s (cap %d)", g_lwSem.on ? "on" : "off", g_lwSem.cap);
}
