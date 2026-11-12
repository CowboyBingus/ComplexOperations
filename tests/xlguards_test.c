/* The extra large world's four guards: the destruction records (beside HellForge's 0x10000 slots), the render
 * version tables, the SpeedTree extents and the 256 boundary requests. Each acts on a complex level, whether or not
 * the living world runs, and is the game's code on any other level. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLive(int complex);
const char* CoXlTestGuards(uintptr_t game);
void CoXlTestRenderShrink(uintptr_t map, void (*rehash)(uintptr_t, uint32_t));

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define REC_WORLD 0x346D528u
#define REC_INSERT 0x13E0200u
#define REC_BUILD 0x13DF7D0u
#define REC_RESET 0x13E1730u
#define MAP 0xC5F48u
#define COUNTER 0x1EAF78u
#define SCATTER 0x9E3779B1u
#define MOVER_PASS 0x59CB50u
#define MOVER_APPEND 0x134A7D0u
#define TREE_LIMITS 0x954D70u
#define IN_MISSION 0x3326340u

typedef uintptr_t (*InsertFn)(uintptr_t, uintptr_t, uint32_t, uintptr_t);
typedef void (*WorldFn)(uintptr_t);
typedef void (*PassFn)(uintptr_t);
typedef uint32_t (*AppendFn)(uint32_t*, const void*);
typedef void (*TreeFn)(uintptr_t, const float*, const float*);

static uint32_t g_inserted, g_passCount, g_rehashed, g_rehashTo;
static int32_t g_gameExtents[4];
static uintptr_t TestInsert(uintptr_t a, uintptr_t entries, uint32_t count, uintptr_t d) { g_inserted = count; return 0; }
static void TestPass(uintptr_t system) { g_passCount = *(volatile uint32_t*)(system + 0x4890); }
static uint32_t TestAppend(uint32_t* list, const void* request) { return 7; }
static void TestTree(uintptr_t owner, const float* low, const float* high) { memcpy((void*)(owner + 0x1B4), g_gameExtents, sizeof g_gameExtents); }
static void TestRehash(uintptr_t map, uint32_t buckets) { g_rehashed++; g_rehashTo = buckets; }

static uint8_t* g_world, *g_slots;
/* The world's destruction-record map: `slots` places, empty (key 0), the multiplier `multiplier`. */
static void Map(uint32_t slots, uint32_t multiplier, uint32_t counter)
{
    memset(g_slots, 0, 0x10000u * 8u);
    memcpy(g_world + MAP, &g_slots, 8);
    memcpy(g_world + MAP + 8, &slots, 4);
    memset(g_world + MAP + 0xC, 0, 4);
    memcpy(g_world + MAP + 0x10, &multiplier, 4);
    memcpy(g_world + COUNTER, &counter, 4);
}
static uint32_t Multiplier(void) { uint32_t m; memcpy(&m, g_world + MAP + 0x10, 4); return m; }
/* The game's probe over the map (lookup 0x5F2D21..0x5F2D6C, insert 0x13E0326..0x13E036E): slot (mult x key + i) &
 * (slots - 1), stopping at the empty key or the key. The slot of `key` (or the first empty one), -1 after a full scan. */
static int64_t GameProbe(uint32_t key)
{
    uint32_t slots, empty, mult;
    memcpy(&slots, g_world + MAP + 8, 4); memcpy(&empty, g_world + MAP + 0xC, 4); memcpy(&mult, g_world + MAP + 0x10, 4);
    for (uint32_t i = 0; i < slots; i++)
    {
        const uint32_t at = (mult * key + i) & (slots - 1u);
        uint32_t k; memcpy(&k, g_slots + 8u * at, 4);
        if (k == empty || k == key) return at;
    }
    return -1;
}
static void GameInsert(uint32_t key, uint32_t value)
{
    const int64_t at = GameProbe(key);
    if (at >= 0) { memcpy(g_slots + 8u * (uint32_t)at, &key, 4); memcpy(g_slots + 8u * (uint32_t)at + 4u, &value, 4); }
}
static int GameFinds(uint32_t key, uint32_t value)
{
    const int64_t at = GameProbe(key);
    uint32_t k = 0, v = 0;
    if (at >= 0) { memcpy(&k, g_slots + 8u * (uint32_t)at, 4); memcpy(&v, g_slots + 8u * (uint32_t)at + 4u, 4); }
    return at >= 0 && k == key && v == value;
}
static void Counter(uint32_t counter) { memcpy(g_world + COUNTER, &counter, 4); }

