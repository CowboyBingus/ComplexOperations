/* Whether the complex part is on, asked of a game: HellForge's chained step, the mod's own sites and the memory each
 * turn it off with their own line, and while it is off nothing of it is hooked or written. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_normal.h"
#include "xl/co_rows.h"
#include "xl/co_pages.h"
#include "xl/co_page_sites.h"

void co_capacity_test_commit(uint32_t mb);

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
    co_capacity_test_commit(CO_COMPLEX_COMMIT_MB);
    co_capacity_init();
    co_capacity_game((uintptr_t)image);
}

static int Untouched(void) { return g_fake.writes == 0 && g_fake.hookCount == 0; }

int main(void)
{
    Start("everything there");
    CHECK(co_capacity_poll(1) == 1);
    const CoComplexNeeds* n = co_capacity_needs();
    CHECK(n->stock_stubs && n->own_sites && n->commit_free_mb == CO_COMPLEX_COMMIT_MB && co_complex_refusal(n) == NULL);
    /* this build lacks parts of a complex operation: off all the same, with its own line, and nothing goes in */
    CHECK(CO_COMPLEX_COMPLETE == 0 && !co_complex_on() && co_complex_start() == 0 && Untouched());
    CHECK(Lines("complex operations are off") == 1 && fake_host_logged("makes none yet"));
    CHECK(co_peers_host_row() == CO_HOST_ROW_OFF && !co_complex_rolls(co_peers_is_host(), co_complex_on(), co_peers_host_row()));

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
    Start("no radii step");
    memset(g_fake.image + CO_RADII_SITE, 0x90, 7);
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->stock_stubs && !co_complex_on());

    const uint32_t sites[] = {kCoWorldReaders[7].rva, kCoPageReaders[3].rva + 1u, kCoPageImmediates[5].rva, CO_HUD_FIND_SITE + 20u, CO_MARK_FILL + 3u, CO_LAUNCH_STRING + 9u,
        CO_ROW_OUTPOSTS_SITE + 2u, 0xF7D9B6u + 6u, 0x108C878u + 3u, 0xF73F37u + 2u};
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
    co_capacity_test_commit(0); /* asked of Windows: any answer settles, and the part stays off in this build */
    CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->commit_free_mb != 0 && !co_complex_on() && Untouched());

    Start("no level 11");
    g_fake.levelRow = 0;
    CHECK(co_capacity_poll(1) == 0 && co_capacity_poll(1 + CO_GEN_WAIT_MS) == 1 && !co_capacity_needs()->stock_stubs && !co_capacity_needs()->own_sites);
    CHECK(fake_host_logged("difficulty 11 is not installed") && co_complex_start() == 0 && Untouched()); /* the game is not even asked */

    if (g_failed) { printf("start: %d checks failed\n", g_failed); return 1; }
    printf("start: all cases passed\n");
    return 0;
}
