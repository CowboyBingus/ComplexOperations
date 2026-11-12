/* The navigation grid per level (co_nav.h). */
#include <string.h>

#include <windows.h>

#include "co_nav.h"
#include "co_nav_sites.h"
#include "co_navtrim.h"
#include "co_normal.h"

static struct
{
    uintptr_t game, exe;
    uint8_t* page;
    float* tile;          /* near the exe: a tile size the engine has no constant for */
    void (*ship)(void);
    /* The ship's grid is set in the PrepareShip enter, before the ship's carvers are filed under the old one; the
     * Ship enter's step then skips the two retiles only for the id this token holds (id + 1), which every other
     * retile clears. */
    uint64_t (*prepare)(void* state, uintptr_t game);
    uintptr_t hfShip;     /* HellForge's ship stub, whose pinned function is the second step; 0 without one */
    volatile LONG token, early;
} g_nav;

void co_nav_ship(void (*ship)(void)) { g_nav.ship = ship; }

/* Taken before the game's large allocations can fill the space within reach of the two images (co_nav_reserve). */
static struct { uint8_t* page; float* tile; } g_reserve;

void co_nav_reserve(uintptr_t game, uintptr_t exe)
{
    if (exe != 0 && g_reserve.tile == NULL && g_nav.tile == NULL && (g_reserve.tile = (float*)co_alloc_near(exe, 0x1000)) == NULL)
        co_log("navigation: no page free within reach of helldivers2.exe at 0x%llx as the mod loaded", (unsigned long long)exe);
    if (game != 0 && g_reserve.page == NULL && g_nav.page == NULL && (g_reserve.page = (uint8_t*)co_alloc_near(game, 0x1000)) == NULL)
        co_log("navigation: no page free within reach of game.dll at 0x%llx as it loaded", (unsigned long long)game);
    co_navtrim_reserve(exe);
}

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
    if (!ship)
    {
        memcpy(out, kNavStock, 124);
        memcpy(out + 60, &retile, 8);
        out[124] = 0xE9;
        const int32_t rel = (int32_t)((int64_t)stock - (int64_t)(code + 129u));
        memcpy(out + 125, &rel, 4);
        return 129;
    }
    /* The ship's: a non-zero return skips HellForge's retile, going on at its stub's own call to the game (+129).
     * Only flag-free instructions (movdqu, lea, pop) lie between the test and the branch. */
    static const uint8_t test[2] = {0x84, 0xC0}, lea[8] = {0x48, 0x8D, 0xA4, 0x24, 0x88, 0x00, 0x00, 0x00};
    memcpy(out, kNavShipStock, 75);
    memcpy(out + 65, &retile, 8);
    memcpy(out + 75, test, 2);
    memcpy(out + 77, kNavShipStock + 75, 36);
    memcpy(out + 113, lea, 8);
    memcpy(out + 121, kNavShipStock + 118, 11);
    const int32_t skip = (int32_t)((int64_t)(stock + 129u) - (int64_t)(code + 138u)), on = (int32_t)((int64_t)stock - (int64_t)(code + 143u));
    out[132] = 0x0F; out[133] = 0x85; memcpy(out + 134, &skip, 4);
    out[138] = 0xE9; memcpy(out + 139, &on, 4);
    return CO_NAV_SHIP_STUB;
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

/* One line for a world left with the whole grid on a level that should have stock's tiles (a 6b FAIL, not silent). */
static void NotArmed(uint32_t id, const char* why)
{
    co_log("navigation: nav world %u is not armed for stock's 8 x 8 tiles (%s); it builds all 32 x 32", id, why);
}

