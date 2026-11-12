/* The cell types' kill height and water level and the sea plane, lowered by a complex level: each lowered field keeps
 * its saved value until a later level whose table holds the descriptor puts it back, whatever levels come between. A
 * descriptor that reads another value than the mod's mark was reloaded and is left alone; a full list lowers nothing
 * more. Run on a fake level generator. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLevelInit(uintptr_t level);
void CoXlTestGenLevel(float current);
void CoXlTestLowered(uint64_t op);
void CoXlTestPending(uint32_t* kill, uint32_t* dry, uint32_t* sea);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define DESCRIPTORS 1400u
#define DESC_SIZE 0x130u
#define KILL 0x12Cu
#define WATER 0xE4u
#define SEA 0x44u
static uint8_t* g_lg;   /* the level generator: +0x2D0 the types, +0x2DD0 the sea plane, +0x2DE0 the map type */
static uint8_t* g_ts;   /* +0 the table, +0xA0 its count */
static uint64_t* g_table;
static uint8_t* g_desc; /* the cell type descriptors, and the sea planes after them */
static uint8_t g_mapType[0x40];

static uint64_t D(uint32_t k) { return (uint64_t)(uintptr_t)(g_desc + (size_t)k * DESC_SIZE); }
static float F(uint32_t k, uint32_t field) { float v; memcpy(&v, g_desc + (size_t)k * DESC_SIZE + field, 4); return v; }
static void SetF(uint32_t k, uint32_t field, float v) { memcpy(g_desc + (size_t)k * DESC_SIZE + field, &v, 4); }

/* A level of the given types and sea plane: level init, then its ops 0x00 and 0x0A. */
static void Level(float current, const uint32_t* types, uint32_t n, uint32_t sea)
{
    for (uint32_t i = 0; i < n; i++) g_table[i] = D(types[i]);
    memcpy(g_ts + 0xA0, &n, 4);
    const uint64_t plane = D(sea);
    memcpy(g_lg + 0x2DD0, &plane, 8);
    g_fake.log[0] = 0; /* the fake host's log holds 8 KB: each level's lines alone */
    CoXlTestGenLevel(current);
    CoXlTestLowered(0x00);
    CoXlTestLowered(0x05);
    CoXlTestLowered(0x0A);
}

static int PendingIs(uint32_t kill, uint32_t dry, uint32_t sea)
{
    uint32_t k, d, s;
    CoXlTestPending(&k, &d, &s);
    return k == kill && d == dry && s == sea;
}

