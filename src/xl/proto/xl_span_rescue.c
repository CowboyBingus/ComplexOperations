/* Extra large maps: rpmalloc spans that stamp churn strands. A stamp's layer units are made by the world's async spawn
 * jobs on the pool workers (0x12D600 -> jobs 0x12D3E0 -> 0x1A3930) and destroyed on the game thread (0x12D600 ->
 * 0x1AE980), so their blocks return to the workers' spans as deferred frees (0x66A360). A span that was full reaches
 * its owner again only once every block in it is free (0x66A37E): one live block keeps the rest from any reuse, and
 * the owner maps new spans instead (mp-city2: about 73 KB a stamp that left, 1.36 GB in 6.7 min). A pass moves each
 * such span back to its owner's partial list, on the owner's thread, as the owner's own free does with a full span
 * (0x66A3C6..0x66A3F9) and as its allocation takes a deferred list (0x6698C0..0x6698E0). A thread takes its list at a game
 * function rpmalloc never calls: the trim, the game thread's tick, the renderer's message dispatch and Havok's task step
 * (the renderer and Havok's workers never trim; the step also runs on any thread that waits on a Havok task). Included
 * after the probe. */
#include "xl_span_rescue_pins.h"

#define SR_MAP_AT 0x27B8C60u        /* rpmalloc's span map and unmap pointers; init stores the defaults (0x66AABF) */
#define SR_UNMAP_AT 0x27B8C68u
#define SR_MAP_DEFAULT 0x667850u
#define SR_UNMAP_DEFAULT 0x667920u
#define SR_INITIALISED 0x2360FC4u
#define SR_SIZES 0x27B0DF0u         /* {u32 block size, u16 count, u16 class}[96] */
#define SR_CONFIG_REST 0x27B8C70u    /* the two config pointers after unmap; init zeroes them (0x66AAE4) */
#define SR_REGIONS 0x2360D04u       /* regions mapped now: +1 at each map (0x667B3E..0x6691CA), -1 at a release (0x667D1C) */
#define SR_TRIM 0x6302F0u
#define SR_WORKER_TRIMS 0x6253CEu   /* return address of the pool worker's trim(0) when its +0x7D is set (0x6253C9) */
#define SR_APP 0x1A10208u           /* [app+0x318] the pool: workers [+0x90], count [+0x88], trim flag [w+0x7D] */
#define SR_RENDER_STEP 0x113010u    /* the renderer's message dispatch: only its thread's loop calls it (0x113EEE) */
#define SR_TASK_STEP 0x99DB30u      /* Havok's router step: only the task queue loops call it, once an iteration, on
                                     * Havok's workers and on threads that wait on a task (0x142F158 +0x60 = 0xA2BBB0) */
#define SR_SPAN 0x10000u
#define SR_CLASSES 96u
#define SR_INVALID UINT64_MAX
/* span_t: free list +0, blocks +8, class +0xC, free list limit +0x10, used +0x14, deferred list +0x18 (-1 while a free
 * pushes), deferred count +0x20, block size +0x24, heap +0x40, next +0x48, prev +0x50. heap_t: owner TEB +0, partial
 * list of class c +0x10 + 0x10 c, full span count +0x718, finalising +0x73C. */
#define SR_TOP (1u << 15)           /* a leaf for each 4 GiB of the user address space */
#define SR_LEAF_WORDS 1024u         /* a bit for each 64 KiB in it */
#define SR_HEAPS 64
#define SR_PER_HEAP 4096u
#define SR_EVERY 256u               /* a pass after this many stamps removed, */
#define SR_GAP_MS 20000ull          /* at most one in this time, */
#define SR_TAKE_MS 5000ull          /* and each heap's thread has this long to take its list */

enum { SR_IDLE, SR_SCANNING, SR_PUBLISHED };
enum { SR_SLOT_FREE, SR_SLOT_READY, SR_SLOT_BUSY, SR_SLOT_DONE, SR_SLOT_GONE };
enum { SR_VIA_TRIM, SR_VIA_TICK, SR_VIA_RENDER, SR_VIA_TASK, SR_VIAS };
typedef void* (*SrMapFn)(size_t size, size_t* offset);
typedef void (*SrUnmapFn)(void* address, size_t size, size_t offset, size_t release);
typedef uint64_t (*SrTrimFn)(uint64_t global);
typedef uint64_t (*SrRenderFn)(uint64_t renderer, uint64_t message, uint64_t payload);
typedef uint64_t (*SrTaskFn)(uint64_t router);

typedef struct
{
    volatile uintptr_t heap;
    volatile LONG state;
    uint32_t count, rescued, changed, via;
    LONG64 found, bytes, cls[3]; /* free block bytes: in the spans found, made reusable, of classes 34-36 */
    uint64_t* spans;
} SrSlot;

