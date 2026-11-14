/* The tree cutters' set (co_cutset.h): one record a pool slot, what a tick adds (wanted, in reach, nearest first,
 * paced) and removes (out of reach for 5 s, capped; at the cap the farthest for a nearer one), the drops on a new
 * world or pool, and stale handles. After every case the set's counters are checked against its records. The last
 * case is 180 000 trees with 8 players and 56 squads against a brute-force answer, with the tick's time printed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "xl/co_cutset.h"

static int g_failed;
static const char* g_case = "";
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s (line %d)\n", g_case, #what, __LINE__); g_failed++; } } while (0)

#define MASK 0xFFFFFu
#define H(i) (0x81100000u | (uint32_t)(i))  /* world 2, live bit 2^20, slot i */
#define G(i) (H(i) | 0x200000u)             /* the same slot's other generation */
#define GRID CO_CUTSET_GRID

/* Each with a guard behind it: a tick that writes past its adds, its removes or the set's heap fails a check. */
static struct { CoCutSet set; uint32_t guard[1024]; } g_s;
static struct { CoCutTick out; uint32_t guard[256]; } g_o;
#define g_set g_s.set
#define g_out g_o.out
static void* g_memory;

static void Fresh(const char* name)
{
    g_case = name;
    free(g_memory);
    g_memory = calloc(1, co_cutset_bytes());
    co_cutset_init(&g_set, g_memory);
    co_cutset_bind(&g_set, 0x1000, 0x2000, MASK);
}

/* The set's counters, cells, slots and on-cell list recomputed from its records. */
static void Sound(void)
{
    CoCutSet* s = &g_set;
    uint32_t records = 0, wanted = 0, on = 0, chained = 0, listedOn = 0, listed = 0, cellsOn = 0;
    int ok = 1;
    for (uint32_t i = 0; i < 1024; i++) ok &= g_s.guard[i] == 0;
    for (uint32_t i = 0; i <= s->mask && s->world != 0; i++)
    {
        const uint32_t r = s->slot[i];
        if (r == 0) continue;
        records++;
        ok &= r <= s->made && (s->rec[r - 1].handle & s->mask) == i;
        if (s->flag[r - 1] & 1) wanted++;
        if (s->flag[r - 1] & 2) { on++; ok &= (s->flag[r - 1] & 1) != 0; }
    }
    for (uint32_t c = 0; c < (uint32_t)GRID * GRID; c++)
    {
        uint32_t pending = 0, cellOn = 0;
        for (uint32_t r = s->cell[c].head; r != 0 && chained <= s->made; r = s->rec[r - 1].next)
        {
            chained++;
            ok &= s->slot[s->rec[r - 1].handle & s->mask] == r;
            if ((s->flag[r - 1] & 3) == 1) pending++;
            if (s->flag[r - 1] & 2) cellOn++;
        }
        ok &= pending == s->cell[c].pending && cellOn == s->cell[c].on;
        if (cellOn != 0) { cellsOn++; ok &= s->cell[c].listed == 1; }
        if (s->cell[c].listed) listed++;
    }
    for (uint32_t i = 0; i < s->onCells; i++) if (s->cell[s->onCell[i]].on != 0) listedOn++;
    CHECK(ok);
    CHECK(records == s->records && chained == records);
    CHECK(wanted == s->wanted && on == s->on);
    CHECK(listedOn == cellsOn && listed == s->onCells);
}

static CoCutTick* Tick(const CoCutCentre* c, uint32_t n, uint32_t now, uint32_t removes)
{
    int whole = 1;
    memset(&g_o, 0xEE, sizeof g_o);
    co_cutset_tick(&g_set, c, n, now, removes, &g_out);
    for (uint32_t i = 0; i < 256; i++) whole &= g_o.guard[i] == 0xEEEEEEEEu;
    for (uint32_t i = 0; i < 1024; i++) whole &= g_s.guard[i] == 0;
    CHECK(whole);
    return &g_out;
}
static int Adds(uint32_t handle) { for (uint32_t i = 0; i < g_out.adds; i++) if (g_out.add[i] == handle) return 1; return 0; }
static int Removes(uint32_t handle) { for (uint32_t i = 0; i < g_out.removes; i++) if (g_out.remove[i] == handle) return 1; return 0; }
static CoCutCentre Player(float x, float y) { return (CoCutCentre){x, y, CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT}; }

static void Binding(void)
{
    g_case = "bind";
    free(g_memory);
    g_memory = calloc(1, co_cutset_bytes());
    co_cutset_init(&g_set, g_memory);
    CHECK(g_set.cap == CO_CUTSET_CAP && g_set.adds == CO_CUTSET_ADDS && g_set.leaveMs == CO_CUTSET_LEAVE_MS);
    CHECK(co_cutset_takes(&g_set, 1.0f, 2.0f) == 0);
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 0 && g_set.records == 0 && g_set.refused == 1);
    CHECK(co_cutset_gone(&g_set, (const uint32_t[]){H(1)}, 1) == 0);
    co_cutset_taken(&g_set, H(1));
    CoCutCentre c = Player(0, 0);
    Tick(&c, 1, 1000, 16);
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.waiting == 0 && g_out.over == 0 && g_out.shed == 0 && g_out.capped == 0);
    CHECK(g_set.now == 0); /* no tick ran */
    CHECK(co_cutset_bind(&g_set, 0, 0x2000, MASK) == 0);
    CHECK(co_cutset_bind(&g_set, 0x1000, 0, MASK) == 0);
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, 0) == 0);
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, 0xFFFFEu) == 0);   /* not 2^k - 1 */
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, 0x1FFFFFu) == 0);  /* more slots than the set has */
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, 0xFFFFFFFFu) == 0);
    CHECK(g_set.world == 0 && g_set.binds == 0);
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, 0xFFFFu) == 1 && g_set.binds == 1); /* HellForge's own room */
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, MASK) == 1 && g_set.binds == 2);
    CHECK(co_cutset_takes(&g_set, 1.0f, 2.0f) == 1);
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 1 && co_cutset_made(&g_set, H(2), 1.0f, 2.0f, 0) == 1);
    CHECK(g_set.records == 2 && g_set.wanted == 1);
    Tick(&c, 1, 1000, 16);
    CHECK(g_set.on == 1);
    CHECK(co_cutset_bind(&g_set, 0x1000, 0x2000, MASK) == 1 && g_set.records == 2 && g_set.on == 1 && g_set.binds == 2);
    Sound();
    /* a new AI world, a new pool, a new mask: every record goes */
    CHECK(co_cutset_bind(&g_set, 0x1008, 0x2000, MASK) == 1 && g_set.records == 0 && g_set.on == 0 && g_set.wanted == 0 && g_set.binds == 3);
    CHECK(g_set.now == 0 && g_set.onCells == 0); /* the next tick's time is taken as it comes */
    Sound();
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 1);
    CHECK(co_cutset_bind(&g_set, 0x1008, 0x2008, MASK) == 1 && g_set.records == 0 && g_set.binds == 4);
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 1);
    CHECK(co_cutset_bind(&g_set, 0x1008, 0x2008, 0x7FFFFu) == 1 && g_set.records == 0 && g_set.binds == 5);
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 1 && g_set.records == 1);
    Tick(&c, 1, 2000, 16);
    CHECK(g_out.adds == 1); /* the dropped record's "on" did not stay with the slot */
    Sound();
    /* the world gone: nothing kept, nothing taken */
    CHECK(co_cutset_bind(&g_set, 0, 0, 0) == 0 && g_set.records == 0 && g_set.world == 0 && g_set.pool == 0);
    CHECK(co_cutset_made(&g_set, H(1), 1.0f, 2.0f, 1) == 0);
    CHECK(co_cutset_bind(&g_set, 0x1008, 0x2008, 0x7FFFFu) == 1 && g_set.binds == 6);
    Sound();
}

