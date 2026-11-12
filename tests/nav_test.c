/* The navigation grid's steps in front of HellForge's: HellForge's jumps and stubs and the engine's code are pinned, a
 * grid of 8 x 8 is left to HellForge untouched, a grid of 32 x 32 gets the level's plan, the ship's step puts play's
 * world back first, and a refused install leaves both sites HellForge's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"
#include "xl/co_normal.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define RUN 0x3300000u
#define LEVEL 0x1500000u
#define WORLDS 0x100000u /* in the exe: the pointers, then world 0 at +0x1000 */
#define WORLD (WORLDS + 0x1000u)

static uint8_t* g_exe;
static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_nav_test_reset();
    fake_stock_nav(g_fake.image);
    g_exe = g_fake.exe;
    return g_fake.image;
}

static struct { int calls, ships; uint32_t id; uintptr_t level; } g_seen;
static void Retile(uint32_t id, uintptr_t level) { g_seen.calls++; g_seen.id = id; g_seen.level = level; }
static void Ship(void) { g_seen.ships++; }

/* A stub as the game reaches it: by a jump with the stack aligned, the id in ecx, the pass in r15 (the mission's) or
 * the state in r14 (the ship's). HellForge's stub behind it is a `ret` here. */
static void Run(uint8_t* image, uintptr_t stub, int ship, uint32_t id, void* context)
{
    static const uint8_t mission[] = {0x41, 0x57, 0x48, 0x83, 0xEC, 0x08, 0x4C, 0x8B, 0xFA, 0x41, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x08, 0x41, 0x5F, 0xC3};
    static const uint8_t state[] = {0x41, 0x56, 0x48, 0x83, 0xEC, 0x08, 0x4C, 0x8B, 0xF2, 0x41, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x08, 0x41, 0x5E, 0xC3};
    memcpy(image + RUN, ship ? state : mission, sizeof mission);
    ((void (*)(uint32_t, void*, uintptr_t))(void*)(image + RUN))(id, context, stub);
}

/* Nav world 0 with a grid of `cells` whose block holds `room` entries of the 12-byte array more than it has. */
static uintptr_t World(uint16_t cells, int room)
{
    const uint64_t worlds = (uint64_t)(uintptr_t)(g_exe + WORLDS), world = (uint64_t)(uintptr_t)(g_exe + WORLD);
    const uint32_t count = 1, entries = 1024;
    const uint16_t grid[2] = {cells, cells};
    const uint64_t array = world + 0x10000u, back = array + 12u * (uint64_t)(int64_t)((int)entries + room);
    memset(g_exe + WORLD, 0x11, 0x6800);
    memcpy(g_exe + CO_NAV_WORLDS, &worlds, 8); memcpy(g_exe + CO_NAV_COUNT, &count, 4); memcpy(g_exe + WORLDS, &world, 8);
    memcpy(g_exe + WORLD + 0x6710, grid, 4); memcpy(g_exe + WORLD + 0x6758, &array, 8); memcpy(g_exe + WORLD + 0x6750, &entries, 4);
    memcpy(g_exe + WORLD + 0x78, &back, 8);
    return (uintptr_t)world;
}
static uintptr_t Level(float disk, float radius)
{
    memcpy(g_fake.image + LEVEL + 0x2D7C, &disk, 4); memcpy(g_fake.image + LEVEL + 0x2D8C, &radius, 4);
    return (uintptr_t)g_fake.image + LEVEL;
}
static float F(uint32_t at) { float f; memcpy(&f, g_exe + WORLD + at, 4); return f; }
/* The grid's tiles are the plan's; the world's own bounds are stock's (`own`) on an armed level, the plan's on a complex one. */
static int Bounds(float low, float high, float tile, float ownLow, float ownHigh)
{
    return F(0x08) == ownLow && F(0x0C) == ownLow && F(0x14) == ownHigh && F(0x18) == ownHigh && F(0x6718) == tile && F(0x671C) == low && F(0x6720) == low &&
        F(0x6724) == high && F(0x6728) == high && F(0x10) == F(0x04) && F(0x672C) == F(0x04); /* and nothing beside them */
}
static uintptr_t TileRead(void) { int32_t d; memcpy(&d, g_exe + CO_NAV_TILE_READ + 4, 4); return CO_NAV_TILE_READ + 8u + (uintptr_t)(intptr_t)d; }

