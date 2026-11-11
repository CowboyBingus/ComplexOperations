/* What the extra large world's readers get where the loader's own state stood: the navigation grid's engine and the
 * tile size of a complex level from the mod's own step, and the HUD rows' changing texts behind the engine's lookup,
 * which answers every other id as before and is left alone when it is another's code or has a text at the rows' ids. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_nav.h"
#include "xl/co_nav_sites.h"
#include "xl/co_normal.h"
#include "xl/co_text.h"
#include "xl/co_text_sites.h"

void co_text_test_reset(void);
void co_text_test_original(void* original);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define LEVEL 0x1500000u
#define WORLDS 0x100000u /* in the exe: the pointers, then world 0 at +0x1000 */
#define WORLD (WORLDS + 0x1000u)
#define FIRST 0x6F424800u
#define KNOWN 0x1234ABCDu
#define NAME 0x322000u    /* in the exe: a text the engine has, and its empty one */
#define EMPTY 0x322040u

static uint8_t* g_exe;
static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_nav_test_reset(); co_text_test_reset();
    fake_stock_nav(g_fake.image);
    g_exe = g_fake.exe;
    return g_fake.image;
}

/* Nav world 0 with the grid of 32 x 32 and room in its block. */
static void World(void)
{
    const uint64_t worlds = (uint64_t)(uintptr_t)(g_exe + WORLDS), world = (uint64_t)(uintptr_t)(g_exe + WORLD);
    const uint32_t count = 1, entries = 1024;
    const uint16_t grid[2] = {CO_NAV_CELLS, CO_NAV_CELLS};
    const uint64_t array = world + 0x10000u, back = array + 12u * (entries + 100u);
    memcpy(g_exe + CO_NAV_WORLDS, &worlds, 8); memcpy(g_exe + CO_NAV_COUNT, &count, 4); memcpy(g_exe + WORLDS, &world, 8);
    memcpy(g_exe + WORLD + 0x6710, grid, 4); memcpy(g_exe + WORLD + 0x6758, &array, 8); memcpy(g_exe + WORLD + 0x6750, &entries, 4);
    memcpy(g_exe + WORLD + 0x78, &back, 8);
}
static uintptr_t Level(float disk, float radius)
{
    memcpy(g_fake.image + LEVEL + 0x2D7C, &disk, 4); memcpy(g_fake.image + LEVEL + 0x2D8C, &radius, 4);
    return (uintptr_t)g_fake.image + LEVEL;
}
/* The living world's question (proto/living_world.c, LwNavWorld): the nav world's tiles are the extra large world's. */
static int WorldTiles(void)
{
    float tile;
    memcpy(&tile, g_exe + WORLD + 0x6718, 4);
    return co_nav_world_tile() != NULL && fabsf(tile - *co_nav_world_tile()) <= 1.0f;
}

/* The engine's lookup in the fake exe: the pinned entry and prologue, then KNOWN's text or the empty one, and out. */
static void Engine(void)
{
    static const uint8_t body[] = {0x81, 0xF9, 0, 0, 0, 0, 0x48, 0x8D, 0x05, 0, 0, 0, 0, 0x48, 0x8D, 0x15, 0, 0, 0, 0, 0x48, 0x0F, 0x44, 0xC2,
        0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0xC3};
    const uint32_t at = CO_TEXT_LOOKUP + sizeof kCoTextLookup, known = KNOWN;
    int32_t rel;
    DWORD old;
    memcpy(g_exe + CO_TEXT_ENTRY, kCoTextEntry, sizeof kCoTextEntry);
    memcpy(g_exe + CO_TEXT_LOOKUP, kCoTextLookup, sizeof kCoTextLookup);
    memcpy(g_exe + at, body, sizeof body);
    memcpy(g_exe + at + 2, &known, 4);
    rel = (int32_t)(EMPTY - (at + 13u)); memcpy(g_exe + at + 9, &rel, 4);
    rel = (int32_t)(NAME - (at + 20u)); memcpy(g_exe + at + 16, &rel, 4);
    strcpy((char*)g_exe + NAME, "LAUNCH ICBM");
    VirtualProtect(g_exe, FAKE_EXE_SIZE, PAGE_EXECUTE_READWRITE, &old);
}

static int g_asked;
static const char* Rows(uint32_t id) { g_asked++; return id == FIRST + 5u ? "LAUNCH ICBM  1/3" : NULL; }
typedef const char* (*LookupFn)(uint64_t id, uint64_t language, uint64_t r8, uint64_t r9);

