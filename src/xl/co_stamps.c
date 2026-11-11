/* The stamp index per level (co_stamps.h). HellForge's writes are pinned from its source (co_stamp_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_stamps.h"
#include "co_stamp_sites.h"
#include "co_normal.h"

static struct { uint8_t* page; uint32_t* counts; } g_stamps;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[64];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* The size's multiply with the mod's bytes a cell in it: what the site holds once the room is in. */
static int Roomy(uintptr_t game)
{
    uint8_t cell[6];
    const uint32_t bytes = CO_STAMP_CELL_BYTES;
    memcpy(cell, kCoStampCellGame, 2); memcpy(cell + 2, &bytes, 4);
    return Same(game + CO_STAMP_CELL_SITE, cell, sizeof cell);
}

const char* co_stamps_room_check(uintptr_t game)
{
    return Roomy(game) || Same(game + CO_STAMP_CELL_SITE, kCoStampCellGame, sizeof kCoStampCellGame) ? NULL : "the stamp index's size is not the game's code this mod was made for";
}

const char* co_stamps_room(uintptr_t game)
{
    const char* why;
    const uint32_t bytes = CO_STAMP_CELL_BYTES;
    if ((why = co_stamps_room_check(game)) != NULL) return why;
    if (Roomy(game)) return NULL;
    /* only the allocation and the clearing read the size: a larger block changes no index built in it */
    if (g_coApi->write(game + CO_STAMP_CELL_SITE + 2u, &bytes, 4) != 0) return "the stamp index's memory could not be enlarged";
    return NULL;
}

const char* co_stamps_check(uintptr_t game)
{
    if (!Roomy(game)) return "the stamp index's memory was not enlarged as the game started";
    if (!Same(game + CO_STAMP_ADD_SITE, kCoStampAddStock, sizeof kCoStampAddStock) || !Same(game + CO_STAMP_CHECK_SITE, kCoStampCheckStock, sizeof kCoStampCheckStock) ||
        !Same(game + CO_STAMP_SLOTS_SITE, kCoStampSlotsStock, sizeof kCoStampSlotsStock) || !Same(game + CO_STAMP_LAYOUT_SITE, kCoStampLayoutStock, sizeof kCoStampLayoutStock))
        return "the stamp index is not as difficulty 11 leaves it";
    return NULL;
}

static void Rel(uint8_t* out, size_t at, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + at + 4));
    memcpy(out + at, &rel, 4);
}

size_t co_stamps_slots_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t counts)
{
    static const uint8_t stub[CO_STAMP_SLOTS_STUB] = {
        0x44, 0x8B, 0x2D, 0, 0, 0, 0,                     /*  0 mov r13d, [rip -> slots] */
        0xE9, 0, 0, 0, 0};                                /*  7 jmp 0xC04C20 */
    memcpy(out, stub, sizeof stub);
    Rel(out, 3, code, counts);
    Rel(out, 8, code, game + CO_STAMP_SLOTS_BACK);
    return sizeof stub;
}

/* HellForge's instructions from 0xC04CB2 with its three immediates read instead: where the cells begin and the slot
 * count + 1 follow r13d's slots, the bitset's width is the level's. eax, edx = 1 and the flags leave as the game's. */
