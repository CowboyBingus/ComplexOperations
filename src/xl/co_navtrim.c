/* Stock's 8 x 8 navigation tiles on the grid of 32 x 32 (co_navtrim.h). The block: the point-to-cell stub at +0, the
 * set-up stub at +0x100 (both run, read only), the armed worlds at +0x1000 and the priority table at +0x1100. */
#include <string.h>

#include <windows.h>

#include "co_navtrim.h"

#define BLOCK 0x4000u
#define DATA 0x1000u
#define TABLE 0x1100u
#define CELLS 1024u

/* The engine's point-to-cell step, 0x7ACDD0..0x7ACE4A: every byte is copied into the armed path. */
const uint8_t kCoNavCell[0x7B] = {
    0xF3, 0x0F, 0x10, 0x41, 0x14, 0xF3, 0x0F, 0x5C, 0x41, 0x0C, 0xF3, 0x0F, 0x5C, 0x49, 0x0C, 0x0F, 0xB7, 0x01, 0xF3, 0x0F, 0x5C, 0x51, 0x10, 0x44,
    0x0F, 0xB7, 0x41, 0x02, 0xF3, 0x0F, 0x5E, 0xC8, 0x66, 0x0F, 0x6E, 0xC0, 0xFF, 0xC8, 0x0F, 0x5B, 0xC0, 0xF3, 0x0F, 0x59, 0xC8, 0xF3, 0x0F, 0x10,
    0x41, 0x18, 0xF3, 0x0F, 0x5C, 0x41, 0x10, 0xF3, 0x0F, 0x2C, 0xD1, 0xF3, 0x0F, 0x5E, 0xD0, 0x66, 0x41, 0x0F, 0x6E, 0xC0, 0x0F, 0x5B, 0xC0, 0xF3,
    0x0F, 0x59, 0xD0, 0xF3, 0x0F, 0x2C, 0xCA, 0x85, 0xD2, 0x7E, 0x07, 0x3B, 0xD0, 0x0F, 0x4F, 0xD0, 0xEB, 0x02, 0x33, 0xD2, 0x41, 0x8D, 0x40, 0xFF,
    0x85, 0xC9, 0x7E, 0x0D, 0x3B, 0xC8, 0x0F, 0x4F, 0xC8, 0xC1, 0xE1, 0x10, 0x0B, 0xCA, 0x8B, 0xC1, 0xC3, 0x33, 0xC9, 0xC1, 0xE1, 0x10, 0x0B, 0xCA,
    0x8B, 0xC1, 0xC3};
/* 0x7B7440: lea rdx, [rbx+0x6710]; call 0x7B31B0; mov [rsi+0xC4], eax; movzx edi, [rbx+0x6712]; movzx eax, [rbx+0x6710]. */
const uint8_t kCoNavSet[0x20] = {
    0x48, 0x8D, 0x93, 0x10, 0x67, 0x00, 0x00, 0xE8, 0x64, 0xBD, 0xFF, 0xFF, 0x89, 0x86, 0xC4, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0xBB, 0x12, 0x67, 0x00,
    0x00, 0x0F, 0xB7, 0x83, 0x10, 0x67, 0x00, 0x00};
