/* Research log for crash exe+0x4F5037 (a render terrain's first resolve read a material block that was no longer
 * one). The best candidate: a terrain's material comes from a refcounted cache keyed by the template's id, a stored
 * material keeps the block of the template it was cloned from, and something kept a slot alive across a planet
 * change. These lines show the cache, each get, each terrain's materials and the world's terrains; they change
 * nothing the game reads. A CO_RESEARCH build only; the views are xl/co_terrainlog.c. */
#ifdef CO_RESEARCH

enum { TL_GET, TL_HIT, TL_BEFORE, TL_LISTS, TL_SLOT, TL_SAME, TL_PASS, TL_STATE };
static CoTlPace g_tlPace;
#define TL_SHOWN 16 /* slots or terrains written per dump; the header says how many there are */

static int TlPace(int kind, uint32_t most) { return co_tl_pace(&g_tlPace, g_world.levels, kind, most); }
static const char* TlLast(int pace) { return pace == 2 ? " (the last such line of this level)" : ""; }

static void TerrainLogCache(uint64_t world, const char* when)
{
    CoTlEntry e[TL_SHOWN];
    uint64_t holder = 0;
    uint32_t slots = 0;
    if (world == 0 || SafeRead((uintptr_t)world + 0x2301F8u, &holder, 8)) holder = 0;
    const int n = co_tl_cache(SafeRead, holder, e, TL_SHOWN, &slots);
    Note("research: terrain log: cache %s: holder 0x%llX, %u slots, %d with a terrain key%s", when, (unsigned long long)holder, slots, n,
        n < 0 ? " (the table did not read)" : n > TL_SHOWN ? " (16 written)" : "");
    for (int i = 0; i < n && i < TL_SHOWN; i++)
        Note("research: terrain log: cache %s: key 0x%016llX material 0x%llX refs %d generation %u handle 0x%X block 0x%llX id 0x%016llX", when,
            (unsigned long long)e[i].key, (unsigned long long)e[i].material, (int)e[i].refs, e[i].generation, e[i].handle, (unsigned long long)e[i].block,
            (unsigned long long)e[i].id);
}

/* The map is read without the world's terrain lock: the leave's caller holds the streamer's, and a read that races
 * a change only gives a line that says it did not read. */
static void TerrainLogListed(uint64_t world, const char* when)
{
    CoTlTerrain t[TL_SHOWN];
    const int n = co_tl_terrains(SafeRead, world, t, TL_SHOWN);
    Note("research: terrain log: terrains listed %s: %d (world 0x%llX)%s", when, n, (unsigned long long)world,
        n < 0 ? " (the map did not read)" : n > TL_SHOWN ? " (16 written)" : "");
    for (int i = 0; i < n && i < TL_SHOWN; i++)
        Note("research: terrain log: terrain listed %s: 0x%llX heightmap 0x%llX material map 0x%llX", when, (unsigned long long)t[i].handle,
            (unsigned long long)t[i].heightmap, (unsigned long long)t[i].material);
}

typedef uint64_t (*TlGetFn)(uint64_t holder, uint64_t descriptor, uint64_t key, uint64_t resources);
/* In place of the terrain constructor's two cache gets (exe 0x1DE180). A stored material is returned as it is and
 * the template is not looked at again (0x1DE204..0x1DE32C); the clone would take [descriptor+0x20] (0x4F2CB0). */
