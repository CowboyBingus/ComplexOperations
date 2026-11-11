/* The chains in front of HellForge's keyed generator steps (co_chain.h). */
#include <math.h>
#include <string.h>

#include <windows.h>

#include "co_chain.h"
#include "co_normal.h"
#include "co_chain_sites.h"

static uint8_t* g_chainPage;

const CoChain* co_chain_sites(void) { return kCoChains; }

void co_chain_values(float cells[CO_CELLS])
{
    /* the border and the disk in metres, as the level's radii have them (co_complex_radii) */
    const float half = 8192.0f, border = 389.12f * CO_COMPLEX_MAP_SCALE + 174.0f;
    const float disk = border + 117.0f < half - 2.0f ? border + 117.0f : half - 2.0f;
    /* 1.2 x farther than the smaller maps' rule: at 16384 m the route edges ran past the 1621 the generator holds */
    cells[CO_CELL_SPACING] = ceilf(160.0f * 1.2f * sqrtf((0.8f * border * border + 0.2f * (disk + 1.0f) * (disk + 1.0f)) /
        (0.8f * 563.12f * 563.12f + 0.2f * 681.0f * 681.0f))) / 4.0f;
    /* half the sites: at this size the grid's corners split and GenerateVoronoi retried without end */
    cells[CO_CELL_GRID] = ceilf(16.0f * (disk + 1.0f) / 681.0f * 1.015f * 1.42f * 4.0f) / 4.0f;
    cells[CO_CELL_FACTOR] = (disk + 1.0f) / 681.0f * 1.04f * 4.0f;
    cells[CO_CELL_QUARTER] = 0.25f;
    /* map type 1 rebuilds the route cells over the whole square, 4 / pi of the disk's area */
    cells[CO_CELL_SQUARE] = cells[CO_CELL_FACTOR] * sqrtf(4.0f / 3.14159265f);
    cells[CO_CELL_VISTA] = (border + 57.0f < disk ? border + 57.0f : disk) / 620.0f;
}

static int32_t RelTo(uintptr_t target, uintptr_t end) { return (int32_t)((int64_t)target - (int64_t)end); }

const char* co_chain_check(uintptr_t game, uintptr_t stubs[CO_CHAINS])
{
    const float disk = 1022.0f / 1024.0f;
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &kCoChains[i];
        uint8_t site[16], stub[96];
        int32_t rel;
        if (g_coApi->read(game + c->site, site, c->length) != 0 || site[0] != 0xE9) return "a generator step of difficulty 11 is not in place";
        for (int k = 5; k < c->length; k++) if (site[k] != 0x90) return "a generator step of difficulty 11 is not in place";
        memcpy(&rel, site + 1, 4);
        const uintptr_t at = game + c->site + 5 + (intptr_t)rel;
        if (g_coApi->read(at, stub, c->stockSize) != 0) return "a generator step of difficulty 11 is not readable";
        for (int k = 0; k < c->stockFillCount; k++)
        {
            const CoFill* f = &c->stockFills[k];
            int32_t now;
            memcpy(&now, stub + f->at, 4);
            if (f->kind == CO_FILL_KEY && memcmp(&now, &disk, 4) != 0) return "difficulty 11's map is not the size this mod was built for";
            if (f->kind == CO_FILL_GAME && now != RelTo(game + f->arg, at + f->at + 4)) return "a generator step of difficulty 11 does not go back where the game goes on";
            memset(stub + f->at, 0, 4);
        }
        if (memcmp(stub, c->stock, c->stockSize) != 0) return "a generator step of difficulty 11 is not the one this mod was built for";
        if (stubs != NULL) stubs[i] = at;
    }
    return NULL;
}

