/* Address space within reach of the game's heap objects, reserved while the game's heaps grow. The mod reaches its new
 * blocks for those objects by 32-bit displacements from them, so each block must lie within 1.75 GB of its object. On
 * Windows nothing is free there by the time the objects are built: in game (HellForge dev 55f186ef, profile bingus,
 * 2026-11-17) the +-1.75 GB around the entity state was all free when the mod loaded, the game's maps climbed about
 * 16 GB to it between 1.3 s and 4.3 s, the game object's allocation appeared at 5.65 s with 1.66 GB still free in reach,
 * and none was left at 5.98 s, two seconds before the state's constructor (KB complex-operations-near-memory-2026-11-17).
 * A reservation made as the mod loads lands about 16 GB away.
 *
 * The objects (kTargets), each with the extent its blocks' displacements span:
 * - the game object B ([game+0x3326340]; its block is 0x8347000 bytes, an rpmalloc huge map with B at its base + 0x80):
 *   the entity state (B + 0xE49F0) and the blocks reached from inside B, the pods', managers', entity records', galactic
 *   map table's and figure records' (the last about 122 MB in). A build for eight takes about 19.5 MB near it with the
 *   guards, the managers' 16.6 MB in one block.
 * - the session context ([game+0x347CEF0]; fields up to 0x1F000): the session lists' extension, 32 KB.
 * - the HUD's H2 ([game+0x346D538] + 0x24E340; tables up to 0x201000): the HUD and name tag tables of a build for
 *   eight, moved at the mission's first HUD init.
 *
 * Every map of rpmalloc's OS layer (_rpmalloc_mmap_os, exe 0x667850; HellForge's span wrappers and the mod's span rescue
 * swap rpmalloc's map pointer and still call it) is seen from game.dll's load on. Reservations of CO_HN_SIZE are kept in
 * reach in three ways; how a PC lays out its address space decides which one serves it, and the summary line says which:
 * - Startup, from game.dll's load to the entity state's constructor (or CO_HN_TRAIL_MS after the install when no
 *   constructor hook went in), on the thread that mapped:
 *   - Large: a map of CO_HN_LARGE or more (the game object's among them) gets, at once, a reservation reaching all of it
 *     and CO_HN_LARGE_PAST beyond, whatever its globals say yet: space beside it is still free when it is made, and the
 *     map does not return (so B is not built) before the reservation is recorded.
 *   - Trail: a map outside every area handled so far gets a reservation reaching the whole area around it (the map and
 *     CO_HN_STEP on both sides): beside it, else the nearest free space in reach (a wide search). An area where none is
 *     found is kept too, so it is not searched again.
 * - Watch, for the session: the three globals. A new object gets a reservation with room for its blocks within reach
 *   of its whole extent, unless one is there already (the session context may be set after the constructor or
 *   replaced; H2 is made in the mission). At startup the mapping thread does it; after startup a map that sees a global
 *   change only wakes the mod's worker thread, which also looks every CO_HN_WORKER_MS (an object no map follows), and
 *   every take (co_alloc_near_as) first finishes any change still pending and waits for searches in flight (at most
 *   CO_HN_WAIT_MS in all), so a reservation is in place before a block is needed, whatever the timing. A target whose searches keep finding nothing stops after CO_HN_FAILS of them; its last CO_HN_RECENT objects
 *   are not searched for again.
 * Searches run without the lock (plan under it, search, record under it); one target or area is searched for by one
 * thread at a time. CO_HN_KEPT reservation slots are kept for large maps and the watch. co_alloc_near_as takes committed
 * pages from a reservation in reach of a heap target before it searches; targets in game.dll's or the exe's image are
 * never served from them. Blocks are 64 KB aligned with an uncommitted 64 KB after each, so an overrun faults as a
 * separate allocation's would; a block freed is decommitted and its space handed out again. Only address space is
 * reserved: no commit until a block is taken. Reservations stay for the session (at most CO_HN_SPACES x 32 MB of address
 * space), never below CO_HN_FLOOR (4 GB: the game's LuaJIT is not GC64 and needs the low 2 GB). game.dll is pinned at
 * the install: the hook reads its globals on every map, to the process's end. Once the process is exiting
 * (RtlDllShutdownInProgress) startup maps do nothing, as another thread may have been ended holding the lock; after
 * startup a map takes no lock.
 *
 * Cost: none per frame. After startup each rpmalloc map reads three globals and compares them, with no lock and no call
 * (unmeasured in game); a changed one sets an event. At startup each map takes the lock shared to compare with the areas
 * so far (at most CO_HN_AREAS), exclusive only for a new area, a large map or a changed global; the summary line gives
 * the hook's own time over all startup maps. A search on a game thread makes at most CO_HN_QUERIES VirtualQuery calls
 * without the lock: 4.6 ms at the typical in-game 4.5 us, up to 0.6 s at the 0.57 ms p95; beside a map 1 to 8; on the
 * worker, which nothing waits for, at most CO_HN_QUERIES_WORKER (reaching farther in a packed heap). The worker wakes
 * once a second to read the three globals; the summary line gives the
 * searches' total and longest time (6 searches took 0.1 ms in all, in game, 2026-11-17). Searches: one per area beside
 * its map, at most CO_HN_WIDE_MAX wide ones and CO_HN_LARGE_MAX for large maps, all at startup; one per new watched
 * object (at most CO_HN_FAILS failing ones per target in a session), on the worker or on a take's thread. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "co.h"

#define CO_HN_MAP 0x667850u
#define CO_HN_STEP 0x10000000u    /* 256 MB on either side of a map: one area */
#define CO_HN_SIZE 0x2000000u     /* 32 MB: a build for eight's 19.5 MB near the game object, with room left */
#define CO_HN_SPACES 512          /* reservations */
#define CO_HN_KEPT 64             /* of them, slots only large maps and the watch may fill */
#define CO_HN_AREAS 1024          /* trail areas: 256 GB of growth */
#define CO_HN_BESIDE 0x4000000u   /* 64 MB beside a map */
#define CO_HN_GRAIN 0x10000u      /* also each block's guard */
#define CO_HN_SPAN 0x70000000u    /* the reach co_alloc_near's search keeps: 1.75 GB */
#define CO_HN_QUERIES 1024        /* VirtualQuery calls one search may make on a thread of the game's */
#define CO_HN_QUERIES_WORKER 16384 /* and on the mod's worker, which nothing waits for */
#define CO_HN_WORKER_MS 1000u     /* the worker also looks at the globals this often */
#define CO_HN_WIDE_MAX 16
#define CO_HN_LARGE 0x4000000u    /* 64 MB: a map this large gets its own reservation at startup */
#define CO_HN_LARGE_PAST 0x100000u /* the game object's extent runs 0xB9080 past its block */
#define CO_HN_LARGE_MAX 128
/* No reservation below 4 GB: the game's LuaJIT (lua51.dll, 2.1.0-alpha, not GC64) needs its memory in the low 2 GB.
 * On Windows the game's maps lie far above (helldivers2.exe has high-entropy ASLR); under Wine they may not. */
