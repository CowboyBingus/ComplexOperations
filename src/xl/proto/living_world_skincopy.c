/* Lever A: the skin palette upload (5.9 to 7.4 MB a frame, 1.2 ms on the render thread: memcpy 0x1267850 called at
 * 0x6EE008) copied by helper threads. memcpy itself is not hooked: its second instruction is rip-relative and it has
 * thousands of callers. The one call goes through a 17-byte stub in int3 padding (0xA34030: mov r9,rbx; jmp), so the
 * copy also gets the record payload (rbx) that tells the skin upload from the other records. The call's rel32 changes
 * in one aligned qword while every other thread is stopped. Helpers copy 32-byte aligned chunks with SSE2 non-temporal
 * stores, the game's own store type for large copies (0x1267B50, sfence at 0x1267C7A); the render thread returns only
 * when no helper runs, so every chunk is written and fenced before the record is released (0x25A312).
 * HELLFORGE_LW_SKINCOPY=N / "skincopy N": 0 the game's call, 1 redirected and timed on the render thread alone, 2..5
 * with 1..4 helpers; "skincopy ab N S" flips 1 and N every S s; "skincopy chunk K" sets the chunk in KB. */
#include <emmintrin.h>
#define LW_SKC_CALL 0x6EE008u
#define LW_SKC_CAVE 0xA34030u
#define LW_SKC_MIN (1u << 20) /* smaller copies stay on the render thread */
#define LW_SKC_HELPERS 4
#define LW_SKC_MODES 6
#define LW_SKC_EDGES 9
_Static_assert(LW_SKC_CAVE - (LW_SKC_CALL + 5) == 0x346023u, "rel32 of the redirected call");
typedef void* (*LwSkcCopyFn)(void* dst, const void* src, size_t n);
enum { LW_SKC_IDLE = 0, LW_SKC_WOKEN = 1, LW_SKC_RUNNING = 2 };
static const uint8_t g_lwSkcGame[8] = {0xE8, 0x43, 0x98, 0xB7, 0x00, 0xF6, 0x43, 0x1C}; /* call 0x1267850; test b[rbx+1Ch] */
static const uint8_t g_lwSkcMine[8] = {0xE8, 0x23, 0x60, 0x34, 0x00, 0xF6, 0x43, 0x1C}; /* call 0xA34030 */
static const float g_lwSkcEdges[LW_SKC_EDGES] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};
static struct
{
    int ready, caveDone, made, abMode, abSecs; volatile int mode; volatile LONG chunkKb;
    uintptr_t exe; LwSkcCopyFn gameCopy; volatile LONG owner; ULONGLONG abAt;
    HANDLE wake[LW_SKC_HELPERS]; volatile LONG state[LW_SKC_HELPERS];
    /* the job: written by the render thread only, before any state goes to WOKEN */
    uint8_t* dst; const uint8_t* src; size_t len, chunk; LONG chunks; volatile LONG next;
    volatile LONG64 calls, skinCalls, notRender, busyFallback;
    volatile LONG64 bytes[LW_SKC_MODES], tCopy[LW_SKC_MODES], nCopy[LW_SKC_MODES], maxCopy[LW_SKC_MODES], hist[LW_SKC_MODES][LW_SKC_EDGES + 1];
    volatile LONG64 tWake, maxWake, nWake, tWaitHelpers, maxWaitHelpers, chunksHelper, chunksRender, cancelled;
} g_lwSkc = {.chunkKb = 128};

/* d 16-byte aligned, n a multiple of 32: the shape of the game's own non-temporal loop. */
static void LwSkcNtCopy(uint8_t* d, const uint8_t* s, size_t n)
{
    for (size_t i = 0; i < n; i += 32)
    {
        __m128i a = _mm_loadu_si128((const __m128i*)(s + i)), b = _mm_loadu_si128((const __m128i*)(s + i + 16));
        _mm_stream_si128((__m128i*)(d + i), a);
        _mm_stream_si128((__m128i*)(d + i + 16), b);
    }
    _mm_sfence();
}

/* The next chunk; never moves next past the chunk count, so a late claimer cannot take a chunk of a later job. */
static LONG LwSkcClaim(void)
{
    for (;;)
    {
        LONG i = g_lwSkc.next;
        if (i >= g_lwSkc.chunks) return -1;
        if (InterlockedCompareExchange(&g_lwSkc.next, i + 1, i) == i) return i;
    }
}