int main(void)
{
    g_case = "the guards' install";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    const uintptr_t game = (uintptr_t)image, exe = (uintptr_t)GetModuleHandleA(NULL);
    g_world = (uint8_t*)VirtualAlloc(NULL, 0x200000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_slots = (uint8_t*)VirtualAlloc(NULL, 0x10000u * 8u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t* engine = (uint8_t*)VirtualAlloc(NULL, 0xB0000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t* system = (uint8_t*)VirtualAlloc(NULL, 0x100000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL || g_slots == NULL || engine == NULL || system == NULL) { printf("xlguards: no memory\n"); return 1; }
    memcpy(image + REC_WORLD, &g_world, 8);
    g_fake.originals[0].target = game + REC_INSERT; g_fake.originals[0].original = (void*)TestInsert;
    g_fake.originals[1].target = game + MOVER_PASS; g_fake.originals[1].original = (void*)TestPass;
    g_fake.originals[2].target = game + MOVER_APPEND; g_fake.originals[2].original = (void*)TestAppend;
    g_fake.originals[3].target = exe + TREE_LIMITS; g_fake.originals[3].original = (void*)TestTree;
    g_fake.noMatch[0] = 0x13DF877u; /* HellForge's larger maps set the map up through their own jump there */
    CoXlTestLive(0);
    CHECK(CoXlTestGuards(game) == NULL);
    InsertFn insert = (InsertFn)fake_host_detour(REC_INSERT);
    WorldFn built = (WorldFn)fake_host_detour(REC_BUILD), reset = (WorldFn)fake_host_detour(REC_RESET);
    PassFn pass = (PassFn)fake_host_detour(MOVER_PASS);
    AppendFn append = (AppendFn)fake_host_detour(MOVER_APPEND);
    TreeFn trees = (TreeFn)fake_host_detour(exe + TREE_LIMITS - game);
    CHECK(insert != NULL && built != NULL && reset != NULL && pass != NULL && append != NULL && trees != NULL);
    CHECK(fake_host_detour(exe + 0x6F0240u - game) != NULL); /* the render batch end */
    if (insert == NULL || built == NULL || reset == NULL || pass == NULL || append == NULL || trees == NULL) { printf("xlguards: not hooked\n"); return 1; }

    /* the destruction records, with HellForge's 0x10000 slots and with the game's 0x8000 */
    const uint32_t capacities[2] = {0x10000u, 0x8000u};
    for (int c = 0; c < 2; c++)
    {
        g_case = c == 0 ? "the records beside HellForge's 0x10000 slots" : "the records with the game's 0x8000 slots";
        CoXlTestLive(0);
        Map(capacities[c], 1, 0);
        insert(0, 0, 5, 0);
        CHECK(g_inserted == 5 && Multiplier() == 1); /* a normal level: the game's insert and multiplier */
        Counter(0x6FFEu);
        insert(0, 0, 5, 0);
        CHECK(g_inserted == 5 && Multiplier() == 1); /* no room taken off: the game's */

        CoXlTestLive(1);
        Map(capacities[c], 1, 0);
        insert(0, 0, 5, 0);
        CHECK(g_inserted == 5 && Multiplier() == SCATTER); /* a complex level's first batch, the map empty */
        Counter(0x6FFEu);
        insert(0, 0, 5, 0);
        CHECK(g_inserted == 2);
        Counter(0x7000u);
        insert(0, 0, 5, 0);
        CHECK(g_inserted == 0);

        CoXlTestLive(0); /* the next level is a normal one: the game's multiplier while the map is empty again */
        Counter(0);
        insert(0, 0, 3, 0);
        CHECK(g_inserted == 3 && Multiplier() == 1);
        Map(capacities[c], SCATTER, 0);
        uint32_t key = 0x10001u;
        memcpy(g_slots + 8u * 17u, &key, 4); /* a key still in it: the multiplier it was hashed with stays */
        insert(0, 0, 3, 0);
        CHECK(g_inserted == 3 && Multiplier() == SCATTER);

        Map(capacities[c], 1, 0);
        CoXlTestLive(1);
        built((uintptr_t)g_world);
        CHECK(Multiplier() == SCATTER);
        CoXlTestLive(0);
        reset((uintptr_t)g_world);
        CHECK(Multiplier() == 1);
    }
    /* After a complex mission the map can keep its keys into a normal level: its multiplier stays, and every record
     * is found by the game's own probe, as under the game's multiplier (only the slots they sit in differ). */
    g_case = "a complex mission's records at a normal level's first batch";
    {
        CoXlTestLive(1);
        Map(0x8000u, 1, 0);
        insert(0, 0, 5, 0);
        CHECK(Multiplier() == SCATTER);
        for (uint32_t k = 1; k <= 400; k++) GameInsert(k * 0x9E37u + 3u, k);
        for (uint32_t k = 0; k < 24; k++) GameInsert(1u + k * 0x8000u, 1000u + k); /* one home slot: a run of collisions */
        CoXlTestLive(0);
        Counter(0);
        insert(0, 0, 3, 0);
        CHECK(g_inserted == 3 && Multiplier() == SCATTER);
        int found = 1;
        for (uint32_t k = 1; k <= 400; k++) found &= GameFinds(k * 0x9E37u + 3u, k);
        for (uint32_t k = 0; k < 24; k++) found &= GameFinds(1u + k * 0x8000u, 1000u + k);
        CHECK(found);
        CHECK(GameProbe(0x12345678u) >= 0 && GameFinds(0x12345678u, 0) == 0); /* a key never inserted: an empty slot, a miss */
    }
    g_case = "a map whose slots are not a power of two";
    CoXlTestLive(1);
    Map(0x9000u, 1, 0);
    insert(0, 0, 5, 0);
    built((uintptr_t)g_world);
    CHECK(g_inserted == 5 && Multiplier() == 1);

    /* the 256 boundary requests: the pass stops at 240 on a complex level; any other runs all of them */
    g_case = "the boundary requests";
    uint32_t list[4] = {0};
    uint8_t* records = system + 0x80000;
    uintptr_t at = (uintptr_t)list, recs = (uintptr_t)records;
    const uint32_t n = 300u;
    memcpy(system + 0x4878, &at, 8); memcpy(system + 0x48D0, &recs, 8); memcpy(system + 0x4890, &n, 4);
    for (uint32_t i = 0; i < n; i++) { uint32_t handle = 0x10000u | i; memcpy(records + 0xA4u * i + 0x54, &handle, 4); }
    for (uint32_t i = 0; i < n; i++) { uint32_t request = 9; memcpy(system + 8u * (3u + i) + 4u, &request, 4); }
    CoXlTestLive(0);
    pass((uintptr_t)system);
    uint32_t after, kept = 0, cut = 0;
    memcpy(&after, system + 0x4890, 4);
    for (uint32_t i = 0; i < n; i++) { uint32_t r; memcpy(&r, system + 8u * (3u + i) + 4u, 4); kept += r == 9; }
    CHECK(g_passCount == n && after == n && kept == n);
    CoXlTestLive(1);
    pass((uintptr_t)system);
    memcpy(&after, system + 0x4890, 4);
    kept = 0;
    for (uint32_t i = 0; i < n; i++) { uint32_t r; memcpy(&r, system + 8u * (3u + i) + 4u, 4); kept += r == 9; cut += r == 0; }
    CHECK(g_passCount == 240u && after == n && kept == 240u && cut == n - 240u);
    list[0] = 256;
    CHECK(append(list, NULL) == 0); /* the list is full: request 0, "none this frame" */
    list[0] = 10;
    CHECK(append(list, NULL) == 7);
    CoXlTestLive(0);
    list[0] = 256;
    CHECK(append(list, NULL) == 7); /* a normal level: the game's append */

    /* the trees' extents: a mission's second axis emptied by a destroyed tree comes back on a complex level only */
    g_case = "the trees' extents";
    const uint32_t mission = 4;
    uintptr_t engineAt = (uintptr_t)engine;
    memcpy(engine + 0xAC21C, &mission, 4);
    memcpy(image + IN_MISSION, &engineAt, 8);
    static uint8_t owner[0x200];
    const float low[2] = {0, 0}, high[2] = {100, 100};
    const int32_t wide[4] = {10, 20, 5, 15}, narrow[4] = {10, 20, 0, 0};
    int32_t now[4];
    CoXlTestLive(1);
    memcpy(g_gameExtents, wide, sizeof wide);
    trees((uintptr_t)owner, low, high);
    memcpy(g_gameExtents, narrow, sizeof narrow);
    trees((uintptr_t)owner, low, high);
    memcpy(now, owner + 0x1B4, sizeof now);
    CHECK(memcmp(now, wide, sizeof wide) == 0);
    CoXlTestLive(0);
    memcpy(g_gameExtents, wide, sizeof wide);
    trees((uintptr_t)owner, low, high);
    memcpy(g_gameExtents, narrow, sizeof narrow);
    trees((uintptr_t)owner, low, high);
    memcpy(now, owner + 0x1B4, sizeof now);
    CHECK(memcmp(now, narrow, sizeof narrow) == 0); /* the game's */

    /* the render version tables: a long, nearly empty table is rehashed small on a complex level only */
    g_case = "the render version tables";
    uint8_t map[0x20] = {0};
    const uint32_t size = 56114u, used = 120u, buckets = 65536u;
    memcpy(map, &size, 4); memcpy(map + 0x18, &used, 4); memcpy(map + 0x1C, &buckets, 4);
    CoXlTestLive(0);
    CoXlTestRenderShrink((uintptr_t)map, TestRehash);
    CHECK(g_rehashed == 0);
    CoXlTestLive(1);
    CoXlTestRenderShrink((uintptr_t)map, TestRehash);
    CHECK(g_rehashed == 1 && g_rehashTo == used * 4u + 19u);

    if (g_failed) { printf("xlguards: %d checks failed\n", g_failed); return 1; }
    printf("xlguards: all cases passed\n");
    return 0;
}