#define CO_HN_FLOOR 0x100000000ull
#define CO_HN_FAILS 4             /* failed searches a watched target may make in a session */
#define CO_HN_RECENT 8
#define CO_HN_TRAIL_MS 120000ull
#define CO_HN_WAIT_MS 2000u       /* how long a take or the constructor waits for searches in flight */
#define CO_HN_TAKEN 1024
#define CO_HN_FREED 1024
#define CO_HN_STATE_GLOBAL 0x346BF98u /* the entity state, set by its constructor */

static const char kMapPin[] = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20";

typedef void* (*CoHnMapFn)(size_t size, size_t* offset);
enum { CO_HN_BY_BESIDE, CO_HN_BY_WIDE, CO_HN_BY_LARGE, CO_HN_BY_WATCH, CO_HN_BYS };
static const char* const kBy[CO_HN_BYS] = {"the trail (beside a map)", "the trail (a wide search)", "a large map's reservation",
                                           "the watch"};
enum { CO_HN_OFF, CO_HN_STARTUP, CO_HN_SESSION };
typedef struct { uintptr_t base; size_t used; int by; } CoHnSpace;
typedef struct { uintptr_t at; size_t size, step; int space; } CoHnTaken; /* space -1: the exe arena's */
typedef struct { uintptr_t at; size_t step; int space; } CoHnFreed;
typedef struct { uintptr_t lo, hi; } CoHnArea;
/* A search planned under the lock and run without it. */
typedef struct { int by, target, wide; uintptr_t lo, hi, beside; size_t need; uintptr_t base; } CoHnJob;
enum { CO_HN_GAME, CO_HN_SESSION_CONTEXT, CO_HN_HUD, CO_HN_TARGETS };
static const struct { uint32_t global, add, extent, need; const char* name; } kTargets[CO_HN_TARGETS] = {
    {0x3326340u, 0, 0x8400000u, 0x1400000u, "the game object"},
    {0x347CEF0u, 0, 0x100000u, 0x100000u, "the session context"},
    {0x346D538u, 0x24E340u, 0x210000u, 0x200000u, "the HUD"},
};

static struct
{
    SRWLOCK lock;
    CONDITION_VARIABLE done;   /* a search was recorded */
    CoHnMapFn map;
    uintptr_t game, gameEnd, exe, exeEnd;
    volatile LONG watching, phase, settled, expired;
    int constructorHooked;
    ULONGLONG trailUntil;
    HANDLE wake;               /* the worker's: a global changed after startup */
    CoHnSpace space[CO_HN_SPACES];
    int count;
    CoHnArea area[CO_HN_AREAS];
    int areas, wide, large;
    unsigned noRoom;
    uintptr_t recent[CO_HN_TARGETS][CO_HN_RECENT]; /* each target's last objects checked, newest at [0] */
    int served[CO_HN_TARGETS];                     /* the space serving the newest, + 1; 0 when none does */
    int fails[CO_HN_TARGETS], searching[CO_HN_TARGETS];
    CoHnTaken taken[CO_HN_TAKEN];
    int takenCount;
    CoHnFreed freed[CO_HN_FREED];
    int freedCount;
    volatile LONG64 searchTicks, longestTicks, hookTicks;
    volatile LONG searches, hookMaps;
    ULONGLONG installedAt, gameSeen; /* when the hook went in, and when the game object was first seen (0: not yet) */
} g_hn = {SRWLOCK_INIT, CONDITION_VARIABLE_INIT};