/* The fill 0x7B31B0..0x7B32E6: its priorities are S1's table only while these bytes and constants are the engine's. */
const uint8_t kCoNavFill[0x137] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B, 0xDA, 0x0F, 0x29, 0x74, 0x24, 0x30, 0xF3, 0x0F, 0x10, 0x35, 0xDA, 0x67, 0xEF, 0x00, 0x45, 0x33,
    0xD2, 0x0F, 0xB7, 0x12, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xD9, 0x41, 0x0F, 0xB7, 0x43, 0x02, 0x66, 0x0F, 0x6E, 0xE2, 0x0F, 0x5B, 0xE4, 0x66, 0x0F,
    0x6E, 0xE8, 0xF3, 0x0F, 0x59, 0xE6, 0x0F, 0x5B, 0xED, 0xF3, 0x0F, 0x59, 0xEE, 0x85, 0xC0, 0x0F, 0x84, 0xE4, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x7C,
    0x24, 0x20, 0xF3, 0x0F, 0x10, 0x3D, 0x3E, 0x76, 0xEF, 0x00, 0x44, 0x0F, 0x29, 0x44, 0x24, 0x10, 0xF3, 0x44, 0x0F, 0x10, 0x05, 0xCF, 0x68, 0xEF,
    0x00, 0x44, 0x0F, 0x29, 0x0C, 0x24, 0xF3, 0x44, 0x0F, 0x10, 0x0D, 0x5D, 0x6B, 0xEF, 0x00, 0x90, 0x45, 0x33, 0xC0, 0x66, 0x85, 0xD2, 0x0F, 0x84,
    0x8C, 0x00, 0x00, 0x00, 0x66, 0x41, 0x0F, 0x6E, 0xC1, 0x0F, 0x28, 0xDD, 0x0F, 0x5B, 0xC0, 0x0F, 0xB7, 0xD2, 0xF3, 0x0F, 0x58, 0xC6, 0xF3, 0x0F,
    0x5C, 0xD8, 0x0F, 0x54, 0xDF, 0x66, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x0F, 0x6E, 0xC0, 0x0F, 0x28, 0xD4,
    0x0F, 0x5B, 0xC0, 0xF3, 0x0F, 0x58, 0xC6, 0xF3, 0x0F, 0x5C, 0xD0, 0x0F, 0x54, 0xD7, 0x0F, 0x2F, 0xD4, 0x77, 0x27, 0x0F, 0x2F, 0xDD, 0x77, 0x22,
    0xF3, 0x0F, 0x5E, 0xD4, 0x0F, 0x28, 0xC3, 0x41, 0x0F, 0x28, 0xC8, 0xF3, 0x0F, 0x5C, 0xCA, 0xF3, 0x0F, 0x5E, 0xC5, 0xF3, 0x0F, 0x5C, 0xC8, 0xF3,
    0x41, 0x0F, 0x59, 0xC9, 0xF3, 0x0F, 0x2C, 0xC9, 0xEB, 0x02, 0x33, 0xC9, 0x41, 0x0F, 0xAF, 0xD1, 0x33, 0xC0, 0x41, 0x03, 0xD0, 0x85, 0xC9, 0x0F,
    0x95, 0xC0, 0x41, 0xFF, 0xC0, 0x89, 0x14, 0xD3, 0x44, 0x03, 0xD0, 0x89, 0x4C, 0xD3, 0x04, 0x41, 0x0F, 0xB7, 0x13, 0x44, 0x3B, 0xC2, 0x75, 0x98,
    0x41, 0x0F, 0xB7, 0x4B, 0x02, 0x41, 0xFF, 0xC1, 0x44, 0x3B, 0xC9, 0x0F, 0x85, 0x57, 0xFF, 0xFF, 0xFF, 0x44, 0x0F, 0x28, 0x0C, 0x24, 0x44, 0x0F,
    0x28, 0x44, 0x24, 0x10, 0x0F, 0x28, 0x7C, 0x24, 0x20, 0x0F, 0x28, 0x74, 0x24, 0x30, 0x41, 0x8B, 0xC2, 0x48, 0x83, 0xC4, 0x40, 0x5B, 0xC3};
const uint32_t kCoNavFillConst[4][2] = {{0x16A99A0u, 0x3F000000u}, {0x16AA840u, 0x7FFFFFFFu}, {0x16A9AE0u, 0x40000000u}, {0x16A9D7Cu, 0x42480000u}};
const int32_t kCoNavStockPrio[64] = {
    12, 25, 37, 50, 50, 37, 25, 12,
    25, 37, 50, 62, 62, 50, 37, 25,
    37, 50, 62, 75, 75, 62, 50, 37,
    50, 62, 75, 87, 87, 75, 62, 50,
    50, 62, 75, 87, 87, 75, 62, 50,
    37, 50, 62, 75, 75, 62, 50, 37,
    25, 37, 50, 62, 62, 50, 37, 25,
    12, 25, 37, 50, 50, 37, 25, 12};

/* An armed world: the grid it is keyed by, stock's grid as the cell step reads it (+0 W, +2 H, +0xC low x, +0x10 low y,
 * +0x14 high x, +0x18 high y), and the world the set-up stub is keyed by. 0x40 bytes each. */
typedef struct
{
    volatile uintptr_t grid;
    uint16_t cells[2];
    uint32_t pad;
    float tile, low[2], high[2];
    uint32_t pad2;
    volatile uintptr_t world;
    uint8_t rest[0x10];
} Armed;
_Static_assert(sizeof(Armed) == 0x40, "the stubs step 0x40 a world");

