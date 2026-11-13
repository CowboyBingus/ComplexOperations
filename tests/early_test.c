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
