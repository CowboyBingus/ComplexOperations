/* Stranded rpmalloc spans: a full span that other threads freed blocks into is taken back by its own heap. The game's
 * own allocation (0x669AB0, 0x6697E0) and free (0x66A2F0) run in the fake exe against the rescue: the vanilla code
 * maps a new span while one live block holds the rest; after a pass the freed blocks are handed out again, the span
 * goes back when its last block does, and no block is lost or doubled while another thread frees into it. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_rpmalloc_bytes.h"

void CoXlTestSrReset(int sync);
int CoXlTestSrInstall(void);
void CoXlTestSrHellForge(void* map, void* unmap);
void CoXlTestSrHeap(uintptr_t heap);
int CoXlTestSrStep(uint32_t removed, uint32_t left, unsigned long long now, int inPlay);
uint64_t CoXlTestSrTrim(uint64_t global);
int CoXlTestSrMarked(uintptr_t span);
long long CoXlTestSrRegistered(void);
int CoXlTestSrRescue(uintptr_t heap, uintptr_t span);
void CoXlTestSrLast(long long* out);
int CoXlTestSrThreadEnded(unsigned ms);
void CoXlTestSrLock(int mode, int take);
void CoXlTestSrCosts(long long* out);
void CoXlTestSrPause(void (*pause)(void));
int CoXlTestMemProbeOn(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s (line %d)\n", g_case, #what, __LINE__); g_failed++; } } while (0)

#define MAP_AT 0x27B8C60u
#define UNMAP_AT 0x27B8C68u
#define MAP_DEFAULT 0x667850u
#define UNMAP_DEFAULT 0x667920u
#define INITIALISED 0x2360FC4u
#define TRIM 0x6302F0u
#define APP 0x1A10208u
#define HEAPS 0x27B1100u
#define CONFIG_REST 0x27B8C70u
#define REGIONS 0x2360D04u
#define WORKER_TRIM 0x6253C3u /* the test's entry: sub rsp, 8, then the worker's own `xor ecx, ecx; call trim` */
#define RENDER_STEP 0x113010u
#define TASK_STEP 0x99DB30u
#define CLASS 34u
#define BLOCK 1040u
#define COUNT 62u
enum { F_FOUND, F_RESCUED, F_CHANGED, F_UNTAKEN, F_HEAPS, F_THREADS, F_FOUND_BYTES, F_BYTES, F_CLASS34, F_PASSES, F_SEEN, F_VIA_TRIM, F_VIA_TICK,
    F_VIA_RENDER, F_VIA_TASK, F_N };

static uint8_t* g_exe;
typedef void* (*AllocFn)(uintptr_t heap, size_t size);
typedef void (*FreeFn)(uintptr_t span, void* block);
typedef void* (*MapFn)(size_t size, size_t* offset);
typedef void (*UnmapFn)(void* address, size_t size, size_t offset, size_t release);

/* ---- what the game's allocator calls outside the code under test ---------------------------------------------- */
static int g_newSpans, g_released, g_finalised, g_maps, g_unmaps, g_unmapSawBit, g_trims;
static uintptr_t g_releasedSpan;
static uint64_t g_trimArg;
/* While spans churn they come from an arena of 65,536 spans, taken in turn and decommitted when released, so a
 * released span's address stays unmapped for a long time and a late read of it faults. A rescued one may still be
 * linked in the heap's lists until the pass ends; the test releases it then. */
#define ARENA_SPANS 65536u
static int g_grave;
static CRITICAL_SECTION g_graveLock;
static uint8_t* g_arena;
static uint8_t g_arenaUsed[ARENA_SPANS];
static uint32_t g_arenaNext;
static void* g_graves[1 << 14];
static int g_graveCount;
static void* FakeMap(size_t size, size_t* offset)
{
    g_maps++;
    *offset = 0;
    if (!g_grave || size != 0x10000) return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void* p = NULL;
    EnterCriticalSection(&g_graveLock);
    for (uint32_t k = 0; k < ARENA_SPANS && p == NULL; k++, g_arenaNext = (g_arenaNext + 1) % ARENA_SPANS)
        if (!g_arenaUsed[g_arenaNext])
        {
            g_arenaUsed[g_arenaNext] = 1;
            p = VirtualAlloc(g_arena + (size_t)g_arenaNext * 0x10000, 0x10000, MEM_COMMIT, PAGE_READWRITE);
        }
    LeaveCriticalSection(&g_graveLock);
    return p;
}
static void ArenaFree(void* address)
{
    VirtualFree(address, 0x10000, MEM_DECOMMIT);
    g_arenaUsed[((uint8_t*)address - g_arena) / 0x10000] = 0;
}
static void FakeUnmap(void* address, size_t size, size_t offset, size_t release)
{
    g_unmaps++;
    g_unmapSawBit = CoXlTestSrMarked((uintptr_t)address);
    if (g_grave && (uint8_t*)address >= g_arena && (uint8_t*)address < g_arena + (size_t)ARENA_SPANS * 0x10000)
    {
        EnterCriticalSection(&g_graveLock);
        if (*(volatile uint64_t*)address != 0 && g_graveCount < (int)(sizeof g_graves / sizeof g_graves[0])) g_graves[g_graveCount++] = address;
        else ArenaFree(address);
        LeaveCriticalSection(&g_graveLock);
        return;
    }
    if (release) VirtualFree(address, 0, MEM_RELEASE);
}
static void Bury(void)
{
    EnterCriticalSection(&g_graveLock);
    for (int i = 0; i < g_graveCount; i++) ArenaFree(g_graves[i]);
    g_graveCount = 0;
    LeaveCriticalSection(&g_graveLock);
}
/* 0x668D30 (heap, heap class, span count, class): a span from the cache or a new map, here always the map through the
 * config pointer, as rpmalloc's own map call sites do. */
static uintptr_t TestNewSpan(uintptr_t heap, uintptr_t heapClass, uint32_t spanCount, uint32_t cls)
{
    g_newSpans++;
    size_t offset = 0;
    uint8_t* span = ((MapFn) * (void**)(g_exe + MAP_AT))(0x10000, &offset);
    uint32_t one = 1;
    if (span != NULL) memcpy(span + 0x2C, &one, 4);
    return (uintptr_t)span;
}
static void TestRelease(uintptr_t heap, uintptr_t span) { g_released++; g_releasedSpan = span; }
static void TestFinalised(void) { g_finalised++; }
static uint64_t TestTrim(uint64_t global) { g_trims++; g_trimArg = global; return 0x77; }
static int g_renders, g_tasks;
static uint64_t g_renderArgs[3], g_taskArg;
static uint64_t TestRender(uint64_t a, uint64_t b, uint64_t c) { g_renders++; g_renderArgs[0] = a; g_renderArgs[1] = b; g_renderArgs[2] = c; return 0x55; }
static uint64_t TestTask(uint64_t router) { g_tasks++; g_taskArg = router; return 0x66; }

static void Thunk(uint32_t rva, void* to)
{
    uint8_t code[12] = {0x48, 0xB8};
    uint64_t at = (uint64_t)(uintptr_t)to;
    memcpy(code + 2, &at, 8); code[10] = 0xFF; code[11] = 0xE0;
    memcpy(g_exe + rva, code, sizeof code);
    FlushInstructionCache(GetCurrentProcess(), g_exe + rva, sizeof code);
}

