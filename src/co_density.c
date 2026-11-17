/* Living world density: the guards and gates that more online enemies run into first. Every site is the game's own
 * unchecked write or a gate below the density the mod aims for; the facts are in the knowledge base entry
 * living-world-density (studies A-C) and the plan in living-world-density-design. */
#include <stddef.h>
#include <string.h>
#include <windows.h>

#include "co.h"

/* A site the mod rewrites in place: the reviewed bytes and what replaces them. */
typedef struct CoDensityPatch { uint32_t rva; uint8_t size; uint8_t code[8], patch[8]; const char* what; } CoDensityPatch;
/* A site that jumps to a stub: the stub does the replaced instruction, then refuses or returns. */
typedef struct CoDensityStub
{
    uint32_t rva;
    uint8_t size;
    uint8_t code[8];
    uint8_t length, refuseAt;  /* the stub's bytes before its jump back; where its jae's rel32 is */
    uint8_t stub[16];
    uint32_t refuseTo;         /* the rva the jae leads to */
    const char* what;
} CoDensityStub;

/* The squad-slot search ends with r13 = rsi = 0 when all 512 slots are taken, and the request then writes through
 * r13 (0x953E44). The jump takes the function's own refusal exit, which 0x9538EE already uses after the same queries. */
static const CoDensityPatch kPatches[] = {
    {0x953942u, 5, {0x4C, 0x8B, 0xEE, 0xEB, 0x0C}, {0xE9, 0x32, 0x07, 0x00, 0x00}, "squad slots: a full table refuses"},
    /* 448 is 7/8 of the AiJob table's first capacity, not a size: the table grows (0x7F6080). */
    {0x952F62u, 7, {0x81, 0x78, 0x0C, 0xC0, 0x01, 0x00, 0x00}, {0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00}, "AiJob gate (spawn check): 1024"},
    {0x90EDEAu, 7, {0x81, 0x78, 0x0C, 0xC0, 0x01, 0x00, 0x00}, {0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00}, "AiJob gate (garrison fill): 1024"},
};

/* The budget's pair table holds 64 entries but appends at its count unchecked: the 66th overwrites the count, which
 * every spawn request then loops. A pair past 64 goes uncounted (its units only miss the per-type cap), the path the
 * table-off byte +0x1651 already takes. */
static const CoDensityStub kStubs[] = {
    {0xA2BE29u, 7, {0x41, 0x8B, 0x85, 0x40, 0x16, 0x00, 0x00}, 16, 12,
     {0x41, 0x8B, 0x85, 0x40, 0x16, 0x00, 0x00, 0x83, 0xF8, 0x40, 0x0F, 0x83, 0, 0, 0, 0}, 0xA2BE62u, "pair table (unit added)"},
    {0xA2D6A9u, 6, {0x8B, 0x83, 0x40, 0x16, 0x00, 0x00}, 15, 11,
     {0x8B, 0x83, 0x40, 0x16, 0x00, 0x00, 0x83, 0xF8, 0x40, 0x0F, 0x83, 0, 0, 0, 0}, 0xA2D6E0u, "pair table (queued unit)"},
};
/* The native-only gates: the game counts every AI character, the living world's units included, against its 70 room,
 * its breach release (100) and its despawner (tracked >= pending + 100). These stubs take the living world's units
 * out (clamped at 0, since the count is a few frames old), so natives keep vanilla's room. `unitsAt` is where the
 * stub's rip-relative reference to the count is; Difficulty11's own patch starts at 0x8B88BF, after the release stub. */
