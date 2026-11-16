/* The engine's navmesh cutters: the destroy leaves out handles of 0, and failed makes are counted (co_cutters.h). */
#include <string.h>

#include "co_cutters.h"
#include "co_shim.h"

typedef void (*DestroyFn)(const uint32_t* handles, uint32_t count);
typedef uint32_t (*MakeFn)(const void* description);
typedef void (*SwitchFn)(const uint32_t* handles, uint32_t count, int on);

static struct
{
    DestroyFn destroy;
    MakeFn make;
    uintptr_t exe;
    volatile LONG64 dropped, failed;
    volatile LONG firstFail;
    uint32_t usedAtFail, roomAtFail;
} g_cut;

/* The destroy's entry and its read of the first handle's pool; the make's entry and its "pool full" branch
 * (0x812DBA: cmp eax,-1 after the allocator, then the object is freed and 0 returned); the pool's store, room and
 * in-use counter. */
typedef struct Pin { uint32_t rva; const char* pin; } Pin;
static const Pin kCutterPins[] = {
    {CO_CUTTERS_DESTROY, "85 D2 0F 84 32 01 00 00 41 54 41 55 48 83 EC 38 48 89 5C 24 50 45 33 C0"},
    {0x8127F6u, "49 8B 9C C3 30 63 7C 02 66 90 41 8B 74 BD 00 8B CE 23 4B 28 39 4B 24"},
    {CO_CUTTERS_MAKE, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57"},
    {0x812DBAu, "83 F8 FF 0F 85 3F 03 00 00 48 8B CF E8 45 9F 18 00 8B 85 10 08 00 00"},
    {0x7D7F60u, "41 89 48 24"},
    {0x7D8090u, "4F 89 84 EC 30 63 7C 02"},
    {0x75331u, "F0 41 FF 41 20"},
    {0x754A6u, "F0 FF 49 20"},
};

/* What the record reads and what switching a cutter on will call: the "add now" test, the switch's entry, its
 * handle check and its two halves, the forest's description (place, add-now, world, type, body, the call), the
 * pool's mask and the AI world's slot; the swap of the queued changes, their count, and "a run is in flight". */
static const Pin kRecordPins[] = {
    {0x8132F8u, "83 7E 0C 00"},
    {0x812AE0u, "85 D2 0F 84 D1 01 00 00 56 41 54 41 55 41 57 48 83 EC 28"},
    {0x812B20u, "45 8B 04 B4 45 85 C0 0F 84 62 01 00 00 45 8B C8 41 8B C8 49 C1 E9 1E 4A 8D 2C CD 00 00 00 00 48 8B 84 2F 30 63 7C 02 23 48 28 39 48 24"},
    {0x812BB4u, "4D 8B B4 39 A0 D4 90 01"},
    {0x812BBCu, "45 85 ED 74 5E 48 85 C0 0F 85 C5 00 00 00 48 8B AC 2F 50 63 7C 02 48 0B D1 48 8B CD 48 89 53 18 FF 15 D6 3A BE 00 48 8B 7B 18 48 8B CB E8 92 A0 18 00 48 8B CB E8 1A A1 18 00 48 8B D3 49 8B CE E8 3F 5C 3D 00 48 C1 EF 23 40 80 E7 03 40 80 FF 03 75 70 48 8B 83 D0 00 00 00 48 89 83 B8 00 00 00 EB 60 48 85 C0"},
    {0x81542Au, "F3 44 0F 10 29 F3 44 0F 10 71 04 F3 44 0F 10 79 08"},
    {0x8154ABu, "F3 44 0F 11 6D A0"},
    {0x815508u, "44 89 7D CC"},
    {0x815516u, "89 45 C4"},
    {0x815521u, "F3 44 0F 11 75 A4"},
    {0x815534u, "F3 44 0F 11 7D A8"},
    {0x8155D5u, "89 4D C8 48 8D 44 24 70 48 8D 4D C0 48 89 45 D0 C7 45 C0 03 00 00 00 44 89 75 E0 89 75 D8 E8 C8 D6 FF FF 8B C0"},
    {0x7D7F9Fu, "41 89 40 28 8D 48 01 0D 00 00 C0 FF F7 D0 41 89 48 34"},
    {0xBB1430u, "40 53 48 83 EC 20 48 8B D9 48 8B 89 A0 04 00 00"},
    {0xAD4740u, "48 8B 01 8B 40 08 03 41 1C C3"},
    {0xBD1190u, "48 83 B9 B0 04 00 00 00 0F 95 C0 C3"},
    {0xBE8D32u, "48 63 86 30 02 00 00 33 FF 85 C0 7E 24 48 8B D0 8B CF 48 8B 86 28 02 00 00"},
};

/* The make can run on a thread other than the game's (the forest's spawn has several callers), so the set has its
 * own lock; the tick holds it for well under a millisecond. Makes and destroys are counted by thread: a level's
 * first trees are made before any tick has said which thread the tick's is. */
#define THREADS 64
static struct
{
    volatile LONG armed, live, switching, alive, tickAt;
    SwitchFn sw;
    volatile LONG tickThread, took;
    uint64_t held, atOnce, noWorld, stock, sentOn, sentOff, skips;
    struct { LONG id; uint64_t makes, destroys; } thread[THREADS];
    uint64_t makesPast, destroysPast; /* on threads past the table: counted as off the tick's */
    SRWLOCK lock;
    CoCutSet set;
    void* memory;
    CoCutTick tick;
    uint64_t trees, others, unbound, destroyed;
    volatile LONG loadHold; int loadEnd; uint32_t loadAt, missionAt, flushCursor; uint64_t loadHeld, flushed; /* this level's load */
} g_rec;

/* The mission world's AI world and pool as they read now; 1 when there are both. The engine's switch reads the
 * pool and hands the AI world on with no test of either (0x812B3F, 0x812BB4), so it is called only with both. */
static int Worlds(uintptr_t* world, uintptr_t* pool)
{
    *world = *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD);
    *pool = *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_POOLS + 8u * CO_CUTTERS_WORLD);
    return *world != 0 && *pool != 0;
}

