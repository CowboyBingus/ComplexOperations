/* A normal level 11 mission stays the game's: which level is complex, the navigation tiles and limits each kind gets,
 * and the HUD stubs, whose normal path must be the bytes the game holds at each site and end where the game goes on. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "xl/co_normal.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define CODE 0x3000000u /* where the stubs run: a free place in the image */
#define FLAG 0x3001140u

/* Where the rel32 at `at` of a stub placed at `code` leads, as an offset in the image. */
static uint32_t Leads(const uint8_t* stub, uint32_t code, size_t at) { int32_t d; memcpy(&d, stub + at, 4); return code + (uint32_t)at + 4u + (uint32_t)d; }
static int TestsFlag(const uint8_t* stub, uint32_t code) { return stub[0] == 0x80 && stub[1] == 0x3D && stub[6] == 0 && Leads(stub, code, 2) + 1u == FLAG; }

int main(void)
{
    g_case = "the level's kind";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const float hellforge = 1022.0f / 1024.0f;
    CHECK(co_level_complex(CO_DISK_KEY) && !co_level_complex(hellforge) && !co_level_complex(0.0f) && !co_level_complex(1.0f));
    CHECK(!co_level_complex(0.98549998f - 0.0000001f) && !co_level_complex(0.9856f));

    g_case = "the navigation tiles";
    CoNavPlan p;
    co_nav_plan(hellforge, 1023.0f, &p); /* HellForge's level 11: 64 tiles of 256 m, as without the mod */
    CHECK(p.grid == 8 && p.low == -1023.75f && p.high == 1024.25f && p.tile == 256.0f && (p.high - p.low) / (float)p.grid == p.tile);
    co_nav_plan(0.5f, 682.0f, &p);       /* a lower level */
    CHECK(p.grid == 8 && p.low == -511.75f && p.high == 512.25f && p.tile == 128.0f && (p.high - p.low) / (float)p.grid == p.tile);
    co_nav_plan(0.0f, 0.0f, &p);         /* the ship */
    CHECK(p.grid == 8 && p.tile == 128.0f);
    co_nav_plan(CO_DISK_KEY, 8074.4f, &p);
    CHECK(p.grid == 32 && p.low == -8190.0f && p.high == 8194.0f && p.tile == 512.0f && (p.high - p.low) / (float)p.grid == p.tile);
    co_nav_plan(hellforge, 8074.4f, &p); /* the radius alone makes no level complex */
    CHECK(p.grid == 8 && p.tile == 256.0f);

    g_case = "the limits";
    CHECK(co_level_limit(CO_LIMIT_SPAWNERS, 0) == 128 && co_level_limit(CO_LIMIT_SPAWNERS, 1) == 1024);
    CHECK(co_level_limit(CO_LIMIT_MARKER_SLOTS, 0) == 16 && co_level_limit(CO_LIMIT_MARKER_SLOTS, 1) == 64);
    CHECK(co_level_limit(CO_LIMIT_MARKER_TRACKERS, 0) == 10 && co_level_limit(CO_LIMIT_MARKER_TRACKERS, 1) == 32);
    CHECK(co_level_limit(CO_LIMIT_WELD_VERTICES, 0) == 16 && co_level_limit(CO_LIMIT_WELD_VERTICES, 1) == 256);
    CHECK(co_level_limit(CO_LIMIT_STAMP_SLOTS, 0) == 16383 && co_level_limit(CO_LIMIT_STAMP_SLOTS, 1) == 131071);
    CHECK(co_level_limit(CO_LIMITS, 1) == 0 && co_level_limit(-1, 0) == 0);

    g_case = "the HUD sites";
    const uintptr_t game = (uintptr_t)g_fake.image;
    CHECK(co_hud_check(game) != NULL); /* an empty image is not the game */
    memcpy(g_fake.image + CO_HUD_LIST_SITE, kCoHudListBytes, sizeof kCoHudListBytes);
    memcpy(g_fake.image + CO_HUD_FIND_SITE, kCoHudFindBytes, sizeof kCoHudFindBytes);
    CHECK(co_hud_check(game) != NULL);
    memcpy(g_fake.image + CO_HUD_FLAG_SITE, kCoHudFlagBytes, sizeof kCoHudFlagBytes);
    CHECK(co_hud_check(game) == NULL && g_fake.writes == 0);
    g_fake.image[CO_HUD_FLAG_SITE + 5] ^= 1;
    CHECK(co_hud_check(game) != NULL);
    g_fake.image[CO_HUD_FLAG_SITE + 5] ^= 1;
    /* the sites as the game lays them out: the call the list site holds, the search's end, the store's end */
    CHECK(Leads(kCoHudListBytes, CO_HUD_LIST_SITE, 1) == CO_HUD_LIST_CALL && CO_HUD_LIST_SITE + sizeof kCoHudListBytes == CO_HUD_LIST_NEXT);
    CHECK(CO_HUD_FIND_SITE + sizeof kCoHudFindBytes == CO_HUD_FIND_BACK && CO_HUD_FLAG_SITE + sizeof kCoHudFlagBytes == CO_HUD_FLAG_BACK);

    g_case = "the HUD list stub";
    uint8_t s[128];
    memset(s, 0xCC, sizeof s);
    CHECK(co_hud_list_stub(s, game + CODE, game, game + FLAG, 0x1122334455667788ull) == CO_HUD_LIST_STUB && s[CO_HUD_LIST_STUB] == 0xCC);
    CHECK(TestsFlag(s, CODE) && s[7] == 0x75 && 9u + s[8] == 19u);
    /* a normal mission: the site's own call, then the game's list part */
    CHECK(s[9] == 0xE8 && Leads(s, CODE, 10) == CO_HUD_LIST_CALL && s[14] == 0xE9 && Leads(s, CODE, 15) == CO_HUD_LIST_NEXT);
    uint64_t rows; memcpy(&rows, s + 24, 8);
    CHECK(s[19] == 0x4C && s[22] == 0x48 && s[23] == 0xB8 && rows == 0x1122334455667788ull && s[32] == 0xFF && s[33] == 0xD0);
    CHECK(s[34] == 0xE9 && Leads(s, CODE, 35) == CO_HUD_LIST_BACK);

    g_case = "the HUD row search";
    memset(s, 0xCC, sizeof s);
    CHECK(co_hud_find_head(s, game + CODE + 0x40, game, game + FLAG) == CO_HUD_FIND_HEAD && s[CO_HUD_FIND_HEAD] == 0xCC);
    CHECK(TestsFlag(s, CODE + 0x40) && s[7] == 0x75 && 9u + s[8] == CO_HUD_FIND_HEAD); /* complex: the grouped search behind */
    CHECK(memcmp(s + 9, kCoHudFindBytes, sizeof kCoHudFindBytes) == 0);
    /* the game's bytes hold no address, and their three jumps stay inside or end right behind them */
    CHECK(s[9 + 24] == 0x74 && 9 + 26 + s[9 + 25] == 9 + 41 && s[9 + 37] == 0x72 && 9 + 39 + (int8_t)s[9 + 38] == 9 + 22 && s[9 + 39] == 0xEB && 9 + 41 + s[9 + 40] == 52);
    CHECK(s[52] == 0xE9 && Leads(s, CODE + 0x40, 53) == CO_HUD_FIND_BACK);

    g_case = "the HUD near mark";
    memset(s, 0xCC, sizeof s);
    CHECK(co_hud_flag_stub(s, game + CODE + 0xC0, game, game + FLAG) == CO_HUD_FLAG_STUB && s[CO_HUD_FLAG_STUB] == 0xCC);
    CHECK(TestsFlag(s, CODE + 0xC0) && s[7] == 0x74 && 9u + s[8] == 15u && s[13] == 0xEB && 15u + s[14] == 19u);
    /* a normal mission runs the site's seven bytes in order; a complex one the same with the store as an OR */
    CHECK(memcmp(s + 15, kCoHudFlagBytes, sizeof kCoHudFlagBytes) == 0);
    CHECK(s[9] == 0x08 && memcmp(s + 10, kCoHudFlagBytes + 1, 3) == 0);
    CHECK(s[22] == 0xE9 && Leads(s, CODE + 0xC0, 23) == CO_HUD_FLAG_BACK);

    if (g_failed) { printf("normal: %d checks failed\n", g_failed); return 1; }
    printf("normal: all cases passed\n");
    return 0;
}
