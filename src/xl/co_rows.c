/* The row readers' copies and the distance row (co_rows.h). */
#include <string.h>

#include <windows.h>

#include "co_rows.h"
#include "co_normal.h"

const uint8_t kCoRowRadii[8] = {0x0F, 0x11, 0x40, 0x20, 0x40, 0x80, 0xFF, 0x02};     /* movups [rax+0x20], xmm0; cmp dil, 2 */
const uint8_t kCoRowOutposts[8] = {0x0F, 0x11, 0x40, 0x20, 0x41, 0x80, 0xFF, 0x02};  /* ...; cmp r15b, 2 */
const uint8_t kCoRowObjectives[6] = {0x0F, 0x11, 0x40, 0x20, 0x33, 0xC0};            /* ...; xor eax, eax */

static const CoDistanceSite kDistance[CO_DISTANCE_SITES] = {
    {0xF417C8u, 5, {0xF3, 0x0F, 0x10, 0x84, 0x8B}},       /* movss xmm0, [rbx+rcx*4+table] */
    {0x7BDEE8u, 4, {0x41, 0x8B, 0x84, 0x89}},             /* mov eax, [r9+rcx*4+table] */
    {0xF62EDBu, 4, {0x41, 0x8B, 0x84, 0x89}},
    {0xF7D9B6u, 6, {0xF3, 0x41, 0x0F, 0x10, 0x84, 0x89}}, /* movss xmm0, [r9+rcx*4+table] */
    {0xF7E506u, 6, {0xF3, 0x41, 0x0F, 0x10, 0x84, 0x89}},
    {0xF82CF6u, 6, {0xF3, 0x41, 0x0F, 0x10, 0x84, 0x89}},
    {0xF84BA6u, 6, {0xF3, 0x41, 0x0F, 0x10, 0x84, 0x89}}};

static struct
{
    uintptr_t game;
    uint8_t* code;            /* the stubs' page */
    volatile uint8_t* honours;
    float* distance;          /* eleven rows of three */
    float stock[3];           /* HellForge's row 11 */
    float cityRatio;
} g_rows = {.cityRatio = 1.0f};

const CoDistanceSite* co_distance_sites(void) { return kDistance; }
float co_rows_city_ratio(void) { return g_rows.cityRatio; }

static void Rel(uint8_t* out, size_t at, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + at + 4));
    memcpy(out + at, &rel, 4);
}

size_t co_row_radii_stub(uint8_t* out, uintptr_t code, uintptr_t game, const float radii[4])
{
    static const uint8_t head[17] = {
        0x0F, 0x11, 0x40, 0x20,                           /*  0 movups [rax+0x20], xmm0 (the copy's last store) */
        0x41, 0x81, 0xBD, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0, /*  4 cmp dword [r13+0x2d7c], key (at 11) */
        0x75, 0x1C};                                      /* 15 jne 45 */
    const float key = co_disk_key();
    memcpy(out, head, sizeof head);
    memcpy(out + 11, &key, 4);
    for (int k = 0; k < 4; k++)
    {
        uint8_t* mov = out + 17 + 7 * k;                  /* 17 mov dword [rbp+0x70+4k], radius: the copy's +0xF0.. */
        mov[0] = 0xC7; mov[1] = 0x45; mov[2] = (uint8_t)(0x70 + 4 * k);
        memcpy(mov + 3, &radii[k], 4);
    }
    out[45] = 0x40; out[46] = 0x80; out[47] = 0xFF; out[48] = 0x02; /* 45 cmp dil, 2 */
    out[49] = 0xE9; Rel(out, 50, code, game + CO_ROW_RADII_BACK);
    return CO_ROW_RADII_STUB;
}

size_t co_row_outposts_stub(uint8_t* out, uintptr_t code, uintptr_t game)
{
    static const uint8_t stub[CO_ROW_OUTPOSTS_STUB] = {
        0x0F, 0x11, 0x40, 0x20,                           /*  0 movups [rax+0x20], xmm0 */
        0x50, 0x51,                                       /*  4 push rax; push rcx */
        0x48, 0x8B, 0x44, 0x24, 0x70,                     /*  6 mov rax, [rsp+0x70] (the pass: the game's [rsp+0x60]) */
        0x48, 0x8B, 0x40, 0x18,                           /* 11 mov rax, [rax+0x18] (the level) */
        0x81, 0xB8, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,   /* 15 cmp dword [rax+0x2d7c], key (at 21) */
        0x75, 0x1A,                                       /* 25 jne 53 */
        0x31, 0xC9,                                       /* 27 xor ecx, ecx */
        0x8B, 0x84, 0x8D, 0x04, 0x01, 0x00, 0x00,         /* 29 mov eax, [rbp+rcx*4+0x104] (the copy's +0x34: both lists) */
        0x8D, 0x04, 0x40,                                 /* 36 lea eax, [rax+rax*2] (three missions) */
        0x89, 0x84, 0x8D, 0x04, 0x01, 0x00, 0x00,         /* 39 mov [rbp+rcx*4+0x104], eax */
        0xFF, 0xC1,                                       /* 46 inc ecx */
        0x83, 0xF9, 0x28,                                 /* 48 cmp ecx, 40 */
        0x72, 0xE8,                                       /* 51 jb 29 */
        0x59, 0x58,                                       /* 53 pop rcx; pop rax */
        0x41, 0x80, 0xFF, 0x02,                           /* 55 cmp r15b, 2 */
        0xE9, 0, 0, 0, 0};                                /* 59 jmp 0xF7E4FD */
    const float key = co_disk_key();
    memcpy(out, stub, sizeof stub);
    memcpy(out + 21, &key, 4);
    Rel(out, 60, code, game + CO_ROW_OUTPOSTS_BACK);
    return sizeof stub;
}

