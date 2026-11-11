/* A complex level's objective list from the selection: every mission's primaries as whole blocks within the places
 * HellForge reports (30), what makes room for them and what never does, and the repeats while places are free. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_objectives.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define MISSIONS 0x3773420u
#define OBJECTIVES 0x32EF870u
#define LIVE_EVENT 0x68BFBB59u
#define RADAR 0xF1969B14u
#define MANY 0xFFFFFFFFu
enum { ICBM = 0x100, CODES, POWER, FUEL, MAIN_A, MAIN_B, MAIN_F, MAIN_G, STEP_T, STEP_X, MAIN_N, MAIN_D, CONVOY, NOWHERE, STRAY,
    M1 = 0x200, M2, M3, O1 = 0x300, O2, O3, O4, O5, O6 };
enum { PLAIN = 50, LEADING = 45, BASE = 5, PARTNER = 22 };

static uint8_t* g_image;
static uint32_t g_definitions;
static CoList g_list;

static void Entry(uint32_t r, int k, uint32_t id, float weight, uint32_t cls, uint32_t most)
{
    uint32_t e[6] = {id, 0, 0, cls, 0, most};
    memcpy(&e[1], &weight, 4);
    memcpy(g_image + MISSIONS + r * 0x380u + 0x40u + (uint32_t)k * 0x18u, e, sizeof e);
}
static void Objective(uint32_t id, uint32_t most, uint8_t bits, uint8_t nowhere)
{
    uint8_t* row = g_image + OBJECTIVES + ++g_definitions * 0xA0u;
    memcpy(row, &id, 4); memcpy(row + 0x20, &most, 4); row[0x2C] = bits; row[0x2D] = nowhere;
}

static void Reset(const char* name)
{
    static const uint32_t placed[] = {ICBM, CODES, POWER, FUEL, MAIN_A, MAIN_B, MAIN_F, MAIN_G, STEP_T, STEP_X, MAIN_D, CONVOY, RADAR, LIVE_EVENT};
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    g_definitions = 0;
    co_list_test_reset();
    for (uint32_t i = 0; i < sizeof placed / sizeof placed[0]; i++) Objective(placed[i], 3, 0, 0);
    Objective(MAIN_N, 1, 0, 1); Objective(NOWHERE, 1, 0, 1);
    for (uint32_t id = M1; id <= M3; id++) Objective(id, 1, 0, 0);
    for (uint32_t id = O1; id <= O6; id++) Objective(id, 2, 0, 0);
    /* a city's Launch ICBM mission: no partner */
    Entry(PLAIN, 0, ICBM, 1.0f, 0, 3); Entry(PLAIN, 1, CODES, 1.0f, 1, 1); Entry(PLAIN, 2, POWER, 1.0f, 1, 1); Entry(PLAIN, 3, FUEL, 1.0f, 1, 1);
    /* a mission whose selection starts with a step, with a main that has no place on the map */
    Entry(LEADING, 0, MAIN_N, 1.0f, 0, 1); Entry(LEADING, 1, MAIN_D, 1.0f, 0, 4); Entry(LEADING, 2, STEP_X, 1.0f, 1, 1); Entry(LEADING, 3, CONVOY, 1.0f, 4, 4);
    /* a base with two mains twice, and its partner with three of one main, another and a step the base has registered */
    Entry(BASE, 0, MAIN_A, 1.0f, 0, 2); Entry(BASE, 1, MAIN_B, 1.0f, 0, 2); Entry(BASE, 2, STEP_T, 0.0f, 1, 0);
    Entry(PARTNER, 0, MAIN_F, 1.0f, 0, 3); Entry(PARTNER, 1, MAIN_G, 1.0f, 0, 1); Entry(PARTNER, 2, STEP_T, 1.0f, 1, 1);
    /* a third mission that has the partner's first main alone: the shorter run */
    Entry(60, 0, MAIN_F, 1.0f, 0, 3);
    for (uint32_t r = 0; r < 4; r++)
    {
        static const uint32_t rows[4] = {PLAIN, LEADING, BASE, PARTNER};
        for (int k = 0; k < 3; k++) Entry(rows[r], 8 + k, M1 + (uint32_t)k, 1.0f, 2, MANY);
        for (int k = 0; k < 6; k++) Entry(rows[r], 12 + k, O1 + (uint32_t)k, 1.0f, 3, MANY);
        Entry(rows[r], 20, RADAR, 1.0f, 3, MANY); Entry(rows[r], 21, NOWHERE, 1.0f, 2, MANY);
    }
}

