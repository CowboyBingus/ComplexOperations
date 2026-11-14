/* The nav job lever (KB lever-navjob-registry). On extra large maps the main thread's nav job start 0x7B7590 grows each
 * cell's hkGeometry arrays (16-byte entries) by doubling, and every request over 0xEFF60 bytes is a fresh rpmalloc huge
 * block: mapped at the allocation, unmapped at the owner's free, so the start spends 6-15 ms in page faults. Mode 1
 * reserves each array once from the summed source sizes. Mode 2 also keeps freed huge blocks of the "hk_main_system"
 * allocator for the next start: that one object's vtable pointer points at a copy with two slots replaced, and the
 * main-owned blocks a worker frees (the terrain append reallocates them) come back through a lock-free list. Blocks stay
 * genuine rpmalloc blocks, so any mix with the game's free is safe. HELLFORGE_LW_NAVJOB=0|1|2 / command "navjob N". */
#define LW_NJ_HUGE 0xEFF60u               /* request bytes above which the block is huge (0x669E3D less the 0x20 align) */
#define LW_NJ_CAP (128ull << 20)
#define LW_NJ_SLOTS 32
#define LW_NJ_IDLE_MS 10000u
#define LW_NJ_JOBS 128
#define LW_NJ_LOG 12
typedef void* (*LwNjAllocFn)(uintptr_t obj, size_t n, size_t* out);
typedef void (*LwNjFreeFn)(uintptr_t obj, uintptr_t p, size_t n);
typedef uintptr_t (*LwNjPassFn)(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);
typedef uint32_t (*LwNjCellFn)(uintptr_t grid, float x, float y);
typedef uint32_t (*LwNjGatherFn)(uintptr_t grid, uint32_t lo, uint32_t hi, uintptr_t* buf, uint32_t limit);
typedef uintptr_t (*LwNjReserveFn)(uintptr_t heap, int32_t need, int32_t elem, uintptr_t data, int32_t count, int32_t* cap);
typedef void (*LwNjArrFreeFn)(uintptr_t heap, uintptr_t data, int32_t elem, int32_t cap);
typedef struct { uintptr_t p; uint64_t usable, stamp; } LwNjSlot;
/* One job: [0] vertices, [1] triangles; data and capacity after 0x7B7590 (at) and at the completion's free (end). */
typedef struct { uintptr_t g, at[2], end[2]; uint32_t atCap[2], endCap[2], count[2]; uint8_t ended[2]; } LwNjJob;
typedef struct
{
    uint32_t jobs, reserves, skips, hits, misses, evictions, retained, flushes, unmatched;
    uint32_t ended[2], moved[2], made[2], same[2], maxCount[2], maxCap[2];
    uint64_t reserved, peak;
} LwNjCount;
static struct
{
    int installed, quitHooked, mode, objOk, heapOk, dead, wasMission, threadNoted, jobs, logged, count, liveMode;
    volatile LONG want, inTick, cacheOn, foreign, returns, refused;
    volatile DWORD tid;
    uintptr_t exe, obj, mainHeap, page;
    volatile uintptr_t* geometry; /* the stubs store the completion's geometry (rdi) here */
    void *tick, *start, *quit;
    LwNjAllocFn origAlloc; LwNjFreeFn origFree;
    LwNjCellFn cell; LwNjGatherFn gather; LwNjReserveFn reserve; LwNjArrFreeFn arrFree;
    LwNjSlot slot[LW_NJ_SLOTS]; uint64_t bytes, seq; ULONGLONG lastUse, notedAt;
    uintptr_t ret; int64_t retBytes;
    LwNjJob job[LW_NJ_JOBS], log[LW_NJ_LOG];
    LwNjCount c;
} g_lwNj;
static uintptr_t g_lwNjVt[14]; /* the vtable copy (the -8 word and 13 slots): never freed, a thread may hold its address */

static int LwNjOnMain(void) { return GetCurrentThreadId() == __atomic_load_n(&g_lwNj.tid, __ATOMIC_RELAXED); }

/* A huge block of the main thread's rpmalloc heap: its usable bytes, else 0. The span header is what rpmalloc's own
 * free reads (0x66A60E..0x66A632): class -1 at +0xC, pages at +0x2C, heap at +0x40 (0x669CB5..0x669CC9); the block
 * starts at span + 0x80 (0x669CEF), and usable = pages * page size - 0x80 as the size query 0x630161 counts it. */
static uint64_t LwNjMainHuge(uintptr_t p)
{
    uintptr_t span = p & ~(uintptr_t)0xFFFF;
    if (span == 0 || p - span != 0x80 || *(volatile uint32_t*)(span + 0xC) != 0xFFFFFFFFu ||
        *(volatile uintptr_t*)(span + 0x40) != g_lwNj.mainHeap) return 0;
    return (uint64_t)*(volatile uint32_t*)(span + 0x2C) * g_lwNj.page - 0x80u;
}

/* The cache: main thread only, so no lock. */
static uintptr_t LwNjTakeAt(int i)
{
    uintptr_t p = g_lwNj.slot[i].p;
    g_lwNj.bytes -= g_lwNj.slot[i].usable;
    g_lwNj.slot[i] = g_lwNj.slot[--g_lwNj.count];
    return p;
}
/* Freed through the game's own free with size 0: the owner unmaps it at once, and 0 skips only the statistic that the
 * retain already took (0x817200 uses r8 for nothing else). */
static void LwNjEvict(int i) { uintptr_t p = LwNjTakeAt(i); g_lwNj.c.evictions++; g_lwNj.origFree(g_lwNj.obj, p, 0); }
/* The least recently kept block; with `notLarger`, among those no larger than `usable` first, so stale small blocks
 * never lock the big ones out (review F4). */