/* A fresh fake game: rpmalloc's code, tables and config as the build has them after init, the trim's original. */
static void Setup(int sync)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_exe = fake_host_exe();
    g_fake.strictCode = 1;
    for (size_t i = 0; i < sizeof kRpmallocBytes / sizeof kRpmallocBytes[0]; i++) memcpy(g_exe + kRpmallocBytes[i].rva, kRpmallocBytes[i].bytes, kRpmallocBytes[i].size);
    memcpy(g_exe + 0x27B0DF0u, kRpmallocSizes, sizeof kRpmallocSizes);
    memcpy(g_exe + 0x27B10F0u, kRpmallocPage, sizeof kRpmallocPage);
    uint64_t map = (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT), unmap = (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT);
    uint32_t one = 1;
    memcpy(g_exe + MAP_AT, &map, 8); memcpy(g_exe + UNMAP_AT, &unmap, 8); memcpy(g_exe + INITIALISED, &one, 4);
    Thunk(0x668D30u, (void*)TestNewSpan);
    Thunk(0x668860u, (void*)TestRelease);
    Thunk(0x667C80u, (void*)TestFinalised);
    g_fake.originals[0].target = (uintptr_t)(g_exe + TRIM);
    g_fake.originals[0].original = (void*)TestTrim;
    g_fake.originals[1].target = (uintptr_t)(g_exe + RENDER_STEP);
    g_fake.originals[1].original = (void*)TestRender;
    g_fake.originals[2].target = (uintptr_t)(g_exe + TASK_STEP);
    g_fake.originals[2].original = (void*)TestTask;
    g_newSpans = g_released = g_finalised = g_maps = g_unmaps = g_unmapSawBit = g_trims = g_renders = g_tasks = 0;
    g_releasedSpan = 0; g_trimArg = 0;
    CoXlTestSrReset(sync);
}
/* After the install: the defaults the wrappers call are the fake map and unmap (their pinned prologues are read only
 * by the install). */
static void FakeMapping(void)
{
    Thunk(MAP_DEFAULT, (void*)FakeMap);
    Thunk(UNMAP_DEFAULT, (void*)FakeUnmap);
}

static uintptr_t NewHeap(void)
{
    uint8_t* heap = (uint8_t*)VirtualAlloc(NULL, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint64_t teb = (uint64_t)(uintptr_t)NtCurrentTeb();
    memcpy(heap, &teb, 8);
    return (uintptr_t)heap;
}
static void* Alloc(uintptr_t heap) { return ((AllocFn)(void*)(g_exe + 0x669AB0u))(heap, BLOCK); }
static void Free(void* block) { ((FreeFn)(void*)(g_exe + 0x66A2F0u))((uintptr_t)block & ~(uintptr_t)0xFFFF, block); }
static uint64_t Q(uintptr_t at) { uint64_t v; memcpy(&v, (void*)at, 8); return v; }
static uint32_t D(uintptr_t at) { uint32_t v; memcpy(&v, (void*)at, 4); return v; }
static void SetQ(uintptr_t at, uint64_t v) { memcpy((void*)at, &v, 8); }
static void SetD(uintptr_t at, uint32_t v) { memcpy((void*)at, &v, 4); }
static uintptr_t SpanOf(void* block) { return (uintptr_t)block & ~(uintptr_t)0xFFFF; }
static uintptr_t Partial(uintptr_t heap) { return (uintptr_t)Q(heap + 0x10 + 0x10 * CLASS); }

typedef struct { void** blocks; int first, n, spin; } FreeJob;
static DWORD WINAPI FreeThread(void* p)
{
    FreeJob* j = (FreeJob*)p;
    for (int i = j->first; i < j->first + j->n; i++)
    {
        Free(j->blocks[i]);
        for (volatile int k = 0; k < j->spin; k++) {}
    }
    return 0;
}
static HANDLE FreeOnOther(void** blocks, int first, int n, int spin, FreeJob* job)
{
    job->blocks = blocks; job->first = first; job->n = n; job->spin = spin;
    return CreateThread(NULL, 0, FreeThread, job, 0, NULL);
}
static void FreeElsewhere(void** blocks, int first, int n)
{
    FreeJob job;
    HANDLE t = FreeOnOther(blocks, first, n, 0, &job);
    WaitForSingleObject(t, INFINITE); CloseHandle(t);
}
/* One full span of the class: 62 blocks, the first from a new span. */
static uintptr_t Fill(uintptr_t heap, void** blocks)
{
    for (uint32_t i = 0; i < COUNT; i++) blocks[i] = Alloc(heap);
    return SpanOf(blocks[0]);
}
static int InSet(void* p, void** set, int n) { for (int i = 0; i < n; i++) if (set[i] == p) return 1; return 0; }
static int Len(uint64_t list) { int n = 0; for (; list != 0 && n < 1000; n++) list = Q((uintptr_t)list); return n; }

/* One pass on the game thread: the scan (sync), the publish (the game thread's own heap), the close. */
static void Pass(uint32_t removed, unsigned long long now)
{
    CHECK(CoXlTestSrStep(removed, 7, now, 1) == 1);
    CHECK(CoXlTestSrStep(removed, 7, now + 1, 1) == 2);
    CHECK(CoXlTestSrStep(removed, 7, now + 2, 1) == 0);
}

static void Vanilla(void)
{
    g_case = "the game's code alone strands the span";
    Setup(1);
    FakeMapping();
    uintptr_t heap = NewHeap();
    void* b[COUNT];
    uintptr_t span = Fill(heap, b);
    CHECK(g_newSpans == 1 && SpanOf(b[COUNT - 1]) == span);
    CHECK(Q(span) == 0 && D(span + 0x10) == COUNT && D(span + 0x14) == COUNT && Q(heap + 0x718) == 1 && Partial(heap) == 0);
    FreeElsewhere(b, 1, COUNT - 1);
    CHECK(D(span + 0x20) == COUNT - 1 && Q(span + 0x18) != 0 && Q(span) == 0);
    void* next = Alloc(heap);
    CHECK(g_newSpans == 2 && SpanOf(next) != span); /* 61 free blocks, and a new span */
    Free(b[0]);
    CHECK(g_released == 1 && g_releasedSpan == span); /* only the last live block lets them go */
}

static void Rescue(void)
{
    g_case = "a pass gives the heap its blocks back";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    CHECK(fake_host_logged("stranded spans: on"));
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) != (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT));
    CHECK(Q((uintptr_t)(g_exe + UNMAP_AT)) != (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
    FakeMapping();
    uintptr_t heap = NewHeap();
    CoXlTestSrHeap(heap);
    void* b[COUNT];
    uintptr_t span = Fill(heap, b);
    CHECK(CoXlTestSrMarked(span) && CoXlTestSrRegistered() == 1);
    FreeElsewhere(b, 1, COUNT - 1);
    CHECK(CoXlTestSrStep(255, 7, 100000, 1) == 0); /* fewer removals than a pass needs */
    CHECK(CoXlTestSrStep(256, 7, 100000, 0) == 0); /* not in play */
    g_fake.log[0] = 0;
    Pass(256, 100000);
    long long f[F_N]; CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 1 && f[F_RESCUED] == 1 && f[F_CHANGED] == 0 && f[F_UNTAKEN] == 0 && f[F_HEAPS] == 1 && f[F_THREADS] == 1);
    CHECK(f[F_BYTES] == (long long)(COUNT - 1) * BLOCK && f[F_CLASS34] == f[F_BYTES] && f[F_SEEN] == 1);
    CHECK(fake_host_logged("stranded spans: pass 1 after 256 stamps removed (+256, 7 left the window)"));
    CHECK(fake_host_logged("1 rescued (0.1 MB: class 34 0.1"));
    CHECK(Q(span) != 0 && D(span + 0x14) == 1 && D(span + 0x20) == 0 && Q(span + 0x18) == 0);
    CHECK(Partial(heap) == span && Q(span + 0x48) == 0 && Q(heap + 0x718) == 0);
    CHECK(Len(Q(span)) == COUNT - 1);

    g_case = "the rescued blocks are handed out again, then the span goes back with its last block";
    void* again[COUNT];
    for (uint32_t i = 0; i < COUNT - 1; i++) again[i] = Alloc(heap);
    CHECK(g_newSpans == 1);
    int same = 1;
    for (uint32_t i = 0; i < COUNT - 1; i++) same &= InSet(again[i], b + 1, COUNT - 1) && !InSet(again[i], again, (int)i);
    CHECK(same);
    CHECK(Q(heap + 0x718) == 1 && D(span + 0x14) == COUNT && Partial(heap) == 0); /* full again, off the list */
    again[COUNT - 1] = b[0];
    for (uint32_t i = 0; i < COUNT; i++)
    {
        Free(again[i]);
        if (i + 1 < COUNT && g_released != 0) { CHECK(!"released before the last block"); break; }
    }
    CHECK(g_released == 1 && g_releasedSpan == span && Partial(heap) == 0 && Q(heap + 0x718) == 0);

    g_case = "the unmap clears the span's bit before the region goes";
    ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)span, 0x10000, 0, 0x10000);
    CHECK(g_unmaps == 1 && g_unmapSawBit == 0 && !CoXlTestSrMarked(span) && CoXlTestSrRegistered() == 0);
    size_t off;
    void* two = ((MapFn) * (void**)(g_exe + MAP_AT))(0x20000, &off);
    CHECK(two != NULL && !CoXlTestSrMarked((uintptr_t)two) && CoXlTestSrRegistered() == 0); /* only one span's regions */

    g_case = "the next pass waits for the gap and more removals";
    CHECK(CoXlTestSrStep(600, 7, 100002 + 19000, 1) == 0);
    CHECK(CoXlTestSrStep(511, 7, 100002 + 20001, 1) == 0);
    CHECK(CoXlTestSrStep(512, 7, 100002 + 20001, 1) == 1);
    CHECK(CoXlTestSrStep(512, 7, 100002 + 20002, 1) == 2);
    CHECK(CoXlTestSrStep(512, 7, 100002 + 20003, 1) == 0);
    CHECK(fake_host_logged("pass 2 after 512 stamps removed (+256"));
    CHECK(CoXlTestSrStep(10, 7, 200000, 1) == 0); /* another level: its removals count from its own start */
    CHECK(CoXlTestSrStep(265, 7, 200000, 1) == 0);
    CHECK(CoXlTestSrStep(266, 7, 200000, 1) == 1);
}

