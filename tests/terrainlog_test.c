/* The research log's read-only views (xl/co_terrainlog.h) on made-up engine memory: the material cache's terrain
 * keys, one key's slot, a terrain's material lists and their blocks, the render config's slot find, the world's
 * terrain map with a chained entry, memory that does not read, and the lines a level may write. */
#include <stdio.h>
#include <string.h>

#include "xl/co_terrainlog.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uintptr_t g_bad, g_badEnd; /* a range that does not read */
static int Read(uintptr_t at, void* out, size_t size)
{
    if (at < 0x10000 || (at + size > g_bad && at < g_badEnd)) return 1;
    memcpy(out, (const void*)at, size);
    return 0;
}
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }
static uint64_t At(const void* p) { return (uint64_t)(uintptr_t)p; }

#define SLOTS 16u
static uint8_t g_holder[0x100], g_table[SLOTS * 24], g_mat[4][0x48], g_block[2][0x60];
static void Slot(uint32_t i, uint64_t key, uint64_t material, int32_t refs, uint32_t generation)
{
    Put64(g_table + i * 24, key); Put64(g_table + i * 24 + 8, material); Put32(g_table + i * 24 + 16, (uint32_t)refs); Put32(g_table + i * 24 + 20, generation);
}
static void Material(int k, uint32_t handle, const uint8_t* block, uint64_t id)
{
    Put32(g_mat[k] + 4, handle); Put64(g_mat[k] + 0x10, At(block)); Put64(g_mat[k] + 0x38, id);
}