static int LwNjVictim(uint64_t usable, int notLarger)
{
    int lru = 0, fit = -1;
    for (int i = 1; i < g_lwNj.count; i++) if (g_lwNj.slot[i].stamp < g_lwNj.slot[lru].stamp) lru = i;
    for (int i = 0; notLarger && i < g_lwNj.count; i++)
        if (g_lwNj.slot[i].usable <= usable && (fit < 0 || g_lwNj.slot[i].stamp < g_lwNj.slot[fit].stamp)) fit = i;
    return fit >= 0 ? fit : lru;
}
static void LwNjPut(uintptr_t p, uint64_t usable)
{
    g_lwNj.lastUse = GetTickCount64();
    if (usable > LW_NJ_CAP) { g_lwNj.origFree(g_lwNj.obj, p, 0); return; }
    while (g_lwNj.count && g_lwNj.bytes + usable > LW_NJ_CAP) LwNjEvict(LwNjVictim(usable, 0));
    if (g_lwNj.count == LW_NJ_SLOTS) LwNjEvict(LwNjVictim(usable, 1));
    g_lwNj.slot[g_lwNj.count++] = (LwNjSlot){p, usable, ++g_lwNj.seq};
    g_lwNj.bytes += usable;
    if (g_lwNj.bytes > g_lwNj.c.peak) g_lwNj.c.peak = g_lwNj.bytes;
}
static int LwNjCached(uintptr_t p)
{
    for (int i = 0; i < g_lwNj.count; i++) if (g_lwNj.slot[i].p == p) return 1;
    return 0;
}
static void LwNjFlush(void)
{
    if (g_lwNj.count) g_lwNj.c.flushes++;
    while (g_lwNj.count) { uintptr_t p = LwNjTakeAt(g_lwNj.count - 1); g_lwNj.origFree(g_lwNj.obj, p, 0); }
}

/* Slot +8 of the hk_main_system object (0x817160: rcx obj, rdx bytes, r8 out or 0). The allocator gets only the low 32
 * bits of the size (0x817189); the statistic and *out get all of it (0x817196..0x8171B2). Smallest fit, at most 4n. */
static __attribute__((noinline)) void* LwNjAllocHuge(uintptr_t obj, size_t n, size_t* out)
{
    uint32_t nn = (uint32_t)n;
    if (!LwNjOnMain() || !g_lwNj.inTick) return g_lwNj.origAlloc(obj, n, out);
    g_lwNj.lastUse = GetTickCount64();
    int best = -1;
    for (int i = 0; i < g_lwNj.count; i++)
    {
        uint64_t u = g_lwNj.slot[i].usable;
        if (u >= nn && u <= 4ull * nn && (best < 0 || u < g_lwNj.slot[best].usable)) best = i;
    }
    if (best < 0) { g_lwNj.c.misses++; return g_lwNj.origAlloc(obj, n, out); }
    g_lwNj.c.hits++;
    uintptr_t p = LwNjTakeAt(best);
    uint64_t cur = __atomic_add_fetch((uint64_t*)(obj + 0x20), (uint64_t)n, __ATOMIC_SEQ_CST);
    if (cur > *(volatile uint64_t*)(obj + 0x18)) *(volatile uint64_t*)(obj + 0x18) = *(volatile uint64_t*)(obj + 0x20);
    if (out) *out = n;
    return (void*)p;
}
/* Every Havok allocation on every thread passes here: the common case is one compare and a jump. */
static void* LwNjAlloc(uintptr_t obj, size_t n, size_t* out)
{
    if ((uint32_t)n <= LW_NJ_HUGE || !__atomic_load_n(&g_lwNj.cacheOn, __ATOMIC_RELAXED)) return g_lwNj.origAlloc(obj, n, out);
    return LwNjAllocHuge(obj, n, out);
}

/* Review F1: a main-owned block that a worker frees goes on this list (its first two words: next, usable) instead of
 * rpmalloc's deferred list +0x710, which only main's next huge allocation would unmap. Workers only push and main takes
 * the whole list at once, so no ABA. */