/* Spans of every other kind, registered: only the stranded one is listed, and only its heap's thread takes it. */
static uintptr_t Hand(uintptr_t heap, uint64_t freeList, uint32_t limit, uint32_t listSize, uint64_t deferred)
{
    size_t off;
    uintptr_t span = (uintptr_t)((MapFn) * (void**)(g_exe + MAP_AT))(0x10000, &off);
    SetQ(span, freeList); SetD(span + 8, COUNT); SetD(span + 0xC, CLASS); SetD(span + 0x10, limit); SetD(span + 0x14, COUNT);
    SetQ(span + 0x18, deferred); SetD(span + 0x20, listSize); SetD(span + 0x24, BLOCK); SetD(span + 0x2C, 1); SetQ(span + 0x40, heap);
    return span;
}
static void Kinds(void)
{
    g_case = "only full spans holding some deferred blocks are stranded";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    uintptr_t heap = NewHeap(), other = NewHeap();
    CoXlTestSrHeap(heap);
    SetQ(heap + 0x718, 1);
    uintptr_t stranded = Hand(heap, 0, COUNT, 3, 0);
    SetQ(stranded + 0x18, stranded + 0x80); SetQ(stranded + 0x80, stranded + 0x80 + BLOCK); SetQ(stranded + 0x80 + BLOCK, stranded + 0x80 + 2 * BLOCK);
    uintptr_t partial = Hand(heap, 0x1234, COUNT, 3, 0x5678);
    uintptr_t lazy = Hand(heap, 0, COUNT - 1, 3, 0x5678);       /* not every block handed out yet */
    uintptr_t all = Hand(heap, 0, COUNT, COUNT, 0x5678);        /* on its heap's +0x710 list */
    uintptr_t locked = Hand(heap, 0, COUNT, 3, UINT64_MAX);     /* a free holds its deferred list */
    uintptr_t none = Hand(heap, 0, COUNT, 0, 0);                /* full, nothing freed */
    uintptr_t wrong = Hand(heap, 0, COUNT, 3, 0x5678); SetD(wrong + 0x24, 1024); /* not the class's block size */
    uintptr_t orphan = Hand(0, 0, COUNT, 3, 0x5678);
    uintptr_t theirs = Hand(other, 0, COUNT, 3, 0x5678);       /* stranded, another heap's */
    uint8_t* unregistered = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memcpy(unregistered, (void*)stranded, 0x80);
    CHECK(CoXlTestSrRegistered() == 9);
    CHECK(CoXlTestSrStep(256, 7, 100000, 1) == 1 && CoXlTestSrStep(256, 7, 100001, 1) == 2);
    CHECK(CoXlTestSrStep(256, 7, 100002, 1) == 2 && CoXlTestSrStep(256, 7, 105000, 1) == 2); /* another heap's list, 5 s to come */
    CHECK(CoXlTestSrStep(256, 7, 105001, 1) == 0);
    long long f[F_N]; CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 2 && f[F_HEAPS] == 2 && f[F_RESCUED] == 1 && f[F_UNTAKEN] == 1 && f[F_THREADS] == 1 && f[F_SEEN] == 9);
    CHECK(Q(stranded) == stranded + 0x80 && D(stranded + 0x14) == COUNT - 3 && Partial(heap) == stranded && Q(heap + 0x718) == 0);
    CHECK(Q(partial + 0x18) == 0x5678 && Q(lazy + 0x18) == 0x5678 && Q(all + 0x18) == 0x5678 && Q(locked + 0x18) == UINT64_MAX);
    CHECK(Q(wrong + 0x18) == 0x5678 && Q(orphan + 0x18) == 0x5678 && Q(theirs + 0x18) == 0x5678 && D(none + 0x14) == COUNT);
    CHECK(Q((uintptr_t)unregistered + 0x18) == stranded + 0x80);
    CHECK(fake_host_logged("2 stranded in 2 heaps") && fake_host_logged("1 in heaps whose thread did not come"));

    CHECK(CoXlTestSrRescue(heap, theirs) == 0 && Q(theirs + 0x18) == 0x5678); /* another heap's stranded span */
    g_case = "a heap's thread takes its list at its trim, and the trim runs";
    for (uint32_t i = 0; i < 3; i++) SetQ(theirs + 0x80 + i * BLOCK, i < 2 ? theirs + 0x80 + (i + 1) * BLOCK : 0);
    SetQ(theirs + 0x18, theirs + 0x80); SetQ(other + 0x718, 1);
    CHECK(CoXlTestSrStep(512, 7, 200000, 1) == 1);
    CHECK(CoXlTestSrStep(512, 7, 200001, 1) == 2); /* the game thread's heap has nothing */
    CHECK(Q(theirs + 0x18) == theirs + 0x80);
    CHECK(CoXlTestSrStep(512, 7, 200002, 1) == 2); /* its thread has not come yet */
    CoXlTestSrHeap(other);
    CHECK(CoXlTestSrTrim(5) == 0x77 && g_trims == 1 && g_trimArg == 5);
    CHECK(Q(theirs) == theirs + 0x80 && Q(theirs + 0x18) == 0 && Partial(other) == theirs && Q(other + 0x718) == 0);
    CHECK(CoXlTestSrTrim(0) == 0x77 && g_trims == 2); /* taken once */
    CHECK(CoXlTestSrStep(512, 7, 200003, 1) == 0);
    CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 1 && f[F_RESCUED] == 1 && f[F_UNTAKEN] == 0);
    CHECK(CoXlTestSrTrim(1) == 0x77 && g_trims == 3); /* between passes: the trim alone */

    g_case = "a heap whose thread is not its owner, or that is ending, is left alone";
    uintptr_t again = Hand(other, 0, COUNT, 3, 0x9ABC);
    SetQ(other, 0x1111);
    CHECK(CoXlTestSrStep(768, 7, 300000, 1) == 1 && CoXlTestSrStep(768, 7, 300001, 1) == 2);
    CHECK(Q(again + 0x18) == 0x9ABC);
    CHECK(CoXlTestSrStep(768, 7, 300002, 1) == 0);
    CoXlTestSrLast(f);
    CHECK(f[F_RESCUED] == 0 && f[F_CHANGED] == 1);
    SetQ(other, (uint64_t)(uintptr_t)NtCurrentTeb()); SetD(other + 0x73C, 1);
    CHECK(CoXlTestSrStep(1024, 7, 400000, 1) == 1 && CoXlTestSrStep(1024, 7, 400001, 1) == 2);
    CHECK(Q(again + 0x18) == 0x9ABC);
    CHECK(CoXlTestSrStep(1024, 7, 400002, 1) == 0);

    g_case = "a span that changed after the scan is left alone";
    CHECK(CoXlTestSrRescue(heap, theirs) == 0);           /* another heap's */
    SetD(other + 0x73C, 0);
    CHECK(CoXlTestSrRescue(other, partial) == 0);
    SetQ(again + 0x18, 0); CHECK(CoXlTestSrRescue(other, again) == 0); /* taken by its heap meanwhile */

    g_case = "a span unmapped after the scan is not touched";
    uintptr_t gone = Hand(other, 0, COUNT, 1, 0);
    SetQ(gone + 0x18, gone + 0x80);
    CoXlTestSrHeap(heap);
    CHECK(CoXlTestSrStep(1280, 7, 500000, 1) == 1 && CoXlTestSrStep(1280, 7, 500001, 1) == 2);
    ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)gone, 0x10000, 0, 0); /* decommitted: still readable here */
    CoXlTestSrHeap(other);
    CHECK(CoXlTestSrTrim(0) == 0x77);
    CHECK(Q(gone + 0x18) == gone + 0x80 && Q(gone) == 0);
    CHECK(CoXlTestSrStep(1280, 7, 500002, 1) == 0);
    CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 1 && f[F_RESCUED] == 0 && f[F_CHANGED] == 1);
}

