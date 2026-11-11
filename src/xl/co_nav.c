/* The navigation grid per level (co_nav.h). */
#include <string.h>

#include <windows.h>

#include "co_nav.h"
#include "co_nav_sites.h"
#include "co_normal.h"

static struct
{
    uintptr_t game, exe;
    uint8_t* page;
    float* tile;          /* near the exe: a tile size the engine has no constant for */
    void (*ship)(void);
} g_nav;

void co_nav_ship(void (*ship)(void)) { g_nav.ship = ship; }

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[160];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* HellForge's jump at a site (E9 and nops) and where it leads, or 0. */
static uintptr_t Jump(uintptr_t site, size_t length)
{
    uint8_t now[15];
    int32_t rel;
    if (g_coApi->read(site, now, length) != 0 || now[0] != 0xE9) return 0;
    for (size_t i = 5; i < length; i++) if (now[i] != 0x90) return 0;
    memcpy(&rel, now + 1, 4);
    return site + 5u + (intptr_t)rel;
}

/* HellForge's stub at `at`: its form, with its own function left out, and its ways on. */
static int Stock(uintptr_t game, uintptr_t at, int ship)
{
    uint8_t now[145];
    const size_t size = ship ? sizeof kNavShipStock : sizeof kNavStock, call = ship ? 65 : 60, back = ship ? 140 : 141;
    const uint64_t system = game + CO_NAV_SYSTEM;
    int32_t rel;
    if (at == 0 || g_coApi->read(at, now, size) != 0) return 0;
    memcpy(&rel, now + back, 4);
    if (at + back + 4u + (intptr_t)rel != game + (ship ? CO_NAV_SHIP_BACK : CO_NAV_BACK)) return 0;
    if (!ship && memcmp(now + 126, &system, 8) != 0) return 0;
    memset(now + call, 0, 8); memset(now + back, 0, 4);
    if (!ship) memset(now + 126, 0, 8);
    return memcmp(now, ship ? kNavShipStock : kNavStock, size) == 0;
}

/* The engine's side. The tile size read may be HellForge's 256 m at this moment: either of its two forms. */
static int Engine(uintptr_t exe)
{
    float sizes[2] = {0.0f, 0.0f};
    return exe != 0 && Same(exe + CO_NAV_EXE_PIN, kNavExe, sizeof kNavExe) && Same(exe + CO_NAV_TILE_READ, kNavTileRead, 4) &&
        (Same(exe + CO_NAV_TILE_READ + 4u, kNavTileRead + 4, 4) || Same(exe + CO_NAV_TILE_READ + 4u, kNavTileRead256, 4)) &&
        g_coApi->read(exe + CO_NAV_TILE_128, &sizes[0], 4) == 0 && g_coApi->read(exe + CO_NAV_TILE_256, &sizes[1], 4) == 0 && sizes[0] == 128.0f && sizes[1] == 256.0f;
}

const char* co_nav_check(uintptr_t game, uintptr_t stubs[2])
{
    const uintptr_t mission = Jump(game + CO_NAV_SITE, sizeof kNavGame), ship = Jump(game + CO_NAV_SHIP_SITE, sizeof kNavShipGame);
    if (!Stock(game, mission, 0) || !Stock(game, ship, 1)) return "difficulty 11's navigation steps are not the ones this mod was built for";
    if (!Engine(g_coApi->module_base("helldivers2.exe", NULL))) return "the engine's navigation grid is not the code this mod was made for";
    if (stubs != NULL) { stubs[0] = mission; stubs[1] = ship; }
    return NULL;
}

int co_nav_plain(uintptr_t game)
{
    return Same(game + CO_NAV_SITE, kNavGame, sizeof kNavGame) && Same(game + CO_NAV_SHIP_SITE, kNavShipGame, sizeof kNavShipGame) &&
        Engine(g_coApi->module_base("helldivers2.exe", NULL));
}

