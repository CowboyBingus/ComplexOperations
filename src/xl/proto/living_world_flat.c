/* Test instrument (HELLFORGE_LW_PROFILE=2 with the frame notes): the pass times explain 2 of the 7 ms a frame that
 * 100 units cost, and none of the 45-50 ms frames. A sampler stops the game thread about every millisecond anywhere in
 * the frame and counts the function it is in, apart for the frames over 33 ms, so both can be read per function. */
#define LW_FLAT_KEYS 8192
#define LW_FLAT_RING 8192
#define LW_FLAT_STACK 0x2000
#define LW_FLAT_CALLER 0x80000000u    /* key bit: the game.dll function a sample outside game.dll ran under */
#define LW_FLAT_MODULES 31            /* key bits 26-30: the module, 0 game.dll, 1-30 in the order first seen, 31 the rest */
#define LW_FLAT_OFFSET 0x3FFFFFFu
#define LW_FLAT_CHAINS 1024           /* the last quarter is kept for the other threads' chains */
#define LW_FLAT_FENCE 0x106030u       /* helldivers2.exe: the wait for a render fence, to LW_FLAT_FENCE_END */
#define LW_FLAT_FENCE_END 0x106173u
static struct {
    HANDLE thread; int on; volatile uintptr_t base; volatile uint32_t frame;
    volatile float ms[256];                      /* the last frames' lengths, by frame number */
    struct { uint32_t frame, key, caller, chain[4]; } ring[LW_FLAT_RING]; uint32_t head, tail;
    uint32_t key[LW_FLAT_KEYS], count[4][LW_FLAT_KEYS], total[4], dropped;   /* 0 frames up to 33 ms, 1 over; 2, 3 the busiest other thread */
    uint8_t stack[LW_FLAT_STACK];
    uintptr_t module[LW_FLAT_MODULES]; int modules;
    struct { uint32_t fn[4], count[4]; } chain[LW_FLAT_CHAINS]; int chains;
    DWORD gameThread; uint32_t fenceRun;
} g_lwFlat;

/* A module's number for the keys, noted with its file name the first time so the offsets can be read offline. */
static uint32_t LwFlatModule(uintptr_t base)
{
    if (base == g_lw.game) return 0;
    for (int i = 0; i < g_lwFlat.modules; i++) if (g_lwFlat.module[i] == base) return (uint32_t)i + 1;
    if (g_lwFlat.modules >= LW_FLAT_MODULES - 1) return LW_FLAT_MODULES;
    char name[MAX_PATH] = "";
    GetModuleFileNameA((HMODULE)base, name, sizeof name);
    g_lwFlat.module[g_lwFlat.modules++] = base;
    Note("living world: test: frame profile module %d is %s", g_lwFlat.modules, name);
    return (uint32_t)g_lwFlat.modules;
}

/* The function a code address lies in: its module's number over its .pdata start, or over its 4 KiB page where there
 * is no .pdata (the protected sections, system libraries built without it). */
static uint32_t LwFlatFunction(uintptr_t at)
{
    DWORD64 base = 0;
    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(at, &base, NULL);
    if (f != NULL) return LwFlatModule((uintptr_t)base) << 26 | (f->BeginAddress & LW_FLAT_OFFSET);
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) || m == NULL)
        return (uint32_t)LW_FLAT_MODULES << 26;
    return LwFlatModule((uintptr_t)m) << 26 | ((uint32_t)(at - (uintptr_t)m) & LW_FLAT_OFFSET & ~0xFFFu);
}

/* Stopped in the engine or a system call: the nearest game.dll return address on the copied stack names the game
 * code it ran under. A stale slot can name the wrong caller, so these keys are a hint, unlike the stopped address. */
static uint32_t LwFlatCaller(size_t len)
{
    for (size_t i = 0; i + 8 <= len; i += 8)
    {
        uintptr_t v; memcpy(&v, g_lwFlat.stack + i, 8);
        if (v <= g_lw.game + 0x1000u || v - g_lw.game >= 0x8000000u) continue;
        uint32_t fn = LwFlatFunction(v);
        if (fn && fn >> 26 == 0 && LwProfCallBefore((const uint8_t*)v)) return fn | LW_FLAT_CALLER;
    }
    return LW_FLAT_CALLER;
}

