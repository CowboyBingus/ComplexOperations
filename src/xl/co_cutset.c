/* The trees' navmesh cutters to switch on near players and squads (co_cutset.h). */
#include <math.h>
#include <string.h>

#include "co_cutset.h"

enum { F_WANTED = 1, F_ON = 2 }; /* on only with wanted: the game's own "never added" trees stay out */
#define CELLS ((uint32_t)CO_CUTSET_GRID * CO_CUTSET_GRID)
/* Half a cell's diagonal, rounded up: a cell whose middle is this much farther than a record holds none nearer. */
#define CELL_REACH 23.0f

size_t co_cutset_bytes(void)
{
    return (size_t)CO_CUTSET_SLOTS * (sizeof(CoCutRec) + sizeof(uint32_t) + 1) + (size_t)CELLS * (sizeof(CoCutCell) + sizeof(uint32_t));
}

void co_cutset_init(CoCutSet* s, void* memory)
{
    uint8_t* at = memory;
    memset(s, 0, sizeof *s);
    s->rec = (CoCutRec*)at; at += (size_t)CO_CUTSET_SLOTS * sizeof(CoCutRec);
    s->slot = (uint32_t*)at; at += (size_t)CO_CUTSET_SLOTS * sizeof(uint32_t);
    s->cell = (CoCutCell*)at; at += (size_t)CELLS * sizeof(CoCutCell);
    s->onCell = (uint32_t*)at; at += (size_t)CELLS * sizeof(uint32_t);
    s->flag = at;
    s->cap = CO_CUTSET_CAP; s->adds = CO_CUTSET_ADDS; s->leaveMs = CO_CUTSET_LEAVE_MS;
}

/* A place past the grid, or one that is no number, falls in an edge cell: distances are still taken from the
 * record's own place. */
static int Axis(float v)
{
    const float c = v / CO_CUTSET_CELL + (float)(CO_CUTSET_GRID / 2);
    return !(c > 0.0f) ? 0 : c > (float)(CO_CUTSET_GRID - 1) ? CO_CUTSET_GRID - 1 : (int)c;
}
static uint32_t CellOf(float x, float y) { return (uint32_t)Axis(y) * CO_CUTSET_GRID + (uint32_t)Axis(x); }

void co_cutset_clear(CoCutSet* s)
{
    if (s->dirty)
    {
        memset(s->slot, 0, ((size_t)s->mask + 1) * sizeof(uint32_t));
        memset(s->cell, 0, (size_t)CELLS * sizeof(CoCutCell));
    }
    s->made = s->freeHead = s->records = s->wanted = s->on = s->onCells = s->now = 0; s->dirty = 0;
}

int co_cutset_bind(CoCutSet* s, uintptr_t world, uintptr_t pool, uint32_t mask)
{
    if (world == 0 || pool == 0 || mask == 0 || (mask & (mask + 1)) != 0 || mask >= CO_CUTSET_SLOTS)
    {
        s->stranded += s->wanted - s->on;
        co_cutset_clear(s);
        s->world = s->pool = 0;
        return 0;
    }
    if (world != s->world || pool != s->pool || mask != s->mask)
    {
        s->stranded += s->wanted - s->on;
        co_cutset_clear(s); /* with the old mask: the slots that were in use */
        s->world = world; s->pool = pool; s->mask = mask; s->binds++;
    }
    return 1;
}

int co_cutset_takes(const CoCutSet* s, float x, float y) { return s->world != 0 && isfinite(x) && isfinite(y); }

static void Drop(CoCutSet* s, uint32_t r)
{
    CoCutRec* rec = &s->rec[r];
    CoCutCell* c = &s->cell[CellOf(rec->x, rec->y)];
    uint32_t* link = &c->head;
    while (*link != r + 1) link = &s->rec[*link - 1].next;
    *link = rec->next;
    if (s->flag[r] & F_ON) { c->on--; s->on--; }
    else if (s->flag[r] & F_WANTED) c->pending--;
    if (s->flag[r] & F_WANTED) s->wanted--;
    s->slot[rec->handle & s->mask] = 0;
    rec->next = s->freeHead; s->freeHead = r + 1; s->records--;
}