typedef struct CoDensityGate { uint32_t rva; uint8_t size; uint8_t code[10]; uint8_t length, unitsAt; uint8_t stub[24]; const char* what; } CoDensityGate;
static const CoDensityGate kGates[] = {
    {0x952F53u, 6, {0x03, 0x48, 0x1C, 0x83, 0xF9, 0x46}, 16, 5,
     {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x83, 0xF9, 0x46}, "the 70 room (spawn check)"},
    {0x90EDDBu, 6, {0x03, 0x48, 0x1C, 0x83, 0xF9, 0x46}, 16, 5,
     {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x83, 0xF9, 0x46}, "the 70 room (garrison fill)"},
    {0x95577Fu, 6, {0x03, 0x48, 0x1C, 0x83, 0xF9, 0x46}, 16, 5,
     {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x83, 0xF9, 0x46}, "the 70 room (slot respawn)"},
    {0x8B88B2u, 5, {0x03, 0x48, 0x1C, 0x03, 0xCA}, 15, 5,
     {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x03, 0xCA}, "the breach group release"},
    {0x572C51u, 9, {0x8B, 0x87, 0xD4, 0xE9, 0x75, 0x00, 0x83, 0xC0, 0x64}, 15, 11,
     {0x8B, 0x87, 0xD4, 0xE9, 0x75, 0x00, 0x83, 0xC0, 0x64, 0x03, 0x05, 0, 0, 0, 0}, "the despawner's threshold"},
    {0xA2C08Au, 6, {0x8B, 0x99, 0x54, 0x16, 0x00, 0x00}, 12, 8,
     {0x8B, 0x99, 0x54, 0x16, 0x00, 0x00, 0x03, 0x1D, 0, 0, 0, 0}, "the despawner's pressure exception"},
};
#define CO_DENSITY_GATES (sizeof kGates / sizeof kGates[0])

#define CO_DENSITY_PATCHES (sizeof kPatches / sizeof kPatches[0])
#define CO_DENSITY_STUBS (sizeof kStubs / sizeof kStubs[0])
#define CO_DENSITY_STUB_ROOM 0x20u

static uintptr_t g_game, g_stubs, g_data; /* g_data: the living world's unit count, within reach of the stubs */
static int g_state; /* 0 not done, 1 installed, -1 left as the game has it */

static void Undo(size_t patches, size_t stubs, size_t gates)
{
    for (size_t j = 0; j < gates; j++) g_coApi->write(g_game + kGates[j].rva, kGates[j].code, kGates[j].size);
    for (size_t j = 0; j < stubs; j++) g_coApi->write(g_game + kStubs[j].rva, kStubs[j].code, kStubs[j].size);
    for (size_t j = 0; j < patches; j++) g_coApi->write(g_game + kPatches[j].rva, kPatches[j].code, kPatches[j].size);
}

static void Release(void)
{
    if (g_stubs) VirtualFree((void*)g_stubs, 0, MEM_RELEASE);
    if (g_data) VirtualFree((void*)g_data, 0, MEM_RELEASE);
    g_stubs = g_data = 0;
}

static int JumpTo(uintptr_t at, uint8_t size, uintptr_t to)
{
    uint8_t jump[10];
    int32_t rel = (int32_t)((int64_t)to - (int64_t)(at + 5u));
    memset(jump, 0x90, sizeof jump);
    jump[0] = 0xE9;
    memcpy(jump + 1, &rel, 4);
    return g_coApi->write(at, jump, size);
}

static int Reviewed(uint32_t rva, const uint8_t* code, uint8_t size)
{
    uint8_t bytes[16];
    return g_coApi->read(g_game + rva, bytes, size) == 0 && memcmp(bytes, code, size) == 0;
}

/* ---- the living world's units ------------------------------------------------------------------------------------
 * Only the living world sets AiJob keep byte +0x18 on squad members; vanilla keeps only squad entities (KB
 * living-world-density, S5). So a living-world unit is an AiJob record with keep 1, job 1 or 10, whose entity has no
 * AiSquad record. Counted from bulk copies of the two hash tables, a few times a second on the game thread. */
#define CO_AIJOB 0x33264D8u   /* [game+]: hash +0x20, active count +0xC, records +0x48 (0x1C: +0xC job, +0x18 keep) */
#define CO_AISQUAD 0x33265A0u /* [game+]: hash +0x20 */
#define CO_HASH_MAX 65536u
#define CO_JOBS_MAX 8192u
typedef struct CoHash { uint64_t buckets; uint32_t capacity, empty, multiplier; } CoHash;
static uint32_t g_jobBuckets[CO_HASH_MAX * 2], g_squadBuckets[CO_HASH_MAX * 2];
static uint8_t g_jobs[CO_JOBS_MAX * 0x1C];

