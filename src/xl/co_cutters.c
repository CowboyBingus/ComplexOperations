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
static const struct { uint32_t rva; const char* pin; } kCutterPins[] = {
    {CO_CUTTERS_DESTROY, "85 D2 0F 84 32 01 00 00 41 54 41 55 48 83 EC 38 48 89 5C 24 50 45 33 C0"},
    {0x8127F6u, "49 8B 9C C3 30 63 7C 02 66 90 41 8B 74 BD 00 8B CE 23 4B 28 39 4B 24"},
    {CO_CUTTERS_MAKE, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57"},
    {0x812DBAu, "83 F8 FF 0F 85 3F 03 00 00 48 8B CF E8 45 9F 18 00 8B 85 10 08 00 00"},
    {0x7D7F60u, "41 89 48 24"},
    {0x7D8090u, "4F 89 84 EC 30 63 7C 02"},
    {0x75331u, "F0 41 FF 41 20"},
    {0x754A6u, "F0 FF 49 20"},
};

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

/* The description's +4 is the world (0x812CDF). */
static uint32_t Make(const void* description)
{
    const uint32_t handle = g_cut.make(description);
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

#ifdef CO_TEST
void co_cutters_test_reset(void) { memset((void*)&g_cut, 0, sizeof g_cut); }
#endif