size_t co_row_objectives_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t honours)
{
    static const uint8_t stub[CO_ROW_OBJECTIVES_STUB] = {
        0x0F, 0x11, 0x40, 0x20,                           /*  0 movups [rax+0x20], xmm0 */
        0x80, 0x3D, 0, 0, 0, 0, 0x00,                     /*  4 cmp byte [honours], 0 (rel32 at 6) */
        0x74, 0x3B,                                       /* 11 je 72 */
        0x48, 0x8B, 0x44, 0x24, 0x60,                     /* 13 mov rax, [rsp+0x60] (the inputs; rax is cleared below) */
        0x83, 0x78, 0x10, CO_COMPLEX_LEVEL,               /* 18 cmp dword [rax+0x10], 11 */
        0x75, 0x30,                                       /* 22 jne 72 */
        0xF6, 0x00, 0x01,                                 /* 24 test byte [rax], 1 (the seed's mark) */
        0x74, 0x2B,                                       /* 27 je 72 */
        0x8B, 0x45, 0xCC,                                 /* 29 mov eax, [rbp-0x34] (major objectives) */
        0x8D, 0x04, 0x40,                                 /* 32 lea eax, [rax+rax*2] */
        0x83, 0xF8, CO_OBJECTIVE_ROOM,                    /* 35 cmp eax, 18 */
        0x76, 0x05,                                       /* 38 jbe 45 */
        0xB8, CO_OBJECTIVE_ROOM, 0x00, 0x00, 0x00,        /* 40 mov eax, 18 */
        0x89, 0x45, 0xCC,                                 /* 45 mov [rbp-0x34], eax */
        0x51,                                             /* 48 push rcx */
        0xB9, CO_OBJECTIVE_ROOM, 0x00, 0x00, 0x00,        /* 49 mov ecx, 18 */
        0x29, 0xC1,                                       /* 54 sub ecx, eax (the room the major ones leave) */
        0x8B, 0x45, 0xC8,                                 /* 56 mov eax, [rbp-0x38] (optional objectives) */
        0x8D, 0x04, 0x40,                                 /* 59 lea eax, [rax+rax*2] */
        0x39, 0xC8,                                       /* 62 cmp eax, ecx */
        0x76, 0x02,                                       /* 64 jbe 68 */
        0x89, 0xC8,                                       /* 66 mov eax, ecx */
        0x89, 0x45, 0xC8,                                 /* 68 mov [rbp-0x38], eax */
        0x59,                                             /* 71 pop rcx */
        0x33, 0xC0,                                       /* 72 xor eax, eax */
        0xE9, 0, 0, 0, 0};                                /* 74 jmp 0x1756872 */
    memcpy(out, stub, sizeof stub);
    Rel(out, 6, code + 1, honours);                       /* the immediate follows the displacement */
    Rel(out, 75, code, game + CO_ROW_OBJECTIVES_BACK);
    return sizeof stub;
}

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[16];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* The table the seven reads have behind them: 0 unless all name the same one. */
static uintptr_t DistanceTable(uintptr_t game)
{
    int32_t first = 0;
    for (int i = 0; i < CO_DISTANCE_SITES; i++)
    {
        int32_t disp;
        if (!Same(game + kDistance[i].rva, kDistance[i].bytes, kDistance[i].at) || g_coApi->read(game + kDistance[i].rva + kDistance[i].at, &disp, 4) != 0) return 0;
        if (i == 0) first = disp; else if (disp != first) return 0;
    }
    return game + (intptr_t)first; /* the base register holds game.dll */
}

const char* co_rows_check(uintptr_t game)
{
    float rows[11 * 3], game_rows[10 * 3];
    if (!Same(game + CO_ROW_RADII_SITE, kCoRowRadii, sizeof kCoRowRadii)) return "the level's radius read is not the game's code this mod was made for";
    if (!Same(game + CO_ROW_OUTPOSTS_SITE, kCoRowOutposts, sizeof kCoRowOutposts)) return "the outposts' count read is not the game's code this mod was made for";
    if (!Same(game + CO_ROW_OBJECTIVES_SITE, kCoRowObjectives, sizeof kCoRowObjectives)) return "the objectives' count read is not the game's code this mod was made for";
    const uintptr_t table = DistanceTable(game);
    if (table == 0) return "the mission distance reads are not the game's code this mod was made for";
    /* the game's own table has ten rows: an eleventh is read only from HellForge's, whose first ten are the game's */
    if (table == game + CO_DISTANCE_GAME || g_coApi->read(table, rows, sizeof rows) != 0 || g_coApi->read(game + CO_DISTANCE_GAME, game_rows, sizeof game_rows) != 0 ||
        memcmp(rows, game_rows, sizeof game_rows) != 0) return "the mission distance rows are not difficulty 11's";
    return NULL;
}