int main(void)
{
    uint8_t* image = Reset("the navigation grid's readers");
    const uintptr_t game = (uintptr_t)image;
    const float hellforge = 1022.0f / 1024.0f;
    World();
    CHECK(co_nav_exe() == 0 && co_nav_world_tile() == NULL && !WorldTiles()); /* before the step is in: no reader goes on */
    CHECK(co_nav_install(game) == NULL && co_nav_exe() == (uintptr_t)g_exe && co_nav_world_tile() != NULL && *co_nav_world_tile() == 0.0f);
    co_nav_retile(0, Level(hellforge, 1023.0f));
    CHECK(!WorldTiles()); /* HellForge's level 11: tiles of 256 m */
    co_nav_retile(0, Level(co_disk_key(), 8074.4f));
    CHECK(*co_nav_world_tile() == 512.0f && WorldTiles()); /* a complex level's */
    co_nav_retile(0, Level(hellforge, 1023.0f));
    CHECK(*co_nav_world_tile() == 512.0f && !WorldTiles()); /* a normal level after it */
    co_nav_retile(0, 0);
    CHECK(!WorldTiles()); /* the ship */

    image = Reset("the engine's text lookup");
    Engine();
    const uintptr_t exe = (uintptr_t)g_exe;
    const uintptr_t target = exe + CO_TEXT_LOOKUP - (uintptr_t)image;
    CHECK(co_text_engine() == NULL && co_text_check(exe) == NULL && g_fake.writes == 0 && g_fake.hookCount == 0);
    CHECK(co_text_install(exe, FIRST, 64) == NULL && g_fake.hookCount == 1 && g_fake.hooks[0].target == target && g_fake.writes == 0);
    co_text_test_original(g_exe + CO_TEXT_LOOKUP); /* the harness hooks nothing: the way on is the engine's lookup */
    {
        const LookupFn lookup = (LookupFn)g_fake.hooks[0].detour;
        const CoEngineTextFn engine = co_text_engine();
        CHECK(engine != NULL && strcmp(lookup(KNOWN, 0, 0, 0), "LAUNCH ICBM") == 0 && lookup(FIRST + 5u, 0, 0, 0)[0] == 0);
        *co_text_dynamic() = Rows;
        CHECK(strcmp(lookup(FIRST + 5u, 0, 0, 0), "LAUNCH ICBM  1/3") == 0 && g_asked == 1);
        CHECK(lookup(FIRST + 6u, 0, 0, 0)[0] == 0 && g_asked == 2); /* not shown now: the engine's answer */
        /* every other id is the engine's and the rows are not asked: the ids around theirs, a text of the game's */
        CHECK(lookup(FIRST + 64u, 0, 0, 0)[0] == 0 && lookup(FIRST - 1u, 0, 0, 0)[0] == 0 && strcmp(lookup(KNOWN, 0, 0, 0), "LAUNCH ICBM") == 0 && g_asked == 2);
        /* the name a row's title is built from is the engine's own, whatever a caller leaves in the other registers */
        CHECK(engine != NULL && strcmp(engine(KNOWN, (void*)1, (void*)2, (void*)3), "LAUNCH ICBM") == 0 && engine(FIRST + 5u, NULL, NULL, NULL)[0] == 0 && g_asked == 2);
        *co_text_dynamic() = NULL;
        CHECK(lookup(FIRST + 5u, 0, 0, 0)[0] == 0);
    }
    CHECK(co_text_install(exe, FIRST, 64) == NULL && g_fake.hookCount == 1); /* once */

    const uint32_t other[] = {CO_TEXT_ENTRY, CO_TEXT_ENTRY + 4u, CO_TEXT_LOOKUP + 2u, CO_TEXT_LOOKUP + 27u};
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
    {
        image = Reset("the lookup is another's code");
        Engine();
        g_exe[other[i]] ^= 0x40;
        CHECK(co_text_check((uintptr_t)g_exe) != NULL && co_text_install((uintptr_t)g_exe, FIRST, 64) != NULL && g_fake.hookCount == 0 && co_text_engine() == NULL);
    }
    image = Reset("the engine has a text at one of the ids");
    Engine();
    CHECK(co_text_install((uintptr_t)g_exe, KNOWN - 3u, 8) != NULL && g_fake.hookCount == 0 && co_text_engine() == NULL);
    CHECK(co_text_install((uintptr_t)g_exe, KNOWN + 1u, 8) == NULL && g_fake.hookCount == 1);
    image = Reset("the hook is refused");
    Engine();
    g_fake.noHook = (uintptr_t)g_exe + CO_TEXT_LOOKUP - (uintptr_t)image;
    CHECK(co_text_install((uintptr_t)g_exe, FIRST, 64) != NULL && g_fake.hookCount == 0 && co_text_engine() == NULL && g_fake.writes == 0);
    image = Reset("no engine");
    CHECK(co_text_check(0) != NULL && co_text_install(0, FIRST, 64) != NULL && co_text_engine() == NULL);

    if (g_failed) { printf("standins: %d checks failed\n", g_failed); return 1; }
    printf("standins: all cases passed\n");
    return 0;
}