static const char* Fill(uint32_t row, const uint32_t* selected, uint32_t n, uint32_t places, int primaries)
{
    const CoListRules rules = {3, places, primaries, 6, 24};
    memset(&g_list, 0xEE, sizeof g_list);
    return co_list((uintptr_t)g_image, row, selected, n, &rules, &g_list);
}
static int Is(uint32_t from, const uint32_t* ids, uint32_t n) { return from + n <= g_list.count && memcmp(g_list.ids + from, ids, 4ull * n) == 0; }
static uint32_t Count(uint32_t id) { uint32_t c = 0; for (uint32_t i = 0; i < g_list.count; i++) c += g_list.ids[i] == id; return c; }
/* Every block past the selection's is one of the selection's blocks, id for id and main for main. */
static int Whole(void)
{
    for (uint32_t b = g_list.unit + 1; b <= g_list.blocks; b++)
    {
        int same = 0;
        for (uint32_t s = 1; s <= g_list.unit && !same; s++)
        {
            uint32_t i = 0, j = 0;
            same = g_list.blockRow[b - 1] == g_list.blockRow[s - 1];
            while (same)
            {
                while (i < g_list.count && g_list.block[i] != b) i++;
                while (j < g_list.count && g_list.block[j] != s) j++;
                if (i == g_list.count || j == g_list.count) { same = i == g_list.count && j == g_list.count; break; }
                same = g_list.ids[i] == g_list.ids[j] && g_list.main[i] == g_list.main[j];
                i++; j++;
            }
        }
        if (!same) return 0;
    }
    for (uint32_t i = 0; i < g_list.count; i++) if (g_list.block[i] > g_list.blocks) return 0;
    return 1;
}

/* The mixing selection: the base's four mains, optional and major objectives, then the partner's five primaries. */
static uint32_t Mixed(uint32_t* s, uint32_t optional, uint32_t major, uint32_t tail)
{
    static const uint32_t base[4] = {MAIN_A, MAIN_A, MAIN_B, MAIN_B}, partner[5] = {MAIN_F, MAIN_F, MAIN_F, MAIN_G, STEP_T};
    uint32_t n = 0;
    memcpy(s, base, sizeof base); n += 4;
    for (uint32_t i = 0; i < optional; i++) s[n++] = O1 + i % 6;
    for (uint32_t i = 0; i < major; i++) s[n++] = M1 + i % 3;
    memcpy(s + n, partner, sizeof partner); n += 5;
    for (uint32_t i = 0; i < tail; i++) s[n++] = O1 + i % 6;
    return n;
}