static void LwSkcDrain(int helper)
{
    for (LONG i; (i = LwSkcClaim()) >= 0;)
    {
        size_t at = (size_t)i * g_lwSkc.chunk, n = g_lwSkc.len - at < g_lwSkc.chunk ? g_lwSkc.len - at : g_lwSkc.chunk;
        LwSkcNtCopy(g_lwSkc.dst + at, g_lwSkc.src + at, n);
        InterlockedIncrement64(helper ? &g_lwSkc.chunksHelper : &g_lwSkc.chunksRender);
    }
}

/* A helper reads the job only after its own WOKEN -> RUNNING exchange, and only the render thread sets WOKEN. */
static DWORD WINAPI LwSkcHelper(void* arg)
{
    int k = (int)(intptr_t)arg;
    for (;;)
    {
        WaitForSingleObject(g_lwSkc.wake[k], INFINITE); /* auto-reset */
        if (InterlockedCompareExchange(&g_lwSkc.state[k], LW_SKC_RUNNING, LW_SKC_WOKEN) != LW_SKC_WOKEN) continue;
        LwSkcDrain(1);
        InterlockedExchange(&g_lwSkc.state[k], LW_SKC_IDLE); /* after the sfence in LwSkcNtCopy */
    }
    return 0;
}

static void LwSkcAdd(volatile LONG64* sum, volatile LONG64* most, uint64_t t)
{
    InterlockedAdd64(sum, (LONG64)t);
    LwSkinMax(most, (LONG64)t);
}

/* Render thread: the skin upload's record matches a live world's desc handle (S+0x150) and size (S+0x158). */
static int LwSkcIsSkin(const uint32_t* desc, size_t n)
{
    uint32_t d = 0;
    if (!desc || SafeRead((uintptr_t)desc, &d, 4)) return 0;
    for (LONG i = 0; i < g_lwSkin.nWorlds; i++)
    {
        const LwSkinWorld* w = &g_lwSkin.w[i]; uint32_t f[3];
        if (LwSkinLive(w) && !SafeRead(w->S + 0x150, f, sizeof f) && f[0] == d && f[2] == n) return 1;
    }
    return 0;
}

/* The aligned middle by chunks over the render thread and up to `helpers` helpers; head and tail by the game's copy. */
static void LwSkcSplit(uint8_t* dst, const uint8_t* src, size_t n, int helpers)
{
    size_t head = (32 - ((uintptr_t)dst & 31)) & 31; /* the game aligns its destination to 32 too (0x12679E4) */
    if (head) g_lwSkc.gameCopy(dst, src, head);
    g_lwSkc.dst = dst + head; g_lwSkc.src = src + head; g_lwSkc.len = (n - head) & ~(size_t)31;
    g_lwSkc.chunk = (size_t)g_lwSkc.chunkKb << 10; /* this job's copy: the game thread may change chunkKb meanwhile */
    g_lwSkc.chunks = (LONG)((g_lwSkc.len + g_lwSkc.chunk - 1) / g_lwSkc.chunk);
    g_lwSkc.next = 0;
    uint64_t w0 = __builtin_ia32_rdtsc();
    for (int k = 0; k < helpers; k++) { InterlockedExchange(&g_lwSkc.state[k], LW_SKC_WOKEN); SetEvent(g_lwSkc.wake[k]); }
    LwSkcAdd(&g_lwSkc.tWake, &g_lwSkc.maxWake, __builtin_ia32_rdtsc() - w0); InterlockedIncrement64(&g_lwSkc.nWake);
    LwSkcDrain(0);
    uint64_t w = __builtin_ia32_rdtsc();
    for (int k = 0; k < helpers; k++) /* a helper that has not started yet never will for this job */
        if (InterlockedCompareExchange(&g_lwSkc.state[k], LW_SKC_IDLE, LW_SKC_WOKEN) == LW_SKC_WOKEN) InterlockedIncrement64(&g_lwSkc.cancelled);
    for (int k = 0; k < helpers; k++) while (g_lwSkc.state[k] == LW_SKC_RUNNING) _mm_pause();
    LwSkcAdd(&g_lwSkc.tWaitHelpers, &g_lwSkc.maxWaitHelpers, __builtin_ia32_rdtsc() - w);
    size_t tail = n - head - g_lwSkc.len;
    if (tail) g_lwSkc.gameCopy(dst + n - tail, src + n - tail, tail);
}

