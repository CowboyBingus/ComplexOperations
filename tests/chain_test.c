/* The mod's stubs in front of HellForge's keyed generator steps: HellForge's jump and stub are pinned at each site,
 * a level that is not complex runs HellForge's stub untouched, a complex one gets the constants of a 16384 m level,
 * and a refused install leaves every site as it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_chain.h"
#include "xl/co_normal.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define CODE 0x3200000u /* where a built page runs: a free place in the image */
#define FLAG 0x3201F00u

static uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t Leads(const uint8_t* image, uint32_t at) { int32_t d; memcpy(&d, image + at, 4); return at + 4u + (uint32_t)d; }

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_chain_test_reset();
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    return g_fake.image;
}

/* The spacing stub as the generator runs it: rax the settings' pair (the game's spacing at +4), r14 the pass, whose
 * +0x18 is the level. The site's way on is a `ret` in the image. */
static float RunSpacing(uintptr_t stub, float* pair, void** pass)
{
    float out = 0.0f;
    register void** r14 __asm__("r14") = pass;
    __asm__ volatile("call *%[stub]\n\tmovss %%xmm2, %[out]"
        : [out] "=m" (out), "+a" (pair)
        : [stub] "r" (stub), "r" (r14)
        : "xmm2", "cc", "memory");
    return out;
}

