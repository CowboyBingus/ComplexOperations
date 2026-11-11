/* The stamp index per level: its memory enlarged as the game loads, its constructor's counts read from the mod once
 * HellForge's level 11 is in; a normal level's are HellForge's, and a site that is not HellForge's is refused. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_stamps.h"
#include "xl/co_stamp_sites.h"
#include "xl/co_normal.h"

void co_stamps_test_reset(void);
const uint8_t* co_stamps_test_page(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t g_before[0x1000];
#define SPAN 0xC04000u /* every site lies in 0xC04000..0xC05000 */

static uint8_t* Reset(const char* name, int room)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_stamps_test_reset();
    fake_stock_early(g_fake.image);
    if (room) co_stamps_room((uintptr_t)g_fake.image);
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    memcpy(g_before, g_fake.image + SPAN, sizeof g_before);
    g_fake.writes = 0;
    return g_fake.image;
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image + SPAN, sizeof g_before) == 0; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

int main(void)
{
    uint8_t* image = Reset("the room as the game loads", 0);
    uintptr_t game = (uintptr_t)image;
    CHECK(co_stamps_room_check(game) == NULL && g_fake.writes == 0);
    CHECK(co_stamps_check(game) != NULL); /* not before the room */
    CHECK(co_stamps_room(game) == NULL && g_fake.writes == 1);
    CHECK(memcmp(image + CO_STAMP_CELL_SITE, kCoStampCellGame, 2) == 0 && U32(image + CO_STAMP_CELL_SITE + 2) == CO_STAMP_CELL_BYTES);
    CHECK(co_stamps_room(game) == NULL && g_fake.writes == 1 && co_stamps_check(game) == NULL); /* once */
    /* the memory of every index, 128 cells whatever the add: both layouts fit, slots + 1 entries and the cells' bitsets */
    const uint32_t own = co_level_limit(CO_LIMIT_STAMP_SLOTS, 1), stock = co_level_limit(CO_LIMIT_STAMP_SLOTS, 0);
    CHECK(stock == CO_STAMP_STOCK_SLOTS && own == 131071u && CO_STAMP_OWN_QWORDS * 64u > own);
    CHECK((own + 1u) * 4u + CO_STAMP_CELLS * CO_STAMP_OWN_QWORDS * 8u <= CO_STAMP_CELLS * CO_STAMP_CELL_BYTES);
    CHECK((stock + 1u) * 4u + CO_STAMP_CELLS * CO_STAMP_STOCK_QWORDS * 8u <= CO_STAMP_CELLS * CO_STAMP_CELL_BYTES);

    image = Reset("the size is another", 0); game = (uintptr_t)image;
    image[CO_STAMP_CELL_SITE + 3] ^= 1;
    CHECK(co_stamps_room_check(game) != NULL && co_stamps_room(game) != NULL && g_fake.writes == 0);
    image = Reset("the room cannot be written", 0); game = (uintptr_t)image;
    g_fake.failWriteAt = 0;
    CHECK(co_stamps_room(game) != NULL && co_stamps_check(game) != NULL && Unchanged());
    CHECK(co_stamps_install(game) != NULL && co_stamps_test_page() == NULL && Unchanged());

    image = Reset("beside HellForge's level 11", 1); game = (uintptr_t)image;
    CHECK(co_stamps_slots() == CO_STAMP_STOCK_SLOTS && !co_stamps_complex());
    co_stamps_level(1);
    CHECK(co_stamps_slots() == CO_STAMP_STOCK_SLOTS && !co_stamps_complex()); /* nothing before the install */
    CHECK(co_stamps_check(game) == NULL && co_stamps_install(game) == NULL && g_fake.writes == 2);
    const uint8_t* page = co_stamps_test_page();
    CHECK(page != NULL);
    if (page != NULL)
    {
        const uint32_t* counts = (const uint32_t*)(page + 0x1000);
        /* the two jumps; HellForge's other writes and the instructions behind the second jump are as they were */
        CHECK(image[CO_STAMP_SLOTS_SITE] == 0xE9 && image[CO_STAMP_SLOTS_SITE + 5] == 0x90 && Leads(image + CO_STAMP_SLOTS_SITE + 1) == (uintptr_t)page);
        CHECK(image[CO_STAMP_LAYOUT_SITE] == 0xE9 && image[CO_STAMP_LAYOUT_SITE + 5] == 0x90 && image[CO_STAMP_LAYOUT_SITE + 6] == 0x90 &&
            Leads(image + CO_STAMP_LAYOUT_SITE + 1) == (uintptr_t)page + 0x40u);
        CHECK(memcmp(image + CO_STAMP_LAYOUT_SITE + 7, kCoStampLayoutStock + 7, sizeof kCoStampLayoutStock - 7) == 0);
        CHECK(memcmp(image + CO_STAMP_ADD_SITE, kCoStampAddStock, 5) == 0 && memcmp(image + CO_STAMP_CHECK_SITE, kCoStampCheckStock, 5) == 0);
        /* mov r13d, [slots]; back behind the game's mov r13d */
        CHECK(page[0] == 0x44 && page[1] == 0x8B && page[2] == 0x2D && Leads(page + 3) == (uintptr_t)counts);
        CHECK(page[7] == 0xE9 && Leads(page + 8) == game + CO_STAMP_SLOTS_BACK && CO_STAMP_SLOTS_BACK == CO_STAMP_SLOTS_SITE + sizeof kCoStampSlotsStock);
        /* the layout part: HellForge's instructions in its order, its three immediates read or derived */
        const uint8_t* s = page + 0x40;
        CHECK(s[0] == 0x8B && s[1] == 0x05 && Leads(s + 2) == (uintptr_t)counts);
        CHECK(memcmp(s + 11, kCoStampLayoutStock + 7, 14) == 0);              /* mov [rdi+0x38], rax; lea eax, [rdx+r8*4]; movss [rdi+8], xmm8 */
        CHECK(memcmp(s + 32, kCoStampLayoutStock + 33, 7) == 0);              /* mov dword [rdi+0x24], 4 */
        CHECK(memcmp(s + 48, kCoStampLayoutStock + 21, 5) == 0);              /* mov edx, 1: last, as the game leaves edx */
        CHECK(s[39] == 0x8B && s[40] == 0x15 && Leads(s + 41) == (uintptr_t)counts + 4u);
        CHECK(s[53] == 0xE9 && Leads(s + 54) == game + CO_STAMP_LAYOUT_BACK && CO_STAMP_LAYOUT_BACK == CO_STAMP_LAYOUT_SITE + sizeof kCoStampLayoutStock);
        /* a level that is not complex: the counts HellForge's own immediates held (lea +(slots + 1) x 4, +0x1C slots + 1, +0x28) */
        CHECK(counts[0] == U32(kCoStampSlotsStock + 2) && (counts[0] + 1u) * 4u == U32(kCoStampLayoutStock + 3) && counts[0] + 1u == U32(kCoStampLayoutStock + 29));
        CHECK(counts[1] == U32(kCoStampLayoutStock + 43) && !co_stamps_complex());
        co_stamps_level(1);
        CHECK(counts[0] == 131071u && counts[1] == 0x800u && co_stamps_complex() && co_stamps_slots() == 131071u);
        co_stamps_level(0);
        CHECK(counts[0] == CO_STAMP_STOCK_SLOTS && counts[1] == CO_STAMP_STOCK_QWORDS && !co_stamps_complex());
        MEMORY_BASIC_INFORMATION info;
        CHECK(VirtualQuery(page, &info, sizeof info) != 0 && info.Protect == PAGE_EXECUTE_READ);
    }
    const int writes = g_fake.writes;
    CHECK(co_stamps_install(game) == NULL && g_fake.writes == writes); /* once */

    /* the game's own index (no larger level 11), or anything else at one of HellForge's five sites or between them */
    const uint32_t sites[] = {CO_STAMP_ADD_SITE + 1, CO_STAMP_CHECK_SITE + 1, CO_STAMP_SLOTS_SITE + 2, CO_STAMP_LAYOUT_SITE + 3, CO_STAMP_LAYOUT_SITE + 12,
        CO_STAMP_LAYOUT_SITE + 29, CO_STAMP_LAYOUT_SITE + 43};
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++)
    {
        image = Reset("a site is not as HellForge leaves it", 1);
        image[sites[i]] ^= 0x04;
        memcpy(g_before, image + SPAN, sizeof g_before);
        CHECK(co_stamps_check((uintptr_t)image) != NULL && co_stamps_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0 && co_stamps_test_page() == NULL);
    }
    for (int fail = 0; fail < 2; fail++)
    {
        image = Reset("a jump cannot be written", 1);
        g_fake.failWriteAt = fail;
        CHECK(co_stamps_install((uintptr_t)image) != NULL && Unchanged() && co_stamps_test_page() == NULL && !co_stamps_complex());
        co_stamps_level(1);
        CHECK(co_stamps_slots() == CO_STAMP_STOCK_SLOTS);
    }

    if (g_failed) { printf("stamps: %d checks failed\n", g_failed); return 1; }
    printf("stamps: all cases passed\n");
    return 0;
}
