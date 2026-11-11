/* The mission rows HellForge's level 11 mixes a partner into, worked out again by its rules: who is a base, what
 * rules a partner out, and that what HellForge adds to the rows after its own pass does not change the answer. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_mix.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define MISSIONS 0x3773420u
#define OPERATIONS 0x32FEF10u
#define OBJECTIVES 0x32EF870u
#define ERADICATE 0xC5DD9A6650C23B4Aull
#define RADAR 0xF1969B14u
enum { MAIN_A = 0x100, MAIN_B, MAIN_C, MAIN_OLD, STEP_A, STEP_B, MAJOR };

static uint8_t* g_image;
static uint8_t g_base[CO_MIX_ROWS];
static int g_mixing;

static void Row(uint32_t r, uint32_t id, int enabled, int faction, uint32_t low, uint32_t high, int kind, uint64_t delta)
{
    uint8_t* row = g_image + MISSIONS + r * 0x380u;
    memcpy(row, &id, 4); row[4] = (uint8_t)enabled; row[8] = (uint8_t)faction; memcpy(row + 0xC, &low, 4); memcpy(row + 0x10, &high, 4);
    row[0x34] = (uint8_t)kind; memcpy(row + 0x360, &delta, 8);
}
static void Entry(uint32_t r, int k, uint32_t id, float weight, uint32_t cls, uint32_t last)
{
    uint32_t e[6] = {id, 0, 0, cls, 0, last};
    memcpy(&e[1], &weight, 4);
    memcpy(g_image + MISSIONS + r * 0x380u + 0x40u + (uint32_t)k * 0x18u, e, sizeof e);
}
static void Listed(uint32_t o, int k, uint32_t id) { memcpy(g_image + OPERATIONS + o * 0x490u + 0x38u + 8u * (uint32_t)k, &id, 4); }
static void Objective(uint32_t d, uint32_t id, uint32_t low, uint32_t high)
{
    uint8_t* row = g_image + OBJECTIVES + d * 0xA0u;
    memcpy(row, &id, 4); memcpy(row + 0x24, &low, 4); memcpy(row + 0x28, &high, 4);
}

/* Rows 1 and 2: two standard missions of faction 3 one operation lists, each with a main the other does not list.
 * Row 3 is of another faction, row 4 is listed by another operation only. */
static void Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    Objective(1, MAIN_A, 1, 11); Objective(2, MAIN_B, 1, 11); Objective(3, MAIN_C, 1, 11); Objective(4, MAIN_OLD, 1, 10);
    Row(1, 0xA1, 1, 3, 1, 11, 1, 0); Entry(1, 0, MAIN_A, 1.0f, 0, 0); Entry(1, 1, STEP_A, 1.0f, 1, 0); Entry(1, 2, MAJOR, 1.0f, 2, 0);
    Row(2, 0xA2, 1, 3, 1, 11, 1, 0); Entry(2, 0, MAIN_B, 1.0f, 0, 0); Entry(2, 1, STEP_B, 1.0f, 1, 0);
    Row(3, 0xA3, 1, 4, 1, 11, 1, 0); Entry(3, 0, MAIN_C, 1.0f, 0, 0);
    Row(4, 0xA4, 1, 3, 1, 11, 1, 0); Entry(4, 0, MAIN_C, 1.0f, 0, 0);
    Listed(0, 0, 0xA1); Listed(0, 1, 0xA2); Listed(0, 2, 0xA3); Listed(1, 0, 0xA4);
}
static int Bases(void)
{
    int n = 0;
    memset(g_base, 0xEE, sizeof g_base); g_mixing = -1;
    if (co_mix_bases((uintptr_t)g_image, g_base, &g_mixing) != NULL) return -1;
    for (uint32_t r = 0; r < CO_MIX_ROWS; r++) n += g_base[r] == 1;
    return n;
}

