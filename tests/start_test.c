/* Whether the complex part is on, asked of a game: HellForge's chained step, the mod's own sites and the memory each
 * turn it off with their own line, and while it is off nothing of it is hooked or written. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_nav_sites.h"
#include "xl/co_nav.h"
#include "xl/co_navtrim.h"
#include "xl/co_normal.h"
#include "xl/co_rows.h"
#include "xl/co_pages.h"
#include "xl/co_page_sites.h"

void co_capacity_test_commit(uint32_t mb);
void co_early_test_reset(void);
void CoXlTestRecords(int state);
void CoXlTestRenderReset(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const uint8_t kFill[14] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
static const uint8_t kLaunch[11] = {0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};
static const uint8_t kStock[116] = {
    0x4C, 0x89, 0x2D, 0, 0, 0, 0, 0x49, 0x89, 0x95, 0xE0, 0x2D, 0x00, 0x00, 0x41, 0x80, 0xBD, 0xF9, 0x00, 0x00, 0x00, 11, 0x75, 0x57,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x41, 0xC7, 0x85, 0x78, 0x2D, 0x00, 0x00, 0, 0, 0, 0, 0x41, 0xC7, 0x85, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
    0x41, 0xC7, 0x85, 0x84, 0x2D, 0x00, 0x00, 0, 0, 0, 0, 0x41, 0xC7, 0x85, 0x88, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
    0x41, 0xC7, 0x85, 0x8C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
    0x31, 0xC0, 0x49, 0x89, 0x85, 0x94, 0x2D, 0x00, 0x00, 0x48, 0xB8, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F,
    0x49, 0x89, 0x85, 0x9C, 0x2D, 0x00, 0x00, 0xE9, 0, 0, 0, 0};
#define STOCK 0x3100C40u
#define RENDER_ENGINE_AT 0x3700000u /* a free place in the image for the engine object, when a case has one */

static int g_worldMade;       /* the next Start's main world is made before the game-loaded event */
static void (*g_beforeLoad)(void); /* and this runs just before that event, once */
static char g_loadLog[8192];  /* what Start's game-loaded event logged, the writes it asked for and their addresses */
static int g_loadWrites, g_loadWroteCount;
static uintptr_t g_loadWrote[64];

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at += strlen(text)) n++;
    return n;
}