static void Records(void)
{
    Fresh("records");
    const float none = nanf(""), inf = INFINITY;
    CHECK(co_cutset_made(&g_set, 0, 1.0f, 2.0f, 1) == 0);
    CHECK(co_cutset_takes(&g_set, none, 0.0f) == 0 && co_cutset_takes(&g_set, 0.0f, -inf) == 0);
    CHECK(co_cutset_takes(&g_set, 0.0f, none) == 0 && co_cutset_takes(&g_set, inf, 0.0f) == 0);
    CHECK(co_cutset_made(&g_set, H(5), none, 2.0f, 1) == 0 && co_cutset_made(&g_set, H(5), 1.0f, inf, 1) == 0);
    CHECK(g_set.records == 0 && g_set.refused == 3);
    /* a handle of 0 names no slot: slot 0's record stays through a 0 in a destroy and a 0 "taken" */
    CHECK(co_cutset_made(&g_set, H(0), 7.0f, 7.0f, 1) == 1);
    const uint32_t zeros[2] = {0, 0};
    CHECK(co_cutset_gone(&g_set, zeros, 2) == 0 && g_set.stale == 0);
    co_cutset_taken(&g_set, 0);
    CHECK(g_set.records == 1 && g_set.replaced == 0);
    const uint32_t first[1] = {H(0)};
    CHECK(co_cutset_gone(&g_set, first, 1) == 1 && g_set.records == 0);
    CHECK(co_cutset_made(&g_set, H(5), 10.0f, 20.0f, 1) == 1);
    CHECK(co_cutset_made(&g_set, H(6), 11.0f, 20.0f, 0) == 1);
    CHECK(co_cutset_made(&g_set, H(7), 500.0f, -300.0f, 1) == 1);
    CHECK(g_set.records == 3 && g_set.wanted == 2 && g_set.on == 0);
    Sound();
    /* the engine's batch: zeros and handles the set never saw pass */
    const uint32_t batch[4] = {0, H(6), H(900), H(5)};
    CHECK(co_cutset_gone(&g_set, batch, 4) == 2 && g_set.records == 1 && g_set.wanted == 1 && g_set.stale == 0);
    CHECK(co_cutset_gone(&g_set, batch, 4) == 0);
    Sound();
    /* the slot's other generation in a destroy: the engine leaves the slot alone, and so does the set */
    const uint32_t other[1] = {G(7)};
    CHECK(co_cutset_gone(&g_set, other, 1) == 0 && g_set.records == 1 && g_set.stale == 1);
    /* a make in a slot that still has a record: the old cutter is dead */
    CHECK(co_cutset_made(&g_set, G(7), -40.0f, 60.0f, 0) == 1 && g_set.records == 1 && g_set.wanted == 0 && g_set.replaced == 1);
    const uint32_t old[1] = {H(7)};
    CHECK(co_cutset_gone(&g_set, old, 1) == 0 && g_set.records == 1 && g_set.stale == 2);
    CHECK(co_cutset_gone(&g_set, other, 1) == 1 && g_set.records == 0);
    Sound();
    /* another kind of cutter in the slot */
    CHECK(co_cutset_made(&g_set, H(8), 1.0f, 1.0f, 1) == 1);
    co_cutset_taken(&g_set, 0);
    co_cutset_taken(&g_set, H(9));
    CHECK(g_set.records == 1 && g_set.replaced == 1);
    co_cutset_taken(&g_set, G(8));
    CHECK(g_set.records == 0 && g_set.wanted == 0 && g_set.replaced == 2);
    Sound();
    /* freed records are used again: the set never grows past the most alive at once */
    for (uint32_t i = 0; i < 1000; i++) co_cutset_made(&g_set, H(100 + i), (float)(i % 50) * 7.0f, (float)(i / 50) * 9.0f, 1);
    CHECK(g_set.records == 1000 && g_set.made == 1000);
    uint32_t some[500];
    for (uint32_t i = 0; i < 500; i++) some[i] = H(100 + 2 * i);
    CHECK(co_cutset_gone(&g_set, some, 500) == 500);
    Sound();
    for (uint32_t i = 0; i < 500; i++) co_cutset_made(&g_set, H(5000 + i), (float)i, 3.0f, 1);
    CHECK(g_set.records == 1000 && g_set.made == 1000 && g_set.wanted == 1000);
    Sound();
    /* a level's end: every record goes, also the freed ones kept for reuse; the binding stays */
    CHECK(co_cutset_gone(&g_set, some + 1, 1) == 0 && co_cutset_gone(&g_set, (const uint32_t[]){H(5000)}, 1) == 1);
    co_cutset_clear(&g_set);
    CHECK(g_set.records == 0 && g_set.wanted == 0 && g_set.made == 0 && g_set.world == 0x1000 && g_set.binds == 1);
    CHECK(co_cutset_made(&g_set, H(100), 3.0f, 3.0f, 1) == 1 && g_set.records == 1 && g_set.made == 1);
    Sound();
}

