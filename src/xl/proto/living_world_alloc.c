/* Test instrument (HELLFORGE_LW_TEST_ALLOC=1 with the frame notes): the slow frame state starts in the 30 s in which
 * the game commits 1.5-2.8 GB more (KB xl-destruction-record-map-full). Every commit of 64 KiB and more is counted by
 * the first engine or game functions on its stack, and the 30 s note names the largest. */
#define LW_ALLOC_KEYS 1024
#define LW_ALLOC_DEPTH 5
typedef LONG (WINAPI *LwAllocFn)(HANDLE process, PVOID* base, ULONG_PTR zeroBits, SIZE_T* size, ULONG type, ULONG protect);
static struct {
    void* orig; SRWLOCK lock; uintptr_t exe;
    struct { uint32_t fn[LW_ALLOC_DEPTH], count; uint64_t bytes; } key[LW_ALLOC_KEYS];
    uint64_t bytes; uint32_t lost;
} g_lwAlloc;
/* A system TLS slot, not a compiler one: the compiler's allocates on a thread's first use, inside this very hook. */
static DWORD g_lwAllocSlot = TLS_OUT_OF_INDEXES;

/* The heap's own functions and the tracking allocate (0x5C2E10) say nothing of who asked. */
static int LwAllocInner(uintptr_t rva)
{
    return (rva >= 0x667000u && rva < 0x66A700u) || (rva >= 0x630060u && rva < 0x63010Fu) || (rva >= 0x5C2E10u && rva < 0x5C2ECEu);
}

/* The first LW_ALLOC_DEPTH different functions of helldivers2.exe or game.dll among the callers, innermost first. */
static void LwAllocCallers(uint32_t* fn)
{
    void* frames[32];
    USHORT n = RtlCaptureStackBackTrace(2, 32, frames, NULL);
    int found = 0;
    memset(fn, 0, LW_ALLOC_DEPTH * sizeof *fn);
    for (USHORT i = 0; i < n && found < LW_ALLOC_DEPTH; i++)
    {
        uintptr_t at = (uintptr_t)frames[i];
        if (!(at >= g_lw.game && at - g_lw.game < 0x4000000u) && !(at >= g_lwAlloc.exe && at - g_lwAlloc.exe < 0x4000000u)) continue;
        if (LwAllocInner(at - g_lwAlloc.exe)) continue;
        uint32_t f = LwFlatFunction(at);
        if (found == 0 || fn[found - 1] != f) fn[found++] = f;
    }
}

static void LwAllocCount(const uint32_t* fn, uint64_t bytes)
{
    uint32_t h = 0;
    for (int d = 0; d < LW_ALLOC_DEPTH; d++) h = (h ^ fn[d]) * 2654435761u;
    h >>= 22;
    AcquireSRWLockExclusive(&g_lwAlloc.lock);
    g_lwAlloc.bytes += bytes;
    int k = 0;
    for (; k < LW_ALLOC_KEYS; k++, h = (h + 1) & (LW_ALLOC_KEYS - 1))
    {
        if (g_lwAlloc.key[h].count && memcmp(g_lwAlloc.key[h].fn, fn, sizeof g_lwAlloc.key[h].fn) != 0) continue;
        memcpy(g_lwAlloc.key[h].fn, fn, sizeof g_lwAlloc.key[h].fn);
        g_lwAlloc.key[h].count++; g_lwAlloc.key[h].bytes += bytes;
        break;
    }
    if (k == LW_ALLOC_KEYS) g_lwAlloc.lost++;
    ReleaseSRWLockExclusive(&g_lwAlloc.lock);
}

/* The guard keeps the hook's own work (the stack walk, a first module's note) from counting its allocations. */
static LONG WINAPI LwAllocDetour(HANDLE process, PVOID* base, ULONG_PTR zeroBits, SIZE_T* size, ULONG type, ULONG protect)
{
    LONG status = ((LwAllocFn)g_lwAlloc.orig)(process, base, zeroBits, size, type, protect);
    if (status < 0 || !(type & MEM_COMMIT) || size == NULL || *size < 0x10000u || process != GetCurrentProcess() || TlsGetValue(g_lwAllocSlot) != NULL) return status;
    TlsSetValue(g_lwAllocSlot, (LPVOID)1);
    uint32_t fn[LW_ALLOC_DEPTH];
    LwAllocCallers(fn);
    LwAllocCount(fn, *size);
    TlsSetValue(g_lwAllocSlot, NULL);
    return status;
}

