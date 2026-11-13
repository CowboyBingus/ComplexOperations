/* The engine's room for a world of 16384 m: written as the game loads, only over the bytes it was made for and only
 * before the game's setup; all of it or none; decided on what a machine is known to have then. And the navigation
 * grid's plan for every level of such a process, with HellForge's steps or without them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "co_early_sites.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"
#include "xl/co_forest.h"

void co_capacity_test_commit(uint32_t mb);
void co_early_test_reset(void);
const float* co_early_test_cell(void);
void CoXlTestRecords(int state);
void CoXlTestRenderReset(void);
const uint8_t* CoXlTestRenderPage(void);
void CoXlTestRenderNote(LONG made);
void CoXlTestRenderMade(LONG made);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define POOLS (sizeof kCoEarlyPools / sizeof kCoEarlyPools[0])

static uint8_t g_before[0x2000000];

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_early_test_reset(); co_nav_test_reset(); CoXlTestRecords(0); CoXlTestRenderReset();
    fake_stock_early(g_fake.image);
    memcpy(g_before, g_fake.image + 0x400000u, sizeof g_before); /* every site lies in it */
    return g_fake.image;
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image + 0x400000u, sizeof g_before) == 0; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static float F32(const uint8_t* at) { float v; memcpy(&v, at, 4); return v; }
/* The rel32 at buffer+at for an instruction that ends at `end` and goes to `to`, both as addresses in the process. */
static void Rel32(uint8_t* buffer, size_t at, uintptr_t end, uintptr_t to) { const int32_t rel = (int32_t)((intptr_t)to - (intptr_t)end); memcpy(buffer + at, &rel, 4); }
#define RENDER_ENGINE_AT 0x3700000u /* a free place in the image for the engine object, when a case has one */

/* The engine object the setup stores at game+0x3326340, with its main world at +0x10E8 (0: not made yet). */
static void Engine(uint8_t* image, uint64_t world)
{
    const uint64_t engine = (uint64_t)(uintptr_t)(image + RENDER_ENGINE_AT);
    memcpy(image + 0x3326340u, &engine, 8);
    memcpy(image + RENDER_ENGINE_AT + 0x10E8u, &world, 8);
}

/* A navmesh cutter pool as the engine lays it out (0x7D7F60..0x7D808B) and hands out and takes back handles (0x75230,
 * 0x75420, lookup 0x8128E0): index | live bit (next power of two of the room) | generation | kind and world (22..31). */
static struct { uint32_t room, mask, live, generation, head; uint32_t* entries; } g_cut;
static int CutInit(uint32_t room)
{
    uint32_t m = room - 1u, bits = 0;
    m |= m >> 1; m |= m >> 2; m |= m >> 4; m |= m >> 8; m |= m >> 16;
    const uint32_t x = ~(m | 0xFFC00000u);
    g_cut.room = room; g_cut.mask = m; g_cut.live = m + 1u; g_cut.generation = (x + x) & x; g_cut.head = 0;
    for (uint32_t c = (m + 1u) >> 1; c != 0; c >>= 1) bits++;
    free(g_cut.entries);
    if ((g_cut.entries = calloc(room, 4)) == NULL) return 0;
    if (bits + 9u >= 32u) return 1; /* the engine leaves such a pool's free list empty */
    for (uint32_t i = 0; i < room; i++) g_cut.entries[i] = (i + 1u) | g_cut.generation;
    g_cut.entries[room - 1u] = g_cut.generation;
    g_cut.head = 1; /* flag 0x100: index 0 is never handed out */
    return 1;
}
static uint32_t CutMake(uint32_t user)
{
    const uint32_t index = g_cut.head & g_cut.mask;
    if (index == 0) return 0; /* full: 0x752F4 */
    const uint32_t e = g_cut.entries[index];
    g_cut.head = ((g_cut.head + g_cut.live) & ~g_cut.mask) | (e & g_cut.mask);
    return g_cut.entries[index] = ((e + 2u * g_cut.live) & g_cut.generation) | index | g_cut.live | user; /* flag 0x200 */
}
static int CutLive(uint32_t h)
{
    const uint32_t index = h & g_cut.mask;
    return index < g_cut.room && (h & g_cut.live) != 0 && g_cut.entries[index] == h;
}
static void CutFree(uint32_t h)
{
    const uint32_t index = h & g_cut.mask;
    g_cut.entries[index] = (h & ~(g_cut.live | g_cut.mask)) | (g_cut.head & g_cut.mask);
    g_cut.head = ((g_cut.head + g_cut.live) & ~g_cut.mask) | index;
}