/* Another thread frees into the span while the heap takes it back: every block ends on exactly one list. */
static void Race(void)
{
    g_case = "a rescue during another thread's frees loses and doubles no block";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    int bad = 0, gaveUp = 0, during = 0;
    for (int round = 0; round < 2000 && !bad; round++)
    {
        uintptr_t heap = NewHeap();
        void* b[COUNT];
        uintptr_t span = Fill(heap, b);
        FreeElsewhere(b, 1, 30);
        FreeJob job;
        HANDLE t = FreeOnOther(b, 31, COUNT - 31, round % 7 * 20, &job);
        int r;
        for (uint32_t want = 30 + round % 31; *(volatile uint32_t*)(span + 0x20) < want && WaitForSingleObject(t, 0) == WAIT_TIMEOUT;) {}
        while ((r = CoXlTestSrRescue(heap, span)) <= 0) { if (r < 0) gaveUp++; if (r == 0 && Q(span) != 0) break; }
        during += WaitForSingleObject(t, 0) == WAIT_TIMEOUT;
        WaitForSingleObject(t, INFINITE); CloseHandle(t);
        CHECK(r > 0);
        uint32_t own = (uint32_t)Len(Q(span)), deferred = (uint32_t)Len(Q(span + 0x18));
        bad |= own + deferred != COUNT - 1 || deferred != D(span + 0x20) || D(span + 0x14) != COUNT - own;
        void* seen[COUNT]; int n = 0;
        for (uint64_t p = Q(span); p != 0 && n < (int)COUNT; p = Q((uintptr_t)p)) seen[n++] = (void*)(uintptr_t)p;
        for (uint64_t p = Q(span + 0x18); p != 0 && n < (int)COUNT; p = Q((uintptr_t)p)) seen[n++] = (void*)(uintptr_t)p;
        for (int i = 0; i < n; i++) bad |= !InSet(seen[i], b + 1, COUNT - 1) || InSet(seen[i], seen, i);
        VirtualFree((void*)heap, 0, MEM_RELEASE);
    }
    CHECK(!bad);
    CHECK(during > 0);
    printf("  race: %d of 2000 rescues while the other thread was still freeing; %d gave up a held lock and were tried again\n", during, gaveUp);
}

/* The last live block freed by another thread while the heap takes the span back: the span goes to exactly one list,
 * the heap's partial list (rescued first) or the +0x710 list (the free made every block deferred first). */
static volatile LONG g_go, g_ready;
static void* g_lastBlock;
static DWORD WINAPI LastFree(void* unused) { (void)unused; InterlockedExchange(&g_ready, 1); while (!g_go) {} Free(g_lastBlock); return 0; }
static void Last(void)
{
    g_case = "the last block's free during a rescue leaves the span on one list";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    int bad = 0, rescued = 0, deferredAll = 0;
    for (int round = 0; round < 20000 && !bad; round++)
    {
        uintptr_t heap = NewHeap();
        void* b[COUNT];
        uintptr_t span = Fill(heap, b);
        FreeElsewhere(b, 1, COUNT - 1);
        g_go = 0; g_ready = 0; g_lastBlock = b[0];
        HANDLE t = CreateThread(NULL, 0, LastFree, NULL, 0, NULL);
        while (!g_ready) {}
        InterlockedExchange(&g_go, 1);
        for (volatile int k = 0; k < round % 32; k++) {}
        int r = 0;
        while ((uintptr_t)Q(heap + 0x710) != span && (r = CoXlTestSrRescue(heap, span)) <= 0) {}
        WaitForSingleObject(t, INFINITE); CloseHandle(t);
        int on710 = (uintptr_t)Q(heap + 0x710) == span, onPartial = Partial(heap) == span;
        if (on710) { deferredAll++; bad |= onPartial || r > 0 || D(span + 0x20) != COUNT || Q(heap + 0x718) != 1; }
        else { rescued++; bad |= !onPartial || r != (int)COUNT - 1 || D(span + 0x20) != 1 || Len(Q(span)) != (int)COUNT - 1 || Q(heap + 0x718) != 0; }
        VirtualFree((void*)heap, 0, MEM_RELEASE);
    }
    CHECK(!bad);
    printf("  last: %d rescued first, %d went to +0x710 first\n", rescued, deferredAll);
}

/* The pool's workers are told to trim once the lists are out, not before. */
static void Workers(void)
{
    g_case = "the pool's workers trim after the publish";
    Setup(1);
    static uint8_t app[0x400], pool[0x100], w0[0x100], w1[0x100];
    uint64_t workers[2] = {(uint64_t)(uintptr_t)w0, (uint64_t)(uintptr_t)w1};
    memset(w0, 0, sizeof w0); memset(w1, 0, sizeof w1);
    SetQ((uintptr_t)(g_exe + APP), (uint64_t)(uintptr_t)app); SetQ((uintptr_t)app + 0x318, (uint64_t)(uintptr_t)pool);
    SetD((uintptr_t)pool + 0x88, 2); SetQ((uintptr_t)pool + 0x90, (uint64_t)(uintptr_t)workers);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    CHECK(CoXlTestSrStep(256, 0, 100000, 1) == 1);
    CHECK(w0[0x7D] == 0 && w1[0x7D] == 0);
    CHECK(CoXlTestSrStep(256, 0, 100001, 1) == 2);
    CHECK(w0[0x7D] == 1 && w1[0x7D] == 1);
}