static void Retile(uint32_t id, uintptr_t level)
{
    const uintptr_t exe = g_nav.exe;
    uintptr_t worlds = 0, world = 0;
    uint32_t count = 0;
    uint16_t grid[2] = {0, 0};
    float disk = 0.0f, radius = 0.0f;
    CoNavPlan plan;
    co_navtrim_disarm(id); /* armed again below only for a normal level or the ship whose bounds were written */
    /* any other grid is HellForge's to retile, in its own step right behind this one */
    if (exe == 0 || g_coApi->read(exe + CO_NAV_COUNT, &count, 4) != 0 || id >= count || g_coApi->read(exe + CO_NAV_WORLDS, &worlds, 8) != 0 || worlds == 0 ||
        g_coApi->read(worlds + 8u * (uintptr_t)id, &world, 8) != 0 || world == 0 || g_coApi->read(world + 0x6710, grid, 4) != 0)
    { NotArmed(id, "its world could not be read"); return; }
    if (grid[0] != CO_NAV_CELLS || grid[1] != CO_NAV_CELLS) return; /* HellForge's 8 x 8: stock's tiles already */
    if (!Fits(world)) { NotArmed(id, "its block does not hold the grid's arrays"); return; }
    if (level != 0 && (g_coApi->read(level + 0x2D7C, &disk, 4) != 0 || g_coApi->read(level + 0x2D8C, &radius, 4) != 0)) { NotArmed(id, "the level could not be read"); return; }
    co_nav_plan(disk, radius, &plan);
    const int normal = plan.stockHigh > plan.stockLow;
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
    if (g_coApi->write(exe + CO_NAV_TILE_READ + 4u, read, 4) != 0) { if (normal) NotArmed(id, "the tile size could not be written"); return; }
    /* an armed world's own bounds (closest navmesh, 0x7B5F00) are stock's; the grid keeps the mod's for the tile boxes */
    const char* why = !normal ? NULL : co_navtrim_refused() != NULL ? co_navtrim_refused() : id >= CO_NAVTRIM_WORLDS ? "its nav id has no slot in the stubs" : NULL;
    const int armed = normal && why == NULL;
    const float low[2] = {armed ? plan.stockLow : plan.low, armed ? plan.stockLow : plan.low}, high[2] = {armed ? plan.stockHigh : plan.high, armed ? plan.stockHigh : plan.high};
    const float tiles[5] = {plan.tile, plan.low, plan.low, plan.high, plan.high};
    if (g_coApi->write(world + 0x6718, tiles, sizeof tiles) != 0 || g_coApi->write(world + 0x08, low, sizeof low) != 0 || g_coApi->write(world + 0x14, high, sizeof high) != 0)
    { if (normal) NotArmed(id, "its bounds could not be written"); return; }
    /* Arming refuses only without the stubs, for an id past 4 or a null world, all ruled out above. Were it to fail, the
     * world would keep stock's bounds on a grid that is not remapped; the line below names that level. */
    if (armed && co_navtrim_arm(id, world, plan.stockLow, plan.stockHigh) != 0) why = "the stubs refused it";
    if (why != NULL) NotArmed(id, why);
}

void co_nav_retile(uint32_t id, uintptr_t level)
{
    InterlockedExchange(&g_nav.token, 0);
    Retile(id, level);
}

int co_nav_ship_step(uint32_t id, uintptr_t level)
{
    if (g_nav.ship != NULL) g_nav.ship();
    if (InterlockedExchange(&g_nav.token, 0) == (LONG)id + 1) return 1;
    Retile(id, level);
    return 0;
}

#define CO_NAV_ID 0x10F0u /* the game object's main nav world id, set once at its creation (game 0xAB1B31) */

/* The world of `id`, or 0. */
static uintptr_t World(uint32_t id)
{
    uintptr_t worlds = 0, world = 0;
    uint32_t count = 0;
    if (g_nav.exe == 0 || g_coApi->read(g_nav.exe + CO_NAV_COUNT, &count, 4) != 0 || id >= count ||
        g_coApi->read(g_nav.exe + CO_NAV_WORLDS, &worlds, 8) != 0 || worlds == 0 || g_coApi->read(worlds + 8u * (uintptr_t)id, &world, 8) != 0) return 0;
    return world;
}