static uint64_t TerrainLogGet(uint64_t holder, uint64_t descriptor, uint64_t key, uint64_t resources)
{
    const TlGetFn original = (TlGetFn)((uintptr_t)GetModuleHandleW(NULL) + 0x1DE180u);
    CoTlEntry before = {0}, after = {0};
    uint32_t flag = 0;
    uint64_t template = 0, id = 0, block = 0;
    const int had = co_tl_cache_key(SafeRead, holder, key, &before);
    const int read = descriptor != 0 && !SafeRead((uintptr_t)descriptor + 4, &flag, 4) && !SafeRead((uintptr_t)descriptor + 0x20, &template, 8) &&
        template != 0 && !SafeRead((uintptr_t)template + 0x38, &id, 8) && !SafeRead((uintptr_t)template + 0x10, &block, 8);
    const uint64_t material = original(holder, descriptor, key, resources);
    const int got = co_tl_material(SafeRead, material, &after);
    const int stored = had == 1 && before.refs > 0 && before.material == material;
    const int differs = read && got && after.block != block;
    const int pace = differs ? TlPace(TL_HIT, 32) : TlPace(TL_GET, 96);
    if (!pace) return material;
    char was[96] = "no slot";
    if (had < 0) snprintf(was, sizeof was, "the table did not read");
    else if (had == 1) snprintf(was, sizeof was, "material 0x%llX refs %d generation %u", (unsigned long long)before.material, (int)before.refs, before.generation);
    Note("research: terrain log: get: thread %lu key 0x%016llX descriptor 0x%llX (flag %u) template 0x%llX id 0x%016llX block 0x%llX; slot before: %s; "
        "returned 0x%llX handle 0x%X block 0x%llX id 0x%016llX: %s%s%s", (unsigned long)GetCurrentThreadId(), (unsigned long long)key,
        (unsigned long long)descriptor, flag, (unsigned long long)template, (unsigned long long)id, (unsigned long long)block, was,
        (unsigned long long)material, after.handle, (unsigned long long)after.block, (unsigned long long)after.id,
        !read || !got ? "not read" : differs ? (stored ? "THE STORED MATERIAL'S BLOCK IS NOT THE TEMPLATE'S" : "A NEW CLONE WHOSE BLOCK IS NOT THE TEMPLATE'S") :
        stored ? "the stored material, its block is the template's" : "a new clone",
        read && (uint32_t)key != (uint32_t)id ? "; the key is not this template's id" : "", TlLast(pace));
    return material;
}

/* The two calls point at a stub for good: each rel32 lies inside one cache line (0x2F4528, 0x2F4619), so the write
 * is whole for a thread that runs the call meanwhile, and nothing ever points them back. */
static void TerrainLogInstall(void)
{
    static LONG once;
    static const uint32_t sites[2] = {0x2F4527u, 0x2F4618u};
    if (InterlockedCompareExchange(&once, 1, 0) != 0) return;
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    if (!ApiCodeMatches(exe + sites[0], "E8 54 9C EE FF") || !ApiCodeMatches(exe + sites[1], "E8 63 9B EE FF"))
    { Note("research: terrain log: the terrain constructor's material gets are not the reviewed code; no get lines"); return; }
    uint8_t* stub = AllocateNearGame(exe, 0x1000);
    DWORD old = 0;
    if (stub == NULL) { Note("research: terrain log: no room near the exe for the get's stub; no get lines"); return; }
    static const uint8_t jmp[6] = {0xFF, 0x25, 0, 0, 0, 0}; /* jmp [rip+0] */
    const uint64_t to = (uint64_t)(uintptr_t)TerrainLogGet;
    int32_t rel[2];
    for (int k = 0; k < 2; k++)
    {
        const intptr_t reach = (intptr_t)stub - (intptr_t)(exe + sites[k] + 5);
        if (reach != (intptr_t)(int32_t)reach) { Note("research: terrain log: the get's stub is out of reach; no get lines"); return; }
        rel[k] = (int32_t)reach;
    }
    memcpy(stub, jmp, 6); memcpy(stub + 6, &to, 8);
    if (!VirtualProtect(stub, 0x1000, PAGE_EXECUTE_READ, &old)) { Note("research: terrain log: the get's stub is not executable; no get lines"); return; }
    const uintptr_t first = exe + sites[0], last = exe + sites[1] + 5;
    if (!VirtualProtect((void*)first, last - first, PAGE_EXECUTE_READWRITE, &old)) { Note("research: terrain log: the gets are not writable; no get lines"); return; }
    for (int k = 0; k < 2; k++) *(volatile int32_t*)(exe + sites[k] + 1) = rel[k];
    VirtualProtect((void*)first, last - first, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)first, last - first);
    Note("research: terrain log: the terrain constructor's two material gets are logged (exe+0x2F4527, exe+0x2F4618)");
}