/* Stopped in a system library (a wait, mostly): the first four engine or game functions up the copied stack say which
 * wait it is. Slots are taken as return addresses when they point after a call in helldivers2.exe or game.dll; a
 * stale slot can add a wrong one, so a chain is a hint. */
static void LwFlatChain(size_t len, uint32_t* chain, int anyModule)
{
    static uintptr_t exe;
    if (exe == 0) exe = (uintptr_t)GetModuleHandleA(NULL);
    int n = 0;
    memset(chain, 0, 4 * sizeof *chain);
    for (size_t i = 0; i + 8 <= len && n < 4; i += 8)
    {
        uintptr_t v; memcpy(&v, g_lwFlat.stack + i, 8);
        int inGame = v > g_lw.game + 0x1000u && v - g_lw.game < 0x4000000u, inExe = v > exe + 0x1000u && v - exe < 0x4000000u;
        if (!inGame && !inExe && !(anyModule && v > 0x10000u && v < 0x7FFFFFFFFFFFull)) continue;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(v, &base, NULL);
        if (f == NULL || !LwProfCallBefore((const uint8_t*)v)) continue;
        uint32_t fn = LwFlatModule((uintptr_t)base) << 26 | (f->BeginAddress & LW_FLAT_OFFSET);
        if (n == 0 || chain[n - 1] != fn) chain[n++] = fn;
    }
}

static void LwFlatChainCount(const uint32_t* chain, int slow)
{
    if (chain[0] == 0) return;
    for (int i = 0; i < g_lwFlat.chains; i++)
        if (memcmp(g_lwFlat.chain[i].fn, chain, sizeof g_lwFlat.chain[i].fn) == 0) { g_lwFlat.chain[i].count[slow]++; return; }
    if (g_lwFlat.chains >= (slow >= 2 ? LW_FLAT_CHAINS : LW_FLAT_CHAINS * 3 / 4)) return;
    memcpy(g_lwFlat.chain[g_lwFlat.chains].fn, chain, sizeof g_lwFlat.chain[0].fn);
    g_lwFlat.chain[g_lwFlat.chains++].count[slow] = 1;
}

static void LwFlatCount(uint32_t key, int slow)
{
    uint32_t h = (key * 2654435761u) >> 19;
    for (int k = 0; k < LW_FLAT_KEYS; k++, h = (h + 1) & (LW_FLAT_KEYS - 1))
    {
        if (g_lwFlat.key[h] != key && g_lwFlat.key[h] != 0) continue;
        g_lwFlat.key[h] = key; g_lwFlat.count[slow][h]++;
        return;
    }
    g_lwFlat.dropped++;
}

/* A sample is counted once its frame has ended and the game thread has written that frame's length. */
static void LwFlatDrain(void)
{
    uint32_t now = g_lwFlat.frame;
    while (g_lwFlat.tail != g_lwFlat.head && g_lwFlat.ring[g_lwFlat.tail].frame != now)
    {
        uint32_t frame = g_lwFlat.ring[g_lwFlat.tail].frame, key = g_lwFlat.ring[g_lwFlat.tail].key, caller = g_lwFlat.ring[g_lwFlat.tail].caller;
        int slow = g_lwFlat.ms[frame & 255] > 33.0f;
        g_lwFlat.tail = (g_lwFlat.tail + 1) & (LW_FLAT_RING - 1);
        if (now - frame >= 200) { g_lwFlat.dropped++; continue; }
        g_lwFlat.total[slow]++;
        LwFlatCount(key, slow);
        if (caller) LwFlatCount(caller, slow);
        LwFlatChainCount(g_lwFlat.ring[(g_lwFlat.tail - 1) & (LW_FLAT_RING - 1)].chain, slow);
    }
}

/* One sample into the ring, after the game thread runs again: the lookups here may lock. The chain is taken only
 * for a sample in neither the engine's executable nor game.dll, where the stopped address names no game work. */