int main(void)
{
    uint32_t s[32], n = 0;
    static const uint32_t icbm[6] = {ICBM, ICBM, ICBM, CODES, POWER, FUEL};
    static const uint32_t base[4] = {MAIN_A, MAIN_A, MAIN_B, MAIN_B}, partner[5] = {MAIN_F, MAIN_F, MAIN_F, MAIN_G, STEP_T};

    Reset("a row with no partner: three missions' blocks, the last optional objectives make room");
    memcpy(s, icbm, sizeof icbm); n = 6;
    for (uint32_t i = 0; i < 12; i++) s[n++] = O1 + i % 6;
    for (uint32_t i = 0; i < 6; i++) s[n++] = M1 + i % 3;
    CHECK(n == 24 && Fill(PLAIN, s, n, 30, 1) == NULL);
    CHECK(g_list.count == 30 && g_list.unit == 1 && g_list.own == 6 && g_list.blocks == 3 && g_list.held == 3 && g_list.short_ == 0);
    CHECK(Is(0, s, 12) && Is(12, s + 18, 6) && Is(18, icbm, 6) && Is(24, icbm, 6));
    CHECK(g_list.dropped[0] == 0 && g_list.dropped[1] == 6 && g_list.major[1] == 6 && g_list.optional[1] == 6 && g_list.mains == 9 && g_list.steps == 9);
    CHECK(g_list.block[0] == 1 && g_list.main[0] == 1 && g_list.block[3] == 1 && g_list.main[3] == 0 && g_list.block[6] == 0 && g_list.block[18] == 2 &&
        g_list.block[23] == 2 && g_list.block[24] == 3 && g_list.main[26] == 1 && g_list.main[27] == 0 && g_list.blockRow[2] == PLAIN && Whole());
    {
        uint8_t block[CO_LIST_MOST], isMain[CO_LIST_MOST], rows[CO_LIST_BLOCKS];
        uint32_t blocks = 0;
        CHECK(co_list_kept(PLAIN, g_list.ids, g_list.count, block, isMain, rows, &blocks) == 1 && blocks == 3 && memcmp(block, g_list.block, 30) == 0 &&
            memcmp(isMain, g_list.main, 30) == 0 && rows[1] == PLAIN);
        CHECK(co_list_kept(PLAIN, g_list.ids, g_list.count - 1, block, isMain, rows, &blocks) == 0); /* another list */
        CHECK(co_list_kept(BASE, g_list.ids, g_list.count, block, isMain, rows, &blocks) == 0);      /* another row */
    }

    Reset("a block that begins with a step keeps it; a main with no place is on the list as often as its block");
    {
        static const uint32_t block[5] = {STEP_X, MAIN_N, MAIN_D, MAIN_D, MAIN_D};
        memcpy(s, block, sizeof block); n = 5;
        for (uint32_t i = 0; i < 4; i++) s[n++] = CONVOY;
        for (uint32_t i = 0; i < 3; i++) s[n++] = M1 + i;
        CHECK(Fill(LEADING, s, n, 30, 1) == NULL);
        /* 12 selected, two more blocks of five, then each major objective once more: the missions want six */
        CHECK(g_list.count == 25 && g_list.unit == 1 && g_list.own == 5 && g_list.blocks == 3 && Is(0, s, 12) && Is(12, block, 5) && Is(17, block, 5));
        CHECK(g_list.block[12] == 2 && g_list.main[12] == 0 && g_list.main[13] == 1 && g_list.block[21] == 3 && Whole());
        CHECK(g_list.major[0] == 3 && g_list.major[1] == 6 && g_list.optional[1] == 0 && g_list.capped[0] == 0 && g_list.capped[1] == 1 && Count(M1) == 2 && Count(CONVOY) == 4);
    }

    Reset("a mixing row: the partner's three mains, its other main and its step are one block");
    n = Mixed(s, 6, 6, 0);
    CHECK(n == 21 && Fill(BASE, s, n, 30, 1) == NULL);
    CHECK(g_list.count == 30 && g_list.unit == 2 && g_list.own == 4 && g_list.blocks == 5 && g_list.held == 3 && g_list.short_ == 1);
    CHECK(Is(0, s, 6) && Is(6, s + 10, 11) && Is(17, base, 4) && Is(21, partner, 5) && Is(26, base, 4));
    CHECK(g_list.blockRow[0] == BASE && g_list.blockRow[1] == PARTNER && g_list.blockRow[2] == BASE && g_list.blockRow[3] == PARTNER && g_list.blockRow[4] == BASE);
    CHECK(g_list.dropped[0] == 0 && g_list.dropped[1] == 4 && g_list.major[1] == 6 && g_list.optional[1] == 2 && g_list.mains == 20 && g_list.steps == 2 && Whole());
    CHECK(g_list.block[12] == 2 && g_list.main[12] == 1 && g_list.block[16] == 2 && g_list.main[16] == 0);

    Reset("a full selection: the list is made again all the same");
    n = Mixed(s, 12, 6, 3);
    CHECK(n == 30 && Fill(BASE, s, n, 30, 1) == NULL);
    CHECK(g_list.count == 30 && g_list.blocks == 5 && g_list.held == 3 && g_list.dropped[1] == 13 && g_list.optional[1] == 2 && g_list.major[1] == 6 && Whole());

    Reset("a mission's first block may cost a major objective, a partner's block never");
    n = Mixed(s, 2, 6, 0);
    CHECK(n == 17 && Fill(BASE, s, n, 22, 1) == NULL);
    CHECK(g_list.count == 22 && g_list.blocks == 4 && g_list.held == 3 && g_list.short_ == 2 && g_list.dropped[0] == 1 && g_list.dropped[1] == 2);
    CHECK(Is(0, base, 4) && Is(4, s + 6, 5) && Is(9, partner, 5) && Is(14, base, 4) && Is(18, base, 4) && Count(M3) == 1 && Whole());

    Reset("what never makes room: the radar station, the live event, a class of the mission's own, an objective with no place");
    {
        static const uint32_t sel[8] = {MAIN_D, STEP_X, RADAR, LIVE_EVENT, NOWHERE, CONVOY, O1, MAIN_N};
        CHECK(Fill(LEADING, sel, 8, 10, 1) == NULL);
        /* the block is the row's three primaries; one more fits for the one optional objective, a third does not */
        CHECK(g_list.count == 10 && g_list.unit == 1 && g_list.blocks == 2 && g_list.held == 2 && g_list.short_ == 1 && g_list.dropped[1] == 1 && g_list.dropped[0] == 0);
        CHECK(Count(RADAR) == 1 && Count(LIVE_EVENT) == 1 && Count(NOWHERE) == 1 && Count(CONVOY) == 1 && Count(O1) == 0 && Count(MAIN_D) == 2 && Whole());
    }

    Reset("no place is passed and no block is cut, whatever the places");
    n = Mixed(s, 6, 6, 0);
    for (uint32_t places = n; places <= CO_LIST_MOST; places++)
    {
        uint32_t members = 0, sides = 0;
        CHECK(Fill(BASE, s, n, places, 1) == NULL && g_list.count <= places && g_list.unit == 2 && g_list.blocks >= 2 && Whole());
        for (uint32_t i = 0; i < g_list.count; i++) { members += g_list.block[i] != 0; sides += g_list.block[i] == 0; }
        /* both other missions' first blocks always, then a partner's block or two */
        CHECK((members == 17 || members == 22 || members == 27) && g_list.mains + g_list.steps == members && sides == g_list.major[1] + g_list.optional[1]);
        for (uint32_t i = g_list.count; i < CO_LIST_MOST; i++) CHECK(g_list.ids[i] == 0 && g_list.block[i] == 0);
        if (places >= 39) CHECK(g_list.blocks == 6 && g_list.held == 3 && g_list.short_ == 0 && g_list.dropped[0] + g_list.dropped[1] == 0);
    }
    CHECK(Fill(BASE, s, n, n - 1, 1) != NULL && g_list.count == 0);
    CHECK(Fill(BASE, s, n, CO_LIST_MOST + 1, 1) != NULL && Fill(BASE, s, 0, 30, 1) != NULL && Fill(BASE, s, 33, 64, 1) != NULL && Fill(162, s, n, 30, 1) != NULL);

    Reset("without the grouped rows no primary is added: side objectives only, within the places");
    n = Mixed(s, 6, 6, 0);
    CHECK(Fill(BASE, s, n, 30, 0) == NULL);
    /* each optional id twice a mission: six uses, one in the selection; nine places are free */
    CHECK(g_list.count == 30 && g_list.blocks == 2 && g_list.held == 1 && Is(0, s, 21) && g_list.mains == 8 && g_list.steps == 1 && g_list.major[1] == 6 && g_list.optional[1] == 15);
    CHECK(Count(O1) == 3 && Count(O4) == 2 && g_list.capped[1] == 0);

    Reset("a repeat within the definition's uses and its exclusive bits");
    {
        static const uint32_t sel[5] = {MAIN_A, 0x400, 0x401, 0x402, M1};
        Entry(BASE, 24, 0x400, 1.0f, 3, MANY); Entry(BASE, 25, 0x401, 1.0f, 3, MANY); Entry(BASE, 26, 0x402, 1.0f, 3, 1);
        Objective(0x400, 1, 2, 0); Objective(0x401, 3, 2, 0); Objective(0x402, 5, 0, 0);
        CHECK(Fill(BASE, sel, 5, 30, 0) == NULL);
        /* M1 twice more (one use a mission); the bit is used twice and a third time by 0x400, which closes 0x401;
         * 0x402 is held by the row's entry at one a mission */
        CHECK(g_list.count == 10 && Count(M1) == 3 && Count(0x400) == 2 && Count(0x401) == 1 && Count(0x402) == 3 && g_list.capped[0] == 1 && g_list.capped[1] == 1);
    }

    Reset("a primary no mission row has: no blocks, so no primary is added");
    {
        uint8_t block[CO_LIST_MOST], isMain[CO_LIST_MOST], rows[CO_LIST_BLOCKS];
        uint32_t blocks = 0;
        static const uint32_t sel[4] = {MAIN_A, MAIN_B, M1, STRAY};
        CHECK(Fill(BASE, sel, 4, 30, 1) == NULL && g_list.unit == 0 && g_list.blocks == 0 && g_list.held == 0 && Is(0, sel, 4) && g_list.mains == 0);
        CHECK(co_list_kept(BASE, g_list.ids, g_list.count, block, isMain, rows, &blocks) == 0);
    }

    Reset("the rows cannot be read");
    n = Mixed(s, 6, 6, 0);
    {
        const CoListRules rules = {3, 30, 1, 6, 24};
        CHECK(co_list((uintptr_t)g_image + FAKE_IMAGE_SIZE, BASE, s, n, &rules, &g_list) != NULL && g_list.count == 0);
    }

    if (g_failed) { printf("  objectives: %d checks failed\n", g_failed); return 1; }
    printf("  objectives: all cases passed\n");
    return 0;
}