static void AddsInReach(void)
{
    Fresh("adds: wanted, in reach, nearest first");
    CoCutCentre c = Player(0, 0);
    co_cutset_made(&g_set, H(1), 100.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(2), 0.0f, -10.0f, 1);
    co_cutset_made(&g_set, H(3), -159.5f, 0.0f, 1);
    co_cutset_made(&g_set, H(4), 0.0f, 160.0f, 1);   /* at the radius: in */
    co_cutset_made(&g_set, H(5), 0.0f, 160.5f, 1);   /* past it, in the same cells' reach */
    co_cutset_made(&g_set, H(6), 300.0f, 300.0f, 1);
    co_cutset_made(&g_set, H(7), 5.0f, 0.0f, 0);     /* the game never adds this tree's cutter */
    co_cutset_made(&g_set, H(8), 113.2f, 113.2f, 1); /* in the square, outside the disc (160.09 m) */
    Tick(&c, 1, 1000, 16);
    CHECK(g_out.adds == 4 && g_out.add[0] == H(2) && g_out.add[1] == H(1) && g_out.add[2] == H(3) && g_out.add[3] == H(4));
    CHECK(g_out.removes == 0 && g_out.waiting == 0 && g_out.over == 0 && g_out.capped == 0 && g_set.on == 4 && g_set.added == 4);
    Sound();
    Tick(&c, 1, 1250, 16);
    CHECK(g_out.adds == 0 && g_set.on == 4);
    /* one made later is added on the next tick */
    co_cutset_made(&g_set, H(9), -20.0f, 20.0f, 1);
    Tick(&c, 1, 1500, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(9));
    Sound();
    /* the player 40 m on: the tree now 199.5 m behind is past `in` and inside `out`, and stays */
    c = Player(40.0f, 0.0f);
    Tick(&c, 1, 2000, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(8)); /* and the one that was past the disc is in it now */
    Tick(&c, 1, 8000, 16);
    CHECK(g_out.removes == 0 && g_set.on == 6);

    Fresh("adds: paced");
    c = Player(0, 0);
    g_set.adds = 2;
    for (uint32_t i = 0; i < 5; i++) co_cutset_made(&g_set, H(10 + i), 150.0f - 30.0f * (float)i, 0.0f, 1); /* 150 .. 30 m */
    Tick(&c, 1, 1000, 16);
    CHECK(g_out.adds == 2 && g_out.add[0] == H(14) && g_out.add[1] == H(13) && g_out.over == 3 && g_out.waiting == 0);
    Tick(&c, 1, 1250, 16);
    CHECK(g_out.adds == 2 && g_out.add[0] == H(12) && g_out.add[1] == H(11) && g_out.over == 1);
    Tick(&c, 1, 1500, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(10) && g_out.over == 0);
    g_set.adds = 0; /* adds off */
    co_cutset_made(&g_set, H(20), 1.0f, 1.0f, 1);
    Tick(&c, 1, 1750, 16);
    CHECK(g_out.adds == 0 && g_out.over == 0 && g_set.on == 5);
    g_set.adds = 100000; /* never more than the tick's array */
    for (uint32_t i = 0; i < CO_CUTSET_ADDS + 40; i++) co_cutset_made(&g_set, H(1000 + i), -100.0f + 0.25f * (float)i, 50.0f, 1);
    Tick(&c, 1, 2000, 16);
    CHECK(g_out.adds == CO_CUTSET_ADDS && g_out.add[0] == H(20) && g_out.over == 41);
    int nearestFirst = 1;
    for (uint32_t i = 2; i < g_out.adds; i++)
    {
        const float a = fabsf(-100.0f + 0.25f * (float)(g_out.add[i - 1] - H(1000))), b = fabsf(-100.0f + 0.25f * (float)(g_out.add[i] - H(1000)));
        nearestFirst &= a <= b;
    }
    CHECK(nearestFirst);
    Tick(&c, 1, 2250, 16);
    CHECK(g_out.adds == 41 && g_out.over == 0);
    Sound();

    Fresh("adds: paced, the farthest seen first");
    c = Player(0, 0);
    g_set.adds = 2;
    for (uint32_t i = 0; i < 5; i++) co_cutset_made(&g_set, H(30 + i), -30.0f - 30.0f * (float)i, 0.0f, 1); /* -30 .. -150 m */
    Tick(&c, 1, 1000, 16); /* the cells are walked from -150 m: the two nearest still win */
    CHECK(g_out.adds == 2 && g_out.add[0] == H(30) && g_out.add[1] == H(31) && g_out.over == 3);
    Tick(&c, 1, 1250, 16);
    CHECK(g_out.adds == 2 && g_out.add[0] == H(32) && g_out.add[1] == H(33) && g_out.over == 1);
    Sound();

    Fresh("adds: two centres");
    CoCutCentre two[2] = {Player(0, 0), {200.0f, 0.0f, CO_CUTSET_SQUAD_IN, CO_CUTSET_SQUAD_OUT}};
    co_cutset_made(&g_set, H(1), 150.0f, 0.0f, 1);  /* 150 m from the player, 50 m from the squad: once, as the nearer */
    co_cutset_made(&g_set, H(2), -30.0f, 0.0f, 1);  /* 30 m from the player */
    co_cutset_made(&g_set, H(3), 262.0f, 0.0f, 1);  /* the squad's alone, 62 m */
    co_cutset_made(&g_set, H(4), 265.0f, 0.0f, 1);  /* 65 m from the squad: inside its `out`, not its `in` */
    Tick(two, 2, 1000, 16);
    CHECK(g_out.adds == 3 && g_out.add[0] == H(2) && g_out.add[1] == H(1) && g_out.add[2] == H(3) && g_set.on == 3 && g_out.waiting == 0);
    Sound();
    /* centres that are no place, or reach nothing, are passed over; only the first 64 count */
    Fresh("adds: centres");
    CoCutCentre odd[CO_CUTSET_CENTRES + 1];
    for (uint32_t i = 0; i < CO_CUTSET_CENTRES + 1; i++) odd[i] = (CoCutCentre){nanf(""), 0.0f, 64.0f, 96.0f};
    odd[1] = (CoCutCentre){0.0f, INFINITY, 64.0f, 96.0f};
    odd[2] = (CoCutCentre){16.0f, 18.0f, 0.0f, 96.0f};   /* on the tree itself, reaching nothing */
    odd[3] = (CoCutCentre){16.0f, 16.0f, -5.0f, 96.0f};
    odd[4] = (CoCutCentre){0.0f, 0.0f, nanf(""), 96.0f};
    odd[5] = (CoCutCentre){0.0f, 0.0f, INFINITY, 96.0f};
    odd[CO_CUTSET_CENTRES] = Player(0, 0);
    co_cutset_made(&g_set, H(1), 16.0f, 18.0f, 1);
    Tick(odd, CO_CUTSET_CENTRES + 1, 1000, 16);
    CHECK(g_out.adds == 0);
    odd[CO_CUTSET_CENTRES - 1] = (CoCutCentre){0.0f, 0.0f, 64.0f, nanf("")}; /* an `out` that is no number: the `in` */
    Tick(odd, CO_CUTSET_CENTRES + 1, 1250, 16);
    CHECK(g_out.adds == 1);
    Tick(odd, CO_CUTSET_CENTRES + 1, 7000, 16); /* and its cell is in that centre's reach */
    CHECK(g_out.removes == 0 && g_set.on == 1);
    Sound();
    /* a centre that is no place keeps no cell in reach: trees at the grid's edges, where such a place would fall */
    Fresh("adds: a centre that is no place");
    const float rim = 0.5f * (float)GRID * CO_CUTSET_CELL + 8.0f;
    CoCutCentre real[2] = {Player(-rim, 0.0f), Player(0.0f, rim)};
    co_cutset_made(&g_set, H(1), -rim, 0.0f, 1);
    co_cutset_made(&g_set, H(2), 0.0f, rim, 1);
    Tick(real, 2, 1000, 16);
    CHECK(g_out.adds == 2);
    CoCutCentre bad[2] = {{nanf(""), 0.0f, 64.0f, 96.0f}, {0.0f, INFINITY, 64.0f, 96.0f}};
    Tick(bad, 2, 5999, 16);
    CHECK(g_out.removes == 0);
    Tick(bad, 2, 6000, 16);
    CHECK(g_out.removes == 2 && g_set.on == 0);
    Sound();

    Fresh("adds: past the grid");
    const float edge = 0.5f * (float)GRID * CO_CUTSET_CELL; /* 8192 m */
    co_cutset_made(&g_set, H(1), edge + 5000.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(2), -edge - 5000.0f, -edge - 9000.0f, 1);
    co_cutset_made(&g_set, H(3), edge - 1.0f, 0.0f, 1);
    CoCutCentre away = Player(edge + 5010.0f, 0.0f);
    Tick(&away, 1, 1000, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(1)); /* the edge cell holds H(3) too, 5 km away */
    away = Player(-edge - 5000.0f, -edge - 8900.0f);
    Tick(&away, 1, 1250, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(2));
    /* a place a whole grid past the edge is still the edge cell, not the cell a grid's width back */
    co_cutset_made(&g_set, H(4), 2.0f * edge + 10.0f, 0.0f, 1);
    away = Player(2.0f * edge + 10.0f, 0.0f);
    Tick(&away, 1, 1500, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(4));
    away = Player(10.0f, 37.0f);
    Tick(&away, 1, 1750, 0);
    Tick(&away, 1, 7000, 16);
    CHECK(g_out.removes == 3 && g_set.on == 0);
    Sound();
}