/* The leave, the ship and a level init: what the cache and the world still hold there. */
static void TerrainLogState(const char* when)
{
    TerrainLogInstall();
    const int pace = TlPace(TL_STATE, 12);
    if (!pace) return;
    const uint64_t world = NearMainWorld();
    TerrainLogCache(world, when);
    TerrainLogListed(world, when);
}

static void TerrainLogBefore(void* world, const char* what)
{
    char when[64];
    if (world == NULL || !TlPace(TL_BEFORE, 24)) return;
    snprintf(when, sizeof when, "before %s spawn", what);
    TerrainLogCache((uint64_t)(uintptr_t)world, when);
}

static void TerrainLogSpawned(uint64_t terrain, const char* what)
{
    if (terrain == 0 || !TlPace(TL_LISTS, 24)) return;
    for (int list = 0; list < 2; list++)
    {
        CoTlMaterial m[8];
        const int n = co_tl_materials(SafeRead, terrain, list, m, 8);
        if (n <= 0) { Note("research: terrain log: terrain 0x%llX (%s) list %d: %s", (unsigned long long)terrain, what, list, n < 0 ? "did not read" : "empty"); continue; }
        for (int i = 0; i < n && i < 8; i++)
            Note("research: terrain log: terrain 0x%llX (%s) list %d entry %d of %d: name 0x%08X material 0x%llX handle 0x%X block 0x%llX: entries 0x%llX "
                "count %u data 0x%llX", (unsigned long long)terrain, what, list, i + 1, n, m[i].name, (unsigned long long)m[i].material, m[i].handle,
                (unsigned long long)m[i].block, (unsigned long long)m[i].entries, m[i].count, (unsigned long long)m[i].data);
    }
}

/* After the hook put the config slots back: a spawn that made the table grow (exe 0xA6E30's path to 0xA7B60) moved
 * its cells, and the value went into the old ones. The find here never inserts. */
static void TerrainLogSlots(void* world, const char* what, uint64_t* heightmap, uint64_t* material)
{
    static const uint32_t keys[2] = {0x2A9E3232u, 0x95E7E3ACu};
    uint64_t* const kept[2] = {heightmap, material};
    uint64_t holder = 0, renderer = 0;
    int moved = 0, checked = 0;
    if (world == NULL || SafeRead((uintptr_t)world + 0x2301F8u, &holder, 8) || holder == 0 || SafeRead((uintptr_t)holder, &renderer, 8) || renderer == 0) return;
    for (int k = 0; k < 2; k++)
    {
        int state = 0;
        if (kept[k] == NULL) continue;
        const uint64_t found = co_tl_slot(SafeRead, renderer + 0x1600, keys[k], &state);
        checked++;
        if (state == 1 && found == (uint64_t)(uintptr_t)kept[k]) continue;
        moved++;
        if (TlPace(TL_SLOT, 8))
            Note("research: terrain log: slot 0x%08X after %s spawn: kept 0x%llX, found 0x%llX (%s): THE SLOT IS NOT WHERE IT WAS KEPT", keys[k], what,
                (unsigned long long)(uintptr_t)kept[k], (unsigned long long)found, state == 1 ? "found" : state == 0 ? "the key is not there" : "the table did not read");
    }
    if (checked && !moved && TlPace(TL_SAME, 1))
        Note("research: terrain log: %d slot(s) found again where they were kept after %s spawn (written once a level; a moved slot is always written)", checked, what);
}

static void TerrainLogPassed(const char* hook, uint32_t type, uint32_t name, uint64_t handle, DWORD armedFor)
{
    const int pace = TlPace(TL_PASS, 32);
    if (pace)
        Note("research: terrain log: spawn passed through the %s hook: type 0x%08X name 0x%08X handle 0x%llX thread %lu (the hook is armed for thread %lu)%s",
            hook, type, name, (unsigned long long)handle, (unsigned long)GetCurrentThreadId(), (unsigned long)armedFor, TlLast(pace));
}

#endif