static size_t LwAllocPrint(char* out, size_t room, int k)
{
    size_t at = (size_t)snprintf(out, room, " [");
    for (int d = 0; d < LW_ALLOC_DEPTH && at < room; d++)
        at += (size_t)snprintf(out + at, room - at, "%s%u:%X", d ? " " : "", g_lwAlloc.key[k].fn[d] >> 26, g_lwAlloc.key[k].fn[d] & LW_FLAT_OFFSET);
    if (at < room) at += (size_t)snprintf(out + at, room - at, "] %.0f MB in %u", (double)g_lwAlloc.key[k].bytes / 1048576.0, g_lwAlloc.key[k].count);
    return at < room ? at : room - 1;
}

/* The engine's named tracking allocators form a list ([exe+0x1B13180]: head +0, lock +8; constructor 0x5C2C40): next
 * +0x10, live bytes +0x30, name +0x78. The heap growth at the slow state's start comes through their allocate
 * (0x5C2E10), so the 30 s note names the ones whose bytes changed most. Read without the list's lock, by safe reads. */
#define LW_POOL_LIST 0x1B13180u
#define LW_POOLS 4096
static struct { uintptr_t at; int64_t bytes, grew; } g_lwPool[LW_POOLS];

static void LwPoolSeen(uintptr_t at, int64_t bytes)
{
    int k = 0, free = -1;
    for (; k < LW_POOLS && g_lwPool[k].at != at; k++) if (g_lwPool[k].at == 0 && free < 0) free = k;
    if (k == LW_POOLS) { if (free < 0) return; k = free; g_lwPool[k].at = at; g_lwPool[k].bytes = 0; }
    g_lwPool[k].grew = bytes - g_lwPool[k].bytes;
    g_lwPool[k].bytes = bytes;
}

static size_t LwPoolPrint(char* out, size_t room, int k)
{
    char name[40] = "?"; uintptr_t text;
    if (EvpPtr(g_lwPool[k].at + 0x78u, &text) && SafeRead(text, name, sizeof name - 1) == 0) name[sizeof name - 1] = 0; else strcpy(name, "?");
    int n = snprintf(out, room, " %s %.0f MB (%+.0f)", name, (double)g_lwPool[k].bytes / 1048576.0, (double)g_lwPool[k].grew / 1048576.0);
    return n > 0 && (size_t)n < room ? (size_t)n : 0;
}

static int LwPoolWalk(uintptr_t at)
{
    int count = 0;
    for (; count < 8192; count++)
    {
        int64_t bytes;
        if (SafeRead(at + 0x30u, &bytes, 8)) break;
        LwPoolSeen(at, bytes);
        if (!EvpPtr(at + 0x10u, &at)) break;
    }
    return count;
}

static int LwPoolMost(void)
{
    int best = -1;
    for (int k = 0; k < LW_POOLS; k++)
        if (llabs(g_lwPool[k].grew) >= (8 << 20) && (best < 0 || llabs(g_lwPool[k].grew) > llabs(g_lwPool[best].grew))) best = k;
    return best;
}

static void LwPoolNote(void)
{
    uintptr_t list, at; char line[700]; size_t used = 0;
    if (!EvpPtr(g_lwAlloc.exe + LW_POOL_LIST, &list) || !EvpPtr(list, &at)) return;
    for (int k = 0; k < LW_POOLS; k++) g_lwPool[k].grew = 0;
    int count = LwPoolWalk(at), best;
    for (int n = 0; n < 8 && used < sizeof line - 80 && (best = LwPoolMost()) >= 0; n++)
    {
        used += LwPoolPrint(line + used, sizeof line - used, best);
        g_lwPool[best].grew = 0;
    }
    line[used] = 0;
    Note("living world: test: pools (30 s, %d named): changed by 8 MB or more:%s", count, used ? line : " none");
}

/* SpeedTree's per-view cull (job 0x210E10 -> 0x212150 -> 0x9548C0) fills the view's rectangle of cells with 0xE0-byte
 * nodes in a vector (owner +0x80: data +8, count +0x10, room +0x18) that 0x218860 grows to exactly the count asked.
 * The slow state starts when the rectangle jumps from ~19,600 to 120,000-325,000 cells a view: the limits the
 * rectangle is cut to (0x954D70 from the forest's extents: +0x1B4/+0x1B8 one axis, +0x1BC/+0x1C0 the other) then read
 * 0, 0 on one axis, and 0x9548C0 skips the cut unless both axes have a range. This counts the growths by vector
 * (HELLFORGE_LW_TEST_TREEGRID=1); the fix is living_world_trees.c. */
