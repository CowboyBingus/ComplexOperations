/* A normal level 11 mission stays the game's: which level is complex, the navigation tiles and limits each kind gets,
 * and the HUD stubs, whose normal path must be the bytes the game holds at each site and end where the game goes on. */
#include <stdio.h>
#include <math.h>
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
    const float key = co_disk_key();
    float radii[5];
    co_complex_radii(radii);
    /* HellForge's rule at map scale 20 on 16384 m: border 7956.4, disk 8073.4 of 8192 */
    CHECK(radii[1] == key && radii[1] == 8073.4f / 8192.0f && radii[0] == 8013.4f / 8192.0f && radii[4] == 8074.4f);
    CHECK(radii[2] == 8192.0f - 8073.4f - 1.0f && radii[3] == 8192.0f + 8073.4f + 1.0f && key > 0.9855f && key < 0.9856f);
    CHECK(co_level_complex(key) && !co_level_complex(hellforge) && !co_level_complex(0.0f) && !co_level_complex(1.0f));
    CHECK(!co_level_complex(0.9855f) && !co_level_complex(nextafterf(key, 1.0f)) && !co_level_complex(nextafterf(key, 0.0f)));

    g_case = "the navigation tiles";
    CoNavPlan p;
    /* the game's tile bounds: min + x * tile, and that + tile (exe 0x7B44D3) */
    const struct { float disk, radius, low, tile, stockLow; } normal[] = {
        {hellforge, 1023.0f, -4095.75f, 256.0f, -1023.75f}, /* HellForge's level 11 */
        {0.5f, 682.0f, -2047.75f, 128.0f, -511.75f},        /* a lower level */
        {0.0f, 0.0f, -2047.75f, 128.0f, -511.75f},          /* the ship */
        {hellforge, 8074.4f, -4095.75f, 256.0f, -1023.75f}};/* the radius alone makes no level complex */
    for (size_t i = 0; i < sizeof normal / sizeof normal[0]; i++)
    {
        co_nav_plan(normal[i].disk, normal[i].radius, &p);
        CHECK(p.grid == 32 && p.low == normal[i].low && p.tile == normal[i].tile && p.high == p.low + 32.0f * p.tile);
        /* tiles 12..19 are HellForge's 0..7, bit for bit: the navmesh over its world is built from the same bounds */
        for (int x = 0; x < 8; x++)
        {
            volatile float mine = p.low + (float)(x + 12) * p.tile, stock = normal[i].stockLow + (float)x * p.tile;
            volatile float mineEnd = mine + p.tile, stockEnd = stock + p.tile;
            CHECK(memcmp((void*)&mine, (void*)&stock, 4) == 0 && memcmp((void*)&mineEnd, (void*)&stockEnd, 4) == 0);
        }
    }
    co_nav_plan(key, 8074.4f, &p);
    CHECK(p.grid == 32 && p.low == -8190.0f && p.high == 8194.0f && p.tile == 512.0f && (p.high - p.low) / (float)p.grid == p.tile);

    g_case = "the limits";
    CHECK(co_level_limit(CO_LIMIT_SPAWNERS, 0) == 128 && co_level_limit(CO_LIMIT_SPAWNERS, 1) == 1024);
    CHECK(co_level_limit(CO_LIMIT_MARKER_SLOTS, 0) == 16 && co_level_limit(CO_LIMIT_MARKER_SLOTS, 1) == 16);
    CHECK(co_level_limit(CO_LIMIT_MARKER_TRACKERS, 0) == 10 && co_level_limit(CO_LIMIT_MARKER_TRACKERS, 1) == 10);
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

    /* The radii step: HellForge's stub as its source builds it for map scale 2, reached from the site's jump. */
    g_case = "the radii step";
    static const uint8_t stock[116] = {
        0x4C, 0x89, 0x2D, 0x11, 0x22, 0x33, 0x00, 0x49, 0x89, 0x95, 0xE0, 0x2D, 0x00, 0x00, 0x41, 0x80, 0xBD, 0xF9, 0x00, 0x00, 0x00, 11, 0x75, 0x57,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x41, 0xC7, 0x85, 0x78, 0x2D, 0x00, 0x00, 0, 0, 0, 0, 0x41, 0xC7, 0x85, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
        0x41, 0xC7, 0x85, 0x84, 0x2D, 0x00, 0x00, 0, 0, 0, 0, 0x41, 0xC7, 0x85, 0x88, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
        0x41, 0xC7, 0x85, 0x8C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,
        0x31, 0xC0, 0x49, 0x89, 0x85, 0x94, 0x2D, 0x00, 0x00, 0x48, 0xB8, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F,
        0x49, 0x89, 0x85, 0x9C, 0x2D, 0x00, 0x00, 0xE9, 0, 0, 0, 0};
    const uint32_t stockAt = 0x3100C40u;
    const float stockRadii[5] = {1009.24f / 1024.0f, 1022.0f / 1024.0f, 1.0f, 2047.0f, 1023.0f};
    uint8_t* st = g_fake.image + stockAt;
    CHECK(co_radii_check(game) != NULL);
    memcpy(g_fake.image + CO_RADII_BEHIND, kCoRadiiBehind, sizeof kCoRadiiBehind);
    CHECK(co_radii_check(game) != NULL); /* the game's own instruction at the site: HellForge's step is not there */
    const uint8_t jump[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    const int32_t to = (int32_t)(stockAt - (CO_RADII_SITE + 5u)), back = (int32_t)(CO_RADII_SITE + 7u - (stockAt + 116u));
    memcpy(g_fake.image + CO_RADII_SITE, jump, 7); memcpy(g_fake.image + CO_RADII_SITE + 1, &to, 4);
    CHECK(co_radii_check(game) != NULL); /* a jump to nothing */
    memcpy(st, stock, sizeof stock);
    for (int i = 0; i < 5; i++) memcpy(st + 37 + 11 * i, &stockRadii[i], 4);
    memcpy(st + 112, &back, 4);
    CHECK(co_radii_check(game) == NULL && g_fake.writes == 0);
    const float other = 800.0f / 1024.0f; /* another map scale's disk */
    memcpy(st + 48, &other, 4); CHECK(co_radii_check(game) != NULL); memcpy(st + 48, &stockRadii[1], 4);
    st[21] = 10; CHECK(co_radii_check(game) != NULL); st[21] = 11;
    st[90] ^= 1; CHECK(co_radii_check(game) != NULL); st[90] ^= 1;
    g_fake.image[CO_RADII_BEHIND + 4] ^= 1; CHECK(co_radii_check(game) != NULL); g_fake.image[CO_RADII_BEHIND + 4] ^= 1;
    CHECK(co_radii_check(game) == NULL && CO_RADII_SITE + 7u == CO_RADII_BEHIND && CO_RADII_BEHIND + sizeof kCoRadiiBehind == CO_RADII_BACK);
    memset(s, 0xCC, sizeof s);
    CHECK(co_radii_stub(s, game + CODE + 0x100, game, game + FLAG + 1) == CO_RADII_STUB && s[CO_RADII_STUB] == 0xCC);
    CHECK(s[0] == 0x80 && s[1] == 0x3D && Leads(s, CODE + 0x100, 2) + 1u == FLAG + 1u && s[7] == 0x74 && 9u + s[8] == 64u);
    for (int i = 0; i < 5; i++)
    {
        /* each store is HellForge's own instruction for the field, with the complex level's value */
        CHECK(memcmp(s + 9 + 11 * i, stock + 30 + 11 * i, 7) == 0 && memcmp(s + 16 + 11 * i, &radii[i], 4) == 0);
    }
    CHECK(memcmp(s + 64, kCoRadiiBehind, sizeof kCoRadiiBehind) == 0 && s[74] == 0xE9 && Leads(s, CODE + 0x100, 75) == CO_RADII_BACK);

    /* One generation's decision: taken at its first level init, kept through its retries. */
    g_case = "a generation's decision";
    CoGeneration g = {0};
    CHECK(co_generation_decision(&g, 0x1000, 0x51, 0, 11, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_COMPLEX);
    CHECK(co_generation_decision(&g, 0x1000, 0x51, 1, 11, 1, 0, CO_HOST_ROW_NONE) == CO_MISSION_COMPLEX); /* a retry keeps it */
    CHECK(co_generation_decision(&g, 0x1000, 0x50, 1, 11, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_COMPLEX);
    CHECK(co_generation_decision(&g, 0x1000, 0x50, 0, 11, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_NORMAL);  /* a new generation */
    CHECK(co_generation_decision(&g, 0x1000, 0x51, 1, 11, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_NORMAL);
    CHECK(co_generation_decision(&g, 0x2000, 0x51, 1, 11, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_COMPLEX); /* another level: nothing held for it */
    CHECK(co_generation_decision(&g, 0x2000, 0x51, 0, 10, 1, 1, CO_HOST_ROW_NONE) == CO_MISSION_NORMAL);  /* other difficulties */
    CHECK(co_generation_decision(&g, 0x2000, 0x51, 1, 11, 1, 0, CO_HOST_ROW_NONE) == CO_MISSION_NORMAL);  /* and nothing held after one */
    CHECK(co_generation_decision(&g, 0x3000, 0x51, 0, 11, 0, 1, CO_HOST_ROW_ON) == CO_MISSION_COMPLEX);
    CHECK(co_generation_decision(&g, 0x3000, 0x51, 0, 11, 0, 1, CO_HOST_ROW_OFF) == CO_MISSION_NORMAL);
    CHECK(co_generation_decision(&g, 0x3000, 0x51, 0, 11, 0, 1, CO_HOST_ROW_NONE) == CO_MISSION_LEAVE);

    if (g_failed) { printf("normal: %d checks failed\n", g_failed); return 1; }
    printf("normal: all cases passed\n");
    return 0;
}
