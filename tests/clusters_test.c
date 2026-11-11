/* The location clusters' two counts with the mod's stubs in front of HellForge's: a pass with HellForge's 24 runs
 * HellForge's stub and gets its count to the bit, a complex level's pass counts by 3, and a refused install leaves
 * both jumps HellForge's. Every count is run as the generator runs it. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_clusters.h"
#include "xl/co_cluster_sites.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* Reset(const char* name)
{
    DWORD old;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_clusters_test_reset();
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    /* the game behind each count: back to the test */
    for (int i = 0; i < 2; i++) g_fake.image[kCoClusterSites[i].back] = 0xC3;
    VirtualProtect(g_fake.image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    return g_fake.image;
}
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

/* Path A (0xF7991F): r10d the kind's locations, edx the settings' draw, r9d the list's places, r15d the clusters so
 * far; the count leaves in edx. */
static uint32_t RunA(uintptr_t at, uint32_t locations, uint32_t draw, uint32_t places, uint32_t used)
{
    register uint64_t r10 __asm__("r10") = locations;
    register uint64_t r9 __asm__("r9") = places;
    register uint64_t r15 __asm__("r15") = used;
    uint64_t d = draw;
    __asm__ volatile("call *%[at]" : "+d" (d), "+r" (r10) : [at] "r" (at), "r" (r9), "r" (r15) : "rax", "rcx", "r8", "r11", "cc", "memory");
    return (uint32_t)d;
}
/* Path B (0xF79947): ecx the cap (4 or fewer), r10d the locations, rax the list's places, which the game reads behind
 * the site; the count leaves in r10d. */
static uint32_t RunB(uintptr_t at, uint32_t locations, uint32_t cap, uint64_t* keeps)
{
    register uint64_t r10 __asm__("r10") = locations;
    uint64_t c = cap, a = *keeps;
    __asm__ volatile("call *%[at]" : "+c" (c), "+a" (a), "+r" (r10) : [at] "r" (at) : "rdx", "r8", "r11", "cc", "memory");
    *keeps = a;
    return (uint32_t)r10;
}

static uint32_t Ceil(uint32_t n, uint32_t per) { return (n + per - 1u) / per; }
static uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t Max(uint32_t a, uint32_t b) { return a > b ? a : b; }
/* HellForge's rule (its difficulty_block.c): at least ceil(n / 24) within the places left, never below the game's own. */
static uint32_t StockA(uint32_t n, uint32_t draw, uint32_t places, uint32_t used) { return Max(Min(draw, n), Min(Ceil(n, 24), places - used)); }
static uint32_t StockB(uint32_t n, uint32_t cap) { return Min(n, Max(cap, Ceil(n, 24))); }
/* The complex level's: at least ceil(n / 3), and the whole count within the places left. */
static uint32_t OwnA(uint32_t n, uint32_t draw, uint32_t places, uint32_t used) { return Min(Max(Min(draw, n), Ceil(n, 3)), places - used); }
static uint32_t OwnB(uint32_t n, uint32_t cap) { return Min(n, Max(cap, Ceil(n, 3))); }

static const uint32_t kLocations[] = {0, 1, 2, 3, 4, 5, 6, 7, 23, 24, 25, 47, 48, 49, 64, 72, 73, 96, 97, 300, 767, 768, 769, 2740, 65024};
static const uint32_t kDraws[] = {0, 1, 2, 3, 4, 5, 8, 31, 32, 33, 100};
static const uint32_t kUsed[] = {0, 1, 5, 16, 29, 31, 32};
#define COUNT(a) (sizeof(a) / sizeof(a)[0])

/* Both sites run from the game's own addresses over every case; 0 when each count is the rule's. */
static int Wrong(const uint8_t* image, int complex)
{
    int wrong = 0;
    for (size_t n = 0; n < COUNT(kLocations); n++)
    {
        for (size_t d = 0; d < COUNT(kDraws); d++)
            for (size_t u = 0; u < COUNT(kUsed); u++)
            {
                const uint32_t want = complex ? OwnA(kLocations[n], kDraws[d], 32, kUsed[u]) : StockA(kLocations[n], kDraws[d], 32, kUsed[u]);
                wrong += RunA((uintptr_t)image + kCoClusterSites[0].site, kLocations[n], kDraws[d], 32, kUsed[u]) != want;
            }
        for (uint32_t cap = 0; cap <= 4; cap++)
        {
            uint64_t keeps = 0x1122334400000020ull;
            const uint32_t want = complex ? OwnB(kLocations[n], cap) : StockB(kLocations[n], cap);
            wrong += RunB((uintptr_t)image + kCoClusterSites[1].site, kLocations[n], cap, &keeps) != want || keeps != 0x1122334400000020ull;
        }
    }
    return wrong;
}