static int LwNjPush(uintptr_t p, uint64_t usable)
{
    if ((uint64_t)__atomic_add_fetch(&g_lwNj.retBytes, (int64_t)usable, __ATOMIC_RELAXED) > LW_NJ_CAP)
    {
        __atomic_sub_fetch(&g_lwNj.retBytes, (int64_t)usable, __ATOMIC_RELAXED);
        InterlockedIncrement(&g_lwNj.refused);
        return 0;
    }
    ((volatile uint64_t*)p)[1] = usable;
    uintptr_t head = __atomic_load_n(&g_lwNj.ret, __ATOMIC_RELAXED);
    do *(volatile uintptr_t*)p = head;
    while (!__atomic_compare_exchange_n(&g_lwNj.ret, &head, p, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    InterlockedIncrement(&g_lwNj.returns);
    return 1;
}

/* Main keeps its own huge blocks freed inside the tick, workers return main's (F1); anything else, and a block already
 * kept (a double free), goes to the game's free as before. */
static __attribute__((noinline)) void LwNjFreeHuge(uintptr_t obj, uintptr_t p, size_t n)
{
    uint64_t usable = LwNjMainHuge(p);
    int onMain = LwNjOnMain();
    if (!usable || (onMain && (!g_lwNj.inTick || LwNjCached(p))) || (!onMain && !LwNjPush(p, usable))) { g_lwNj.origFree(obj, p, n); return; }
    __atomic_sub_fetch((uint64_t*)(obj + 0x20), (uint64_t)n, __ATOMIC_SEQ_CST); /* the statistic 0x817203 takes */
    if (onMain) { LwNjPut(p, usable); g_lwNj.c.retained++; }
}
/* Slot +0x10 (0x817200: rcx obj, rdx block, r8 bytes for the statistic only). */
static void LwNjFree(uintptr_t obj, uintptr_t p, size_t n)
{
    if ((uint32_t)n <= LW_NJ_HUGE || p == 0 || !__atomic_load_n(&g_lwNj.cacheOn, __ATOMIC_RELAXED)) { g_lwNj.origFree(obj, p, n); return; }
    LwNjFreeHuge(obj, p, n);
}

/* Main thread: the workers' returns into the cache (keep), or back to rpmalloc. Their statistic is already taken. */
static void LwNjDrain(int keep)
{
    uintptr_t p = __atomic_exchange_n(&g_lwNj.ret, 0, __ATOMIC_ACQUIRE);
    while (p)
    {
        uintptr_t next = *(volatile uintptr_t*)p;
        uint64_t usable = ((volatile uint64_t*)p)[1];
        __atomic_sub_fetch(&g_lwNj.retBytes, (int64_t)usable, __ATOMIC_RELAXED);
        if (keep) LwNjPut(p, usable);
        else g_lwNj.origFree(g_lwNj.obj, p, 0);
        p = next;
    }
}

static int LwNjQword(uintptr_t at, uintptr_t* v) { *v = 0; return !SafeRead(at, v, 8) && *v != 0; }
static int LwNjIs(uintptr_t at, uintptr_t want) { uintptr_t v; return LwNjQword(at, &v) && v == want; }

/* Review F3 and the design's install checks, on the nav tick's thread: the memory system's allocator at sys+0x4F8
 * (0xA31577) is the "hk_main_system" wrapper (0x78ADA0..0x78ADDC; name at tracker+8, 0x5C2C57), this thread's router
 * +0x58 is it (0x99D75A..0x99D77D), and this thread's rpmalloc heap ([[gs:0x58]]+0x4350, 0x630066) is owned by it. */
static int LwNjObject(void)
{
    uintptr_t e = g_lwNj.exe, sys, obj, trk, name, router = 0, heap, tls, blk, page, slots[2] = {0, 0}, teb = (uintptr_t)NtCurrentTeb();
    DWORD index = 0; char nm[16] = "";
    if (!LwNjQword(e + 0x1985408u, &sys) || !LwNjIs(sys, e + 0x169E540u) || !LwNjQword(sys + 0x4F8, &obj) || (obj & 7) ||
        !LwNjIs(obj, e + 0x169E5E8u) || !LwNjQword(obj + 0x10, &trk) || !LwNjIs(trk, e + 0x168B5A0u) ||
        !LwNjQword(trk + 8, &name) || SafeRead(name, nm, 15) || strcmp(nm, "hk_main_system"))
    { Note("living world: nav job: the Havok allocator is not the reviewed hk_main_system object; no cache"); return 0; }
    if (SafeRead(e + 0x1985320u, &index, 4) || ((router = (uintptr_t)TlsGetValue(index)) == 0 && !LwNjQword(e + 0x1985318u, &router)) ||
        !LwNjIs(router + 0x58, obj) || !LwNjQword(teb + 0x58, &tls) || !LwNjQword(tls, &blk) || !LwNjQword(blk + 0x4350, &heap) ||
        !LwNjIs(heap, teb) || !LwNjQword(e + 0x27B10F0u, &page) || page < 0x1000 || page > 0x200000 || (page & (page - 1)) ||
        SafeRead(e + 0x169E5F0u, slots, 16) || slots[0] != e + 0x817160u || slots[1] != e + 0x817200u || SafeRead(e + 0x169E5E0u, g_lwNjVt, sizeof g_lwNjVt))
    { Note("living world: nav job: this thread's Havok router, rpmalloc heap or page size is not as reviewed; no cache"); return 0; }
    g_lwNj.obj = obj; g_lwNj.mainHeap = heap; g_lwNj.page = page;
    g_lwNj.origAlloc = (LwNjAllocFn)slots[0]; g_lwNj.origFree = (LwNjFreeFn)slots[1];
    g_lwNjVt[2] = (uintptr_t)LwNjAlloc; g_lwNjVt[3] = (uintptr_t)LwNjFree;
    return 1;
}

/* One aligned pointer store: other threads see either vtable, and both are valid. Only this object's pointer changes;
 * other objects share the game's vtable 0x169E5E8 (ctors 0x8170D0, 0x816F80). */
static int LwNjCacheOn(void)
{
    if (g_lwNj.objOk == 0) g_lwNj.objOk = LwNjObject() ? 1 : -1;
    if (g_lwNj.objOk < 0 || !g_lwNj.quitHooked) return 0;
    uintptr_t game = g_lwNj.exe + 0x169E5E8u;
    __atomic_store_n(&g_lwNj.cacheOn, 1, __ATOMIC_SEQ_CST);
    if (__atomic_compare_exchange_n((uintptr_t*)g_lwNj.obj, &game, (uintptr_t)&g_lwNjVt[1], 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 1;
    __atomic_store_n(&g_lwNj.cacheOn, 0, __ATOMIC_SEQ_CST);
    return 0;
}
/* Hooks first stop keeping, then the game's vtable comes back, then (on the main thread only) the blocks are freed. A
 * worker that pushes after the drain is drained at the next tick; the copy stays valid for a call already in it. */
static void LwNjCacheOff(int onMain)
{
    uintptr_t ours = (uintptr_t)&g_lwNjVt[1];
    __atomic_store_n(&g_lwNj.cacheOn, 0, __ATOMIC_SEQ_CST);
    __atomic_compare_exchange_n((uintptr_t*)g_lwNj.obj, &ours, g_lwNj.exe + 0x169E5E8u, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (!onMain) return;
    LwNjFlush();
    LwNjDrain(0);
}

/* The reserve's heap: the container heap 0x1985330 (vt 0x140FD00: alloc 0x99D750, free 0x99D7A0), as 0xA045E5 uses. */
static int LwNjHeapOk(void)
{
    uintptr_t e = g_lwNj.exe;
    return LwNjIs(e + 0x1899880u, e + 0x1985330u) && LwNjIs(e + 0x1985330u, e + 0x140FD00u) &&
           LwNjIs(e + 0x140FD08u, e + 0x99D750u) && LwNjIs(e + 0x140FD10u, e + 0x99D7A0u);
}

static const char* const g_lwNjModes[3] = {"as the game has it", "arrays reserved once", "arrays reserved once, huge blocks kept"};
/* Main thread: the asked mode, when it can be had. */
static void LwNjApply(void)
{
    int want = (int)g_lwNj.want;
    if (want == g_lwNj.mode || g_lwNj.dead) return;
    if (g_lwNj.mode == 2) LwNjCacheOff(1);
    if (g_lwNj.heapOk == 0) g_lwNj.heapOk = LwNjHeapOk() ? 1 : -1;
    if (want > 0 && g_lwNj.heapOk < 0) { Note("living world: nav job: the container heap is not the reviewed one; mode 0"); want = 0; }
    if (want == 2 && !LwNjCacheOn()) { Note("living world: nav job: the huge-block cache cannot go in (see above, or the Havok quit hook is missing); mode 1"); want = 1; }
    InterlockedExchange(&g_lwNj.want, want);
    g_lwNj.mode = want;
    Note("living world: nav job mode %d: %s", want, g_lwNjModes[want]);
}

/* The start's own gather (0x7B7682..0x7B76D9): the snapshot's bounds widened by the vector at 0x16AA6E0, the grid cells
 * of both corners (0x7ACDD0: rcx grid, xmm1 x, xmm2 y), then the overlapping objects into the buffer (0x7ADA50). It
 * stores only to that buffer and returns every hit, written or not (0x7ADC09..0x7ADC3F): more than the buffer holds
 * is refused. Sums the vertex (+0x20) and triangle (+0x30) counts of each object with [o+0x40] bit 0 (0x7B777B). */
static int LwNjSum(uintptr_t grid, uint32_t cellId, uintptr_t buf, int64_t* sum)
{
    uintptr_t rec = *(volatile uintptr_t*)(grid + 0x38) + (uint32_t)(*(volatile uint16_t*)(grid + 0x26) * cellId);
    uintptr_t snap = *(volatile uintptr_t*)(rec + 0x18);
    const volatile float* m = (const volatile float*)(g_lwNj.exe + 0x16AA6E0u);
    const volatile float* hi = (const volatile float*)(snap + 0xE0);
    const volatile float* lo = (const volatile float*)(snap + 0xD0);
    uint32_t cellHi = g_lwNj.cell(grid, hi[0] + m[0], hi[1] + m[1]);
    uint32_t cellLo = g_lwNj.cell(grid, lo[0] - m[0], lo[1] - m[1]);
    uint32_t limit = *(volatile uint32_t*)(grid + 0x1C);
    uint32_t n = g_lwNj.gather(grid, cellLo, cellHi, (uintptr_t*)buf, limit);
    if (n > limit) return 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uintptr_t o = ((volatile uintptr_t*)buf)[i], src;
        if (!(*(volatile uint32_t*)(o + 0x40) & 1) || (src = *(volatile uintptr_t*)(o + 0x78)) == 0) continue;
        sum[0] += *(volatile int32_t*)(src + 0x20);
        sum[1] += *(volatile int32_t*)(src + 0x30);
    }
    return 1;
}
/* One array (data +0, count +8, capacity|flags +0xC) to count + extra entries, as the append 0xA045C9..0xA04612
 * reserves it: 0x99A2D0 grows to max(need, 2 cap), so an empty array gets exactly need. */
static void LwNjReserveArr(uintptr_t arr, int64_t extra)
{
    int32_t count = *(volatile int32_t*)(arr + 8);
    int32_t* cap = (int32_t*)(arr + 0xC);
    int64_t need = (int64_t)count + extra;
    if (extra <= 0 || need > 0x3FFFFFFF || need <= (*cap & 0x3FFFFFFF)) return;
    uintptr_t heap = *(volatile uintptr_t*)(g_lwNj.exe + 0x1899880u);
    *(volatile uintptr_t*)arr = g_lwNj.reserve(heap, (int32_t)need, 0x10, *(volatile uintptr_t*)arr, count, cap);
    g_lwNj.c.reserves++;
    g_lwNj.c.reserved += (uint64_t)need * 16u;
}

static LwNjJob* LwNjFind(uintptr_t g)
{
    for (int i = 0; i < g_lwNj.jobs; i++) if (g_lwNj.job[i].g == g) return &g_lwNj.job[i];
    return NULL;
}
static void LwNjDrop(LwNjJob* j) { *j = g_lwNj.job[--g_lwNj.jobs]; }
/* Review F8: the dest arrays right after the start. A cell started again before its triangle free was seen counts as
 * unmatched; so does the oldest job when the table is full. */
static void LwNjJobStart(uintptr_t g)
{
    LwNjJob* j = LwNjFind(g);
    if (j) g_lwNj.c.unmatched++;
    if (!j && g_lwNj.jobs == LW_NJ_JOBS) { g_lwNj.c.unmatched++; LwNjDrop(&g_lwNj.job[0]); }
    if (!j) j = &g_lwNj.job[g_lwNj.jobs++];
    memset(j, 0, sizeof *j);
    j->g = g;
    for (int k = 0; k < 2; k++)
    {
        uintptr_t arr = g + 0x18 + 0x10u * k;
        j->at[k] = *(volatile uintptr_t*)arr;
        j->count[k] = *(volatile uint32_t*)(arr + 8);
        j->atCap[k] = *(volatile uint32_t*)(arr + 0xC) & 0x3FFFFFFFu;
        if (j->count[k] > g_lwNj.c.maxCount[k]) g_lwNj.c.maxCount[k] = j->count[k];
    }
}
/* The completion's free of [cell+0]'s array k (0x7B8DA0 vertices, 0x7B8DDD triangles; rdi = [cell+0] at both): a
 * changed pointer means the worker reallocated (a null one at the start: it allocated). */
static void LwNjEnd(int k, uintptr_t data, uint32_t cap)
{
    LwNjJob* j = LwNjFind(*g_lwNj.geometry);
    if (!j || j->ended[k]) return;
    j->ended[k] = 1; j->end[k] = data; j->endCap[k] = cap;
    g_lwNj.c.ended[k]++;
    if (data == j->at[k]) g_lwNj.c.same[k]++;
    else if (j->at[k]) g_lwNj.c.moved[k]++;
    else g_lwNj.c.made[k]++;
    if (cap > g_lwNj.c.maxCap[k]) g_lwNj.c.maxCap[k] = cap;
    if (k == 0) return;
    if (((uint64_t)j->endCap[0] * 16u > LW_NJ_HUGE || (uint64_t)cap * 16u > LW_NJ_HUGE) && g_lwNj.logged < LW_NJ_LOG) g_lwNj.log[g_lwNj.logged++] = *j;
    LwNjDrop(j);
}
static void LwNjEndV(uintptr_t heap, uintptr_t data, uintptr_t elem, uintptr_t cap)
{
    if (LwNjOnMain()) LwNjEnd(0, data, (uint32_t)cap);
    g_lwNj.arrFree(heap, data, (int32_t)elem, (int32_t)cap);
}
static void LwNjEndT(uintptr_t heap, uintptr_t data, uintptr_t elem, uintptr_t cap)
{
    if (LwNjOnMain()) LwNjEnd(1, data, (uint32_t)cap);
    g_lwNj.arrFree(heap, data, (int32_t)elem, (int32_t)cap);
}

/* 0x7B7590 (rcx grid, edx cell id, r8 dest [cell+0], r9 object buffer; 0x7B84ED..0x7B8501). The reserve runs on the
 * thread and data of the original, right before it, which recomputes the same gather. */
static uintptr_t LwNjStart(uintptr_t grid, uintptr_t cellId, uintptr_t dest, uintptr_t buf)
{
    LwNjPassFn orig = (LwNjPassFn)g_lwNj.start;
    if (!LwLive() || !LwNjOnMain() || g_lwNj.dead || dest == 0) /* any other level: the game's call */
    {
        uintptr_t r0 = orig(grid, cellId, dest, buf);
#ifdef CO_RESEARCH /* the tilt probe's baseline: what a normal mission's cell builds are handed */
        if (!LwLive() && !g_lwNj.dead && dest != 0) LwTnCellStart(grid, (uint32_t)cellId);
#endif
        return r0;
    }
    int64_t sum[2] = {0, 0};
    if (g_lwNj.mode >= 1 && !LwNjSum(grid, (uint32_t)cellId, buf, sum)) g_lwNj.c.skips++;
    else if (g_lwNj.mode >= 1) { LwNjReserveArr(dest + 0x18, sum[0]); LwNjReserveArr(dest + 0x28, sum[1]); }
    uintptr_t r = orig(grid, cellId, dest, buf);
    g_lwNj.c.jobs++;
    LwNjJobStart(dest);
    LwTnCellStart(grid, (uint32_t)cellId);
    return r;
}

static double LwNjMiB(uint64_t b) { return (double)b / 1048576.0; }
static void LwNjNoteJobs(void)
{
    for (int i = 0; i < g_lwNj.logged; i++)
    {
        const LwNjJob* j = &g_lwNj.log[i];
        Note("living world: nav job: geometry %p: vertices %p cap %u after the start, %p cap %u at completion; triangles %p cap %u, %p cap %u",
            (void*)j->g, (void*)j->at[0], j->atCap[0], (void*)j->end[0], j->endCap[0], (void*)j->at[1], j->atCap[1], (void*)j->end[1], j->endCap[1]);
    }
    g_lwNj.logged = 0;
}
/* Every 30 s with any activity: the counters of review F8. */
static void LwNjNote(ULONGLONG now)
{
    if (now - g_lwNj.notedAt < 30000) return;
    g_lwNj.notedAt = now;
    LONG returns = InterlockedExchange(&g_lwNj.returns, 0), refused = InterlockedExchange(&g_lwNj.refused, 0), foreign = InterlockedExchange(&g_lwNj.foreign, 0);
    const LwNjCount* c = &g_lwNj.c;
    if (!c->jobs && !c->ended[0] && !returns && !g_lwNj.count) return;
    Note("living world: nav job (30 s, mode %d): %u job starts, %u arrays reserved (%.1f MiB, %u gathers over the buffer); cache %u hits, %u misses, "
        "%u evictions, %u kept on main, %ld worker returns (%ld over the cap), %.1f MiB held now (peak %.1f), %u flushes; %ld ticks on another thread",
        g_lwNj.mode, c->jobs, c->reserves, LwNjMiB(c->reserved), c->skips, c->hits, c->misses, c->evictions, c->retained, returns, refused,
        LwNjMiB(g_lwNj.bytes), LwNjMiB(c->peak), c->flushes, foreign);
    Note("living world: nav job completions (30 s): vertices %u (worker reallocated %u, worker allocated %u, same block %u), triangles %u (%u, %u, %u); "
        "%u starts without a seen completion; largest after the start %u vertices / %u triangles, largest capacity at completion %u / %u",
        c->ended[0], c->moved[0], c->made[0], c->same[0], c->ended[1], c->moved[1], c->made[1], c->same[1], c->unmatched,
        c->maxCount[0], c->maxCount[1], c->maxCap[0], c->maxCap[1]);
    LwNjNoteJobs();
    memset(&g_lwNj.c, 0, sizeof g_lwNj.c);
    g_lwNj.c.peak = g_lwNj.bytes;
}
/* Main thread, around the tick and each frame: the asked mode, the workers' returns, the idle flush, the note. */
static void LwNjHouse(void)
{
    LwNjApply();
    LwNjDrain(g_lwNj.mode == 2);
    ULONGLONG now = GetTickCount64();
    if (g_lwNj.count && now - g_lwNj.lastUse >= LW_NJ_IDLE_MS) LwNjFlush();
    LwNjNote(now);
}

/* 0x7B8A10, the nav tick (callers 0x78E1E6 and the table entry 0x27CD530). Its first thread is the main thread of this
 * lever; on any other the tick runs as the game has it. */
static uintptr_t LwNjTick(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    LwNjPassFn orig = (LwNjPassFn)g_lwNj.tick;
    /* any other level: the game's call, once the complex level's mode is undone (LwNjLive, or here when that ran off
     * this thread) */
    if (!LwLive() && g_lwNj.mode == 0) return orig(a, b, c, d);
    DWORD me = GetCurrentThreadId(), none = 0;
    __atomic_compare_exchange_n(&g_lwNj.tid, &none, me, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (me != g_lwNj.tid) InterlockedIncrement(&g_lwNj.foreign);
    if (me != g_lwNj.tid || g_lwNj.inTick || g_lwNj.dead) return orig(a, b, c, d);
    LwNjHouse();
    __atomic_store_n(&g_lwNj.inTick, 1, __ATOMIC_RELAXED);
    uintptr_t r = orig(a, b, c, d);
    __atomic_store_n(&g_lwNj.inTick, 0, __ATOMIC_RELAXED);
    LwNjHouse();
    return r;
}

/* hkBaseSystem::quit 0x99EA00 (caller 0x78B673): the wrapper's dtor 0x817030 rewrites its vtable pointer and a later
 * free would call a dead allocator, so the cache ends here. Off the main thread the blocks are left to the exit. */
static uintptr_t LwNjQuit(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    if (!g_lwNj.dead)
    {
        int onMain = LwNjOnMain();
        g_lwNj.dead = 1;
        if (__atomic_load_n(&g_lwNj.cacheOn, __ATOMIC_SEQ_CST)) LwNjCacheOff(onMain);
        Note("living world: nav job: Havok quits; the lever is off%s", onMain ? "" : " (not on the nav thread: kept blocks are left to the exit)");
    }
    return ((LwNjPassFn)g_lwNj.quit)(a, b, c, d);
}

/* From LivingTickBody on the game thread: when it is the nav tick's thread, the flushes also run while no nav ticks
 * do (the ship), and a mission's end flushes at once; its geometries go, so the job table goes too. */
static void LwNjFrame(void)
{
    DWORD tid = __atomic_load_n(&g_lwNj.tid, __ATOMIC_RELAXED);
    if (!g_lwNj.installed || g_lwNj.dead || tid == 0) return;
    if (GetCurrentThreadId() != tid)
    {
        if (!g_lwNj.threadNoted++) Note("living world: nav job: the nav tick runs on another thread than the game update; its flushes run in the nav tick only");
        return;
    }
    int inMission = ReadState(g_lw.game) == HF_STATE_MISSION;
    if (g_lwNj.wasMission && !inMission) { LwNjFlush(); g_lwNj.jobs = 0; }
    g_lwNj.wasMission = inMission;
    LwNjHouse();
}

/* The mission in play changed kind (LwLiveSet). A complex level asks its mode, applied at the nav thread's next tick;
 * any other level mode 0. Leaving, the game's vtable comes back at once on any thread; the kept blocks are freed here
 * on the nav thread, else at its next tick, before the tick's own work. */
static void LwNjLive(int complex)
{
    if (!g_lwNj.installed || g_lwNj.dead) return;
    InterlockedExchange(&g_lwNj.want, complex ? g_lwNj.liveMode : 0);
    if (complex) return;
    const int onMain = LwNjOnMain();
    if (__atomic_load_n(&g_lwNj.cacheOn, __ATOMIC_SEQ_CST)) LwNjCacheOff(onMain);
    if (!onMain) return;
    LwNjApply();
    g_lwNj.jobs = 0; /* the level's geometries go with it */
}

static LONG LwNjWanted(void) { return g_lwNj.installed ? g_lwNj.want : -1; }

static void LwNavJobCmd(int mode)
{
    if (!g_lwNj.installed || g_lwNj.dead || mode < 0 || mode > 2)
    { Note("living world: test: navjob: the lever is not installed (HELLFORGE_LW_NAVJOB unset, navtime on, or not the reviewed code), or a value outside 0..2"); return; }
    InterlockedExchange(&g_lwNj.want, mode);
    Note("living world: test: navjob %d asked; the nav thread applies it at its next tick", mode);
}

static const struct { uint32_t rva; const char* bytes; } g_lwNjPins[] = {
    {0x7B7590u, "4C 89 4C 24 20 4C 89 44 24 18 53 55 56 57 41 54"}, /* job start */
    {0x7B75AAu, "0F B7 59 26 45 33 ED F3 0F 10 05 03 6D BA 01 41 8B C5 48 8B 79 38 0F 57 C9 0F AF DA"}, /* cell record */
    {0x7B7682u, "48 8B 44 3B 18 48 8B CE 0F 10 88 E0 00 00 00 0F 10 B0 D0 00 00 00 0F 58 0D 41 30 EF 00 0F 5C 35 3A 30 EF 00 "
                "0F 28 D1 0F C6 D1 55 E8 1E 57 FF FF 0F 28 D6 0F 28 CE 0F C6 D6 55 48 8B CE 44 8B D0 E8 09 57 FF FF 8B 4E 1C "
                "8B D0 89 4C 24 20 4D 8B CC 48 8B CE 45 8B C2 E8 72 63 FF FF"}, /* the gather */
    {0x7B7700u, "48 8B 8C 24 D8 00 00 00 41 8B C5 48 8B 34 C1"},
    {0x7B777Bu, "8B 46 40 A8 01 74 4D 4C 8B 46 78 48 8D 54 24 30 49 63 5F 30 4C 8B CE 49 8B CF E8 16 CE 24 00"}, /* append */
    {0x7ACDD0u, "F3 0F 10 41 14 F3 0F 5C 41 0C F3 0F 5C 49 0C 0F B7 01 F3 0F 5C 51 10"},
    {0x7ADA50u, "4C 89 4C 24 20 53 55 56 57 41 54 41 56 41 57 48 83 EC 20"}, {0x7ADAD8u, "44 8B 94 24 80 00 00 00"},
    {0x7ADB91u, "8B B4 24 80 00 00 00"}, {0x7ADC64u, "8B C3 48 83 C4 20"},
    {0xA045C9u, "48 63 71 20 48 8B D9 8B 43 24 4C 8B FA 41 8B 50 20 4D 8B E8 4C 63 71 30 4C 8D 43 24 48 8B 0D 94 52 E9 00 25 FF FF FF 3F "
                "03 D6 3B C2 7D 26 8B 43 20 4C 8B 4B 18 4C 89 44 24 28 41 B8 10 00 00 00 89 44 24 20 E8 BE 5C F9 FF 48 89 43 18"},
    {0xA04620u, "4C 8D 43 34 41 8B 55 30 25 FF FF FF 3F 41 03 D6 3B C2 7D 26 8B 43 30 4C 8B 4B 28"},
    {0x99A2D0u, "4C 8B DC 49 89 5B 08 49 89 6B 18 49 89 73 20 89 54 24 10 57 48 83 EC 60"},
    {0x99A359u, "48 8B 84 24 98 00 00 00 4C 8B CB 48 89 44 24 28 44 8B C7 48 8B CE 8B 10 8B 84 24 90 00 00 00 23 D5 03 D2"},
    {0x99A228u, "83 3E 00 49 8B D2 49 8B CF 7C 3B 49 63 C0 4C 8D 45 B0 48 0F AF D8 49 8B 07 FF 50 08"}, /* grow: alloc vt+8 */
    {0x99A920u, "48 89 5C 24 08 48 89 74 24 10 44 89 4C 24 20 44 89 44 24 18 57 48 83 EC 70"},
    {0x99D750u, "48 89 5C 24 08 57 48 83 EC 20 8B 0D C0 7B FE 00 49 8B D8 48 8B FA FF 15 DC 92 A5 00 4C 8B C3 48 8B D7 48 85 C0 48 0F 44 05 9B 7B FE 00 48 8B 48 58 48 8B 01"},
    {0x99D7CDu, "48 8B 48 58 48 8B 01 48 8B 5C 24 30 48 83 C4 20 5F 48 FF 60 10"},
    {0x817160u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 05 2A CF 2F 01 49 8B F0 48 8B FA 48 8B D9 48 8B 49 10 41 B8 20 00 00 00 "
                "8B D2 FF 50 58 48 85 C0 74 32 48 8B CF F0 48 0F C1 4B 20 48 03 CF 48 3B 4B 18 76 08 48 8B 4B 20 48 89 4B 18 48 85 F6 74 33 48 89 3E"},
    {0x817200u, "49 F7 D8 F0 4C 0F C1 41 20 48 85 D2 74 0F 48 8B 05 8B CE 2F 01 48 8B 49 10 48 FF 60 60 C3"},
    {0x78ADBFu, "48 8D 05 22 38 F1 00 48 89 5F 10 48 89 07"}, {0x5C2C57u, "48 89 51 08"}, {0x5C2CC5u, "48 8D 05 D4 88 0C 01"},
    {0x99F3D0u, "48 8B 05 31 60 FE 00 C3"}, {0xA31577u, "48 89 91 F8 04 00 00"}, {0xA315DCu, "48 8B 87 F8 04 00 00 48 89 42 58"},
    {0x630066u, "65 48 8B 04 25 58 00 00 00 48 8B DA B9 50 43 00 00 41 8B D1 48 8B 00 48 8B 0C 01"},
    {0x630150u, "41 8B 40 2C 75 0B 48 C1 E0 10 49 2B C1 49 03 C0 C3 48 0F AF 05 87 0F 18 02"},
    {0x669CB5u, "C7 40 0C FF FF FF FF 8B CE 89 70 2C 8B 44 24 30 41 89 41 3C 49 89 59 40"}, {0x669CEFu, "49 8D 81 80 00 00 00"},
    {0x669E3Du, "48 81 FA 80 FF 0E 00"}, {0x669EC3u, "48 83 FA 10"}, {0x669EF5u, "49 03 D0"},
    {0x66A530u, "48 83 EC 28 48 8B 51 40 4C 8B D1 65 48 8B 04 25 30 00 00 00 48 39 02 74"},
    {0x66A60Eu, "4C 8B C1 49 81 E0 00 00 FF FF 0F 84 8F 00 00 00 41 83 78 0C 60 73 0B"}, {0x66A6A5u, "49 8B C8 E9 83 FE FF FF"},
    {0x7B8A10u, "48 8B C4 89 48 08 55 53 56 57 41 54"}, {0x7B8ABAu, "E8 41 F7 FF FF"},
    {0x7B84EDu, "4C 8B 8F B8 00 00 00 48 8D 8F 10 67 00 00 8B 53 58 4C 8B C0 E8 8A F0 FF FF"},
    {0x7B8D6Eu, "48 8B 3B 48 85 FF 74 77 44 8B 4F 24 44 89 67 20 45 85 C9 78 22 48 8B 57 18 48 85 D2 74 19 48 8B 0D ED 0A 0E 01 "
                "41 81 E1 FF FF FF 3F 41 B8 10 00 00 00 E8 7B 1B 1E 00 4C 89 67 18"},
    {0x7B8DB0u, "48 8B 3B 44 8B 4F 34 44 89 67 30 45 85 C9 78 22 48 8B 57 28 48 85 D2 74 19 48 8B 0D B0 0A 0E 01 "
                "41 81 E1 FF FF FF 3F 41 B8 10 00 00 00 E8 3E 1B 1E 00 4C 89 67 28"},
    {0x99EA00u, "48 83 EC 28 80 3D 8D 69 FE 00 00 74 7F"},
};

/* The two completion frees of [cell+0]'s arrays become calls to stubs that store rdi (the geometry) and go on to
 * LwNjEndV / LwNjEndT, which call 0x99A920 as the site did. Written before the first mission, while no nav tick runs. */
static int LwNjEndSites(uintptr_t e)
{
    static const uint32_t site[2] = {0x7B8DA0u, 0x7B8DDDu};
    static const uint8_t was[5] = {0xE8, 0x7B, 0x1B, 0x1E, 0x00}; /* 0x7B8DA0: call 0x99A920 */
    void* fn[2] = {(void*)LwNjEndV, (void*)LwNjEndT};
    uint8_t* block = AllocateNearGame(e, 0x2000); /* the stubs' page, then the geometry word's (read-write) */
    if (block == NULL) return 0;
    for (int k = 0; k < 2; k++)
    {
        uint8_t s[19] = {0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0}; /* mov [rip+G],rdi; mov rax,fn; jmp rax */
        int32_t disp = 0x1000 - (0x20 * k + 7); uint64_t f = (uint64_t)(uintptr_t)fn[k];
        memcpy(s + 3, &disp, 4); memcpy(s + 9, &f, 8); memcpy(block + 0x20 * k, s, sizeof s);
    }
    DWORD protect; VirtualProtect(block, 0x1000, PAGE_EXECUTE_READ, &protect); FlushInstructionCache(GetCurrentProcess(), block, 0x1000);
    g_lwNj.geometry = (volatile uintptr_t*)(block + 0x1000);
    for (int k = 0; k < 2; k++)
    {
        int64_t rel = (int64_t)(uintptr_t)(block + 0x20 * k) - (int64_t)(e + site[k] + 5);
        uint8_t call[5] = {0xE8, 0, 0, 0, 0}; int32_t r32 = (int32_t)rel; memcpy(call + 1, &r32, 4);
        if (InRel32(rel) && !ApiWrite(e + site[k], call, sizeof call)) continue;
        if (k) ApiWrite(e + site[0], was, sizeof was); /* back to the original */
        return 0;
    }
    return 1;
}

static int LwNjHooks(uintptr_t e)
{
    if (LoaderHook("nav job: Havok quit", e + 0x99EA00u, (void*)LwNjQuit, &g_lwNj.quit) == 0 && g_lwNj.quit) g_lwNj.quitHooked = 1;
    else Note("living world: nav job: Havok's quit could not be hooked; the cache stays off");
    if (LoaderHook("nav job: job start", e + 0x7B7590u, (void*)LwNjStart, &g_lwNj.start) || !g_lwNj.start) return 0;
    return !LoaderHook("nav job: nav tick", e + 0x7B8A10u, (void*)LwNjTick, &g_lwNj.tick) && g_lwNj.tick;
}

/* After LwInstallNavTime. Complex levels run mode 2 (research builds: HELLFORGE_LW_NAVJOB=0|1|2 sets it); every other
 * level mode 0, whose hooks pass straight to the game's code. */
static void LwInstallNavJob(void)
{
    static int done; char v[4] = "2", t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_NAVJOB", v, sizeof v) == 0) strcpy(v, "2");
    if (GetEnvironmentVariableA("HELLFORGE_LW_NAVTIME", t, sizeof t) > 0 && t[0] == '1')
    { Note("living world: nav job lever not installed: the test instrument HELLFORGE_LW_NAVTIME=1 hooks 0x7B8A10 and 0x7B7590 too; run without it to use the lever"); return; }
    uintptr_t e = ApiModuleBase("helldivers2.exe", NULL);
    if (e == 0) e = (uintptr_t)GetModuleHandleA(NULL);
    float m[4] = {0};
    int ok = !strcmp(g_build, "25480438") && !SafeRead(e + 0x16AA6E0u, m, sizeof m) && m[0] == 2.0f && m[1] == 2.0f;
    for (size_t i = 0; ok && i < sizeof g_lwNjPins / sizeof g_lwNjPins[0]; i++)
        if (!ExeCodeMatches(e + g_lwNjPins[i].rva, g_lwNjPins[i].bytes)) { Note("living world: nav job: exe+0x%X is not the reviewed code", g_lwNjPins[i].rva); ok = 0; }
    if (!ok) { Note("living world: nav job lever not installed: not the reviewed build"); return; }
    g_lwNj.exe = e; g_lwNj.notedAt = GetTickCount64();
    g_lwNj.cell = (LwNjCellFn)(e + 0x7ACDD0u); g_lwNj.gather = (LwNjGatherFn)(e + 0x7ADA50u);
    g_lwNj.reserve = (LwNjReserveFn)(e + 0x99A2D0u); g_lwNj.arrFree = (LwNjArrFreeFn)(e + 0x99A920u);
#ifdef CO_RESEARCH /* the completions' counters rewrite two calls on every level: research builds only */
    if (!LwNjEndSites(e)) Note("living world: nav job: the completion frees could not be redirected; completions are not counted");
#else
    (void)LwNjEndSites;
#endif
    if (!LwNjHooks(e)) { Note("living world: nav job lever not installed: a hook could not go in"); return; }
    g_lwNj.liveMode = v[0] >= '0' && v[0] <= '2' ? v[0] - '0' : 0;
    g_lwNj.installed = 1;
    LwNjLive(LwLive());
    Note("living world: nav job lever installed (mode %d on complex levels, 0 on any other; the nav thread applies it at its next tick)", g_lwNj.liveMode);
}