size_t co_nav_whole(uint8_t* out, uintptr_t code, int ship, uintptr_t retile, uintptr_t game)
{
    const size_t size = ship ? sizeof kNavShipStock : sizeof kNavStock, call = ship ? 65 : 60, back = ship ? 140 : 141;
    const uint64_t system = game + CO_NAV_SYSTEM;
    const int32_t rel = (int32_t)((int64_t)(game + (ship ? CO_NAV_SHIP_BACK : CO_NAV_BACK)) - (int64_t)(code + back + 4u));
    memcpy(out, ship ? kNavShipStock : kNavStock, size);
    memcpy(out + call, &retile, 8);
    if (!ship) memcpy(out + 126, &system, 8);
    memcpy(out + back, &rel, 4);
    return size;
}

size_t co_nav_stub(uint8_t* out, uintptr_t code, int ship, uintptr_t retile, uintptr_t stock)
{
    /* HellForge's own stub up to where the registers are back: the same call, with the mod's function */
    const size_t head = ship ? 129 : 124, call = ship ? 65 : 60;
    memcpy(out, ship ? kNavShipStock : kNavStock, head);
    memcpy(out + call, &retile, 8);
    out[head] = 0xE9;
    const int32_t rel = (int32_t)((int64_t)stock - (int64_t)(code + head + 5u));
    memcpy(out + head + 1u, &rel, 4);
    return head + 5u;
}

/* The grid is the 32 x 32 one and its block holds it: the 12-byte array behind the tiles' list ends before the back
 * regions ([W+0x6758] + 12 x [W+0x6750] <= [W+0x78]). */
static int Fits(uintptr_t world)
{
    uint16_t grid[2] = {0, 0};
    uintptr_t array = 0, back = 0;
    uint32_t count = 0;
    return g_coApi->read(world + 0x6710, grid, 4) == 0 && grid[0] == CO_NAV_CELLS && grid[1] == CO_NAV_CELLS &&
        g_coApi->read(world + 0x6758, &array, 8) == 0 && g_coApi->read(world + 0x6750, &count, 4) == 0 && g_coApi->read(world + 0x78, &back, 8) == 0 &&
        array != 0 && back != 0 && array + 12u * (uintptr_t)count <= back;
}

void co_nav_retile(uint32_t id, uintptr_t level)
{
    const uintptr_t exe = g_nav.exe;
    uintptr_t worlds = 0, world = 0;
    uint32_t count = 0;
    float disk = 0.0f, radius = 0.0f;
    CoNavPlan plan;
    if (level == 0 && g_nav.ship != NULL) g_nav.ship();
    /* any other grid is HellForge's to retile, in its own step right behind this one */
    if (exe == 0 || g_coApi->read(exe + CO_NAV_COUNT, &count, 4) != 0 || id >= count || g_coApi->read(exe + CO_NAV_WORLDS, &worlds, 8) != 0 || worlds == 0 ||
        g_coApi->read(worlds + 8u * (uintptr_t)id, &world, 8) != 0 || world == 0 || !Fits(world)) return;
    if (level != 0 && (g_coApi->read(level + 0x2D7C, &disk, 4) != 0 || g_coApi->read(level + 0x2D8C, &radius, 4) != 0)) return;
    co_nav_plan(disk, radius, &plan);
    uint8_t read[4];
    if (plan.tile == 128.0f) memcpy(read, kNavTileRead + 4, 4);
    else if (plan.tile == 256.0f) memcpy(read, kNavTileRead256, 4);
    else
    {
        if (g_nav.tile == NULL) return;
        *g_nav.tile = plan.tile;
        const int32_t rel = (int32_t)((int64_t)(uintptr_t)g_nav.tile - (int64_t)(exe + CO_NAV_TILE_READ + 8u));
        memcpy(read, &rel, 4);
    }
    /* the terrain build's tile size first: tiles larger than it sizes its rows for are written past them */
    if (g_coApi->write(exe + CO_NAV_TILE_READ + 4u, read, 4) != 0) return;
    const float low[2] = {plan.low, plan.low}, high[2] = {plan.high, plan.high}, tiles[5] = {plan.tile, plan.low, plan.low, plan.high, plan.high};
    if (g_coApi->write(world + 0x08, low, sizeof low) != 0 || g_coApi->write(world + 0x14, high, sizeof high) != 0) return;
    g_coApi->write(world + 0x6718, tiles, sizeof tiles);
}