/* A HellForge that gave everything, level 11 installed, and the game's bytes at every site of the part. */
static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u; c->moved = 1;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x1200000u + 0x10010u * i; c->regions[i].places = 100u; }
    g_fake.levelRow = 0x1000;
    uint8_t* image = g_fake.image;
    for (size_t i = 0; i < sizeof kCoWorldReaders / sizeof kCoWorldReaders[0]; i++) memcpy(image + kCoWorldReaders[i].rva, kCoWorldReaders[i].bytes, kCoWorldReaders[i].length);
    for (size_t i = 0; i < sizeof kCoPageReaders / sizeof kCoPageReaders[0]; i++) memcpy(image + kCoPageReaders[i].rva, kCoPageReaders[i].bytes, kCoPageReaders[i].length);
    for (size_t i = 0; i < sizeof kCoPageImmediates / sizeof kCoPageImmediates[0]; i++) memcpy(image + kCoPageImmediates[i].rva, kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u);
    memcpy(image + CO_HUD_LIST_SITE, kCoHudListBytes, sizeof kCoHudListBytes);
    memcpy(image + CO_HUD_FIND_SITE, kCoHudFindBytes, sizeof kCoHudFindBytes);
    memcpy(image + CO_HUD_FLAG_SITE, kCoHudFlagBytes, sizeof kCoHudFlagBytes);
    memcpy(image + CO_MARK_FILL, kFill, sizeof kFill);
    memcpy(image + CO_LAUNCH_STRING, kLaunch, sizeof kLaunch);
    memcpy(image + CO_RADII_BEHIND, kCoRadiiBehind, sizeof kCoRadiiBehind);
    const uint8_t jump[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    const int32_t to = (int32_t)(STOCK - (CO_RADII_SITE + 5u));
    const float disk = 1022.0f / 1024.0f;
    memcpy(image + CO_RADII_SITE, jump, 7); memcpy(image + CO_RADII_SITE + 1, &to, 4);
    memcpy(image + STOCK, kStock, sizeof kStock); memcpy(image + STOCK + 48, &disk, 4);
    fake_stock_chains(image, disk); /* its keyed generator steps, which the mod chains in front of */
    fake_stock_rows(image);         /* and its distance rows, behind the game's row readers */
    co_join_test_image(image, 4); co_drain_test_image(image);
    fake_stock_nav(image);          /* its navigation steps, and the engine they retile */
    fake_stock_early(image);        /* its pool sizes, in a game whose setup has not begun */
    co_capacity_test_commit(CO_COMPLEX_COMMIT_MB);
    co_capacity_init();
    co_capacity_game((uintptr_t)image);
    /* the engine's room goes in as the game loads, whatever is decided later: not what the cases below count */
    co_early_test_reset(); CoXlTestRecords(0); CoXlTestRenderReset();
    if (g_worldMade)
    {
        const uint64_t engine = (uint64_t)(uintptr_t)(image + RENDER_ENGINE_AT), world = 0x123456780ull;
        memcpy(image + 0x3326340u, &engine, 8); memcpy(image + RENDER_ENGINE_AT + 0x10E8u, &world, 8);
        g_worldMade = 0;
    }
    if (g_beforeLoad != NULL) { g_beforeLoad(); g_beforeLoad = NULL; }
    co_capacity_early();
    strcpy(g_loadLog, g_fake.log);
    g_loadWrites = g_fake.writes; g_loadWroteCount = g_fake.wroteCount; memcpy(g_loadWrote, g_fake.wrote, sizeof g_loadWrote);
    g_fake.failWriteAt = -1;
    g_fake.writes = 0; g_fake.hookCount = 0; g_fake.log[0] = 0;
}

static int Untouched(void) { return g_fake.writes == 0 && g_fake.hookCount == 0; }
static int InGame(uintptr_t at) { return at >= (uintptr_t)g_fake.image && at < (uintptr_t)g_fake.image + FAKE_IMAGE_SIZE; }
static void SceneNotReviewed(void) { g_fake.noMatchExe[0] = FAKE_RENDER_SCENE; }
static void FirstJumpRefused(void) { g_fake.failWriteAt = 18; } /* after the room's 18 writes: the scene jump */

