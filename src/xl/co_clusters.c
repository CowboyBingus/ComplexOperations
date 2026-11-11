/* The location clusters' divisor per pass (co_clusters.h). HellForge's stubs are pinned from its source
 * (co_cluster_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_clusters.h"
#include "co_cluster_sites.h"

static struct { uint8_t* page; volatile uint32_t* per; } g_clusters;

static int32_t RelTo(uintptr_t target, uintptr_t end) { return (int32_t)((int64_t)target - (int64_t)end); }

const char* co_clusters_check(uintptr_t game, uintptr_t stubs[2])
{
    for (int i = 0; i < 2; i++)
    {
        const CoClusterSite* c = &kCoClusterSites[i];
        uint8_t site[9], stub[64];
        int32_t rel;
        if (g_coApi->read(game + c->site, site, c->length) != 0 || site[0] != 0xE9) return "the location clusters' count of difficulty 11 is not in place";
        for (int k = 5; k < c->length; k++) if (site[k] != 0x90) return "the location clusters' count of difficulty 11 is not in place";
        memcpy(&rel, site + 1, 4);
        const uintptr_t at = game + c->site + 5 + (intptr_t)rel;
        if (g_coApi->read(at, stub, c->stockSize) != 0) return "the location clusters' count of difficulty 11 is not readable";
        memcpy(&rel, stub + c->stockSize - 4, 4);
        if (rel != RelTo(game + c->back, at + c->stockSize)) return "the location clusters' count of difficulty 11 does not go back where the game goes on";
        memset(stub + c->stockSize - 4, 0, 4);
        if (memcmp(stub, c->stock, c->stockSize) != 0) return "the location clusters' count of difficulty 11 is not the one this mod was built for";
        if (stubs != NULL) stubs[i] = at;
    }
    return NULL;
}

size_t co_clusters_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t per, uintptr_t stock)
{
    const CoClusterSite* c = &kCoClusterSites[which];
    int32_t rel;
    /* the game's instruction here is a compare and both stubs begin with one or set the flags before they read them */
    out[0] = 0x83; out[1] = 0x3D; rel = RelTo(per, code + 7); memcpy(out + 2, &rel, 4); out[6] = CO_CLUSTER_STOCK_ADD; /* cmp dword [per], 23 */
    out[7] = 0x0F; out[8] = 0x84; rel = RelTo(stock, code + CO_CLUSTER_HEAD); memcpy(out + 9, &rel, 4);                /* je HellForge's stub */
    uint8_t* own = out + CO_CLUSTER_HEAD;
    const uintptr_t at = code + CO_CLUSTER_HEAD;
    memcpy(own, c->own, c->ownSize);
    rel = RelTo(per, at + c->addAt + 4); memcpy(own + c->addAt, &rel, 4);
    rel = RelTo(per + 4, at + c->shiftAt + 4); memcpy(own + c->shiftAt, &rel, 4);
    rel = RelTo(game + c->back, at + c->ownSize); memcpy(own + c->ownSize - 4, &rel, 4);
    return CO_CLUSTER_HEAD + c->ownSize;
}

static int Reaches(uintptr_t from, uintptr_t to)
{
    const int64_t d = (int64_t)to - (int64_t)from;
    return d > -0x7FF00000ll && d < 0x7FF00000ll;
}

const char* co_clusters_install(uintptr_t game)
{
    uintptr_t stubs[2];
    const char* why;
    DWORD old;
    if (g_clusters.page != NULL) return NULL;
    if ((why = co_clusters_check(game, stubs)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + kCoClusterSites[0].site, 0x2000);
    if (page == NULL) return "no memory near the game for the location clusters' count";
    volatile uint32_t* per = (volatile uint32_t*)(page + 0x1000); /* written per pass: not on the page that runs */
    per[0] = CO_CLUSTER_STOCK_ADD; per[1] = CO_CLUSTER_STOCK_SHIFT;
    memset(page, 0xCC, 0x1000);
    for (int i = 0; i < 2; i++)
    {
        if (!Reaches((uintptr_t)page, stubs[i]) || !Reaches((uintptr_t)page, game + kCoClusterSites[i].site))
        { VirtualFree(page, 0, MEM_RELEASE); return "the location clusters' count cannot reach difficulty 11's"; }
        co_clusters_stub(page + CO_CLUSTER_SLOT * (unsigned)i, (uintptr_t)page + CO_CLUSTER_SLOT * (unsigned)i, game, i, (uintptr_t)per, stubs[i]);
    }
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the location clusters' count could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    for (int i = 0; i < 2; i++)
    {
        const int32_t rel = RelTo((uintptr_t)page + CO_CLUSTER_SLOT * (unsigned)i, game + kCoClusterSites[i].site + 5);
        if (g_coApi->write(game + kCoClusterSites[i].site + 1, &rel, 4) == 0) continue;
        if (i == 1) /* HellForge's own jump again: the mod's stub passed every pass on to it anyway */
        {
            const int32_t back = RelTo(stubs[0], game + kCoClusterSites[0].site + 5);
            g_coApi->write(game + kCoClusterSites[0].site + 1, &back, 4);
        }
        return "the location clusters' count of difficulty 11 could not be chained";
    }
    g_clusters.per = per;
    g_clusters.page = page;
    return NULL;
}

void co_clusters_pass(int complex)
{
    if (g_clusters.page == NULL) return;
    /* the shift first on the way in and last on the way out: 23 with either shift is never read as a complex count */
    if (complex) { g_clusters.per[1] = CO_CLUSTER_OWN_SHIFT; g_clusters.per[0] = CO_CLUSTER_OWN_ADD; }
    else { g_clusters.per[0] = CO_CLUSTER_STOCK_ADD; g_clusters.per[1] = CO_CLUSTER_STOCK_SHIFT; }
}

uint32_t co_clusters_per(void) { return g_clusters.page != NULL ? g_clusters.per[0] + 1u : CO_CLUSTER_STOCK_ADD + 1u; }

#ifdef CO_TEST
void co_clusters_test_reset(void) { memset(&g_clusters, 0, sizeof g_clusters); }
const uint8_t* co_clusters_test_page(void) { return g_clusters.page; }
#endif
