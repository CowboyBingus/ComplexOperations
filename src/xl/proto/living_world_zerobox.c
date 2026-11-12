/* Absent SpeedTree cells kept out of every view. 0x212150 gives a cell missing from the forest hash an all-zero box, and
 * 0x953B70 passes that box whenever the view holds the world origin. On XL maps the static last sun-shadow slice's four
 * origin tiles then walk about 20,000 empty cells, and the render thread waits 5-9 ms on 4 of every 64 frames. The
 * mod's path writes the type's own empty box (+FLT_MAX min, -FLT_MAX max), which 0x953B70 skips at its first test.
 * Absent cells hold no instances, so nothing drawn changes. A switched site (../co_switch.h): complex levels take the
 * mod's path, every other level the game's 31 bytes. Research builds: HELLFORGE_LW_ZEROBOX=0 leaves it out. */
#define LW_ZB_AT 0x21265Du
#define LW_ZB_LEN 31
static const uint8_t g_lwZbOld[LW_ZB_LEN] = {0x0F, 0x57, 0xC0, 0x41, 0x0F, 0x11, 0x46, 0x58, 0x0F, 0x11, 0x45, 0x10, 0x41, 0x0F, 0x28, 0xC1,
    0x41, 0x0F, 0x14, 0xC1, 0xF2, 0x0F, 0x11, 0x45, 0x20, 0xF2, 0x41, 0x0F, 0x11, 0x46, 0x68};
/* movaps xmm0, [exe+0x16AA990]; movups [r14+0x58], xmm0; shufps xmm0, xmm0, 0xFF; movsd [r14+0x68], xmm0. The game's
 * stores to [rbp+0x10] and [rbp+0x20] are dead locals and xmm0 is dead after the block (KB speedtree-absent-cell-zero-box). */
static const uint8_t g_lwZbMod[22] = {0x0F, 0x28, 0x05, 0, 0, 0, 0, 0x41, 0x0F, 0x11, 0x46, 0x58, 0x0F, 0xC6, 0xC0, 0xFF,
    0xF2, 0x41, 0x0F, 0x11, 0x46, 0x68};

static struct
{
    uintptr_t exe; int ready, refused; void* passOrig;
    uintptr_t writeAt; size_t writeLen; /* the range LwZbWrite may not stop a thread inside */
    volatile LONG64 views, cells, zero, empty, originViews, originZero, zeroMax, nodesMax;
} g_lwZb;

/* The process's other threads, read before any is stopped: the snapshot's walk maps views, which takes a lock. */
#define LW_ZB_THREADS 1024
static int LwZbThreads(DWORD* ids)
{
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 te = {sizeof te};
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) { if (n == LW_ZB_THREADS) { n = -1; break; } ids[n++] = te.th32ThreadID; }
    CloseHandle(snap);
    return n;
}

/* Those threads stopped; 0 when one could not be, or its rip is strictly inside the range written, where it would
 * resume between the new instructions. Only system calls run here. The caller resumes `count` threads either way. */
static int LwZbStopAll(const DWORD* ids, int n, int* count)
{
    int ok = 1;
    for (int i = 0; i < n && ok; i++)
    {
        DWORD64 rip = 0;
        ok = LwRegSuspend(ids[i], count, &rip) && !(rip > g_lwZb.writeAt && rip < g_lwZb.writeAt + g_lwZb.writeLen);
    }
    return ok;
}

/* The bytes as aligned 8-byte stores, with the other threads stopped: no lock, allocation or log here. */
static void LwZbStore(const uint8_t* bytes, uintptr_t at, size_t len, uintptr_t lo, uintptr_t hi)
{
    for (uintptr_t q = lo; q < hi; q += 8)
    {
        uint64_t v = *(volatile uint64_t*)q;
        for (uintptr_t a = q; a < q + 8; a++)
            if (a >= at && a < at + len) ((uint8_t*)&v)[a - q] = bytes[a - at];
        __atomic_store_n((volatile uint64_t*)q, v, __ATOMIC_SEQ_CST);
    }
}

/* The switched site's writer (co_switch_install_by): 0 written. Eight tries back to back; a thread inside the range
 * every time refuses the install, which the next level init tries again. */