static struct { uint8_t* block; uintptr_t exe; const char* refused; int in; } g_trim;
static uint8_t* g_reserved;

/* The dispatch: an armed grid goes to the copy below, any other runs the engine's first two instructions and goes on
 * at 0x7ACDDA. Only rax, rdx and rcx change before the engine's own code writes them again. */
static const uint8_t kCellHead[] = {
    0x48, 0x8D, 0x05, 0xF9, 0x0F, 0x00, 0x00, /*  0 lea rax, [rip -> armed] */
    0xBA, 0x04, 0x00, 0x00, 0x00,             /*  7 mov edx, 4 */
    0x48, 0x3B, 0x08,                         /*  c cmp rcx, [rax] */
    0x74, 0x17,                               /*  f je 28 */
    0x48, 0x83, 0xC0, 0x40,                   /* 11 add rax, 0x40 */
    0xFF, 0xCA,                               /* 15 dec edx */
    0x75, 0xF3,                               /* 17 jne c */
    0xF3, 0x0F, 0x10, 0x41, 0x14,             /* 19 movss xmm0, [rcx+0x14] (the engine's) */
    0xF3, 0x0F, 0x5C, 0x41, 0x0C,             /* 1e subss xmm0, [rcx+0xc] */
    0xE9, 0, 0, 0, 0,                         /* 23 jmp 0x7ACDDA */
    0x48, 0x8D, 0x48, 0x08};                  /* 28 lea rcx, [rax+8]: stock's grid */
#define CELL_BACK 0x24u
static const uint8_t kAdd12[6] = {0x05, 0x0C, 0x00, 0x0C, 0x00, 0xC3}; /* add eax, 0x000C000C; ret */

/* At 0x7B744C, eax the fill's count: an armed world gets the table and 64, any other the engine's store. rax, rcx, rdx
 * and the flags change; 0x7B7459, 0x7B746A, 0x7B746D and the imul at 0x7B7467 write each before it is read. */
static const uint8_t kSet[] = {
    0x48, 0x8D, 0x0D, 0xF9, 0x0E, 0x00, 0x00, /*  0 lea rcx, [rip -> armed] */
    0xBA, 0x04, 0x00, 0x00, 0x00,             /*  7 mov edx, 4 */
    0x48, 0x3B, 0x71, 0x28,                   /*  c cmp rsi, [rcx+0x28] */
    0x74, 0x13,                               /* 10 je 25 */
    0x48, 0x83, 0xC1, 0x40,                   /* 12 add rcx, 0x40 */
    0xFF, 0xCA,                               /* 16 dec edx */
    0x75, 0xF2,                               /* 18 jne c */
    0x89, 0x86, 0xC4, 0x00, 0x00, 0x00,       /* 1a mov [rsi+0xC4], eax (the engine's) */
    0xE9, 0, 0, 0, 0,                         /* 20 jmp 0x7B7452 */
    0x48, 0x8B, 0x8E, 0x08, 0x67, 0x00, 0x00, /* 25 mov rcx, [rsi+0x6708] */
    0x31, 0xD2,                               /* 2c xor edx, edx */
    0x48, 0x8D, 0x05, 0xCB, 0x0F, 0x00, 0x00, /* 2e lea rax, [rip -> table] */
    0x48, 0x8B, 0x04, 0xD0,                   /* 35 mov rax, [rax+rdx*8] */
    0x48, 0x89, 0x04, 0xD1,                   /* 39 mov [rcx+rdx*8], rax */
    0xFF, 0xC2,                               /* 3d inc edx */
    0x81, 0xFA, 0x00, 0x04, 0x00, 0x00,       /* 3f cmp edx, 1024 */
    0x75, 0xE7,                               /* 45 jne 2e */
    0xC7, 0x86, 0xC4, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, /* 47 mov dword [rsi+0xC4], 64 */
    0xE9, 0, 0, 0, 0};                        /* 51 jmp 0x7B7452 */
#define SET_BACK1 0x21u
#define SET_BACK2 0x52u

static int Rel(uint8_t* field, uintptr_t end, uintptr_t to)
{
    const int64_t d = (int64_t)to - (int64_t)end;
    if (d < INT32_MIN || d > INT32_MAX) return 0;
    const int32_t r = (int32_t)d;
    memcpy(field, &r, 4);
    return 1;
}

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[0x140];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