static int LwFlatRecord(uintptr_t rip, size_t len, uint32_t frame)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    int engine = (rip >= g_lw.game && rip - g_lw.game < 0x4000000u) || (rip >= exe && rip - exe < 0x4000000u);
    uint32_t key = LwFlatFunction(rip);
    g_lwFlat.ring[g_lwFlat.head].frame = frame;
    g_lwFlat.ring[g_lwFlat.head].key = key;
    g_lwFlat.ring[g_lwFlat.head].caller = key >> 26 ? LwFlatCaller(len) : 0;
    if (!engine) LwFlatChain(len, g_lwFlat.ring[g_lwFlat.head].chain, 0);
    else memset(g_lwFlat.ring[g_lwFlat.head].chain, 0, sizeof g_lwFlat.ring[0].chain);
    uint32_t inner = g_lwFlat.ring[g_lwFlat.head].chain[0];
    g_lwFlat.head = (g_lwFlat.head + 1) & (LW_FLAT_RING - 1);
    return !engine && inner >> 26 == LwFlatModule(exe) && (inner & LW_FLAT_OFFSET) >= LW_FLAT_FENCE && (inner & LW_FLAT_OFFSET) < LW_FLAT_FENCE_END;
}

/* The slow frames are the game thread in the engine's render fence wait while one other thread runs at a full core
 * (KB xl-destruction-record-map-full), so that thread is what matters. It is found by processor time over the first
 * long fence waits. After that it is stopped every fourth sample: in fence waits of 8 ms and more (kind 2) and at
 * other times (kind 3). */
#define LW_FLAT_THREADS 160
#define LW_FLAT_FIND 60
static struct { HANDLE thread[LW_FLAT_THREADS]; uintptr_t base[LW_FLAT_THREADS]; DWORD id[LW_FLAT_THREADS]; uint64_t cpu[LW_FLAT_THREADS];
    int count, hot; uint32_t sweeps; } g_lwFlatOthers = {.hot = -1};

/* One stop of a thread: its stopped address and the top of its stack into the copy, nothing else while it is
 * stopped (a lock taken there could be one it holds). 0 when it could not be read. */
static uintptr_t LwFlatSnap(HANDLE thread, uintptr_t base, size_t* len)
{
    CONTEXT c;
    if (SuspendThread(thread) == (DWORD)-1) return 0;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL;
    int ok = GetThreadContext(thread, &c);
    *len = ok && c.Rsp < base ? (base - c.Rsp < LW_FLAT_STACK ? (size_t)(base - c.Rsp) : LW_FLAT_STACK) : 0;
    if (*len) memcpy(g_lwFlat.stack, (const void*)(uintptr_t)c.Rsp, *len);
    ResumeThread(thread);
    return ok ? (uintptr_t)c.Rip : 0;
}

/* ThreadBasicInformation's layout, for a thread's TEB and from it its stack base (the loader includes no winternl.h). */
typedef struct { LONG status; void* teb; void* process; void* thread; ULONG_PTR affinity; LONG priority, basePriority; } LwFlatThreadInfo;
typedef LONG (WINAPI *LwFlatQueryFn)(HANDLE thread, int kind, void* info, ULONG size, ULONG* used);

static void LwFlatOthersList(void)
{
    LwFlatQueryFn query = (LwFlatQueryFn)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 e = {.dwSize = sizeof e};
    if (snap == INVALID_HANDLE_VALUE || query == NULL) return;
    for (BOOL more = Thread32First(snap, &e); more && g_lwFlatOthers.count < LW_FLAT_THREADS; more = Thread32Next(snap, &e))
    {
        if (e.th32OwnerProcessID != GetCurrentProcessId() || e.th32ThreadID == GetCurrentThreadId() || e.th32ThreadID == g_lwFlat.gameThread) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, e.th32ThreadID);
        LwFlatThreadInfo info;
        if (h == NULL) continue;
        if (query(h, 0, &info, sizeof info, NULL) != 0 || info.teb == NULL) { CloseHandle(h); continue; }
        g_lwFlatOthers.base[g_lwFlatOthers.count] = (uintptr_t)((NT_TIB*)info.teb)->StackBase;
        g_lwFlatOthers.id[g_lwFlatOthers.count] = e.th32ThreadID;
        g_lwFlatOthers.thread[g_lwFlatOthers.count++] = h;
    }
    CloseHandle(snap);
}

