/* The coarse terrain's draw frames (co_coarse.h). */
#include <string.h>

#include <windows.h>

#include "co_coarse.h"

void co_coarse_order(int* list, int count, int columns)
{
    for (int i = 1; i < count; i++)
    {
        int t = list[i], s = co_coarse_step(t, columns), j = i;
        for (; j > 0 && co_coarse_step(list[j - 1], columns) > s; j--) list[j] = list[j - 1];
        list[j] = t;
    }
}

int co_coarse_group(const int* list, int count, int at, int columns, int batch)
{
    if (at >= count) return 0;
    int n = 1, s = co_coarse_step(list[at], columns);
    while (n < batch && at + n < count && co_coarse_step(list[at + n], columns) == s) n++;
    return n;
}

int co_coarse_entries_same(const uint8_t* a, const uint8_t* b)
{
    uint32_t id = 0;
    memcpy(&id, b + 0x68, 4);
    return id == 0xFFFFFFFFu && memcmp(a, b, 0x24) == 0 && a[0x24] == b[0x24] && memcmp(a + 0x38, b + 0x38, 2) == 0 &&
        memcmp(a + 0x58, b + 0x58, 0x14) == 0;
}

int co_coarse_queue_drop(uintptr_t queue)
{
    volatile LONG64* ticket = (volatile LONG64*)queue;
    LONG64 mine = InterlockedExchangeAdd64(ticket, 1);
    while (*(volatile LONG64*)(queue + 8) != mine) YieldProcessor();
    volatile uint32_t* count = (volatile uint32_t*)(queue + 0x10);
    uint32_t n = *count;
    int same = 0;
    if (n >= 2 && n <= CO_COARSE_ENTRIES)
    {
        const uint8_t* a = (const uint8_t*)(queue + 0x18 + (uintptr_t)(n - 2) * CO_COARSE_ENTRY);
        same = co_coarse_entries_same(a, a + CO_COARSE_ENTRY);
        if (same) *count = n - 1;
    }
    InterlockedExchangeAdd64((volatile LONG64*)(queue + 8), 1);
    return same;
}

void co_coarse_begin(CoCoarseBatch* b, int columns, int batch, int limit)
{
    memset(b, 0, sizeof *b);
    b->columns = columns > 0 ? columns : 1;
    b->batch = batch < 1 ? 1 : batch > CO_COARSE_BATCH_MAX ? CO_COARSE_BATCH_MAX : batch;
    b->limit = limit > 0 ? limit : 1;
}

/* The arena is reset only once the whole group is finished: a tile whose first call did not finish reads its visited
 * buffer (an arena block) in the next call. */
static void GroupDone(CoCoarseBatch* b, const CoCoarseOps* ops)
{
    ops->reset(ops->self);
    for (int i = 0; i < b->n; i++) ops->done(ops->self, b->next + i, (b->moved >> i) & 1u ? CO_COARSE_DRAWN : CO_COARSE_UNFINISHED);
    b->next += b->n; b->n = 0; b->groups++;
}

int co_coarse_frame(CoCoarseBatch* b, const int* list, int count, const CoCoarseOps* ops)
{
    for (;;)
    {
        if (b->n == 0)
        {
            if (b->next >= count) return 0;
            int st = ops->built(ops->self, b->next);
            if (st < 0) { ops->done(ops->self, b->next, CO_COARSE_UNBUILT); b->next++; continue; }
            /* a group waits for all its tiles: drawn as they come, a step would take a frame a tile */
            int want = co_coarse_group(list, count, b->next, b->columns, b->batch), n = 0;
            while (n < want && (st = ops->built(ops->self, b->next + n)) > 0) n++;
            if (n < want && st == 0) { if (!ops->over(ops->self) && ops->wait(ops->self, b->next + n)) continue; return 1; }
            b->n = n; b->moved = 0; b->gaveUp = 0;
            memset(b->calls, 0, sizeof b->calls);
        }
        if (ops->over(ops->self)) return 1;
        if (ops->queued(ops->self) != 0) { b->queueWaits++; return 1; }
        int made = 0;
        for (int i = 0; i < b->n && made < b->batch; i++)
        {
            uint32_t bit = 1u << i;
            if ((b->moved | b->gaveUp) & bit) continue;
            if (made > 0 && ops->queued(ops->self) != 1) break; /* not only this frame's entry: the rest wait */
            int r = ops->call(ops->self, b->next + i), stop = 0;
            if (made++ > 0)
            {
                if (ops->drop(ops->self)) b->dropped++;
                else { b->refused++; b->batch = 1; stop = 1; } /* entries that differ: one tile a frame from here */
            }
            if (r < 0)
            {
                for (int j = 0; j < b->n; j++) if ((b->moved >> j) & 1u) ops->done(ops->self, b->next + j, CO_COARSE_DRAWN);
                ops->done(ops->self, b->next + i, CO_COARSE_FAULTED);
                b->next = count; b->n = 0;
                return -1;
            }
            b->calls[i]++;
            if (r > 0) b->moved |= bit;
            else if (b->calls[i] >= b->limit) b->gaveUp |= bit;
            if (stop) break;
        }
        b->frames++;
        if (made > b->tilesFrameMax) b->tilesFrameMax = made;
        if ((b->moved | b->gaveUp) == (1u << b->n) - 1u) GroupDone(b, ops);
    }
}
