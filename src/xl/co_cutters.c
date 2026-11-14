/* The engine's navmesh cutters: the destroy leaves out handles of 0, and failed makes are counted (co_cutters.h). */
#include <string.h>

#include "co_cutters.h"
#include "co_shim.h"

typedef void (*DestroyFn)(const uint32_t* handles, uint32_t count);
typedef uint32_t (*MakeFn)(const void* description);

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
 * pool's mask and the AI world's slot. */
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
};

/* The make can run on a thread other than the game's (the forest's spawn has several callers), so the set has its
 * own lock; the tick holds it for well under a millisecond. */
static struct
{
    volatile LONG armed, live;
    SRWLOCK lock;
    CoCutSet set;
    void* memory;
    CoCutTick tick;
    uint64_t trees, others, unbound, destroyed;
} g_rec;

/* The AI world and the pool the set's handles are of, read anew: either one changed or gone drops every record. */
static int Bound(void)
{
    const uintptr_t world = *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD);
    const uintptr_t pool = *(const uintptr_t*)(g_cut.exe + CO_CUTTERS_POOLS + 8u * CO_CUTTERS_WORLD);
    uint32_t mask = g_rec.set.mask;
    if (world == 0 || pool == 0) return co_cutset_bind(&g_rec.set, 0, 0, 0);
    if (pool != g_rec.set.pool && co_safe_read(pool + CO_CUTTERS_MASK, &mask, 4)) mask = 0;
    return co_cutset_bind(&g_rec.set, world, pool, mask);
}

/* A cutter of the mission's world was made: a tree's is recorded, any other only frees its slot of an old record. */
static void Recorded(uint32_t handle, int tree, float x, float y, int wanted)
{
    AcquireSRWLockExclusive(&g_rec.lock);
    if (!g_rec.live || !Bound()) g_rec.unbound++;
    else if (!tree) { co_cutset_taken(&g_rec.set, handle); g_rec.others++; }
    else if (co_cutset_made(&g_rec.set, handle, x, y, wanted)) g_rec.trees++;
    ReleaseSRWLockExclusive(&g_rec.lock);
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
 * (0x8155D5..0x8155F3): read here, never written. */
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
    const uint32_t handle = g_cut.make(description);
    if (handle != 0 && madeIn == CO_CUTTERS_WORLD) Recorded(handle, tree, x, y, add != 0);
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
    ReleaseSRWLockExclusive(&g_rec.lock);
    InterlockedExchange(&g_rec.armed, 1);
    return NULL;
}

uint32_t co_cutters_live(int complex)
{
    uint32_t dropped = 0;
    InterlockedExchange(&g_rec.live, complex != 0);
    if (complex || !g_rec.armed) return 0;
    AcquireSRWLockExclusive(&g_rec.lock);
    dropped = g_rec.set.records;
    co_cutset_clear(&g_rec.set);
    ReleaseSRWLockExclusive(&g_rec.lock);
    return dropped;
}

int co_cutters_tick(const CoCutCentre* centres, uint32_t count, uint32_t nowMs, uint32_t removes, CoCuttersTick* out)
{
    LARGE_INTEGER a, b, f;
    if (!g_rec.armed || !g_rec.live) return 0;
    QueryPerformanceCounter(&a);
    AcquireSRWLockExclusive(&g_rec.lock);
    const CoCutSet* s = &g_rec.set;
    out->hooked = g_cut.make != NULL && g_cut.destroy != NULL;
    out->bound = g_cut.exe != 0 && Bound();
    co_cutset_tick(&g_rec.set, centres, count, nowMs, removes, &g_rec.tick);
    out->capped = g_rec.tick.capped; out->adds = g_rec.tick.adds; out->removes = g_rec.tick.removes; out->shed = g_rec.tick.shed;
    out->waiting = g_rec.tick.waiting; out->over = g_rec.tick.over;
    out->records = s->records; out->wanted = s->wanted; out->on = s->on; out->cap = s->cap; out->world = s->world;
    out->trees = g_rec.trees; out->others = g_rec.others; out->unbound = g_rec.unbound; out->destroyed = g_rec.destroyed;
    out->refused = s->refused; out->replaced = s->replaced; out->stale = s->stale; out->binds = s->binds;
    ReleaseSRWLockExclusive(&g_rec.lock);
    QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f);
    out->us = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;
    return 1;
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
#endif
