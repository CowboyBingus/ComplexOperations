/* The extra large world's sites beside HellForge's level 11 (co_beside.h). HellForge's forms are its source's, in
 * co_beside_sites.h; a form that is neither the game's nor HellForge's is refused by the caller. */
#include <string.h>

#include "co_beside.h"
#include "co_beside_sites.h"

static uintptr_t Leads(const uint8_t* rel, uintptr_t end) { int32_t d; memcpy(&d, rel, 4); return end + (uintptr_t)(intptr_t)d; }

int co_beside_form(uintptr_t at, const uint8_t* bytes, size_t size, size_t jump, uintptr_t* stub)
{
    uint8_t now[32];
    if (size > sizeof now || jump < 5 || jump > size || g_coApi->read(at, now, size) != 0) return CO_BESIDE_OTHER;
    if (memcmp(now, bytes, size) == 0) return CO_BESIDE_GAME;
    if (now[0] != 0xE9 || memcmp(now + jump, bytes + jump, size - jump) != 0) return CO_BESIDE_OTHER;
    for (size_t i = 5; i < jump; i++) if (now[i] != 0x90) return CO_BESIDE_OTHER;
    if (stub != NULL) *stub = Leads(now + 1, at + 5);
    return CO_BESIDE_STOCK;
}

static int Hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

int co_beside_matches(uintptr_t game, uint32_t rva, const char* pattern)
{
    uint8_t want[256], now[256];
    size_t n = 0;
    for (const char* p = pattern; *p != 0; )
    {
        if (*p == ' ') { p++; continue; }
        if (Hex(p[0]) < 0 || Hex(p[1]) < 0 || n == sizeof want) return 0;
        want[n++] = (uint8_t)(Hex(p[0]) * 16 + Hex(p[1]));
        p += 2;
    }
    if (n == 0 || g_coApi->read(game + rva, now, n) != 0) return 0;
    for (size_t i = 0; i < n; )
    {
        size_t jump = 0;
        for (unsigned k = 0; k < CO_BESIDE_JUMPS; k++)
            if (kCoBesideJumps[k].site == rva + i && i + kCoBesideJumps[k].length <= n) jump = kCoBesideJumps[k].length;
        if (jump != 0 && now[i] == 0xE9 && memcmp(now + i, want + i, jump) != 0)
        {
            for (size_t k = 5; k < jump; k++) if (now[i + k] != 0x90) return 0;
            i += jump;
            continue;
        }
        if (now[i] != want[i]) return 0;
        i++;
    }
    return 1;
}

int co_beside_merge(uintptr_t game, uintptr_t stub)
{
    uint8_t s[41];
    if (g_coApi->read(stub, s, sizeof s) != 0) return 0;
    return memcmp(s, kCoBesideMergeHead, 17) == 0 && s[17] == 0xF0 && s[18] == 0xFF && s[19] == 0x05 &&
        s[24] == 0xE9 && Leads(s + 25, stub + 29) == game + CO_BESIDE_MERGE_DROP && memcmp(s + 29, kCoBesideMergePin, 7) == 0 &&
        s[36] == 0xE9 && Leads(s + 37, stub + 41) == game + CO_BESIDE_MERGE + 7;
}

int co_beside_mine(uintptr_t game, uintptr_t stub)
{
    uint8_t s[30];
    if (g_coApi->read(stub, s, sizeof s) != 0) return 0;
    return memcmp(s, kCoBesideMineStub, 14) == 0 && Leads(s + 14, stub + 18) == game + CO_BESIDE_MINE + 6 &&
        memcmp(s + 18, kCoBesideMineStub + 18, 3) == 0 && s[25] == 0xE9 && Leads(s + 26, stub + 30) == game + CO_BESIDE_MINE_DONE;
}

int co_beside_store(uintptr_t game, uintptr_t stub)
{
    static const uint8_t head[5] = {0x4D, 0x85, 0xC0, 0x74, 0x0C}; /* test r8, r8; jz the skip */
    uint8_t s[29];
    if (g_coApi->read(stub, s, sizeof s) != 0) return 0;
    return memcmp(s, head, 5) == 0 && memcmp(s + 5, kCoBesideStorePin, 7) == 0 && s[12] == 0xE9 && Leads(s + 13, stub + 17) == game + CO_BESIDE_STORE + 7 &&
        s[17] == 0xF0 && s[18] == 0xFF && s[19] == 0x05 && s[24] == 0xE9 && Leads(s + 25, stub + 29) == game + CO_BESIDE_STORE + 7;
}

void co_beside_place_tail(uint8_t out[CO_BESIDE_TAIL], uintptr_t code, uintptr_t game, uintptr_t stub)
{
    int32_t rel;
    memset(out, 0xCC, CO_BESIDE_TAIL);
    if (stub != 0)
    {
        out[0] = 0xE9; rel = (int32_t)(int64_t)(stub - (code + 5)); memcpy(out + 1, &rel, 4);
        return;
    }
    memcpy(out, kCoBesideMergePin, 7);
    out[7] = 0xE9; rel = (int32_t)(int64_t)(game + CO_BESIDE_MERGE + 7 - (code + 12)); memcpy(out + 8, &rel, 4);
}
