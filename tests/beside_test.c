/* The extra large world's sites beside HellForge's level 11: its jump is told from the game's bytes and from anything
 * else, its three stubs are known by their form, the city gates' pins take its jumps inside them and nothing else, and
 * a kept stamp goes on through its merge stub. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_beside.h"
#include "xl/co_beside_sites.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const uint8_t kLoop[10] = {0x48, 0x8B, 0x4F, 0x20, 0x45, 0x8B, 0xD5, 0x41, 0xFF, 0xC5};

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    return g_fake.image;
}
static uint32_t Leads(const uint8_t* image, uint32_t at) { int32_t d; memcpy(&d, image + at, 4); return at + 4u + (uint32_t)d; }

/* A pattern of `n` bytes that holds no E9, as text and in the image at `rva`. */
static void Pattern(uint8_t* image, uint32_t rva, size_t n, char* text)
{
    for (size_t i = 0; i < n; i++)
    {
        image[rva + i] = (uint8_t)(0x10 + i % 0x60);
        sprintf(text + 3 * i, "%02X ", image[rva + i]);
    }
    text[3 * n - 1] = 0;
}

int main(void)
{
    uint8_t* image = Reset("the game's bytes");
    const uintptr_t game = (uintptr_t)image;
    uintptr_t stub = 1;
    memcpy(image + CO_BESIDE_MERGE, kLoop, 10);
    memcpy(image + CO_BESIDE_MINE, kCoBesideMinePin, 6);
    memcpy(image + CO_BESIDE_STORE, kCoBesideStorePin, 7);
    CHECK(co_beside_form(game + CO_BESIDE_MERGE, kLoop, 10, 7, &stub) == CO_BESIDE_GAME && stub == 1);
    CHECK(co_beside_form(game + CO_BESIDE_MINE, kCoBesideMinePin, 6, 6, NULL) == CO_BESIDE_GAME);
    CHECK(co_beside_form(game + CO_BESIDE_STORE, kCoBesideStorePin, 7, 7, NULL) == CO_BESIDE_GAME);
    image[CO_BESIDE_MERGE + 8] ^= 1;
    CHECK(co_beside_form(game + CO_BESIDE_MERGE, kLoop, 10, 7, NULL) == CO_BESIDE_OTHER);

    image = Reset("HellForge's jumps and stubs");
    fake_stock_beside(image);
    CHECK(co_beside_form(game + CO_BESIDE_MERGE, kLoop, 10, 7, &stub) == CO_BESIDE_STOCK && stub == game + FAKE_STOCK_BESIDE);
    CHECK(co_beside_merge(game, stub) && !co_beside_mine(game, stub) && !co_beside_store(game, stub));
    CHECK(co_beside_form(game + CO_BESIDE_MINE, kCoBesideMinePin, 6, 6, &stub) == CO_BESIDE_STOCK && stub == game + FAKE_STOCK_BESIDE + 0x40u);
    CHECK(co_beside_mine(game, stub) && !co_beside_merge(game, stub));
    CHECK(co_beside_form(game + CO_BESIDE_STORE, kCoBesideStorePin, 7, 7, &stub) == CO_BESIDE_STOCK && stub == game + FAKE_STOCK_BESIDE + 0x80u);
    CHECK(co_beside_store(game, stub) && !co_beside_mine(game, stub));

    g_case = "another jump is not HellForge's";
    image[CO_BESIDE_MERGE + 6] = 0xCC; /* the fill behind the jump */
    CHECK(co_beside_form(game + CO_BESIDE_MERGE, kLoop, 10, 7, NULL) == CO_BESIDE_OTHER);
    image[CO_BESIDE_MERGE + 6] = 0x90; image[CO_BESIDE_MERGE + 9] ^= 1; /* the game's bytes behind it */
    CHECK(co_beside_form(game + CO_BESIDE_MERGE, kLoop, 10, 7, NULL) == CO_BESIDE_OTHER);

    g_case = "another stub is not HellForge's";
    {
        static const struct { uint32_t at; int which; } k[] = {
            {FAKE_STOCK_BESIDE + 9u, 0}, {FAKE_STOCK_BESIDE + 25u, 0}, {FAKE_STOCK_BESIDE + 30u, 0}, {FAKE_STOCK_BESIDE + 37u, 0},
            {FAKE_STOCK_BESIDE + 0x40u + 7u, 1}, {FAKE_STOCK_BESIDE + 0x40u + 14u, 1}, {FAKE_STOCK_BESIDE + 0x40u + 26u, 1},
            {FAKE_STOCK_BESIDE + 0x80u + 3u, 2}, {FAKE_STOCK_BESIDE + 0x80u + 6u, 2}, {FAKE_STOCK_BESIDE + 0x80u + 13u, 2}, {FAKE_STOCK_BESIDE + 0x80u + 25u, 2}};
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        {
            const uintptr_t s = game + FAKE_STOCK_BESIDE + 0x40u * (unsigned)k[i].which;
            image[k[i].at] ^= 0x10;
            CHECK(!(k[i].which == 0 ? co_beside_merge(game, s) : k[i].which == 1 ? co_beside_mine(game, s) : co_beside_store(game, s)));
            image[k[i].at] ^= 0x10;
            CHECK(k[i].which == 0 ? co_beside_merge(game, s) : k[i].which == 1 ? co_beside_mine(game, s) : co_beside_store(game, s));
        }
    }

    image = Reset("a pin with HellForge's jumps inside");
    {
        /* the city buildings' gate pin: 159 bytes from 0xF9954F, HellForge's border step at 0xF9959A and its refusal at 0xF995E8 */
        static char text[3 * 256];
        const uint32_t rva = 0xF9954Fu;
        static const uint8_t jump[6] = {0xE9, 0x11, 0x22, 0x33, 0x44, 0x90};
        Pattern(image, rva, 159, text);
        CHECK(co_beside_matches(game, rva, text));
        memcpy(image + 0xF9959Au, jump, 6);
        CHECK(co_beside_matches(game, rva, text));
        memcpy(image + 0xF995E8u, jump, 6);
        CHECK(co_beside_matches(game, rva, text));
        image[0xF995E8u + 5] = 0xCC; /* not its fill */
        CHECK(!co_beside_matches(game, rva, text));
        image[0xF995E8u + 5] = 0x90;
        memcpy(image + 0xF99570u, jump, 6); /* a jump where HellForge has none */
        CHECK(!co_beside_matches(game, rva, text));
        Pattern(image, rva, 159, text);
        image[rva + 158] ^= 1;
        CHECK(!co_beside_matches(game, rva, text));
        /* a jump that would end past the pin is not taken for one: the pin ends inside HellForge's site */
        Pattern(image, rva, 76, text);
        memcpy(image + 0xF9959Au, jump, 6);
        CHECK(!co_beside_matches(game, rva, text));
        CHECK(!co_beside_matches(game, rva, "") && !co_beside_matches(game, rva, "4G"));
    }

    image = Reset("a kept stamp's way on");
    {
        uint8_t tail[CO_BESIDE_TAIL];
        const uint32_t code = 0x3200039u;
        co_beside_place_tail(tail, game + code, game, 0);
        memcpy(image + code, tail, sizeof tail);
        CHECK(memcmp(tail, kCoBesideMergePin, 7) == 0 && tail[7] == 0xE9 && Leads(image, code + 8u) == CO_BESIDE_MERGE + 7u);
        co_beside_place_tail(tail, game + code, game, game + FAKE_STOCK_BESIDE);
        memcpy(image + code, tail, sizeof tail);
        CHECK(tail[0] == 0xE9 && Leads(image, code + 1u) == FAKE_STOCK_BESIDE && tail[5] == 0xCC && tail[11] == 0xCC);
    }

    if (g_failed) { printf("  beside: %d checks failed\n", g_failed); return 1; }
    printf("  beside: all cases passed\n");
    return 0;
}
