/* The coarse terrain's tiles drawn several a frame at the level's SmoothHeightmap wait (op 0x40). One draw call of
 * RenderHeightmapVoronoi (0xF499B0) puts the tile's triangles on the merge's gui and appends a render of that gui to
 * the generation's queue; the drain renders every queued entry, so two entries in one frame add a frame's heights
 * twice. Tiles of one step (2 row + column) are two columns apart and never touch: their calls share a frame, and the
 * repeated entries are removed so the drain renders the frame's gui once. Neighbours keep the tile order's sequence. */
#ifndef CO_COARSE_H
#define CO_COARSE_H

#include <stdint.h>

#define CO_COARSE_BATCH 4      /* tiles a frame: about 128k gui triangles, within the 190k a frame drew whole */
#define CO_COARSE_BATCH_MAX 12 /* a step's tiles on a 24-column map */
#define CO_COARSE_ENTRY 0x1D0u /* one queue entry (0xF4A210), from queue + 0x18; the count at + 0x10 */
#define CO_COARSE_ENTRIES 16u  /* the entries the queue holds before the next field */

static inline int co_coarse_step(int tile, int columns) { return 2 * (tile / columns) + tile % columns; }
/* Sorts a list in tile order by step, keeping the tile order within a step. */
void co_coarse_order(int* list, int count, int columns);
/* The entries from `at` on that share its step, at most `batch`. */
int co_coarse_group(const int* list, int count, int at, int columns, int batch);
/* Whether two queue entries hold the same render: every field 0xF4A195 writes, with the id -1 it gives its own. */
int co_coarse_entries_same(const uint8_t* a, const uint8_t* b);
/* Under the queue's ticket lock (+0 ticket, +8 serving): removes the newest entry when it repeats the one before it. */
int co_coarse_queue_drop(uintptr_t queue);

enum { CO_COARSE_UNBUILT, CO_COARSE_DRAWN, CO_COARSE_UNFINISHED, CO_COARSE_FAULTED };
typedef struct CoCoarseOps
{
    void* self;
    int (*built)(void* self, int k);           /* list entry k: 1 ready, -1 its build failed, 0 not yet */
    int (*wait)(void* self, int k);            /* k is not built: 1 once it was built now, 0 to wait for a frame */
    int (*call)(void* self, int k);            /* one draw call: 1 its index moved, 0 not yet, -1 it faulted */
    uint32_t (*queued)(void* self);            /* the generation queue's count */
    int (*drop)(void* self);                   /* co_coarse_queue_drop */
    void (*reset)(void* self);                 /* the generation arena, once no tile of the group reads its buffer */
    void (*done)(void* self, int k, int how);  /* k is finished (CO_COARSE_*), its slot free */
    int (*over)(void* self);                   /* the dispatcher call's time is spent */
} CoCoarseOps;

typedef struct CoCoarseBatch
{
    int columns, batch, limit;  /* limit: draw calls a tile, across frames */
    int next, n;                /* the group: list entries [next, next + n), none while n is 0 */
    uint32_t moved, gaveUp;     /* the group's tiles whose index moved or that ran out of calls, bit i entry next + i */
    int calls[CO_COARSE_BATCH_MAX];
    long frames, groups, dropped, refused, queueWaits;
    int tilesFrameMax;
} CoCoarseBatch;

void co_coarse_begin(CoCoarseBatch* b, int columns, int batch, int limit);
/* One dispatcher call: 1 while tiles remain, 0 when all are finished, -1 after a fault (the merge's state is unknown,
 * so nothing more is drawn). A frame draws only with the queue empty and leaves one entry on it. */
int co_coarse_frame(CoCoarseBatch* b, const int* list, int count, const CoCoarseOps* ops);

#endif
