/* The coarse terrain's draw frames: tiles of a step share a frame and never touch, touching tiles keep the tile order's
 * sequence, every tile is drawn once and every frame's triangles are rendered once (one queue entry a drain), the arena
 * is reset only when no tile of the group reads its buffer, and a level cut short leaves nothing for the next one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "xl/co_coarse.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define COLUMNS 24
#define TILES (COLUMNS * COLUMNS)
#define EMITS 4 /* draw calls with triangles a tile may make */

/* TerrainTileWanted: the tiles within the playable disk plus one tile. */
static int Wanted(int* list)
{
    int n = 0;
    for (int t = 0; t < TILES; t++)
    {
        double e = 704.0, c = 8192.0, r = 8192.0 + e, x0 = (t % COLUMNS) * e, y0 = (t / COLUMNS) * e;
        double dx = c < x0 ? x0 - c : (c > x0 + e ? c - (x0 + e) : 0.0), dy = c < y0 ? y0 - c : (c > y0 + e ? c - (y0 + e) : 0.0);
        if (dx * dx + dy * dy <= r * r) list[n++] = t;
    }
    return n;
}

static int Touch(int a, int b) { return a != b && abs(a % COLUMNS - b % COLUMNS) <= 1 && abs(a / COLUMNS - b / COLUMNS) <= 1; }

/* The game as the batch sees it: built tiles, a draw call that emits a tile's triangles until its last call moves the
 * index, a queue whose drain renders the gui put there since the last render once an entry. */
typedef struct
{
    const int* list; int count;
    int readyAt[TILES];   /* the frame a list entry is built from; -1 a failed build */
    int need[TILES];      /* draw calls until the index moves; the last emits nothing (phase 2: the append only) */
    int faultAt;          /* list entry whose first call faults, -1 none */
    int refuseAt;         /* the drop call that finds two different entries, -1 none */
    int selfBuild, overEvery, limit;
    int frame, queued, drops, calls[TILES];
    int frameTiles[CO_COARSE_BATCH_MAX + 1], frameCount;  /* tiles that emitted in this frame */
    int gui[TILES * EMITS], guiCount;                      /* emissions not yet rendered, by tile */
    int emits[TILES], renders[TILES], firstFrame[TILES], lastFrame[TILES];
    int done[TILES], how[TILES], buffer[TILES], resets, badReset, touched, twice, overCalls;
} Game;

static int Built(void* self, int k)
{
    Game* g = self;
    if (g->readyAt[k] < 0) return -1;
    return g->frame >= g->readyAt[k];
}
static int Wait(void* self, int k)
{
    Game* g = self;
    if (!g->selfBuild || g->readyAt[k] < 0) return 0;
    g->readyAt[k] = g->frame;
    return 1;
}
static int Call(void* self, int k)
{
    Game* g = self;
    int t = g->list[k];
    if (k == g->faultAt) return -1;
    g->calls[k]++;
    g->queued++; /* the append, the call's last act */
    if (g->calls[k] < g->need[k])
    {
        for (int i = 0; i < g->frameCount; i++) if (Touch(g->frameTiles[i], t)) g->touched++;
        g->frameTiles[g->frameCount++] = t;
        g->gui[g->guiCount++] = t; g->emits[t]++;
        if (g->firstFrame[t] < 0) g->firstFrame[t] = g->frame;
        g->lastFrame[t] = g->frame;
        g->buffer[k] = 1;
        return 0;
    }
    g->buffer[k] = 0;
    return 1;
}
static uint32_t Queued(void* self) { return (uint32_t)((Game*)self)->queued; }
static int Drop(void* self)
{
    Game* g = self;
    if (g->drops++ == g->refuseAt || g->queued < 2) return 0;
    g->queued--;
    return 1;
}
static void Reset(void* self)
{
    Game* g = self;
    g->resets++;
    for (int k = 0; k < g->count; k++) if (g->buffer[k] && g->calls[k] < g->limit) g->badReset++;
}
static void Done(void* self, int k, int how) { Game* g = self; g->done[k]++; g->how[k] = how; }
static int Over(void* self) { Game* g = self; return g->overEvery > 0 && ++g->overCalls % g->overEvery == 0; }

static CoCoarseOps Ops(Game* g) { CoCoarseOps o = {g, Built, Wait, Call, Queued, Drop, Reset, Done, Over}; return o; }

static void Setup(Game* g, const char* name, const int* list, int count, int built)
{
    g_case = name;
    memset(g, 0, sizeof *g);
    g->list = list; g->count = count; g->faultAt = -1; g->refuseAt = -1; g->limit = 4096;
    for (int k = 0; k < TILES; k++) { g->readyAt[k] = built ? 0 : k / 7; g->need[k] = 2; g->firstFrame[k] = g->lastFrame[k] = -1; }
}

/* The drain at the frame's end (0xAB0590): every entry renders the gui put there since the last render. */
static void Drain(Game* g)
{
    if (g->queued > 1 && g->guiCount > 0) g->twice++;
    if (g->queued > 0)
    {
        for (int i = 0; i < g->guiCount; i++) g->renders[g->gui[i]] += g->queued;
        g->guiCount = 0;
    }
    g->queued = 0; g->frameCount = 0; g->frame++;
}