static void LwSkcTime(int mode, size_t n, uint64_t t)
{
    InterlockedAdd64(&g_lwSkc.bytes[mode], (LONG64)n); InterlockedAdd64(&g_lwSkc.tCopy[mode], (LONG64)t);
    InterlockedIncrement64(&g_lwSkc.nCopy[mode]); LwSkinMax(&g_lwSkc.maxCopy[mode], (LONG64)t);
    LwSkinBucket(g_lwSkc.hist[mode], t, g_lwSkcEdges, LW_SKC_EDGES);
}

/* Reached from 0x6EE008 through the stub: rcx dst, rdx src, r8 size, r9 the record payload (rbx of 0x6EDEB0). */
static void* LwSkcCopy(void* dst, const void* src, size_t n, const uint32_t* desc)
{
    int mode = g_lwSkc.mode; /* read once */
    if (mode < 1 || mode >= LW_SKC_MODES) return g_lwSkc.gameCopy(dst, src, n);
    if (GetCurrentThreadId() != g_lwSkin.renderTid) { InterlockedIncrement64(&g_lwSkc.notRender); return g_lwSkc.gameCopy(dst, src, n); }
    InterlockedIncrement64(&g_lwSkc.calls);
    if (!LwSkcIsSkin(desc, n)) return g_lwSkc.gameCopy(dst, src, n);
    if (InterlockedCompareExchange(&g_lwSkc.owner, 1, 0)) { InterlockedIncrement64(&g_lwSkc.busyFallback); return g_lwSkc.gameCopy(dst, src, n); }
    InterlockedIncrement64(&g_lwSkc.skinCalls);
    uint64_t t0 = __builtin_ia32_rdtsc();
    int helpers = mode - 1 < g_lwSkc.made ? mode - 1 : g_lwSkc.made;
    const uint8_t *d = (const uint8_t*)dst, *s = (const uint8_t*)src;
    if (helpers < 1 || n < LW_SKC_MIN || (d < s + n && s < d + n)) g_lwSkc.gameCopy(dst, src, n); /* mode 1: the game's copy, timed */
    else LwSkcSplit((uint8_t*)dst, s, n, helpers);
    LwSkcTime(mode, n, __builtin_ia32_rdtsc() - t0);
    InterlockedExchange(&g_lwSkc.owner, 0);
    return dst;
}

/* The stub, once, before the first redirect: unreachable until then, so no thread needs stopping for it. */
static int LwSkcCave(void)
{
    if (g_lwSkc.caveDone) return 0;
    uint8_t stub[17] = {0x4C, 0x8B, 0xCB, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00}; /* mov r9,rbx; jmp qword [rip+0] */
    uint64_t fn = (uint64_t)(uintptr_t)LwSkcCopy;
    memcpy(stub + 9, &fn, 8);
    uint8_t* at = (uint8_t*)(g_lwSkc.exe + LW_SKC_CAVE); DWORD old;
    if (!VirtualProtect(at, sizeof stub, PAGE_EXECUTE_READWRITE, &old)) return 1;
    memcpy(at, stub, sizeof stub);
    VirtualProtect(at, sizeof stub, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof stub);
    RecordPatch((uintptr_t)at, stub, sizeof stub);
    g_lwSkc.caveDone = 1;
    return 0;
}

/* The call to the stub or back to memcpy; 0 when it holds the wanted bytes. The rel32 is written through the
 * registry lever's stopped-thread write (LwRegCommit), one aligned qword: a thread stopped at 0x6EE008 runs either call. */
static int LwSkcRedirect(int on)
{
    uint8_t now[8];
    const uint8_t *want = on ? g_lwSkcMine : g_lwSkcGame, *other = on ? g_lwSkcGame : g_lwSkcMine;
    if (SafeRead(g_lwSkc.exe + LW_SKC_CALL, now, 8)) return 1;
    if (!memcmp(now, want, 8)) return 0;
    if (memcmp(now, other, 8)) return 1; /* someone else's bytes: left alone */
    if (on && LwSkcCave()) return 1;
    LwRegEdit e = {g_lwSkc.exe + LW_SKC_CALL + 1, {0}, 4};
    memcpy(e.bytes, want + 1, 4);
    return LwRegCommit(&e, 1, NULL);
}