int main(void)
{
    Reset("two missions of one operation and faction");
    CHECK(Bases() == 2 && g_base[1] == 1 && g_base[2] == 1 && g_base[3] == 0 && g_base[4] == 0 && g_mixing == 0);
    CHECK(co_mix_read((uintptr_t)g_image, g_base, &g_mixing) != NULL); /* not the game's table */

    Reset("a partner's main the base lists");
    Entry(1, 3, MAIN_B, 1.0f, 3, 0);
    CHECK(Bases() == 1 && g_base[1] == 0 && g_base[2] == 1);

    Reset("a partner's step the base lists as more than a step");
    Entry(1, 3, STEP_B, 1.0f, 2, 0);
    CHECK(Bases() == 1 && g_base[1] == 0);
    Entry(1, 3, STEP_B, 1.0f, 1, 0);
    CHECK(Bases() == 2);

    Reset("a main not offered at level 11, an entry never drawn");
    Entry(2, 0, MAIN_OLD, 1.0f, 0, 0);
    CHECK(Bases() == 1 && g_base[1] == 0 && g_base[2] == 1);
    Entry(2, 0, MAIN_B, 0.0f, 0, 0);
    CHECK(Bases() == 1 && g_base[1] == 0);

    Reset("a row that is no standard mission");
    Row(2, 0xA2, 1, 3, 1, 10, 1, 0); /* not offered at level 11 */
    CHECK(Bases() == 0);
    Row(2, 0xA2, 0, 3, 1, 11, 1, 0); /* disabled */
    CHECK(Bases() == 0);
    Row(2, 0xA2, 1, 3, 1, 11, 8, 0); /* another kind */
    CHECK(Bases() == 0);

    Reset("an Eradicate mission partners only where the level has Eradicate");
    Row(2, 0xA2, 1, 3, 1, 11, 1, ERADICATE);
    CHECK(Bases() == 1 && g_base[1] == 1 && g_base[2] == 0);
    Row(2, 0xA2, 1, 3, 1, 10, 1, ERADICATE); /* its range not widened: the level leaves Eradicate out */
    CHECK(Bases() == 0);

    Reset("what HellForge added after its own pass");
    Entry(1, 3, STEP_B, 0.0f, 1, 0);          /* the partner's step, registered in the base */
    Entry(2, 2, RADAR, 1.0f, 1, 0);           /* a partner that has the radar station as a step */
    Entry(1, 4, RADAR, 0.0f, 3, 0xFFFFFFFFu); /* the radar station given to the base */
    Entry(1, 5, MAIN_B, 0.0f, 1, 0);          /* a step entry naming the partner's main */
    CHECK(Bases() == 2 && g_mixing == 1);
    Entry(1, 4, RADAR, 1.0f, 3, 0xFFFFFFFFu); /* the game's own entry of that id counts */
    CHECK(Bases() == 1 && g_base[1] == 0);
    Row(5, 0xA5, 1, 3, 11, 11, 1, 0); Entry(5, 0, MAIN_C, 1.0f, 0, 0); Listed(0, 3, 0xA5); /* a city mission made for level 11 */
    Entry(1, 4, 0, 0.0f, 0, 0);
    CHECK(Bases() == 2 && g_base[5] == 0);
    Row(5, 0xA5, 1, 3, 1, 11, 1, 0);
    CHECK(Bases() == 3 && g_base[5] == 1);

    Reset("more partners than HellForge keeps change nothing");
    for (uint32_t r = 10; r < 39; r++) { Row(r, 0xB00 + r, 1, 3, 1, 11, 1, 0); Entry(r, 0, MAIN_C, 1.0f, 0, 0); Listed(0, (int)(r - 10 + 3), 0xB00 + r); }
    CHECK(Bases() == 2 + 29 && g_base[10] == 1 && g_base[38] == 1);

    if (g_failed) { printf("  mix: %d checks failed\n", g_failed); return 1; }
    printf("  mix: all cases passed\n");
    return 0;
}