/* Frames until the batch is done (or `stop` frames), each drained: what co_coarse_frame returned last. */
static int Run(Game* g, CoCoarseBatch* b, int stop)
{
    const CoCoarseOps ops = Ops(g);
    int r = 1;
    for (int f = 0; f < 100000 && (stop <= 0 || f < stop) && r > 0; f++) { r = co_coarse_frame(b, g->list, g->count, &ops); Drain(g); }
    return r;
}

/* Each tile drawn once, its triangles rendered once, neighbours in the tile order's sequence and never in one frame. */
static void Whole(const Game* g, const CoCoarseBatch* b)
{
    int once = 1, rendered = 1, order = 1;
    for (int k = 0; k < g->count; k++)
    {
        int t = g->list[k];
        if (g->done[k] != 1 || g->how[k] != CO_COARSE_DRAWN) once = 0;
        if (g->renders[t] != g->emits[t] || g->emits[t] == 0) rendered = 0;
    }
    for (int i = 0; i < g->count; i++)
        for (int j = 0; j < g->count; j++)
        {
            int a = g->list[i], c = g->list[j];
            if (a < c && Touch(a, c) && !(g->lastFrame[a] < g->firstFrame[c])) order = 0;
        }
    CHECK(once);
    CHECK(rendered);
    CHECK(order);
    CHECK(g->touched == 0);
    CHECK(g->twice == 0);
    CHECK(g->badReset == 0);
    CHECK(b->next == g->count && b->n == 0);
}