static int ReadHash(uintptr_t header, CoHash* h, uint32_t* buckets)
{
    uint8_t raw[20];
    if (g_coApi->read(header, raw, sizeof raw) != 0) return 0;
    memcpy(&h->buckets, raw, 8); memcpy(&h->capacity, raw + 8, 4); memcpy(&h->empty, raw + 12, 4); memcpy(&h->multiplier, raw + 16, 4);
    return h->buckets >= 0x10000 && h->capacity != 0 && h->capacity <= CO_HASH_MAX &&
        g_coApi->read((uintptr_t)h->buckets, buckets, (size_t)h->capacity * 8u) == 0;
}

static int InHash(const CoHash* h, const uint32_t* buckets, uint32_t key)
{
    uint32_t start = key * h->multiplier;
    for (uint32_t i = 0; i < h->capacity; i++)
    {
        uint32_t k = buckets[((start + i) % h->capacity) * 2];
        if (k == key) return buckets[((start + i) % h->capacity) * 2 + 1] != 0xFFFFFFFFu;
        if (k == h->empty) return 0;
    }
    return 0;
}

static int CountUnits(uint32_t* units)
{
    uint64_t jobs, squads, records;
    CoHash jh, sh;
    if (g_coApi->read(g_game + CO_AIJOB, &jobs, 8) != 0 || g_coApi->read(g_game + CO_AISQUAD, &squads, 8) != 0 || jobs < 0x10000 || squads < 0x10000) return 0;
    uint32_t active = 0;
    if (g_coApi->read((uintptr_t)jobs + 0xC, &active, 4) != 0 || active > CO_JOBS_MAX || g_coApi->read((uintptr_t)jobs + 0x48, &records, 8) != 0 ||
        !ReadHash((uintptr_t)jobs + 0x20, &jh, g_jobBuckets) || !ReadHash((uintptr_t)squads + 0x20, &sh, g_squadBuckets) ||
        (active && g_coApi->read((uintptr_t)records, g_jobs, (size_t)active * 0x1Cu) != 0)) return 0;
    uint32_t n = 0;
    for (uint32_t b = 0; b < jh.capacity; b++)
    {
        uint32_t key = g_jobBuckets[b * 2], index = g_jobBuckets[b * 2 + 1];
        if (key == jh.empty || index >= active) continue;
        const uint8_t* r = g_jobs + (size_t)index * 0x1Cu;
        uint32_t job; memcpy(&job, r + 0xC, 4);
        if (r[0x18] == 1 && (job == 1 || job == 10) && !InHash(&sh, g_squadBuckets, key)) n++;
    }
    *units = n;
    return 1;
}

/* ---- the ray queue -----------------------------------------------------------------------------------------------
 * The world's ray queue holds 4096 entries and its submit (0x175CA60, twin 0x175CBD0) appends unchecked. Returning 0
 * means "no ray" to every caller but two, which index with (0 - 1) << 6 (0x9181EC, 0x13C16B1): those, callers outside
 * game.dll and the 18 inline copies of the submit get the last 512 entries. */