#define LW_GRID_RESERVE 0x218860u
#define LW_GRID_VECTORS 64
typedef uint8_t (*LwGridFn)(uintptr_t vec, uint64_t count);
static struct {
    void* orig; SRWLOCK lock; uint32_t calls, grew, lost; uint64_t cells;
    struct { uintptr_t vec; uint32_t grew; uint64_t count, room; int32_t rect[4]; float cell; } v[LW_GRID_VECTORS];
} g_lwGrid;

static void LwGridCount(uintptr_t vec, uint64_t count, uint64_t room, int fromCull)
{
    AcquireSRWLockExclusive(&g_lwGrid.lock);
    g_lwGrid.grew++; g_lwGrid.cells += count;
    int k = 0;
    while (k < LW_GRID_VECTORS && g_lwGrid.v[k].vec != vec && g_lwGrid.v[k].vec != 0) k++;
    if (k == LW_GRID_VECTORS) g_lwGrid.lost++;
    else
    {
        g_lwGrid.v[k].vec = vec; g_lwGrid.v[k].grew++; g_lwGrid.v[k].count = count; g_lwGrid.v[k].room = room;
        if (fromCull)   /* the cull's owner: cell rectangle +0x1A4 (x0, y0, x1, y1), cell size +0x160 */
        { memcpy(g_lwGrid.v[k].rect, (const void*)(vec - 0x80u + 0x1A4u), 16); g_lwGrid.v[k].cell = *(const float*)(vec - 0x80u + 0x160u); }
    }
    ReleaseSRWLockExclusive(&g_lwGrid.lock);
}

static uint8_t LwGridDetour(uintptr_t vec, uint64_t count)
{
    uint64_t room = *(const uint64_t*)(vec + 0x18u);
    InterlockedIncrement((volatile LONG*)&g_lwGrid.calls);
    if (*(const uint8_t*)(vec + 0x28u) == 0 && count > room)
    {
        LwGridCount(vec, count, room, (uintptr_t)__builtin_return_address(0) == g_lwAlloc.exe + 0x954CD7u);
    }
    return ((LwGridFn)g_lwGrid.orig)(vec, count);
}

/* The cut is looked at where it is used: whatever wrote the limits, a cull that finds an axis without a range is
 * counted (none are expected with living_world_trees.c on). */
typedef uint8_t (*LwCullFn)(uintptr_t owner, uintptr_t view, uint32_t a3, float a4);
static struct { void* orig; volatile LONG calls, empty; int32_t emptyNow[4]; } g_lwCull;
static uint8_t LwCullDetour(uintptr_t owner, uintptr_t view, uint32_t a3, float a4)
{
    const int32_t* limit = (const int32_t*)(owner + 0x1B4u);
    InterlockedIncrement(&g_lwCull.calls);
    for (int axis = 0; axis < 2; axis++)
    {
        const int32_t* pair = limit + 2 * axis;
        if (pair[0] >= pair[1] && InterlockedIncrement(&g_lwCull.empty) == 1) memcpy(g_lwCull.emptyNow, limit, 16);
    }
    return ((LwCullFn)g_lwCull.orig)(owner, view, a3, a4);
}

static void LwCullNote(void)
{
    LONG empty = InterlockedExchange(&g_lwCull.empty, 0);
    Note("living world: test: tree culls (30 s): %ld, %ld axis ranges empty at a cull; first empty: %d..%d and %d..%d",
        InterlockedExchange(&g_lwCull.calls, 0), empty,
        g_lwCull.emptyNow[0], g_lwCull.emptyNow[1], g_lwCull.emptyNow[2], g_lwCull.emptyNow[3]);
}

static void LwGridNote(void)
{
    if (g_lwGrid.orig == NULL) return;
    if (g_lwCull.orig) LwCullNote();
    char line[700]; size_t used = 0; int vectors = 0;
    AcquireSRWLockExclusive(&g_lwGrid.lock);
    for (int k = 0; k < LW_GRID_VECTORS && g_lwGrid.v[k].vec; k++, vectors++)
        if (used < sizeof line - 110 && g_lwGrid.v[k].count * 0xE0u >= (1u << 20))
            used += (size_t)snprintf(line + used, sizeof line - used, " [%llX: %u times, last %llu cells over room %llu, x %d..%d y %d..%d, cell %.0f m]",
                (unsigned long long)g_lwGrid.v[k].vec, g_lwGrid.v[k].grew, (unsigned long long)g_lwGrid.v[k].count, (unsigned long long)g_lwGrid.v[k].room,
                g_lwGrid.v[k].rect[0], g_lwGrid.v[k].rect[2], g_lwGrid.v[k].rect[1], g_lwGrid.v[k].rect[3], (double)g_lwGrid.v[k].cell);
    line[used] = 0;
    Note("living world: test: tree grid (30 s): %u reserve calls, %u grew (%.0f MB of cells), %d vectors (%u more not kept); of 1 MB and more:%s",
        g_lwGrid.calls, g_lwGrid.grew, (double)g_lwGrid.cells * 224.0 / 1048576.0, vectors, g_lwGrid.lost, used ? line : " none");
    g_lwGrid.calls = g_lwGrid.grew = g_lwGrid.lost = 0; g_lwGrid.cells = 0;
    memset(g_lwGrid.v, 0, sizeof g_lwGrid.v);
    ReleaseSRWLockExclusive(&g_lwGrid.lock);
}

