#include <string.h>

#include <windows.h>

#include "co_unwind.h"

#define CO_UNWIND_LEVELS 4

/* Slots one unwind code takes (its first holds the code offset); 0 for a code this copy does not handle. */
static unsigned CoUnwindSlots(uint16_t code)
{
    const unsigned op = (code >> 8) & 15u, info = code >> 12;
    if (op == 1u) return info == 0 ? 2u : 3u; /* UWOP_ALLOC_LARGE */
    if (op == 4u || op == 8u) return 2u;      /* UWOP_SAVE_NONVOL, UWOP_SAVE_XMM128 */
    if (op == 5u || op == 9u) return 3u;      /* their _FAR forms */
    return op == 6u || op > 10u ? 0u : 1u;    /* version 2's UWOP_EPILOG: refused */
}

/* The game's unwind data at `info` (and what it chains to) copied to `out` with no prologue: the stub stands in the
 * body, so every code applies. With no prologue the unwinder never reads the code offsets; they are zeroed in case
 * one is read. The unwinder reads saved registers at the frame it took from the stub's Rsp, `delta` bytes under
 * the game's: the save offsets move by it. Returns the bytes written; 0 when it does not fit, -1 for another handler. */
static long CoUnwindCopy(const uint8_t* info, uintptr_t gameBase, uint8_t* out, size_t room, uint8_t* block, uint32_t end, uint32_t delta,
    uintptr_t handler, int level)
{
    const unsigned count = info[2], slots = (count + 1u) & ~1u;
    const int chained = (info[0] >> 3) & UNW_FLAG_CHAININFO;
    const size_t size = 4u + (size_t)slots * 2u + (chained ? sizeof(RUNTIME_FUNCTION) : 0u);
    if (size > room || level >= CO_UNWIND_LEVELS || (info[0] & 7u) != 1u || (info[3] & 15u) != 0) return 0; /* no frame register */
    if (!chained && ((info[0] >> 3) & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) &&
        gameBase + *(const uint32_t*)(info + 4 + (size_t)slots * 2u) != handler) return -1;
    memcpy(out, info, 4u + (size_t)slots * 2u);
    out[0] = (uint8_t)(1u | (chained ? UNW_FLAG_CHAININFO << 3 : 0)); /* the stack-cookie check is not ours to call */
    out[1] = 0;
    for (unsigned i = 0, n; i < count; i += n)
    {
        uint8_t* c = out + 4 + i * 2;
        const unsigned op = c[1] & 15u;
        if ((n = CoUnwindSlots((uint16_t)(c[0] | (c[1] << 8)))) == 0 || i + n > count) return 0;
        c[0] = 0;
        uint16_t* v = (uint16_t*)(c + 2);
        if (op == 4u) v[0] = (uint16_t)(v[0] + delta / 8u);
        else if (op == 8u) v[0] = (uint16_t)(v[0] + delta / 16u);
        else if (op == 5u || op == 9u) { uint32_t wide; memcpy(&wide, v, 4); wide += delta; memcpy(v, &wide, 4); }
    }
    if (!chained) return (long)size;
    const RUNTIME_FUNCTION* next = (const RUNTIME_FUNCTION*)(info + 4 + (size_t)slots * 2u);
    RUNTIME_FUNCTION* link = (RUNTIME_FUNCTION*)(out + 4 + (size_t)slots * 2u);
    long more = CoUnwindCopy((const uint8_t*)(gameBase + next->UnwindData), gameBase, out + size, room - size, block, end, delta, handler, level + 1);
    if (more <= 0) return more;
    link->BeginAddress = 0; link->EndAddress = end; link->UnwindData = (DWORD)(out + size - block);
    return (long)size + more;
}

/* Bytes a range's own codes put under the game's Rsp once its prologue ran; ~0 for a code other than a push or a small
 * allocation (the save offsets could not be moved by it). */
static uint32_t CoUnwindDelta(const CoUnwindRange* r)
{
    uint32_t delta = 0;
    for (unsigned i = 0; i < r->count; i++)
    {
        const unsigned op = (r->codes[i] >> 8) & 15u, info = r->codes[i] >> 12;
        if (op != 0u && op != 2u) return ~0u;
        delta += op == 0u ? 8u : info * 8u + 8u;
    }
    return delta;
}

int co_unwind_block(uint8_t* block, const CoUnwindRange* ranges, int count, uintptr_t site, uintptr_t handler, uint8_t* space, size_t bytes)
{
    DWORD64 gameBase = 0;
    PRUNTIME_FUNCTION game = RtlLookupFunctionEntry((DWORD64)site, &gameBase, NULL);
    if (game == NULL || count < 1 || count > 4 || space < block || (uintptr_t)(space - block) + bytes > 0xFFFFFFFFu) return -1;
    uint32_t end = 0;
    for (int r = 0; r < count; r++) if (ranges[r].begin + ranges[r].length > end) end = ranges[r].begin + ranges[r].length;
    RUNTIME_FUNCTION* entries = (RUNTIME_FUNCTION*)(((uintptr_t)space + 3u) & ~(uintptr_t)3u);
    uint8_t* at = (uint8_t*)(entries + count);
    const uint8_t* stop = space + bytes;
    uint32_t deltas[4]; uint8_t* parents[4]; int copies = 0;
    for (int r = 0; r < count; r++)
    {
        const uint32_t delta = CoUnwindDelta(&ranges[r]);
        if (delta == ~0u || delta % 16u != 0 || ranges[r].count > 6) return -1; /* a save offset in 16-byte units moves by whole units */
        uint8_t* parent = NULL;
        for (int c = 0; c < copies; c++) if (deltas[c] == delta) parent = parents[c];
        if (parent == NULL)
        {
            long copied = CoUnwindCopy((const uint8_t*)(gameBase + game->UnwindData), (uintptr_t)gameBase, at, (size_t)(stop - at), block, end, delta, handler, 0);
            if (copied <= 0) return copied < 0 ? -2 : -1;
            parent = at; deltas[copies] = delta; parents[copies++] = at;
            at += ((size_t)copied + 3u) & ~(size_t)3u;
        }
        const unsigned slots = (ranges[r].count + 1u) & ~1u;
        if (at + 4 + slots * 2u + sizeof(RUNTIME_FUNCTION) > stop) return -1;
        at[0] = 1u | (UNW_FLAG_CHAININFO << 3); at[1] = ranges[r].prolog; at[2] = ranges[r].count; at[3] = 0;
        memset(at + 4, 0, slots * 2u);
        memcpy(at + 4, ranges[r].codes, ranges[r].count * 2u);
        RUNTIME_FUNCTION* link = (RUNTIME_FUNCTION*)(at + 4 + slots * 2u);
        link->BeginAddress = 0; link->EndAddress = end; link->UnwindData = (DWORD)(parent - block);
        entries[r].BeginAddress = ranges[r].begin; entries[r].EndAddress = ranges[r].begin + ranges[r].length;
        entries[r].UnwindData = (DWORD)(at - block);
        at = (uint8_t*)(link + 1);
    }
    return RtlAddFunctionTable(entries, (DWORD)count, (DWORD64)(uintptr_t)block) ? 0 : -1;
}