/* The AI world and the pool the set's handles are of, read anew: either one changed or gone drops every record. */
static int Bound(void)
{
    uintptr_t world, pool;
    uint32_t mask = g_rec.set.mask;
    if (!Worlds(&world, &pool)) return co_cutset_bind(&g_rec.set, 0, 0, 0);
    /* a mask that does not read keeps the binding as it is: dropping the records would strand held-back cutters */
    if (pool != g_rec.set.pool && co_safe_read(pool + CO_CUTTERS_MASK, &mask, 4)) return g_rec.set.world != 0;
    return co_cutset_bind(&g_rec.set, world, pool, mask);
}

/* Called under the set's lock. Entry 0 is kept for the tick's thread, so that one always has a place. */
static void Counted(int destroy)
{
    const LONG id = (LONG)GetCurrentThreadId();
    uint32_t i = g_rec.thread[0].id == id ? 0 : 1;
    while (i < THREADS && g_rec.thread[i].id != id && g_rec.thread[i].id != 0) i++;
    if (i == THREADS) { if (destroy) g_rec.destroysPast++; else g_rec.makesPast++; return; }
    g_rec.thread[i].id = id;
    if (destroy) g_rec.thread[i].destroys++; else g_rec.thread[i].makes++;
}

/* A cutter of the mission's world was made: a tree's is recorded, any other only frees its slot of an old record.
 * `lit`: the game added it itself. `held`: the mod held its add back, so with no record it is added here, at once,
 * unless the AI world or the pool went during the make: then nothing can add it, and it is counted.
 * The engine's switch reads the cutter before it takes any lock (0x812B90): it is called only under the set's lock,
 * where a destroy on another thread still waits in its hook, and for a handle of a pool that exists. */