void co_navtrim_reserve(uintptr_t exe)
{
    if (exe != 0 && g_reserved == NULL && g_trim.block == NULL) g_reserved = (uint8_t*)co_alloc_near(exe, BLOCK);
}

static const char* Pins(uintptr_t exe)
{
    if (!Same(exe + CO_NAVTRIM_CELL, kCoNavCell, sizeof kCoNavCell)) return "the engine's point-to-cell step is not the code this mod was made for";
    if (!Same(exe + CO_NAVTRIM_SET, kCoNavSet, sizeof kCoNavSet)) return "the engine's navigation set-up is not the code this mod was made for";
    if (!Same(exe + CO_NAVTRIM_FILL, kCoNavFill, sizeof kCoNavFill)) return "the engine's tile priority fill is not the code this mod was made for";
    for (int i = 0; i < 4; i++)
        if (!Same(exe + kCoNavFillConst[i][0], (const uint8_t*)&kCoNavFillConst[i][1], 4)) return "the engine's tile priority constants are not the ones this mod was made for";
    return NULL;
}

/* The stubs' code: the dispatch, the engine's cell step on stock's grid with each ret made `add eax, 0x000C000C; ret`
 * (its one rel8 across the first ret, at +0x63, moves by 5), then the set-up stub. */
static int Build(uint8_t* b, uintptr_t at, uintptr_t exe) /* `b` the bytes, `at` where they run */
{
    uint8_t* body = b + sizeof kCellHead;
    memcpy(b, kCellHead, sizeof kCellHead);
    memcpy(body, kCoNavCell, 0x70);
    body[0x63] = (uint8_t)(kCoNavCell[0x63] + 5u);
    memcpy(body + 0x70, kAdd12, sizeof kAdd12);
    memcpy(body + 0x76, kCoNavCell + 0x71, 9);
    memcpy(body + 0x7F, kAdd12, sizeof kAdd12);
    memcpy(b + 0x100, kSet, sizeof kSet);
    Armed* armed = (Armed*)(b + DATA);
    memset(armed, 0, CO_NAVTRIM_WORLDS * sizeof *armed);
    int32_t* table = (int32_t*)(b + TABLE);
    for (uint32_t c = 0; c < CELLS; c++)
    {
        const uint32_t x = c % 32u, y = c / 32u;
        const int inner = x >= 12u && x < 20u && y >= 12u && y < 20u;
        table[2 * c] = (int32_t)c;
        table[2 * c + 1] = inner ? kCoNavStockPrio[(y - 12u) * 8u + (x - 12u)] : CO_NAVTRIM_OUTER - (int32_t)c;
    }
    return Rel(b + CELL_BACK, at + CELL_BACK + 4u, exe + CO_NAVTRIM_CELL + 10u) &&
        Rel(b + 0x100 + SET_BACK1, at + 0x100 + SET_BACK1 + 4u, exe + CO_NAVTRIM_SET_BACK) &&
        Rel(b + 0x100 + SET_BACK2, at + 0x100 + SET_BACK2 + 4u, exe + CO_NAVTRIM_SET_BACK);
}
#define CELL_CODE (sizeof kCellHead + 0x85u)

/* E9 and its rel32 over the first 5 bytes; the bytes behind stay the engine's (no branch lands in them). */
static int Jump(uintptr_t site, uintptr_t to)
{
    uint8_t jump[5] = {0xE9};
    return Rel(jump + 1, site + 5u, to) && g_coApi->write(site, jump, 5) == 0;
}

const char* co_navtrim_install(uintptr_t exe)
{
    DWORD old;
    if (g_trim.in) return NULL;
    if (exe == 0) return g_trim.refused = "no engine";
    if ((g_trim.refused = Pins(exe)) != NULL) return g_trim.refused;
    uint8_t* b = g_reserved != NULL ? g_reserved : (uint8_t*)co_alloc_near(exe, BLOCK);
    if (b == NULL) return g_trim.refused = "no memory within reach of helldivers2.exe for the tile stubs";
    g_reserved = NULL;
    g_trim.block = b; g_trim.exe = exe;
    if (!Build(b, (uintptr_t)b, exe)) return g_trim.refused = "the tile stubs are out of reach of helldivers2.exe";
    if (!VirtualProtect(b, DATA, PAGE_EXECUTE_READ, &old)) return g_trim.refused = "the tile stubs could not be made to run";
    FlushInstructionCache(GetCurrentProcess(), b, DATA);
    if (!Jump(exe + CO_NAVTRIM_SET_SITE, (uintptr_t)b + 0x100u)) return g_trim.refused = "the navigation set-up stub could not be written";
    if (!Jump(exe + CO_NAVTRIM_CELL, (uintptr_t)b))
    {
        g_coApi->write(exe + CO_NAVTRIM_SET_SITE, kCoNavSet + 0xC, 5);
        return g_trim.refused = "the point-to-cell stub could not be written";
    }
    g_trim.in = 1;
    g_trim.refused = NULL;
    return NULL;
}