int main(void)
{
    Start("everything there");
    CHECK(co_capacity_poll(1) == 1);
    const CoComplexNeeds* n = co_capacity_needs();
    CHECK(n->stock_stubs && n->own_sites && n->commit_free_mb == CO_COMPLEX_COMMIT_MB && co_complex_refusal(n) == NULL);
    /* the part is on, with no line, and nothing has gone in before its start (the last case below) */
    CHECK(CO_COMPLEX_COMPLETE == 1 && co_complex_on() && Untouched());
    CHECK(Lines("complex operations are off") == 0 && co_complex_rolls(1, co_complex_on(), CO_HOST_ROW_NONE));
    /* the main world's pools went in after the engine's room (its 18 writes: the forest's 2 in the exe, the rest in
     * game.dll) and before the setup */
    CHECK(co_capacity_needs()->early && co_capacity_needs()->render && strstr(g_loadLog, "render capacity: the main world (capacity 0x200000) is made at 2x") != NULL);
    CHECK(strstr(g_loadLog, "is made at 2x") < strstr(g_loadLog, "the engine has room for a world"));
    /* the streamed layers are off by default: said where their count would go in (before the engine's room), no hook */
    CHECK(strstr(g_loadLog, "streamed broad phase layers: off by default (HELLFORGE_XL_STREAM_LAYERS=1 turns them on)") != NULL);
    CHECK(strstr(g_loadLog, "streamed broad phase layers: off by default") < strstr(g_loadLog, "is made at 2x"));
    CHECK(g_fake.exe[FAKE_RENDER_CAP] == 0xE9 && g_fake.exe[FAKE_RENDER_POOL0] == 0xE9 && g_fake.exe[FAKE_RENDER_SCENE] == 0xE9);
    CHECK(g_loadWroteCount >= 21 && g_loadWrote[18] == (uintptr_t)g_fake.exe + FAKE_RENDER_SCENE &&
        g_loadWrote[19] == (uintptr_t)g_fake.exe + FAKE_RENDER_POOL0 && g_loadWrote[20] == (uintptr_t)g_fake.exe + FAKE_RENDER_CAP);
    for (int i = 0; i < 18; i++) CHECK(InGame(g_loadWrote[i]) == (i != 10 && i != 11));
    CHECK(g_loadWrote[10] == (uintptr_t)g_fake.exe + 0x20ADA4u && g_loadWrote[11] == (uintptr_t)g_fake.exe + 0x20AE22u);

    /* A check that refuses comes before any write: the engine's room, the navigation and the exe stay the game's. */
    g_worldMade = 1;
    Start("the main world was made before the game-loaded event");
    CHECK(strstr(g_loadLog, "the main world was made before the mod could size it") != NULL && strstr(g_loadLog, "is made at 2x") == NULL);
    CHECK(g_loadWrites == 0 && g_loadWroteCount == 0 && !co_early_in());
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->early && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("the main world was made before its culling pools could be enlarged"));
    CHECK(co_capacity_normal() == 1 && Untouched()); /* no room: the grid is HellForge's */

    g_beforeLoad = SceneNotReviewed;
    Start("the main world's scene store is not the reviewed code");
    CHECK(g_loadWrites == 0 && g_loadWroteCount == 0 && !co_early_in() && strstr(g_loadLog, "is not the reviewed code") != NULL);
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("the main world's scene store is not the reviewed code"));

    /* A jump refused after the room: the exe is the game's again, the room and its navigation plan stay, the part is off. */
    g_beforeLoad = FirstJumpRefused;
    Start("the main world's first jump is refused");
    CHECK(co_early_in() && co_capacity_needs()->early && !co_capacity_needs()->render && strstr(g_loadLog, "the sites written are the game's again") != NULL);
    CHECK(g_fake.exe[FAKE_RENDER_CAP] == 0x8B && g_fake.exe[FAKE_RENDER_SCENE] == 0x49 && g_fake.exe[FAKE_RENDER_POOL0] == 0x89);
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 &&
        fake_host_logged("the main world's culling pools were not enlarged as the game started (the main world's creation could not be patched)"));
    CHECK(co_capacity_normal() == 1 && g_fake.writes == 4 && g_fake.hookCount == 1 && g_fake.hooks[0].target == CO_NAV_PREPARE);
    co_nav_test_reset(); /* a process installs the navigation once: the cases below make their own */

    Start("HellForge's radii step is another");
    g_fake.image[STOCK + 21] = 10;
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("larger-map steps are not the ones this mod was built for"));
    Start("difficulty 11 with another map scale");
    const float other = 800.0f / 1024.0f;
    memcpy(g_fake.image + STOCK + 48, &other, 4);
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && fake_host_logged("larger-map steps"));
    Start("a generator step of difficulty 11 is another");
    g_fake.image[fake_stock_stub(4) + 9] ^= 1;
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("larger-map steps are not the ones this mod was built for"));
    Start("a navigation step of difficulty 11 is another");
    g_fake.image[FAKE_STOCK_NAV + 0x100u + 56u] ^= 1;
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && !co_complex_on() && co_complex_start() == 0 && Untouched());
    Start("no radii step");
    memset(g_fake.image + CO_RADII_SITE, 0x90, 7);
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && !co_complex_on());

    const uint32_t sites[] = {kCoWorldReaders[7].rva, kCoPageReaders[3].rva + 1u, kCoPageImmediates[5].rva, CO_HUD_FIND_SITE + 20u, CO_MARK_FILL + 3u, CO_LAUNCH_STRING + 9u,
        CO_ROW_OUTPOSTS_SITE + 2u, CO_ROW_EMPTY_SITE + 7u, 0xF7D9B6u + 6u, 0x108C878u + 3u, 0xF73F37u + 2u};
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++)
    {
        Start("one of the mod's sites is another game's");
        g_fake.image[sites[i]] ^= 0x20;
        CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->stock_stubs && !co_capacity_needs()->own_sites && !co_complex_on() && co_complex_start() == 0 && Untouched());
        CHECK(Lines("complex operations are off") == 1 && fake_host_logged("the game's code is not the build this mod was made for") && fake_host_logged("this mod was made for ("));
    }

    Start("too little memory");
    co_capacity_test_commit(CO_COMPLEX_COMMIT_MB - 1u);
    CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->own_sites && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("not enough memory free") && fake_host_logged("28671 MB can be committed, 28672 are needed"));

    Start("the machine's own memory");
    co_capacity_test_commit(0); /* asked of Windows: the part is on exactly when this machine has the commit */
    CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->commit_free_mb != 0 && Untouched() &&
          co_complex_on() == (co_capacity_needs()->commit_free_mb >= CO_COMPLEX_COMMIT_MB));

    Start("no level 11");
    g_fake.levelRow = 0;
    CHECK(co_capacity_poll(1) == 0 && co_capacity_poll(1 + CO_GEN_WAIT_MS) == 1 && !co_capacity_needs()->stock_stubs && !co_capacity_needs()->own_sites);
    CHECK(fake_host_logged("difficulty 11 is not installed") && co_complex_start() == 0 && Untouched()); /* the game is not even asked */

    Start("the engine's room was not made as the game loaded");
    co_capacity_init();
    co_capacity_game((uintptr_t)g_fake.image); /* asked anew, and nothing put in */
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->early && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("were not enlarged as the game started"));
    CHECK(co_capacity_normal() == 1 && Untouched()); /* the grid is HellForge's 8 x 8: nothing to plan */

    Start("the entity records could not be moved");
    CoXlTestRecords(-1);
    CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->early && !co_capacity_needs()->records && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("the entity records kept the game's 2048 places"));

    /* the engine's room is in and the part is off: every level still gets its navigation plan, in front of HellForge's */
    Start("a normal level's navigation with the room made");
    co_capacity_test_commit(CO_COMPLEX_COMMIT_MB - 1u);
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && Untouched());
    /* 2 navigation stubs, then the 2 tile stubs that give a normal level stock's 8 x 8 tiles; one hook, the ship's
     * early retile in the PrepareShip enter */
    CHECK(co_capacity_normal() == 1 && g_fake.writes == 4 && co_navtrim_refused() == NULL && g_fake.hookCount == 1 &&
        g_fake.hooks[0].target == CO_NAV_PREPARE && co_complex_start() == 0 && g_fake.writes == 4);

    /* The start itself, last: its installs stay in. This image has no mission tables, so the world's settings refuse
     * and the part is off again with its line. */
    Start("the start on an image without the game's missions");
    CHECK(co_capacity_poll(1) == 1 && co_complex_on() && co_complex_start() == 0 && !co_complex_on());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("the missions difficulty 11 mixes"));
    CHECK(!co_complex_rolls(1, co_complex_on(), CO_HOST_ROW_NONE));

    if (g_failed) { printf("start: %d checks failed\n", g_failed); return 1; }
    printf("start: all cases passed\n");
    return 0;
}