#ifdef CO_TEST
static void (*volatile g_srTestPause)(void); /* the tests widen the time a header read takes */
#define SR_TEST_PAUSE() do { if (g_srTestPause != NULL) g_srTestPause(); } while (0)
#else
#define SR_TEST_PAUSE() do { } while (0)
#endif
static SRWLOCK g_srLock = SRWLOCK_INIT; /* shared while span headers are read; exclusive while a span's bit is cleared */
static struct
{
    int on;                              /* 1 on, -1 refused */
    int beside;                          /* the map and unmap wrapped are HellForge's span-rescue's, not rpmalloc's */
    uintptr_t exe;
    SrMapFn map; SrUnmapFn unmap; SrTrimFn trim; SrRenderFn render; SrTaskFn task;
    volatile uint64_t* volatile* top;
    volatile LONG64 registered;
    uint32_t size[SR_CLASSES], count[SR_CLASSES];
    SrSlot slot[SR_HEAPS];
    HANDLE wake;
    int sync;                            /* the scan runs on the caller (tests) */
    volatile LONG scanned;
    int phase;
    ULONGLONG nextAt, deadline, firstAt;
    uint32_t removedAt, passRemoved, passLeft, passes, firstRemoved;
    uint32_t seen, overRoom; double scanMs;
    LONG64 fragFirst, fragLast; int fragOn;
    volatile LONG quit;                  /* the scan thread ends (a refused install) */
    HANDLE thread;
    volatile LONG gameTrims;             /* trims with `global` set: every game store of a worker's +0x7D follows one */
    struct { volatile uintptr_t heap; volatile LONG seen; } worker[SR_HEAPS];
    volatile LONG64 waitMax, trimTicks, trimMax; volatile LONG trimsRun, trimsSkipped;
    volatile LONG64 takeTicks[SR_VIAS], takeMax[SR_VIAS]; /* a slot's work, by the hook that took it: the cost on that thread */
    struct { uint32_t found, rescued, changed, untaken, heaps, threads, via[SR_VIAS]; LONG64 foundBytes, bytes, cls[3]; } last;
} g_sr;