static void Leaving(void)
{
    Fresh("removes: out of reach for 5 s");
    CoCutCentre c = Player(0, 0);
    co_cutset_made(&g_set, H(1), 100.0f, 0.0f, 1);
    Tick(&c, 1, 10000, 16);
    CHECK(g_out.adds == 1);
    /* between `in` and `out`: it stays, however long */
    c = Player(-100.0f, 0.0f);
    for (uint32_t t = 10250; t <= 40000; t += 250) { Tick(&c, 1, t, 16); CHECK(g_out.removes == 0 && g_out.adds == 0); }
    /* out of reach from 40250 on (last in reach at 40000) */
    c = Player(-400.0f, 0.0f);
    for (uint32_t t = 40250; t < 45000; t += 250) { Tick(&c, 1, t, 16); CHECK(g_out.removes == 0); }
    /* back in reach at 43000 on the dot would reset it; here it stays away: gone at 45000, not before */
    Tick(&c, 1, 44999, 16);
    CHECK(g_out.removes == 0 && g_set.on == 1);
    Tick(&c, 1, 45000, 16);
    CHECK(g_out.removes == 1 && g_out.remove[0] == H(1) && g_set.on == 0 && g_set.removed == 1 && g_out.shed == 0);
    Sound();
    /* it is wanted again only inside `in` */
    c = Player(-100.0f, 0.0f);
    Tick(&c, 1, 45250, 16);
    CHECK(g_out.adds == 0);
    c = Player(-59.0f, 0.0f);
    Tick(&c, 1, 45500, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(1));
    /* a return inside the 5 s starts them again */
    c = Player(-400.0f, 0.0f);
    Tick(&c, 1, 45750, 16);
    Tick(&c, 1, 48000, 16);
    c = Player(-100.0f, 0.0f);
    Tick(&c, 1, 48500, 16); /* in reach again */
    c = Player(-400.0f, 0.0f);
    Tick(&c, 1, 50500, 16); /* 5 s after it first left */
    CHECK(g_out.removes == 0);
    Tick(&c, 1, 53499, 16);
    CHECK(g_out.removes == 0);
    Tick(&c, 1, 53500, 16);
    CHECK(g_out.removes == 1);
    Sound();
    /* a shorter delay asked for */
    c = Player(100.0f, 0.0f);
    Tick(&c, 1, 54000, 16);
    CHECK(g_out.adds == 1);
    g_set.leaveMs = 1000;
    c = Player(-400.0f, 0.0f);
    Tick(&c, 1, 54999, 16);
    CHECK(g_out.removes == 0);
    Tick(&c, 1, 55000, 16);
    CHECK(g_out.removes == 1);

    Fresh("removes: capped a tick, the longest out first");
    CoCutCentre two[2] = {Player(100.0f, 0.0f), Player(-1000.0f, 0.0f)};
    for (uint32_t i = 0; i < 100; i++) co_cutset_made(&g_set, H(100 + i), 100.0f + 0.2f * (float)i, 5.0f, 1);   /* one cell */
    for (uint32_t i = 0; i < 3; i++) co_cutset_made(&g_set, H(300 + i), -1000.0f + (float)i, 5.0f, 1);
    Tick(two, 2, 1000, 16);
    CHECK(g_out.adds == 103 && g_set.on == 103);
    two[0] = Player(5000.0f, 5000.0f);  /* the first group is out from 1250 on, the second from 2250 on */
    Tick(two, 2, 1250, 16);
    Tick(two, 2, 2000, 16);
    two[1] = Player(5000.0f, 5000.0f);
    Tick(two, 2, 2250, 16);
    Tick(two, 2, 5999, 64);
    CHECK(g_out.removes == 0);
    Tick(two, 2, 6000, 0); /* no removes asked for (the engine's queue is long) */
    CHECK(g_out.removes == 0 && g_set.on == 103);
    Tick(two, 2, 6000, 16);
    CHECK(g_out.removes == 16 && g_set.on == 87);
    int first = 1;
    for (uint32_t i = 0; i < g_out.removes; i++) first &= g_out.remove[i] >= H(100) && g_out.remove[i] < H(200);
    CHECK(first);
    Tick(two, 2, 7500, 1000); /* never more than one batch; both groups are due, the first is the longer out */
    CHECK(g_out.removes == CO_CUTSET_BATCH && g_set.on == 23);
    first = 1;
    for (uint32_t i = 0; i < g_out.removes; i++) first &= g_out.remove[i] >= H(100) && g_out.remove[i] < H(200);
    CHECK(first);
    Sound();
    Tick(two, 2, 7750, 22);
    CHECK(g_out.removes == 22 && g_set.on == 1 && Removes(H(300)) + Removes(H(301)) + Removes(H(302)) == 2);
    Tick(two, 2, 8000, 22);
    CHECK(g_out.removes == 1 && g_set.on == 0 && g_set.removed == 103);
    Tick(two, 2, 8250, 22);
    CHECK(g_out.removes == 0);
    Sound();

    Fresh("removes: a destroyed or replaced cutter that was on");
    CoCutCentre c2 = Player(0, 0);
    co_cutset_made(&g_set, H(1), 10.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(2), 12.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(3), 300.0f, 0.0f, 1);
    Tick(&c2, 1, 1000, 16);
    CHECK(g_set.on == 2);
    const uint32_t one[1] = {H(1)};
    CHECK(co_cutset_gone(&g_set, one, 1) == 1 && g_set.on == 1 && g_set.wanted == 2);
    co_cutset_taken(&g_set, G(2));
    CHECK(g_set.on == 0 && g_set.wanted == 1 && g_set.records == 1);
    Sound();
    c2 = Player(5000.0f, 0.0f);
    Tick(&c2, 1, 9000, 16); /* the emptied cell is out of reach and has nothing to remove */
    CHECK(g_out.removes == 0 && g_set.onCells == 0);
    co_cutset_made(&g_set, H(1), 10.0f, 0.0f, 1);
    c2 = Player(0, 0);
    Tick(&c2, 1, 9250, 16);
    CHECK(g_out.adds == 1 && g_set.on == 1 && g_set.onCells == 1);
    Sound();
}

