/* A loot container's location on a complex mission (co_locate.h). */
#include <string.h>

#include <windows.h>

#include "co_locate.h"
#include "co_locate_sites.h"

static struct { uint8_t* page; uint32_t* at; int theirs; } g_locate;

/* 0 the game's instruction, 1 the game's with HellForge's offset, -1 neither. */
static int Form(uintptr_t game)
{
    uint8_t now[sizeof kCoLocateGame];
    const uint32_t moved = co_gen_at(HF_GEN_LOCATIONS_A);
    if (g_coApi->read(game + CO_LOCATE_SITE, now, sizeof now) != 0) return -1;
    if (memcmp(now, kCoLocateGame, sizeof now) == 0) return 0;
    return moved != 0 && memcmp(now, kCoLocateGame, 3) == 0 && memcmp(now + 3, &moved, 4) == 0 ? 1 : -1;
}

const char* co_locate_check(uintptr_t game)
{
    return Form(game) < 0 ? "the loot containers' location lookup is neither the game's code nor moved by difficulty 11" : NULL;
}

uint32_t co_locate_at(int complex)
{
    const uint32_t moved = co_gen_at(HF_GEN_LOCATIONS_A);
    return complex && moved != 0 ? moved : CO_LOCATE_GAME_AT;
}

size_t co_locate_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t at)
{
    /* the offset is below 2 GB, so the zero-extending load gives what the game's sign-extended displacement did */
    static const uint8_t stub[CO_LOCATE_STUB] = {0x8B, 0x0D, 0, 0, 0, 0, 0x4A, 0x8D, 0x0C, 0x11, 0xE9, 0, 0, 0, 0};
    int32_t rel = (int32_t)((int64_t)at - (int64_t)(code + 6u));
    memcpy(out, stub, sizeof stub);
    memcpy(out + 2, &rel, 4);
    rel = (int32_t)((int64_t)(game + CO_LOCATE_BACK) - (int64_t)(code + CO_LOCATE_STUB));
    memcpy(out + 11, &rel, 4);
    return CO_LOCATE_STUB;
}

const char* co_locate_install(uintptr_t game)
{
    uint8_t jump[sizeof kCoLocateGame] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    DWORD old;
    if (g_locate.page != NULL || g_locate.theirs) return NULL;
    const int form = Form(game);
    if (form < 0) return co_locate_check(game);
    if (form == 1) { g_locate.theirs = 1; return NULL; } /* every mission reads the moved array: HellForge's own answer */
    if (co_gen_at(HF_GEN_LOCATIONS_A) >= 0x80000000u) return "the location array's place is out of the lookup's reach";
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_LOCATE_SITE, 0x2000);
    if (page == NULL) return "no memory near the game for the loot containers' location lookup";
    uint32_t* at = (uint32_t*)(page + 0x1000);
    *at = co_locate_at(0);
    co_locate_stub(page, (uintptr_t)page, game, (uintptr_t)at);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the location lookup's step could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    const int32_t rel = (int32_t)((int64_t)(uintptr_t)page - (int64_t)(game + CO_LOCATE_SITE + 5u));
    memcpy(jump + 1, &rel, 4);
    if (g_coApi->write(game + CO_LOCATE_SITE, jump, sizeof jump) != 0) { VirtualFree(page, 0, MEM_RELEASE); return "the loot containers' location lookup could not be written"; }
    g_locate.page = page; g_locate.at = at;
    return NULL;
}

void co_locate_live(int complex) { if (g_locate.page != NULL) *g_locate.at = co_locate_at(complex); }

#ifdef CO_TEST
void co_locate_test_reset(void) { memset(&g_locate, 0, sizeof g_locate); }
const uint8_t* co_locate_test_page(void) { return g_locate.page; }
#endif
