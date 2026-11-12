/* A complex mission's director patrols switched off at their gate (co_patrols.h). */
#include <string.h>

#include <windows.h>

#include "co_patrols.h"

const uint8_t kCoPatrolCmp[7] = {0x44, 0x39, 0xA0, 0x14, 0x06, 0x00, 0x00};
const uint8_t kCoPatrolGate[6] = {0x0F, 0x83, 0xDC, 0x11, 0x00, 0x00};
const uint8_t kCoPatrolNext[3] = {0x45, 0x85, 0xED};
const uint8_t kCoPatrolSkip[3] = {0x45, 0x85, 0xED};

static struct { uint8_t* page; volatile uint8_t* off; } g_patrols;

const char* co_patrols_check(uintptr_t game)
{
    static const struct { uint32_t rva; const uint8_t* bytes; size_t size; } pins[] = {
        {CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp}, {CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate},
        {CO_PATROL_NEXT, kCoPatrolNext, sizeof kCoPatrolNext}, {CO_PATROL_SKIP, kCoPatrolSkip, sizeof kCoPatrolSkip}};
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++)
    {
        uint8_t now[8];
        if (g_coApi->read(game + pins[i].rva, now, pins[i].size) != 0 || memcmp(now, pins[i].bytes, pins[i].size) != 0)
            return "the director's patrol gate is not the game's code this mod was made for";
    }
    return NULL;
}

size_t co_patrols_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag)
{
    static const uint8_t stub[CO_PATROL_STUB] = {
        0x0F, 0x83, 0, 0, 0, 0,       /*  0 jae 0x95258A: the game's own test, flags from the cmp */
        0x80, 0x3D, 0, 0, 0, 0, 0x00, /*  6 cmp byte [rip+flag], 0 */
        0x0F, 0x85, 0, 0, 0, 0,       /* 13 jne 0x95258A */
        0xE9, 0, 0, 0, 0};            /* 19 jmp 0x9513AE */
    static const uint8_t at[3] = {2, 15, 20}, next[3] = {6, 19, 24};
    const uintptr_t to[3] = {game + CO_PATROL_SKIP, game + CO_PATROL_SKIP, game + CO_PATROL_NEXT};
    memcpy(out, stub, sizeof stub);
    for (int i = 0; i < 3; i++)
    {
        const int32_t rel = (int32_t)((int64_t)to[i] - (int64_t)(code + next[i]));
        memcpy(out + at[i], &rel, 4);
    }
    const int32_t rel = (int32_t)((int64_t)flag - (int64_t)(code + 13u));
    memcpy(out + 8, &rel, 4);
    return CO_PATROL_STUB;
}

const char* co_patrols_install(uintptr_t game)
{
    const char* why;
    DWORD old;
    if (g_patrols.page != NULL) return NULL;
    if ((why = co_patrols_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_PATROL_GATE, 0x2000);
    if (page == NULL) return "no memory near the game for the director's patrol gate";
    volatile uint8_t* off = page + 0x1000;
    *off = 0;
    memset(page, 0xCC, 0x1000);
    co_patrols_stub(page, (uintptr_t)page, game, (uintptr_t)off);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the patrol gate's stub could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    uint8_t jump[sizeof kCoPatrolGate] = {0xE9, 0, 0, 0, 0, 0x90};
    const int32_t rel = (int32_t)((int64_t)(uintptr_t)page - (int64_t)(game + CO_PATROL_GATE + 5u));
    memcpy(jump + 1, &rel, 4);
    if (g_coApi->write(game + CO_PATROL_GATE, jump, sizeof jump) != 0) { VirtualFree(page, 0, MEM_RELEASE); return "the director's patrol gate could not be written"; }
    g_patrols.page = page; g_patrols.off = off;
    return NULL;
}

void co_patrols_live(int off) { if (g_patrols.off != NULL) *g_patrols.off = (uint8_t)(off != 0); }

#ifdef CO_TEST
void co_patrols_test_reset(void) { memset(&g_patrols, 0, sizeof g_patrols); }
const uint8_t* co_patrols_test_page(void) { return g_patrols.page; }
#endif
