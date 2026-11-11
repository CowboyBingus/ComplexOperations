/* The rules that keep a normal level 11 mission as it is without the mod (co_normal.h). */
#include <string.h>

#include "co_normal.h"

int co_level_complex(float disk)
{
    const float key = CO_DISK_KEY;
    return memcmp(&disk, &key, 4) == 0; /* the value as written, not one near it: no other level may match */
}

void co_nav_plan(float disk, float radius, CoNavPlan* plan)
{
    if (co_level_complex(disk)) { plan->grid = 32; plan->low = -8190.0f; plan->high = 8194.0f; plan->tile = 512.0f; }
    else if (radius > 682.0f) { plan->grid = 8; plan->low = -1023.75f; plan->high = 1024.25f; plan->tile = 256.0f; }
    else { plan->grid = 8; plan->low = -511.75f; plan->high = 512.25f; plan->tile = 128.0f; }
}

uint32_t co_level_limit(int which, int complex)
{
    static const uint32_t limits[CO_LIMITS][2] = {{128, 1024}, {16, 64}, {10, 32}, {16, 256}, {16383, 131071}};
    return which >= 0 && which < CO_LIMITS ? limits[which][complex != 0] : 0;
}

const uint8_t kCoHudListBytes[5] = {0xE8, 0x04, 0xA0, 0xE0, 0xFE};
const uint8_t kCoHudFindBytes[43] = {0x49, 0x8B, 0x45, 0x50, 0x4A, 0x8B, 0x14, 0xE0, 0x48, 0x8B, 0x44, 0x24, 0x28, 0x48, 0x05, 0x50, 0x62, 0x01, 0x00,
    0x8B, 0x72, 0x08, 0x39, 0x30, 0x74, 0x0F, 0xFF, 0xC1, 0x48, 0x05, 0x68, 0xF0, 0x00, 0x00, 0x83, 0xF9, 0x10, 0x72, 0xEF, 0xEB, 0x02, 0x8B, 0xE9};
const uint8_t kCoHudFlagBytes[7] = {0x88, 0x4C, 0x04, 0x30, 0x45, 0x84, 0xC0};

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[64];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_hud_check(uintptr_t game)
{
    if (!Same(game + CO_HUD_LIST_SITE, kCoHudListBytes, sizeof kCoHudListBytes)) return "the HUD's objective list is not the game's code this mod was made for";
    if (!Same(game + CO_HUD_FIND_SITE, kCoHudFindBytes, sizeof kCoHudFindBytes)) return "the HUD's row search is not the game's code this mod was made for";
    if (!Same(game + CO_HUD_FLAG_SITE, kCoHudFlagBytes, sizeof kCoHudFlagBytes)) return "the HUD's near mark is not the game's code this mod was made for";
    return NULL;
}

static void Rel(uint8_t* out, size_t at, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + at + 4)); /* both lie within a jump of game.dll */
    memcpy(out + at, &rel, 4);
}

/* cmp byte [rip -> flag], 0: 7 bytes, the displacement counted from the immediate's end */
static void Test(uint8_t* out, uintptr_t code, uintptr_t flag)
{
    const int32_t rel = (int32_t)((int64_t)flag - (int64_t)(code + 7));
    out[0] = 0x80; out[1] = 0x3D; memcpy(out + 2, &rel, 4); out[6] = 0x00;
}

size_t co_hud_list_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t rows)
{
    static const uint8_t stub[CO_HUD_LIST_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*  0 cmp byte [flag], 0 */
        0x75, 0x0A,                                       /*  7 jne 19 */
        0xE8, 0, 0, 0, 0,                                 /*  9 call 0x5D96F0 (the site's own call) */
        0xE9, 0, 0, 0, 0,                                 /* 14 jmp 0x17CF6EC (the game's list part) */
        0x4C, 0x89, 0xF9,                                 /* 19 mov rcx, r15 (the HUD) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,               /* 22 mov rax, rows */
        0xFF, 0xD0,                                       /* 32 call rax */
        0xE9, 0, 0, 0, 0};                                /* 34 jmp 0x17CF79A */
    const uint64_t to = rows;
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    Rel(out, 10, code, game + CO_HUD_LIST_CALL);
    Rel(out, 15, code, game + CO_HUD_LIST_NEXT);
    memcpy(out + 24, &to, 8);
    Rel(out, 35, code, game + CO_HUD_LIST_BACK);
    return sizeof stub;
}

size_t co_hud_find_head(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag)
{
    Test(out, code, flag);
    out[7] = 0x75; out[8] = (uint8_t)(sizeof kCoHudFindBytes + 5);  /* jne: past the game's search and its jump */
    memcpy(out + 9, kCoHudFindBytes, sizeof kCoHudFindBytes);       /* no address in it; its last jump ends behind it */
    out[52] = 0xE9; Rel(out, 53, code, game + CO_HUD_FIND_BACK);
    return CO_HUD_FIND_HEAD;
}

size_t co_hud_flag_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag)
{
    static const uint8_t stub[CO_HUD_FLAG_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*  0 cmp byte [flag], 0 */
        0x74, 0x06,                                       /*  7 je 15 */
        0x08, 0x4C, 0x04, 0x30,                           /*  9 or [rsp+rax+0x30], cl (near when any copy is) */
        0xEB, 0x04,                                       /* 13 jmp 19 */
        0x88, 0x4C, 0x04, 0x30,                           /* 15 mov [rsp+rax+0x30], cl (the game's) */
        0x45, 0x84, 0xC0,                                 /* 19 test r8b, r8b (the game's; its flags go back) */
        0xE9, 0, 0, 0, 0};                                /* 22 jmp 0x17CFA6D */
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    Rel(out, 23, code, game + CO_HUD_FLAG_BACK);
    return sizeof stub;
}