/* An unbound set has no record, so every slot reads 0. */
void co_cutset_taken(CoCutSet* s, uint32_t handle)
{
    if (handle == 0) return;
    const uint32_t r = s->slot[handle & s->mask];
    if (r != 0) { Drop(s, r - 1); s->replaced++; }
}

/* One record a slot and the mask under CO_CUTSET_SLOTS, so a record is always free once the slot's old one is gone. */
int co_cutset_made(CoCutSet* s, uint32_t handle, float x, float y, int wanted)
{
    if (handle == 0 || !co_cutset_takes(s, x, y)) { s->refused++; return 0; }
    co_cutset_taken(s, handle);
    uint32_t r;
    if (s->freeHead != 0) { r = s->freeHead - 1; s->freeHead = s->rec[r].next; }
    else r = s->made++;
    CoCutCell* c = &s->cell[CellOf(x, y)];
    s->rec[r] = (CoCutRec){handle, x, y, c->head};
    c->head = r + 1;
    s->flag[r] = wanted ? F_WANTED : 0;
    if (wanted) { c->pending++; s->wanted++; }
    s->slot[handle & s->mask] = r + 1; s->records++; s->dirty = 1;
    return 1;
}

uint32_t co_cutset_gone(CoCutSet* s, const uint32_t* handles, uint32_t count)
{
    uint32_t dropped = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t r = handles[i] != 0 ? s->slot[handles[i] & s->mask] : 0;
        if (r == 0) continue;
        if (s->rec[r - 1].handle == handles[i]) { Drop(s, r - 1); dropped++; }
        else s->stale++;
    }
    return dropped;
}

static int Usable(const CoCutCentre* c) { return isfinite(c->x) && isfinite(c->y) && isfinite(c->in) && c->in > 0.0f; }
static float Out(const CoCutCentre* c) { return c->out > c->in ? c->out : c->in; }

/* Every cell the centre's `out` square touches is in reach this tick; its middle's distance orders the cap's sheds. */
static void Mark(CoCutSet* s, const CoCutCentre* c)
{
    const float out = Out(c);
    const int x0 = Axis(c->x - out), x1 = Axis(c->x + out), y0 = Axis(c->y - out), y1 = Axis(c->y + out);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            CoCutCell* cell = &s->cell[(uint32_t)y * CO_CUTSET_GRID + (uint32_t)x];
            const float dx = ((float)(x - CO_CUTSET_GRID / 2) + 0.5f) * CO_CUTSET_CELL - c->x;
            const float dy = ((float)(y - CO_CUTSET_GRID / 2) + 0.5f) * CO_CUTSET_CELL - c->y;
            const float d = sqrtf(dx * dx + dy * dy);
            const uint16_t metres = d > 65535.0f ? 65535 : (uint16_t)d;
            if (cell->at != s->now) { cell->at = s->now; cell->metres = metres; }
            else if (metres < cell->metres) cell->metres = metres;
        }
}

/* Places `v` at `i` of the max-heap `h` of `n`, sinking it under larger children. */
static void Sink(CoCutCand* h, uint32_t n, uint32_t i, CoCutCand v)
{
    for (;;)
    {
        uint32_t k = 2 * i + 1;
        if (k >= n) break;
        if (k + 1 < n && h[k + 1].d2 > h[k].d2) k++;
        if (!(h[k].d2 > v.d2)) break;
        h[i] = h[k]; i = k;
    }
    h[i] = v;
}