static int Near(uintptr_t a, uintptr_t b, uintptr_t span) { return a > b ? a - b < span : b - a < span; }
static uintptr_t Up(uintptr_t at) { return (at + CO_HN_GRAIN - 1) & ~(uintptr_t)(CO_HN_GRAIN - 1); }
static uintptr_t Down(uintptr_t at) { return at & ~(uintptr_t)(CO_HN_GRAIN - 1); }
/* 1 when every byte of [base, base + CO_HN_SIZE) is within reach of every byte of [lo, hi]. */
static int Reaches(uintptr_t base, uintptr_t lo, uintptr_t hi)
{
    return Near(base, lo, CO_HN_SPAN) && Near(base + CO_HN_SIZE, lo, CO_HN_SPAN) && Near(base, hi, CO_HN_SPAN) &&
           Near(base + CO_HN_SIZE, hi, CO_HN_SPAN);
}

/* ---- searching: no shared state, run without the lock ---- */

/* A reservation of CO_HN_SIZE at `at` when the free region `m` (queried at `at`) holds it; 0 otherwise. */
static uintptr_t Take(uintptr_t at, const MEMORY_BASIC_INFORMATION* m)
{
    const uintptr_t end = (uintptr_t)m->BaseAddress + m->RegionSize;
    if (at < CO_HN_FLOOR || m->State != MEM_FREE || at < (uintptr_t)m->BaseAddress || end <= at || end - at < CO_HN_SIZE) return 0;
    return (uintptr_t)VirtualAlloc((void*)at, CO_HN_SIZE, MEM_RESERVE, PAGE_READWRITE);
}

/* The nearest free CO_HN_SIZE whose whole extent lies in [low, high], from `center` up and down alternately, in at most
 * `queries` VirtualQuery calls; 0 when none. */
static uintptr_t ReserveIn(uintptr_t center, uintptr_t low, uintptr_t high, int queries)
{
    if (low < CO_HN_FLOOR) low = CO_HN_FLOOR;
    if (center < low) center = low;
    if (high <= low) return 0;
    uintptr_t up = Up(center), down = Down(center);
    int upOpen = 1, downOpen = 1;
    MEMORY_BASIC_INFORMATION m;
    /* A free run around the centre: the first candidate below ends where it ends, so a run that crosses the centre
     * with neither half big enough is still tried whole. */
    if (queries > 0 && VirtualQuery((void*)Down(center), &m, sizeof m) != 0 && m.State == MEM_FREE)
    {
        const uintptr_t end = Down((uintptr_t)m.BaseAddress + m.RegionSize);
        down = end < high ? end : Down(high);
    }
    queries--;
    while ((upOpen || downOpen) && queries > 0)
    {
        if (upOpen)
        {
            queries--;
            if (up < center || up > high || high - up < CO_HN_SIZE || VirtualQuery((void*)up, &m, sizeof m) == 0) upOpen = 0;
            else
            {
                const uintptr_t got = Take(up, &m);
                if (got != 0) return got;
                up = Up((uintptr_t)m.BaseAddress + m.RegionSize);
            }
        }
        if (downOpen && queries > 0)
        {
            queries--;
            /* the candidate ends at `down`; VirtualQuery answers from the queried page on, so ask at its start */
            if (down < CO_HN_SIZE || down - CO_HN_SIZE < low) { downOpen = 0; continue; }
            const uintptr_t at = Down(down - CO_HN_SIZE);
            if (VirtualQuery((void*)at, &m, sizeof m) == 0) { downOpen = 0; continue; }
            const uintptr_t got = Take(at, &m);
            if (got != 0) return got;
            /* Free but short: something starts where the free run ends, and a candidate ending there may fit (the run
             * can begin below `at`). Taken: the next candidate ends below its allocation. Always downward. */
            const uintptr_t next = m.State == MEM_FREE ? Down((uintptr_t)m.BaseAddress + m.RegionSize) : Down((uintptr_t)m.AllocationBase);
            down = next < down ? next : at;
        }
    }
    return 0;
}

/* A new reservation reaching all of [lo, hi], nearest its middle; 0 when none (or [lo, hi] is too wide). */
static uintptr_t ReserveFor(uintptr_t lo, uintptr_t hi, int queries)
{
    if (hi < lo || hi - lo >= CO_HN_SPAN - CO_HN_SIZE) return 0;
    const uintptr_t reach = CO_HN_SPAN - 2 * CO_HN_GRAIN; /* Near is strict */
    const uintptr_t low = hi > reach ? hi - reach : 0, high = lo + reach;
    return ReserveIn(lo + (hi - lo) / 2, low, high, queries);
}

static void Run(CoHnJob* job, int queries)
{
    if (job->by != CO_HN_BY_BESIDE) { job->base = ReserveFor(job->lo, job->hi, queries); return; }
    job->base = ReserveIn(job->beside, job->lo, job->beside + CO_HN_BESIDE, 8);
    if (job->base != 0 && !Reaches(job->base, job->lo, job->hi)) { VirtualFree((void*)job->base, 0, MEM_RELEASE); job->base = 0; }
    if (job->base != 0) { job->wide = -job->wide; return; } /* the wide search planned was not needed: given back */
    if (job->wide) { job->by = CO_HN_BY_WIDE; job->base = ReserveFor(job->lo, job->hi, queries); }
}