/* The forest grid (exe 0x7ACE50 sizes the block, 0x7AD1D0 lays it out; KB xl-forest-grid-full-crash): `sized` and
 * `laid` are the rooms the two sites hand them. The block: 8 B a bit word (ceil(room/64)) for each of 256 + 256 rows and
 * columns of 8 m over 2048 m, 4 B a slot, 7 to align, all in u32 (0x7ACF3E..0x7ACF83). The layout: 4 B a slot, then
 * the words for 128 + 128 rows and columns of 16 m over -1024..1024. Its free list chains slots 1..laid-1 (0x7AD3D0;
 * the head starts at 1), so laid - 1 trees fit. Its key is the cell box, four 7-bit fields, over the slot (0x7AD778). */
static uint64_t ForestBlock(uint32_t sized) { return 8ull * ((sized + 63ull) / 64u) * 512u + 4ull * sized + 7u; }
static uint32_t ForestBlock32(uint32_t sized) { const uint32_t w = (uint32_t)((sized + 63ull) / 64u); return w * 512u * 8u + 4u * sized + 7u; }
static uint64_t ForestLaid(uint32_t laid) { return 4ull * laid + 8ull * ((laid + 63ull) / 64u) * 256u; }
static uint32_t ForestFits(uint32_t laid)
{
    uint32_t* next = malloc((size_t)laid * 4u), head = 1, n = 0;
    if (next == NULL) return 0;
    for (uint32_t i = 0; i + 1u < laid; i++) next[i] = i + 1u;
    next[laid - 1u] = 0xFFFFFFFFu;
    while (head != 0xFFFFFFFFu) { head = next[head]; n++; }
    free(next);
    return n;
}
static int ForestKeyFits(uint32_t laid) { uint32_t bits = 0; while (bits < 32 && (laid - 1u) >> bits) bits++; return bits + 4u * 7u <= 64u; }
/* The trees an XL map wants at once (KB xl-forest-grid-full-crash; inferred from the crash's 98304 trees at no more
 * than 11262 live stamps and 8-11 cutters a stamp): about 110000 with the window full, about 160000 while a respawn's
 * new stamps come in before the old ones leave. The window's near (80 %) and far (70 %) shares must hold neither. */
#define XL_TREES_WINDOW 110000u
#define XL_TREES_RESPAWN 160000u

/* A HellForge that granted the capacity at boot, and a machine with the memory. */
static void Machine(void)
{
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x1200000u + 0x10010u * i; c->regions[i].places = 100u; }
    co_capacity_test_commit(CO_COMPLEX_COMMIT_MB);
}