static int LwZbWrite(uintptr_t at, const void* data, size_t len)
{
    uintptr_t lo = at & ~(uintptr_t)7, hi = (at + len + 7) & ~(uintptr_t)7;
    DWORD prot; int bad = 1;
    if (len == 0 || len > 64 || !VirtualProtect((void*)lo, hi - lo, PAGE_EXECUTE_READWRITE, &prot)) return 1;
    AcquireSRWLockExclusive(&g_hookLock);
    g_lwZb.writeAt = at; g_lwZb.writeLen = len;
    static DWORD ids[LW_ZB_THREADS]; /* under g_hookLock */
    for (int tries = 0; tries < 8 && bad; tries++)
    {
        int count = 0, n = LwZbThreads(ids), ok = n >= 0 && LwZbStopAll(ids, n, &count);
        if (ok) LwZbStore((const uint8_t*)data, at, len, lo, hi);
        LwRegResume(count);
        bad = !ok;
    }
    ReleaseSRWLockExclusive(&g_hookLock);
    VirtualProtect((void*)lo, hi - lo, prot, &prot);
    if (bad) return 1;
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    return 0;
}

static uintptr_t LwZbSite(void) { return g_lwZb.exe + LW_ZB_AT; }

/* Research: "zerobox 0|1" holds the game's or the mod's path until the next level change. */
static void LwZeroBoxCmd(int on)
{
    if (!g_lwZb.ready || on < 0 || on > 1) { Note("living world: test: zerobox: not installed, or a value outside 0..1"); return; }
    co_switch_live(CO_SWITCH_XL, on);
    Note("living world: absent SpeedTree cells %s", on ? "get the empty box every view skips (0x21265D)" : "get the game's zero box");
}

static void LwZbMax(volatile LONG64* at, LONG64 v) { LONG64 o = *at; while (v > o && InterlockedCompareExchange64(at, v, o) != o) o = *at; }

/* Counter (HELLFORGE_LW_ZEROBOX_COUNT=1), on the view task's thread before the game reads the boxes. With the lever on,
 * absent cells are among the empty boxes, so the zero-box count is the absent count only with it off. */
typedef uintptr_t (*LwZbPassFn)(uintptr_t owner, uintptr_t view, uint32_t stamp);
static uintptr_t LwZbPass(uintptr_t owner, uintptr_t view, uint32_t stamp)
{
    uint64_t data = 0, n = 0, nodes = 0;
    LONG64 zero = 0, empty = 0;
    if (!SafeRead(owner + 0x88, &data, 8) && !SafeRead(owner + 0x90, &n, 8) && data && n < 1000000)
        for (const uint8_t* c = (const uint8_t*)data; c < (const uint8_t*)data + n * 0xE0; c += 0xE0)
        {
            const uint32_t* b = (const uint32_t*)(c + 0x58);
            if (!(b[0] | b[1] | b[2] | b[3] | b[4] | b[5])) zero++;
            else if (b[0] == 0x7F7FFFFFu && b[1] == 0x7F7FFFFFu && b[2] == 0x7F7FFFFFu && b[3] == 0xFF7FFFFFu && b[4] == 0xFF7FFFFFu &&
                b[5] == 0xFF7FFFFFu) empty++;
        }
    int origin = 1; /* the game rejects a zero box only when a plane's w is below 0, so NaN counts as in */
    for (int k = 0; k < 6; k++) { float w = 0; SafeRead(view + 0x220 + 16u * (unsigned)k, &w, 4); if (w < 0) origin = 0; }
    uintptr_t out = ((LwZbPassFn)g_lwZb.passOrig)(owner, view, stamp);
    SafeRead(owner + 0xC0, &nodes, 8);
    InterlockedIncrement64(&g_lwZb.views); InterlockedExchangeAdd64(&g_lwZb.cells, (LONG64)n);
    InterlockedExchangeAdd64(&g_lwZb.zero, zero); InterlockedExchangeAdd64(&g_lwZb.empty, empty);
    if (origin) { InterlockedIncrement64(&g_lwZb.originViews); InterlockedExchangeAdd64(&g_lwZb.originZero, zero); LwZbMax(&g_lwZb.zeroMax, zero); }
    LwZbMax(&g_lwZb.nodesMax, (LONG64)nodes);
    return out;
}