int main(void)
{
    uint8_t* image = Reset("HellForge's counts");
    uintptr_t game = (uintptr_t)image, stubs[2] = {0, 0};
    CHECK(CO_CLUSTER_STOCK_ADD == 23u && CO_CLUSTER_STOCK_SHIFT == 36u && CO_CLUSTER_OWN_ADD == 2u && CO_CLUSTER_OWN_SHIFT == 33u);
    CHECK(co_clusters_check(game, stubs) == NULL && g_fake.writes == 0);
    CHECK(stubs[0] == game + FAKE_STOCK_CLUSTERS && stubs[1] == game + FAKE_STOCK_CLUSTERS + 0x40u);
    CHECK(Wrong(image, 0) == 0);   /* the harness's HellForge counts by HellForge's rule */
    CHECK(co_clusters_per() == 24u);
    co_clusters_pass(1);           /* nothing before the install */
    CHECK(co_clusters_per() == 24u && Wrong(image, 0) == 0 && g_fake.writes == 0);
    {
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        CHECK(co_clusters_check((uintptr_t)g_fake.image, NULL) != NULL); /* a game without HellForge's level 11 */
    }
    for (int i = 0; i < 2; i++)
    {
        const CoClusterSite* c = &kCoClusterSites[i];
        const uint32_t stub = FAKE_STOCK_CLUSTERS + 0x40u * (uint32_t)i;
        image = Reset("the game's own count");
        memcpy(image + c->site, c->game, c->length);
        CHECK(co_clusters_check((uintptr_t)image, NULL) != NULL && co_clusters_install((uintptr_t)image) != NULL);
        image = Reset("HellForge's jump is shorter");
        image[c->site + c->length - 1u] = 0xCC;
        CHECK(co_clusters_check((uintptr_t)image, NULL) != NULL);
        image = Reset("HellForge's stub divides by another number");
        for (uint32_t k = 0; k + 4u <= c->stockSize; k++) if (memcmp(image + stub + k, "\x48\xC1\xE8\x24", 4) == 0) image[stub + k + 3u] = 0x23;
        CHECK(co_clusters_check((uintptr_t)image, NULL) != NULL);
        image = Reset("HellForge's stub ends elsewhere");
        image[stub + c->stockSize - 4u] ^= 1;
        CHECK(co_clusters_check((uintptr_t)image, NULL) != NULL && co_clusters_install((uintptr_t)image) != NULL);
        CHECK(g_fake.writes == 0 && co_clusters_test_page() == NULL);
    }

    image = Reset("the install");
    game = (uintptr_t)image;
    static uint8_t before[0x1000], stock[0x100];
    memcpy(before, image + 0xF79000u, sizeof before); memcpy(stock, image + FAKE_STOCK_CLUSTERS, sizeof stock);
    CHECK(co_clusters_install(game) == NULL && g_fake.writes == 2 && co_clusters_test_page() != NULL);
    const uint8_t* page = co_clusters_test_page();
    const uint8_t* per = page + 0x1000;
    for (int i = 0; i < 2; i++)
    {
        const CoClusterSite* c = &kCoClusterSites[i];
        const uint8_t* s = page + CO_CLUSTER_SLOT * (unsigned)i;
        /* only the jump's target changed; HellForge's stub is not written */
        CHECK(image[c->site] == 0xE9 && Leads(image + c->site + 1u) == (uintptr_t)s);
        CHECK(memcmp(image + c->site + 5u, before + (c->site - 0xF79000u) + 5u, c->length - 5u) == 0);
        /* cmp dword [per], 23; je HellForge's stub; then the complex count, reading both numbers, back into the game */
        CHECK(s[0] == 0x83 && s[1] == 0x3D && Leads(s + 2) + 1u == (uintptr_t)per && s[6] == 23 && s[7] == 0x0F && s[8] == 0x84);
        CHECK(Leads(s + 9) == game + FAKE_STOCK_CLUSTERS + 0x40u * (unsigned)i);
        const uint8_t* own = s + CO_CLUSTER_HEAD;
        CHECK(Leads(own + c->addAt) == (uintptr_t)per && Leads(own + c->shiftAt) == (uintptr_t)per + 4u && Leads(own + c->ownSize - 4u) == game + c->back);
        CHECK(s[CO_CLUSTER_HEAD + c->ownSize] == 0xCC && CO_CLUSTER_HEAD + c->ownSize <= CO_CLUSTER_SLOT);
    }
    CHECK(memcmp(stock, image + FAKE_STOCK_CLUSTERS, sizeof stock) == 0);
    CHECK(co_clusters_install(game) == NULL && g_fake.writes == 2); /* once */

    g_case = "a normal level's pass";
    CHECK(co_clusters_per() == 24u && Wrong(image, 0) == 0);
    co_clusters_pass(0);
    CHECK(co_clusters_per() == 24u && Wrong(image, 0) == 0);
    g_case = "a complex level's pass";
    co_clusters_pass(1);
    CHECK(co_clusters_per() == 3u && Wrong(image, 1) == 0);
    /* 64 locations of a kind: 22 clusters where HellForge's rule makes the draw's 4 */
    CHECK(RunA(game + kCoClusterSites[0].site, 64, 4, 32, 0) == 22u && StockA(64, 4, 32, 0) == 4u);
    g_case = "the pass ended";
    co_clusters_pass(0);
    CHECK(co_clusters_per() == 24u && Wrong(image, 0) == 0);
    CHECK(RunA(game + kCoClusterSites[0].site, 64, 4, 32, 0) == 4u);

    for (int fail = 0; fail < 2; fail++)
    {
        image = Reset("a jump that cannot be written");
        memcpy(before, image + 0xF79000u, sizeof before);
        g_fake.failWriteAt = fail;
        CHECK(co_clusters_install((uintptr_t)image) != NULL && co_clusters_test_page() == NULL);
        CHECK(memcmp(before, image + 0xF79000u, sizeof before) == 0 && Wrong(image, 0) == 0); /* HellForge's jumps again */
    }

    if (g_failed) { printf("clusters: %d checks failed\n", g_failed); return 1; }
    printf("clusters: all cases passed\n");
    return 0;
}