/* ---- the registry's lock ---------------------------------------------------------------------------------------- */
typedef struct { int mode; HANDLE held, done; } Holder;
static DWORD WINAPI HoldLock(void* p)
{
    Holder* h = (Holder*)p;
    CoXlTestSrLock(h->mode, 1);
    SetEvent(h->held);
    WaitForSingleObject(h->done, INFINITE);
    CoXlTestSrLock(h->mode, 0);
    return 0;
}
static HANDLE Hold(Holder* h, int mode)
{
    h->mode = mode; h->held = CreateEventA(NULL, TRUE, FALSE, NULL); h->done = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, HoldLock, h, 0, NULL);
    WaitForSingleObject(h->held, INFINITE);
    return t;
}
static void LetGo(Holder* h, HANDLE t) { SetEvent(h->done); WaitForSingleObject(t, INFINITE); CloseHandle(t); CloseHandle(h->held); CloseHandle(h->done); }
static uintptr_t g_unmapSpan;
static DWORD WINAPI UnmapOne(void* unused) { (void)unused; ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)g_unmapSpan, 0x10000, 0, 0); return 0; }
static uintptr_t g_trimHeap;
static DWORD WINAPI TrimOne(void* unused) { (void)unused; SetQ(g_trimHeap, (uint64_t)(uintptr_t)NtCurrentTeb()); CoXlTestSrTrim(0); return 0; }
static DWORD WINAPI StepOne(void* unused) { (void)unused; CoXlTestSrStep(256, 0, 100000, 1); return 0; }
static void Locks(void)
{
    g_case = "an unmap waits for a reader of the headers";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    uintptr_t heap = NewHeap(), other = NewHeap();
    uintptr_t span = Hand(heap, 0, COUNT, 1, 0);
    SetQ(span + 0x18, span + 0x80);
    Holder h;
    HANDLE reader = Hold(&h, 1);
    g_unmapSpan = span;
    HANDLE u = CreateThread(NULL, 0, UnmapOne, NULL, 0, NULL);
    CHECK(WaitForSingleObject(u, 100) == WAIT_TIMEOUT);
    CHECK(g_unmaps == 0 && CoXlTestSrMarked(span));
    LetGo(&h, reader);
    CHECK(WaitForSingleObject(u, 5000) == WAIT_OBJECT_0); CloseHandle(u);
    CHECK(g_unmaps == 1 && !CoXlTestSrMarked(span));
    long long c[11]; CoXlTestSrCosts(c);
    CHECK(c[3] > 0); /* the wait was measured */

    g_case = "a scan and a rescue wait for an unmap";
    span = Hand(heap, 0, COUNT, 1, 0);
    SetQ(span + 0x18, span + 0x80); SetQ(heap + 0x718, 1);
    CoXlTestSrHeap(other);
    HANDLE w = Hold(&h, 2);
    HANDLE t = CreateThread(NULL, 0, StepOne, NULL, 0, NULL);
    CHECK(WaitForSingleObject(t, 100) == WAIT_TIMEOUT); /* the scan */
    LetGo(&h, w);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0); CloseHandle(t);
    CHECK(CoXlTestSrStep(256, 0, 100001, 1) == 2); /* published; the game thread's heap has none */
    CoXlTestSrHeap(heap); g_trimHeap = heap;
    w = Hold(&h, 2);
    t = CreateThread(NULL, 0, TrimOne, NULL, 0, NULL);
    CHECK(WaitForSingleObject(t, 100) == WAIT_TIMEOUT); /* the rescue */
    CHECK(Q(span) == 0 && Q(span + 0x18) == span + 0x80);
    LetGo(&h, w);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0); CloseHandle(t);
    CHECK(Q(span) == span + 0x80 && Partial(heap) == span);
}

/* Spans mapped and released on another thread while passes scan and rescue, and each read of a header made slower:
 * a reader inside a header that is going faults. A rescued span stays committed until its pass ends and the test
 * empties the heap's lists, so only a read the lock should have kept out reaches a decommitted one. Odd slots are
 * read by the scan only; even ones are stranded. */