/* Live carvers, walking the free list from its head as the insert pops it: slot 0 is never handed out, so it is neither
 * free nor live. -1 when the list does not end at -1 within the slots. */
static int64_t Live(uintptr_t world, uint32_t* slots)
{
    const uintptr_t grid = world + 0x6710u;
    uintptr_t pool = 0;
    uint32_t n = 0, spare = 0;
    uint16_t stride = 0;
    int32_t at = -1;
    if (g_coApi->read(grid + 0x1C, &n, 4) != 0 || g_coApi->read(grid + 0x24, &stride, 2) != 0 || g_coApi->read(grid + 0x2C, &at, 4) != 0 ||
        g_coApi->read(grid + 0x30, &pool, 8) != 0 || pool == 0 || n == 0) return -1;
    *slots = n;
    for (; at != -1; spare++)
        if (at <= 0 || (uint32_t)at >= n || spare >= n - 1u || g_coApi->read(pool + (uintptr_t)stride * (uint32_t)at, &at, 4) != 0) return -1;
    return (int64_t)n - 1 - spare;
}

/* The ship's two retiles, before anything of the ship is filed. A world still building (bit 1 of +0x20, set by the
 * last level's update until the leave request clears it) keeps the old order: retiling it here could meet a tile build. */
static void Early(uintptr_t game)
{
    uint32_t id = 0, flags = 0, slots = 0;
    InterlockedExchange(&g_nav.token, 0);
    if (!g_nav.early) return;
    const uintptr_t world = g_coApi->read(game + CO_NAV_ID, &id, 4) == 0 ? World(id) : 0;
    if (world == 0 || g_coApi->read(world + 0x20, &flags, 4) != 0)
    { co_log("navigation: early ship retile skipped: nav world %u could not be read; the ship's set-up retiles it", id); return; }
    if (flags & 2u)
    { co_log("navigation: early ship retile skipped: nav world %u is building (flags 0x%X); the ship's set-up retiles it", id, flags); return; }
    if (g_nav.hfShip != 0 && !Stock(g_nav.game, g_nav.hfShip, 1))
    {
        InterlockedExchange(&g_nav.early, 0);
        co_log("navigation: early ship retile off: HellForge's ship step is not the pinned form; the ship's set-up retiles as before");
        return;
    }
    const int64_t live = Live(world, &slots);
    if (live < 0) co_log("navigation: early ship retile: nav world %u, the carver free list does not end", id);
    else co_log("navigation: early ship retile: nav world %u, %lld of %u carver slots live", id, (long long)live, slots);
    Retile(id, 0);
    if (g_nav.hfShip != 0)
    {
        void (*second)(uint32_t, uintptr_t) = NULL;
        g_coApi->read(g_nav.hfShip + 65u, &second, 8); /* pinned as HellForge's own call by Stock() */
        if (second != NULL) second(id, 0);
    }
    InterlockedExchange(&g_nav.token, (LONG)id + 1);
}

static uint64_t Prepare(void* state, uintptr_t game)
{
    Early(game);
    return g_nav.prepare(state, game);
}