/* The installed block still holds what Build wrote: both stubs' code and the priority table. */
static int Reviewed(void)
{
    static uint8_t want[TABLE + 8u * CELLS];
    const uint8_t* b = g_trim.block;
    return Build(want, (uintptr_t)b, g_trim.exe) && memcmp(b, want, CELL_CODE) == 0 && memcmp(b + 0x100, want + 0x100, sizeof kSet) == 0 &&
        memcmp(b + TABLE, want + TABLE, 8u * CELLS) == 0;
}

void co_navtrim_unpatched(uintptr_t at, uint8_t* bytes, size_t size)
{
    static const struct { uint32_t site, to; const uint8_t* engine; } kSites[] = {
        {CO_NAVTRIM_CELL, 0, kCoNavCell}, {CO_NAVTRIM_SET_SITE, 0x100, kCoNavSet + (CO_NAVTRIM_SET_SITE - CO_NAVTRIM_SET)}};
    if (!g_trim.in) return;
    for (size_t i = 0; i < sizeof kSites / sizeof kSites[0]; i++)
    {
        const uintptr_t site = g_trim.exe + kSites[i].site;
        uint8_t now[5], jump[5] = {0xE9};
        if (site + 5u <= at || site >= at + size) continue;
        if (g_coApi->read(site, now, 5) != 0 || !Rel(jump + 1, site + 5u, (uintptr_t)g_trim.block + kSites[i].to) || memcmp(now, jump, 5) != 0 || !Reviewed())
            continue;
        for (size_t k = 0; k < 5; k++)
            if (site + k >= at && site + k < at + size) bytes[site + k - at] = kSites[i].engine[k];
    }
}

const char* co_navtrim_refused(void) { return g_trim.in ? NULL : g_trim.refused != NULL ? g_trim.refused : "the tile stubs are not in"; }

void co_navtrim_disarm(uint32_t id)
{
    if (!g_trim.in || id >= CO_NAVTRIM_WORLDS) return;
    Armed* a = (Armed*)(g_trim.block + DATA) + id;
    a->grid = 0; /* the cell step first: no point is mapped by bounds the world no longer has */
    MemoryBarrier();
    a->world = 0;
    MemoryBarrier();
}

int co_navtrim_arm(uint32_t id, uintptr_t world, float low, float high)
{
    if (!g_trim.in || id >= CO_NAVTRIM_WORLDS || world == 0) return 1;
    co_navtrim_disarm(id);
    Armed* a = (Armed*)(g_trim.block + DATA) + id;
    a->cells[0] = 8; a->cells[1] = 8;
    a->tile = (high - low) / 8.0f;
    a->low[0] = low; a->low[1] = low; a->high[0] = high; a->high[1] = high;
    MemoryBarrier();
    a->world = world;
    MemoryBarrier();
    a->grid = world + 0x6710u;
    MemoryBarrier();
    return 0;
}

uintptr_t co_navtrim_armed(uint32_t id)
{
    if (!g_trim.in || id >= CO_NAVTRIM_WORLDS) return 0;
    const Armed* a = (const Armed*)(g_trim.block + DATA) + id;
    return a->grid != 0 ? a->world : 0;
}

#ifdef CO_TEST
void co_navtrim_test_reset(void)
{
    if (g_trim.block != NULL) VirtualFree(g_trim.block, 0, MEM_RELEASE);
    if (g_reserved != NULL) VirtualFree(g_reserved, 0, MEM_RELEASE);
    memset(&g_trim, 0, sizeof g_trim);
    g_reserved = NULL;
}
uintptr_t co_navtrim_test_block(void) { return (uintptr_t)g_trim.block; }
#endif
