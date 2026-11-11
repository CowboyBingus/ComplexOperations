/* The row readers' copies and the distance row: HellForge's row and table are never written, a level that is not
 * complex reads what it read without the mod, a complex one gets three missions' outposts and objectives and the
 * radii and distance of a 16384 m world, and a refused install leaves every site as it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_normal.h"
#include "xl/co_rows.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define RUN 0x3300000u /* the test's way into a stub */

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_rows_test_reset();
    fake_stock_rows(g_fake.image);
    return g_fake.image;
}

/* A reader at its copy's last store: rbp its frame, r13 its level (the radius read), rax the copy's part the store
 * fills, and what it keeps at [rsp+0x60]. The site's way on is a `ret` in the image. */
typedef struct Reader { void* frame; void* r13; void* store; void* kept; uintptr_t stub; } Reader;
static void Run(uint8_t* image, Reader* reader)
{
    static const uint8_t code[] = {
        0x55, 0x41, 0x55,                               /* push rbp; push r13 */
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,       /* sub rsp, 0x80 */
        0x48, 0x8B, 0x29, 0x4C, 0x8B, 0x69, 0x08,       /* mov rbp, [rcx]; mov r13, [rcx+8] */
        0x48, 0x8B, 0x41, 0x10, 0x48, 0x8B, 0x51, 0x18, /* mov rax, [rcx+0x10]; mov rdx, [rcx+0x18] */
        0x48, 0x89, 0x54, 0x24, 0x58,                   /* mov [rsp+0x58], rdx: [rsp+0x60] behind the call */
        0xFF, 0x51, 0x20,                               /* call [rcx+0x20] */
        0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,       /* add rsp, 0x80 */
        0x41, 0x5D, 0x5D, 0xC3};                        /* pop r13; pop rbp; ret */
    memcpy(image + RUN, code, sizeof code);
    ((void (*)(Reader*))(void*)(image + RUN))(reader);
}

static int Jumps(const uint8_t* image, uint32_t site, uintptr_t to, uint32_t length)
{
    int32_t rel; memcpy(&rel, image + site + 1u, 4);
    for (uint32_t i = 5; i < length; i++) if (image[site + i] != 0x90) return 0;
    return image[site] == 0xE9 && (uintptr_t)image + site + 5u + (intptr_t)rel == to;
}