int main(void)
{
    g_case = "the values";
    float cells[CO_CELLS];
    co_chain_values(cells);
    /* the prototype's rules at map scale 20: border 7956.4, disk 8073.4; 651 m between route sites */
    CHECK(cells[CO_CELL_SPACING] == 651.0f && cells[CO_CELL_GRID] == 273.5f && cells[CO_CELL_QUARTER] == 0.25f);
    CHECK(Bits(cells[CO_CELL_FACTOR]) == 0x42454B90u && Bits(cells[CO_CELL_SQUARE]) == 0x425E9FAEu && Bits(cells[CO_CELL_VISTA]) == 0x414ECC24u);
    CHECK(Bits(co_disk_key()) == 0x3F7C4B33u);

    uint8_t* image = Reset("HellForge's steps");
    const uintptr_t game = (uintptr_t)image;
    const CoChain* chains = co_chain_sites();
    uintptr_t stubs[CO_CHAINS] = {0};
    CHECK(co_chain_check(game, stubs) == NULL && g_fake.writes == 0);
    for (int i = 0; i < CO_CHAINS; i++) CHECK(stubs[i] == game + fake_stock_stub(i) && chains[i].length >= 5 && chains[i].length <= 11);
    {
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        CHECK(co_chain_check((uintptr_t)g_fake.image, NULL) != NULL); /* a game without HellForge's level 11 */
    }
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &chains[i];
        const uint32_t stub = fake_stock_stub(i);
        image = Reset("HellForge's jump is another");
        image[c->site] = 0xE8;
        CHECK(co_chain_check((uintptr_t)image, NULL) != NULL);
        if (c->length > 5)
        {
            image = Reset("HellForge's jump is shorter");
            image[c->site + c->length - 1u] = 0xCC;
            CHECK(co_chain_check((uintptr_t)image, NULL) != NULL);
        }
        image = Reset("HellForge's stub is another");
        image[stub] ^= 1;                              /* its first instruction */
        CHECK(co_chain_check((uintptr_t)image, NULL) != NULL);
        image = Reset("HellForge's stub ends elsewhere");
        image[stub + c->stockSize - 4u] ^= 1;          /* its last jump's target */
        CHECK(co_chain_check((uintptr_t)image, NULL) != NULL);
        image = Reset("HellForge's map is another size");
        for (int k = 0; k < c->stockFillCount; k++)
            if (c->stockFills[k].kind == CO_FILL_KEY) { const float other = 800.0f / 1024.0f; memcpy(image + stub + c->stockFills[k].at, &other, 4); }
        const char* why = co_chain_check((uintptr_t)image, NULL);
        CHECK(why != NULL && strstr(why, "not the size") != NULL && g_fake.writes == 0);
    }

    image = Reset("the page");
    co_chain_check((uintptr_t)image, stubs);
    co_chain_build(image + CODE, (uintptr_t)image + CODE, (uintptr_t)image, (uintptr_t)image + FLAG, stubs);
    CHECK(memcmp(image + CODE + CO_CHAIN_CELLS, cells, sizeof cells) == 0);
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &chains[i];
        const uint32_t slot = CODE + (uint32_t)i * CO_CHAIN_SLOT, own = slot + CO_CHAIN_HEAD;
        const uint8_t* s = image + slot;
        /* flags kept, the switch tested, HellForge's stub when it is 0 */
        CHECK(s[0] == 0x9C && s[1] == 0x80 && s[2] == 0x3D && s[7] == 0 && Leads(image, slot + 3u) + 1u == FLAG);
        CHECK(s[8] == 0x75 && 10u + s[9] == 16u && s[10] == 0x9D && s[11] == 0xE9 && Leads(image, slot + 12u) == fake_stock_stub(i) && s[16] == 0x9D);
        CHECK(c->ownSize + CO_CHAIN_HEAD <= CO_CHAIN_SLOT && s[CO_CHAIN_HEAD + c->ownSize] == 0xCC);
        uint8_t body[96];
        int keys = 0, backs = 0;
        memcpy(body, image + own, c->ownSize);
        for (int k = 0; k < c->ownFillCount; k++)
        {
            const CoFill* f = &c->ownFills[k];
            uint32_t now; memcpy(&now, body + f->at, 4);
            if (f->kind == CO_FILL_KEY) { CHECK(now == Bits(co_disk_key())); keys++; }
            else if (f->kind == CO_FILL_GAME) { CHECK(Leads(image, own + f->at) == f->arg); backs += f->arg == c->site + c->length; }
            else if (f->kind == CO_FILL_CELL) CHECK(Leads(image, own + f->at) == CODE + CO_CHAIN_CELLS + 4u * f->arg && f->arg < CO_CELLS);
            else if (f->kind == CO_FILL_FLOAT) CHECK(now == Bits(cells[f->arg]));
            else CHECK(0); /* nothing of the mod's stub is left to HellForge */
            memset(body + f->at, 0, 4);
        }
        /* one test of the level's disk, at least one way back behind the site, and nothing else changed */
        CHECK(keys == 1 && backs >= 1 && memcmp(body, c->own, c->ownSize) == 0);
        /* where the mod's stub begins with the game's instruction, as HellForge's does: this many bytes of the site */
        static const uint8_t replayed[CO_CHAINS] = {5, 0, 9, 4, 9, 8, 8, 9, 0, 7, 0};
        CHECK(memcmp(c->own, c->game, replayed[i]) == 0 && memcmp(c->stock, c->game, replayed[i]) == 0);
    }
    /* the forms that are the mod's own */
    CHECK(chains[3].ownSize == 82 && chains[3].stockSize == 52 && Leads(image, CODE + 3u * CO_CHAIN_SLOT + CO_CHAIN_HEAD + 53u) == CODE + CO_CHAIN_CELLS + 4u * CO_CELL_SQUARE);
    CHECK(Leads(image, CODE + 4u * CO_CHAIN_SLOT + CO_CHAIN_HEAD + 34u) == CODE + CO_CHAIN_CELLS + 4u * CO_CELL_SQUARE);
    CHECK(chains[10].ownSize == 37 && Leads(image, CODE + 10u * CO_CHAIN_SLOT + CO_CHAIN_HEAD + 22u) == 0xF67FB7u);
    const float* pose = (const float*)(image + CODE + 9u * CO_CHAIN_SLOT + CO_CHAIN_HEAD + 80u);
    CHECK(pose[0] == cells[CO_CELL_VISTA] && pose[1] == cells[CO_CELL_VISTA] && pose[2] == 1.0f && pose[3] == 1.0f);

    g_case = "the spacing, run";
    {
        DWORD old;
        float pair[2] = {0.0f, 40.0f};      /* the game's spacing */
        uint8_t level[0x2E00] = {0};
        void* pass[4] = {0, 0, 0, level};
        const float hellforge = 1022.0f / 1024.0f, key = co_disk_key(), lower = 0.5f;
        const uintptr_t stub = (uintptr_t)image + CODE;
        image[chains[0].site + chains[0].length] = 0xC3; /* the game behind the site: back to the test */
        CHECK(VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old));
        image[FLAG] = 0;
        memcpy(level + 0x2D7C, &hellforge, 4);
        CHECK(RunSpacing(stub, pair, pass) == FAKE_STOCK_SPACING); /* HellForge's level 11: HellForge's value */
        memcpy(level + 0x2D7C, &lower, 4);
        CHECK(RunSpacing(stub, pair, pass) == 40.0f);              /* a lower level: the game's */
        memcpy(level + 0x2D7C, &key, 4);
        CHECK(RunSpacing(stub, pair, pass) == 40.0f);              /* the mod's disk without the switch: nobody's level */
        image[FLAG] = 1;
        CHECK(RunSpacing(stub, pair, pass) == 651.0f);             /* a complex level */
        memcpy(level + 0x2D7C, &hellforge, 4);
        CHECK(RunSpacing(stub, pair, pass) == 40.0f);              /* the switch without the disk: the game's */
        image[FLAG] = 0;
        CHECK(RunSpacing(stub, pair, pass) == FAKE_STOCK_SPACING);
    }

    image = Reset("the install");
    uint8_t* before = (uint8_t*)malloc(FAKE_IMAGE_SIZE);
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_chain_install((uintptr_t)image, (uintptr_t)image + FLAG) == NULL && g_fake.writes == CO_CHAINS && co_chain_test_page() != 0);
    const uintptr_t page = co_chain_test_page();
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &chains[i];
        int32_t rel; memcpy(&rel, image + c->site + 1u, 4);
        CHECK(image[c->site] == 0xE9 && (uintptr_t)image + c->site + 5u + (intptr_t)rel == page + (uintptr_t)i * CO_CHAIN_SLOT);
        CHECK(memcmp(image + c->site + 5u, before + c->site + 5u, c->length - 5u) == 0);
        CHECK(memcmp(image + fake_stock_stub(i), before + fake_stock_stub(i), 0x80) == 0); /* HellForge's stub is not written */
        CHECK(((const uint8_t*)page)[i * CO_CHAIN_SLOT] == 0x9C);
    }
    CHECK(co_chain_install((uintptr_t)image, (uintptr_t)image + FLAG) == NULL && g_fake.writes == CO_CHAINS); /* once */

    for (int fail = 0; fail < CO_CHAINS; fail++)
    {
        image = Reset("a jump that cannot be written");
        memcpy(before, image, FAKE_IMAGE_SIZE);
        g_fake.failWriteAt = fail;
        CHECK(co_chain_install((uintptr_t)image, (uintptr_t)image + FLAG) != NULL && co_chain_test_page() == 0);
        CHECK(memcmp(before, image, FAKE_IMAGE_SIZE) == 0); /* HellForge's jumps again */
    }
    image = Reset("HellForge's step is another at the install");
    image[fake_stock_stub(7) + 2] ^= 4;
    CHECK(co_chain_install((uintptr_t)image, (uintptr_t)image + FLAG) != NULL && g_fake.writes == 0 && co_chain_test_page() == 0);
    free(before);

    if (g_failed) { printf("chain: %d checks failed\n", g_failed); return 1; }
    printf("chain: all cases passed\n");
    return 0;
}