static int Count(const char* text) { int n = 0; for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p++) n++; return n; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_ts = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_table = (uint64_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_desc = (uint8_t*)VirtualAlloc(NULL, (size_t)DESCRIPTORS * DESC_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL || g_ts == NULL || g_table == NULL || g_desc == NULL) { printf("killheight: no memory\n"); return 1; }
    for (uint32_t k = 0; k < DESCRIPTORS; k++) { SetF(k, KILL, -100.0f); SetF(k, WATER, -20.0f); SetF(k, SEA, -4.5f); }
    const uint64_t ts = (uint64_t)(uintptr_t)g_ts, table = (uint64_t)(uintptr_t)g_table, mt = (uint64_t)(uintptr_t)g_mapType;
    memcpy(g_lg + 0x2D0, &ts, 8);
    memcpy(g_ts, &table, 8);
    memcpy(g_lg + 0x2DE0, &mt, 8);
    g_mapType[0x1D] = 3;
    CoXlTestLevelInit((uintptr_t)g_lg);
    enum { A = 1, B = 2, C = 3, E = 4, SEA_A = 10, SEA_C = 11, SEA_E = 12 };
    SetF(SEA_C, SEA, -2.0f); SetF(SEA_E, SEA, -3.0f);

    g_case = "a complex level on map A";
    Level(8.0f, (const uint32_t[]){A, B}, 2, SEA_A);
    CHECK(F(A, KILL) == -480.0f && F(B, KILL) == -480.0f && F(A, WATER) == -501.0f && F(B, WATER) == -501.0f && F(SEA_A, SEA) == -501.0f);
    CHECK(PendingIs(2, 2, 1));

    g_case = "a normal level on a map without A's types (a war-table preview of another map)";
    Level(1.0f, (const uint32_t[]){C}, 1, SEA_C);
    CHECK(F(A, KILL) == -480.0f && F(B, KILL) == -480.0f && F(A, WATER) == -501.0f && F(B, WATER) == -501.0f && F(SEA_A, SEA) == -501.0f);
    CHECK(F(C, KILL) == -100.0f && F(C, WATER) == -20.0f && F(SEA_C, SEA) == -2.0f); /* nothing written to that level's */
    CHECK(PendingIs(2, 2, 1));

    g_case = "a complex level on map E: two maps' sea planes pending at once";
    Level(8.0f, (const uint32_t[]){E}, 1, SEA_E);
    CHECK(F(E, KILL) == -480.0f && F(E, WATER) == -501.0f && F(SEA_E, SEA) == -501.0f && F(SEA_A, SEA) == -501.0f);
    CHECK(PendingIs(3, 3, 2));

    g_case = "a normal level on map A again";
    Level(1.0f, (const uint32_t[]){A, C}, 2, SEA_A);
    CHECK(F(A, KILL) == -100.0f && F(A, WATER) == -20.0f && F(SEA_A, SEA) == -4.5f); /* back */
    CHECK(F(B, KILL) == -480.0f && F(B, WATER) == -501.0f && F(E, KILL) == -480.0f && F(SEA_E, SEA) == -501.0f); /* still pending */
    CHECK(PendingIs(2, 2, 1));

    g_case = "a normal level whose descriptors were reloaded";
    SetF(B, KILL, -90.0f); SetF(B, WATER, -30.0f); SetF(SEA_E, SEA, -7.0f); /* the game's own values, not the mod's mark */
    Level(1.0f, (const uint32_t[]){B, E}, 2, SEA_E);
    CHECK(F(B, KILL) == -90.0f && F(B, WATER) == -30.0f && F(SEA_E, SEA) == -7.0f); /* left alone, dropped */
    CHECK(F(E, KILL) == -100.0f && F(E, WATER) == -20.0f);                       /* still the mark: back */
    CHECK(PendingIs(0, 0, 0));

    g_case = "a complex level on a map it lowered before, its entry still pending";
    Level(8.0f, (const uint32_t[]){A}, 1, SEA_A);
    Level(1.0f, (const uint32_t[]){C}, 1, SEA_C);
    Level(8.0f, (const uint32_t[]){A}, 1, SEA_A); /* put back first, then lowered again: one entry, the authored value */
    CHECK(F(A, KILL) == -480.0f && F(SEA_A, SEA) == -501.0f && PendingIs(1, 1, 1));
    Level(1.0f, (const uint32_t[]){A}, 1, SEA_A);
    CHECK(F(A, KILL) == -100.0f && F(A, WATER) == -20.0f && F(SEA_A, SEA) == -4.5f && PendingIs(0, 0, 0));

    g_case = "the list is full";
    {
        uint32_t types[256];
        for (uint32_t m = 0; m < 4; m++) /* four complex maps of 256 types each, none seen again */
        {
            for (uint32_t i = 0; i < 256; i++) types[i] = 100 + 256 * m + i;
            Level(8.0f, types, 256, 20 + m);
        }
        CHECK(PendingIs(1024, 1024, 4));
        Level(8.0f, (const uint32_t[]){A, B}, 2, SEA_A);
        CHECK(F(A, KILL) == -100.0f && F(B, KILL) == -90.0f && F(A, WATER) == -20.0f && PendingIs(1024, 1024, 5)); /* not lowered */
        CHECK(Count("kill heights to put back is full") == 1 && Count("water levels to put back is full") == 1);
        for (uint32_t i = 0; i < 256; i++) types[i] = 100 + i; /* the first map's level: its 256 back */
        Level(1.0f, types, 256, 20);
        CHECK(PendingIs(768, 768, 4) && F(100, KILL) == -100.0f && F(355, WATER) == -20.0f && F(356, KILL) == -480.0f);
        Level(8.0f, (const uint32_t[]){A, B}, 2, SEA_A);
        CHECK(F(A, KILL) == -480.0f && F(B, KILL) == -480.0f && PendingIs(770, 770, 4));
    }

    if (g_failed) { printf("killheight: %d checks failed\n", g_failed); return 1; }
    printf("killheight: all cases passed\n");
    return 0;
}