int main(void)
{
    uint8_t* image = Reset("the check");
    const uintptr_t game = (uintptr_t)image;
    uintptr_t stubs[2] = {0, 0};
    DWORD old;
    CHECK(co_nav_check(game, stubs) == NULL && stubs[0] == game + FAKE_STOCK_NAV && stubs[1] == game + FAKE_STOCK_NAV + 0x100u && g_fake.writes == 0);
    memcpy(g_exe + CO_NAV_TILE_READ + 4, kNavTileRead256, 4);
    CHECK(co_nav_check(game, NULL) == NULL); /* HellForge's 256 m tiles at this moment */
    {
        const uint32_t other[] = {CO_NAV_SITE, CO_NAV_SITE + 14u, CO_NAV_SHIP_SITE + 9u, FAKE_STOCK_NAV + 54u, FAKE_STOCK_NAV + 127u, FAKE_STOCK_NAV + 141u,
            FAKE_STOCK_NAV + 0x100u + 61u, FAKE_STOCK_NAV + 0x100u + 140u};
        const uint32_t engine[] = {CO_NAV_EXE_PIN + 5u, CO_NAV_TILE_READ + 2u, CO_NAV_TILE_READ + 5u, CO_NAV_TILE_128 + 3u, CO_NAV_TILE_256 + 3u};
        for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
        {
            image = Reset("HellForge's step is another");
            image[other[i]] ^= 0x04;
            CHECK(co_nav_check(game, NULL) != NULL && co_nav_install(game) != NULL && g_fake.writes == 0);
        }
        for (size_t i = 0; i < sizeof engine / sizeof engine[0]; i++)
        {
            image = Reset("the engine's grid is another");
            g_exe[engine[i]] ^= 0x04;
            CHECK(co_nav_check(game, NULL) != NULL && co_nav_install(game) != NULL && g_fake.writes == 0);
        }
        image = Reset("HellForge's own function may be any");
        image[FAKE_STOCK_NAV + 62u] ^= 0xFF; image[FAKE_STOCK_NAV + 0x100u + 70u] ^= 0xFF;
        CHECK(co_nav_check(game, NULL) == NULL);
        image = Reset("no engine");
        g_fake.exe = NULL;
        CHECK(co_nav_check(game, NULL) != NULL);
        g_coApi = fake_host_reset(CO_GAME_BUILD); /* a game without HellForge's level 11 */
        fake_host_exe();
        CHECK(co_nav_check((uintptr_t)g_fake.image, NULL) != NULL);
    }

    image = Reset("the stubs");
    {
        uint8_t stub[160];
        const uintptr_t retile = (uintptr_t)Retile;
        int32_t rel;
        CHECK(co_nav_stub(stub, 0x5000, 0, retile, 0x9000) == CO_NAV_STUB && co_nav_stub(stub, 0x5000, 0, retile, 0x9000) <= 0x100);
        memcpy(&rel, stub + 125, 4);
        CHECK(memcmp(stub, kNavStock, 60) == 0 && memcmp(stub + 60, &retile, 8) == 0 && memcmp(stub + 68, kNavStock + 68, 56) == 0 && stub[124] == 0xE9 &&
            0x5000 + 129 + rel == 0x9000);
        CHECK(co_nav_stub(stub, 0x5000, 1, retile, 0x9000) == CO_NAV_SHIP_STUB);
        memcpy(&rel, stub + 130, 4);
        CHECK(memcmp(stub, kNavShipStock, 65) == 0 && memcmp(stub + 65, &retile, 8) == 0 && memcmp(stub + 73, kNavShipStock + 73, 56) == 0 && stub[129] == 0xE9 &&
            0x5000 + 134 + rel == 0x9000);

        g_case = "the stubs, run";
        void* pass[4] = {0, 0, 0, (void*)(uintptr_t)0x7777};
        uint8_t state[0x1100] = {0};
        const uint32_t id = 3;
        memcpy(state + 0x10F0, &id, 4);
        CHECK(VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old));
        image[0x3301000u] = 0xC3; /* HellForge's stub: back to the test */
        co_nav_stub(image + 0x3302000u, game + 0x3302000u, 0, retile, game + 0x3301000u);
        co_nav_stub(image + 0x3302100u, game + 0x3302100u, 1, retile, game + 0x3301000u);
        memset(&g_seen, 0, sizeof g_seen);
        Run(image, game + 0x3302000u, 0, 5, pass);
        CHECK(g_seen.calls == 1 && g_seen.id == 5 && g_seen.level == 0x7777);
        Run(image, game + 0x3302100u, 1, 9, state);
        CHECK(g_seen.calls == 2 && g_seen.id == 3 && g_seen.level == 0); /* the ship: the id from its state, no level */
    }

    image = Reset("the install");
    uint8_t* before = (uint8_t*)malloc(FAKE_IMAGE_SIZE);
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_nav_install(game) == NULL && g_fake.writes == 4 && co_nav_test_page() != 0 && co_nav_test_tile() != NULL); /* and the tile stubs' two */
    {
        const uintptr_t page = co_nav_test_page();
        int32_t rel;
        memcpy(&rel, image + CO_NAV_SITE + 1, 4);
        CHECK(image[CO_NAV_SITE] == 0xE9 && game + CO_NAV_SITE + 5u + (intptr_t)rel == page && memcmp(image + CO_NAV_SITE + 5, before + CO_NAV_SITE + 5, 10) == 0);
        memcpy(&rel, image + CO_NAV_SHIP_SITE + 1, 4);
        CHECK(image[CO_NAV_SHIP_SITE] == 0xE9 && game + CO_NAV_SHIP_SITE + 5u + (intptr_t)rel == page + 0x100u);
        CHECK(memcmp(image + FAKE_STOCK_NAV, before + FAKE_STOCK_NAV, 0x200) == 0); /* HellForge's stubs are not written */
        memcpy(&rel, (const uint8_t*)page + 125, 4);
        CHECK(page + 129u + (intptr_t)rel == game + FAKE_STOCK_NAV); /* and are where the mod's go on */
        memcpy(&rel, (const uint8_t*)page + 0x100 + 130, 4);
        CHECK(page + 0x100u + 134u + (intptr_t)rel == game + FAKE_STOCK_NAV + 0x100u);
        CHECK(co_nav_install(game) == NULL && g_fake.writes == 4);
    }

    g_case = "a grid of 8 x 8";
    const float hellforge = 1022.0f / 1024.0f, key = co_disk_key();
    co_nav_ship(Ship);
    memset(&g_seen, 0, sizeof g_seen);
    World(8, 100);
    uint8_t* world = (uint8_t*)malloc(0x6800);
    memcpy(world, g_exe + WORLD, 0x6800);
    co_nav_retile(0, Level(hellforge, 1023.0f)); co_nav_retile(0, Level(key, 8074.4f)); co_nav_retile(0, 0);
    /* HellForge's to retile: nothing written, and the ship's step still puts play's world back */
    CHECK(g_fake.writes == 4 && memcmp(world, g_exe + WORLD, 0x6800) == 0 && TileRead() == CO_NAV_TILE_128 && g_seen.ships == 1);

    g_case = "a grid of 32 x 32";
    World(32, -1); /* a block the grid's arrays do not fit */
    memcpy(world, g_exe + WORLD, 0x6800);
    co_nav_retile(0, Level(hellforge, 1023.0f));
    CHECK(g_fake.writes == 4 && memcmp(world, g_exe + WORLD, 0x6800) == 0 && fake_host_logged("nav world 0 is not armed"));
    World(32, 0);
    co_nav_retile(1, Level(hellforge, 1023.0f)); /* no such world */
    CHECK(g_fake.writes == 4 && fake_host_logged("nav world 1 is not armed"));
    co_nav_retile(0, Level(hellforge, 1023.0f)); /* HellForge's level 11: its 256 m tiles, bounds four times wider */
    CHECK(Bounds(-4095.75f, 4096.25f, 256.0f, -1023.75f, 1024.25f) && TileRead() == CO_NAV_TILE_256);
    co_nav_retile(0, Level(0.5f, 600.0f));       /* a lower level */
    CHECK(Bounds(-2047.75f, 2048.25f, 128.0f, -511.75f, 512.25f) && TileRead() == CO_NAV_TILE_128);
    co_nav_retile(0, Level(key, 8074.4f));       /* a complex level: 512 m, a size the engine has no constant for */
    CHECK(Bounds(-8190.0f, 8194.0f, 512.0f, -8190.0f, 8194.0f) && (uintptr_t)g_exe + TileRead() == (uintptr_t)co_nav_test_tile() && *co_nav_test_tile() == 512.0f);
    CHECK(memcmp(g_exe + CO_NAV_TILE_READ, kNavTileRead, 4) == 0);
    memset(&g_seen, 0, sizeof g_seen);
    co_nav_retile(0, 0);                         /* the ship */
    CHECK(Bounds(-2047.75f, 2048.25f, 128.0f, -511.75f, 512.25f) && TileRead() == CO_NAV_TILE_128 && g_seen.ships == 1);
    free(world);

    for (int fail = 0; fail < 2; fail++)
    {
        image = Reset("a jump that cannot be written");
        memcpy(before, image, FAKE_IMAGE_SIZE);
        g_fake.failWriteAt = fail;
        CHECK(co_nav_install(game) != NULL && co_nav_test_page() == 0 && memcmp(before, image, FAKE_IMAGE_SIZE) == 0);
        World(32, 0);
        co_nav_retile(0, Level(hellforge, 1023.0f)); /* not in: nothing of the engine's is written */
        CHECK(F(0x08) != -4095.75f && TileRead() == CO_NAV_TILE_128);
    }
    free(before);

    if (g_failed) { printf("nav: %d checks failed\n", g_failed); return 1; }
    printf("nav: all cases passed\n");
    return 0;
}
