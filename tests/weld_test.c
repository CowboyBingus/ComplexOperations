/* The vertex weld's pool: laid out for 256 a block beside HellForge's level 11, a block's places read per level (16 on
 * a normal one), the endpoint's lookup the game's unless the level is complex; refused where a site is another's. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_weld.h"
#include "xl/co_weld_sites.h"
#include "xl/co_normal.h"

void co_weld_test_reset(void);
const uint8_t* co_weld_test_page(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define VALUES (sizeof kCoWeldValues / sizeof kCoWeldValues[0])
#define SPAN 0xFB9000u
static uint8_t g_before[0x3000]; /* every site lies in 0xFB9000..0xFBC000 */

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_weld_test_reset();
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    memcpy(g_before, g_fake.image + SPAN, sizeof g_before);
    return g_fake.image;
}
static int Unchanged(void) { return memcmp(g_before, g_fake.image + SPAN, sizeof g_before) == 0; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

static int g_gameCalls;
static uint64_t Game(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { g_gameCalls++; return 7; }

int main(void)
{
    uint8_t* image = Reset("beside HellForge's level 11");
    uintptr_t game = (uintptr_t)image;
    /* a block: count, 256 vertices of 0x98, 256 pointers; 576 of them, then the count the game's 16-vertex pool had at +0x14CA60 */
    CHECK(CO_WELD_POINTERS == 8u + 256u * 0x98u && CO_WELD_BLOCK == CO_WELD_POINTERS + 256u * 8u && CO_WELD_COUNT == CO_WELD_POOL + 576u * CO_WELD_BLOCK);
    CHECK(co_level_limit(CO_LIMIT_WELD_VERTICES, 1) == CO_WELD_PER && co_level_limit(CO_LIMIT_WELD_VERTICES, 0) == 16u);
    CHECK(co_weld_check(game) == NULL && g_fake.writes == 0 && co_weld_limit() == 16u);
    co_weld_level(1);
    CHECK(co_weld_limit() == 16u); /* nothing before the install */
    CHECK(co_weld_install(game) == NULL && g_fake.writes == 16 && g_fake.hookCount == 1);
    const uint8_t* page = co_weld_test_page();
    CHECK(page != NULL);
    if (page != NULL)
    {
        const uint32_t* limit = (const uint32_t*)(page + 0x1000);
        for (size_t i = 0; i < VALUES; i++)
        {
            const CoWeldValue* v = &kCoWeldValues[i];
            CHECK(memcmp(image + v->rva, v->stock, v->at) == 0 && U32(image + v->rva + v->at) == v->own);
        }
        CHECK(U32(image + 0xFBBB58u + 1) == CO_WELD_COUNT + 8u && U32(image + 0xFBBBEFu + 2) == 576u * CO_WELD_BLOCK && U32(image + 0xFB9B9Au + 3) == CO_WELD_BLOCK);
        for (int i = 0; i < 3; i++)
        {
            const CoWeldFull* f = &kCoWeldFull[i];
            const uint8_t* s = page + 0x20 * i;
            const int wide = i < 2; /* cmp r8d has a prefix byte, cmp edx none */
            CHECK(image[f->rva] == 0xE9 && Leads(image + f->rva + 1) == (uintptr_t)s);
            for (unsigned k = 5; k < f->length; k++) CHECK(image[f->rva + k] == 0x90);
            CHECK(wide ? s[0] == 0x44 && s[1] == 0x3B && s[2] == 0x05 : s[0] == 0x3B && s[1] == 0x15);
            CHECK(Leads(s + 2 + wide) == (uintptr_t)limit);
            /* the game's own condition and its two ways: jb to room at the first, jae to full at the others */
            CHECK(s[6 + wide] == 0x0F && s[7 + wide] == (i == 0 ? 0x82 : 0x83) && (f->bytes[wide + 3] == 0x72 ? i == 0 : f->bytes[wide + 4] == 0x83));
            CHECK(Leads(s + 8 + wide) == game + f->taken && s[12 + wide] == 0xE9 && Leads(s + 13 + wide) == game + f->on && f->on == f->rva + f->length);
        }
        CHECK(*limit == 16u);
        co_weld_level(1); CHECK(*limit == 256u && co_weld_limit() == 256u);
        co_weld_level(0); CHECK(*limit == 16u);
        CHECK(g_fake.hooks[0].target == CO_WELD_ENDPOINT && g_fake.hooks[0].detour == (void*)(page + 0x80));
        /* the thunk keeps r9 around the call */
        CHECK(page[0x80] == 0x41 && page[0x81] == 0x51 && page[0x80 + 22] == 0x41 && page[0x80 + 23] == 0x59 && page[0x80 + 24] == 0xC3);
    }
    const int writes = g_fake.writes;
    CHECK(co_weld_install(game) == NULL && g_fake.writes == writes); /* once */

    /* A complex level's lookup, on a weld with two used blocks. */
    {
        static uint8_t made[4 * 0x98];
        struct { uint32_t count, cap; uint8_t* array; } nodes = {0, 2, made};
        uint8_t* out = NULL;
        g_case = "the endpoint's own block";
        /* the count lies 576 blocks behind the pool: a weld object of the real size */
        uint8_t* real = (uint8_t*)VirtualAlloc(NULL, CO_WELD_COUNT + 8u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK(real != NULL);
        if (real != NULL)
        {
            uint8_t* b = real + CO_WELD_POOL + CO_WELD_BLOCK;
            *(uint32_t*)(real + CO_WELD_COUNT) = 2u;
            *(uint32_t*)b = 200u;
            memset(b + 8 + 199 * 0x98, 0x5A, 0x98);
            g_gameCalls = 0;
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 8 + 199 * 0x98), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 1 && g_gameCalls == 0);
            CHECK(out == made && nodes.count == 1 && made[0] == 0x5A && made[0x97] == 0x5A && *(uint8_t**)(b + CO_WELD_POINTERS + 199 * 8) == made);
            out = NULL;
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 8 + 199 * 0x98), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 1 && out == made && nodes.count == 1); /* the same node again */
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 8 + 3 * 0x98), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 1 && out == made + 0x98 && nodes.count == 2);
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 8 + 4 * 0x98), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 0 && out == NULL && nodes.count == 2); /* no node left */
            /* not a vertex of a used block: the game's lookup decides */
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 8 + 200 * 0x98), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 7 && g_gameCalls == 1);
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(b + 9), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 7 && g_gameCalls == 2);
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)(real + CO_WELD_POOL + 2 * CO_WELD_BLOCK + 8), (uintptr_t)&nodes, (uintptr_t)&out, Game) == 7 && g_gameCalls == 3);
            CHECK(co_weld_endpoint((uintptr_t)real, (uintptr_t)made, (uintptr_t)&nodes, (uintptr_t)&out, Game) == 7 && g_gameCalls == 4);
            VirtualFree(real, 0, MEM_RELEASE);
        }
    }

    /* the game's own pool of 512 (no larger level 11), or another's code at any site */
    const uint32_t sites[] = {0xFBBB58u + 2, 0xFBBC10u + 3, 0xFB9B93u + 4, 0xFB9B9Au + 3, 0xFB9E88u + 1, 0xFBB0AFu + 4, 0xFB9C19u + 3, 0xFB9F9Du, 0xFBB074u + 2};
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++)
    {
        image = Reset("a site is not as HellForge leaves it");
        image[sites[i]] ^= 0x04;
        memcpy(g_before, image + SPAN, sizeof g_before);
        CHECK(co_weld_check((uintptr_t)image) != NULL && co_weld_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0 && g_fake.hookCount == 0);
    }
    image = Reset("the endpoint's lookup is another");
    g_fake.noMatch[0] = CO_WELD_ENDPOINT;
    CHECK(co_weld_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0);
    for (int fail = 0; fail < 16; fail++)
    {
        image = Reset("a write is refused");
        g_fake.failWriteAt = fail;
        CHECK(co_weld_install((uintptr_t)image) != NULL && Unchanged() && co_weld_test_page() == NULL && g_fake.hookCount == 0 && co_weld_limit() == 16u);
    }
    image = Reset("the hook is refused");
    g_fake.noHook = CO_WELD_ENDPOINT;
    CHECK(co_weld_install((uintptr_t)image) != NULL && Unchanged() && co_weld_test_page() == NULL);

    if (g_failed) { printf("weld: %d checks failed\n", g_failed); return 1; }
    printf("weld: all cases passed\n");
    return 0;
}