void co_chain_build(uint8_t* page, uintptr_t code, uintptr_t game, uintptr_t flag, const uintptr_t stubs[CO_CHAINS])
{
    float cells[CO_CELLS];
    const float key = co_disk_key();
    co_chain_values(cells);
    memset(page, 0xCC, CO_CHAIN_PAGE);
    memcpy(page + CO_CHAIN_CELLS, cells, sizeof cells);
    for (int i = 0; i < CO_CHAINS; i++)
    {
        const CoChain* c = &kCoChains[i];
        uint8_t* s = page + i * CO_CHAIN_SLOT;
        const uintptr_t at = code + i * CO_CHAIN_SLOT;
        int32_t rel;
        /* the flags are the game's at some of these instructions: kept across the test */
        s[0] = 0x9C;                                                          /*  0 pushfq */
        s[1] = 0x80; s[2] = 0x3D; rel = RelTo(flag, at + 8); memcpy(s + 3, &rel, 4); s[7] = 0x00; /*  1 cmp byte [flag], 0 */
        s[8] = 0x75; s[9] = 0x06;                                             /*  8 jne 16 */
        s[10] = 0x9D;                                                         /* 10 popfq */
        s[11] = 0xE9; rel = RelTo(stubs[i], at + 16); memcpy(s + 12, &rel, 4); /* 11 jmp HellForge's stub */
        s[16] = 0x9D;                                                         /* 16 popfq */
        uint8_t* own = s + CO_CHAIN_HEAD;
        memcpy(own, c->own, c->ownSize);
        for (int k = 0; k < c->ownFillCount; k++)
        {
            const CoFill* f = &c->ownFills[k];
            const uintptr_t end = at + CO_CHAIN_HEAD + f->at + 4;
            if (f->kind == CO_FILL_KEY) memcpy(own + f->at, &key, 4);
            else if (f->kind == CO_FILL_FLOAT) memcpy(own + f->at, &cells[f->arg], 4);
            else if (f->kind == CO_FILL_GAME) { rel = RelTo(game + f->arg, end); memcpy(own + f->at, &rel, 4); }
            else if (f->kind == CO_FILL_CELL) { rel = RelTo(code + CO_CHAIN_CELLS + 4u * f->arg, end); memcpy(own + f->at, &rel, 4); }
        }
    }
}

static int Reaches(uintptr_t from, uintptr_t to)
{
    const int64_t d = (int64_t)to - (int64_t)from;
    return d > -0x7FF00000ll && d < 0x7FF00000ll;
}

const char* co_chain_install(uintptr_t game, uintptr_t flag)
{
    uintptr_t stubs[CO_CHAINS];
    const char* why;
    DWORD old;
    int done = 0;
    if (g_chainPage != NULL) return NULL;
    if ((why = co_chain_check(game, stubs)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game, CO_CHAIN_PAGE);
    if (page == NULL) return "no memory near the game for the generator's steps";
    if (!Reaches((uintptr_t)page, flag)) return "the generator's steps cannot reach their switch";
    for (int i = 0; i < CO_CHAINS; i++)
        if (!Reaches((uintptr_t)page, stubs[i]) || !Reaches((uintptr_t)page, game + kCoChains[i].site)) return "the generator's steps cannot reach difficulty 11's";
    co_chain_build(page, (uintptr_t)page, game, flag, stubs);
    if (!VirtualProtect(page, CO_CHAIN_PAGE, PAGE_EXECUTE_READ, &old)) return "the generator's steps could not be made to run";
    FlushInstructionCache(GetCurrentProcess(), page, CO_CHAIN_PAGE);
    for (; done < CO_CHAINS; done++)
    {
        const int32_t rel = RelTo((uintptr_t)page + done * CO_CHAIN_SLOT, game + kCoChains[done].site + 5);
        if (g_coApi->write(game + kCoChains[done].site + 1, &rel, 4) != 0) break;
    }
    if (done == CO_CHAINS) { g_chainPage = page; return NULL; }
    while (done-- > 0) /* HellForge's own jumps again: each stub passed every level on to them anyway */
    {
        const int32_t rel = RelTo(stubs[done], game + kCoChains[done].site + 5);
        g_coApi->write(game + kCoChains[done].site + 1, &rel, 4);
    }
    return "a generator step of difficulty 11 could not be chained";
}

uintptr_t co_chain_slot(int chain)
{
    return g_chainPage != NULL && chain >= 0 && chain < CO_CHAINS ? (uintptr_t)g_chainPage + (uintptr_t)chain * CO_CHAIN_SLOT : 0;
}

#ifdef CO_TEST
void co_chain_test_reset(void) { g_chainPage = NULL; }
uintptr_t co_chain_test_page(void) { return (uintptr_t)g_chainPage; }
#endif