static void Search(CoHnJob* jobs, int n, int queries)
{
    for (int i = 0; i < n; i++)
    {
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        Run(&jobs[i], queries);
        QueryPerformanceCounter(&b);
        const LONG64 took = b.QuadPart - a.QuadPart;
        InterlockedAdd64(&g_hn.searchTicks, took);
        LONG64 longest = g_hn.longestTicks;
        while (took > longest && InterlockedCompareExchange64(&g_hn.longestTicks, took, longest) != longest) longest = g_hn.longestTicks;
        InterlockedIncrement(&g_hn.searches);
    }
}

/* ---- state, under the lock ---- */

/* The trail may not fill the last CO_HN_KEPT slots. 1 when kept. */
static int Add(uintptr_t base, int by)
{
    const int limit = by == CO_HN_BY_BESIDE || by == CO_HN_BY_WIDE ? CO_HN_SPACES - CO_HN_KEPT : CO_HN_SPACES;
    if (base == 0) return 0;
    if (g_hn.count >= limit) { VirtualFree((void*)base, 0, MEM_RELEASE); return 0; }
    /* The first 64 KB stay unused: a block handed out never starts the reservation, so a VirtualFree with MEM_RELEASE
     * on it fails instead of releasing every block in it. */
    g_hn.space[g_hn.count++] = (CoHnSpace){base, CO_HN_GRAIN, by};
    return 1;
}

/* The bytes a reservation can still give: what is left at its end, or its largest freed range. */
static size_t Room(int i)
{
    size_t room = CO_HN_SIZE - g_hn.space[i].used;
    for (int k = 0; k < g_hn.freedCount; k++)
        if (g_hn.freed[k].space == i && g_hn.freed[k].step > room) room = g_hn.freed[k].step;
    return room;
}

/* The first reservation with `need` bytes of room that reaches all of [lo, hi], + 1; 0 when none. */
static int Served(uintptr_t lo, uintptr_t hi, size_t need)
{
    for (int i = 0; i < g_hn.count; i++)
        if (Room(i) >= need && Reaches(g_hn.space[i].base, lo, hi)) return i + 1;
    return 0;
}

static int InArea(uintptr_t start, uintptr_t end)
{
    for (int i = 0; i < g_hn.areas; i++)
        if (start >= g_hn.area[i].lo && end <= g_hn.area[i].hi) return 1;
    return 0;
}

static int PlanLarge(CoHnJob* job, uintptr_t start, uintptr_t end)
{
    const uintptr_t hi = end + CO_HN_LARGE_PAST;
    if (end - start < CO_HN_LARGE || g_hn.large >= CO_HN_LARGE_MAX || Served(start, hi, kTargets[CO_HN_GAME].need)) return 0;
    g_hn.large++;
    *job = (CoHnJob){CO_HN_BY_LARGE, -1, 0, start, hi, 0, kTargets[CO_HN_GAME].need, 0};
    return 1;
}

static int PlanTrail(CoHnJob* job, uintptr_t start, uintptr_t end)
{
    if (InArea(start, end) || g_hn.areas == CO_HN_AREAS || end - start >= CO_HN_SPAN) return 0;
    const CoHnArea a = {start > CO_HN_STEP ? start - CO_HN_STEP : 0, end + CO_HN_STEP};
    g_hn.area[g_hn.areas++] = a; /* claimed now: no other thread searches for this area */
    if (Served(a.lo, a.hi, CO_HN_SIZE / 2)) return 0;
    const int wide = g_hn.wide < CO_HN_WIDE_MAX;
    g_hn.wide += wide; /* given back if the search beside the map is enough */
    *job = (CoHnJob){CO_HN_BY_BESIDE, -1, wide, a.lo, a.hi, end, CO_HN_SIZE / 2, 0};
    return 1;
}

static int Recent(int t, uintptr_t lo)
{
    for (int k = 0; k < CO_HN_RECENT; k++)
        if (g_hn.recent[t][k] == lo) return 1;
    return 0;
}

/* Each target whose global holds an object other than its newest, and that no thread is searching for now (it is seen
 * again once that search is recorded); a search for it when no reservation serves it. */
static int PlanWatch(CoHnJob* jobs)
{
    int n = 0;
    if (g_hn.game == 0) return 0;
    for (int t = 0; t < CO_HN_TARGETS; t++)
    {
        const uintptr_t object = *(volatile const uintptr_t*)(g_hn.game + kTargets[t].global);
        const uintptr_t lo = object + kTargets[t].add, hi = lo + kTargets[t].extent;
        if (object == 0 || lo == g_hn.recent[t][0] || g_hn.searching[t]) continue;
        const int known = Recent(t, lo);
        if (t == CO_HN_GAME && g_hn.recent[t][0] == 0) g_hn.gameSeen = GetTickCount64();
        memmove(&g_hn.recent[t][1], &g_hn.recent[t][0], sizeof g_hn.recent[t] - sizeof g_hn.recent[t][0]);
        g_hn.recent[t][0] = lo;
        g_hn.served[t] = Served(lo, hi, kTargets[t].need);
        if (g_hn.served[t] != 0 || known || g_hn.fails[t] >= CO_HN_FAILS) continue;
        g_hn.searching[t] = 1;
        jobs[n++] = (CoHnJob){CO_HN_BY_WATCH, t, 0, lo, hi, 0, kTargets[t].need, 0};
    }
    return n;
}