size_t co_stamps_layout_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t counts)
{
    static const uint8_t stub[CO_STAMP_LAYOUT_STUB] = {
        0x8B, 0x05, 0, 0, 0, 0,                           /*  0 mov eax, [rip -> slots] */
        0x48, 0x8D, 0x44, 0x81, 0x04,                     /*  6 lea rax, [rcx+rax*4+4] (the free list's end: the cells) */
        0x48, 0x89, 0x47, 0x38,                           /* 11 mov [rdi+0x38], rax */
        0x42, 0x8D, 0x04, 0x82,                           /* 15 lea eax, [rdx+r8*4] */
        0xF3, 0x44, 0x0F, 0x11, 0x47, 0x08,               /* 19 movss [rdi+8], xmm8 */
        0x41, 0x8D, 0x55, 0x01,                           /* 25 lea edx, [r13+1] */
        0x89, 0x57, 0x1C,                                 /* 29 mov [rdi+0x1c], edx */
        0xC7, 0x47, 0x24, 0x04, 0x00, 0x00, 0x00,         /* 32 mov dword [rdi+0x24], 4 */
        0x8B, 0x15, 0, 0, 0, 0,                           /* 39 mov edx, [rip -> qwords a cell] */
        0x89, 0x57, 0x28,                                 /* 45 mov [rdi+0x28], edx */
        0xBA, 0x01, 0x00, 0x00, 0x00,                     /* 48 mov edx, 1 */
        0xE9, 0, 0, 0, 0};                                /* 53 jmp 0xC04CE1 */
    memcpy(out, stub, sizeof stub);
    Rel(out, 2, code, counts);
    Rel(out, 41, code, counts + 4u);
    Rel(out, 54, code, game + CO_STAMP_LAYOUT_BACK);
    return sizeof stub;
}

/* The site's first instruction becomes a jump; what follows it up to the way back is never reached again. */
static int Take(uintptr_t site, size_t length, uintptr_t to)
{
    uint8_t jump[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    Rel(jump, 1, site, to);
    return g_coApi->write(site, jump, length);
}

const char* co_stamps_install(uintptr_t game)
{
    const char* why;
    DWORD old;
    if (g_stamps.page != NULL) return NULL;
    if ((why = co_stamps_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_STAMP_SLOTS_SITE, 0x2000);
    if (page == NULL) return "no memory near the game for the stamp index's steps";
    uint32_t* counts = (uint32_t*)(page + 0x1000); /* its own page: the stubs' page stops being writable */
    counts[0] = CO_STAMP_STOCK_SLOTS; counts[1] = CO_STAMP_STOCK_QWORDS;
    co_stamps_slots_stub(page, (uintptr_t)page, game, (uintptr_t)counts);
    co_stamps_layout_stub(page + 0x40, (uintptr_t)page + 0x40, game, (uintptr_t)counts);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the stamp index's steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    g_stamps.counts = counts;
    if (Take(game + CO_STAMP_SLOTS_SITE, sizeof kCoStampSlotsStock, (uintptr_t)page) == 0)
    {
        if (Take(game + CO_STAMP_LAYOUT_SITE, 7, (uintptr_t)page + 0x40) == 0) { g_stamps.page = page; return NULL; }
        g_coApi->write(game + CO_STAMP_SLOTS_SITE, kCoStampSlotsStock, sizeof kCoStampSlotsStock);
    }
    g_stamps.counts = NULL; /* the page stays: a thread may be inside the first stub */
    return "the stamp index's steps could not be written";
}

void co_stamps_level(int complex)
{
    if (g_stamps.page == NULL) return;
    /* the bitset's width first on the way up, last on the way down: the two are never 131071 slots in 0xA9 qwords */
    if (complex) { g_stamps.counts[1] = CO_STAMP_OWN_QWORDS; g_stamps.counts[0] = co_level_limit(CO_LIMIT_STAMP_SLOTS, 1); }
    else { g_stamps.counts[0] = co_level_limit(CO_LIMIT_STAMP_SLOTS, 0); g_stamps.counts[1] = CO_STAMP_STOCK_QWORDS; }
}

uint32_t co_stamps_slots(void) { return g_stamps.page != NULL ? g_stamps.counts[0] : CO_STAMP_STOCK_SLOTS; }
int co_stamps_complex(void) { return g_stamps.page != NULL && g_stamps.counts[0] != CO_STAMP_STOCK_SLOTS; }

#ifdef CO_TEST
void co_stamps_test_reset(void) { memset(&g_stamps, 0, sizeof g_stamps); }
const uint8_t* co_stamps_test_page(void) { return g_stamps.page; }
#endif
