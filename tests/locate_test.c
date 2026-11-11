/* The loot containers' location lookup: the game's instruction becomes a stub that reads the array's offset of the
 * mission in play (the game's own on a normal one, as HellForge leaves it), a site HellForge moved itself is left
 * alone, and anything else is refused with nothing written. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_locate.h"
#include "xl/co_locate_sites.h"

void co_locate_test_reset(void);
const uint8_t* co_locate_test_page(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define RUN 0x3300000u
#define MOVED 0x11A7280u

/* A game with the lookup's instruction; `moved`: HellForge's level 11 installed, array A at MOVED. */
static uint8_t* Reset(const char* name, int moved)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_locate_test_reset();
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u; c->moved = 1;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = MOVED + 0x1000000u * i; c->regions[i].places = 100u; }
    g_fake.levelRow = 0x1000;
    co_capacity_init();
    if (moved) co_capacity_poll(1);
    memcpy(g_fake.image + CO_LOCATE_SITE, kCoLocateGame, sizeof kCoLocateGame);
    g_fake.writes = 0;
    return g_fake.image;
}

/* The site as the leaf reaches it, with the block in r10; the leaf's next instruction is `mov rax, rcx; ret` here. */
static uint64_t Run(uint8_t* image, uint64_t block)
{
    static const uint8_t enter[] = {0x49, 0x89, 0xCA, 0xE9, 0, 0, 0, 0}, behind[] = {0x48, 0x89, 0xC8, 0xC3};
    const int32_t rel = (int32_t)(CO_LOCATE_SITE - (RUN + sizeof enter));
    memcpy(image + RUN, enter, sizeof enter); memcpy(image + RUN + 4, &rel, 4);
    memcpy(image + CO_LOCATE_BACK, behind, sizeof behind);
    DWORD old;
    if (!VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old)) return 0;
    return ((uint64_t (*)(uint64_t))(void*)(image + RUN))(block);
}
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

int main(void)
{
    const uint64_t block = 0x7FF612340000ull;
    uint8_t* image = Reset("the game's instruction beside HellForge's moved block", 1);
    uintptr_t game = (uintptr_t)image;
    uint8_t before[sizeof kCoLocateGame];
    CHECK(CO_LOCATE_BACK == CO_LOCATE_SITE + sizeof kCoLocateGame && co_gen_at(HF_GEN_LOCATIONS_A) == MOVED);
    CHECK(co_locate_at(0) == CO_LOCATE_GAME_AT && co_locate_at(1) == MOVED);
    CHECK(co_locate_check(game) == NULL && g_fake.writes == 0);
    CHECK(Run(image, block) == block + CO_LOCATE_GAME_AT); /* the game's own instruction, as the harness runs it */
    co_locate_live(1);
    CHECK(g_fake.writes == 0); /* nothing before the install */
    CHECK(co_locate_install(game) == NULL && g_fake.writes == 1 && g_fake.hookCount == 0);
    const uint8_t* page = co_locate_test_page();
    CHECK(page != NULL);
    if (page != NULL)
    {
        const uint32_t* at = (const uint32_t*)(page + 0x1000);
        CHECK(image[CO_LOCATE_SITE] == 0xE9 && Leads(image + CO_LOCATE_SITE + 1) == (uintptr_t)page && image[CO_LOCATE_SITE + 5] == 0x90 && image[CO_LOCATE_SITE + 6] == 0x90);
        CHECK(page[0] == 0x8B && page[1] == 0x0D && Leads(page + 2) == (uintptr_t)at && page[10] == 0xE9 && Leads(page + 11) == game + CO_LOCATE_BACK);
        /* a normal mission and the ship: the place HellForge's block has nothing at, as without the mod */
        CHECK(*at == CO_LOCATE_GAME_AT && Run(image, block) == block + CO_LOCATE_GAME_AT);
        co_locate_live(1);
        CHECK(*at == MOVED && Run(image, block) == block + MOVED);
        co_locate_live(0);
        CHECK(*at == CO_LOCATE_GAME_AT && Run(image, block) == block + CO_LOCATE_GAME_AT);
        /* rcx is the only register the stub writes: the leaf's eax, r8, r9 and rdx are live across it */
        static const uint8_t writes_rcx_only[] = {0x8B, 0x0D, 0x4A, 0x8D, 0x0C, 0x11};
        CHECK(memcmp(page, writes_rcx_only, 2) == 0 && memcmp(page + 6, writes_rcx_only + 2, 4) == 0);
    }
    CHECK(co_locate_install(game) == NULL && g_fake.writes == 1); /* once */

    image = Reset("HellForge moves the site itself", 1);
    const uint32_t moved = MOVED;
    memcpy(image + CO_LOCATE_SITE + 3, &moved, 4);
    memcpy(before, image + CO_LOCATE_SITE, sizeof before);
    CHECK(co_locate_check((uintptr_t)image) == NULL && co_locate_install((uintptr_t)image) == NULL && g_fake.writes == 0 && co_locate_test_page() == NULL);
    co_locate_live(1);
    CHECK(memcmp(before, image + CO_LOCATE_SITE, sizeof before) == 0 && Run(image, block) == block + MOVED);

    image = Reset("another offset at the site", 1);
    const uint32_t other = MOVED + 0xE0u;
    memcpy(image + CO_LOCATE_SITE + 3, &other, 4);
    CHECK(co_locate_check((uintptr_t)image) != NULL && co_locate_install((uintptr_t)image) != NULL && g_fake.writes == 0 && co_locate_test_page() == NULL);
    for (unsigned i = 0; i < sizeof kCoLocateGame; i++)
    {
        image = Reset("the site is another's code", 1);
        image[CO_LOCATE_SITE + i] ^= 0x10;
        memcpy(before, image + CO_LOCATE_SITE, sizeof before);
        CHECK(co_locate_check((uintptr_t)image) != NULL && co_locate_install((uintptr_t)image) != NULL && g_fake.writes == 0);
        CHECK(memcmp(before, image + CO_LOCATE_SITE, sizeof before) == 0 && co_locate_test_page() == NULL);
    }

    image = Reset("the write is refused", 1);
    g_fake.failWriteAt = 0;
    CHECK(co_locate_install((uintptr_t)image) != NULL && memcmp(image + CO_LOCATE_SITE, kCoLocateGame, sizeof kCoLocateGame) == 0 && co_locate_test_page() == NULL);
    co_locate_live(1);
    CHECK(Run(image, block) == block + CO_LOCATE_GAME_AT);

    /* no layout read yet (level 11 not installed): the game's bytes pass, a moved site cannot be told, a complex
     * mission would read the game's place */
    image = Reset("before the block's layout is known", 0);
    CHECK(co_gen_at(HF_GEN_LOCATIONS_A) == 0 && co_locate_at(1) == CO_LOCATE_GAME_AT && co_locate_check((uintptr_t)image) == NULL);
    memcpy(image + CO_LOCATE_SITE + 3, &moved, 4);
    CHECK(co_locate_check((uintptr_t)image) != NULL);

    if (g_failed) { printf("locate: %d checks failed\n", g_failed); return 1; }
    printf("locate: all cases passed\n");
    return 0;
}
