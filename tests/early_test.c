/* The engine's room for a world of 16384 m: written as the game loads, only over the bytes it was made for and only
 * before the game's setup; all of it or none; decided on what a machine is known to have then. And the navigation
 * grid's plan for every level of such a process, with HellForge's steps or without them. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "co_early_sites.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"

void co_capacity_test_commit(uint32_t mb);
void co_early_test_reset(void);
const float* co_early_test_cell(void);
void CoXlTestRecords(int state);

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
    co_early_test_reset(); co_nav_test_reset(); CoXlTestRecords(0);
    fake_stock_early(g_fake.image);
    memcpy(g_before, g_fake.image + 0x400000u, sizeof g_before); /* every site lies in it */
    return g_fake.image;
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image + 0x400000u, sizeof g_before) == 0; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static float F32(const uint8_t* at) { float v; memcpy(&v, at, 4); return v; }

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
    CHECK(F32(image + CO_EARLY_BROADPHASE) == 17408.0f);
    for (size_t i = 0; i < 4; i++) CHECK(F32(image + kCoEarlyExtents[i].rva + 3) == 4096.0f && memcmp(image + kCoEarlyExtents[i].rva, kCoEarlyExtents[i].bytes, 3) == 0);
    /* the cell size's read reaches the mod's float: 1024 m over 32 cells; sized by 4096 / 128, laid out by 1024 / 32 */
    const float* cell = co_early_test_cell();
    int32_t rel; memcpy(&rel, image + CO_EARLY_CELL_READ + 4, 4);
    CHECK(cell != NULL && *cell == 32.0f && game + CO_EARLY_CELL_READ + 8u + (intptr_t)rel == (uintptr_t)cell);
    CHECK(memcmp(image + CO_EARLY_CELL_READ, kCoEarlyCellRead, 4) == 0);
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
    image = Reset("the engine's terrain build is another");
    g_fake.exe[CO_EARLY_TILE_READ + 5] ^= 1;
    CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && Unchanged());
    image = Reset("no engine");
    CHECK(co_early_install((uintptr_t)image, 0) != NULL && Unchanged());

    /* 10 pools, the broadphase, 4 extents, the read: a write refused at any of them leaves every site as it was */
    for (int fail = 0; fail < 16; fail++)
    {
        image = Reset("a write is refused");
        g_fake.failWriteAt = fail;
        CHECK(co_early_install((uintptr_t)image, (uintptr_t)g_fake.exe) != NULL && !co_early_in() && Unchanged());
    }

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
        const uint64_t retile = (uint64_t)(uintptr_t)co_nav_retile, system = (uint64_t)(uintptr_t)image + CO_NAV_SYSTEM;
        memcpy(&rel, image + CO_NAV_SITE + 1, 4);
        CHECK((uintptr_t)image + CO_NAV_SITE + 5u + (intptr_t)rel == (uintptr_t)page);
        memcpy(&rel, image + CO_NAV_SHIP_SITE + 1, 4);
        CHECK((uintptr_t)image + CO_NAV_SHIP_SITE + 5u + (intptr_t)rel == (uintptr_t)page + 0x100u);
        /* HellForge's form to the byte, but for the function, the nav system and the way back into the game */
        CHECK(memcmp(page, kNavStock, 60) == 0 && memcmp(page + 60, &retile, 8) == 0 && memcmp(page + 68, kNavStock + 68, 58) == 0);
        CHECK(memcmp(page + 126, &system, 8) == 0 && memcmp(page + 134, kNavStock + 134, 7) == 0);
        memcpy(&rel, page + 141, 4);
        CHECK((uintptr_t)page + 145u + (intptr_t)rel == (uintptr_t)image + CO_NAV_BACK);
        CHECK(memcmp(page + 0x100, kNavShipStock, 65) == 0 && memcmp(page + 0x100 + 65, &retile, 8) == 0 && memcmp(page + 0x100 + 73, kNavShipStock + 73, 67) == 0);
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