#define CO_RAY_LIMIT 3584u
typedef uint32_t (*CoRayFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static CoRayFn g_raySubmit, g_raySubmit2;
static uintptr_t g_gameEnd;
static volatile LONG g_raysRefused;

static int RefuseRay(uint64_t queue, uintptr_t back)
{
    if (*(volatile uint32_t*)(uintptr_t)queue < CO_RAY_LIMIT || back < g_game || back >= g_gameEnd) return 0;
    if (back == g_game + 0x916F63u || back == g_game + 0x13C5DBCu) return 0;
    InterlockedIncrement(&g_raysRefused);
    return 1;
}

static uint32_t RaySubmit(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    if (RefuseRay(a, (uintptr_t)__builtin_return_address(0))) return 0;
    return g_raySubmit(a, b, c, d, e, f, g, h);
}

static uint32_t RaySubmit2(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    if (RefuseRay(a, (uintptr_t)__builtin_return_address(0))) return 0;
    return g_raySubmit2(a, b, c, d, e, f, g, h);
}

static int HookFirst(uintptr_t target, void* detour, void** original)
{
    /* First in the chain, so the return address is the game's caller (a mod ahead of it reads as "outside game.dll"). */
    if (g_coApi->size >= offsetof(HfApi, hook_ex) + sizeof(void*) && g_coApi->hook_ex != NULL)
        return g_coApi->hook_ex(g_coMod, target, detour, original, 0x7FFFFFFF);
    return g_coApi->hook(g_coMod, target, detour, original);
}

static void InstallRayGuard(void)
{
    if (!g_coApi->code_matches(g_game + 0x175CA60u, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57") ||
        !g_coApi->code_matches(g_game + 0x175CBD0u, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56"))
    { co_log("density: the ray queue's submit is not the reviewed one; no ray guard"); return; }
    if (HookFirst(g_game + 0x175CA60u, (void*)RaySubmit, (void**)&g_raySubmit) != 0 ||
        HookFirst(g_game + 0x175CBD0u, (void*)RaySubmit2, (void**)&g_raySubmit2) != 0)
    { co_log("density: the ray queue's submit could not be hooked; no ray guard"); return; }
    co_log("density: the ray queue refuses new rays above %u of 4096", CO_RAY_LIMIT);
}

/* ---- the game-thread tick ---------------------------------------------------------------------------------------- */
static unsigned g_ticks;
static float g_noteIn = 10.0f;
static uint32_t g_unitsSeen;

static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    if (++g_ticks % 6 == 0)
    {
        uint32_t units;
        /* Unreadable tables count as 0: the gates are then vanilla's. */
        if (!CountUnits(&units)) units = 0;
        *(volatile uint32_t*)g_data = units;
        if (units > g_unitsSeen) g_unitsSeen = units;
    }
    g_noteIn -= dt;
    if (g_noteIn <= 0.0f)
    {
        g_noteIn = 10.0f;
        LONG refused = InterlockedExchange(&g_raysRefused, 0);
        if (g_unitsSeen || refused)
            co_log("density: living world units %u (most %u in 10 s), rays refused %ld", *(volatile uint32_t*)g_data, g_unitsSeen, (long)refused);
        g_unitsSeen = 0;
    }
}

void co_density_install(uintptr_t game)
{
    g_game = game;
    g_state = -1;
    size_t size = 0;
    g_gameEnd = g_coApi->module_base("game.dll", &size) == game && size ? game + size : game;
    for (size_t i = 0; i < CO_DENSITY_PATCHES; i++)
        if (!Reviewed(kPatches[i].rva, kPatches[i].code, kPatches[i].size))
        { co_log("density: the code at game+0x%X is not the reviewed one; the guards stay off", kPatches[i].rva); return; }
    for (size_t i = 0; i < CO_DENSITY_STUBS; i++)
        if (!Reviewed(kStubs[i].rva, kStubs[i].code, kStubs[i].size))
        { co_log("density: the code at game+0x%X is not the reviewed one; the guards stay off", kStubs[i].rva); return; }
    for (size_t i = 0; i < CO_DENSITY_GATES; i++)
        if (!Reviewed(kGates[i].rva, kGates[i].code, kGates[i].size))
        { co_log("density: the code at game+0x%X is not the reviewed one; the guards stay off", kGates[i].rva); return; }
    g_stubs = co_alloc_near(game + 0x1000000u, 0x1000u);
    g_data = co_alloc_near(game + 0x1000000u, 0x1000u);
    if (g_stubs == 0 || g_data == 0) { Release(); co_log("density: no free memory near the game's code; the guards stay off"); return; }
    /* The stubs first, whole and executable, before any jump leads to them. */
    for (size_t i = 0; i < CO_DENSITY_STUBS; i++)
    {
        const CoDensityStub* s = &kStubs[i];
        uint8_t* stub = (uint8_t*)(g_stubs + i * CO_DENSITY_STUB_ROOM);
        memcpy(stub, s->stub, s->length);
        int32_t refuse = (int32_t)((int64_t)(game + s->refuseTo) - (int64_t)((uintptr_t)stub + s->refuseAt + 4u));
        memcpy(stub + s->refuseAt, &refuse, 4);
        stub[s->length] = 0xE9;
        int32_t back = (int32_t)((int64_t)(game + s->rva + s->size) - (int64_t)((uintptr_t)stub + s->length + 5u));
        memcpy(stub + s->length + 1, &back, 4);
    }
    for (size_t i = 0; i < CO_DENSITY_GATES; i++)
    {
        const CoDensityGate* s = &kGates[i];
        uint8_t* stub = (uint8_t*)(g_stubs + (CO_DENSITY_STUBS + i) * CO_DENSITY_STUB_ROOM);
        memcpy(stub, s->stub, s->length);
        int32_t units = (int32_t)((int64_t)g_data - (int64_t)((uintptr_t)stub + s->unitsAt + 4u));
        memcpy(stub + s->unitsAt, &units, 4);
        stub[s->length] = 0xE9;
        int32_t back = (int32_t)((int64_t)(game + s->rva + s->size) - (int64_t)((uintptr_t)stub + s->length + 5u));
        memcpy(stub + s->length + 1, &back, 4);
    }
    DWORD before;
    if (!VirtualProtect((void*)g_stubs, 0x1000u, PAGE_EXECUTE_READ, &before))
    { Release(); co_log("density: the stubs could not be made executable; the guards stay off"); return; }
    for (size_t i = 0; i < CO_DENSITY_PATCHES; i++)
        if (g_coApi->write(game + kPatches[i].rva, kPatches[i].patch, kPatches[i].size) != 0)
        { Undo(i, 0, 0); Release(); co_log("density: game+0x%X could not be written; the guards stay off", kPatches[i].rva); return; }
    for (size_t i = 0; i < CO_DENSITY_STUBS; i++)
        if (JumpTo(game + kStubs[i].rva, kStubs[i].size, g_stubs + i * CO_DENSITY_STUB_ROOM) != 0)
        { Undo(CO_DENSITY_PATCHES, i, 0); Release(); co_log("density: game+0x%X could not be written; the guards stay off", kStubs[i].rva); return; }
    for (size_t i = 0; i < CO_DENSITY_GATES; i++)
        if (JumpTo(game + kGates[i].rva, kGates[i].size, g_stubs + (CO_DENSITY_STUBS + i) * CO_DENSITY_STUB_ROOM) != 0)
        { Undo(CO_DENSITY_PATCHES, CO_DENSITY_STUBS, i); Release(); co_log("density: game+0x%X could not be written; the guards stay off", kGates[i].rva); return; }
    g_state = 1;
    for (size_t i = 0; i < CO_DENSITY_PATCHES; i++) co_log("density: %s", kPatches[i].what);
    for (size_t i = 0; i < CO_DENSITY_STUBS; i++) co_log("density: %s: at most 64 pairs", kStubs[i].what);
    for (size_t i = 0; i < CO_DENSITY_GATES; i++) co_log("density: %s counts natives only", kGates[i].what);
    /* Without the tick the count stays 0 and the gates count as vanilla's. */
    if (g_coApi->size < offsetof(HfApi, on_game_thread) + sizeof(void*) || g_coApi->on_game_thread == NULL || !g_coApi->on_game_thread(g_coMod, Tick, NULL))
        co_log("density: no game-thread tick; the gates count every unit, as vanilla's");
    InstallRayGuard();
}

#ifdef CO_TEST
void co_density_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_DENSITY_PATCHES; i++) memcpy(image + kPatches[i].rva, kPatches[i].code, kPatches[i].size);
    for (size_t i = 0; i < CO_DENSITY_STUBS; i++) memcpy(image + kStubs[i].rva, kStubs[i].code, kStubs[i].size);
    for (size_t i = 0; i < CO_DENSITY_GATES; i++) memcpy(image + kGates[i].rva, kGates[i].code, kGates[i].size);
}
int co_density_test_state(void) { return g_state; }
uintptr_t co_density_test_stub(int i) { return g_stubs ? g_stubs + (uintptr_t)i * CO_DENSITY_STUB_ROOM : 0; }
uintptr_t co_density_test_data(void) { return g_data; }
int co_density_test_count(uint32_t* units) { return CountUnits(units); }
int co_density_test_refuse(uint64_t queue, uintptr_t back) { return RefuseRay(queue, back); }
void co_density_test_tick(float dt) { Tick(g_coMod, dt, NULL); }
#endif