/* 0xA34023..0xA340C0: int3 padding between two functions (pdata 0xA33FE0..0xA34023, 0xA340C0..), referenced by
 * nothing (review scan), or the stub this lever wrote there. */
static int LwSkcCaveFree(uintptr_t exe)
{
    uint8_t pad[0x9D];
    if (g_lwSkc.caveDone || SafeRead(exe + 0xA34023u, pad, sizeof pad)) return g_lwSkc.caveDone;
    for (size_t i = 0; i < sizeof pad; i++) if (pad[i] != 0xCC) return 0;
    return 1;
}

static int LwSkcInstall(void)
{
    uintptr_t exe = g_lwSkc.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (g_lwSkc.ready) return 1;
    if (!LwSkinHook()) return 0;
    if (!ApiCodeMatches(exe + 0x6EDFFEu, "4C 8B 78 08 F2 0F 11 44 24 50 E8 43 98 B7 00 F6 43 1C 02") ||
        !ApiCodeMatches(exe + 0x6EDEC3u, "41 8B 40 08 4D 8B F1 89 42 18 49 8B D8") ||
        !ApiCodeMatches(exe + 0x6EE151u, "4C 8B CD 4C 8B C3 48 8B CE E8 51 FD FF FF") ||
        !ApiCodeMatches(exe + 0x1267850u, "48 8B C1 4C 8D 15 A6 87 D9 FE 49 83 F8 0F 0F 87") ||
        !ApiCodeMatches(exe + 0xA3401Eu, "48 83 C4 28 C3") || !LwSkcCaveFree(exe))
    { Note("living world: skin copy: the upload call, memcpy or the padding at 0xA34030 is not the reviewed code; off"); return 0; }
    g_lwSkc.gameCopy = (LwSkcCopyFn)(exe + 0x1267850u);
    g_lwSkc.ready = 1;
    return 1;
}

/* Made once, at the first mode of 2 or more; they sleep on their events after. */
static void LwSkcHelpers(void)
{
    while (g_lwSkc.made < LW_SKC_HELPERS)
    {
        int k = g_lwSkc.made;
        g_lwSkc.wake[k] = CreateEventA(NULL, FALSE, FALSE, NULL);
        HANDLE t = g_lwSkc.wake[k] ? CreateThread(NULL, 0, LwSkcHelper, (void*)(intptr_t)k, 0, NULL) : NULL;
        if (!t) { Note("living world: skin copy: helper %d could not be made; %d helpers", k + 1, k); return; }
        CloseHandle(t);
        g_lwSkc.made++;
    }
}

static int LwSkcActive(void) { return g_lwSkc.mode >= 1; }

static void LwSkcSet(int mode)
{
    if (mode < 0 || mode >= LW_SKC_MODES) { Note("living world: test: skincopy: a value outside 0..5"); return; }
    if (mode >= 1 && !LwSkcInstall()) return;
    if (mode >= 2) LwSkcHelpers();
    if (mode == 0) g_lwSkc.mode = 0; /* a copy already inside LwSkcCopy finishes as it began */
    if (g_lwSkc.ready && LwSkcRedirect(mode >= 1))
    { Note("living world: skin copy: the call at 0x6EE008 is not changed (other bytes there, or a thread could not be stopped)"); return; }
    g_lwSkc.mode = mode;
    Note("living world: skin copy %d: %s", mode, mode == 0 ? "the game's call at 0x6EE008" : mode == 1 ? "redirected, the render thread copies alone (timed)" : "redirected, helpers copy with the render thread");
}

static void LwSkcAb(int mode, int secs)
{
    if (mode == 0) { g_lwSkc.abMode = 0; LwSkcSet(1); return; }
    if (mode < 2 || mode >= LW_SKC_MODES || secs < 1) { Note("living world: test: skincopy ab: a mode outside 2..5 or a period under 1 s"); return; }
    LwSkcSet(1);
    if (g_lwSkc.mode != 1) return;
    LwSkcHelpers();
    g_lwSkc.abMode = mode; g_lwSkc.abSecs = secs; g_lwSkc.abAt = GetTickCount64();
    InterlockedExchange(&g_lwFr.phase, 1001);
    Note("living world: skin copy: modes 1 and %d take turns every %d s (recorder phase 1001 / %d)", mode, secs, 1000 + mode);
}