static void Record(const CoHnJob* job)
{
    if (job->wide < 0) g_hn.wide--;
    uintptr_t base = job->base;
    /* another thread's reservation may serve it by now: this one goes back */
    if (base != 0 && Served(job->lo, job->hi, job->need)) { VirtualFree((void*)base, 0, MEM_RELEASE); base = 0; }
    else if (!Add(base, job->by) && job->target < 0) g_hn.noRoom++;
    if (job->target < 0) return;
    const int t = job->target;
    g_hn.searching[t] = 0;
    if (base == 0 && !Served(job->lo, job->hi, job->need)) g_hn.fails[t]++;
    if (g_hn.recent[t][0] == job->lo) g_hn.served[t] = Served(job->lo, job->hi, job->need);
}

/* 1 when a global holds an object other than its newest. Reads only, no lock. */
static int Changed(void)
{
    const uintptr_t game = g_hn.game;
    if (game == 0) return 0;
    for (int t = 0; t < CO_HN_TARGETS; t++)
    {
        const uintptr_t object = *(volatile const uintptr_t*)(game + kTargets[t].global);
        if (object != 0 && object + kTargets[t].add != g_hn.recent[t][0]) return 1;
    }
    return 0;
}

static int Searching(void)
{
    for (int t = 0; t < CO_HN_TARGETS; t++)
        if (g_hn.searching[t]) return 1;
    return 0;
}

/* ---- the watch, outside the lock ---- */

/* Plans, searches and records the watch's changes now, on this thread. */
static void WatchNow(int queries)
{
    CoHnJob jobs[CO_HN_TARGETS];
    AcquireSRWLockExclusive(&g_hn.lock);
    const int n = PlanWatch(jobs);
    ReleaseSRWLockExclusive(&g_hn.lock);
    if (n == 0) return;
    Search(jobs, n, queries);
    AcquireSRWLockExclusive(&g_hn.lock);
    for (int i = 0; i < n; i++) Record(&jobs[i]);
    ReleaseSRWLockExclusive(&g_hn.lock);
    WakeAllConditionVariable(&g_hn.done);
}

/* Waits, until `until` at the latest, while a thread is searching for a target. */
static void WaitSearches(ULONGLONG until)
{
    AcquireSRWLockExclusive(&g_hn.lock);
    for (ULONGLONG now = GetTickCount64(); Searching() && now < until; now = GetTickCount64())
        SleepConditionVariableSRW(&g_hn.done, &g_hn.lock, (DWORD)(until - now), 0);
    ReleaseSRWLockExclusive(&g_hn.lock);
}

/* Before a block is taken or the state settled: searches in flight finished, changes still pending searched for.
 * Waits CO_HN_WAIT_MS in all at most (a search of its own adds at most `queries` VirtualQuery calls). */
static void Ensure(int queries)
{
    if (!g_hn.watching) return;
    const ULONGLONG until = GetTickCount64() + CO_HN_WAIT_MS;
    for (int round = 0; round < 3 && (Changed() || Searching()) && GetTickCount64() < until; round++)
    {
        WaitSearches(until);
        WatchNow(queries);
    }
}

static volatile LONG g_workerBusy;

static DWORD WINAPI Worker(LPVOID unused)
{
    (void)unused;
    for (;;)
    {
        /* woken by a map that saw a change, or every CO_HN_WORKER_MS: an object no map follows is seen too */
        WaitForSingleObject(g_hn.wake, CO_HN_WORKER_MS);
        InterlockedIncrement(&g_workerBusy);
        if (Changed()) Ensure(CO_HN_QUERIES_WORKER);
        InterlockedDecrement(&g_workerBusy);
    }
}

/* ---- the hook ---- */

/* The summary, in two lines (co_log formats into 512 bytes): the work done, and how each target is served. */
typedef struct { char work[480], targets[480]; } CoHnSummary;
static void Summary(CoHnSummary* out, const char* when);

/* ntdll's RtlDllShutdownInProgress (reads the PEB, no system call): 1 once the process is exiting, when another thread
 * may have been ended holding the lock. Resolved at the install; HellForge's span rescue makes the same check. */
typedef BOOLEAN (NTAPI* CoHnShutdownFn)(void);
static CoHnShutdownFn g_shutdown;