static void Cap(void)
{
    Fresh("cap: the nearest are on, the rest wait");
    CoCutCentre c = Player(0, 0);
    g_set.cap = 3;
    for (uint32_t i = 1; i <= 12; i++) co_cutset_made(&g_set, H(i), 10.0f * (float)i, 0.0f, 1); /* 10 .. 120 m */
    Tick(&c, 1, 1000, 16);
    CHECK(g_out.adds == 3 && g_out.add[0] == H(1) && g_out.add[1] == H(2) && g_out.add[2] == H(3));
    CHECK(g_out.capped == 1 && g_out.waiting == 9 && g_out.removes == 0 && g_set.on == 3);
    /* no cutter that is on is farther than one that waits: nothing moves, tick after tick */
    for (uint32_t t = 1250; t < 9000; t += 250) { Tick(&c, 1, t, 16); CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1); }
    Sound();
    /* the player walks 100 m: the cell behind (its middle 85 m away) gives way to the trees now beside the player */
    c = Player(100.0f, 0.0f);
    Tick(&c, 1, 9000, 1);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(10) && g_out.removes == 1 && g_out.shed == 1 && g_out.capped == 1 && g_set.on == 3);
    CHECK(Removes(H(1)) + Removes(H(2)) + Removes(H(3)) == 1);
    Tick(&c, 1, 9250, 0); /* no removes allowed: no room is made */
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1);
    Tick(&c, 1, 9500, 16);
    CHECK(g_out.adds == 2 && Adds(H(9)) && Adds(H(11)) && g_out.removes == 2 && g_out.shed == 2 && g_set.on == 3 && g_set.shed == 3);
    Tick(&c, 1, 9750, 16); /* the three on are the three nearest (within one cell's size): the rest wait */
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1 && g_out.waiting == 9);
    Sound();
    g_set.cap = 0; /* no cutter may be on: none is added, and none is taken off without a nearer one to add */
    Tick(&c, 1, 10000, 16);
    CHECK(g_out.adds == 0 && g_set.on == 3);

    Fresh("cap: the farthest cell first");
    c = Player(0, 0);
    g_set.cap = 2;
    co_cutset_made(&g_set, H(1), 60.0f, 0.0f, 1);   /* cell middle 51 m away */
    co_cutset_made(&g_set, H(2), 120.0f, 0.0f, 1);  /* cell middle 113 m away */
    Tick(&c, 1, 1000, 16);
    CHECK(g_set.on == 2);
    co_cutset_made(&g_set, H(3), 5.0f, 0.0f, 1);
    Tick(&c, 1, 1250, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(3) && g_out.removes == 1 && g_out.remove[0] == H(2));
    co_cutset_made(&g_set, H(4), 6.0f, 0.0f, 1);
    Tick(&c, 1, 1500, 16);
    CHECK(g_out.adds == 1 && g_out.add[0] == H(4) && g_out.removes == 1 && g_out.remove[0] == H(1));
    /* a cell is given up only when every tree in it is farther: its middle over 23 m farther than the one to add */
    co_cutset_made(&g_set, H(5), 0.0f, 0.5f, 1); /* 0.5 m away; the cell of H(3) and H(4) has its middle 22.6 m away */
    Tick(&c, 1, 1750, 16);
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1);
    Sound();

    Fresh("cap: a cell no centre reaches goes before any other, without the 5 s");
    c = Player(0, 0);
    g_set.cap = 3;
    co_cutset_made(&g_set, H(1), 10.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(2), 20.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(3), 1150.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(4), 1005.0f, 0.0f, 1);
    co_cutset_made(&g_set, H(5), 1010.0f, 0.0f, 1);
    CoCutCentre two[2] = {Player(0, 0), Player(1150.0f, 0.0f)};
    Tick(two, 2, 1000, 16);
    CHECK(g_set.on == 3 && Adds(H(1)) && Adds(H(2)) && Adds(H(3)));
    two[0] = Player(1000.0f, 0.0f); /* H(3) is in reach, 150 m off; H(1), H(2) are 1 km behind */
    Tick(two, 2, 1250, 16);
    CHECK(g_out.adds == 2 && g_out.add[0] == H(4) && g_out.add[1] == H(5) && g_out.removes == 2 && Removes(H(1)) && Removes(H(2)) && g_out.shed == 2);
    /* H(3)'s cell is 137 m from the first centre and 21 m from the second: the nearer counts, and it stays */
    co_cutset_made(&g_set, H(6), 1003.0f, 0.0f, 1);
    Tick(two, 2, 1500, 16);
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1 && g_set.on == 3);
    Sound();

    Fresh("cap: a cell out of reach for a moment goes before a far one in reach");
    g_set.cap = 2;
    CoCutCentre wide[2] = {{0.0f, 0.0f, 400.0f, 400.0f}, Player(2000.0f, 0.0f)};
    co_cutset_made(&g_set, H(1), 300.0f, 0.0f, 1);   /* its cell's middle is 304 m from the wide centre */
    co_cutset_made(&g_set, H(2), 2000.0f, 0.0f, 1);
    Tick(wide, 2, 1000, 16);
    CHECK(g_set.on == 2);
    co_cutset_made(&g_set, H(3), 1.0f, 0.0f, 1);
    Tick(wide, 1, 1250, 1); /* the second centre is gone: its cell is 250 ms out of reach */
    CHECK(g_out.adds == 1 && g_out.removes == 1 && g_out.remove[0] == H(2));
    Sound();

    Fresh("cap: a cell exactly 23 m farther stays");
    g_set.cap = 1;
    c = Player(-7.5f, 16.0f); /* 23.5 m from the middle (16, 16) of the first cell: 23 in whole metres */
    co_cutset_made(&g_set, H(1), 16.0f, 20.0f, 1);
    Tick(&c, 1, 1000, 16);
    CHECK(g_set.on == 1);
    co_cutset_made(&g_set, H(2), -7.5f, 16.0f, 1); /* at the centre itself */
    Tick(&c, 1, 1250, 16);
    CHECK(g_out.adds == 0 && g_out.removes == 0 && g_out.capped == 1);
    Sound();

    Fresh("cap: lowered under what is on");
    c = Player(0, 0);
    for (uint32_t i = 0; i < 5; i++) co_cutset_made(&g_set, H(1 + i), 100.0f + (float)i, 0.0f, 1);
    Tick(&c, 1, 1000, 16);
    CHECK(g_set.on == 5);
    g_set.cap = 2;
    Tick(&c, 1, 1250, 16); /* nothing to add: nothing is taken off */
    CHECK(g_out.removes == 0 && g_set.on == 5);
    co_cutset_made(&g_set, H(9), 3.0f, 0.0f, 1);
    Tick(&c, 1, 1500, 3); /* three removes do not make room yet */
    CHECK(g_out.removes == 3 && g_out.adds == 0 && g_out.capped == 1 && g_out.waiting == 1 && g_set.on == 2);
    Tick(&c, 1, 1750, 3);
    CHECK(g_out.removes == 1 && g_out.adds == 1 && g_out.add[0] == H(9) && g_set.on == 2);
    Sound();
}