static void LwSkcCmd(const char* a)
{
    int x = 0, y = 2, n = sscanf(a, "ab %d %d", &x, &y);
    if (n >= 1) LwSkcAb(x, n == 2 ? y : 2);
    else if (sscanf(a, "chunk %d", &x) == 1 && x >= 32 && x <= 1024) { g_lwSkc.chunkKb = x; Note("living world: skin copy: chunks of %d KB", x); }
    else if (sscanf(a, "%d", &x) == 1) { g_lwSkc.abMode = 0; LwSkcSet(x); }
    else Note("living world: test: skincopy %s: not understood (N, ab N S, chunk K with K 32..1024)", a);
}

/* Game thread: the A/B flip. Only the mode changes, so no thread is stopped. */
static void LwSkcFrame(ULONGLONG now)
{
    if (!g_lwSkc.abMode || now - g_lwSkc.abAt < (ULONGLONG)g_lwSkc.abSecs * 1000u) return;
    g_lwSkc.abAt = now;
    g_lwSkc.mode = g_lwSkc.mode == 1 ? g_lwSkc.abMode : 1;
    InterlockedExchange(&g_lwFr.phase, 1000 + g_lwSkc.mode);
}

static void LwSkcModeNote(int m, double tpm)
{
    LONG64 n = InterlockedExchange64(&g_lwSkc.nCopy[m], 0), b = InterlockedExchange64(&g_lwSkc.bytes[m], 0);
    LONG64 t = InterlockedExchange64(&g_lwSkc.tCopy[m], 0), mx = InterlockedExchange64(&g_lwSkc.maxCopy[m], 0);
    char hist[200];
    double p999 = LwSkinHist(g_lwSkc.hist[m], g_lwSkcEdges, LW_SKC_EDGES, hist, sizeof hist), ms = (double)t / tpm;
    if (!n) return;
    Note("living world: skin copy mode %d: %lld copies, %.2f MB each, %.3f ms each (max %.3f, p99.9 under %.2f ms), %.2f MB a ms; "
         "<=0.25:0.5:0.75:1:1.25:1.5:2:3:4:more %s", m, (long long)n, (double)b / 1048576.0 / (double)n, ms / (double)n, (double)mx / tpm,
         p999, ms > 0 ? (double)b / 1048576.0 / ms : 0.0, hist);
}

static void LwSkcNote(double secs)
{
    if (!g_lwSkc.ready) return;
    double tpm = g_lwSkin.tscPerMs > 0 ? g_lwSkin.tscPerMs : 1;
    LONG64 nw = InterlockedExchange64(&g_lwSkc.nWake, 0), tw = InterlockedExchange64(&g_lwSkc.tWake, 0), mw = InterlockedExchange64(&g_lwSkc.maxWake, 0);
    LONG64 th = InterlockedExchange64(&g_lwSkc.tWaitHelpers, 0), mh = InterlockedExchange64(&g_lwSkc.maxWaitHelpers, 0);
    Note("living world: skin copy (%.0f s): mode %d (A/B %d); %lld render-thread calls, %lld skin copies, %lld on other threads, %lld busy; "
         "wake %.1f us each (max %.1f), helper wait %.1f us each (max %.1f); chunks by helpers %lld, by the render thread %lld; %lld wakes cancelled; chunk %ld KB",
         secs, g_lwSkc.mode, g_lwSkc.abMode, (long long)InterlockedExchange64(&g_lwSkc.calls, 0), (long long)InterlockedExchange64(&g_lwSkc.skinCalls, 0),
         (long long)InterlockedExchange64(&g_lwSkc.notRender, 0), (long long)InterlockedExchange64(&g_lwSkc.busyFallback, 0),
         nw ? (double)tw / tpm * 1000.0 / (double)nw : 0.0, (double)mw / tpm * 1000.0, nw ? (double)th / tpm * 1000.0 / (double)nw : 0.0, (double)mh / tpm * 1000.0,
         (long long)InterlockedExchange64(&g_lwSkc.chunksHelper, 0), (long long)InterlockedExchange64(&g_lwSkc.chunksRender, 0),
         (long long)InterlockedExchange64(&g_lwSkc.cancelled, 0), (long)g_lwSkc.chunkKb);
    for (int m = 1; m < LW_SKC_MODES; m++) LwSkcModeNote(m, tpm);
}