static uint64_t LwFlatCpu(HANDLE thread)
{
    FILETIME made, ended, kernel, user;
    if (!GetThreadTimes(thread, &made, &ended, &kernel, &user)) return 0;
    return ((uint64_t)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) + ((uint64_t)user.dwHighDateTime << 32 | user.dwLowDateTime);
}

/* The search: the thread that used the most processor time over LW_FLAT_FIND samples of long fence waits. The list
 * is taken once; a thread made later is not in it. */
static void LwFlatFind(void)
{
    if (g_lwFlatOthers.sweeps == 0)
    {
        LwFlatOthersList();
        for (int i = 0; i < g_lwFlatOthers.count; i++) g_lwFlatOthers.cpu[i] = LwFlatCpu(g_lwFlatOthers.thread[i]);
    }
    if (++g_lwFlatOthers.sweeps < LW_FLAT_FIND || g_lwFlatOthers.count == 0) return;
    int hot = 0;
    uint64_t most = 0;
    for (int i = 0; i < g_lwFlatOthers.count; i++)
    {
        uint64_t now = LwFlatCpu(g_lwFlatOthers.thread[i]);   /* 0 once the thread has ended */
        if (now > g_lwFlatOthers.cpu[i] && now - g_lwFlatOthers.cpu[i] > most) { most = now - g_lwFlatOthers.cpu[i]; hot = i; }
    }
    g_lwFlatOthers.hot = hot;
    Note("living world: test: frame profile: the busiest other thread is %lu, %.0f ms of processor time over the search (%d threads)",
        g_lwFlatOthers.id[hot], (double)most / 10000.0, g_lwFlatOthers.count);
}

/* One sample of the busiest thread: its function, and under a system or driver call the first four functions of any
 * module up its stack (the translation layer and the engine's caller). */
static void LwFlatHot(int kind)
{
    uint32_t chain[4];
    size_t len = 0;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t rip = LwFlatSnap(g_lwFlatOthers.thread[g_lwFlatOthers.hot], g_lwFlatOthers.base[g_lwFlatOthers.hot], &len);
    if (rip == 0) return;
    g_lwFlat.total[kind]++;
    LwFlatCount(LwFlatFunction(rip), kind);
    if ((rip >= g_lw.game && rip - g_lw.game < 0x4000000u) || (rip >= exe && rip - exe < 0x4000000u)) return;
    LwFlatChain(len, chain, 1);
    LwFlatChainCount(chain, kind);
}

static void LwFlatOthers(int longWait)
{
    static uint32_t tick;
    if (tick++ % 4 != 0) return;
    if (g_lwFlatOthers.hot >= 0) LwFlatHot(longWait ? 2 : 3);
    else if (longWait) LwFlatFind();
}

static DWORD WINAPI LwFlatThread(LPVOID unused)
{
    for (;;)
    {
        Sleep(1);
        LwFlatDrain();
        if (((g_lwFlat.head + 1) & (LW_FLAT_RING - 1)) == g_lwFlat.tail) continue;
        size_t len = 0;
        uint32_t frame = g_lwFlat.frame;
        uintptr_t rip = LwFlatSnap(g_lwFlat.thread, g_lwFlat.base, &len);
        if (rip == 0) continue;
        g_lwFlat.fenceRun = LwFlatRecord(rip, len, frame) ? g_lwFlat.fenceRun + 1 : 0;
        LwFlatOthers(g_lwFlat.fenceRun >= 8);
    }
    return 0;
}

/* On the game thread at each frame's end, with that frame's length. */
static void LwFlatFrame(double ms)
{
    if (g_lwFlat.on == 0)
    {
        char t[4] = "";
        g_lwFlat.on = GetEnvironmentVariableA("HELLFORGE_LW_PROFILE", t, sizeof t) > 0 && t[0] == '2' ? 1 : -1;
        g_lwFlat.base = (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
        g_lwFlat.gameThread = GetCurrentThreadId();
        if (g_lwFlat.on > 0 && ((g_lwFlat.thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetCurrentThreadId())) == NULL ||
            CreateThread(NULL, 0, LwFlatThread, NULL, 0, NULL) == NULL)) g_lwFlat.on = -1;
        if (g_lwFlat.on > 0) Note("living world: test: frame profile on (game thread %lu)", GetCurrentThreadId());
    }
    if (g_lwFlat.on < 0) return;
    g_lwFlat.ms[g_lwFlat.frame & 255] = (float)ms;
    g_lwFlat.frame++;
}