/* At startup, on the thread that mapped: shared first, exclusive only when there is something to do. */
static void Startup(void* p, size_t size)
{
    if (g_shutdown != NULL && g_shutdown()) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    const uintptr_t start = (uintptr_t)p, end = start + size;
    const int due = !g_hn.constructorHooked && GetTickCount64() >= g_hn.trailUntil;
    AcquireSRWLockShared(&g_hn.lock);
    const int work = due || Changed() ||
                     (p != NULL && (!InArea(start, end) ||
                                    (size >= CO_HN_LARGE && g_hn.large < CO_HN_LARGE_MAX &&
                                     !Served(start, end + CO_HN_LARGE_PAST, kTargets[CO_HN_GAME].need))));
    ReleaseSRWLockShared(&g_hn.lock);
    if (work)
    {
        CoHnJob jobs[2 + CO_HN_TARGETS];
        int n = 0, ended = 0;
        AcquireSRWLockExclusive(&g_hn.lock);
        if (g_hn.phase == CO_HN_STARTUP && !g_hn.constructorHooked && GetTickCount64() >= g_hn.trailUntil)
        {
            InterlockedExchange(&g_hn.phase, CO_HN_SESSION);
            ended = InterlockedExchange(&g_hn.expired, 1) == 0;
        }
        if (p != NULL && g_hn.phase == CO_HN_STARTUP)
        {
            n += PlanLarge(&jobs[n], start, end);
            n += PlanTrail(&jobs[n], start, end);
        }
        n += PlanWatch(&jobs[n]);
        ReleaseSRWLockExclusive(&g_hn.lock);
        Search(jobs, n, CO_HN_QUERIES);
        CoHnSummary line;
        AcquireSRWLockExclusive(&g_hn.lock);
        for (int i = 0; i < n; i++) Record(&jobs[i]);
        if (ended) Summary(&line, "(startup ended at its deadline, no constructor settled it)");
        ReleaseSRWLockExclusive(&g_hn.lock);
        if (n != 0) WakeAllConditionVariable(&g_hn.done);
        if (ended) { co_log("%s", line.work); co_log("%s", line.targets); }
    }
    QueryPerformanceCounter(&t1);
    InterlockedAdd64(&g_hn.hookTicks, t1.QuadPart - t0.QuadPart);
    InterlockedIncrement(&g_hn.hookMaps);
}

static void* MapDetour(size_t size, size_t* offset)
{
    void* p = g_hn.map(size, offset);
    if (!g_hn.watching) return p;
    if (g_hn.phase == CO_HN_STARTUP) Startup(p, size);
    else if (Changed() && g_hn.wake != NULL) SetEvent(g_hn.wake); /* the worker plans and searches, not this thread */
    return p;
}

/* ---- the summary line ---- */

static const char* Said(int t, char* text, size_t size)
{
    if (g_hn.recent[t][0] == 0) return "not made yet";
    if (g_hn.served[t] == 0) return g_hn.fails[t] >= CO_HN_FAILS ? "NOT SERVED (no more searches)" : "NOT SERVED";
    snprintf(text, size, "served by %s", kBy[g_hn.space[g_hn.served[t] - 1].by]);
    return text;
}

/* Written under the lock, logged after it. */
static void Summary(CoHnSummary* out, const char* when)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const double ms = 1000.0 / (double)(f.QuadPart ? f.QuadPart : 1);
    char said[CO_HN_TARGETS][48], margin[64];
    /* how long before the game object the hook went in: the margin a faster PC eats into */
    if (g_hn.gameSeen == 0) snprintf(margin, sizeof margin, "the game object not seen yet");
    else if (g_hn.gameSeen - g_hn.installedAt < 50) snprintf(margin, sizeof margin, "the game object already made at the install");
    else snprintf(margin, sizeof margin, "the game object seen %llu ms after the install", (unsigned long long)(g_hn.gameSeen - g_hn.installedAt));
    snprintf(out->work, sizeof out->work,
        "heap reservations %s: %d of %u MB (%d areas of the heaps' growth handled, %d large maps searched for on their own; %u found "
        "no room, %d wide searches; %ld searches took %.1f ms, the longest %.1f ms; the hook's own work on %ld maps at startup "
        "took %.1f ms)",
        when, g_hn.count, CO_HN_SIZE >> 20, g_hn.areas, g_hn.large, g_hn.noRoom, g_hn.wide, (long)g_hn.searches,
        (double)g_hn.searchTicks * ms, (double)g_hn.longestTicks * ms, (long)g_hn.hookMaps, (double)g_hn.hookTicks * ms);
    snprintf(out->targets, sizeof out->targets, "heap reservations: %s %s, %s %s, %s %s; %s",
        kTargets[CO_HN_GAME].name, Said(CO_HN_GAME, said[0], sizeof said[0]), kTargets[CO_HN_SESSION_CONTEXT].name,
        Said(CO_HN_SESSION_CONTEXT, said[1], sizeof said[1]), kTargets[CO_HN_HUD].name, Said(CO_HN_HUD, said[2], sizeof said[2]), margin);
}

/* ---- the mod's calls ---- */