static void LwInstallGrid(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_TEST_TREEGRID", t, sizeof t) == 0 || t[0] != '1') return;
    if (!ApiCodeMatches(g_lwAlloc.exe + LW_GRID_RESERVE, "40 55 41 55 48 83 EC 38 80 79 28 00 4C 8B EA 48 8B 41 18") ||
        !ApiCodeMatches(g_lwAlloc.exe + 0x954CD2u, "E8 89 3B 8C FF") ||
        LoaderHook("living world: tree grid growth (test)", g_lwAlloc.exe + LW_GRID_RESERVE, (void*)LwGridDetour, &g_lwGrid.orig) || g_lwGrid.orig == NULL)
    { g_lwGrid.orig = NULL; Note("living world: test: the tree grid reserve is not the reviewed code or could not be hooked"); return; }
    if (!ApiCodeMatches(g_lwAlloc.exe + 0x9548C0u, "48 8B C4 55 53 57 48 8D 68 A1 48 81 EC D0 00 00 00") ||
        LoaderHook("living world: tree cull limits (test)", g_lwAlloc.exe + 0x9548C0u, (void*)LwCullDetour, &g_lwCull.orig)) g_lwCull.orig = NULL;
    Note("living world: test: tree grid growths and culls without limits are counted");
}

/* With the 30 s notes: the commits since the last one, largest callers first; the counts start again each time. */
static void LwAllocNote(void)
{
    LwGridNote();
    if (g_lwAlloc.orig == NULL) return;
    char line[900]; size_t at = 0;
    TlsSetValue(g_lwAllocSlot, (LPVOID)1);
    AcquireSRWLockExclusive(&g_lwAlloc.lock);
    uint64_t total = g_lwAlloc.bytes;
    for (int n = 0; n < 7 && at < sizeof line - 110; n++)
    {
        int best = -1;
        for (int k = 0; k < LW_ALLOC_KEYS; k++)
            if (g_lwAlloc.key[k].count && g_lwAlloc.key[k].bytes && (best < 0 || g_lwAlloc.key[k].bytes > g_lwAlloc.key[best].bytes)) best = k;
        if (best < 0) break;
        at += LwAllocPrint(line + at, sizeof line - at, best);
        g_lwAlloc.key[best].bytes = 0;
    }
    uint32_t lost = g_lwAlloc.lost;
    memset(g_lwAlloc.key, 0, sizeof g_lwAlloc.key);
    g_lwAlloc.bytes = 0; g_lwAlloc.lost = 0;
    ReleaseSRWLockExclusive(&g_lwAlloc.lock);
    line[at < sizeof line ? at : sizeof line - 1] = 0;
    if (total >= (64u << 20)) Note("living world: test: commits (30 s): %.0f MB (%u not counted by caller):%s", (double)total / 1048576.0, lost, line);
    LwPoolNote();
    TlsSetValue(g_lwAllocSlot, NULL);
}

static void LwInstallAlloc(void)
{
    static int done;
    char t[4] = "";
    if (done++) return;
    g_lwAlloc.exe = (uintptr_t)GetModuleHandleA(NULL);
    LwInstallGrid();
    if (GetEnvironmentVariableA("HELLFORGE_LW_TEST_ALLOC", t, sizeof t) == 0 || t[0] != '1') return;
    void* target = (void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAllocateVirtualMemory");
    if ((g_lwAllocSlot = TlsAlloc()) == TLS_OUT_OF_INDEXES) return;
    if (target == NULL || LoaderHook("living world: commits by caller (test)", (uintptr_t)target, (void*)LwAllocDetour, &g_lwAlloc.orig) || g_lwAlloc.orig == NULL)
    { g_lwAlloc.orig = NULL; Note("living world: test: the commit counter could not be hooked"); return; }
    Note("living world: test: commits of 64 KiB and more are counted by caller");
}