#define CHURN_LIVE 16u /* a span is released about one pass after it is made: often while that pass reads it */
static volatile LONG g_churnStop;
static uintptr_t g_churnHeap;
static LONG g_churned;
static void ChurnHeader(uintptr_t span, uint32_t cls, int plain)
{
    uint32_t size, count;
    memcpy(&size, kRpmallocSizes + 8 * cls, 4); count = (uint32_t)kRpmallocSizes[8 * cls + 4] | (uint32_t)kRpmallocSizes[8 * cls + 5] << 8;
    SetQ(span + 0x80, 0);
    SetQ(span, 0); SetD(span + 8, count); SetD(span + 0xC, cls); SetD(span + 0x10, count); SetD(span + 0x14, count);
    SetQ(span + 0x18, plain ? 0 : span + 0x80); SetD(span + 0x20, plain ? 0 : 1); SetD(span + 0x24, size); SetD(span + 0x2C, 1);
    *(volatile uint64_t*)(span + 0x40) = g_churnHeap; /* last, as rpmalloc sets a span up */
}
static DWORD WINAPI Churn(void* unused)
{
    (void)unused;
    uintptr_t live[CHURN_LIVE] = {0};
    size_t off;
    for (uint32_t k = 0; !g_churnStop; k++)
    {
        uint32_t i = k % CHURN_LIVE;
        if (live[i] != 0) ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)live[i], 0x10000, 0, 0x10000);
        live[i] = (uintptr_t)((MapFn) * (void**)(g_exe + MAP_AT))(0x10000, &off);
        if (live[i] != 0) ChurnHeader(live[i], i, i & 1);
        g_churned++;
    }
    for (int i = 0; i < (int)CHURN_LIVE; i++) if (live[i] != 0) ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)live[i], 0x10000, 0, 0x10000);
    return 0;
}
static void Pause(void) { for (volatile int k = 0; k < 3000; k++) {} } /* about the time an unmap takes */
static void Churning(void)
{
    g_case = "spans released during passes";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    g_churnHeap = NewHeap();
    CoXlTestSrHeap(g_churnHeap);
    if (g_arena == NULL) g_arena = (uint8_t*)VirtualAlloc(NULL, (size_t)ARENA_SPANS * 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(g_arena != NULL);
    g_churnStop = 0; g_churned = 0; g_grave = 1;
    CoXlTestSrPause(Pause);
    HANDLE t = CreateThread(NULL, 0, Churn, NULL, 0, NULL);
    long long rescued = 0, found = 0, f[F_N];
    ULONGLONG until = GetTickCount64() + 1500;
    uint32_t pass = 0;
    for (; GetTickCount64() < until; pass++)
    {
        uint32_t removed = 256 * (pass + 1);
        unsigned long long now = 100000ull + 20003ull * pass;
        CoXlTestSrStep(removed, 0, now, 1); CoXlTestSrStep(removed, 0, now + 1, 1); CoXlTestSrStep(removed, 0, now + 2, 1);
        CoXlTestSrLast(f); rescued += f[F_RESCUED]; found += f[F_FOUND];
        memset((void*)(g_churnHeap + 0x10), 0, 0x10 * 96);
        Bury();
        g_fake.log[0] = 0; /* the fake log holds 8 KB */
    }
    g_churnStop = 1;
    WaitForSingleObject(t, INFINITE); CloseHandle(t);
    Bury(); g_grave = 0;
    CoXlTestSrPause(NULL);
    CHECK(rescued > 0 && g_churned > 1000 && CoXlTestSrRegistered() == 0);
    printf("  churn: %u passes, %ld spans mapped and released, %lld found stranded, %lld rescued\n", pass, (long)g_churned, found, rescued);
}

/* A rescue onto a partial list that holds a span: that span's prev is the rescued one, so freeing its last block
 * (0x66A44E) unlinks it from the middle of the list. */
static void Neighbour(void)
{
    g_case = "a rescued span ahead of a partial one; the partial one goes";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    uintptr_t heap = NewHeap();
    CoXlTestSrHeap(heap);
    void* b[COUNT];
    uintptr_t full = Fill(heap, b);
    void* lone = Alloc(heap);
    uintptr_t partial = SpanOf(lone);
    CHECK(partial != full && Partial(heap) == partial && g_newSpans == 2);
    FreeElsewhere(b, 1, COUNT - 1);
    Pass(256, 100000);
    CHECK(Partial(heap) == full && Q(full + 0x48) == partial && Q(partial + 0x50) == full);
    /* the partial span's other blocks are in the heap's class free list: hand them out, then free them all */
    void* rest[COUNT]; int n = 0;
    while (n < (int)COUNT && Q(heap + 8 + 0x10 * CLASS) != 0) rest[n++] = Alloc(heap);
    CHECK(Partial(heap) == full); /* the class free list emptied; the partial head is still the rescued span */
    for (int i = 0; i < n; i++) if (SpanOf(rest[i]) == partial) Free(rest[i]);
    Free(lone);
    CHECK(g_released == 1 && g_releasedSpan == partial);
    CHECK(Partial(heap) == full && Q(full + 0x48) == 0);
    for (int i = 0; i < n; i++) if (SpanOf(rest[i]) != partial) Free(rest[i]);
}

/* ---- threads that never trim: the renderer and Havok's workers --------------------------------------------------- */
typedef uint64_t (*RenderFn)(uint64_t, uint64_t, uint64_t);
typedef uint64_t (*TaskFn)(uint64_t);
static void* Detour(uint32_t rva) { return fake_host_detour((uintptr_t)(g_exe + rva) - (uintptr_t)g_fake.image); }
static uintptr_t g_renderHeap;
static LARGE_INTEGER g_renderStart; /* when the renderer's step began, as the counter has it */
static DWORD WINAPI RenderOne(void* unused)
{
    (void)unused;
    SetQ(g_renderHeap, (uint64_t)(uintptr_t)NtCurrentTeb()); /* the heap is this thread's, as the renderer's is */
    QueryPerformanceCounter(&g_renderStart);
    ((RenderFn)Detour(RENDER_STEP))(9, 8, 7);
    return 0;
}
static void Reach(void)
{
    g_case = "the renderer and Havok's workers take their lists at a message and at a task step";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1 && g_fake.hookCount == 3);
    CHECK(fake_host_logged("lists are taken at trims and the tick, the renderer's messages, Havok's task steps"));
    FakeMapping();
    RenderFn render = (RenderFn)Detour(RENDER_STEP);
    TaskFn task = (TaskFn)Detour(TASK_STEP);
    CHECK(render != NULL && task != NULL);
    if (render == NULL || task == NULL) return;
    uintptr_t renderer = NewHeap(), worker = NewHeap(), game = NewHeap();
    void* r[COUNT]; void* w[COUNT];
    uintptr_t rs = Fill(renderer, r), ws = Fill(worker, w);
    FreeElsewhere(r, 1, COUNT - 1);
    FreeElsewhere(w, 1, COUNT - 1);
    CoXlTestSrHeap(renderer);
    CHECK(render(1, 2, 3) == 0x55 && g_renders == 1 && g_renderArgs[0] == 1 && g_renderArgs[1] == 2 && g_renderArgs[2] == 3);
    CHECK(Q(rs) == 0 && Partial(renderer) == 0); /* between passes: the dispatch alone */
    CoXlTestSrHeap(game);
    CHECK(CoXlTestSrStep(256, 0, 100000, 1) == 1);
    CoXlTestSrHeap(worker);
    CHECK(task(5) == 0x66 && g_tasks == 1 && g_taskArg == 5 && Partial(worker) == 0); /* scanned, not published */
    CoXlTestSrHeap(game);
    CHECK(CoXlTestSrStep(256, 0, 100001, 1) == 2);
    CoXlTestSrHeap(renderer);
    CHECK(render(4, 5, 6) == 0x55 && g_renders == 2 && g_renderArgs[2] == 6);
    CHECK(Partial(renderer) == rs && Q(rs + 0x18) == 0 && Len(Q(rs)) == (int)COUNT - 1 && D(rs + 0x14) == 1 && Q(renderer + 0x718) == 0);
    CoXlTestSrHeap(worker);
    CHECK(task(7) == 0x66 && g_tasks == 2 && g_taskArg == 7);
    CHECK(Partial(worker) == ws && Q(ws + 0x18) == 0 && Len(Q(ws)) == (int)COUNT - 1);
    CHECK(task(8) == 0x66 && g_tasks == 3); /* taken once */
    long long c[11]; CoXlTestSrCosts(c);
    CHECK(c[5] == 0 && c[6] == 0 && c[9] <= c[7] && c[10] <= c[8]); /* timed by the hook that took the list */
    g_fake.log[0] = 0;
    CHECK(CoXlTestSrStep(256, 0, 100002, 1) == 0);
    long long f[F_N]; CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 2 && f[F_RESCUED] == 2 && f[F_UNTAKEN] == 0 && f[F_THREADS] == 2);
    CHECK(f[F_VIA_RENDER] == 1 && f[F_VIA_TASK] == 1 && f[F_VIA_TRIM] == 0 && f[F_VIA_TICK] == 0);
    CHECK(fake_host_logged("0 in heaps whose thread did not come, 0 over the room; lists taken at trims 0, the tick 0, render messages 1, Havok task steps 1"));
    CHECK(fake_host_logged("rpmalloc regions registered; taking the lists: at trims 0.000 ms (longest 0.000 ms), at the tick 0.000 ms (longest 0.000 ms), at render messages "));
    CHECK(fake_host_logged(" ms), at Havok task steps "));
    CoXlTestSrCosts(c);
    CHECK(c[7] == 0 && c[8] == 0 && c[9] == 0 && c[10] == 0); /* the note starts the next count */
    void* again = Alloc(renderer);
    CHECK(SpanOf(again) == rs && g_newSpans == 2); /* the renderer's next block comes from the rescued span */

    g_case = "the renderer's own thread takes its list";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    g_renderHeap = NewHeap();
    rs = Fill(g_renderHeap, r);
    FreeElsewhere(r, 1, COUNT - 1);
    CoXlTestSrHeap(game);
    CHECK(CoXlTestSrStep(512, 0, 200000, 1) == 1 && CoXlTestSrStep(512, 0, 200001, 1) == 2);
    CoXlTestSrHeap(g_renderHeap);
    Holder h;
    HANDLE unmap = Hold(&h, 2); /* an unmap holds the headers: the renderer's take waits, and that wait is its cost */
    HANDLE t = CreateThread(NULL, 0, RenderOne, NULL, 0, NULL);
    /* A 50 ms timeout follows the scheduler's tick and can end a little early by the counter: what counts is that the
     * renderer's step was held from its start until this wait ended, which the counter measures on both threads. */
    CHECK(WaitForSingleObject(t, 50) == WAIT_TIMEOUT);
    LARGE_INTEGER waitTo, qpf;
    QueryPerformanceCounter(&waitTo);
    QueryPerformanceFrequency(&qpf);
    LetGo(&h, unmap);
    WaitForSingleObject(t, INFINITE); CloseHandle(t);
    CHECK(g_renders == 1 && g_renderArgs[0] == 9 && Partial(g_renderHeap) == rs && Q(rs + 0x18) == 0);
    CoXlTestSrCosts(c);
    /* The renderer's take waited at least from its step's start until this wait ended (less 1 ms for the detour's own
     * prologue before its clock starts), and the wait is counted on the render hook alone. */
    const LONG64 held = waitTo.QuadPart - g_renderStart.QuadPart - qpf.QuadPart / 1000;
    CHECK(g_renderStart.QuadPart != 0 && held > qpf.QuadPart / 100 && c[9] >= held && c[7] == c[9] && c[8] == 0);
    CHECK(CoXlTestSrStep(512, 0, 200002, 1) == 0);

    g_case = "a refused reach hook leaves that thread kind untaken; the rescue stays on";
    Setup(1);
    g_fake.noHook = (uintptr_t)(g_exe + RENDER_STEP) - (uintptr_t)g_fake.image;
    CHECK(CoXlTestSrInstall() == 1 && g_fake.hookCount == 2 && Detour(RENDER_STEP) == NULL && Detour(TASK_STEP) != NULL);
    CHECK(fake_host_logged("the renderer not reached: the hook was refused"));
    CHECK(fake_host_logged("lists are taken at trims and the tick, Havok's task steps"));
    g_fake.noHook = 0;
    Setup(1);
    g_fake.noHook = (uintptr_t)(g_exe + TASK_STEP) - (uintptr_t)g_fake.image;
    CHECK(CoXlTestSrInstall() == 1 && g_fake.hookCount == 2 && Detour(TASK_STEP) == NULL && Detour(RENDER_STEP) != NULL);
    CHECK(fake_host_logged("threads that run Havok tasks not reached: the hook was refused"));
    g_fake.noHook = 0;
}