static void Clock(void)
{
    Fresh("clock: a time that stands or runs back, and the 49-day wrap");
    CoCutCentre c = Player(0, 0);
    co_cutset_made(&g_set, H(1), 10.0f, 0.0f, 1);
    Tick(&c, 1, 0, 16);
    CHECK(g_out.adds == 1 && g_set.now == 1);
    c = Player(5000.0f, 0.0f);
    Tick(&c, 1, 0, 16); /* stands: one more ms, so "in reach this tick" is never read from an older tick */
    CHECK(g_set.now == 2 && g_out.removes == 0);
    Tick(&c, 1, 5000, 16);
    CHECK(g_out.removes == 0); /* in reach last at 1: 4999 ms */
    Tick(&c, 1, 4000, 16); /* runs back: 5001 */
    CHECK(g_set.now == 5001 && g_out.removes == 1);
    Tick(&c, 1, 5001, 16);
    CHECK(g_set.now == 5002);

    Fresh("clock: wrap");
    c = Player(0, 0);
    co_cutset_made(&g_set, H(1), 10.0f, 0.0f, 1);
    Tick(&c, 1, 0xFFFFFF00u, 16);
    CHECK(g_out.adds == 1 && g_set.now == 0xFFFFFF00u);
    c = Player(5000.0f, 0.0f);
    Tick(&c, 1, 0xFFFFFFFFu, 16);
    Tick(&c, 1, 0, 16);
    CHECK(g_set.now == 1); /* 0 is "never": the tick after it */
    Tick(&c, 1, 4743, 16); /* 0xFFFFFF00 + 4999 */
    CHECK(g_out.removes == 0);
    Tick(&c, 1, 4744, 16);
    CHECK(g_out.removes == 1);
    Sound();
}

static uint32_t g_seed = 12345;
static float Rand(float lo, float hi) { g_seed = g_seed * 1664525u + 1013904223u; return lo + (hi - lo) * (float)(g_seed >> 8) / 16777216.0f; }

static int Cx(float v) { const float c = floorf(v / CO_CUTSET_CELL) + (float)(GRID / 2); return c < 0 ? 0 : c > (float)(GRID - 1) ? GRID - 1 : (int)c; }
/* Brute force over every record: `missing` wanted ones inside a centre's `in` that are off, `wrong` on ones that
 * are not wanted or whose cell no centre's `out` square touches. */
static void Brute(const CoCutCentre* c, uint32_t n, uint32_t* missing, uint32_t* wrong, uint32_t* inReach)
{
    CoCutSet* s = &g_set;
    *missing = *wrong = *inReach = 0;
    for (uint32_t i = 0; i <= s->mask; i++)
    {
        const uint32_t r = s->slot[i];
        if (r == 0) continue;
        const CoCutRec* rec = &s->rec[r - 1];
        int in = 0, out = 0;
        for (uint32_t k = 0; k < n; k++)
        {
            const float dx = rec->x - c[k].x, dy = rec->y - c[k].y;
            in |= dx * dx + dy * dy <= c[k].in * c[k].in;
            out |= Cx(rec->x) >= Cx(c[k].x - c[k].out) && Cx(rec->x) <= Cx(c[k].x + c[k].out) && Cx(rec->y) >= Cx(c[k].y - c[k].out) && Cx(rec->y) <= Cx(c[k].y + c[k].out);
        }
        const int wanted = s->flag[r - 1] & 1, on = (s->flag[r - 1] & 2) != 0;
        if (wanted && in) (*inReach)++;
        if (wanted && in && !on) (*missing)++;
        if (on && (!wanted || !out)) (*wrong)++;
    }
}

static double Us(LARGE_INTEGER a, LARGE_INTEGER b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart; }