/* Off, with one line, when any pin fails: the Ship enter's step then retiles as it always did. */
static void EarlyInstall(uintptr_t game, uintptr_t exe, uintptr_t hfShip)
{
    const char* why = NULL;
    if (!g_coApi->code_matches(game + CO_NAV_PREPARE, CO_NAV_PREPARE_PIN) || !g_coApi->code_matches(game + CO_NAV_PREPARE_CALL, CO_NAV_PREPARE_CALL_PIN) ||
        !g_coApi->code_matches(game + CO_NAV_SHIP_CALL, CO_NAV_SHIP_CALL_PIN) || !g_coApi->code_matches(game + CO_NAV_SHIP_KEEP, CO_NAV_SHIP_KEEP_PIN) ||
        !g_coApi->code_matches(exe + CO_NAV_LEAVE, CO_NAV_LEAVE_PIN) || !g_coApi->code_matches(exe + CO_NAV_SCHED, CO_NAV_SCHED_PIN))
        why = "the PrepareShip enter or the nav world's flags are not the code this mod was made for";
    else if (hfShip != 0 && !Stock(game, hfShip, 1)) why = "HellForge's ship step is not the pinned form";
    else if (g_coApi->hook(g_coMod, game + CO_NAV_PREPARE, (void*)Prepare, (void**)&g_nav.prepare) != 0 || g_nav.prepare == NULL)
        why = "the PrepareShip enter could not be hooked";
    if (why != NULL) { g_nav.prepare = NULL; co_log("navigation: early ship retile off: %s; the ship's set-up retiles as before", why); return; }
    g_nav.hfShip = hfShip;
    InterlockedExchange(&g_nav.early, 1);
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

/* Stock's tiles for normal levels and the ship; refused, every level keeps the whole grid and each says so as it loads. */
static void Trim(uintptr_t exe)
{
    const char* why = co_navtrim_install(exe);
    if (why != NULL) co_log("navigation: %s; normal levels build the whole 32 x 32 grid", why);
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
    co_nav_reserve(game, exe);
    uint8_t* page = g_reserve.page;
    float* tile = g_reserve.tile;
    if (page == NULL) return "no memory near the game for the navigation grid's steps";
    if (tile == NULL) return "no memory near helldivers2.exe for the navigation grid's tile size";
    g_reserve.page = NULL; g_reserve.tile = NULL;
    if (plain)
    {
        co_nav_whole(page, (uintptr_t)page, 0, (uintptr_t)co_nav_retile, game);
        co_nav_whole(page + 0x100, (uintptr_t)page + 0x100, 1, (uintptr_t)co_nav_ship_step, game);
    }
    else
    {
        co_nav_stub(page, (uintptr_t)page, 0, (uintptr_t)co_nav_retile, stubs[0]);
        co_nav_stub(page + 0x100, (uintptr_t)page + 0x100, 1, (uintptr_t)co_nav_ship_step, stubs[1]);
    }
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) return "the navigation grid's steps could not be made to run";
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    g_nav.game = game; g_nav.exe = exe; g_nav.tile = tile;
    if (plain)
    {
        if (Take(game + CO_NAV_SITE, sizeof kNavGame, (uintptr_t)page) == 0)
        {
            if (Take(game + CO_NAV_SHIP_SITE, sizeof kNavShipGame, (uintptr_t)page + 0x100) == 0) { g_nav.page = page; Trim(exe); EarlyInstall(game, exe, 0); return NULL; }
            g_coApi->write(game + CO_NAV_SITE, kNavGame, sizeof kNavGame);
        }
        g_nav.exe = 0; g_nav.tile = NULL;
        return "the navigation grid's steps could not be written";
    }
    if (Point(game + CO_NAV_SITE, (uintptr_t)page) == 0)
    {
        if (Point(game + CO_NAV_SHIP_SITE, (uintptr_t)page + 0x100) == 0) { g_nav.page = page; Trim(exe); EarlyInstall(game, exe, stubs[1]); return NULL; }
        Point(game + CO_NAV_SITE, stubs[0]);
    }
    g_nav.exe = 0; g_nav.tile = NULL;
    return "difficulty 11's navigation steps could not be chained";
}

uintptr_t co_nav_exe(void) { return g_nav.page != NULL ? g_nav.exe : 0; }
const float* co_nav_world_tile(void) { return g_nav.page != NULL ? g_nav.tile : NULL; }

#ifdef CO_TEST
void co_nav_test_reset(void) { memset((void*)&g_nav, 0, sizeof g_nav); memset(&g_reserve, 0, sizeof g_reserve); co_navtrim_test_reset(); }
uintptr_t co_nav_test_page(void) { return (uintptr_t)g_nav.page; }
const uint8_t* co_nav_test_reserved(void) { return g_reserve.page; }
const float* co_nav_test_tile(void) { return g_nav.tile; }
#endif