int main(void)
{
    CoTlEntry e[8], one;
    uint32_t slots = 0;

    g_case = "the keys";
    CHECK(co_tl_terrain_key(0x00FFBAAB12345678ull) && co_tl_terrain_key(0x01FFBAAB12345678ull) && co_tl_terrain_key(0x80FFBAAB00000000ull));
    CHECK(!co_tl_terrain_key(0x00FFBAAC12345678ull) && !co_tl_terrain_key(0x0000000012345678ull) && !co_tl_terrain_key(0xFFBAAB0012345678ull));

    g_case = "the cache";
    Put64(g_holder + 0x98, At(g_table)); Put32(g_holder + 0xA8, SLOTS);
    Material(0, 0x41, g_block[0], 0xAAAABBBB12345678ull); Material(1, 0x42, g_block[1], 0xCCCCDDDD12345678ull);
    const uint64_t first = 0x00FFBAAB12345678ull, second = 0x01FFBAAB12345678ull, other = 0x1122334455667788ull;
    Slot((uint32_t)(first % SLOTS), first, At(g_mat[0]), 2, 7);
    Slot((uint32_t)(first % SLOTS) + 1, second, At(g_mat[1]), 0, 9); /* second % 16 is first % 16: probed one on */
    Slot(3, other, At(g_mat[2]), 1, 1);
    CHECK(second % SLOTS == first % SLOTS && first % SLOTS != 3 && first % SLOTS + 1 != 3);
    CHECK(co_tl_cache(Read, At(g_holder), e, 8, &slots) == 2 && slots == SLOTS);
    CHECK(e[0].key == first && e[0].material == At(g_mat[0]) && e[0].refs == 2 && e[0].generation == 7 && e[0].handle == 0x41 &&
        e[0].block == At(g_block[0]) && e[0].id == 0xAAAABBBB12345678ull);
    CHECK(e[1].key == second && e[1].refs == 0 && e[1].generation == 9 && e[1].handle == 0x42 && e[1].block == At(g_block[1]));
    CHECK(co_tl_cache(Read, At(g_holder), e, 1, &slots) == 2); /* both counted, one stored */
    CHECK(co_tl_cache_key(Read, At(g_holder), first, &one) == 1 && one.material == At(g_mat[0]) && one.refs == 2);
    CHECK(co_tl_cache_key(Read, At(g_holder), second, &one) == 1 && one.material == At(g_mat[1]) && one.id == 0xCCCCDDDD12345678ull);
    CHECK(co_tl_cache_key(Read, At(g_holder), 0x00FFBAAB00000009ull, &one) == 0);
    Slot(5, 0x00FFBAAB00000005ull, 0, -1, 3); /* a slot whose material is not stored yet */
    CHECK(co_tl_cache(Read, At(g_holder), e, 8, &slots) == 3 && e[0].key == 0x00FFBAAB00000005ull && e[0].material == 0 && e[0].block == 0 && e[0].refs == -1);
    Slot(5, 0, 0, 0, 0);

    g_case = "a cache that does not read";
    CHECK(co_tl_cache(Read, 0, e, 8, &slots) == -1 && co_tl_cache_key(Read, 0, first, &one) == -1);
    Put32(g_holder + 0xA8, CO_TL_CACHE_SLOTS + 1);
    CHECK(co_tl_cache(Read, At(g_holder), e, 8, &slots) == -1 && co_tl_cache_key(Read, At(g_holder), first, &one) == -1);
    Put32(g_holder + 0xA8, SLOTS);
    g_bad = (uintptr_t)g_table + 5 * 24; g_badEnd = g_bad + 24;
    CHECK(co_tl_cache(Read, At(g_holder), e, 8, &slots) == -1);
    g_bad = (uintptr_t)g_mat[0]; g_badEnd = g_bad + sizeof g_mat[0]; /* a material that is gone: its slot is still shown */
    CHECK(co_tl_cache(Read, At(g_holder), e, 8, &slots) == 2 && e[0].material == At(g_mat[0]) && e[0].block == 0 && e[0].refs == 2);
    g_bad = g_badEnd = 0;

    g_case = "a terrain's lists";
    static uint8_t terrain[0xAA8], listA[2 * 16], listB[1 * 16], entries[40], data[16];
    CoTlMaterial m[4];
    Put32(terrain + 0xA60, 2); Put64(terrain + 0xA68, At(listA)); Put32(terrain + 0xA78, 1); Put64(terrain + 0xA80, At(listB));
    Put32(listA, 0x1111); Put64(listA + 8, At(g_mat[0])); Put32(listA + 16, 0x2222); Put64(listA + 24, 0);
    Put32(listB, 0x3333); Put64(listB + 8, At(g_mat[1]));
    Put64(g_block[0] + 0x48, At(entries)); Put32(g_block[0] + 0x50, 2); Put64(g_block[0] + 0x58, At(data));
    Put64(g_block[1] + 0x48, 0x41AD581Eull); Put32(g_block[1] + 0x50, 77); Put64(g_block[1] + 0x58, 0x41AD581Eull);
    CHECK(co_tl_materials(Read, At(terrain), 0, m, 4) == 2);
    CHECK(m[0].name == 0x1111 && m[0].material == At(g_mat[0]) && m[0].handle == 0x41 && m[0].block == At(g_block[0]) && m[0].entries == At(entries) &&
        m[0].count == 2 && m[0].data == At(data));
    CHECK(m[1].name == 0x2222 && m[1].material == 0 && m[1].block == 0);
    CHECK(co_tl_materials(Read, At(terrain), 1, m, 4) == 1 && m[0].name == 0x3333 && m[0].block == At(g_block[1]) && m[0].count == 77 && m[0].data == 0x41AD581Eull);
    g_bad = (uintptr_t)g_block[1]; g_badEnd = g_bad + sizeof g_block[1]; /* a block that is gone */
    CHECK(co_tl_materials(Read, At(terrain), 1, m, 4) == 1 && m[0].block == At(g_block[1]) && m[0].entries == ~0ull && m[0].count == ~0u);
    g_bad = g_badEnd = 0;
    Put32(terrain + 0xA60, CO_TL_LIST_MOST + 1);
    CHECK(co_tl_materials(Read, At(terrain), 0, m, 4) == -1 && co_tl_materials(Read, 0, 0, m, 4) == -1);
    Put32(terrain + 0xA60, 0); Put64(terrain + 0xA68, 0);
    CHECK(co_tl_materials(Read, At(terrain), 0, m, 4) == 0);

    g_case = "the config slot";
    static uint8_t table[0x28], cells[8 * 0x20];
    int state = 7;
    Put64(table + 8, At(cells)); Put32(table + 0x18, 3); Put32(table + 0x1C, 5); /* 5 buckets, 3 spare cells behind them */
    for (uint32_t i = 0; i < 8; i++) Put32(cells + i * 0x20 + 0x18, 0xFFFFFFFEu);
    const uint32_t ka = 0x2A9E3232u, kb = ka + 5, kc = 0x95E7E3ACu; /* ka and kb share a bucket: kb is chained */
    CHECK(ka % 5 != kc % 5);
    Put32(cells + (ka % 5) * 0x20, ka); Put32(cells + (ka % 5) * 0x20 + 0x18, 6);
    Put32(cells + 6 * 0x20, kb); Put32(cells + 6 * 0x20 + 0x18, 0x7FFFFFFFu);
    Put32(cells + (kc % 5) * 0x20, kc); Put32(cells + (kc % 5) * 0x20 + 0x18, 0x7FFFFFFFu);
    CHECK(co_tl_slot(Read, At(table), ka, &state) == At(cells + (ka % 5) * 0x20 + 8) && state == 1);
    CHECK(co_tl_slot(Read, At(table), kb, &state) == At(cells + 6 * 0x20 + 8) && state == 1);
    CHECK(co_tl_slot(Read, At(table), kc, &state) == At(cells + (kc % 5) * 0x20 + 8) && state == 1);
    CHECK(co_tl_slot(Read, At(table), ka + 10, &state) == 0 && state == 0); /* its bucket's chain does not hold it */
    CHECK(co_tl_slot(Read, At(table), kc + 1, &state) == 0 && state == 0);  /* or its bucket is empty */
    CHECK(co_tl_slot(Read, 0, ka, &state) == 0 && state == -1);
    Put32(cells + 6 * 0x20 + 0x18, 6); /* a chain that never ends is a misread, not a hang */
    CHECK(co_tl_slot(Read, At(table), ka + 10, &state) == 0 && state == -1);

    g_case = "the world's terrains";
    static uint8_t world[0x230550 + 0x20], rows[4 * 0x170];
    CoTlTerrain t[4];
    uint8_t* map = world + 0x230550;
    Put64(map + 8, At(rows)); Put32(map + 0x18, 0); Put32(map + 0x1C, 3);
    for (uint32_t i = 0; i < 4; i++) Put32(rows + i * 0x170 + 0x168, 0xFFFFFFFEu);
    CHECK(co_tl_terrains(Read, At(world), t, 4) == 0);
    Put32(map + 0x18, 3);
    Put64(rows + 1 * 0x170, 0xA000); Put64(rows + 1 * 0x170 + 0x40, 0xA040); Put64(rows + 1 * 0x170 + 0x48, 0xA048); Put32(rows + 1 * 0x170 + 0x168, 3);
    Put64(rows + 3 * 0x170, 0xB000); Put64(rows + 3 * 0x170 + 0x40, 0xB040); Put64(rows + 3 * 0x170 + 0x48, 0xB048); Put32(rows + 3 * 0x170 + 0x168, 0x7FFFFFFFu);
    Put64(rows + 2 * 0x170, 0xC000); Put64(rows + 2 * 0x170 + 0x40, 0xC040); Put64(rows + 2 * 0x170 + 0x48, 0xC048); Put32(rows + 2 * 0x170 + 0x168, 0x7FFFFFFFu);
    CHECK(co_tl_terrains(Read, At(world), t, 4) == 3);
    CHECK(t[0].handle == 0xA000 && t[0].heightmap == 0xA040 && t[0].material == 0xA048 && t[1].handle == 0xB000 && t[2].handle == 0xC000 && t[2].material == 0xC048);
    CHECK(co_tl_terrains(Read, At(world), t, 1) == 3 && co_tl_terrains(Read, 0, t, 4) == -1);
    g_bad = (uintptr_t)rows + 3 * 0x170; g_badEnd = g_bad + 0x170;
    CHECK(co_tl_terrains(Read, At(world), t, 4) == -1);
    g_bad = g_badEnd = 0;

    g_case = "the lines a level may write";
    CoTlPace pace = {0};
    CHECK(co_tl_pace(&pace, 1, 0, 3) == 1 && co_tl_pace(&pace, 1, 0, 3) == 1 && co_tl_pace(&pace, 1, 0, 3) == 2);
    for (int i = 0; i < 100000; i++) CHECK(co_tl_pace(&pace, 1, 0, 3) == 0);
    CHECK(co_tl_pace(&pace, 1, 1, 1) == 2 && co_tl_pace(&pace, 1, 1, 1) == 0); /* each kind has its own count */
    CHECK(co_tl_pace(&pace, 2, 0, 3) == 1 && co_tl_pace(&pace, 2, 1, 1) == 2); /* a new level starts again */
    CHECK(co_tl_pace(&pace, 2, -1, 3) == 0 && co_tl_pace(&pace, 2, CO_TL_KINDS, 3) == 0 && co_tl_pace(&pace, 2, 2, 0) == 0);

    if (g_failed) { printf("terrainlog: %d failed\n", g_failed); return 1; }
    printf("terrainlog: all passed\n");
    return 0;
}
