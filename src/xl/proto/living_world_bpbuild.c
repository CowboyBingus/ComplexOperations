/* Test instrument (HELLFORGE_LW_BPBUILD=1): which broad phase layers the physics step rebuilds whole, and what marked
 * them. The step's build starter 0xD3D780 rebuilds a layer when ([L+0x1AE0] & mask r9b) && [L+0x50] && ([L+0x1AEC] ||
 * [L+0x1AE8]) (0xD3DAA9); add 0xD3CFD0, remove 0xD41AF0 and update 0xD40550 set +0x1AEC. Each build of layer 1, 2 or 4
 * is noted with its QPC time (the frame recorder's clock), its leaf count and the calls that marked it. Read only. */
typedef uintptr_t (*LwBbFn)(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f);
enum { LW_BB_ADD = 1, LW_BB_REMOVE = 2, LW_BB_UPDATE = 4, LW_BB_LAYERS = 8 };
static struct
{
    void *step, *add, *remove, *update;
    volatile LONG src[LW_BB_LAYERS], builds[LW_BB_LAYERS], noted;
    ULONGLONG notedAt; double qpcMs;
} g_lwBb;

/* The layers of a broad phase, at most LW_BB_LAYERS; 0 when it is not readable. */
static int LwBbLayers(uintptr_t bp, uintptr_t* out)
{
    uintptr_t arr = 0; int n = 0;
    if (!bp || SafeRead(bp + 0x10, &arr, 8) || !arr || SafeRead(bp + 0x18, &n, 4) || n <= 0) return 0;
    if (n > LW_BB_LAYERS) n = LW_BB_LAYERS;
    for (int i = 0; i < n; i++) if (SafeRead(arr + 8ull * (unsigned)i, &out[i], 8) || !out[i]) return 0;
    return n;
}

/* Around a call that may mark layers: the layers it marked get its bit. */
static uintptr_t LwBbMarking(void* orig, int bit, const uintptr_t* x)
{
    uintptr_t lay[LW_BB_LAYERS]; uint8_t was[LW_BB_LAYERS]; int n = LwBbLayers(x[0], lay);
    for (int i = 0; i < n; i++) was[i] = *(volatile uint8_t*)(lay[i] + 0x1AEC);
    uintptr_t r = ((LwBbFn)orig)(x[0], x[1], x[2], x[3], x[4], x[5]);
    for (int i = 0; i < n; i++) if (!was[i] && *(volatile uint8_t*)(lay[i] + 0x1AEC)) InterlockedOr(&g_lwBb.src[i], bit);
    return r;
}
static uintptr_t LwBbAdd(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwBbMarking(g_lwBb.add, LW_BB_ADD, x); }
static uintptr_t LwBbRemove(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwBbMarking(g_lwBb.remove, LW_BB_REMOVE, x); }
static uintptr_t LwBbUpdate(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f) { uintptr_t x[6] = {a, b, c, d, e, f}; return LwBbMarking(g_lwBb.update, LW_BB_UPDATE, x); }

/* One layer the step rebuilds: counted, and noted for layers 1, 2 and 4. */
static void LwBbBuilt(int i, const uint8_t* L)
{
    InterlockedIncrement(&g_lwBb.builds[i]);
    LONG src = InterlockedExchange(&g_lwBb.src[i], 0);
    if (!(i == 1 || i == 2 || i == 4) || InterlockedIncrement(&g_lwBb.noted) > 200) return;
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    Note("living world: test: bp build layer %d at %.4f s: %u leaves, marked by%s%s%s%s", i, (double)t.QuadPart * g_lwBb.qpcMs / 1000.0,
        *(const uint32_t*)(L + 0x50), src & LW_BB_ADD ? " add" : "", src & LW_BB_REMOVE ? " remove" : "", src & LW_BB_UPDATE ? " update" : "",
        src ? "" : L[0x1AE8] ? " (always)" : " (none seen)");
}

/* The step's builds: the same test as 0xD3DAA9, before the original runs it. */
static uintptr_t LwBbStep(uintptr_t bp, uintptr_t b, uintptr_t c, uintptr_t mask, uintptr_t e, uintptr_t f)
{
    uintptr_t lay[LW_BB_LAYERS]; int n = LwBbLayers(bp, lay);
    for (int i = 0; i < n; i++)
    {
        const uint8_t* L = (const uint8_t*)lay[i];
        if ((L[0x1AE0] & (uint8_t)mask) && *(const uint32_t*)(L + 0x50) != 0 && (L[0x1AEC] || L[0x1AE8])) LwBbBuilt(i, L);
    }
    return ((LwBbFn)g_lwBb.step)(bp, b, c, mask, e, f);
}

/* Game thread, once a frame: every 30 s the builds per layer. */
static void LwBbFrame(void)
{
    ULONGLONG now = GetTickCount64();
    if (!g_lwBb.step || now - g_lwBb.notedAt < 30000) return;
    g_lwBb.notedAt = now;
    Note("living world: test: bp builds in 30 s by layer: %ld %ld %ld %ld %ld %ld (%ld big-layer notes)", (long)g_lwBb.builds[0], (long)g_lwBb.builds[1],
        (long)g_lwBb.builds[2], (long)g_lwBb.builds[3], (long)g_lwBb.builds[4], (long)g_lwBb.builds[5], (long)g_lwBb.noted);
    for (int i = 0; i < LW_BB_LAYERS; i++) g_lwBb.builds[i] = 0;
    g_lwBb.noted = 0;
}

static void LwInstallBpBuild(void)
{
    static int done; char t[4] = ""; LARGE_INTEGER hz;
    if (done++ || GetEnvironmentVariableA("HELLFORGE_LW_BPBUILD", t, sizeof t) == 0 || t[0] != '1') return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    QueryPerformanceFrequency(&hz); g_lwBb.qpcMs = 1000.0 / (double)hz.QuadPart; g_lwBb.notedAt = GetTickCount64();
    if (!ApiCodeMatches(exe + 0xD3D780u, "44 88 4C 24 20 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F1 48 81") ||
        !ApiCodeMatches(exe + 0xD3DAA9u, "0F B6 86 E0 1A 00 00 22 45 6F 0F 84 EB 01 00 00 83 7E 50 00 0F 84 E1 01 00 00 80 BE EC 1A 00 00 00 75 0D 80 BE E8 1A 00 00 00") ||
        !ApiCodeMatches(exe + 0xD3CFD0u, "48 8B C4 44 89 48 20 48 89 50 10 55 48 8D 68 A1 48 81 EC E0 00 00 00") ||
        !ApiCodeMatches(exe + 0xD41AF0u, "48 89 54 24 10 53 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 8B F1") ||
        !ApiCodeMatches(exe + 0xD40550u, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70") ||
        /* the trampolines are stored before each hook goes live; the frame note waits for the step's */
        LoaderHook("bp builds (test): add", exe + 0xD3CFD0u, (void*)LwBbAdd, &g_lwBb.add) || !g_lwBb.add ||
        LoaderHook("bp builds (test): remove", exe + 0xD41AF0u, (void*)LwBbRemove, &g_lwBb.remove) || !g_lwBb.remove ||
        LoaderHook("bp builds (test): update", exe + 0xD40550u, (void*)LwBbUpdate, &g_lwBb.update) || !g_lwBb.update ||
        LoaderHook("bp builds (test): step", exe + 0xD3D780u, (void*)LwBbStep, &g_lwBb.step) || !g_lwBb.step)
    { Note("living world: test: bp builds: this build's code is not what it expects; off"); return; }
    Note("living world: test: bp builds counted (layers 1, 2 and 4 noted)");
}
