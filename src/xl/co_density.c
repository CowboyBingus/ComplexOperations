/* The living world's density guards (co_density.h). Sites and stubs as studied in KB living-world-density (A-C);
 * each is a switched site, so a level that is not complex runs the game's own instructions. */
#include <stddef.h>
#include <string.h>

#include <windows.h>

#include "co_density.h"
#include "co_switch.h"

/* The squad-slot loop's end (0x953939): cmp r8d, 512; jb 0x953910; mov r13, rsi; jmp 0x953953. */
static const uint8_t kSlot[] = {0x41, 0x81, 0xF8, 0x00, 0x02, 0x00, 0x00, 0x72, 0xCE, 0x4C, 0x8B, 0xEE, 0xEB, 0x0C};
static const uint8_t kSlotGame[] = {0x41, 0x81, 0xF8, 0x00, 0x02, 0x00, 0x00, 0x0F, 0x82, 0, 0, 0, 0, 0x4C, 0x8B, 0xEE};
static const uint8_t kSlotFull[] = {0x41, 0x81, 0xF8, 0x00, 0x02, 0x00, 0x00, 0x0F, 0x82, 0, 0, 0, 0};
/* Behind the jump, never run: Difficulty11 tells the guards are in by these bytes (pressure/natives.c). */
static const uint8_t kSlotAfter[] = {0x90, 0x90, 0x90, 0x90, 0xE9, 0x32, 0x07, 0x00, 0x00};
/* The AiJob gate of the spawn check with its load (0x952F5B): mov rax, [0x33264D8]; cmp dword [rax+0xC], 448. */
static const uint8_t kAiJobSpawn[] = {0x48, 0x8B, 0x05, 0x76, 0x35, 0x9D, 0x02, 0x81, 0x78, 0x0C, 0xC0, 0x01, 0x00, 0x00};
static const uint8_t kAiJobSpawnRaised[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00};
static const uint8_t kAiJobSpawnAfter[] = {0x90, 0x90, 0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00};
static const uint8_t kAiJob[] = {0x81, 0x78, 0x0C, 0xC0, 0x01, 0x00, 0x00};
static const uint8_t kAiJobRaised[] = {0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00};
static const uint8_t kRoom[] = {0x03, 0x48, 0x1C, 0x83, 0xF9, 0x46};
static const uint8_t kRoomNatives[] = {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x83, 0xF9, 0x46};
static const uint8_t kRelease[] = {0x03, 0x48, 0x1C, 0x03, 0xCA};
static const uint8_t kReleaseNatives[] = {0x03, 0x48, 0x1C, 0x2B, 0x0D, 0, 0, 0, 0, 0x79, 0x02, 0x31, 0xC9, 0x03, 0xCA};
static const uint8_t kDespawn[] = {0x8B, 0x87, 0xD4, 0xE9, 0x75, 0x00, 0x83, 0xC0, 0x64};
static const uint8_t kDespawnNatives[] = {0x8B, 0x87, 0xD4, 0xE9, 0x75, 0x00, 0x83, 0xC0, 0x64, 0x03, 0x05, 0, 0, 0, 0};
static const uint8_t kPressure[] = {0x8B, 0x99, 0x54, 0x16, 0x00, 0x00};
static const uint8_t kPressureNatives[] = {0x8B, 0x99, 0x54, 0x16, 0x00, 0x00, 0x03, 0x1D, 0, 0, 0, 0};
static const uint8_t kPairAdded[] = {0x41, 0x8B, 0x85, 0x40, 0x16, 0x00, 0x00};
static const uint8_t kPairAddedFull[] = {0x41, 0x8B, 0x85, 0x40, 0x16, 0x00, 0x00, 0x83, 0xF8, 0x40, 0x0F, 0x83, 0, 0, 0, 0};
static const uint8_t kPairQueued[] = {0x8B, 0x83, 0x40, 0x16, 0x00, 0x00};
static const uint8_t kPairQueuedFull[] = {0x8B, 0x83, 0x40, 0x16, 0x00, 0x00, 0x83, 0xF8, 0x40, 0x0F, 0x83, 0, 0, 0, 0};