static void Recorded(uint32_t handle, int tree, float x, float y, int wanted, int lit, int held)
{
    int made = 0;
    uintptr_t world, pool;
    AcquireSRWLockExclusive(&g_rec.lock);
    Counted(0);
    if (!g_rec.live || !Bound()) g_rec.unbound++;
    else if (!tree) { co_cutset_taken(&g_rec.set, handle); g_rec.others++; }
    else if ((made = co_cutset_made(&g_rec.set, handle, x, y, wanted)) != 0)
    {
        g_rec.trees++;
        if (lit) { g_rec.set.epoch = (uint32_t)g_rec.took; co_cutset_on(&g_rec.set, handle); g_rec.stock++; }
    }
    if (held && made && !g_rec.alive && g_rec.loadEnd == 2)
    { /* held as the load's, but the watch's flush ran during this make: no tick is there to add it */
        g_rec.set.epoch = (uint32_t)g_rec.took; co_cutset_on(&g_rec.set, handle); g_rec.sw(&handle, 1, 1);
        g_rec.held++; g_rec.loadHeld++; g_rec.flushed++; g_rec.sentOn++;
    }
    else if (held && made) { g_rec.held++; if (g_rec.loadHold && !g_rec.alive) g_rec.loadHeld++; }
    else if (held && Worlds(&world, &pool)) { g_rec.sw(&handle, 1, 1); g_rec.atOnce++; }
    else if (held) g_rec.noWorld++;
    ReleaseSRWLockExclusive(&g_rec.lock);
}

/* A make is held back only while a tick is due to add it: with the tick gone (a paused mission, a level still
 * loading) the game adds the cutter itself, so no tree is ever left without one for long. */
/* The AI world's changes queued for its next run, as 0xAD4740 counts them, -1 when they do not read; and its added
 * cutters, the length every remove scans ([H+0x230], 0xBE8D32), 0 when it does not read. */
static int Queued(uintptr_t world)
{
    uintptr_t q = 0, a = 0; int32_t n = 0, m = 0;
    if (co_safe_read(world + CO_CUTTERS_QUEUE, &q, 8) || q == 0 || co_safe_read(q, &a, 8) || a == 0 || co_safe_read(a + 8, &n, 4) || co_safe_read(q + 0x1C, &m, 4)) return -1;
    return n + m;
}
static uint32_t Added(uintptr_t world)
{
    int32_t n = 0;
    return co_safe_read(world + CO_CUTTERS_ADDED, &n, 4) || n < 0 ? 0u : (uint32_t)n;
}

static int Holds(float x, float y)
{
    const int ticking = g_rec.alive && (uint32_t)GetTickCount() - (uint32_t)g_rec.tickAt <= CO_CUTTERS_ALIVE_MS;
    if (!g_rec.switching || !(ticking || (g_rec.loadHold && !g_rec.alive))) return 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    const int holds = g_rec.live && Bound() && co_cutset_takes(&g_rec.set, x, y);
    ReleaseSRWLockExclusive(&g_rec.lock);
    return holds;
}

uint32_t co_cutters_keep(const uint32_t* handles, uint32_t count, uint32_t* kept)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++)
        if (handles[i] != 0) kept[n++] = handles[i];
    return n;
}

int co_cutters_pool(uintptr_t exe, uint32_t world, uint32_t* used, uint32_t* room)
{
    uint64_t pool = 0;
    uint32_t u = 0, r = 0;
    if (exe == 0 || world >= 4 || co_safe_read(exe + CO_CUTTERS_POOLS + 8u * world, &pool, 8) || pool == 0 ||
        co_safe_read((uintptr_t)pool + CO_CUTTERS_USED, &u, 4) || co_safe_read((uintptr_t)pool + CO_CUTTERS_ROOM, &r, 4) ||
        r == 0 || r > (1u << 21) || u > r) return 0;
    *used = u; *room = r;
    return 1;
}

int co_cutters_over(uintptr_t exe, float share)
{
    uint32_t used = 0, room = 0;
    return co_cutters_pool(exe, CO_CUTTERS_WORLD, &used, &room) && (float)used >= share * (float)room;
}