/* ---- the pool worker's trim -------------------------------------------------------------------------------------- */
typedef void (*WorkerFn)(void);
static void WorkerSite(void)
{
    static const uint8_t enter[] = {0x48, 0x83, 0xEC, 0x08}, leave[] = {0x48, 0x83, 0xC4, 0x08, 0xC3};
    memcpy(g_exe + WORKER_TRIM, enter, sizeof enter);       /* 0x6253C7..0x6253CD stay the game's */
    memcpy(g_exe + 0x6253CEu, leave, sizeof leave);
    Thunk(TRIM, fake_host_detour((uintptr_t)(g_exe + TRIM) - (uintptr_t)g_fake.image));
}
static void Skip(void)
{
    g_case = "a worker trim only the pass asked for is skipped";
    Setup(1);
    SetD((uintptr_t)(g_exe + REGIONS), 9);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    WorkerSite();
    uintptr_t w1 = NewHeap(), w2 = NewHeap();
    WorkerFn worker = (WorkerFn)(void*)(g_exe + WORKER_TRIM);
    CoXlTestSrHeap(w1);
    worker(); CHECK(g_trims == 1 && g_trimArg == 0); /* first seen: trims */
    worker(); CHECK(g_trims == 1);                    /* nothing of the game's since */
    CoXlTestSrTrim(1); CHECK(g_trims == 2 && g_trimArg == 1); /* a trim-all: the game raises every worker's flag */
    worker(); CHECK(g_trims == 3);
    worker(); CHECK(g_trims == 3);
    CoXlTestSrHeap(w2);
    worker(); CHECK(g_trims == 4);
    worker(); CHECK(g_trims == 4);
    CoXlTestSrTrim(0); CHECK(g_trims == 5); /* any other caller trims */
    CoXlTestSrHeap(w1);
    CoXlTestSrTrim(0x100); CHECK(g_trims == 6); /* only cl is the argument (0x630310) */
    worker(); CHECK(g_trims == 6);
    long long c[11]; CoXlTestSrCosts(c);
    CHECK(c[0] == 3 && c[1] == 4 && c[2] == 1);
    g_fake.log[0] = 0;
    CoXlTestSrHeap(NewHeap());
    Pass(256, 100000);
    CHECK(fake_host_logged("pass 1 cost: an unmap waited at most 0.000 ms for the scan; pool worker trims since the last pass: 3 run ("));
    CHECK(fake_host_logged("4 skipped (only the pass asked); 0 of 9 rpmalloc regions registered; taking the lists: at trims "));
    CHECK(fake_host_logged("at the tick 0.000 ms (longest 0.000 ms), at render messages 0.000 ms (longest 0.000 ms), at Havok task steps 0.000 ms (longest 0.000 ms)"));
}

/* HellForge 0.5's span-rescue, as its loader leaves rpmalloc: its own map and unmap wrappers around the defaults. */
static int g_hfMaps, g_hfUnmaps;
static void* HfMap(size_t size, size_t* offset) { g_hfMaps++; return ((MapFn)(void*)(g_exe + MAP_DEFAULT))(size, offset); }
static void HfUnmap(void* address, size_t size, size_t offset, size_t release)
{
    g_hfUnmaps++;
    ((UnmapFn)(void*)(g_exe + UNMAP_DEFAULT))(address, size, offset, release);
}
static void Beside(void)
{
    g_case = "beside HellForge's span-rescue: its wrappers wrapped, every span registered by both";
    Setup(1);
    SetQ((uintptr_t)(g_exe + MAP_AT), (uint64_t)(uintptr_t)HfMap); SetQ((uintptr_t)(g_exe + UNMAP_AT), (uint64_t)(uintptr_t)HfUnmap);
    CHECK(CoXlTestSrInstall() == -1); /* wrappers of another module: refused, as any other set-up */
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) == (uint64_t)(uintptr_t)HfMap && Q((uintptr_t)(g_exe + UNMAP_AT)) == (uint64_t)(uintptr_t)HfUnmap);
    Setup(1);
    SetQ((uintptr_t)(g_exe + MAP_AT), (uint64_t)(uintptr_t)HfMap); SetQ((uintptr_t)(g_exe + UNMAP_AT), (uint64_t)(uintptr_t)HfUnmap);
    CoXlTestSrHellForge((void*)HfMap, NULL);
    CHECK(CoXlTestSrInstall() == -1); /* only the map HellForge's: refused */
    Setup(1);
    SetQ((uintptr_t)(g_exe + MAP_AT), (uint64_t)(uintptr_t)HfMap); SetQ((uintptr_t)(g_exe + UNMAP_AT), (uint64_t)(uintptr_t)HfUnmap);
    CoXlTestSrHellForge((void*)HfMap, (void*)HfUnmap);
    SetD((uintptr_t)(g_exe + REGIONS), 9);
    g_hfMaps = g_hfUnmaps = 0;
    CHECK(CoXlTestSrInstall() == 1);
    CHECK(fake_host_logged("beside HellForge's span-rescue"));
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) != (uint64_t)(uintptr_t)HfMap && Q((uintptr_t)(g_exe + UNMAP_AT)) != (uint64_t)(uintptr_t)HfUnmap);
    FakeMapping();
    uintptr_t heap = NewHeap();
    CoXlTestSrHeap(heap);
    void* b[COUNT];
    uintptr_t span = Fill(heap, b);
    CHECK(g_hfMaps == 1 && g_maps == 1 && CoXlTestSrMarked(span) && CoXlTestSrRegistered() == 1);

    g_case = "beside HellForge's span-rescue: a pass in play rescues";
    FreeElsewhere(b, 1, COUNT - 1);
    Pass(256, 100000);
    long long f[F_N]; CoXlTestSrLast(f);
    CHECK(f[F_FOUND] == 1 && f[F_RESCUED] == 1 && f[F_CHANGED] == 0);
    CHECK(Partial(heap) == span && Len(Q(span)) == COUNT - 1 && Q(heap + 0x718) == 0);
    CHECK(CoXlTestSrRescue(heap, span) == 0); /* a second rescuer finds it no longer stranded */

    g_case = "beside HellForge's span-rescue: the unmap clears the mod's bit, then HellForge's unmap runs";
    ((UnmapFn) * (void**)(g_exe + UNMAP_AT))((void*)span, 0x10000, 0, 0x10000);
    CHECK(g_hfUnmaps == 1 && g_unmaps == 1 && g_unmapSawBit == 0 && !CoXlTestSrMarked(span) && CoXlTestSrRegistered() == 0);

    g_case = "beside HellForge's span-rescue: every pool worker's trim runs (a flag may be HellForge's pass's)";
    WorkerSite();
    WorkerFn worker = (WorkerFn)(void*)(g_exe + WORKER_TRIM);
    CoXlTestSrHeap(NewHeap());
    g_trims = 0;
    worker(); worker(); worker();
    CHECK(g_trims == 3);
}

