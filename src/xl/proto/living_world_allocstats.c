/* The engine's tracking allocator keeps four statistics on one shared cache line: allocate 0x5C2E10 does two lock xadd
 * (count +0x28, bytes +0x30) and two CAS-max loops (peaks +0x38, +0x40), free 0x5C2ED0 two lock xadd. Every allocator
 * object runs this one copy (vtables 0x168B5A0 and 0x1671F10), and only statistics, telemetry and the profiler read the
 * fields (KB frame-cost-swarm-cap2). Each patch is a 2-byte short jump over the atomics, stored with one locked 16-bit
 * exchange so a core fetches the old or the new instruction, never a mix. The free side goes first and comes back last,
 * so a counter can only stay high, never reach a false zero. HELLFORGE_LW_ALLOC_STATS / command "allocstats N": 0 the
 * game's code, 1 no peaks, 2 no counters and no peaks. */
typedef struct { uint32_t rva; uint8_t was[2], now[2]; } LwAsSite;
static const LwAsSite g_lwAsFree[] = { {0x5C2EEAu, {0x48, 0xC7}, {0xEB, 0x0B}}, {0x5C2F12u, {0x48, 0xF7}, {0xEB, 0x07}} };
static const LwAsSite g_lwAsAll = {0x5C2E4Bu, {0x41, 0xB8}, {0xEB, 0x6B}};   /* both xadds and both peak loops */
static const LwAsSite g_lwAsPeaks = {0x5C2E69u, {0x48, 0x8B}, {0xEB, 0x4D}}; /* the peak loops only */
static struct { int ready, mode; uintptr_t exe; } g_lwAs;

/* One site to its old or new bytes; 0 when it holds them afterwards. */
static int LwAsWrite(const LwAsSite* s, int on)
{
    volatile SHORT* at = (volatile SHORT*)(g_lwAs.exe + s->rva);
    const uint8_t* want = on ? s->now : s->was;
    const uint8_t* other = on ? s->was : s->now;
    uint8_t cur[2];
    if (SafeRead((uintptr_t)at, cur, 2)) return 1;
    if (!memcmp(cur, want, 2)) return 0;
    if (memcmp(cur, other, 2)) return 1;   /* someone else's bytes: left alone */
    DWORD prev;
    if (!VirtualProtect((void*)at, 2, PAGE_EXECUTE_READWRITE, &prev)) return 1;
    __atomic_exchange_n((volatile uint16_t*)at, (uint16_t)(want[0] | (want[1] << 8)), __ATOMIC_SEQ_CST); /* xchg: locked */
    VirtualProtect((void*)at, 2, prev, &prev);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 2);
    NoteRewrite((uintptr_t)at, 2);
    RecordPatch((uintptr_t)at, want, 2);
    return 0;
}

static void LwAsSet(int mode)
{
    int bad = 0;
    /* back to the game's code: the allocate side first, then the frees */
    bad += LwAsWrite(&g_lwAsAll, 0); bad += LwAsWrite(&g_lwAsPeaks, 0);
    for (int i = 0; i < 2; i++) bad += LwAsWrite(&g_lwAsFree[i], 0);
    if (!bad && mode == 1) bad += LwAsWrite(&g_lwAsPeaks, 1);
    if (!bad && mode == 2) { for (int i = 0; i < 2; i++) bad += LwAsWrite(&g_lwAsFree[i], 1); if (!bad) bad += LwAsWrite(&g_lwAsAll, 1); }
    g_lwAs.mode = bad ? -1 : mode;
    Note("living world: allocator statistics %s%s", mode == 2 ? "off (counters and peaks)" : mode == 1 ? "off (peaks)" : "as the game has them",
        bad ? "; a site held other bytes, so the rest was left as the game has it" : "");
}

static void LwInstallAllocStats(void)
{
    static int done; char t[4] = "";
    if (done++) return;
    g_lwAs.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (strcmp(g_build, "25480438") ||
        !ApiCodeMatches(g_lwAs.exe + 0x5C2E10u, "48 89 5C 24 08 57 48 83 EC 30 48 8B FA 48 8B D9 4D 85 C0 75 17") ||
        !ApiCodeMatches(g_lwAs.exe + 0x5C2E3Cu, "48 8B 49 20 48 8D 54 24 20 48 8B 01 FF 50 30 41 B8 01 00 00 00 F0 4C 0F C1 43 28 48 8B 4C 24 28 49 83 C0 02 4C 8B C9 F0 4C 0F C1 4B 30 48 8B 43 40") ||
        !ApiCodeMatches(g_lwAs.exe + 0x5C2EB8u, "0F 10 44 24 20 48 8B 5C 24 40 48 8B C7 0F 11 07 48 83 C4 30 5F C3") ||
        !ApiCodeMatches(g_lwAs.exe + 0x5C2ED0u, "48 85 D2 74 55 48 89 74 24 10 57 48 83 EC 20 48 8B F2 48 89 5C 24 30 48 8B F9 48 C7 C0 FF FF FF FF F0 48 0F C1 41 28 4C 8B 01 41 FF 50 08 48 8B 4F 20 48 8B D6 4C 8B C0 48 8B D8 4C 8B 09 41 FF 51 40 48 F7 DB F0 48 0F C1 5F 30 48 8B 5C 24 30") ||
        *(uintptr_t*)(g_lwAs.exe + 0x168B5D0u) != g_lwAs.exe + 0x5C2E10u || *(uintptr_t*)(g_lwAs.exe + 0x168B5E0u) != g_lwAs.exe + 0x5C2ED0u)
    { Note("living world: allocator statistics: not the reviewed code; not changed"); return; }
    g_lwAs.ready = 1;
    int mode = GetEnvironmentVariableA("HELLFORGE_LW_ALLOC_STATS", t, sizeof t) > 0 ? t[0] - '0' : 0;
    if (mode < 0 || mode > 2) mode = 0;
    char x[4] = "", y[4] = ""; /* research instruments that read the byte counter */
    if (mode && ((GetEnvironmentVariableA("HELLFORGE_XL_ALLOCS", x, sizeof x) > 0 && x[0] == '1') || MemProbeOn() ||
                 (GetEnvironmentVariableA("HELLFORGE_LW_TEST_ALLOC", y, sizeof y) > 0 && y[0] == '1')))
    { Note("living world: allocator statistics kept: an allocation instrument reads them"); return; }
    if (mode) LwAsSet(mode);
}

static void LwAllocStatsCmd(int mode)
{
    if (!g_lwAs.ready || mode < 0 || mode > 2) { Note("living world: test: allocstats: not the reviewed code, or a value outside 0..2"); return; }
    LwAsSet(mode);
}
