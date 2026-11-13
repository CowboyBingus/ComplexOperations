#include <string.h>

#include <windows.h>

#include "co_unwind.h"

#define CO_UNWIND_TABLES 8
#define CO_UNWIND_LEVELS 4
static PRUNTIME_FUNCTION g_coUnwindTables[CO_UNWIND_TABLES];
static volatile LONG g_coUnwindCount;

/* Slots one unwind code takes (its first holds the code offset). */
static unsigned CoUnwindSlots(uint16_t code)
{
    const unsigned op = (code >> 8) & 15u, info = code >> 12;
    if (op == 1u) return info == 0 ? 2u : 3u; /* UWOP_ALLOC_LARGE */
    if (op == 4u || op == 8u) return 2u;      /* UWOP_SAVE_NONVOL, UWOP_SAVE_XMM128 */
    if (op == 5u || op == 9u) return 3u;      /* their _FAR forms */
    return 1u;
}

/* The game's unwind data at `info` (and what it chains to) copied to `out` with no prologue and every code offset 0:
 * the stub stands in the body, so all of it applies. Wine takes a chained entry's codes against the offset from the
 * stub's own entry, which the original offsets would cut short. Returns the bytes written, 0 when it does not fit. */
static size_t CoUnwindCopy(const uint8_t* info, uintptr_t gameBase, uint8_t* out, size_t room, uint8_t* block, uint32_t end, int level)
{
    const unsigned count = info[2], slots = (count + 1u) & ~1u;
    const int chained = (info[0] >> 3) & UNW_FLAG_CHAININFO;
    size_t size = 4u + (size_t)slots * 2u + (chained ? sizeof(RUNTIME_FUNCTION) : 0u);
    if (size > room || level >= CO_UNWIND_LEVELS || (info[0] & 7u) == 0 || (info[0] & 7u) > 2u) return 0;
    memcpy(out, info, 4u + (size_t)slots * 2u);
    out[0] = (uint8_t)((info[0] & 7u) | (chained ? UNW_FLAG_CHAININFO << 3 : 0)); /* its handlers are not ours to call */
    out[1] = 0;
    for (unsigned i = 0; i < count; i += CoUnwindSlots((uint16_t)(out[4 + i * 2] | (out[5 + i * 2] << 8)))) out[4 + i * 2] = 0;
    if (!chained) return size;
    const RUNTIME_FUNCTION* next = (const RUNTIME_FUNCTION*)(info + 4 + (size_t)slots * 2u);
    RUNTIME_FUNCTION* link = (RUNTIME_FUNCTION*)(out + 4 + (size_t)slots * 2u);
    size_t more = CoUnwindCopy((const uint8_t*)(gameBase + next->UnwindData), gameBase, out + size, room - size, block, end, level + 1);
    if (more == 0) return 0;
    link->BeginAddress = 0; link->EndAddress = end; link->UnwindData = (DWORD)(out + size - block);
    return size + more;
}

int co_unwind_block(uint8_t* block, const CoUnwindRange* ranges, int count, uintptr_t site, uint8_t* space, size_t bytes)
{
    DWORD64 gameBase = 0;
    PRUNTIME_FUNCTION game = RtlLookupFunctionEntry((DWORD64)site, &gameBase, NULL);
    if (game == NULL || count < 1 || count > 4 || space < block || (uintptr_t)(space - block) + bytes > 0xFFFFFFFFu) return -1;
    uint32_t end = 0;
    for (int r = 0; r < count; r++) if (ranges[r].begin + ranges[r].length > end) end = ranges[r].begin + ranges[r].length;
    RUNTIME_FUNCTION* entries = (RUNTIME_FUNCTION*)(((uintptr_t)space + 3u) & ~(uintptr_t)3u);
    uint8_t* at = (uint8_t*)(entries + count);
    const uint8_t* stop = space + bytes;
    uint8_t* parent = at;
    size_t copied = CoUnwindCopy((const uint8_t*)(gameBase + game->UnwindData), (uintptr_t)gameBase, parent, (size_t)(stop - parent), block, end, 0);
    if (copied == 0) return -1;
    at = parent + ((copied + 3u) & ~(size_t)3u);
    for (int r = 0; r < count; r++)
    {
        const unsigned slots = (ranges[r].count + 1u) & ~1u;
        if (ranges[r].count > 6 || at + 4 + slots * 2u + sizeof(RUNTIME_FUNCTION) > stop) return -1;
        at[0] = 1u | (UNW_FLAG_CHAININFO << 3); at[1] = ranges[r].prolog; at[2] = ranges[r].count; at[3] = 0;
        memset(at + 4, 0, slots * 2u);
        memcpy(at + 4, ranges[r].codes, ranges[r].count * 2u);
        RUNTIME_FUNCTION* link = (RUNTIME_FUNCTION*)(at + 4 + slots * 2u);
        link->BeginAddress = 0; link->EndAddress = end; link->UnwindData = (DWORD)(parent - block);
        entries[r].BeginAddress = ranges[r].begin; entries[r].EndAddress = ranges[r].begin + ranges[r].length;
        entries[r].UnwindData = (DWORD)(at - block);
        at = (uint8_t*)(link + 1);
    }
    LONG k = InterlockedIncrement(&g_coUnwindCount) - 1;
    if (k >= CO_UNWIND_TABLES) { InterlockedDecrement(&g_coUnwindCount); return -1; }
    if (!RtlAddFunctionTable(entries, (DWORD)count, (DWORD64)(uintptr_t)block)) { g_coUnwindTables[k] = NULL; return -1; }
    g_coUnwindTables[k] = entries;
    return 0;
}

void co_unwind_remove_all(void)
{
    LONG n = g_coUnwindCount;
    for (LONG k = 0; k < n && k < CO_UNWIND_TABLES; k++)
        if (g_coUnwindTables[k] != NULL) { RtlDeleteFunctionTable(g_coUnwindTables[k]); g_coUnwindTables[k] = NULL; }
}