int main(void)
{
    uint8_t* image = Reset("everything as HellForge leaves it");
    const uintptr_t game = (uintptr_t)image, exe = (uintptr_t)g_fake.exe;
    CHECK(co_early_check(game, exe) == NULL && g_fake.writes == 0 && !co_early_in());
    CHECK(co_early_install(game, exe) == NULL && co_early_in());
    for (size_t i = 0; i < POOLS; i++)
    {
        const CoEarlyPool* p = &kCoEarlyPools[i];
        CHECK(memcmp(image + p->rva, p->stock, p->at) == 0 && U32(image + p->rva + p->at) == p->own && p->own >= U32(p->stock + p->at));
    }
    CHECK(U32(image + 0xAB19EEu + 6) == 0x100000u && U32(image + 0x4EE324u + 3) == 0x100000u && U32(image + 0x4EE340u + 3) == 0x400000u);
    CHECK(U32(image + 0x4EE32Bu + 3) == 0x8000u); /* HellForge's own stays */
    /* The navmesh cutters (config +0x4C): HellForge's 65536 becomes the bodies' room, so every tree, whose cutter holds
     * one of the world's bodies, gets one; the engine's pool with that room still keeps the kind and world bits clear
     * and a generation bit, so a stale handle is not taken for its slot's next one. */
    const uint32_t cutters = U32(image + 0x4EE371u + 3);
    CHECK(U32(kCoEarlyPools[5].stock + 3) == 0x10000u && kCoEarlyPools[5].rva == 0x4EE371u);
    CHECK(cutters == 0x100000u && cutters >= U32(image + 0xAB19EEu + 6));
    CHECK(64ull * (cutters + 0x600u) + 160ull * (0x200u + 11u) <= 0xFFFFFFFFull); /* the block's size, a u32 (0x78A2C1) */
    if (CutInit(cutters))
    {
        const uint32_t user = (2u << 30) | (1u << 24); /* world 2, a forest tree's kind (0x812DA0) */
        uint32_t made = 0, clear = 1, first = 0, h;
        while ((h = CutMake(user)) != 0)
        {
            if (made++ == 0) first = h;
            if ((h & 0xFFC00000u) != user || (h >> 30) != 2u) clear = 0;
        }
        CHECK(made == cutters - 1u && clear && g_cut.generation != 0);
        CutFree(first);
        h = CutMake(user);
        CHECK(h != 0 && (h & g_cut.mask) == (first & g_cut.mask) && h != first && CutLive(h) && !CutLive(first));
    }
    CHECK(F32(image + CO_EARLY_BROADPHASE) == 17408.0f);
    for (size_t i = 0; i < 4; i++) CHECK(F32(image + kCoEarlyExtents[i].rva + 3) == 4096.0f && memcmp(image + kCoEarlyExtents[i].rva, kCoEarlyExtents[i].bytes, 3) == 0);
    /* the cell size's read reaches the mod's float: 1024 m over 32 cells; sized by 4096 / 128, laid out by 1024 / 32 */
    const float* cell = co_early_test_cell();
    int32_t rel; memcpy(&rel, image + CO_EARLY_CELL_READ + 4, 4);
    CHECK(cell != NULL && *cell == 32.0f && game + CO_EARLY_CELL_READ + 8u + (intptr_t)rel == (uintptr_t)cell);
    CHECK(memcmp(image + CO_EARLY_CELL_READ, kCoEarlyCellRead, 4) == 0);
    /* The forest grid: both of its room's sites in the exe, the size's first, and the block holds what the layout lays */
    {
        const uint8_t* x = g_fake.exe;
        const uint32_t sized = U32(x + kCoEarlyForest[0].rva + kCoEarlyForest[0].at), laid = U32(x + kCoEarlyForest[1].rva + kCoEarlyForest[1].at);
        CHECK(sized == CO_EARLY_FOREST_ROOM && laid == CO_EARLY_FOREST_ROOM && U32(kCoEarlyForest[1].stock + 4) == 0x18000u);
        for (size_t i = 0; i < 2; i++)
            CHECK(memcmp(x + kCoEarlyForest[i].rva, kCoEarlyForest[i].stock, kCoEarlyForest[i].at) == 0 &&
                memcmp(x + kCoEarlyForestCalls[i].rva, kCoEarlyForestCalls[i].bytes, 5) == 0);
        CHECK(g_fake.wroteCount > 11 && g_fake.wrote[10] == exe + 0x20ADA4u && g_fake.wrote[11] == exe + 0x20AE22u);
        CHECK(ForestLaid(laid) <= ForestBlock(sized) && ForestBlock(sized) == ForestBlock32(sized) && ForestKeyFits(laid));
        CHECK(laid & (laid - 1u) ? 0 : 1);
        CHECK(ForestFits(laid) == laid - 1u && ForestFits(0x18000u) == 98303u);
        CHECK(!co_forest_over(XL_TREES_RESPAWN, laid, 0.80f) && !co_forest_over(XL_TREES_WINDOW, laid, 0.70f));
        CHECK(co_forest_over(XL_TREES_RESPAWN, 0x18000u, 0.80f)); /* the game's room: the crash's case */
        CHECK(ForestBlock(laid) <= 40u * 1024u * 1024u); /* about 34 MiB a forest, against 6.4 */
    }
    const int writes = g_fake.writes;
    CHECK(co_early_install(game, exe) == NULL && g_fake.writes == writes); /* once */

    image = Reset("the game's setup has begun");
    image[CO_EARLY_SETUP + 3] = 1;
    memcpy(g_before, image + 0x400000u, sizeof g_before);
    CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged() && g_fake.writes == 0);

    image = Reset("the game's own pool sizes: HellForge wrote none");
    image[0x4EE32Bu + 4] = 0x20; /* ragdoll actors 8192 */
    memcpy(g_before, image + 0x400000u, sizeof g_before);
    CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged() && g_fake.writes == 0);

    const uint32_t sites[] = {0xAB19EEu + 1, 0x4EE340u + 4, CO_EARLY_BROADPHASE_LOAD + 2, CO_EARLY_BROADPHASE + 3, 0x4EE101u + 5, 0x4EE3C5u, CO_EARLY_CELL_READ + 6};
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++)
    {
        image = Reset("a site is not the one this was made for");
        image[sites[i]] ^= 0x10;
        memcpy(g_before, image + 0x400000u, sizeof g_before);
        CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged() && g_fake.writes == 0);
    }
    /* the forest's room sites and the calls they feed (exe) */
    const uint32_t forestSites[] = {0x20ADA2u, 0x20ADA4u + 2u, 0x20AE1Eu + 1u, 0x20AE22u + 1u, 0x20ADC5u + 1u, 0x20AE39u + 2u};
    for (size_t i = 0; i < sizeof forestSites / sizeof forestSites[0]; i++)
    {
        image = Reset("a forest site is not the one this was made for");
        g_fake.exe[forestSites[i]] ^= 0x10;
        uint8_t x[0x100];
        memcpy(x, g_fake.exe + 0x20AD80u, sizeof x);
        CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged() && g_fake.writes == 0 &&
            memcmp(x, g_fake.exe + 0x20AD80u, sizeof x) == 0);
    }
    image = Reset("the engine's terrain build is another");
    g_fake.exe[CO_EARLY_TILE_READ + 5] ^= 1;
    CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && Unchanged());
    image = Reset("no engine");
    CHECK(co_early_install((uintptr_t)image, 0) != NULL && Unchanged());

    /* 10 pools, the forest's 2, the broadphase, 4 extents, the read: a write refused at any of them leaves every site as
     * it was, and the forest's layout goes back before its size */
    for (int fail = 0; fail < 18; fail++)
    {
        image = Reset("a write is refused");
        uint8_t x[0x100];
        memcpy(x, g_fake.exe + 0x20AD80u, sizeof x);
        g_fake.failWriteAt = fail;
        CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged() &&
            memcmp(x, g_fake.exe + 0x20AD80u, sizeof x) == 0);
        if (fail >= 12 && g_fake.wroteCount > fail + 2)
            CHECK(g_fake.wrote[fail + 1] == (uintptr_t)g_fake.exe + 0x20AE22u && g_fake.wrote[fail + 2] == (uintptr_t)g_fake.exe + 0x20ADA4u);
    }
    image = Reset("the eighteenth write is the last");
    g_fake.failWriteAt = 18;
    CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) == NULL && co_early_in());

    /* What decides it as the game loads. */
    image = Reset("a machine that can");
    Machine();
    co_capacity_init(); co_capacity_game((uintptr_t)image);
    co_capacity_early();
    /* the mod's own sites are not this game's (the image has none of them): nothing of the engine's changes */
    CHECK(!co_capacity_needs()->early && !co_early_in() && Unchanged() && g_fake.hookCount == 0);

    image = Reset("too little memory as the game loads");
    Machine(); co_capacity_test_commit(CO_COMPLEX_COMMIT_MB - 1u);
    co_capacity_init(); co_capacity_game((uintptr_t)image);
    co_capacity_early();
    CHECK(!co_capacity_needs()->early && Unchanged() && g_fake.hookCount == 0);
    g_fake.levelRow = 0x1000; g_fake.capacity.moved = 1;
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("were not enlarged as the game started (not enough memory free"));
    CHECK(co_capacity_normal() == 1 && Unchanged()); /* no room was made: the grid is HellForge's */

    image = Reset("HellForge refused the capacity");
    Machine(); g_fake.capacityRefused = 1;
    co_capacity_init(); co_capacity_game((uintptr_t)image);
    co_capacity_early();
    CHECK(!co_capacity_needs()->early && Unchanged() && g_fake.hookCount == 0);
    image = Reset("loaded while the game ran");
    Machine(); g_fake.loadPhase = HF_LOAD_LIVE;
    co_capacity_init(); co_capacity_game((uintptr_t)image);
    co_capacity_early();
    CHECK(!co_capacity_needs()->early && Unchanged() && g_fake.hookCount == 0);

    /* A process with a grid of 32 x 32 and no step of HellForge's at the two sites: the mod's stubs, whole. */
    image = Reset("the navigation sites are the game's");
    memcpy(image + CO_NAV_SITE, kNavGame, sizeof kNavGame); memcpy(image + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame);
    memcpy(g_fake.exe + CO_NAV_EXE_PIN, kNavExe, sizeof kNavExe);
    const float sizes[2] = {128.0f, 256.0f};
    memcpy(g_fake.exe + CO_NAV_TILE_128, &sizes[0], 4); memcpy(g_fake.exe + CO_NAV_TILE_256, &sizes[1], 4);
    CHECK(co_nav_plain((uintptr_t)image) && co_nav_check((uintptr_t)image, NULL) != NULL);
    co_nav_reserve(0, (uintptr_t)g_fake.exe); co_nav_reserve((uintptr_t)image, 0); /* the pages taken early are the ones used */
    const uintptr_t early = (uintptr_t)co_nav_test_reserved();
    CHECK(early != 0 && co_nav_install((uintptr_t)image) == NULL && co_nav_test_page() == early && co_nav_test_reserved() == NULL);
    const uint8_t* page = (const uint8_t*)co_nav_test_page();
    CHECK(page != NULL && image[CO_NAV_SITE] == 0xE9 && image[CO_NAV_SHIP_SITE] == 0xE9);
    for (int k = 5; k < 15; k++) CHECK(image[CO_NAV_SITE + k] == 0x90);
    for (int k = 5; k < 10; k++) CHECK(image[CO_NAV_SHIP_SITE + k] == 0x90);
    if (page != NULL)
    {
        const uint64_t retile = (uint64_t)(uintptr_t)co_nav_retile, step = (uint64_t)(uintptr_t)co_nav_ship_step, system = (uint64_t)(uintptr_t)image + CO_NAV_SYSTEM;
        memcpy(&rel, image + CO_NAV_SITE + 1, 4);
        CHECK((uintptr_t)image + CO_NAV_SITE + 5u + (intptr_t)rel == (uintptr_t)page);
        memcpy(&rel, image + CO_NAV_SHIP_SITE + 1, 4);
        CHECK((uintptr_t)image + CO_NAV_SHIP_SITE + 5u + (intptr_t)rel == (uintptr_t)page + 0x100u);
        /* HellForge's form to the byte, but for the function, the nav system and the way back into the game */
        CHECK(memcmp(page, kNavStock, 60) == 0 && memcmp(page + 60, &retile, 8) == 0 && memcmp(page + 68, kNavStock + 68, 58) == 0);
        CHECK(memcmp(page + 126, &system, 8) == 0 && memcmp(page + 134, kNavStock + 134, 7) == 0);
        memcpy(&rel, page + 141, 4);
        CHECK((uintptr_t)page + 145u + (intptr_t)rel == (uintptr_t)image + CO_NAV_BACK);
        CHECK(memcmp(page + 0x100, kNavShipStock, 65) == 0 && memcmp(page + 0x100 + 65, &step, 8) == 0 && memcmp(page + 0x100 + 73, kNavShipStock + 73, 67) == 0);
        memcpy(&rel, page + 0x100 + 140, 4);
        CHECK((uintptr_t)page + 0x100u + 144u + (intptr_t)rel == (uintptr_t)image + CO_NAV_SHIP_BACK);
    }
    for (int fail = 0; fail < 2; fail++)
    {
        image = Reset("a navigation site cannot be written");
        memcpy(image + CO_NAV_SITE, kNavGame, sizeof kNavGame); memcpy(image + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame);
        memcpy(g_fake.exe + CO_NAV_EXE_PIN, kNavExe, sizeof kNavExe);
        memcpy(g_fake.exe + CO_NAV_TILE_128, &sizes[0], 4); memcpy(g_fake.exe + CO_NAV_TILE_256, &sizes[1], 4);
        g_fake.failWriteAt = fail;
        CHECK(co_nav_install((uintptr_t)image) != NULL && co_nav_test_page() == 0);
        CHECK(memcmp(image + CO_NAV_SITE, kNavGame, sizeof kNavGame) == 0 && memcmp(image + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame) == 0);
    }
    image = Reset("another mod's code at a navigation site");
    memcpy(image + CO_NAV_SITE, kNavGame, sizeof kNavGame); memcpy(image + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame);
    image[CO_NAV_SHIP_SITE] = 0xE8;
    CHECK(!co_nav_plain((uintptr_t)image) && co_nav_install((uintptr_t)image) != NULL && g_fake.writes == 0);

    /* The main world's culling pools: made once, in the setup, so they are sized only by stubs that are in before it. */
    image = Reset("the render capacity before the main world is made");
    Engine(image, 0);
    {
        uint8_t* exe = g_fake.exe;
        CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)exe) == NULL && g_fake.writes == 0 && !fake_host_logged("is made at 2x"));
        CHECK(CoXlRenderWrite((uintptr_t)image) == NULL);
        CHECK(fake_host_logged("render capacity: the main world (capacity 0x200000) is made at 2x: culling pools of 524288, not 262144; pools 0 and 1 of 1048576"));
        /* scene, pool, then N: a creation that read N before its jump runs neither of the other two */
        CHECK(g_fake.wroteCount == 3 && g_fake.wrote[0] == (uintptr_t)exe + FAKE_RENDER_SCENE && g_fake.wrote[1] == (uintptr_t)exe + FAKE_RENDER_POOL0 &&
            g_fake.wrote[2] == (uintptr_t)exe + FAKE_RENDER_CAP);
        const uint8_t* page = CoXlTestRenderPage();
        CHECK(page != NULL);
        if (page != NULL)
        {
            const uintptr_t p = (uintptr_t)page, x = (uintptr_t)exe;
            uint8_t want[34], scene[28], pool[23], jump[8];
            static const uint8_t kStub[34] = {0x8B, 0xBC, 0x24, 0xC8, 0x00, 0x00, 0x00, 0x81, 0xFF, 0x00, 0x00, 0x20, 0x00, 0x75, 0x0E,
                0xF0, 0xFF, 0x05, 0, 0, 0, 0, 0x0F, 0xAF, 0x3D, 0, 0, 0, 0, 0xE9, 0, 0, 0, 0};
            static const uint8_t kScene[28] = {0x49, 0x89, 0x07, 0x81, 0xFF, 0x00, 0x00, 0x40, 0x00, 0x75, 0x07, 0x48, 0x89, 0x05, 0, 0, 0, 0,
                0x48, 0x8D, 0x54, 0x24, 0x50, 0xE9, 0, 0, 0, 0};
            static const uint8_t kPool[23] = {0x81, 0xFF, 0x00, 0x00, 0x40, 0x00, 0x75, 0x02, 0x01, 0xC0, 0x89, 0x44, 0x24, 0x60, 0x89, 0x44, 0x24, 0x64,
                0xE9, 0, 0, 0, 0};
            memcpy(want, kStub, sizeof want); Rel32(want, 18, p + 22, p + 0x40); Rel32(want, 25, p + 29, p + 0x44); Rel32(want, 30, p + 34, x + 0x10CE04u);
            memcpy(scene, kScene, sizeof scene); Rel32(scene, 14, p + 0x60 + 18, p + 0x48); Rel32(scene, 24, p + 0x60 + 28, x + 0x10CEBEu);
            memcpy(pool, kPool, sizeof pool); Rel32(pool, 19, p + 0x80 + 23, x + 0x10CE31u);
            CHECK(memcmp(page, want, sizeof want) == 0);
            CHECK(memcmp(page + 0x60, scene, sizeof scene) == 0);
            CHECK(memcmp(page + 0x80, pool, sizeof pool) == 0);
            CHECK(U32(page + 0x40) == 0 && U32(page + 0x44) == 2u && U32(page + 0x48) == 0 && U32(page + 0x4C) == 0);
            static const uint8_t kJump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
            memcpy(jump, kJump, 8); Rel32(jump, 1, x + FAKE_RENDER_CAP + 5u, p);
            CHECK(memcmp(exe + FAKE_RENDER_CAP, jump, 7) == 0 && exe[FAKE_RENDER_CAP + 7] == 0x48); /* the next instruction kept */
            memcpy(jump, kJump, 8); Rel32(jump, 1, x + FAKE_RENDER_SCENE + 5u, p + 0x60);
            CHECK(memcmp(exe + FAKE_RENDER_SCENE, jump, 8) == 0);
            memcpy(jump, kJump, 8); Rel32(jump, 1, x + FAKE_RENDER_POOL0 + 5u, p + 0x80);
            CHECK(memcmp(exe + FAKE_RENDER_POOL0, jump, 8) == 0);
        }
        const int writes = g_fake.writes;
        CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)exe) == NULL && CoXlRenderWrite((uintptr_t)image) == NULL && g_fake.writes == writes); /* once */
        CoXlTestRenderNote(0);
        CHECK(fake_host_logged("render capacity: no main world was made since the install"));
        CoXlTestRenderNote(1);
        CHECK(fake_host_logged("render capacity: 1 main world(s) made at 2x since the install"));
    }

    image = Reset("the render capacity after the main world was made");
    Engine(image, 0x123456780ull);
    memcpy(g_before, image + 0x400000u, sizeof g_before);
    {
        uint8_t exeBefore[0x100];
        memcpy(exeBefore, g_fake.exe + FAKE_RENDER_CAP, sizeof exeBefore);
        CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && g_fake.writes == 0 && g_fake.wroteCount == 0 && CoXlTestRenderPage() == NULL);
        CHECK(fake_host_logged("render capacity: the main world was made before the mod could size it; it keeps vanilla pools (262144)"));
        CHECK(!fake_host_logged("is made at 2x") && memcmp(exeBefore, g_fake.exe + FAKE_RENDER_CAP, sizeof exeBefore) == 0 && Unchanged());
        CHECK(CoXlRenderWrite((uintptr_t)image) != NULL && g_fake.wroteCount == 0); /* nothing prepared, nothing written */
    }

    const uint32_t pins[2] = {FAKE_RENDER_CAP, FAKE_RENDER_SCENE};
    for (int i = 0; i < 2; i++)
    {
        image = Reset("a render capacity site is not the reviewed code");
        Engine(image, 0);
        g_fake.noMatchExe[0] = pins[i];
        CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && g_fake.wroteCount == 0 && fake_host_logged("is not the reviewed code"));
        CHECK(CoXlRenderWrite((uintptr_t)image) != NULL && g_fake.wroteCount == 0);
    }

    /* A refused jump puts back the ones before it: the world creation is the game's, byte for byte. */
    for (int fail = 0; fail < 3; fail++)
    {
        image = Reset("a render capacity jump is refused");
        Engine(image, 0);
        uint8_t before[0x100];
        memcpy(before, g_fake.exe + FAKE_RENDER_CAP, sizeof before); /* 0x10CDFD..0x10CEFD: all three sites */
        CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)g_fake.exe) == NULL);
        g_fake.failWriteAt = fail;
        CHECK(CoXlRenderWrite((uintptr_t)image) != NULL && CoXlTestRenderPage() == NULL);
        CHECK(memcmp(before, g_fake.exe + FAKE_RENDER_CAP, sizeof before) == 0 && fake_host_logged("the sites written are the game's again"));
        CHECK(g_fake.wroteCount == 2 * fail + 1 && !fake_host_logged("is made at 2x"));
    }

    /* After the jumps: the setup not begun, every later world runs them; begun, only a world they counted proves it. */
    image = Reset("the setup began while the jumps went in, no world counted");
    Engine(image, 0);
    CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)g_fake.exe) == NULL);
    image[CO_EARLY_SETUP + 2] = 1;
    CHECK(CoXlRenderWrite((uintptr_t)image) != NULL && fake_host_logged("the game's setup began while the stubs went in"));
    image = Reset("the setup began while the jumps went in, a world counted");
    Engine(image, 0);
    CHECK(CoXlRenderCheck((uintptr_t)image, (uintptr_t)g_fake.exe) == NULL);
    image[CO_EARLY_SETUP + 2] = 1;
    CoXlTestRenderMade(1);
    CHECK(CoXlRenderWrite((uintptr_t)image) == NULL && !fake_host_logged("setup began"));

    g_case = "the pages near the exe come from the space taken at the start";
    {
        const uintptr_t exe = (uintptr_t)g_fake.exe;
        CHECK(co_near_exe_reserve(0, 0) == 0 && co_near_exe_reserve(exe, FAKE_EXE_SIZE) == 1 && co_near_exe_reserve(exe, FAKE_EXE_SIZE) == 1);
        const uintptr_t a = co_alloc_near(exe + 0x1000u, 0x1000), b = co_alloc_near(exe + FAKE_EXE_SIZE - 1u, 0x2000);
        CHECK(a != 0 && b == a + 0x1000u && *(volatile uint8_t*)b == 0); /* committed, one after the other */
        const uintptr_t other = co_alloc_near((uintptr_t)g_fake.image, 0x1000); /* not the exe: searched as before */
        CHECK(other != 0 && (other < a || other >= a + 0x100000u));
    }
    if (g_failed) { printf("early: %d checks failed\n", g_failed); return 1; }
    printf("early: all cases passed\n");
    return 0;
}
