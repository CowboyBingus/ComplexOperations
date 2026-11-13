/* Unwind data for code the mod writes into a game function's body (a jump out to a stub that may call C): without it a
 * stack walk or an exception from the C code finds no entry for the stub, takes it for a leaf and loses the game's
 * frames. Each range unwinds as the game function does at the site, after undoing the stub's own prologue codes. */
#ifndef CO_UNWIND_H
#define CO_UNWIND_H

#include <stdint.h>
#include <string.h>
#include <windows.h>

#define CO_UNWIND_SPACE 48u /* bytes of writable space each range needs, in reach of the stub */
#define CO_UNWIND_PUSH(at, reg) ((uint16_t)((at) | ((0u | ((reg) << 4)) << 8)))  /* UWOP_PUSH_NONVOL after `at` bytes */
#define CO_UNWIND_ALLOC(at, size) ((uint16_t)((at) | ((2u | ((((size) - 8u) / 8u) << 4)) << 8))) /* UWOP_ALLOC_SMALL, 8..128 */

/* begin..begin+length: the range, its prologue `prolog` bytes long with `count` codes (the last first, as in a
 * function's UNWIND_INFO); site: an address in the game function the stub stands for. space: CO_UNWIND_SPACE bytes that
 * stay mapped, readable once the stub's page is protected. 0 when registered. */
static inline int co_unwind_range(uint8_t* begin, uint32_t length, uint8_t prolog, const uint16_t* codes, uint8_t count, uintptr_t site,
    uint8_t* space)
{
    DWORD64 gameBase = 0;
    PRUNTIME_FUNCTION game = RtlLookupFunctionEntry((DWORD64)site, &gameBase, NULL);
    if (game == NULL || count > 8) return -1;
    const uintptr_t base = (uintptr_t)begin < (uintptr_t)gameBase ? (uintptr_t)begin : (uintptr_t)gameBase;
    uintptr_t top = (uintptr_t)space + CO_UNWIND_SPACE, end = (uintptr_t)begin + length;
    if (end > top) top = end;
    if ((uintptr_t)gameBase + game->EndAddress > top) top = (uintptr_t)gameBase + game->EndAddress;
    if ((uintptr_t)gameBase + game->UnwindData + 64u > top) top = (uintptr_t)gameBase + game->UnwindData + 64u;
    if (top - base > 0xFFFFFFFFu || (uintptr_t)space < base) return -1; /* every address an RVA from one base */
    uint8_t* info = space;
    const uint8_t slots = (uint8_t)((count + 1u) & ~1u); /* the chained entry is DWORD aligned */
    info[0] = 1u | (UNW_FLAG_CHAININFO << 3); info[1] = prolog; info[2] = count; info[3] = 0;
    memset(info + 4, 0, (size_t)slots * 2u);
    if (count > 0) memcpy(info + 4, codes, (size_t)count * 2u);
    RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(info + 4 + (size_t)slots * 2u);
    chained->BeginAddress = (DWORD)((uintptr_t)gameBase + game->BeginAddress - base);
    chained->EndAddress = (DWORD)((uintptr_t)gameBase + game->EndAddress - base);
    chained->UnwindData = (DWORD)((uintptr_t)gameBase + game->UnwindData - base);
    RUNTIME_FUNCTION* entry = chained + 1;
    entry->BeginAddress = (DWORD)((uintptr_t)begin - base);
    entry->EndAddress = (DWORD)((uintptr_t)begin + length - base);
    entry->UnwindData = (DWORD)((uintptr_t)info - base);
    return RtlAddFunctionTable(entry, 1, (DWORD64)base) ? 0 : -1;
}

#endif