static int Point(uintptr_t site, uintptr_t to)
{
    const int32_t rel = (int32_t)((int64_t)to - (int64_t)(site + 5u));
    return g_coApi->write(site + 1u, &rel, 4);
}

/* The game's instructions at a site become a jump of HellForge's form: E9 and nops over the whole of them. */
static int Take(uintptr_t site, size_t length, uintptr_t to)
{
    uint8_t jump[15];
    const int32_t rel = (int32_t)((int64_t)to - (int64_t)(site + 5u));
    memset(jump, 0x90, sizeof jump); jump[0] = 0xE9; memcpy(jump + 1, &rel, 4);
    return g_coApi->write(site, jump, length);
}

const char* co_nav_install(uintptr_t game)
{
    const char* why;
    uintptr_t stubs[2];
    DWORD old;
    if (g_nav.page != NULL) return NULL;
    /* a game with no step of HellForge's at the sites (no larger level 11) gets the mod's alone, whole */
    const int plain = co_nav_plain(game);
    if (!plain && (why = co_nav_check(game, stubs)) != NULL) return why;
    const uintptr_t exe = g_coApi->module_base("helldivers2.exe", NULL);
    uint8_t* page = (uint8_t*)co_alloc_near(game, 0x1000);
    float* tile = (float*)co_alloc_near(exe, 0x1000);
    if (page == NULL || tile == NULL) return "no memory near the game for the navigation grid's steps";
    if (plain)
    {
        co_nav_whole(page, (uintptr_t)page, 0, (uintptr_t)co_nav_retile, game);
        co_nav_whole(page + 0x100, (uintptr_t)page + 0x100, 1, (uintptr_t)co_nav_retile, game);
    }
    else
    {
        co_nav_stub(page, (uintptr_t)page, 0, (uintptr_t)co_nav_retile, stubs[0]);
        co_nav_stub(page + 0x100, (uintptr_t)page + 0x100, 1, (uintptr_t)co_nav_retile, stubs[1]);
    }
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) return "the navigation grid's steps could not be made to run";
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    g_nav.game = game; g_nav.exe = exe; g_nav.tile = tile;
    if (plain)
    {
        if (Take(game + CO_NAV_SITE, sizeof kNavGame, (uintptr_t)page) == 0)
        {
            if (Take(game + CO_NAV_SHIP_SITE, sizeof kNavShipGame, (uintptr_t)page + 0x100) == 0) { g_nav.page = page; return NULL; }
            g_coApi->write(game + CO_NAV_SITE, kNavGame, sizeof kNavGame);
        }
        g_nav.exe = 0; g_nav.tile = NULL;
        return "the navigation grid's steps could not be written";
    }
    if (Point(game + CO_NAV_SITE, (uintptr_t)page) == 0)
    {
        if (Point(game + CO_NAV_SHIP_SITE, (uintptr_t)page + 0x100) == 0) { g_nav.page = page; return NULL; }
        Point(game + CO_NAV_SITE, stubs[0]);
    }
    g_nav.exe = 0; g_nav.tile = NULL;
    return "difficulty 11's navigation steps could not be chained";
}

uintptr_t co_nav_exe(void) { return g_nav.page != NULL ? g_nav.exe : 0; }
const float* co_nav_world_tile(void) { return g_nav.page != NULL ? g_nav.tile : NULL; }

#ifdef CO_TEST
void co_nav_test_reset(void) { memset(&g_nav, 0, sizeof g_nav); }
uintptr_t co_nav_test_page(void) { return (uintptr_t)g_nav.page; }
const float* co_nav_test_tile(void) { return g_nav.tile; }
#endif