/* 180 000 trees over 9 km2 (0.02 a m2), a tenth of them never added by the game; 8 players and 56 squads. */
static void Forest(void)
{
    enum { TREES = 180000, PLAYERS = 8, SQUADS = 56, N = PLAYERS + SQUADS };
    Fresh("forest: 180 000 trees, 8 players, 56 squads");
    g_set.cap = 1000000;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    for (uint32_t i = 0; i < TREES; i++) co_cutset_made(&g_set, H(i * 5 % 0x100000u), Rand(-1500.0f, 1500.0f), Rand(-1500.0f, 1500.0f), i % 10 != 0);
    QueryPerformanceCounter(&b);
    printf("  cutset: %u records made in %.0f us (%.2f us each); the set's memory is %.1f MB\n", g_set.records, Us(a, b), Us(a, b) / TREES, (double)co_cutset_bytes() / 1048576.0);
    CHECK(g_set.records == TREES && g_set.wanted == TREES - TREES / 10);
    CoCutCentre c[N];
    for (int k = 0; k < N; k++)
        c[k] = k < PLAYERS ? Player(Rand(-1300.0f, 1300.0f), Rand(-1300.0f, 1300.0f))
                           : (CoCutCentre){Rand(-1300.0f, 1300.0f), Rand(-1300.0f, 1300.0f), CO_CUTSET_SQUAD_IN, CO_CUTSET_SQUAD_OUT};
    uint32_t now = 1000, ticks = 0, missing, wrong, inReach;
    double sum = 0, worst = 0;
    do
    {
        QueryPerformanceCounter(&a);
        Tick(c, N, now += 250, CO_CUTSET_REMOVES);
        QueryPerformanceCounter(&b);
        sum += Us(a, b); if (Us(a, b) > worst) worst = Us(a, b);
        CHECK(g_out.removes == 0 && g_out.capped == 0);
    } while (g_out.adds != 0 && ++ticks < 1000);
    Brute(c, N, &missing, &wrong, &inReach);
    CHECK(missing == 0 && wrong == 0 && g_set.on == inReach && inReach > 5000);
    printf("  cutset: first fill: %u cutters on after %u ticks of at most %u adds, %.0f us a tick on average, %.0f us at worst\n", g_set.on, ticks, CO_CUTSET_ADDS, sum / (ticks + 1), worst);
    Sound();
    /* standing still: a tick finds nothing to do */
    sum = worst = 0;
    for (int t = 0; t < 200; t++)
    {
        QueryPerformanceCounter(&a);
        Tick(c, N, now += 250, CO_CUTSET_REMOVES);
        QueryPerformanceCounter(&b);
        sum += Us(a, b); if (Us(a, b) > worst) worst = Us(a, b);
        CHECK(g_out.adds == 0 && g_out.removes == 0);
    }
    printf("  cutset: standing still: %.0f us a tick on average, %.0f us at worst\n", sum / 200, worst);
    /* 2 minutes of everyone moving: players 7 m/s, squads 4 m/s, a tenth of a stamp of trees leaving and coming */
    sum = worst = 0;
    float dir[N][2];
    for (int k = 0; k < N; k++) { const float t = Rand(0.0f, 6.2831853f); dir[k][0] = cosf(t); dir[k][1] = sinf(t); }
    uint32_t adds = 0, removes = 0, next = TREES;
    for (int t = 0; t < 480; t++)
    {
        for (int k = 0; k < N; k++)
        {
            const float v = (k < PLAYERS ? 7.0f : 4.0f) * 0.25f;
            c[k].x += dir[k][0] * v; c[k].y += dir[k][1] * v;
            if (fabsf(c[k].x) > 1400.0f) dir[k][0] = -dir[k][0];
            if (fabsf(c[k].y) > 1400.0f) dir[k][1] = -dir[k][1];
        }
        uint32_t batch[CO_CUTSET_BATCH];
        for (uint32_t i = 0; i < CO_CUTSET_BATCH; i++) batch[i] = H((uint32_t)(t * 64 + (int)i) * 5 % 0x100000u);
        co_cutset_gone(&g_set, batch, CO_CUTSET_BATCH);
        for (uint32_t i = 0; i < CO_CUTSET_BATCH; i++, next++) co_cutset_made(&g_set, G(next * 5 % 0x100000u), Rand(-1500.0f, 1500.0f), Rand(-1500.0f, 1500.0f), 1);
        QueryPerformanceCounter(&a);
        Tick(c, N, now += 250, CO_CUTSET_REMOVES);
        QueryPerformanceCounter(&b);
        sum += Us(a, b); if (Us(a, b) > worst) worst = Us(a, b);
        adds += g_out.adds; removes += g_out.removes;
        if (t % 40 == 0) { Brute(c, N, &missing, &wrong, &inReach); CHECK(missing == 0); } /* some still on behind them */
    }
    printf("  cutset: moving for 120 s: %.0f us a tick on average, %.0f us at worst; %u adds and %u removes (%.0f and %.0f a second)\n", sum / 480, worst, adds, removes, adds / 120.0, removes / 120.0);
    CHECK(adds > 1000 && removes > 1000);
    Sound();
    /* everyone stops: after the backlog of removes, exactly the trees in reach are on */
    for (int t = 0; t < 4000 && (t < 40 || g_out.removes != 0); t++) Tick(c, N, now += 250, CO_CUTSET_BATCH);
    Brute(c, N, &missing, &wrong, &inReach);
    CHECK(missing == 0 && wrong == 0);
    Sound();
    /* the cap with everyone 300 m on: the cutters left behind make room, and no more than the cap are on */
    g_set.cap = 4000;
    for (int k = 0; k < N; k++) { c[k].x += 300.0f * dir[k][0]; c[k].y += 300.0f * dir[k][1]; }
    for (int t = 0; t < 2000; t++) Tick(c, N, now += 250, CO_CUTSET_BATCH);
    Brute(c, N, &missing, &wrong, &inReach);
    printf("  cutset: cap 4000 with %u in reach: %u on, %u in reach not on\n", inReach, g_set.on, missing);
    CHECK(wrong == 0 && inReach > 4000 && g_set.on == 4000 && g_out.capped == 1);
    Sound();
}