static volatile LONG g_toggleStop;
static DWORD WINAPI ToggleMap(void* unused)
{
    (void)unused;
    void* volatile* at = (void* volatile*)(g_exe + MAP_AT);
    void* def = g_exe + MAP_DEFAULT;
    while (!g_toggleStop)
    {
        InterlockedCompareExchangePointer(at, (void*)0x1234, def);
        InterlockedCompareExchangePointer(at, def, (void*)0x1234);
    }
    return 0;
}
static void Refusals(void)
{
    g_case = "a refused trim hook puts the span map back and ends the scan thread";
    Setup(0); g_fake.noHook = (uintptr_t)(g_exe + TRIM) - (uintptr_t)g_fake.image;
    CHECK(CoXlTestSrInstall() == -1);
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) == (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT));
    CHECK(Q((uintptr_t)(g_exe + UNMAP_AT)) == (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
    CHECK(CoXlTestSrThreadEnded(5000) == 1);
    CHECK(fake_host_logged("stranded spans: off: the trim could not be hooked"));

    g_case = "a map-fail callback or other config refuses";
    Setup(1); SetQ((uintptr_t)(g_exe + CONFIG_REST + 8), 0x1234);
    CHECK(CoXlTestSrInstall() == -1);
    Setup(1); SetQ((uintptr_t)(g_exe + CONFIG_REST), 1);
    CHECK(CoXlTestSrInstall() == -1);
    CHECK(fake_host_logged("map-fail callback"));

    g_case = "a span map that changes during the install";
    Setup(1);
    int on = 0, atCheck = 0, atSwap = 0, bad = 0;
    g_toggleStop = 0;
    HANDLE t = CreateThread(NULL, 0, ToggleMap, NULL, 0, NULL);
    for (int i = 0; i < 5000; i++)
    {
        CoXlTestSrReset(1); g_fake.hookCount = 0; g_fake.log[0] = 0;
        int r = CoXlTestSrInstall();
        void* map = *(void* volatile*)(g_exe + MAP_AT);
        void* unmap = *(void* volatile*)(g_exe + UNMAP_AT);
        if (r == 1)
        {
            on++;
            bad |= unmap == g_exe + UNMAP_DEFAULT || map == g_exe + MAP_DEFAULT || g_fake.hookCount != 3;
            SetQ((uintptr_t)(g_exe + UNMAP_AT), (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
            InterlockedExchangePointer((void* volatile*)(g_exe + MAP_AT), g_exe + MAP_DEFAULT);
        }
        else
        {
            atCheck += fake_host_logged("not set up with its own span map");
            atSwap += fake_host_logged("span map changed while it was wrapped");
            bad |= unmap != g_exe + UNMAP_DEFAULT || g_fake.hookCount != 0 || (map != g_exe + MAP_DEFAULT && map != (void*)0x1234);
        }
    }
    g_toggleStop = 1;
    WaitForSingleObject(t, INFINITE); CloseHandle(t);
    CHECK(!bad && on > 0 && atCheck > 0);
    printf("  refusals: %d installed, %d refused at the check, %d refused at the map swap (put back)\n", on, atCheck, atSwap);
}

static void Install(void)
{
    g_case = "every pinned byte is checked; a reach's own bytes leave only that reach out";
    int missed = 0, reachMissed = 0, reachNoted = 1; uint32_t first = 0;
    Setup(1);
    for (size_t i = 0; i < sizeof kRpmallocBytes / sizeof kRpmallocBytes[0]; i++)
    {
        int group = kRpmallocBytes[i].group;
        if (group == 3) continue; /* run by the test, not pinned */
        for (uint32_t k = 0; k < kRpmallocBytes[i].size; k++)
        {
            CoXlTestSrReset(1);
            g_fake.log[0] = 0; /* the fake log holds 8 KB */
            g_fake.hookCount = 0;
            g_exe[kRpmallocBytes[i].rva + k] ^= 0x5A;
            int r = CoXlTestSrInstall();
            if (group == 0 && r != -1 && missed++ == 0) first = kRpmallocBytes[i].rva + k;
            if (group != 0 && (r != 1 || g_fake.hookCount != 2 || Detour(group == 1 ? RENDER_STEP : TASK_STEP) != NULL ||
                Detour(group == 1 ? TASK_STEP : RENDER_STEP) == NULL || Detour(TRIM) == NULL) && reachMissed++ == 0) first = kRpmallocBytes[i].rva + k;
            if (group != 0) reachNoted &= fake_host_logged(group == 1 ? "the renderer not reached: the code at 0x" : "threads that run Havok tasks not reached: the code at 0x");
            g_exe[kRpmallocBytes[i].rva + k] ^= 0x5A;
            SetQ((uintptr_t)(g_exe + MAP_AT), (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT)); /* as before an install that went in */
            SetQ((uintptr_t)(g_exe + UNMAP_AT), (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
        }
    }
    CHECK(missed == 0 && reachMissed == 0 && reachNoted);
    if (missed || reachMissed) printf("  first byte not handled at 0x%X\n", first);
    Setup(1); g_exe[0x113EEEu] ^= 0x5A;
    CHECK(CoXlTestSrInstall() == 1);
    CHECK(fake_host_logged("lists are taken at trims and the tick, Havok's task steps"));
    CHECK(!fake_host_logged("the renderer's messages"));
    g_exe[0x113EEEu] ^= 0x5A;
    Setup(1); g_exe[TRIM] ^= 0x5A;
    CHECK(CoXlTestSrInstall() == -1 && g_fake.hookCount == 0);
    g_exe[TRIM] ^= 0x5A;
    CHECK(fake_host_logged("is not the reviewed one"));
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) == (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT));

    g_case = "rpmalloc set up otherwise";
    Setup(1); SetQ((uintptr_t)(g_exe + MAP_AT), 0x1234);
    CHECK(CoXlTestSrInstall() == -1 && Q((uintptr_t)(g_exe + UNMAP_AT)) == (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
    Setup(1); SetD((uintptr_t)(g_exe + INITIALISED), 0);
    CHECK(CoXlTestSrInstall() == -1);
    Setup(1); SetD((uintptr_t)(g_exe + 0x27B0DF0u + 8 * CLASS), 1024);
    CHECK(CoXlTestSrInstall() == -1);
    CHECK(fake_host_logged("size classes are not the reviewed ones"));

    g_case = "a refused trim hook changes nothing";
    Setup(1); g_fake.noHook = (uintptr_t)(g_exe + TRIM) - (uintptr_t)g_fake.image;
    CHECK(CoXlTestSrInstall() == -1 && g_fake.hookCount == 0); /* no reach hook either */
    CHECK(Q((uintptr_t)(g_exe + MAP_AT)) == (uint64_t)(uintptr_t)(g_exe + MAP_DEFAULT));
    CHECK(Q((uintptr_t)(g_exe + UNMAP_AT)) == (uint64_t)(uintptr_t)(g_exe + UNMAP_DEFAULT));
    CHECK(CoXlTestSrStep(256, 0, 100000, 1) == 0);

    g_case = "installed once";
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1 && CoXlTestSrInstall() == 1 && g_fake.hookCount == 3);
}

#ifdef CO_RESEARCH
static void Research(void)
{
    g_case = "research: the fragmentation trend per stamp removed";
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", "1");
    CHECK(CoXlTestMemProbeOn() == 1);
    Setup(1);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    uintptr_t heap = NewHeap();
    SetD(heap + 0x738, 5);
    SetQ((uintptr_t)(g_exe + HEAPS + 8 * 5), heap);
    CoXlTestSrHeap(heap);
    void* b[COUNT];
    uintptr_t span = Fill(heap, b);
    FreeElsewhere(b, 1, COUNT - 1);
    CHECK(Q(span) == 0);
    Pass(256, 100000);
    CHECK(fake_host_logged("memprobe: stranded spans: fragmentation 0 MB (classes 34-36 0 MB) at 256 stamps removed"));
    CHECK(fake_host_logged("memprobe: stranded spans: heap 5 (thread "));
    CHECK(fake_host_logged("1 of 1 spans rescued, 0.1 MB of free blocks"));
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", NULL);
    CoXlTestMemProbeOn();
}
#endif

/* The scan on its own thread, as in the game. */
static void Async(void)
{
    g_case = "the scan on its own thread";
    Setup(0);
    CHECK(CoXlTestSrInstall() == 1);
    FakeMapping();
    uintptr_t heap = NewHeap();
    CoXlTestSrHeap(heap);
    void* b[COUNT];
    uintptr_t span = Fill(heap, b);
    FreeElsewhere(b, 1, COUNT - 1);
    CHECK(CoXlTestSrStep(256, 0, 100000, 1) == 1);
    int phase = 1;
    for (int i = 0; i < 2000 && phase == 1; i++) { Sleep(1); phase = CoXlTestSrStep(256, 0, 100001, 1); }
    CHECK(phase == 2 && Partial(heap) == span);
    CHECK(CoXlTestSrStep(256, 0, 100002, 1) == 0);
}

static DWORD WINAPI Watchdog(void* unused) { Sleep(120000); printf("spanrescue: watchdog: no end after 120 s\n"); fflush(stdout); ExitProcess(3); return 0; }

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    InitializeCriticalSection(&g_graveLock);
    CloseHandle(CreateThread(NULL, 0, Watchdog, NULL, 0, NULL));
    g_exe = fake_host_exe();
    DWORD old;
    if (!VirtualProtect(g_exe + 0x667000u, 0x5000, PAGE_EXECUTE_READWRITE, &old) || !VirtualProtect(g_exe + 0x625000u, 0x1000, PAGE_EXECUTE_READWRITE, &old) ||
        !VirtualProtect(g_exe + 0x630000u, 0x1000, PAGE_EXECUTE_READWRITE, &old)) { printf("spanrescue: no executable page\n"); return 1; }
    Vanilla();
    Rescue();
    Kinds();
    Race();
    Last();
    Workers();
    Locks();
    Churning();
    Neighbour();
    Skip();
    Beside();
    Reach();
    Refusals();
    Install();
#ifdef CO_RESEARCH
    Research();
#endif
    Async();
    if (g_failed) { printf("spanrescue: %d check(s) failed\n", g_failed); return 1; }
    printf("spanrescue: all checks passed\n");
    return 0;
}