/* The game's batches pass through as they are; one with a 0 goes on without it, in the batches the callback uses. */
static void Destroy(const uint32_t* handles, uint32_t count)
{
    const DestroyFn next = g_cut.destroy;
    uint32_t i = 0;
    if (g_rec.armed) /* before the engine frees the slots: a make on another thread can take one at once */
    {
        AcquireSRWLockExclusive(&g_rec.lock);
        Counted(1);
        g_rec.destroyed += co_cutset_gone(&g_rec.set, handles, count);
        ReleaseSRWLockExclusive(&g_rec.lock);
    }
    while (i < count && handles[i] != 0) i++;
    if (i == count) { next(handles, count); return; }
    uint32_t kept[CO_CUTTERS_BATCH];
    for (uint32_t at = 0; at < count; at += CO_CUTTERS_BATCH)
    {
        const uint32_t take = count - at < CO_CUTTERS_BATCH ? count - at : CO_CUTTERS_BATCH;
        const uint32_t n = co_cutters_keep(handles + at, take, kept);
        InterlockedAdd64(&g_cut.dropped, (LONG64)(take - n));
        if (n != 0) next(kept, n);
    }
}

/* The description's +4 is the world (0x812CDF). The forest's description and its matrix are in the caller's frame
 * (0x8155D5..0x8155F3): switching clears its "add now" for the call and puts it back. */
static uint32_t MakeFrom(const void* description, uintptr_t from)
{
    const uint8_t* d = description;
    uint32_t madeIn = ~0u, type = 0, add = 0;
    int tree = 0;
    float x = 0.0f, y = 0.0f;
    if (g_rec.armed && g_rec.live)
    {
        memcpy(&type, d + CO_CUTTERS_DESC_TYPE, 4); memcpy(&madeIn, d + CO_CUTTERS_DESC_WORLD, 4);
        if (from == g_cut.exe + CO_CUTTERS_FOREST_RETURN && type == 3)
        {
            const float* matrix;
            memcpy(&matrix, d + CO_CUTTERS_DESC_MATRIX, 8); memcpy(&add, d + CO_CUTTERS_DESC_ADD, 4);
            x = matrix[12]; y = matrix[13]; tree = 1;
        }
    }
    const int held = tree && add != 0 && madeIn == CO_CUTTERS_WORLD && Holds(x, y);
    const uint32_t none = 0;
    if (held) memcpy((uint8_t*)(uintptr_t)d + CO_CUTTERS_DESC_ADD, &none, 4);
    const uint32_t handle = g_cut.make(description);
    if (held) memcpy((uint8_t*)(uintptr_t)d + CO_CUTTERS_DESC_ADD, &add, 4);
    if (handle != 0 && madeIn == CO_CUTTERS_WORLD) Recorded(handle, tree, x, y, add != 0, g_rec.switching && tree && add != 0 && !held, held);
    if (handle == 0)
    {
        InterlockedIncrement64(&g_cut.failed);
        uint32_t world = 0, used = 0, room = 0;
        if (InterlockedCompareExchange(&g_cut.firstFail, 1, 0) == 0 && !co_safe_read((uintptr_t)description + 4, &world, 4) &&
            co_cutters_pool(g_cut.exe, world, &used, &room))
        { g_cut.usedAtFail = used; g_cut.roomAtFail = room; }
    }
    return handle;
}
static uint32_t Make(const void* description) { return MakeFrom(description, (uintptr_t)__builtin_return_address(0)); }

const char* co_cutters_install(uintptr_t exe)
{
    if (g_cut.destroy != NULL) return NULL;
    if (exe == 0) return "the engine's image is not known";
    for (size_t i = 0; i < sizeof kCutterPins / sizeof kCutterPins[0]; i++)
        if (co_exe_code_matches(exe + kCutterPins[i].rva, kCutterPins[i].pin) != 1)
            return "the engine's navmesh cutter code is not the code this mod was made for";
    g_cut.exe = exe;
    if (co_loader_hook("navmesh cutters: the destroy leaves out handles of 0", exe + CO_CUTTERS_DESTROY, (void*)Destroy, (void**)&g_cut.destroy) ||
        g_cut.destroy == NULL)
    { g_cut.destroy = NULL; return "the engine's navmesh cutter destroy could not be hooked"; }
    if (co_loader_hook("navmesh cutters: failed makes are counted", exe + CO_CUTTERS_MAKE, (void*)Make, (void**)&g_cut.make) || g_cut.make == NULL)
    { g_cut.make = NULL; co_note("navmesh cutters: the make could not be hooked: failed makes are not counted"); }
    return NULL;
}