int main(void)
{
    static int list[TILES], tileOrder[TILES];
    const int count = Wanted(tileOrder);
    Game* g = calloc(1, sizeof *g);
    CoCoarseBatch b;

    g_case = "the wanted tiles";
    CHECK(count == 515);

    g_case = "the order";
    memcpy(list, tileOrder, sizeof list);
    co_coarse_order(list, count, COLUMNS);
    {
        int sorted = 1, seen[TILES] = {0}, perm = 1, most = 0;
        for (int k = 0; k < count; k++)
        {
            if (seen[list[k]]++) perm = 0;
            if (k > 0)
            {
                int s0 = co_coarse_step(list[k - 1], COLUMNS), s1 = co_coarse_step(list[k], COLUMNS);
                if (s0 > s1 || (s0 == s1 && list[k - 1] > list[k])) sorted = 0;
            }
        }
        for (int k = 0; k < count; k++) if (!seen[tileOrder[k]]) perm = 0;
        for (int k = 0; k < count; k++)
        {
            int n = co_coarse_group(list, count, k, COLUMNS, 100);
            if (n > most) most = n;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (Touch(list[k + i], list[k + j])) sorted = 0;
        }
        CHECK(perm);
        CHECK(sorted);
        CHECK(most == CO_COARSE_BATCH_MAX);
    }

    /* the frames a batch size takes with every tile built: two a group (the drawing call, then the one that moves on),
     * each removing the entries of all but one tile */
    {
        int perStep[3 * COLUMNS] = {0};
        for (int k = 0; k < count; k++) perStep[co_coarse_step(list[k], COLUMNS)]++;
        for (int batch = 1; batch <= CO_COARSE_BATCH_MAX; batch++)
        {
            long groups = 0;
            for (int s = 0; s < 3 * COLUMNS; s++) groups += (perStep[s] + batch - 1) / batch;
            char name[64];
            snprintf(name, sizeof name, "every tile built, batch %d", batch);
            Setup(g, name, list, count, 1);
            co_coarse_begin(&b, COLUMNS, batch, 4096);
            CHECK(Run(g, &b, 0) == 0);
            Whole(g, &b);
            CHECK(b.groups == groups && b.frames == 2 * groups && b.tilesFrameMax == batch && b.dropped == 2 * (count - b.groups) && b.refused == 0);
            CHECK(g->resets == b.groups);
            CHECK(batch != 1 || b.frames == 1030);
            CHECK(batch != CO_COARSE_BATCH || b.frames == 300);
            printf("batch %2d: %4ld draw frames, %3ld groups, %3ld entries removed\n", batch, b.frames, b.groups, b.dropped);
        }
    }

    Setup(g, "tiles built as the runners get to them, a frame budget spent", list, count, 0);
    g->overEvery = 3;
    for (int k = 0; k < count; k += 9) g->need[k] = 3; /* the first call ran out of its clock */
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 0) == 0);
    Whole(g, &b);
    CHECK(b.tilesFrameMax <= CO_COARSE_BATCH && b.refused == 0);

    Setup(g, "the dispatcher's thread builds what no runner has", list, count, 0);
    g->selfBuild = 1;
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 0) == 0);
    Whole(g, &b);

    Setup(g, "a build that failed", list, count, 1);
    g->readyAt[41] = -1; g->readyAt[42] = -1;
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 0) == 0);
    CHECK(g->done[41] == 1 && g->how[41] == CO_COARSE_UNBUILT && g->calls[41] == 0 && g->done[42] == 1 && g->how[42] == CO_COARSE_UNBUILT);
    {
        int others = 1;
        for (int k = 0; k < count; k++) if (k != 41 && k != 42 && (g->done[k] != 1 || g->how[k] != CO_COARSE_DRAWN)) others = 0;
        CHECK(others && g->twice == 0 && g->touched == 0 && g->badReset == 0);
    }

    Setup(g, "a draw that does not finish", list, count, 1);
    g->need[50] = 100000; g->limit = 6;
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 6);
    CHECK(Run(g, &b, 0) == 0);
    CHECK(g->done[50] == 1 && g->how[50] == CO_COARSE_UNFINISHED && g->calls[50] == 6);
    CHECK(g->twice == 0 && g->touched == 0 && g->badReset == 0 && b.next == count);

    Setup(g, "a draw that faults", list, count, 1);
    g->faultAt = 101;
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 0) == -1);
    CHECK(g->done[101] == 1 && g->how[101] == CO_COARSE_FAULTED && b.next == count && b.n == 0);
    { const CoCoarseOps ops = Ops(g); CHECK(co_coarse_frame(&b, list, count, &ops) == 0); }
    {
        int after = 0;
        for (int k = 102; k < count; k++) after += g->done[k] + g->calls[k];
        CHECK(after == 0 && g->twice == 0);
    }

    Setup(g, "two entries that differ", list, count, 1);
    g->refuseAt = 5;
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 0) == 0);
    CHECK(b.refused == 1 && b.batch == 1);
    {
        int once = 1;
        for (int k = 0; k < count; k++) if (g->done[k] != 1 || g->how[k] != CO_COARSE_DRAWN) once = 0;
        CHECK(once && g->touched == 0 && g->badReset == 0);
    }

    /* TerrainStep's level that ended inside its section: CoarseEnd, then the next level's CoarseStart begins again */
    Setup(g, "a level cut short in a group", list, count, 1);
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(Run(g, &b, 7) == 1);
    CHECK(b.n > 0 && b.moved == 0 && g->queued == 0);
    {
        int drawn = 0, cut = b.next;
        for (int k = 0; k < count; k++) drawn += g->done[k];
        CHECK(drawn == cut);
    }
    Setup(g, "the next level after a cut", list, count, 1);
    co_coarse_begin(&b, COLUMNS, CO_COARSE_BATCH, 4096);
    CHECK(b.next == 0 && b.n == 0 && b.frames == 0 && b.moved == 0);
    CHECK(Run(g, &b, 0) == 0);
    Whole(g, &b);

    g_case = "repeated queue entries";
    {
        uint8_t* q = calloc(1, 0x18 + CO_COARSE_ENTRIES * CO_COARSE_ENTRY + 0x40);
        uint8_t* e0 = q + 0x18, * e1 = e0 + CO_COARSE_ENTRY;
        uint32_t id = 0xFFFFFFFFu;
        for (int i = 0; i < 2; i++)
        {
            uint8_t* e = i ? e1 : e0;
            memset(e, 0x5A + i, CO_COARSE_ENTRY); /* bytes 0xF4A195 does not write */
            for (int j = 0; j < 0x20; j++) e[j] = (uint8_t)(j * 7 + 1);
            memset(e + 0x20, 0, 4); e[0x24] = 0; memset(e + 0x38, 0, 2);
            for (int j = 0; j < 16; j++) e[0x58 + j] = (uint8_t)(j + 3);
            memcpy(e + 0x68, &id, 4);
        }
        CHECK(co_coarse_entries_same(e0, e1));
        uint32_t two = 2, one = 1;
        memcpy(q + 0x10, &two, 4);
        CHECK(co_coarse_queue_drop((uintptr_t)q) == 1);
        uint32_t n; memcpy(&n, q + 0x10, 4);
        LONG64 ticket, serving; memcpy(&ticket, q, 8); memcpy(&serving, q + 8, 8);
        CHECK(n == 1 && ticket == 1 && serving == 1);
        CHECK(co_coarse_queue_drop((uintptr_t)q) == 0); /* one entry: nothing to remove */
        memcpy(&n, q + 0x10, 4);
        CHECK(n == 1);
        memcpy(q + 0x10, &two, 4);
        e1[0x08] ^= 1; /* another camera */
        CHECK(!co_coarse_entries_same(e0, e1) && co_coarse_queue_drop((uintptr_t)q) == 0);
        e1[0x08] ^= 1; e1[0x5F] ^= 1; /* other settings */
        CHECK(!co_coarse_entries_same(e0, e1));
        e1[0x5F] ^= 1; memcpy(e1 + 0x68, &one, 4); /* an entry with an id of its own */
        CHECK(!co_coarse_entries_same(e0, e1));
        memcpy(&n, q + 0x10, 4);
        CHECK(n == 2);
        free(q);
    }

    free(g);
    if (g_failed) { printf("coarse: %d checks failed\n", g_failed); return 1; }
    printf("coarse: all checks passed\n");
    return 0;
}
