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

static void QueueLock(uintptr_t queue)
{
    LONG64 mine = InterlockedExchangeAdd64((volatile LONG64*)queue, 1);
    while (*(volatile LONG64*)(queue + 8) != mine) YieldProcessor();
}
static void QueueUnlock(uintptr_t queue) { InterlockedExchangeAdd64((volatile LONG64*)(queue + 8), 1); }

int co_coarse_queue_take(uintptr_t queue, uint8_t* saved, int* have)
{
    QueueLock(queue);
    volatile uint32_t* count = (volatile uint32_t*)(queue + 0x10);
    const uint8_t* e = (const uint8_t*)(queue + 0x18);
    uint32_t id = 0;
    memcpy(&id, e + 0x68, 4);
    int ok = *count == 1 && id == 0xFFFFFFFFu && (!*have || co_coarse_entries_same(saved, e));
    if (ok) { memcpy(saved, e, CO_COARSE_ENTRY); *have = 1; *count = 0; }
    QueueUnlock(queue);
    return ok;
}

int co_coarse_queue_restore(uintptr_t queue, const uint8_t* saved)
{
    QueueLock(queue);
    volatile uint32_t* count = (volatile uint32_t*)(queue + 0x10);
    int put = *count == 0;
    if (put) { memcpy((void*)(queue + 0x18), saved, CO_COARSE_ENTRY); *count = 1; }
    QueueUnlock(queue);
    return put;
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

/* `made`: this frame's calls; `drew`: those that could put triangles on the gui. */
static int Frame(CoCoarseBatch* b, const int* list, int count, const CoCoarseOps* ops, int* made, int* drew)
{
    int empty = 1; /* every call of this frame so far only appended */
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
            if (n < want && st == 0)
            {
                if (!ops->over(ops->self) && ops->wait(ops->self, b->next + n)) continue;
                if (*made > 0 && b->batch > 1) b->unsharedWait++;
                return 1;
            }
            b->n = n; b->moved = 0; b->gaveUp = 0;
            memset(b->calls, 0, sizeof b->calls);
        }
        if (*made == 0)
        {
            if (ops->over(ops->self)) return 1;
            if (ops->queued(ops->self) != 0) { b->queueWaits++; return 1; }
        }
        /* A frame whose calls only appended renders nothing new: the last group's triangles went out with the
         * previous drain, so the next group's first calls may share it. Only batched: one a frame is the serial path. */
        else if (!empty || b->batch == 1 || ops->over(ops->self))
        {
            if (b->batch > 1) { if (!empty) b->unsharedDrew++; else b->unsharedOver++; }
            return 1;
        }
        else b->sharedFrames++;
        for (int i = 0, here = 0; i < b->n && here < b->batch; i++)
        {
            uint32_t bit = 1u << i;
            if ((b->moved | b->gaveUp) & bit) continue;
            if (*made > 0)
            {
                if (!ops->take(ops->self)) { b->refused++; b->batch = 1; return 1; } /* not ours alone: one a frame */
                b->taken++;
            }
            int only = ops->drawn(ops->self, b->next + i), unclipped = 0;
            int r = ops->call(ops->self, b->next + i, *made > 0, &unclipped);
            if (*made > 0 && ops->restore(ops->self)) b->restored++;
            if (unclipped || r == CO_COARSE_UNCLIPPED) { b->unclipped++; b->batch = 1; } /* one tile a frame from here */
            if (r == CO_COARSE_UNCLIPPED) return 1; /* drawn alone in a later frame */
            (*made)++; here++;
            if (!only) { empty = 0; (*drew)++; }
            if (r < 0)
            {
                for (int j = 0; j < b->n; j++) if ((b->moved >> j) & 1u) ops->done(ops->self, b->next + j, CO_COARSE_DRAWN);
                ops->done(ops->self, b->next + i, CO_COARSE_FAULTED);
                b->next = count; b->n = 0;
                return -1;
            }
            if (++b->calls[i] == 1 && r == 0 && !ops->drawn(ops->self, b->next + i)) b->split++;
            if (r > 0) b->moved |= bit;
            else if (b->calls[i] >= b->limit) b->gaveUp |= bit;
        }
        if ((b->moved | b->gaveUp) != (1u << b->n) - 1u) return 1;
        GroupDone(b, ops);
    }
}

int co_coarse_frame(CoCoarseBatch* b, const int* list, int count, const CoCoarseOps* ops)
{
    int made = 0, drew = 0;
    int r = Frame(b, list, count, ops, &made, &drew);
    if (made > 0) b->frames++;
    if (drew > b->tilesFrameMax) b->tilesFrameMax = drew;
    return r;
}