int co_heap_near_install(uintptr_t exe, uintptr_t game)
{
    if (exe == 0 || game == 0 || g_hn.map != NULL) return g_hn.map != NULL;
    if (g_coApi->code_matches(exe + CO_HN_MAP, kMapPin) != 1)
    { co_log("heap reservations: rpmalloc's map is not the reviewed code; blocks near the game's heap are searched for as before"); return 0; }
    /* The hook reads game.dll's globals on every map until the process ends: game.dll stays mapped. The game never
     * unloads it; a pin that fails is said and the hook goes in all the same. */
    HMODULE pinned = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)game, &pinned) || pinned != (HMODULE)game)
        co_log("heap reservations: game.dll could not be pinned (error %lu); the game keeps it loaded", GetLastError());
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    g_shutdown = ntdll != NULL ? (CoHnShutdownFn)(void*)GetProcAddress(ntdll, "RtlDllShutdownInProgress") : NULL;
    size_t gameSize = 0, exeSize = 0;
    g_coApi->module_base("game.dll", &gameSize);
    g_coApi->module_base("helldivers2.exe", &exeSize);
    g_hn.game = game; g_hn.gameEnd = game + gameSize; g_hn.exe = exe; g_hn.exeEnd = exe + exeSize;
    if (g_coApi->hook(g_coMod, exe + CO_HN_MAP, (void*)MapDetour, (void**)&g_hn.map) != 0 || g_hn.map == NULL)
    {
        g_hn.map = NULL; g_hn.game = 0;
        co_log("heap reservations: rpmalloc's map could not be hooked; blocks near the game's heap are searched for as before");
        return 0;
    }
    if (g_hn.wake == NULL)
    {
        HANDLE wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        HANDLE worker = NULL;
        g_hn.wake = wake; /* set before the thread: it waits on it */
        if (wake != NULL) worker = CreateThread(NULL, 64 * 1024, Worker, NULL, 0, NULL);
        if (worker == NULL)
        {
            g_hn.wake = NULL; /* changes after startup are then searched for at the next take */
            if (wake != NULL) CloseHandle(wake);
            co_log("heap reservations: no worker thread (error %lu); changes after startup are handled when a block is needed", GetLastError());
        }
        else CloseHandle(worker);
    }
    AcquireSRWLockExclusive(&g_hn.lock);
    g_hn.trailUntil = GetTickCount64() + CO_HN_TRAIL_MS;
    g_hn.installedAt = GetTickCount64();
    InterlockedExchange(&g_hn.phase, CO_HN_STARTUP);
    InterlockedExchange(&g_hn.watching, 1);
    ReleaseSRWLockExclusive(&g_hn.lock);
    WatchNow(CO_HN_QUERIES); /* objects the game made before this event: reserved for now, while there may be room */
    return 1;
}

void co_heap_near_constructor_hooked(void)
{
    AcquireSRWLockExclusive(&g_hn.lock);
    /* Startup runs until the constructor, however long the game takes to it; not when the state was built before the
     * hook (its constructor then never settles): the deadline ends it. */
    if (g_hn.game == 0 || *(volatile const uintptr_t*)(g_hn.game + CO_HN_STATE_GLOBAL) == 0) g_hn.constructorHooked = 1;
    ReleaseSRWLockExclusive(&g_hn.lock);
}

void co_heap_near_settle(uintptr_t state)
{
    if (g_hn.game == 0 || InterlockedExchange(&g_hn.settled, 1) != 0) return;
    InterlockedExchange(&g_hn.phase, CO_HN_SESSION);
    Ensure(CO_HN_QUERIES); /* searches other threads started for the game object finish first: the constructor's blocks come next */
    CoHnSummary line;
    char when[64];
    snprintf(when, sizeof when, "at the entity state's constructor (%llx)", (unsigned long long)state);
    AcquireSRWLockExclusive(&g_hn.lock);
    Summary(&line, when);
    ReleaseSRWLockExclusive(&g_hn.lock);
    co_log("%s", line.work);
    co_log("%s", line.targets);
}

/* Under the lock: a range of `step` bytes in a reservation in reach of `target`, freed space first; 0 when none. */
static uintptr_t Claim(uintptr_t target, size_t bytes, size_t step, int* space)
{
    for (int k = 0; k < g_hn.freedCount; k++)
    {
        CoHnFreed* f = &g_hn.freed[k];
        if (f->step < step || !Near(f->at, target, CO_HN_SPAN) || !Near(f->at + bytes, target, CO_HN_SPAN)) continue;
        const uintptr_t at = f->at;
        *space = f->space;
        if (f->step == step) *f = g_hn.freed[--g_hn.freedCount];
        else { f->at += step; f->step -= step; }
        return at;
    }
    for (int i = 0; i < g_hn.count; i++)
    {
        CoHnSpace* s = &g_hn.space[i];
        const uintptr_t at = s->base + s->used;
        if (step > CO_HN_SIZE - s->used || !Near(at, target, CO_HN_SPAN) || !Near(at + bytes, target, CO_HN_SPAN)) continue;
        s->used += step;
        *space = i;
        return at;
    }
    return 0;
}

/* Under the lock: a range back to its reservation, at its end when it is the newest, else to the freed list. */
static void Unclaim(uintptr_t at, size_t step, int space)
{
    CoHnSpace* s = &g_hn.space[space];
    if (s->base + s->used == at + step) { s->used -= step; return; }
    if (g_hn.freedCount < CO_HN_FREED) g_hn.freed[g_hn.freedCount++] = (CoHnFreed){at, step, space};
}