void co_rows_level(int complex)
{
    const float factor = CO_XL_PLAYABLE / 0.38f;
    if (g_rows.distance == NULL) return;
    for (int k = 0; k < 3; k++) g_rows.distance[10 * 3 + k] = complex ? factor : g_rows.stock[k];
}

void co_rows_honours(int on) { if (g_rows.honours != NULL) *g_rows.honours = on != 0; }

static int Jump(uintptr_t site, size_t length, uintptr_t stub)
{
    uint8_t jump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    Rel(jump, 1, site, stub);
    return g_coApi->write(site, jump, length);
}

const char* co_rows_install(uintptr_t game)
{
    const char* why;
    float radii[4], rows[11 * 3];
    DWORD old;
    if (g_rows.game != 0) return NULL;
    if ((why = co_rows_check(game)) != NULL) return why;
    const uintptr_t table = DistanceTable(game), ten = g_coApi->difficulty_row(10);
    if (ten == 0 || g_coApi->read(ten + 0xF0, radii, sizeof radii) != 0 || g_coApi->read(table, rows, sizeof rows) != 0) return "level 10's row is not readable";
    for (int k = 0; k < 4; k++) radii[k] *= CO_RADIUS_SCALE;
    uint8_t* page = (uint8_t*)co_alloc_near(game, 0x2000);
    if (page == NULL) return "no memory near the game for the level row's readers";
    /* +0x1000 stays writable: the switch and the distance rows, within reach of game.dll's base register */
    const int64_t reach = (int64_t)(uintptr_t)(page + 0x1040) - (int64_t)game;
    if (reach < INT32_MIN || reach > INT32_MAX) return "the distance rows are out of the game's reach";
    g_rows.honours = page + 0x1000;
    g_rows.distance = (float*)(page + 0x1040);
    memcpy(g_rows.distance, rows, sizeof rows);
    memcpy(g_rows.stock, rows + 30, sizeof g_rows.stock);
    co_row_radii_stub(page, (uintptr_t)page, game, radii);
    co_row_outposts_stub(page + 0x80, (uintptr_t)page + 0x80, game);
    co_row_objectives_stub(page + 0x100, (uintptr_t)page + 0x100, game, (uintptr_t)g_rows.honours);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { g_rows.honours = NULL; g_rows.distance = NULL; return "the level row's readers could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    const int32_t own = (int32_t)reach, stock = (int32_t)((int64_t)table - (int64_t)game);
    int moved = 0, jumps = 0;
    for (; moved < CO_DISTANCE_SITES; moved++)
        if (g_coApi->write(game + kDistance[moved].rva + kDistance[moved].at, &own, 4) != 0) break;
    if (moved == CO_DISTANCE_SITES &&
        (jumps++, Jump(game + CO_ROW_RADII_SITE, 8, (uintptr_t)page) == 0) &&
        (jumps++, Jump(game + CO_ROW_OUTPOSTS_SITE, 8, (uintptr_t)page + 0x80) == 0) &&
        (jumps++, Jump(game + CO_ROW_OBJECTIVES_SITE, 6, (uintptr_t)page + 0x100) == 0))
    {
        g_rows.game = game; g_rows.code = page;
        /* level 10's city factor, by the same growth, over the complex one's */
        g_rows.cityRatio = rows[27] * CO_RADIUS_SCALE / (CO_XL_PLAYABLE / 0.38f);
        return NULL;
    }
    /* back: the copy holds HellForge's rows, so a read left on it reads what it read before */
    if (jumps > 2) g_coApi->write(game + CO_ROW_OUTPOSTS_SITE, kCoRowOutposts, sizeof kCoRowOutposts);
    if (jumps > 1) g_coApi->write(game + CO_ROW_RADII_SITE, kCoRowRadii, sizeof kCoRowRadii);
    while (moved-- > 0) g_coApi->write(game + kDistance[moved].rva + kDistance[moved].at, &stock, 4);
    g_rows.honours = NULL; g_rows.distance = NULL;
    return "the level row's readers could not be changed";
}

#ifdef CO_TEST
void co_rows_test_reset(void) { memset(&g_rows, 0, sizeof g_rows); g_rows.cityRatio = 1.0f; }
const float* co_rows_test_distance(void) { return g_rows.distance; }
uintptr_t co_rows_test_code(void) { return (uintptr_t)g_rows.code; }
#endif