void co_cutters_seen(CoCuttersSeen* out)
{
    out->dropped = (uint64_t)g_cut.dropped;
    out->failed = (uint64_t)g_cut.failed;
    out->usedAtFail = g_cut.usedAtFail;
    out->roomAtFail = g_cut.roomAtFail;
}

const char* co_cutters_record_arm(uintptr_t exe, uint32_t cap, uint32_t adds)
{
    if (g_rec.armed) return NULL;
    if (exe == 0) return "the engine's image is not known";
    for (size_t i = 0; i < sizeof kRecordPins / sizeof kRecordPins[0]; i++)
        if (co_exe_code_matches(exe + kRecordPins[i].rva, kRecordPins[i].pin) != 1)
            return "the engine's cutter switch or the forest's make is not the code this mod was made for";
    void* memory = VirtualAlloc(NULL, co_cutset_bytes(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == NULL) return "no memory for the records";
    AcquireSRWLockExclusive(&g_rec.lock);
    g_rec.memory = memory;
    co_cutset_init(&g_rec.set, memory);
    g_rec.set.cap = cap; g_rec.set.adds = adds;
    g_rec.sw = (SwitchFn)(exe + CO_CUTTERS_SWITCH);
    ReleaseSRWLockExclusive(&g_rec.lock);
    InterlockedExchange(&g_rec.armed, 1);
    return NULL;
}

void co_cutters_switch(int on)
{
    if (!on) InterlockedExchange(&g_rec.alive, 0);
    InterlockedExchange(&g_rec.switching, on != 0);
}

uint32_t co_cutters_live(int complex)
{
    uint32_t dropped = 0;
    InterlockedExchange(&g_rec.live, complex != 0);
    InterlockedExchange(&g_rec.alive, 0); /* a level's first trees are the game's until its first tick, */
    InterlockedExchange(&g_rec.loadHold, 0); /* unless co_cutters_load_hold says otherwise */
    if (complex || !g_rec.armed) return 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    dropped = g_rec.set.records;
    co_cutset_clear(&g_rec.set);
    ReleaseSRWLockExclusive(&g_rec.lock);
    return dropped;
}

void co_cutters_load_hold(int on, uint32_t nowMs)
{
    if (!g_rec.armed) return;
    AcquireSRWLockExclusive(&g_rec.lock);
    g_rec.loadAt = nowMs; g_rec.missionAt = 0; g_rec.flushCursor = 0; g_rec.loadHeld = g_rec.flushed = 0; g_rec.loadEnd = 0;
    InterlockedExchange(&g_rec.loadHold, on && g_rec.live && g_rec.switching);
    ReleaseSRWLockExclusive(&g_rec.lock);
}

uint32_t co_cutters_load_watch(int inMission, uint32_t nowMs)
{
    if (!g_rec.armed || g_cut.exe == 0 || !g_rec.loadHold || g_rec.alive) return 0;
    if (inMission && g_rec.missionAt == 0) g_rec.missionAt = nowMs != 0 ? nowMs : 1;
    if (!(g_rec.missionAt != 0 && nowMs - g_rec.missionAt > CO_CUTTERS_LOAD_MISSION_MS) && nowMs - g_rec.loadAt <= CO_CUTTERS_LOAD_MS) return 0;
    /* the step's threads read the AI world's cutters without its lock (as in the tick) */
    if (*(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD + CO_CUTTERS_STEP_TASK) != 0) return 0;
    uint32_t sent = 0, batch[CO_CUTSET_BATCH], n;
    AcquireSRWLockExclusive(&g_rec.lock);
    if (g_rec.live && Bound()) /* a world that went took its records along: nothing is held then */
        while ((n = co_cutset_flush(&g_rec.set, &g_rec.flushCursor, batch, CO_CUTSET_BATCH)) != 0) { g_rec.sw(batch, n, 1); sent += n; }
    g_rec.flushed += sent; g_rec.sentOn += sent; g_rec.loadEnd = 2;
    InterlockedExchange(&g_rec.loadHold, 0);
    ReleaseSRWLockExclusive(&g_rec.lock);
    return sent;
}

void co_cutters_load_seen(CoCuttersLoad* out)
{
    AcquireSRWLockExclusive(&g_rec.lock);
    out->holding = g_rec.loadHold != 0; out->ended = g_rec.loadEnd; out->at = g_rec.loadAt; out->held = g_rec.loadHeld; out->flushed = g_rec.flushed;
    ReleaseSRWLockExclusive(&g_rec.lock);
}

int co_cutters_tick(const CoCutCentre* centres, uint32_t count, uint32_t nowMs, uint32_t removes, CoCuttersTick* out)
{
    LARGE_INTEGER a, b, f, h;
    if (!g_rec.armed || !g_rec.live) return 0;
    QueryPerformanceCounter(&a);
    /* the step's threads read the AI world's cutters without its lock: they run only while a step task is out */
    const int skipped = g_rec.switching && *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD + CO_CUTTERS_STEP_TASK) != 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    QueryPerformanceCounter(&h);
    InterlockedExchange(&g_rec.tickThread, (LONG)GetCurrentThreadId());
    g_rec.thread[0].id = g_rec.tickThread;
    const CoCutSet* s = &g_rec.set;
    out->hooked = g_cut.make != NULL && g_cut.destroy != NULL;
    out->bound = g_cut.exe != 0 && Bound();
    out->skipped = skipped; out->added = out->allowed = out->addsAllowed = 0; out->queued = -1;
    if (skipped) { g_rec.skips++; g_rec.tick.adds = g_rec.tick.removes = g_rec.tick.shed = g_rec.tick.waiting = g_rec.tick.over = 0; g_rec.tick.capped = 0; }
    else
    {
        /* the removes' share of the tick shrinks as the scans they cost grow, and waits while the queue is long */
        out->added = out->bound ? Added(g_rec.set.world) : 0; out->queued = out->bound ? Queued(g_rec.set.world) : -1;
        out->allowed = removes = out->bound ? co_cutset_removes(out->added, out->queued, removes) : 0;
        /* and the adds keep the queue under the drain's start: a landing's disc goes over seconds */
        const uint32_t adds = g_rec.set.adds;
        g_rec.set.adds = out->addsAllowed = out->bound ? co_cutset_adds(out->queued, adds) : adds;
        g_rec.set.epoch = (uint32_t)g_rec.took; co_cutset_tick(&g_rec.set, centres, count, nowMs, removes, &g_rec.tick);
        g_rec.set.adds = adds;
    }
    if (g_rec.switching && !skipped)
    {
        /* under the set's lock: a destroy on another thread waits in its hook, so no handle here is stale */
        for (uint32_t k = 0; k < g_rec.tick.adds; k += CO_CUTSET_BATCH)
            g_rec.sw(g_rec.tick.add + k, g_rec.tick.adds - k < CO_CUTSET_BATCH ? g_rec.tick.adds - k : CO_CUTSET_BATCH, 1);
        for (uint32_t k = 0; k < g_rec.tick.removes; k += CO_CUTSET_BATCH)
            g_rec.sw(g_rec.tick.remove + k, g_rec.tick.removes - k < CO_CUTSET_BATCH ? g_rec.tick.removes - k : CO_CUTSET_BATCH, 0);
        g_rec.sentOn += g_rec.tick.adds; g_rec.sentOff += g_rec.tick.removes;
        if (out->bound && out->hooked) { InterlockedExchange(&g_rec.tickAt, (LONG)nowMs); InterlockedExchange(&g_rec.alive, 1); if (InterlockedExchange(&g_rec.loadHold, 0)) g_rec.loadEnd = 1; }
    }
    out->held = g_rec.held; out->stock = g_rec.stock; out->atOnce = g_rec.atOnce; out->noWorld = g_rec.noWorld; out->took = (uint32_t)g_rec.took; out->stranded = s->stranded; out->sentOn = g_rec.sentOn; out->sentOff = g_rec.sentOff; out->skips = g_rec.skips;
    out->makesOff = g_rec.makesPast; out->destroysOff = g_rec.destroysPast; out->threadsOff = 0;
    for (uint32_t i = 1; i < THREADS && g_rec.thread[i].id != 0; i++)
    {
        if (g_rec.thread[i].id == g_rec.tickThread) continue;
        out->makesOff += g_rec.thread[i].makes; out->destroysOff += g_rec.thread[i].destroys; out->threadsOff++;
    }
    out->capped = g_rec.tick.capped; out->adds = g_rec.tick.adds; out->removes = g_rec.tick.removes; out->shed = g_rec.tick.shed;
    out->waiting = g_rec.tick.waiting; out->over = g_rec.tick.over;
    out->records = s->records; out->wanted = s->wanted; out->on = s->on; out->cap = s->cap; out->world = s->world;
    out->trees = g_rec.trees; out->others = g_rec.others; out->unbound = g_rec.unbound; out->destroyed = g_rec.destroyed;
    out->refused = s->refused; out->replaced = s->replaced; out->stale = s->stale; out->binds = s->binds;
    ReleaseSRWLockExclusive(&g_rec.lock);
    QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f);
    out->us = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;
    out->holdUs = (double)(b.QuadPart - h.QuadPart) * 1e6 / (double)f.QuadPart;
    return 1;
}