uintptr_t co_heap_near_take(uintptr_t target, size_t size)
{
    const size_t bytes = (size + CO_HN_GRAIN - 1) & ~(size_t)(CO_HN_GRAIN - 1), step = bytes + CO_HN_GRAIN; /* and its guard */
    /* code targets keep their own memory: a block near game.dll's or the exe's code is not a heap object's */
    if ((target >= g_hn.game && target < g_hn.gameEnd) || (target >= g_hn.exe && target < g_hn.exeEnd)) return 0;
    Ensure(CO_HN_QUERIES);
    int space = -1;
    AcquireSRWLockExclusive(&g_hn.lock);
    const uintptr_t at = Claim(target, bytes, step, &space);
    ReleaseSRWLockExclusive(&g_hn.lock);
    if (at == 0) return 0;
    /* committed outside the lock: other threads' maps do not wait for it */
    const int committed = VirtualAlloc((void*)at, bytes, MEM_COMMIT, PAGE_READWRITE) != NULL;
    AcquireSRWLockExclusive(&g_hn.lock);
    if (!committed) Unclaim(at, step, space);
    /* A full table only means this block is not given back when freed: never a reason to refuse it. */
    else if (g_hn.takenCount < CO_HN_TAKEN) g_hn.taken[g_hn.takenCount++] = (CoHnTaken){at, bytes, step, space};
    ReleaseSRWLockExclusive(&g_hn.lock);
    return committed ? at : 0;
}

void co_heap_near_track(uintptr_t block, size_t size)
{
    AcquireSRWLockExclusive(&g_hn.lock);
    if (block != 0 && g_hn.takenCount < CO_HN_TAKEN) g_hn.taken[g_hn.takenCount++] = (CoHnTaken){block, size, size, -1};
    ReleaseSRWLockExclusive(&g_hn.lock);
}

void co_free_near(uintptr_t block)
{
    if (block == 0) return;
    AcquireSRWLockExclusive(&g_hn.lock);
    for (int i = 0; i < g_hn.takenCount; i++)
        if (g_hn.taken[i].at == block)
        {
            VirtualFree((void*)block, g_hn.taken[i].size, MEM_DECOMMIT);
            if (g_hn.taken[i].space >= 0) Unclaim(block, g_hn.taken[i].step, g_hn.taken[i].space);
            g_hn.taken[i] = g_hn.taken[--g_hn.takenCount];
            ReleaseSRWLockExclusive(&g_hn.lock);
            return;
        }
    ReleaseSRWLockExclusive(&g_hn.lock);
    /* Not a tracked block: released. One no table holds (a full table) fails here and stays committed. */
    VirtualFree((void*)block, 0, MEM_RELEASE);
}

#ifdef CO_TEST
void co_heap_near_test_reset(void)
{
    Ensure(CO_HN_QUERIES);
    InterlockedExchange(&g_hn.watching, 0);
    while (g_workerBusy != 0) Sleep(1); /* the worker is done with the state before it is cleared */
    AcquireSRWLockExclusive(&g_hn.lock);
    for (int i = 0; i < g_hn.count; i++) VirtualFree((void*)g_hn.space[i].base, 0, MEM_RELEASE);
    g_hn.map = NULL;
    g_hn.game = g_hn.gameEnd = g_hn.exe = g_hn.exeEnd = 0;
    g_hn.watching = g_hn.phase = g_hn.settled = g_hn.expired = 0;
    g_hn.constructorHooked = 0;
    g_hn.trailUntil = 0;
    g_hn.count = g_hn.areas = g_hn.wide = g_hn.large = g_hn.takenCount = g_hn.freedCount = 0;
    g_hn.searches = g_hn.hookMaps = 0;
    g_hn.noRoom = 0;
    g_hn.searchTicks = g_hn.longestTicks = g_hn.hookTicks = 0;
    g_hn.installedAt = g_hn.gameSeen = 0;
    memset(g_hn.recent, 0, sizeof g_hn.recent);
    memset(g_hn.served, 0, sizeof g_hn.served);
    memset(g_hn.fails, 0, sizeof g_hn.fails);
    memset(g_hn.searching, 0, sizeof g_hn.searching);
    ReleaseSRWLockExclusive(&g_hn.lock);
}
int co_heap_near_test_count(void) { return g_hn.count; }
uintptr_t co_heap_near_test_space(int i) { return i < g_hn.count ? g_hn.space[i].base : 0; }
void co_heap_near_test_expire(void) { g_hn.trailUntil = 0; }
void co_heap_near_test_sync(void) { Ensure(CO_HN_QUERIES); }
/* Another thread's search for the game object, in flight: begun now, recorded when `finish` is called. */
static CoHnJob g_testJob;
void co_heap_near_test_begin_search(uintptr_t b)
{
    AcquireSRWLockExclusive(&g_hn.lock);
    memmove(&g_hn.recent[CO_HN_GAME][1], &g_hn.recent[CO_HN_GAME][0], sizeof g_hn.recent[0] - sizeof g_hn.recent[0][0]);
    g_hn.recent[CO_HN_GAME][0] = b;
    g_hn.searching[CO_HN_GAME] = 1;
    g_testJob = (CoHnJob){CO_HN_BY_WATCH, CO_HN_GAME, 0, b, b + kTargets[CO_HN_GAME].extent, 0, kTargets[CO_HN_GAME].need, 0};
    ReleaseSRWLockExclusive(&g_hn.lock);
}
void co_heap_near_test_finish_search(void)
{
    Search(&g_testJob, 1, CO_HN_QUERIES);
    AcquireSRWLockExclusive(&g_hn.lock);
    Record(&g_testJob);
    ReleaseSRWLockExclusive(&g_hn.lock);
    WakeAllConditionVariable(&g_hn.done);
}
uintptr_t co_heap_near_test_reserve_in(uintptr_t center, uintptr_t low, uintptr_t high, int queries)
{
    return ReserveIn(center, low, high, queries);
}
#endif