static void LwZbNote(void)
{
    if (!g_lwZb.ready) return;
    const int on = co_switch_path(g_lwZb.exe + LW_ZB_AT) == 1;
    if (!g_lwZb.passOrig) return;
    Note("living world: absent SpeedTree cells (30 s): lever %s; %lld view passes, %lld cells, %lld zero boxes, %lld empty boxes; "
         "%lld passes hold the origin, with %lld zero boxes (most in one %lld); largest node tree %lld", on ? "on" : "off",
        (long long)InterlockedExchange64(&g_lwZb.views, 0), (long long)InterlockedExchange64(&g_lwZb.cells, 0),
        (long long)InterlockedExchange64(&g_lwZb.zero, 0), (long long)InterlockedExchange64(&g_lwZb.empty, 0),
        (long long)InterlockedExchange64(&g_lwZb.originViews, 0), (long long)InterlockedExchange64(&g_lwZb.originZero, 0),
        (long long)InterlockedExchange64(&g_lwZb.zeroMax, 0), (long long)InterlockedExchange64(&g_lwZb.nodesMax, 0));
}

/* With the extra large world's guards (../co_xl.c), and again at each complex level init until it is in. */
static void LwInstallZeroBox(void)
{
    char t[4] = "", cnt[4] = "";
    if (g_lwZb.ready || g_lwZb.refused) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_ZEROBOX", t, sizeof t) > 0 && t[0] == '0') { g_lwZb.refused = 1; return; }
    g_lwZb.exe = ApiModuleBase("helldivers2.exe", NULL);
    if (g_lwZb.exe == 0) g_lwZb.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (strcmp(g_build, "25480438") ||
        !ApiCodeMatches(g_lwZb.exe + 0x212650u, "42 8B 54 11 60 81 FA FF FF FF 7F 75 E3") ||
        !ApiCodeMatches(g_lwZb.exe + 0x21267Cu, "41 FF C7 41 8B C7 49 3B 04 24") ||
        !ApiCodeMatches(g_lwZb.exe + 0x16AA990u, "FF FF 7F 7F FF FF 7F 7F FF FF 7F 7F FF FF 7F FF") ||
        !ApiCodeMatches(g_lwZb.exe + 0x953C67u, "F3 43 0F 10 5C 2E 58 41 0F 2E DA 7A 51 75 4F") ||
        !ApiCodeMatches(g_lwZb.exe + 0x2126A0u, "E8 CB 14 74 00"))
    { g_lwZb.refused = 1; Note("living world: absent SpeedTree cells: not this build's code; the game's zero box stays"); return; }
    const CoSwitchSite site = {.rva = LW_ZB_AT, .size = LW_ZB_LEN, .pin = g_lwZbOld, .copy = LW_ZB_LEN, .back = LW_ZB_AT + LW_ZB_LEN,
        .mod = g_lwZbMod, .modLen = sizeof g_lwZbMod, .modBack = LW_ZB_AT + LW_ZB_LEN,
        .fix = {{3, CO_FIX_REL32, g_lwZb.exe + 0x16AA990u}}, .fill = 0xCC, .what = "the absent SpeedTree cells' box (exe 0x21265D)"};
    const char* why = co_switch_install_by(g_lwZb.exe, &site, 1, CO_SWITCH_XL, LwZbWrite);
    if (why != NULL)
    {
        /* foreign bytes refuse for good; a thread inside the range is tried again at the next complex level */
        if (co_switch_check(g_lwZb.exe, &site, 1) != NULL) g_lwZb.refused = 1;
        Note("living world: absent SpeedTree cells: not switched: %s%s", why, g_lwZb.refused ? "" : "; tried again at the next complex level");
        return;
    }
    g_lwZb.ready = 1;
    if (GetEnvironmentVariableA("HELLFORGE_LW_ZEROBOX_COUNT", cnt, sizeof cnt) && cnt[0] == '1' &&
        (!ApiCodeMatches(g_lwZb.exe + 0x953B70u, "48 8B C4 44 89 40 18 48 89 50 10 53 55 56") ||
            LoaderHook("living world: absent SpeedTree cells counter", g_lwZb.exe + 0x953B70u, (void*)LwZbPass, &g_lwZb.passOrig)))
        g_lwZb.passOrig = NULL;
    Note("living world: absent SpeedTree cells: switched site in (the empty box on complex levels, the game's zero box on any other); counter %s",
        g_lwZb.passOrig ? "in" : "off");
}