static void Centres(void)
{
    g_case = "centres: a squad inside a player's reach";
    const CoCutCentre p[2] = {Player(1000.0f, 0.0f), Player(0.0f, 0.0f)};
    CoCutCentre q = {96.0f, 0.0f, CO_CUTSET_SQUAD_IN, CO_CUTSET_SQUAD_OUT};
    CHECK(co_cutset_covered(p, 2, &q) == 1);      /* 96 + 64 = 160 and 96 + 96 <= 224: at the edge, inside */
    CHECK(co_cutset_covered(p, 1, &q) == 0);      /* the other player is 904 m away */
    CHECK(co_cutset_covered(p, 0, &q) == 0);
    q.x = 96.5f;
    CHECK(co_cutset_covered(p, 2, &q) == 0);      /* its `in` passes the player's by half a metre */
    q = (CoCutCentre){0.0f, -60.0f, 64.0f, 170.0f};
    CHECK(co_cutset_covered(p, 2, &q) == 0);      /* its `in` is inside, its `out` is not (60 + 170 > 224) */
    q.out = 164.0f;
    CHECK(co_cutset_covered(p, 2, &q) == 1);
    q = (CoCutCentre){0.0f, 100.0f, 30.0f, 10.0f}; /* an `out` under its `in` counts as the `in` */
    CHECK(co_cutset_covered(p, 2, &q) == 1);
    q = (CoCutCentre){0.0f, 0.0f, 64.0f, nanf("")}; /* an `out` that is no number: the `in`, as in the tick */
    CHECK(co_cutset_covered(p, 2, &q) == 1);
    q = (CoCutCentre){nanf(""), 0.0f, 64.0f, 96.0f};
    CHECK(co_cutset_covered(p, 2, &q) == 0);
    const CoCutCentre bad[1] = {{0.0f, 0.0f, 0.0f, 500.0f}}; /* a centre the tick does not use covers nothing */
    q = (CoCutCentre){0.0f, 0.0f, 0.0f, 96.0f};
    CHECK(co_cutset_covered(bad, 1, &q) == 0);

    g_case = "centres: a player's last place";
    CoCutHeld held[2];
    float at[2] = {-1.0f, -1.0f};
    memset(held, 0, sizeof held);
    CHECK(co_cutset_player(held, 2, 7, 0, 0, 0, 1000, at) == 0 && at[0] == -1.0f); /* never read: no place */
    CHECK(co_cutset_player(held, 2, 0, 0, 0, 0, 1000, at) == 0 && at[0] == -1.0f); /* nor for a peer of 0 in empty slots */
    CHECK(co_cutset_player(held, 2, 7, 1, 10.0f, 20.0f, 1000, at) == 1 && at[0] == 10.0f && at[1] == 20.0f);
    CHECK(co_cutset_player(held, 2, 0, 1, 30.0f, 40.0f, 1000, at) == 1 && at[0] == 30.0f); /* a peer of 0 is a peer */
    CHECK(co_cutset_player(held, 2, 7, 1, 11.0f, 21.0f, 2000, at) == 1 && at[0] == 11.0f && at[1] == 21.0f);
    CHECK(co_cutset_player(held, 2, 7, 0, 99.0f, 99.0f, 2000 + CO_CUTSET_HOLD_MS, at) == 2 && at[0] == 11.0f && at[1] == 21.0f);
    CHECK(co_cutset_player(held, 2, 0, 0, 0, 0, 5000, at) == 2 && at[0] == 30.0f && at[1] == 40.0f);
    at[0] = at[1] = -1.0f;
    CHECK(co_cutset_player(held, 2, 7, 0, 0, 0, 2001 + CO_CUTSET_HOLD_MS, at) == 0 && at[0] == -1.0f); /* 30 s and 1 ms */
    CHECK(co_cutset_player(held, 2, 7, 1, nanf(""), 5.0f, 2002 + CO_CUTSET_HOLD_MS, at) == 0);        /* no number: not read */
    CHECK(co_cutset_player(held, 2, 7, 1, 5.0f, nanf(""), 2002 + CO_CUTSET_HOLD_MS, at) == 0);
    /* both slots held: a third peer still has its place while it reads, and none once it does not */
    CHECK(co_cutset_player(held, 2, 7, 1, 12.0f, 22.0f, 40000, at) == 1);
    CHECK(co_cutset_player(held, 2, 0, 1, 31.0f, 41.0f, 40000, at) == 1);
    CHECK(co_cutset_player(held, 2, 9, 1, 50.0f, 60.0f, 41000, at) == 1 && at[0] == 50.0f && at[1] == 60.0f);
    CHECK(co_cutset_player(held, 2, 9, 0, 0, 0, 41250, at) == 0);
    CHECK(co_cutset_player(held, 2, 7, 0, 0, 0, 41250, at) == 2 && at[0] == 12.0f);
    CHECK(co_cutset_player(held, 2, 0, 0, 0, 0, 41250, at) == 2 && at[0] == 31.0f);
    /* peer 7 gone for over 30 s: its slot is the third peer's, and peer 0's stays */
    CHECK(co_cutset_player(held, 2, 0, 1, 32.0f, 42.0f, 70500, at) == 1);
    CHECK(co_cutset_player(held, 2, 9, 1, 51.0f, 61.0f, 70500, at) == 1);
    CHECK(co_cutset_player(held, 2, 9, 0, 0, 0, 70750, at) == 2 && at[0] == 51.0f && at[1] == 61.0f);
    CHECK(co_cutset_player(held, 2, 0, 0, 0, 0, 70750, at) == 2 && at[0] == 32.0f);
    CHECK(co_cutset_player(held, 2, 7, 0, 0, 0, 70750, at) == 0);
    /* a peer in the second slot is found past a first slot whose hold ran out */
    memset(held, 0, sizeof held);
    CHECK(co_cutset_player(held, 2, 7, 1, 1.0f, 1.0f, 1000, at) == 1);
    CHECK(co_cutset_player(held, 2, 9, 1, 3.0f, 4.0f, 20000, at) == 1);
    CHECK(co_cutset_player(held, 2, 9, 0, 0, 0, 41000, at) == 2 && at[0] == 3.0f && at[1] == 4.0f);
    /* the clock wraps */
    memset(held, 0, sizeof held);
    CHECK(co_cutset_player(held, 2, 3, 1, 1.0f, 2.0f, 0xFFFFFF00u, at) == 1);
    CHECK(co_cutset_player(held, 2, 3, 0, 0, 0, 0x100u, at) == 2 && at[0] == 1.0f);
    CHECK(co_cutset_player(held, 0, 3, 1, 8.0f, 9.0f, 0x200u, at) == 1 && at[0] == 8.0f); /* no slots: no hold */
    CHECK(co_cutset_player(held, 0, 3, 0, 0, 0, 0x300u, at) == 0);
}

int main(void)
{
    /* unbuffered, and no group after a failed one: a set gone wrong can loop, and its failed checks must show */
    void (*const groups[])(void) = {Binding, Records, AddsInReach, Leaving, Cap, Clock, Centres, Forest};
    setvbuf(stdout, NULL, _IONBF, 0);
    for (size_t i = 0; i < sizeof groups / sizeof groups[0] && g_failed == 0; i++) groups[i]();
    free(g_memory);
    if (g_failed) { printf("cutset: %d check(s) failed\n", g_failed); return 1; }
    printf("cutset: all checks passed\n");
    return 0;
}