/* The `most` nearest stay: a full heap gives up its farthest for a nearer one. */
static void Push(CoCutSet* s, uint32_t* n, uint32_t most, CoCutCand v, CoCutTick* out)
{
    CoCutCand* h = s->heap;
    if (*n < most)
    {
        uint32_t i = (*n)++;
        for (; i > 0 && h[(i - 1) / 2].d2 < v.d2; i = (i - 1) / 2) h[i] = h[(i - 1) / 2];
        h[i] = v;
        return;
    }
    out->over++;
    if (v.d2 < h[0].d2) Sink(h, most, 0, v);
}

static void Collect(CoCutSet* s, const CoCutCentre* c, uint32_t* n, uint32_t most, CoCutTick* out)
{
    const int x0 = Axis(c->x - c->in), x1 = Axis(c->x + c->in), y0 = Axis(c->y - c->in), y1 = Axis(c->y + c->in);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            const CoCutCell* cell = &s->cell[(uint32_t)y * CO_CUTSET_GRID + (uint32_t)x];
            if (cell->pending == 0) continue;
            for (uint32_t r = cell->head; r != 0; r = s->rec[r - 1].next)
            {
                if ((s->flag[r - 1] & (F_WANTED | F_ON)) != F_WANTED) continue;
                const float dx = s->rec[r - 1].x - c->x, dy = s->rec[r - 1].y - c->y, d2 = dx * dx + dy * dy;
                if (d2 <= c->in * c->in) Push(s, n, most, (CoCutCand){d2, r - 1}, out);
            }
        }
}

/* The cell to take cutters from, CELLS for none: one no centre reaches, out for `age` ms or more, the longest out
 * first; else, with `beyond` not negative, the one whose middle is the farthest over `beyond` metres from a centre. */
static uint32_t Pick(CoCutSet* s, uint32_t age, float beyond)
{
    uint32_t best = CELLS, bestKey = 0;
    int bestOut = 0;
    for (uint32_t i = 0; i < s->onCells;)
    {
        const uint32_t ci = s->onCell[i];
        CoCutCell* c = &s->cell[ci];
        if (c->on == 0) { c->listed = 0; s->onCell[i] = s->onCell[--s->onCells]; continue; }
        i++;
        const int isOut = c->at != s->now;
        const uint32_t key = isOut ? s->now - c->at : c->metres;
        if (isOut ? key < age : !(beyond >= 0.0f && (float)key > beyond)) continue;
        if (best == CELLS || isOut > bestOut || (isOut == bestOut && key > bestKey)) { best = ci; bestKey = key; bestOut = isOut; }
    }
    return best;
}

static void Lit(CoCutSet* s, uint32_t r)
{
    const uint32_t ci = CellOf(s->rec[r].x, s->rec[r].y);
    CoCutCell* c = &s->cell[ci];
    s->flag[r] |= F_ON; c->pending--; c->on++; s->on++;
    if (!c->listed) { c->listed = 1; s->onCell[s->onCells++] = ci; }
}

static void On(CoCutSet* s, uint32_t r, CoCutTick* out)
{
    Lit(s, r); s->added++;
    out->add[out->adds++] = s->rec[r].handle;
}

int co_cutset_on(CoCutSet* s, uint32_t handle)
{
    const uint32_t r = handle != 0 ? s->slot[handle & s->mask] : 0;
    if (r == 0 || s->rec[r - 1].handle != handle || (s->flag[r - 1] & (F_WANTED | F_ON)) != F_WANTED) return 0;
    Lit(s, r - 1);
    return 1;
}

/* One of the cell's cutters goes off; the caller saw c->on != 0. */
static void Off(CoCutSet* s, CoCutCell* c, CoCutTick* out)
{
    uint32_t r = c->head;
    while (!(s->flag[r - 1] & F_ON)) r = s->rec[r - 1].next;
    s->flag[r - 1] &= (uint8_t)~F_ON; c->on--; c->pending++; s->on--; s->removed++;
    out->remove[out->removes++] = s->rec[r - 1].handle;
}