#define CO_DENSITY_SITES 9
#define CO_DENSITY_PAIRS 2
static CoSwitchSite g_sites[CO_DENSITY_SITES], g_pairs[CO_DENSITY_PAIRS];

static struct
{
    uintptr_t game, gameEnd;
    volatile uint32_t* units;  /* the living world's units, within reach of the paths */
    int state;                 /* 0 not done, 1 installed, -1 left as the game has it */
    int pairs;                 /* 0 not reached, 1 the mod's, 2 HellForge's, -1 neither */
    volatile LONG complex, raysRefused;
    unsigned ticks;
    float noteIn;
    uint32_t unitsSeen;
} g_density;

static CoSwitchSite Site(uint32_t rva, const uint8_t* pin, uint8_t size, uint8_t copy, uint32_t back, const uint8_t* mod, uint8_t modLen,
    uint32_t modBack, CoSwitchFix fix, const char* what)
{
    return (CoSwitchSite){.rva = rva, .size = size, .pin = pin, .copy = copy, .back = back, .mod = mod, .modLen = modLen, .modBack = modBack,
        .fix = {fix}, .fill = 0x90, .what = what};
}

/* Every site is whole instructions with no branch into it (checked over game.dll's code) and moves no rip-relative
 * reference, but the squad slot's jump, which its game path makes a jump to the same place. */
static void Tables(uintptr_t game, uintptr_t units)
{
    const CoSwitchFix none = {0}, u5 = {5, CO_FIX_REL32, units};
    /* The squad-slot search falls through its jb only after all 512 slots (r13 = rsi = 0, written through at
     * 0x953E44); the function's refusal exit 0x954079 is the one 0x9538EE takes after the same queries. The site
     * starts at the loop's compare so that 0x953942 can hold the bytes Difficulty11 reads; nothing branches into it. */
    g_sites[0] = Site(0x953939u, kSlot, 14, 16, 0x953953u, kSlotFull, 13, 0x954079u, (CoSwitchFix){9, CO_FIX_REL32, game + 0x953910u},
        "the squad-slot search's end");
    g_sites[0].game = kSlotGame;
    g_sites[0].gameFix[0] = (CoSwitchFix){9, CO_FIX_REL32, game + 0x953910u};
    g_sites[0].after = kSlotAfter;
    /* 448 is 7/8 of the AiJob table's first capacity, not a size: the table grows (0x7F6080). The site takes the load
     * before the compare, so 0x952F62 can hold the raised compare Difficulty11 reads; nothing branches into it. */
    g_sites[1] = Site(0x952F5Bu, kAiJobSpawn, 14, 14, 0x952F69u, kAiJobSpawnRaised, 14, 0x952F69u, (CoSwitchFix){3, CO_FIX_REL32, game + 0x33264D8u},
        "the AiJob gate (spawn check)");
    g_sites[1].rel[0] = 3;
    g_sites[1].after = kAiJobSpawnAfter;
    g_sites[2] = Site(0x90EDEAu, kAiJob, 7, 7, 0x90EDF1u, kAiJobRaised, 7, 0x90EDF1u, none, "the AiJob gate (garrison fill)");
    /* The native gates count every AI character; their mod paths leave the living world's units out (clamped at 0, as
     * the count is a few frames old). Each ends with an instruction that sets the flags its branch reads. */
    g_sites[3] = Site(0x952F53u, kRoom, 6, 6, 0x952F59u, kRoomNatives, 16, 0x952F59u, u5, "the 70 room (spawn check)");
    g_sites[4] = Site(0x90EDDBu, kRoom, 6, 6, 0x90EDE1u, kRoomNatives, 16, 0x90EDE1u, u5, "the 70 room (garrison fill)");
    g_sites[5] = Site(0x95577Fu, kRoom, 6, 6, 0x955785u, kRoomNatives, 16, 0x955785u, u5, "the 70 room (slot respawn)");
    g_sites[6] = Site(0x8B88B2u, kRelease, 5, 5, 0x8B88B7u, kReleaseNatives, 15, 0x8B88B7u, u5, "the breach group release");
    g_sites[7] = Site(0x572C51u, kDespawn, 9, 9, 0x572C5Au, kDespawnNatives, 15, 0x572C5Au, (CoSwitchFix){11, CO_FIX_REL32, units},
        "the despawner's threshold");
    /* The flags after 0xA2C090 are set again by 0xA2C098 before any read. */
    g_sites[8] = Site(0xA2C08Au, kPressure, 6, 6, 0xA2C090u, kPressureNatives, 12, 0xA2C090u, (CoSwitchFix){8, CO_FIX_REL32, units},
        "the despawner's pressure exception");
    /* The budget's pair table holds 64 but appends at its count unchecked: the 67th write lands on the count. A pair
     * past 64 goes uncounted (its units only miss the per-type cap), the path the table-off byte +0x1651 takes. The
     * code after each site is lea/mov then a call, so the compare's flags are dead. */
    g_pairs[0] = Site(0xA2BE29u, kPairAdded, 7, 7, 0xA2BE30u, kPairAddedFull, 16, 0xA2BE30u, (CoSwitchFix){12, CO_FIX_REL32, game + 0xA2BE62u},
        "the budget's pair table (unit added)");
    g_pairs[1] = Site(0xA2D6A9u, kPairQueued, 6, 6, 0xA2D6AFu, kPairQueuedFull, 15, 0xA2D6AFu, (CoSwitchFix){11, CO_FIX_REL32, game + 0xA2D6E0u},
        "the budget's pair table (queued unit)");
}