/* The count is read as the swap itself reads it next (0xBB1439..0xBB1450, 0xAD4740): a swap with none takes nothing. */
void co_cutters_swap(uintptr_t scene)
{
    if (!g_rec.armed || g_cut.exe == 0 || scene == 0 || scene != *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD)) return;
    const uintptr_t queue = *(const uintptr_t*)(scene + CO_CUTTERS_QUEUE);
    if (queue != 0 && *(const int32_t*)(*(const uintptr_t*)queue + 8) + *(const int32_t*)(queue + 0x1C) != 0) InterlockedIncrement(&g_rec.took);
}

/* A run that cannot be read, or an AI step in flight (it may be starting one), counts as a run in flight. */
int co_cutters_cut(float x, float y, float r, uint32_t* waiting)
{
    int cut = -1;
    *waiting = 0;
    if (!g_rec.armed || g_cut.exe == 0 || !g_rec.live || !g_rec.switching || !g_rec.alive || (uint32_t)GetTickCount() - (uint32_t)g_rec.tickAt > CO_CUTTERS_ALIVE_MS) return -1;
    const int step = *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD + CO_CUTTERS_STEP_TASK) != 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    if (Bound())
    {
        uintptr_t run = 0;
        const int inFlight = step || co_safe_read(g_rec.set.world + CO_CUTTERS_RUN, &run, 8) || run != 0;
        cut = co_cutset_cut(&g_rec.set, x, y, r, (uint32_t)g_rec.took, inFlight, waiting);
    }
    ReleaseSRWLockExclusive(&g_rec.lock);
    return cut;
}