void co_cutset_tick(CoCutSet* s, const CoCutCentre* centres, uint32_t count, uint32_t nowMs, uint32_t removes, CoCutTick* out)
{
    out->adds = out->removes = out->waiting = out->over = out->shed = 0; out->capped = 0;
    if (s->world == 0) return;
    /* a cell's `at` equal to the tick's time means "in reach this tick": the time never repeats and is never 0 */
    if (s->now != 0 && (int32_t)(nowMs - s->now) <= 0) nowMs = s->now + 1;
    if (nowMs == 0) nowMs = 1;
    s->now = nowMs; s->dirty = 1;
    if (count > CO_CUTSET_CENTRES) count = CO_CUTSET_CENTRES;
    if (removes > CO_CUTSET_BATCH) removes = CO_CUTSET_BATCH;
    const uint32_t most = s->adds < CO_CUTSET_ADDS ? s->adds : CO_CUTSET_ADDS;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) if (Usable(&centres[i])) Mark(s, &centres[i]);
    for (uint32_t i = 0; i < count && most != 0; i++) if (Usable(&centres[i])) Collect(s, &centres[i], &n, most, out);
    for (uint32_t left = n; left > 1; left--) /* the heap into nearest first */
    {
        const CoCutCand top = s->heap[0];
        Sink(s->heap, left - 1, 0, s->heap[left - 1]);
        s->heap[left - 1] = top;
    }
    int stuck = 0; /* nothing farther than this candidate to shed: none for the ones after it either */
    for (uint32_t i = 0; i < n; i++)
    {
        const uint32_t r = s->heap[i].rec;
        if (s->flag[r] & F_ON) continue; /* in two centres' reach: added under the nearer */
        while (s->on >= s->cap && !stuck)
        {
            const uint32_t ci = out->removes < removes ? Pick(s, 0, sqrtf(s->heap[i].d2) + CELL_REACH) : CELLS;
            if (ci == CELLS) { stuck = 1; break; }
            Off(s, &s->cell[ci], out); out->shed++; s->shed++;
        }
        if (s->on >= s->cap) { out->capped = 1; out->waiting++; continue; }
        On(s, r, out);
    }
    while (out->removes < removes)
    {
        const uint32_t ci = Pick(s, s->leaveMs, -1.0f);
        if (ci == CELLS) break;
        while (s->cell[ci].on != 0 && out->removes < removes) Off(s, &s->cell[ci], out);
    }
}

int co_cutset_covered(const CoCutCentre* centres, uint32_t count, const CoCutCentre* c)
{
    for (uint32_t i = 0; i < count; i++)
    {
        const float dx = c->x - centres[i].x, dy = c->y - centres[i].y, d = sqrtf(dx * dx + dy * dy);
        if (Usable(&centres[i]) && d + c->in <= centres[i].in && d + Out(c) <= Out(&centres[i])) return 1;
    }
    return 0;
}

/* A slot whose hold ran out is free for another peer: the player manager's list changes as players join and leave. */
int co_cutset_player(CoCutHeld* held, uint32_t slots, uint64_t peer, int reads, float x, float y, uint32_t nowMs, float* place)
{
    CoCutHeld* h = NULL;
    CoCutHeld* spare = NULL;
    for (uint32_t i = 0; i < slots && h == NULL; i++)
    {
        if (held[i].used && held[i].peer == peer) h = &held[i];
        else if (spare == NULL && (!held[i].used || nowMs - held[i].at > CO_CUTSET_HOLD_MS)) spare = &held[i];
    }
    if (reads && isfinite(x) && isfinite(y))
    {
        if (h == NULL) h = spare;
        if (h != NULL) *h = (CoCutHeld){peer, x, y, nowMs, 1};
        place[0] = x; place[1] = y;
        return 1;
    }
    if (h == NULL || nowMs - h->at > CO_CUTSET_HOLD_MS) return 0;
    place[0] = h->x; place[1] = h->y;
    return 2;
}