/* HellForge guards the same appends when a mod adds enemies (enemy_rosters.c): a jump to a cave that starts with the
 * site's load and `cmp eax, 64; jae`, the rest of the site nops. Its guard does what ours does, so it is kept. */
static int HellForgePair(const CoSwitchSite* s)
{
    uint8_t site[8], cave[24];
    int32_t rel;
    static const uint8_t full[] = {0x83, 0xF8, 0x40, 0x73, 0x05};
    if (g_coApi->read(g_density.game + s->rva, site, s->size) != 0 || site[0] != 0xE9) return 0;
    for (uint8_t i = 5; i < s->size; i++) if (site[i] != 0x90) return 0;
    memcpy(&rel, site + 1, 4);
    const uintptr_t to = g_density.game + s->rva + 5u + (uintptr_t)(intptr_t)rel;
    return g_coApi->read(to, cave, s->size + sizeof full) == 0 && memcmp(cave, s->pin, s->size) == 0 && memcmp(cave + s->size, full, sizeof full) == 0;
}

/* On the first complex level, not at the install: HellForge decides on its guard once the entity data is in, before
 * any mission, and refuses its rosters when the sites are not the game's bytes then. */
static void Pairs(void)
{
    const char* why = co_switch_check(g_density.game, g_pairs, CO_DENSITY_PAIRS);
    if (why == NULL) why = co_switch_install(g_density.game, g_pairs, CO_DENSITY_PAIRS, CO_SWITCH_DENSITY);
    if (why == NULL) { g_density.pairs = 1; co_log("density: the budget's pair table takes at most 64 pairs on complex levels"); return; }
    if (HellForgePair(&g_pairs[0]) && HellForgePair(&g_pairs[1])) { g_density.pairs = 2; co_log("density: the budget's pair table is guarded by HellForge"); return; }
    g_density.pairs = -1;
    co_log("density: %s is not the game's code nor HellForge's guard; the guards stay off", why);
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
    const uintptr_t game = g_density.game;
    if (g_coApi->read(game + CO_AIJOB, &jobs, 8) != 0 || g_coApi->read(game + CO_AISQUAD, &squads, 8) != 0 || jobs < 0x10000 || squads < 0x10000) return 0;
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
 * game.dll and the 18 inline copies of the submit get the last 512 entries. Off a complex level the submit is the game's. */
typedef uint32_t (*CoRayFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static CoRayFn g_raySubmit, g_raySubmit2;

static int RefuseRay(uint64_t queue, uintptr_t back)
{
    const uintptr_t game = g_density.game;
    if (!g_density.complex || *(volatile uint32_t*)(uintptr_t)queue < CO_DENSITY_RAY_LIMIT || back < game || back >= g_density.gameEnd) return 0;
    if (back == game + 0x916F63u || back == game + 0x13C5DBCu) return 0;
    InterlockedIncrement(&g_density.raysRefused);
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
    const uintptr_t game = g_density.game;
    if (!g_coApi->code_matches(game + 0x175CA60u, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57") ||
        !g_coApi->code_matches(game + 0x175CBD0u, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56"))
    { co_log("density: the ray queue's submit is not the reviewed one; no ray guard"); return; }
    if (HookFirst(game + 0x175CA60u, (void*)RaySubmit, (void**)&g_raySubmit) != 0 ||
        HookFirst(game + 0x175CBD0u, (void*)RaySubmit2, (void**)&g_raySubmit2) != 0)
    { co_log("density: the ray queue's submit could not be hooked; no ray guard"); return; }
    co_log("density: on complex levels the ray queue refuses new rays above %u of 4096", CO_DENSITY_RAY_LIMIT);
}

const char* co_density_install(uintptr_t game)
{
    const char* why;
    size_t size = 0;
    if (g_density.state != 0) return g_density.state > 0 ? NULL : "refused before";
    g_density.game = game;
    g_density.state = -1;
    g_density.noteIn = 10.0f;
    g_density.gameEnd = g_coApi->module_base("game.dll", &size) == game && size ? game + size : game;
    uintptr_t data = co_alloc_near(game + 0x952F53u, 0x1000);
    if (data == 0) return "no memory near the game for the living world's unit count";
    g_density.units = (volatile uint32_t*)data;
    Tables(game, data);
    if ((why = co_switch_check(game, g_pairs, CO_DENSITY_PAIRS)) != NULL && !(HellForgePair(&g_pairs[0]) && HellForgePair(&g_pairs[1])))
        return why;
    if ((why = co_switch_install(game, g_sites, CO_DENSITY_SITES, CO_SWITCH_DENSITY)) != NULL) return why;
    g_density.state = 1;
    InstallRayGuard();
    return NULL;
}

void co_density_live(int complex)
{
    if (g_density.state != 1) return;
    if (complex && g_density.pairs == 0) Pairs();
    InterlockedExchange(&g_density.complex, complex && co_density_on());
    if (!g_density.complex) *g_density.units = 0;
    co_switch_live(CO_SWITCH_DENSITY, g_density.complex);
}

int co_density_on(void) { return g_density.state == 1 && g_density.pairs >= 0; }

void co_density_tick(float dt)
{
    if (!g_density.complex) return;
    if (++g_density.ticks % 6 == 0)
    {
        uint32_t units;
        if (!CountUnits(&units)) units = 0; /* unreadable tables count as 0: the gates are then the game's count */
        *g_density.units = units;
        if (units > g_density.unitsSeen) g_density.unitsSeen = units;
    }
    g_density.noteIn -= dt;
    if (g_density.noteIn > 0.0f) return;
    g_density.noteIn = 10.0f;
    LONG refused = InterlockedExchange(&g_density.raysRefused, 0);
    if (g_density.unitsSeen || refused)
        co_log("density: living world units %u (most %u in 10 s), rays refused %ld", *g_density.units, g_density.unitsSeen, (long)refused);
    g_density.unitsSeen = 0;
}

#ifdef CO_TEST
void co_density_test_reset(void) { memset(&g_density, 0, sizeof g_density); }
void co_density_test_fill(uint8_t* image)
{
    Tables(0, 0);
    for (size_t i = 0; i < CO_DENSITY_SITES; i++) memcpy(image + g_sites[i].rva, g_sites[i].pin, g_sites[i].size);
    for (size_t i = 0; i < CO_DENSITY_PAIRS; i++) memcpy(image + g_pairs[i].rva, g_pairs[i].pin, g_pairs[i].size);
}
int co_density_test_pairs(void) { return g_density.pairs; }
uint32_t co_density_test_units(void) { return g_density.units ? *g_density.units : 0xFFFFFFFFu; }
void co_density_test_set_units(uint32_t units) { *g_density.units = units; }
int co_density_test_count(uint32_t* units) { return CountUnits(units); }
int co_density_test_refuse(uint64_t queue, uintptr_t back) { return RefuseRay(queue, back); }
#endif