float co_cutters_tree(float x, float y, float r, int* on)
{
    float d = -1.0f;
    *on = 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    if (g_rec.set.world != 0) /* bound: the set has its memory */
        d = co_cutset_tree(&g_rec.set, x, y, r, on);
    ReleaseSRWLockExclusive(&g_rec.lock);
    return d;
}

#ifdef CO_TEST
void co_cutters_test_reset(void)
{
    memset((void*)&g_cut, 0, sizeof g_cut);
    if (g_rec.memory != NULL) VirtualFree(g_rec.memory, 0, MEM_RELEASE);
    memset((void*)&g_rec, 0, sizeof g_rec);
}
uint32_t co_cutters_test_make(const void* description, uintptr_t from) { return MakeFrom(description, from); }
const CoCuttersPin* co_cutters_test_record_pins(size_t* count)
{
    static CoCuttersPin pins[sizeof kRecordPins / sizeof kRecordPins[0]];
    *count = sizeof kRecordPins / sizeof kRecordPins[0];
    for (size_t i = 0; i < *count; i++) pins[i] = (CoCuttersPin){kRecordPins[i].rva, kRecordPins[i].pin};
    return pins;
}
const CoCutSet* co_cutters_test_set(void) { return &g_rec.set; }
void co_cutters_test_switch(void (*fn)(const uint32_t* handles, uint32_t count, int on)) { g_rec.sw = fn; }
#endif