static const char* const g_lwFlatKind[4] = {"frames up to 33 ms", "frames over 33 ms", "the busiest other thread in fence waits of 8 ms and more",
    "the busiest other thread at other times"};

/* The functions with the most samples of one kind, as module:offset with their share of that kind's samples; with
 * `callers`, the game.dll functions the samples outside game.dll ran under (0 when the stack copy named none). */
static void LwFlatTop(int slow, int callers)
{
    static uint8_t used[LW_FLAT_KEYS];
    char line[1000]; size_t at = 0;
    memset(used, 0, sizeof used);
    for (int n = 0; n < (slow >= 2 ? 24 : 16) && g_lwFlat.total[slow] && at < sizeof line - 28; n++)
    {
        int best = -1;
        for (int k = 0; k < LW_FLAT_KEYS; k++)
            if (!used[k] && g_lwFlat.count[slow][k] && (g_lwFlat.key[k] >> 31) == (uint32_t)callers &&
                (best < 0 || g_lwFlat.count[slow][k] > g_lwFlat.count[slow][best])) best = k;
        if (best < 0) break;
        used[best] = 1;
        at += (size_t)snprintf(line + at, sizeof line - at, " %u:%X %.1f%%", g_lwFlat.key[best] >> 26 & 31u,
            g_lwFlat.key[best] & LW_FLAT_OFFSET, 100.0 * g_lwFlat.count[slow][best] / g_lwFlat.total[slow]);
    }
    line[at < sizeof line ? at : sizeof line - 1] = 0;
    Note("living world: frame profile, %s, %s, %u samples so far (%u dropped):%s", g_lwFlatKind[slow],
        callers ? "game callers of the rest" : "functions", g_lwFlat.total[slow], g_lwFlat.dropped, line);
}

/* The six commonest chains under the system-library samples of one kind, innermost function first. */
static void LwFlatChains(int slow)
{
    static uint8_t used[LW_FLAT_CHAINS];
    char line[1000]; size_t at = 0;
    memset(used, 0, sizeof used);
    for (int n = 0; n < (slow >= 2 ? 10 : 6) && g_lwFlat.total[slow] && at < sizeof line - 80; n++)
    {
        int best = -1;
        for (int k = 0; k < g_lwFlat.chains; k++)
            if (!used[k] && g_lwFlat.chain[k].count[slow] && (best < 0 || g_lwFlat.chain[k].count[slow] > g_lwFlat.chain[best].count[slow])) best = k;
        if (best < 0) break;
        used[best] = 1;
        const uint32_t* fn = g_lwFlat.chain[best].fn;
        at += (size_t)snprintf(line + at, sizeof line - at, " [%u:%X %u:%X %u:%X %u:%X] %.1f%%", fn[0] >> 26, fn[0] & LW_FLAT_OFFSET, fn[1] >> 26,
            fn[1] & LW_FLAT_OFFSET, fn[2] >> 26, fn[2] & LW_FLAT_OFFSET, fn[3] >> 26, fn[3] & LW_FLAT_OFFSET, 100.0 * g_lwFlat.chain[best].count[slow] / g_lwFlat.total[slow]);
    }
    line[at < sizeof line ? at : sizeof line - 1] = 0;
    Note("living world: frame profile, %s, chains under system calls (%d kinds):%s", g_lwFlatKind[slow], g_lwFlat.chains, line);
}

/* With the 30 s notes. The counts run on for the whole level: the slow frames are few, so they need the time. */
static void LwFlatNote(void)
{
    if (g_lwFlat.on <= 0) return;
    for (int kind = 0; kind < 4; kind++) LwFlatTop(kind >> 1, kind & 1);
    LwFlatTop(2, 0);
    LwFlatTop(3, 0);
    for (int slow = 0; slow < 4; slow++) LwFlatChains(slow);
}
