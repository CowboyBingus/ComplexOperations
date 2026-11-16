/* Render thread helps its own device chain (test: HELLFORGE_LW_RENDERHELP=1 or command "renderhelp 0|1"). The render
 * device's ordered tasks are kind 1 priority 3 jobs (channel 10, 0x10BF7F..0x10C4C4), and only workers poll channel
 * 10, last after physics. The render thread's waits for that list (0x1EDF10, 0x109CC0, 0x10BB30) poll channels 18 and 0
 * or spin. Each round calls 0x528210; after it, from those waits on the render thread, one channel 10 job runs here
 * (0x624D00 pops and runs it, any thread). A task needing a lock the waiting thread holds would hang a worker too. */
static struct
{
    int on, installed; void* orig; uintptr_t exe;
    volatile LONG ran, rounds, otherRole; ULONGLONG notedAt; int verbose;
} g_lwRh;
typedef uint8_t (*LwRhNextFn)(uintptr_t dev, uintptr_t sched);
typedef int (*LwRhPopFn)(uintptr_t shed, uint8_t channel);

/* The thread's job role as the game reads it (0x627160): [[gs:0x58][0] + 0x1D8], 1 for the render thread. */
static int LwRhRole(void)
{
    uintptr_t* tls = (uintptr_t*)__readgsqword(0x58);
    return tls && tls[0] ? *(int*)(tls[0] + 0x1D8) : -1;
}

static uint8_t LwRhNext(uintptr_t dev, uintptr_t sched)
{
    uint8_t out = ((LwRhNextFn)g_lwRh.orig)(dev, sched);
#if defined(CO_RESEARCH) || defined(CO_TEST)
    if (g_xrfOn) XrfRound((uintptr_t)__builtin_return_address(0), dev); /* message 8's spin rounds (xl_render_frame.c) */
#endif
    if (!g_lwAff.renderTid && LwRhRole() == 1) g_lwAff.renderTid = GetCurrentThreadId(); /* for the affinity test */
    if (!g_lwRh.on || !sched) return out;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0) - g_lwRh.exe;
    if (ret != 0x1EDF26u && ret != 0x109CDAu && ret != 0x10BB9Au) return out;
    if (LwRhRole() != 1) { InterlockedIncrement(&g_lwRh.otherRole); return out; }
    InterlockedIncrement(&g_lwRh.rounds);
    uintptr_t shed = *(volatile uintptr_t*)(sched + 0xF8);
    if (shed && ((LwRhPopFn)(g_lwRh.exe + 0x624D00u))(shed, 10)) InterlockedIncrement(&g_lwRh.ran);
    return out;
}

/* Game thread, once a frame: every 30 s after the command, the wait rounds and the jobs the render thread ran. */
static void LwRhFrame(void)
{
    ULONGLONG now = GetTickCount64();
    if (!g_lwRh.installed || now - g_lwRh.notedAt < 30000) return;
    if (g_lwRh.verbose)
        Note("living world: test: render thread helps channel 10 (%s): %ld wait rounds, %ld jobs run, %ld rounds on another role in 30 s",
            g_lwRh.on ? "on" : "off", (long)g_lwRh.rounds, (long)g_lwRh.ran, (long)g_lwRh.otherRole);
    g_lwRh.rounds = g_lwRh.ran = g_lwRh.otherRole = 0; g_lwRh.notedAt = now;
}

static void LwInstallRenderHelp(void)
{
    static int done; char t[4] = "";
    if (done++) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!ApiCodeMatches(exe + 0x528210u, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20") ||
        !ApiCodeMatches(exe + 0x624D00u, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC") ||
        !ApiCodeMatches(exe + 0x1EDF21u, "E8 EA A2 33 00 41 C7 46 6C 01 00 00 00") ||
        !ApiCodeMatches(exe + 0x109CD5u, "E8 36 E5 41 00 49 8B C6") || !ApiCodeMatches(exe + 0x10BB95u, "E8 76 C6 41 00 84 C0") ||
        LoaderHook("render thread helps its device chain (test)", exe + 0x528210u, (void*)LwRhNext, &g_lwRh.orig) || !g_lwRh.orig)
    { Note("living world: test: render help: this build's render waits are not what it expects; off"); return; }
    g_lwRh.exe = exe; g_lwRh.installed = 1; g_lwRh.notedAt = GetTickCount64();
    g_lwRh.on = GetEnvironmentVariableA("HELLFORGE_LW_RENDERHELP", t, sizeof t) > 0 && t[0] == '1';
}

static void LwRenderHelpCmd(int on)
{
    if (!g_lwRh.installed) { Note("living world: test: render help is not installed"); return; }
    g_lwRh.on = on != 0; g_lwRh.verbose = 1; g_lwRh.rounds = g_lwRh.ran = 0; g_lwRh.notedAt = GetTickCount64();
    Note("living world: test: render thread helps channel 10 %s", g_lwRh.on ? "on" : "off");
}