/* ---- The registry: every 64 KiB region rpmalloc maps, mapped now ---------------------------------------------------- */
static volatile uint64_t* SrLeaf(uintptr_t span, int make)
{
    uintptr_t t = span >> 32;
    if (g_sr.top == NULL || t >= SR_TOP) return NULL;
    volatile uint64_t* leaf = g_sr.top[t];
    if (leaf != NULL || !make) return leaf;
    volatile uint64_t* fresh = (volatile uint64_t*)VirtualAlloc(NULL, SR_LEAF_WORDS * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (fresh == NULL) return NULL;
    leaf = (volatile uint64_t*)InterlockedCompareExchangePointer((void* volatile*)&g_sr.top[t], (void*)fresh, NULL);
    if (leaf == NULL) return fresh;
    VirtualFree((void*)fresh, 0, MEM_RELEASE);
    return leaf;
}
static int SrMarked(uintptr_t span)
{
    volatile uint64_t* leaf = SrLeaf(span, 0);
    uint32_t i = (uint32_t)(span >> 16) & 0xFFFFu;
    return leaf != NULL && (leaf[i >> 6] >> (i & 63)) & 1;
}
/* Only a region of one span can hold small and medium blocks (one span per map, 0x66AC85); the bit goes in after the
 * map returns, so a reader under the lock finds the region mapped. */
static void* SrMapped(size_t size, size_t* offset)
{
    void* p = g_sr.map(size, offset);
    uintptr_t at = (uintptr_t)p;
    volatile uint64_t* leaf = (p != NULL && size == SR_SPAN && (at & (SR_SPAN - 1)) == 0) ? SrLeaf(at, 1) : NULL;
    if (leaf != NULL)
    {
        uint32_t i = (uint32_t)(at >> 16) & 0xFFFFu;
        LONG64 bit = (LONG64)1 << (i & 63);
        if (!(InterlockedOr64((volatile LONG64*)&leaf[i >> 6], bit) & bit)) InterlockedIncrement64(&g_sr.registered);
    }
    return p;
}
/* The bit goes before the region does, under the lock: no reader is inside a header that is being unmapped. Only the
 * map sets a bit, and only for a region it has just mapped, so a clear bit here stays clear. */
static void SrUnmapped(void* address, size_t size, size_t offset, size_t release)
{
    uintptr_t span = (uintptr_t)address & ~(uintptr_t)(SR_SPAN - 1);
    if (SrMarked(span))
    {
        volatile uint64_t* leaf = SrLeaf(span, 0);
        uint32_t i = (uint32_t)(span >> 16) & 0xFFFFu;
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        AcquireSRWLockExclusive(&g_srLock);
        QueryPerformanceCounter(&b);
        for (LONG64 m = g_sr.waitMax; b.QuadPart - a.QuadPart > m; m = g_sr.waitMax)
            if (InterlockedCompareExchange64(&g_sr.waitMax, b.QuadPart - a.QuadPart, m) == m) break;
        InterlockedAnd64((volatile LONG64*)&leaf[i >> 6], ~((LONG64)1 << (i & 63)));
        ReleaseSRWLockExclusive(&g_srLock);
        InterlockedDecrement64(&g_sr.registered);
    }
    g_sr.unmap(address, size, offset, release);
}

/* ---- A stranded span ---------------------------------------------------------------------------------------------- */
/* Its heap when the span is full (no free list, every block handed out) and holds deferred blocks but not all of them
 * (all of them: it is on its heap's +0x710 list or about to be), else 0. The heap is read last: a span another heap
 * sets up gets its heap before its lists (0x669949, 0x669962..0x669969), and x86 keeps loads in order. A span in a
 * cache fails: the owner's last free left a free list (0x66A40C), or every block came back deferred. */
static uintptr_t SrStranded(uintptr_t span, uint32_t* deferredCount)
{
    uint32_t n = *(volatile uint32_t*)(span + 0x20);
    uint64_t freeList = *(volatile uint64_t*)span;
    uint32_t limit = *(volatile uint32_t*)(span + 0x10), count = *(volatile uint32_t*)(span + 8), cls = *(volatile uint32_t*)(span + 0xC);
    uint32_t size = *(volatile uint32_t*)(span + 0x24);
    uint64_t deferred = *(volatile uint64_t*)(span + 0x18);
    uintptr_t heap = (uintptr_t)*(volatile uint64_t*)(span + 0x40);
    if (cls >= SR_CLASSES || count == 0 || count != g_sr.count[cls] || size != g_sr.size[cls] || freeList != 0 || limit < count ||
        n == 0 || n >= count || deferred == 0 || deferred == SR_INVALID || heap == 0) return 0;
    if (deferredCount != NULL) *deferredCount = n;
    return heap;
}

/* On the heap's owner thread, outside any allocator call: the span to the head of its class's partial list with its
 * deferred blocks as its free list. The lock is the deferred list's own -1 (0x66A360); under it the count is final,
 * and a count at the block count means the span went to +0x710 (0x66A37E). 1 done, 0 not stranded, -1 changed. */
static int SrRescue(uintptr_t heap, uintptr_t span, uint32_t* blocks)
{
    if (SrStranded(span, NULL) != heap) return 0;
    volatile LONG64* deferred = (volatile LONG64*)(span + 0x18);
    LONG64 list = (LONG64)SR_INVALID;
    for (int i = 0; i < 1024 && (list = InterlockedExchange64(deferred, (LONG64)SR_INVALID)) == (LONG64)SR_INVALID; i++) YieldProcessor();
    if (list == (LONG64)SR_INVALID) return -1; /* another thread's free still holds it: nothing was written */
    uint32_t n = *(volatile uint32_t*)(span + 0x20), count = *(volatile uint32_t*)(span + 8);
    if (list == 0 || n == 0 || n >= count) { InterlockedExchange64(deferred, list); return -1; }
    *(volatile uint64_t*)span = (uint64_t)list;
    *(volatile uint32_t*)(span + 0x14) = count - n;
    *(volatile uint32_t*)(span + 0x20) = 0;
    InterlockedExchange64(deferred, 0);
    uint32_t cls = *(volatile uint32_t*)(span + 0xC);
    volatile uint64_t* head = (volatile uint64_t*)(heap + 0x10 + 0x10 * (uintptr_t)cls);
    uint64_t old = *head;
    if (old != 0) *(volatile uint64_t*)((uintptr_t)old + 0x50) = span;
    *(volatile uint64_t*)(span + 0x48) = old;
    *head = span;
    *(volatile uint64_t*)(heap + 0x718) -= 1;
    *blocks = n;
    return 1;
}

#ifdef CO_TEST
static uintptr_t g_srTestHeap;
#endif
/* The calling thread's heap, as the trim finds it (0x6302F7..0x630317). */
static uintptr_t SrHeapHere(void)
{
#ifdef CO_TEST
    if (g_srTestHeap != 0) return g_srTestHeap;
#endif
    uintptr_t tls = 0;
    __asm__ volatile("movq %%gs:0x58, %0" : "=r"(tls));
    uintptr_t block = tls != 0 ? *(volatile uintptr_t*)tls : 0;
    return block != 0 ? *(volatile uintptr_t*)(block + 0x4350) : 0;
}

/* The calling thread's list, if the pass has one for its heap. `via` names the hook, for the pass note. */
static void SrRescueHere(int via)
{
    if (g_sr.phase != SR_PUBLISHED) return;
    uintptr_t heap = SrHeapHere();
    if (heap == 0) return;
    for (int k = 0; k < SR_HEAPS; k++)
    {
        SrSlot* s = &g_sr.slot[k];
        if (s->heap != heap) continue;
        if (InterlockedCompareExchange(&s->state, SR_SLOT_BUSY, SR_SLOT_READY) != SR_SLOT_READY) return;
        if (s->heap != heap) { InterlockedExchange(&s->state, SR_SLOT_READY); return; }
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        int mine = *(volatile uint64_t*)heap == (uint64_t)(uintptr_t)NtCurrentTeb() && *(volatile uint32_t*)(heap + 0x73C) == 0;
        s->via = (uint32_t)via;
        for (uint32_t i = 0; mine && i < s->count; i += 64)
        {
            AcquireSRWLockShared(&g_srLock); /* a span whose bit is set stays mapped until the lock is let go */
            for (uint32_t j = i; j < s->count && j < i + 64; j++)
            {
                uintptr_t span = (uintptr_t)s->spans[j];
                uint32_t n = 0;
                if (!SrMarked(span)) { s->changed++; continue; }
                SR_TEST_PAUSE();
                if (SrRescue(heap, span, &n) != 1) { s->changed++; continue; }
                uint32_t cls = *(volatile uint32_t*)(span + 0xC), size = *(volatile uint32_t*)(span + 0x24);
                s->rescued++; s->bytes += (LONG64)n * size;
                if (cls >= 34 && cls <= 36) s->cls[cls - 34] += (LONG64)n * size;
            }
            ReleaseSRWLockShared(&g_srLock);
        }
        if (!mine) s->changed += s->count;
        QueryPerformanceCounter(&b);
        LONG64 took = b.QuadPart - a.QuadPart;
        InterlockedAdd64(&g_sr.takeTicks[via], took);
        for (LONG64 m = g_sr.takeMax[via]; took > m; m = g_sr.takeMax[via])
            if (InterlockedCompareExchange64(&g_sr.takeMax[via], took, m) == m) break;
        InterlockedExchange(&s->state, SR_SLOT_DONE);
        return;
    }
}

/* A pool worker's trim that only the mod asked for: none of the game's +0x7D stores since this thread's last one.
 * The game stores 1 only right after a trim(1) on its own thread (0x8BDA4, 0x32460F, 0x62766B), which this hook
 * counts before it runs; so an unchanged count means the flag is the pass's. A thread seen first trims. */
static int SrWorkerSkips(void)
{
    uintptr_t heap = SrHeapHere();
    LONG game = g_sr.gameTrims;
    for (int k = 0; heap != 0 && k < SR_HEAPS; k++)
    {
        if (g_sr.worker[k].heap == 0 && InterlockedCompareExchangePointer((void* volatile*)&g_sr.worker[k].heap, (void*)heap, NULL) == NULL)
        { g_sr.worker[k].seen = game; return 0; } /* a thread not seen before: its flag may be the game's */
        if (g_sr.worker[k].heap != heap) continue;
        if (g_sr.worker[k].seen == game) return 1;
        g_sr.worker[k].seen = game;
        return 0;
    }
    return 0;
}
/* Every trim runs on its own thread outside any allocator call (its ten callers, none in rpmalloc); a pool worker's
 * comes after its jobs when its +0x7D is set (0x6253BF) and its result is not read (0x6253CE, 0x625384). The
 * argument is a byte (0x630310). Skipped, the worker keeps its caches as when nothing asked it to trim. */
static uint64_t SrTrimmed(uint64_t global)
{
    uintptr_t from = (uintptr_t)__builtin_return_address(0);
    SrRescueHere(SR_VIA_TRIM);
    if ((uint8_t)global != 0) InterlockedIncrement(&g_sr.gameTrims);
    /* Beside HellForge's span-rescue a flag may be its pass's (its SrWakeWorkers): its trim hook, after this one in the
     * chain, takes the worker's list only if the trim goes on. */
    if (g_sr.on != 1 || g_sr.beside || from != g_sr.exe + SR_WORKER_TRIMS) return g_sr.trim(global);
    if (SrWorkerSkips()) { InterlockedIncrement(&g_sr.trimsSkipped); return 0; }
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    uint64_t r = g_sr.trim(global);
    QueryPerformanceCounter(&b);
    InterlockedIncrement(&g_sr.trimsRun);
    InterlockedAdd64(&g_sr.trimTicks, b.QuadPart - a.QuadPart);
    for (LONG64 m = g_sr.trimMax; b.QuadPart - a.QuadPart > m; m = g_sr.trimMax)
        if (InterlockedCompareExchange64(&g_sr.trimMax, b.QuadPart - a.QuadPart, m) == m) break;
    return r;
}

/* The renderer and Havok's workers never trim (no trim caller is on their loops), so they take their lists here: between
 * messages, and between Havok tasks on any thread that runs them (a waiter on a task too). Arguments and result pass
 * through. */
static uint64_t SrRendered(uint64_t renderer, uint64_t message, uint64_t payload)
{
    SrRescueHere(SR_VIA_RENDER);
#if defined(CO_RESEARCH) || defined(CO_TEST)
    if (g_xrfViaSr) return XrfMessage(renderer, message, payload, g_sr.render); /* the render frame instrument's timing */
#endif
    return g_sr.render(renderer, message, payload);
}
static uint64_t SrTasked(uint64_t router)
{
    SrRescueHere(SR_VIA_TASK);
    return g_sr.task(router);
}

/* ---- The scan: the registry read on a thread of its own, the stranded spans listed by heap ------------------------ */
static SrSlot* SrSlotFor(uintptr_t heap)
{
    for (int k = 0; k < SR_HEAPS; k++) if (g_sr.slot[k].heap == heap) return &g_sr.slot[k];
    for (int k = 0; k < SR_HEAPS; k++)
        if (g_sr.slot[k].heap == 0) { g_sr.slot[k].heap = heap; return &g_sr.slot[k]; }
    return NULL;
}
static void SrScan(void)
{
    LARGE_INTEGER a, b, f;
    QueryPerformanceCounter(&a);
    uint32_t seen = 0, over = 0;
    for (uint32_t t = 0; g_sr.top != NULL && t < SR_TOP; t++)
    {
        volatile uint64_t* leaf = g_sr.top[t];
        for (uint32_t w = 0; leaf != NULL && w < SR_LEAF_WORDS; w++)
        {
            if (leaf[w] == 0) continue;
            AcquireSRWLockShared(&g_srLock);
            for (uint64_t bits = leaf[w]; bits != 0; bits &= bits - 1)
            {
                uintptr_t span = ((uintptr_t)t << 32) | ((uintptr_t)(w * 64 + (uint32_t)__builtin_ctzll(bits)) << 16);
                uint32_t n = 0;
                SR_TEST_PAUSE();
                uintptr_t heap = SrStranded(span, &n);
                seen++;
                if (heap == 0) continue;
                SrSlot* s = SrSlotFor(heap);
                if (s == NULL || s->count >= SR_PER_HEAP) { over++; continue; }
                s->spans[s->count++] = span;
                s->found += (LONG64)n * *(volatile uint32_t*)(span + 0x24);
            }
            ReleaseSRWLockShared(&g_srLock);
        }
    }
    QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f);
    g_sr.seen = seen; g_sr.overRoom = over;
    g_sr.scanMs = f.QuadPart > 0 ? (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart : 0.0;
    for (int k = 0; k < SR_HEAPS; k++)
        if (g_sr.slot[k].heap != 0) InterlockedExchange(&g_sr.slot[k].state, SR_SLOT_READY);
    InterlockedExchange(&g_sr.scanned, 1);
}
static DWORD WINAPI SrScanThread(void* wake)
{
    while (WaitForSingleObject((HANDLE)wake, INFINITE) == WAIT_OBJECT_0 && !g_sr.quit) SrScan();
    return 0;
}

/* Each pool worker trims, and so takes its list, after its jobs: the store trim-all makes (0x324637). */
static void SrWakeWorkers(void)
{
    uint64_t app = 0, pool = 0, workers = 0; uint32_t n = 0;
    if (SafeRead(g_sr.exe + SR_APP, &app, 8) || app == 0 || SafeRead((uintptr_t)app + 0x318, &pool, 8) || pool == 0 ||
        SafeRead((uintptr_t)pool + 0x88, &n, 4) || SafeRead((uintptr_t)pool + 0x90, &workers, 8) || workers == 0 || n > 64) return;
    for (uint32_t i = 0; i < n; i++)
    {
        uint64_t w = 0;
        if (!SafeRead((uintptr_t)workers + 8ull * i, &w, 8) && w != 0) *(volatile uint8_t*)((uintptr_t)w + 0x7D) = 1;
    }
}

/* Small and medium bytes outside live blocks, every heap, and classes 34-36's (research: the trend per stamp). */
static int SrFrag(LONG64* all, LONG64* c3436)
{
    uintptr_t head[47];
    *all = *c3436 = 0;
    if (SafeRead(g_sr.exe + MP_RP_HEAPS, head, sizeof head)) return 0;
    static uint8_t use[SR_CLASSES * 0x2C];
    for (int b = 0; b < 47; b++)
        for (uintptr_t h = head[b], chain = 0; h != 0 && chain < 64; chain++)
        {
            if (SafeRead(h + 0xFC0, use, sizeof use)) break;
            for (uint32_t c = 0; c < SR_CLASSES; c++)
            {
                int32_t blocks, spans; memcpy(&blocks, use + 0x2C * c, 4); memcpy(&spans, use + 0x2C * c + 0x10, 4);
                LONG64 live = blocks > 0 ? (LONG64)blocks * g_sr.size[c] : 0, in = spans > 0 ? (LONG64)spans * SR_SPAN : 0;
                LONG64 frag = in > live ? in - live : 0;
                *all += frag;
                if (c >= 34 && c <= 36) *c3436 += frag;
            }
            if (SafeRead(h + 0x728, &h, 8)) break;
        }
    return 1;
}

/* ---- The pass, on the game thread ----------------------------------------------------------------------------- */
/* 1 once every list is taken or its time is up and none is being worked on; the slots are then empty again. */
static int SrClose(ULONGLONG now)
{
    int busy = 0, ready = 0;
    for (int k = 0; k < SR_HEAPS; k++)
    {
        LONG s = g_sr.slot[k].state;
        busy += s == SR_SLOT_BUSY; ready += s == SR_SLOT_READY;
    }
    if (busy || (ready && now < g_sr.deadline)) return 0;
    for (int k = 0; k < SR_HEAPS; k++)
        if (g_sr.slot[k].heap != 0 && InterlockedCompareExchange(&g_sr.slot[k].state, SR_SLOT_GONE, SR_SLOT_READY) == SR_SLOT_BUSY) return 0;
    memset(&g_sr.last, 0, sizeof g_sr.last);
    for (int k = 0; k < SR_HEAPS; k++)
    {
        SrSlot* s = &g_sr.slot[k];
        if (s->heap == 0) continue;
        g_sr.last.heaps++; g_sr.last.found += s->count; g_sr.last.foundBytes += s->found;
        if (s->state == SR_SLOT_GONE) g_sr.last.untaken += s->count;
        else { g_sr.last.threads++; g_sr.last.rescued += s->rescued; g_sr.last.changed += s->changed; g_sr.last.via[s->via < SR_VIAS ? s->via : 0]++; }
        g_sr.last.bytes += s->bytes;
        for (int c = 0; c < 3; c++) g_sr.last.cls[c] += s->cls[c];
    }
    return 1;
}
static void SrPassNote(uint32_t removed, ULONGLONG now)
{
    const double mb = 1.0 / 1048576.0;
    g_sr.passes++;
    Note("difficulty: stranded spans: pass %u after %u stamps removed (+%u, %u left the window): %lld spans of 64 KiB registered, %u read in %.1f ms; "
        "%u stranded in %u heaps (%.1f MB free in them); %u rescued (%.1f MB: class 34 %.1f, 35 %.1f, 36 %.1f) on %u threads, %u changed first, %u in heaps whose thread did not come, %u over the room; "
        "lists taken at trims %u, the tick %u, render messages %u, Havok task steps %u",
        g_sr.passes, removed, removed - g_sr.removedAt, g_sr.passLeft, (long long)g_sr.registered, g_sr.seen, g_sr.scanMs, g_sr.last.found, g_sr.last.heaps,
        (double)g_sr.last.foundBytes * mb, g_sr.last.rescued, (double)g_sr.last.bytes * mb, (double)g_sr.last.cls[0] * mb, (double)g_sr.last.cls[1] * mb,
        (double)g_sr.last.cls[2] * mb, g_sr.last.threads, g_sr.last.changed, g_sr.last.untaken, g_sr.overRoom, g_sr.last.via[SR_VIA_TRIM],
        g_sr.last.via[SR_VIA_TICK], g_sr.last.via[SR_VIA_RENDER], g_sr.last.via[SR_VIA_TASK]);
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const double ms = f.QuadPart > 0 ? 1000.0 / (double)f.QuadPart : 0.0;
    uint32_t regions = 0;
    SafeRead(g_sr.exe + SR_REGIONS, &regions, 4);
    LONG run = InterlockedExchange(&g_sr.trimsRun, 0), skipped = InterlockedExchange(&g_sr.trimsSkipped, 0);
    LONG64 wait = InterlockedExchange64(&g_sr.waitMax, 0), spent = InterlockedExchange64(&g_sr.trimTicks, 0), longest = InterlockedExchange64(&g_sr.trimMax, 0);
    LONG64 take[SR_VIAS], most[SR_VIAS];
    for (int v = 0; v < SR_VIAS; v++) { take[v] = InterlockedExchange64(&g_sr.takeTicks[v], 0); most[v] = InterlockedExchange64(&g_sr.takeMax[v], 0); }
    Note("difficulty: stranded spans: pass %u cost: an unmap waited at most %.3f ms for the scan; pool worker trims since the last pass: %ld run (%.1f ms, longest %.1f ms), "
        "%ld skipped (only the pass asked); %lld of %u rpmalloc regions registered; taking the lists: at trims %.3f ms (longest %.3f ms), at the tick %.3f ms "
        "(longest %.3f ms), at render messages %.3f ms (longest %.3f ms), at Havok task steps %.3f ms (longest %.3f ms)",
        g_sr.passes, (double)wait * ms, (long)run, (double)spent * ms, (double)longest * ms, (long)skipped, (long long)g_sr.registered, regions,
        (double)take[SR_VIA_TRIM] * ms, (double)most[SR_VIA_TRIM] * ms, (double)take[SR_VIA_TICK] * ms, (double)most[SR_VIA_TICK] * ms,
        (double)take[SR_VIA_RENDER] * ms, (double)most[SR_VIA_RENDER] * ms, (double)take[SR_VIA_TASK] * ms, (double)most[SR_VIA_TASK] * ms);
    LONG64 frag = 0, c3436 = 0;
    if (!MemProbeOn() || !SrFrag(&frag, &c3436)) return;
    if (!g_sr.fragOn) { g_sr.fragOn = 1; g_sr.fragFirst = g_sr.fragLast = frag; g_sr.firstRemoved = g_sr.removedAt; g_sr.firstAt = now; }
    uint32_t since = removed - g_sr.removedAt, all = removed - g_sr.firstRemoved;
    Note("difficulty: memprobe: stranded spans: fragmentation %.0f MB (classes 34-36 %.0f MB) at %u stamps removed; since the last pass %+.0f MB over %u removed "
        "(%.3f MB a stamp), stranded found %.3f MB a stamp; since the first pass %+.0f MB over %u removed (%.3f MB a stamp) in %.0f s",
        (double)frag * mb, (double)c3436 * mb, removed, (double)(frag - g_sr.fragLast) * mb, since,
        since ? (double)(frag - g_sr.fragLast) * mb / since : 0.0, since ? (double)g_sr.last.foundBytes * mb / since : 0.0,
        (double)(frag - g_sr.fragFirst) * mb, all, all ? (double)(frag - g_sr.fragFirst) * mb / all : 0.0, (double)(now - g_sr.firstAt) / 1000.0);
    g_sr.fragLast = frag;
    for (int shown = 0; shown < 4; shown++)
    {
        SrSlot* best = NULL;
        for (int k = 0; k < SR_HEAPS; k++)
            if (g_sr.slot[k].heap != 0 && g_sr.slot[k].bytes > 0 && (best == NULL || g_sr.slot[k].bytes > best->bytes)) best = &g_sr.slot[k];
        if (best == NULL) break;
        uint64_t owner = 0, tid = 0; uint32_t id = 0;
        SafeRead(best->heap + 0x738, &id, 4);
        if (!SafeRead(best->heap, &owner, 8) && owner != 0 && owner != UINT64_MAX) SafeRead((uintptr_t)owner + 0x48, &tid, 8);
        Note("difficulty: memprobe: stranded spans: heap %u (thread %u \"%s\"): %u of %u spans rescued, %.1f MB of free blocks", id, (uint32_t)tid,
            MemProbeThreadName((DWORD)tid), best->rescued, best->count, (double)best->bytes * mb);
        best->bytes = 0;
    }
}
static void SrReset(void)
{
    for (int k = 0; k < SR_HEAPS; k++)
    {
        SrSlot* s = &g_sr.slot[k];
        InterlockedExchange(&s->state, SR_SLOT_FREE);
        s->heap = 0; s->count = s->rescued = s->changed = s->via = 0; s->found = s->bytes = 0; memset(s->cls, 0, sizeof s->cls);
    }
}

/* removed: the streamer's removals so far this level; left: the window's. */
static void SrStep(uint32_t removed, uint32_t left, ULONGLONG now, int inPlay)
{
    if (g_sr.on != 1) return;
    if (g_sr.phase == SR_IDLE)
    {
        if (removed < g_sr.removedAt) g_sr.removedAt = removed; /* another level */
        if (!inPlay || removed - g_sr.removedAt < SR_EVERY || now < g_sr.nextAt) return;
        g_sr.phase = SR_SCANNING; g_sr.passRemoved = removed; g_sr.passLeft = left;
        if (g_sr.sync) SrScan(); else SetEvent(g_sr.wake);
        return;
    }
    if (g_sr.phase == SR_SCANNING)
    {
        if (!InterlockedCompareExchange(&g_sr.scanned, 0, 1)) return;
        g_sr.phase = SR_PUBLISHED; g_sr.deadline = now + SR_TAKE_MS;
        SrRescueHere(SR_VIA_TICK);  /* the game thread's own heap: the tick is outside any allocator call */
        SrWakeWorkers();
        return;
    }
    if (!SrClose(now)) return;
    SrPassNote(g_sr.passRemoved, now);
    SrReset();
    g_sr.removedAt = g_sr.passRemoved; g_sr.nextAt = now + SR_GAP_MS; g_sr.phase = SR_IDLE;
}

static void SpanRescueTick(void)
{
    if (g_sr.on != 1) return;
    int inPlay = g_streamInstallState == 1 && g_stream.captured && g_stream.generatorDone && g_worldLevelLive == g_stream.level &&
        g_world.live > 1.0f && g_win.on && WorldStreamInMission();
    SrStep(g_stream.removed, g_win.left, GetTickCount64(), inPlay);
}

/* ---- Install: once, with the streamer, before the ship's and the mission's spans are mapped ---------------------- */
#ifdef CO_TEST
static void* g_srTestHellForge[2]; /* the tests' stand-ins for HellForge's span map and unmap wrappers */
#endif
/* HellForge 0.5's span-rescue (perf_span_rescue.c, from the loader once the game has loaded) wraps these same two
 * pointers the same way: its map registers a 64 KiB region after rpmalloc's map, its unmap clears the bit under its own
 * lock before rpmalloc's unmap, and it rescues only when a load ends. The mod's wrappers then go around its, so both
 * registries see every span; both rescues run on the heap's own thread, outside any allocator call, and each checks
 * the span is still stranded under its deferred list's lock, so a span one of them moved is left alone by the other. */
static int SrHellForges(void* fn)
{
#ifdef CO_TEST
    if (fn != NULL && (fn == g_srTestHellForge[0] || fn == g_srTestHellForge[1])) return 1;
#endif
    HMODULE m = NULL;
    wchar_t path[MAX_PATH];
    if (fn == NULL || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m))
        return 0;
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 0;
    const wchar_t* base = path;
    for (const wchar_t* c = path; *c; c++) if (*c == L'\\' || *c == L'/') base = c + 1;
    return _wcsicmp(base, L"hellforge_modloader.dll") == 0;
}
/* A refusal after the scan thread started: it ends at its next wake. */
static void SrRefused(const char* why)
{
    InterlockedExchange(&g_sr.quit, 1);
    if (g_sr.wake != NULL) SetEvent(g_sr.wake);
    Note("difficulty: stranded spans: off: %s", why);
}
/* One more thread kind's reach, once the trim hook is in: a refusal leaves that kind untaken, as before, and no hook. */
static int SrReach(const char* who, const SrPin* pins, size_t n, uint32_t rva, void* detour, void** original)
{
    for (size_t i = 0; i < n; i++)
        if (!ApiCodeMatches(g_sr.exe + pins[i].rva, pins[i].hex))
        { Note("difficulty: stranded spans: %s not reached: the code at 0x%X is not the reviewed one", who, pins[i].rva); return 0; }
    char what[96];
    snprintf(what, sizeof what, "extra large maps: stranded spans (%s)", who);
    if (LoaderHook(what, g_sr.exe + rva, detour, original) || *original == NULL)
    { *original = NULL; Note("difficulty: stranded spans: %s not reached: the hook was refused", who); return 0; }
    return 1;
}
static void SpanRescueInstall(void)
{
    if (g_sr.on != 0) return;
    g_sr.on = -1;
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
    if (exe == 0) { Note("difficulty: stranded spans: off: no helldivers2.exe"); return; }
    for (size_t i = 0; i < sizeof kSrPins / sizeof kSrPins[0]; i++)
        if (!ApiCodeMatches(exe + kSrPins[i].rva, kSrPins[i].hex))
        { Note("difficulty: stranded spans: off: rpmalloc's code at 0x%X is not the reviewed one", kSrPins[i].rva); return; }
    void* volatile* mapAt = (void* volatile*)(exe + SR_MAP_AT);
    void* volatile* unmapAt = (void* volatile*)(exe + SR_UNMAP_AT);
    void *map = *mapAt, *unmap = *unmapAt;
    int own = map == (void*)(exe + SR_MAP_DEFAULT) && unmap == (void*)(exe + SR_UNMAP_DEFAULT);
    if (*(volatile uint32_t*)(exe + SR_INITIALISED) != 1 || (!own && !(SrHellForges(map) && SrHellForges(unmap))))
    { Note("difficulty: stranded spans: off: rpmalloc is not set up with its own span map and unmap, or HellForge's span-rescue's"); return; }
    g_sr.beside = !own;
    /* no map-fail callback (0x6678C2 calls it from inside rpmalloc) and no other config: as init left them */
    if (*(volatile uint64_t*)(exe + SR_CONFIG_REST) != 0 || *(volatile uint64_t*)(exe + SR_CONFIG_REST + 8) != 0)
    { Note("difficulty: stranded spans: off: rpmalloc has a map-fail callback or another config set"); return; }
    for (uint32_t c = 0; c < SR_CLASSES; c++)
    {
        uint8_t e[8]; memcpy(e, (const void*)(exe + SR_SIZES + 8 * c), 8);
        memcpy(&g_sr.size[c], e, 4); g_sr.count[c] = (uint32_t)e[4] | (uint32_t)e[5] << 8;
    }
    if (g_sr.size[34] != 1040 || g_sr.count[34] != 62) { Note("difficulty: stranded spans: off: rpmalloc's size classes are not the reviewed ones"); return; }
    if (g_sr.top == NULL) g_sr.top = (volatile uint64_t* volatile*)VirtualAlloc(NULL, SR_TOP * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint64_t* spans = g_sr.slot[0].spans != NULL ? g_sr.slot[0].spans : (uint64_t*)VirtualAlloc(NULL, (size_t)SR_HEAPS * SR_PER_HEAP * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_sr.sync && g_sr.wake == NULL) g_sr.wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_sr.thread = (!g_sr.sync && g_sr.wake != NULL && g_sr.top != NULL && spans != NULL) ? CreateThread(NULL, 64 * 1024, SrScanThread, g_sr.wake, 0, NULL) : NULL;
    if (g_sr.top == NULL || spans == NULL || (!g_sr.sync && g_sr.thread == NULL)) { Note("difficulty: stranded spans: off: no memory or thread for the registry"); return; }
    if (g_sr.thread != NULL) SetThreadPriority(g_sr.thread, THREAD_PRIORITY_BELOW_NORMAL);
    for (int k = 0; k < SR_HEAPS; k++) g_sr.slot[k].spans = spans + (size_t)k * SR_PER_HEAP;
    g_sr.exe = exe;
    /* The unmap first: no span is registered before every unmap clears its bit. A refused swap puts back what went in;
     * bits set meanwhile stay, read by nothing. The trim hook goes in last, so a refusal leaves no hook. */
    g_sr.unmap = (SrUnmapFn)unmap; g_sr.map = (SrMapFn)map;
    if (InterlockedCompareExchangePointer(unmapAt, (void*)SrUnmapped, (void*)g_sr.unmap) != (void*)g_sr.unmap)
    { SrRefused("rpmalloc's span unmap changed while it was wrapped"); return; }
    if (InterlockedCompareExchangePointer(mapAt, (void*)SrMapped, (void*)g_sr.map) != (void*)g_sr.map)
    {
        InterlockedCompareExchangePointer(unmapAt, (void*)g_sr.unmap, (void*)SrUnmapped);
        SrRefused("rpmalloc's span map changed while it was wrapped");
        return;
    }
    if (LoaderHook("extra large maps: stranded spans (rpmalloc's trim)", exe + SR_TRIM, (void*)SrTrimmed, (void**)&g_sr.trim) || g_sr.trim == NULL)
    {
        InterlockedCompareExchangePointer(mapAt, (void*)g_sr.map, (void*)SrMapped);
        InterlockedCompareExchangePointer(unmapAt, (void*)g_sr.unmap, (void*)SrUnmapped);
        g_sr.trim = NULL;
        SrRefused("the trim could not be hooked");
        return;
    }
    int render = SrReach("the renderer", kSrRenderPins, sizeof kSrRenderPins / sizeof kSrRenderPins[0], SR_RENDER_STEP, (void*)SrRendered, (void**)&g_sr.render);
    int task = SrReach("threads that run Havok tasks", kSrTaskPins, sizeof kSrTaskPins / sizeof kSrTaskPins[0], SR_TASK_STEP, (void*)SrTasked, (void**)&g_sr.task);
    g_sr.on = 1;
    Note("difficulty: stranded spans: on: rpmalloc's 64 KiB spans are registered from now; after %u stamps removed (at most once in %llu s) a full span "
        "holding deferred blocks goes back to its heap's partial list on that heap's thread; %s; "
        "lists are taken at trims and the tick%s%s", SR_EVERY, SR_GAP_MS / 1000,
        g_sr.beside ? "beside HellForge's span-rescue (its passes when a load ends, these in play), every pool worker's trim runs"
                    : "a pool worker's trim only a pass asked for is skipped",
        render ? ", the renderer's messages" : "", task ? ", Havok's task steps" : "");
}

#ifdef CO_TEST
void CoXlTestSrReset(int sync)
{
    if (g_sr.top != NULL) for (uint32_t t = 0; t < SR_TOP; t++) if (g_sr.top[t] != NULL) memset((void*)g_sr.top[t], 0, SR_LEAF_WORDS * 8);
    volatile uint64_t* volatile* top = g_sr.top; uint64_t* spans = g_sr.slot[0].spans;
    memset(&g_sr, 0, sizeof g_sr); /* an earlier scan thread keeps waiting on its own event */
    g_sr.top = top; g_sr.sync = sync; g_srTestHeap = 0; g_srTestHellForge[0] = g_srTestHellForge[1] = NULL;
    if (spans != NULL) for (int k = 0; k < SR_HEAPS; k++) g_sr.slot[k].spans = spans + (size_t)k * SR_PER_HEAP;
}
int CoXlTestSrInstall(void) { SpanRescueInstall(); return g_sr.on; }
void CoXlTestSrHellForge(void* map, void* unmap) { g_srTestHellForge[0] = map; g_srTestHellForge[1] = unmap; }
/* The scan thread ended (1), runs (0), none (-1). */
int CoXlTestSrThreadEnded(unsigned ms) { return g_sr.thread == NULL ? -1 : WaitForSingleObject(g_sr.thread, ms) == WAIT_OBJECT_0; }
/* The registry's lock, as a reader (1) or the unmap (2) holds it; 0 lets go of what `mode` took. */
void CoXlTestSrLock(int mode, int take)
{
    if (mode == 1) { if (take) AcquireSRWLockShared(&g_srLock); else ReleaseSRWLockShared(&g_srLock); }
    else { if (take) AcquireSRWLockExclusive(&g_srLock); else ReleaseSRWLockExclusive(&g_srLock); }
}
void CoXlTestSrCosts(long long* out)
{
    long long v[] = {g_sr.trimsRun, g_sr.trimsSkipped, g_sr.gameTrims, g_sr.waitMax, g_sr.trimTicks, g_sr.takeTicks[SR_VIA_TRIM], g_sr.takeTicks[SR_VIA_TICK],
        g_sr.takeTicks[SR_VIA_RENDER], g_sr.takeTicks[SR_VIA_TASK], g_sr.takeMax[SR_VIA_RENDER], g_sr.takeMax[SR_VIA_TASK]};
    memcpy(out, v, sizeof v);
}
void CoXlTestSrHeap(uintptr_t heap) { g_srTestHeap = heap; }
void CoXlTestSrPause(void (*pause)(void)) { g_srTestPause = pause; }
int CoXlTestSrStep(uint32_t removed, uint32_t left, unsigned long long now, int inPlay) { SrStep(removed, left, now, inPlay); return g_sr.phase; }
uint64_t CoXlTestSrTrim(uint64_t global) { return SrTrimmed(global); }
int CoXlTestSrMarked(uintptr_t span) { return SrMarked(span); }
long long CoXlTestSrRegistered(void) { return g_sr.registered; }
int CoXlTestSrRescue(uintptr_t heap, uintptr_t span) { uint32_t n = 0; int r = SrRescue(heap, span, &n); return r == 1 ? (int)n : r; }
void CoXlTestSrLast(long long* out)
{
    long long v[] = {g_sr.last.found, g_sr.last.rescued, g_sr.last.changed, g_sr.last.untaken, g_sr.last.heaps, g_sr.last.threads, g_sr.last.foundBytes,
        g_sr.last.bytes, g_sr.last.cls[0], g_sr.passes, g_sr.seen, g_sr.last.via[SR_VIA_TRIM], g_sr.last.via[SR_VIA_TICK], g_sr.last.via[SR_VIA_RENDER],
        g_sr.last.via[SR_VIA_TASK]};
    memcpy(out, v, sizeof v);
}
#endif