int main(void)
{
    uint8_t* image = Reset("the check");
    const uintptr_t game = (uintptr_t)image;
    const CoDistanceSite* sites = co_distance_sites();
    DWORD old;
    CHECK(co_rows_check(game) == NULL && g_fake.writes == 0 && co_rows_city_ratio() == 1.0f);
    {
        const uint32_t readers[3] = {CO_ROW_RADII_SITE, CO_ROW_OUTPOSTS_SITE + 7u, CO_ROW_OBJECTIVES_SITE + 5u};
        for (int i = 0; i < 3; i++)
        {
            image = Reset("a reader is another game's");
            image[readers[i]] ^= 0x10;
            CHECK(co_rows_check((uintptr_t)image) != NULL && co_rows_install((uintptr_t)image) != NULL && g_fake.writes == 0);
        }
        for (int i = 0; i < CO_DISTANCE_SITES; i++)
        {
            image = Reset("a distance read is another game's");
            image[sites[i].rva + sites[i].at - 1u] ^= 0x10;
            CHECK(co_rows_check((uintptr_t)image) != NULL);
            image = Reset("a distance read has another table");
            image[sites[i].rva + sites[i].at] ^= 0x40;
            CHECK(co_rows_check((uintptr_t)image) != NULL && co_rows_install((uintptr_t)image) != NULL && g_fake.writes == 0);
        }
        image = Reset("the game's own distance table");
        const int32_t own = (int32_t)CO_DISTANCE_GAME; /* no row 11 behind it */
        for (int i = 0; i < CO_DISTANCE_SITES; i++) memcpy(image + sites[i].rva + sites[i].at, &own, 4);
        CHECK(co_rows_check((uintptr_t)image) != NULL);
        image = Reset("a table whose first ten rows are not the game's");
        image[FAKE_STOCK_DISTANCE + 12u * 4u + 2u] ^= 1;
        CHECK(co_rows_check((uintptr_t)image) != NULL);
        image = Reset("no level 10 row");
        g_fake.levelTen = 0;
        CHECK(co_rows_install((uintptr_t)image) != NULL && g_fake.writes == 0);
    }

    image = Reset("the install");
    uint8_t* before = (uint8_t*)malloc(FAKE_IMAGE_SIZE);
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_rows_install(game) == NULL && g_fake.writes == CO_DISTANCE_SITES + 3);
    const uintptr_t code = co_rows_test_code();
    const float* distance = co_rows_test_distance();
    CHECK(code != 0 && distance != NULL);
    CHECK(Jumps(image, CO_ROW_RADII_SITE, code, 8) && Jumps(image, CO_ROW_OUTPOSTS_SITE, code + 0x80u, 8) && Jumps(image, CO_ROW_OBJECTIVES_SITE, code + 0x100u, 6));
    for (int i = 0; i < CO_DISTANCE_SITES; i++)
    {
        int32_t disp; memcpy(&disp, image + sites[i].rva + sites[i].at, 4);
        CHECK(game + (intptr_t)disp == (uintptr_t)distance && memcmp(image + sites[i].rva, sites[i].bytes, sites[i].at) == 0);
    }
    /* HellForge's table and level 10's row are read, never written; the copy is its eleven rows */
    CHECK(memcmp(image + FAKE_STOCK_DISTANCE, before + FAKE_STOCK_DISTANCE, 0x1000) == 0 && memcmp(image + FAKE_STOCK_ROW_TEN, before + FAKE_STOCK_ROW_TEN, 0x330) == 0);
    CHECK(memcmp(distance, image + FAKE_STOCK_DISTANCE, 33 * sizeof(float)) == 0);
    CHECK(co_rows_install(game) == NULL && g_fake.writes == CO_DISTANCE_SITES + 3); /* once */
    /* level 10's city factor by the map's growth, over the complex row's */
    CHECK(co_rows_city_ratio() == 0.85f * 2.5f / (0.95f / 0.38f));

    g_case = "the distance row";
    co_rows_level(1);
    for (int k = 0; k < 3; k++) CHECK(distance[30 + k] == 0.95f / 0.38f); /* 0.95 of half the world on every map kind */
    CHECK(memcmp(distance, image + FAKE_STOCK_DISTANCE, 30 * sizeof(float)) == 0);
    co_rows_level(0);
    CHECK(memcmp(distance, image + FAKE_STOCK_DISTANCE, 33 * sizeof(float)) == 0 && distance[30] == 1.7f); /* HellForge's level 11 again */

    /* the stubs as the game runs them */
    CHECK(VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old));
    image[CO_ROW_RADII_BACK] = 0xC3; image[CO_ROW_OUTPOSTS_BACK] = 0xC3; image[CO_ROW_OBJECTIVES_BACK] = 0xC3;
    const float key = co_disk_key(), hellforge = 1022.0f / 1024.0f;
    uint8_t* level = (uint8_t*)calloc(1, 0x2E00);
    uint8_t store[0x40];
    {
        g_case = "the radii, run";
        float frame[80];                      /* rbp at +0x80 bytes: the copy's +0xF0 is [rbp+0x70] */
        const float row[4] = {0.4f, 0.5f, 0.6f, 0.7f};
        Reader r = {(uint8_t*)frame + 0x80, level, store, NULL, code};
        memset(frame, 0, sizeof frame); memcpy((uint8_t*)frame + 0xF0, row, sizeof row);
        memcpy(level + 0x2D7C, &hellforge, 4);
        Run(image, &r);
        CHECK(memcmp((uint8_t*)frame + 0xF0, row, sizeof row) == 0); /* HellForge's level 11: its row's radii */
        memcpy(level + 0x2D7C, &key, 4);
        Run(image, &r);
        const float* got = (const float*)((uint8_t*)frame + 0xF0);
        CHECK(got[0] == 1.25f && got[1] == 1.5625f && got[2] == 1.875f && got[3] == 2.1875f); /* level 10's x 2.5 */
        CHECK(frame[0xEC / 4] == 0.0f && frame[0x100 / 4] == 0.0f);
    }
    {
        g_case = "the outposts, run";
        uint32_t frame[0x200 / 4], want[40];  /* rbp at +0: the copy at rbp+0xD0, its lists from +0x34 */
        void* pass[4] = {0, 0, 0, level};
        Reader r = {frame, NULL, store, pass, code + 0x80u};
        for (uint32_t i = 0; i < 0x200 / 4; i++) frame[i] = 100u + i;
        for (uint32_t i = 0; i < 40; i++) want[i] = frame[0x104 / 4 + i];
        memcpy(level + 0x2D7C, &hellforge, 4);
        Run(image, &r);
        CHECK(memcmp(frame + 0x104 / 4, want, sizeof want) == 0);
        memcpy(level + 0x2D7C, &key, 4);
        Run(image, &r);
        for (uint32_t i = 0; i < 40; i++) CHECK(frame[0x104 / 4 + i] == 3u * want[i]);
        CHECK(frame[0x100 / 4] == 100u + 0x100 / 4 && frame[0x1A4 / 4] == 100u + 0x1A4 / 4); /* the lists and no more */
    }
    {
        g_case = "the objectives, run";
        /* {honours, level, seed, optional, major} -> {optional, major} */
        static const uint32_t cases[][7] = {
            {1, 11, 0x1235u, 4, 2, 12, 6},  /* three missions' */
            {1, 11, 0x1235u, 9, 5, 3, 15},  /* the optional ones take what the major ones leave */
            {1, 11, 0x1235u, 8, 7, 0, 18},  /* 18 places and no more */
            {1, 11, 0x1234u, 4, 2, 4, 2},   /* a seed without the mark */
            {1, 10, 0x1235u, 4, 2, 4, 2},   /* another level */
            {0, 11, 0x1235u, 4, 2, 4, 2}};  /* a machine that makes no complex mission now */
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        {
            uint32_t frame[32], inputs[8] = {cases[i][2], 0, 0, 0, cases[i][1]};
            Reader r = {(uint8_t*)frame + 0x40, NULL, store, inputs, code + 0x100u}; /* rbp-0x38 optional, rbp-0x34 major */
            memset(frame, 0x55, sizeof frame);
            frame[2] = cases[i][3]; frame[3] = cases[i][4];
            co_rows_honours((int)cases[i][0]);
            Run(image, &r);
            CHECK(frame[2] == cases[i][5] && frame[3] == cases[i][6] && frame[1] == 0x55555555u && frame[4] == 0x55555555u);
        }
        co_rows_honours(0);
    }
    free(level);

    for (int fail = 0; fail < CO_DISTANCE_SITES + 3; fail++)
    {
        image = Reset("a site that cannot be written");
        memcpy(before, image, FAKE_IMAGE_SIZE);
        g_fake.failWriteAt = fail;
        CHECK(co_rows_install((uintptr_t)image) != NULL && co_rows_test_code() == 0 && co_rows_test_distance() == NULL);
        CHECK(memcmp(before, image, FAKE_IMAGE_SIZE) == 0); /* the game's bytes and HellForge's table again */
        co_rows_level(1); co_rows_honours(1);               /* nothing to write to */
        CHECK(memcmp(before, image, FAKE_IMAGE_SIZE) == 0);
    }
    free(before);

    if (g_failed) { printf("rows: %d checks failed\n", g_failed); return 1; }
    printf("rows: all cases passed\n");
    return 0;
}
