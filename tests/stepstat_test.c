/* The AI step's 30 s lines (xl/co_stepstat.h): a step's parts and the window's worst step, the game thread's steps and
 * another world's apart, the drive's time past its budget, calls outside a step apart, a path request's own work
 * without its yields, the queue by kind, and the window's clock with its cap a mission. */
#include <stdio.h>
#include <string.h>

#include "xl/co_stepstat.h"

static int g_failed;
#define CHECK(what) do { if (!(what)) { printf("FAILED: %s (line %d)\n", #what, __LINE__); g_failed++; } } while (0)

#define FREQ 1000000 /* one tick a microsecond */

static CoStepOne One(int64_t start, int64_t drive, int64_t commit, int64_t nav)
{
    CoStepOne one = {0};
    if (start) co_stepstat_part(&one, CO_STEP_START, start);
    if (drive) co_stepstat_drive(&one, drive, 0.0005f, FREQ);
    if (commit) co_stepstat_part(&one, CO_STEP_COMMIT, commit);
    if (nav) co_stepstat_part(&one, CO_STEP_NAV, nav);
    return one;
}

/* Two steps of the world and one of another: sums, means and maxima, and the worst step's own split. */
static void Steps(void)
{
    CoStepWin w = {0};
    CoStepOne a = One(1000, 600, 0, 300), b = One(0, 2500, 400, 200), c = One(9000, 0, 0, 0);
    co_stepstat_step(&w, &a, 2000, CO_STEP_ON_GAME, 0);
    co_stepstat_step(&w, &b, 4000, CO_STEP_ON_OTHER, 3);
    co_stepstat_step(&w, &c, 9500, CO_STEP_ON_FOREIGN, 1);
    CHECK(w.whole.n == 2 && w.whole.sum == 6000 && w.whole.most == 4000);
    CHECK(w.on[CO_STEP_ON_GAME] == 1 && w.on[CO_STEP_ON_OTHER] == 1 && w.on[CO_STEP_ON_FOREIGN] == 1);
    CHECK(w.atOnce == 1); /* the foreign step's flag is not summed; 3 counts once */
    CHECK(w.part[CO_STEP_START].n == 1 && w.part[CO_STEP_START].sum == 1000 && w.part[CO_STEP_START].most == 1000);
    CHECK(w.part[CO_STEP_DRIVE].n == 2 && w.part[CO_STEP_DRIVE].sum == 3100 && w.part[CO_STEP_DRIVE].most == 2500);
    CHECK(w.part[CO_STEP_COMMIT].n == 1 && w.part[CO_STEP_COMMIT].sum == 400);
    CHECK(w.part[CO_STEP_NAV].n == 2 && w.part[CO_STEP_NAV].sum == 500 && w.part[CO_STEP_NAV].most == 300);
    CHECK(w.worst.ticks[CO_STEP_DRIVE] == 2500 && w.worst.ticks[CO_STEP_COMMIT] == 400 && w.worst.ticks[CO_STEP_START] == 0);
    /* drive budget 500 us: 600 is 100 past, 2500 is 2000 past */
    CHECK(w.over == 2000);
    /* an equal step later keeps the first worst's split; a slower one takes it */
    CoStepOne d = One(4000, 0, 0, 0), e = One(0, 0, 0, 4100);
    co_stepstat_step(&w, &d, 4000, CO_STEP_ON_OTHER, 0);
    CHECK(w.worst.ticks[CO_STEP_DRIVE] == 2500 && w.worst.ticks[CO_STEP_START] == 0);
    CHECK(w.over == 2000); /* a later step with no drive keeps the window's longest */
    co_stepstat_step(&w, &e, 4100, CO_STEP_ON_OTHER, 0);
    CHECK(w.worst.ticks[CO_STEP_NAV] == 4100 && w.worst.ticks[CO_STEP_DRIVE] == 0 && w.whole.most == 4100);
    /* the first step is the worst so far even when it took no time */
    CoStepWin z = {0}; CoStepOne f = One(0, 0, 0, 0);
    f.calls[CO_STEP_NAV] = 1;
    co_stepstat_step(&z, &f, 0, CO_STEP_ON_GAME, 0);
    CHECK(z.worst.calls[CO_STEP_NAV] == 1 && z.whole.n == 1 && z.part[CO_STEP_NAV].n == 1);
    /* a place outside the kinds is not counted */
    co_stepstat_step(&z, &f, 10, CO_STEP_ONS, 0);
    co_stepstat_step(&z, &f, 10, -1, 0);
    CHECK(z.whole.n == 1 && z.on[CO_STEP_ON_GAME] == 1);
}

/* A drive within its budget is not past it; a step's longest drive past it counts, and two drives in one step add. */
static void Drive(void)
{
    CoStepOne one = {0};
    co_stepstat_drive(&one, 1500, 0.002f, FREQ);
    CHECK(one.over == 0 && one.ticks[CO_STEP_DRIVE] == 1500 && one.calls[CO_STEP_DRIVE] == 1);
    co_stepstat_drive(&one, 2600, 0.002f, FREQ);
    co_stepstat_drive(&one, 2100, 0.002f, FREQ);
    CHECK(one.over == 600 && one.ticks[CO_STEP_DRIVE] == 6200 && one.calls[CO_STEP_DRIVE] == 3);
    co_stepstat_part(&one, CO_STEP_PARTS, 99);
    co_stepstat_part(&one, -1, 99);
    CHECK(one.ticks[CO_STEP_DRIVE] == 6200 && one.ticks[CO_STEP_START] == 0 && one.ticks[CO_STEP_NAV] == 0);
    CHECK(one.calls[CO_STEP_START] == 0 && one.calls[CO_STEP_DRIVE] == 3 && one.calls[CO_STEP_NAV] == 0 && one.over == 600);
}

/* The mod's nav swaps call the commit and the navigators outside a step: apart, never in the step's parts. */
static void Outside(void)
{
    CoStepWin w = {0};
    co_stepstat_outside(&w, CO_STEP_COMMIT, 700);
    co_stepstat_outside(&w, CO_STEP_COMMIT, 300);
    co_stepstat_outside(&w, CO_STEP_NAV, 50);
    co_stepstat_outside(&w, CO_STEP_PARTS, 50);
    co_stepstat_outside(&w, -1, 50);
    CHECK(w.outside[CO_STEP_COMMIT].n == 2 && w.outside[CO_STEP_COMMIT].sum == 1000 && w.outside[CO_STEP_COMMIT].most == 700);
    CHECK(w.outside[CO_STEP_NAV].n == 1 && w.outside[CO_STEP_START].n == 0 && w.outside[CO_STEP_DRIVE].n == 0);
    CHECK(w.part[CO_STEP_COMMIT].n == 0 && w.part[CO_STEP_NAV].n == 0 && w.whole.n == 0);
    CHECK(w.over == 0 && w.worst.ticks[CO_STEP_START] == 0 && w.worst.calls[CO_STEP_START] == 0);
}

/* A request's yields are not its work; the longest stretch between two yields counts its start and its end. */
static void Requests(void)
{
    CoStepReq r;
    co_stepstat_req_begin(&r, 100);
    co_stepstat_req_yield(&r, 110, 5110);  /* 10 before the first yield, then blocked 5000 */
    co_stepstat_req_yield(&r, 5410, 5420); /* 300 of search */
    int64_t own = co_stepstat_req_end(&r, 100, 5620); /* 200 after the last yield */
    CHECK(own == 5520 - 5010);
    CHECK(r.stretch == 300);
    co_stepstat_req_begin(&r, 0);
    CHECK(co_stepstat_req_end(&r, 0, 900) == 900 && r.stretch == 900); /* no yield: all of it */
    co_stepstat_req_begin(&r, 0);
    co_stepstat_req_yield(&r, 0, 50);
    CHECK(co_stepstat_req_end(&r, 0, 1050) == 1000 && r.stretch == 1000); /* the stretch after the last yield */
    co_stepstat_req_begin(&r, 1000);
    co_stepstat_req_yield(&r, 1700, 1800);
    CHECK(co_stepstat_req_end(&r, 1000, 1900) == 800 && r.stretch == 700); /* the stretch before the first yield */
    co_stepstat_req_begin(&r, 0);
    r.yielded = 500; /* a clock that went back: no negative work */
    CHECK(co_stepstat_req_end(&r, 0, 100) == 0);
    CoStepPaths p = {0};
    co_stepstat_paths(&p, 400, 300);
    co_stepstat_paths(&p, 900, 350);
    co_stepstat_paths(&p, 50, 100);
    CHECK(p.n == 3 && p.own == 900 && p.stretch == 350);
}

/* Cutters by thread, the run's changes by type (a count that went down adds nothing), the generators' most. */
static void Queue(void)
{
    CoStepQueue q = {0};
    q.gens = q.gensMost = -1;
    co_stepstat_cutter(&q, 0, 1);
    co_stepstat_cutter(&q, 0, 0);
    co_stepstat_cutter(&q, 0, 7);
    co_stepstat_cutter(&q, 1, 0);
    CHECK(q.added[0] == 3 && q.added[1] == 2 && q.removed[0] == 1 && q.removed[1] == 0);
    const int32_t before[CO_STEP_TYPES] = {0, 2, 5, 10, 3}, after[CO_STEP_TYPES] = {4, 2, 1, 15, 4};
    co_stepstat_applied(&q, before, after);
    CHECK(q.runs == 1 && q.types[0] == 4 && q.types[1] == 0 && q.types[2] == 0 && q.types[3] == 5 && q.types[4] == 1);
    co_stepstat_applied(&q, before, after);
    CHECK(q.runs == 2 && q.types[0] == 8 && q.types[3] == 10 && q.types[4] == 2);
    co_stepstat_gens(&q, 1200);
    co_stepstat_gens(&q, 3000);
    co_stepstat_gens(&q, 2500);
    CHECK(q.gens == 2500 && q.gensMost == 3000);
    co_stepstat_gens(&q, -1);
    CHECK(q.gens == -1 && q.gensMost == 3000);
}

/* The window's clock: a first frame and a new mission start fresh, a gap restarts the window and keeps the count,
 * a window is due after 30 s, and a mission writes CO_STEP_LINES pairs, the last one told. */
static void Clock(void)
{
    CoStepClock c = {0};
    CHECK(co_stepstat_tick(&c, 1, 0) == CO_STEP_FRESH);
    CHECK(co_stepstat_tick(&c, 1, CO_STEP_PERIOD_MS - 1) == CO_STEP_IDLE);
    CHECK(co_stepstat_tick(&c, 1, CO_STEP_PERIOD_MS) == CO_STEP_DUE && c.lines == 1);
    CHECK(co_stepstat_tick(&c, 1, CO_STEP_PERIOD_MS + 1) == CO_STEP_IDLE);
    /* a gap of exactly the limit is still play; one past it restarts the window but not the count */
    uint64_t t = CO_STEP_PERIOD_MS + 1 + CO_STEP_GAP_MS;
    CHECK(co_stepstat_tick(&c, 1, t) == CO_STEP_DUE && c.lines == 2);
    t += CO_STEP_GAP_MS + 1;
    CHECK(co_stepstat_tick(&c, 1, t) == CO_STEP_FRESH && c.lines == 2 && c.at == t);
    CHECK(co_stepstat_tick(&c, 1, t + CO_STEP_PERIOD_MS - 1) == CO_STEP_IDLE);
    /* a new mission: fresh, its count from 0 */
    CHECK(co_stepstat_tick(&c, 2, t + CO_STEP_PERIOD_MS) == CO_STEP_FRESH && c.lines == 0 && c.mission == 2);
    t += CO_STEP_PERIOD_MS;
    int due = 0, last = 0, quiet = 0;
    for (uint32_t i = 0; i < CO_STEP_LINES + 3; i++)
    {
        t += CO_STEP_PERIOD_MS;
        int r = co_stepstat_tick(&c, 2, t);
        due += r == CO_STEP_DUE; last += r == CO_STEP_LAST; quiet += r == CO_STEP_QUIET;
        if (i == CO_STEP_LINES - 1) CHECK(r == CO_STEP_LAST);
    }
    CHECK(due == (int)CO_STEP_LINES - 1 && last == 1 && quiet == 3 && c.lines == CO_STEP_LINES);
    /* the cap holds through a gap, and the next mission writes again */
    CHECK(co_stepstat_tick(&c, 2, t + CO_STEP_GAP_MS + 1) == CO_STEP_FRESH);
    CHECK(co_stepstat_tick(&c, 2, t + CO_STEP_GAP_MS + 1 + CO_STEP_PERIOD_MS) == CO_STEP_QUIET);
    CHECK(co_stepstat_tick(&c, 3, t + CO_STEP_GAP_MS + 2 + CO_STEP_PERIOD_MS) == CO_STEP_FRESH);
    CHECK(co_stepstat_tick(&c, 3, t + CO_STEP_GAP_MS + 2 + 2 * CO_STEP_PERIOD_MS) == CO_STEP_DUE);
    /* mission 0 on the very first frame is still a first frame */
    CoStepClock d = {0};
    CHECK(co_stepstat_tick(&d, 0, 5) == CO_STEP_FRESH && d.seen);
    CHECK(co_stepstat_tick(&d, 0, 6) == CO_STEP_IDLE);
}

/* The lines' text from a known window, and their length with the largest numbers under the log's 1024. */
static void Lines(void)
{
    char out[1024];
    CHECK(co_stepstat_ms(2500, FREQ) == 2.5 && co_stepstat_ms(5, 0) == 0.0);
    CoStepWin w = {0}; CoStepPaths p = {0};
    CoStepOne a = One(1000, 600, 0, 300), b = One(0, 2500, 400, 200);
    co_stepstat_step(&w, &a, 2000, CO_STEP_ON_GAME, 0);
    co_stepstat_step(&w, &b, 4000, CO_STEP_ON_OTHER, 1);
    co_stepstat_outside(&w, CO_STEP_COMMIT, 1250);
    co_stepstat_outside(&w, CO_STEP_COMMIT, 250);
    co_stepstat_paths(&p, 7000, 3000);
    co_stepstat_line1(out, sizeof out, &w, &p, 1, FREQ);
    CHECK(strstr(out, "2 steps (1 on the game thread, 1 on other threads, 1 overlapping, 0 of other AI worlds), mean 3.00 ms, worst 4.00 ms; ") == out);
    CHECK(strstr(out, "; run start 1, mean 1.00, worst 1.00; drive 2, mean 1.55, worst 2.50, worst past its budget 2.00; commit 1, mean 0.40, worst 0.40; ") != NULL);
    CHECK(strstr(out, "; navigators 2, mean 0.25, worst 0.30; the worst step: start 0.00, drive 2.50, commit 0.40, navigators 0.20, rest 0.90 ms; ") != NULL);
    CHECK(strstr(out, "; path requests 1, the longest 7.00 ms of own work, 3.00 ms at most between two yields; ") != NULL);
    CHECK(strstr(out, "; outside a step: start 0 (worst 0.00 ms), drive 0 (0.00), commit 2 (1.25), navigators 0 (0.00)") != NULL);
    co_stepstat_line1(out, sizeof out, &w, &p, 0, FREQ);
    CHECK(strstr(out, "rest 0.90 ms; path requests not measured; outside a step:") != NULL);
    /* each place a step ran is its own number */
    CoStepWin m = {0}; CoStepOne none = {0};
    co_stepstat_step(&m, &none, 10, CO_STEP_ON_GAME, 0); co_stepstat_step(&m, &none, 10, CO_STEP_ON_GAME, 0);
    co_stepstat_step(&m, &none, 10, CO_STEP_ON_OTHER, 0);
    for (int i = 0; i < 4; i++) co_stepstat_step(&m, &none, 10, CO_STEP_ON_FOREIGN, 0);
    co_stepstat_line1(out, sizeof out, &m, &p, 0, FREQ);
    CHECK(strstr(out, "3 steps (2 on the game thread, 1 on other threads, 0 overlapping, 4 of other AI worlds), mean 0.01 ms, worst 0.01 ms; ") == out);
    /* parts over the step's time (two clocks) give no negative rest; an empty window has no means */
    CoStepWin e = {0};
    e.whole.most = 10; e.worst.ticks[CO_STEP_NAV] = 50;
    co_stepstat_line1(out, sizeof out, &e, &p, 0, FREQ);
    CHECK(strstr(out, "0 steps (0 on the game thread, 0 on other threads, 0 overlapping, 0 of other AI worlds), mean 0.00 ms, worst 0.01 ms; ") == out);
    CHECK(strstr(out, "rest 0.00 ms") != NULL && strstr(out, "run start 0, mean 0.00, worst 0.00") != NULL);
    CoStepQueue q = {0};
    q.added[0] = 12; q.added[1] = 10; q.removed[0] = 30; q.removed[1] = 29; q.gens = 4100; q.gensMost = 5200; q.runs = 3;
    for (int k = 0; k < CO_STEP_TYPES; k++) q.types[k] = (uint32_t)(k + 1) * 11u;
    co_stepstat_line2(out, sizeof out, &q, 777);
    CHECK(strcmp(out, "cutters added 12 (10 on the game thread), removed 30 (29 on the game thread); generators 4100 now, most 5200 (the remove's scan); "
        "3 runs took user-edge pairs (7) 11, user-edge setups (8) 22, type 9 33, cutter adds (10) 44, cutter removes (11) 55; 777 changes queued now") == 0);
    /* every count at its largest and every time 10^10 ms (a step of four months): both lines fit the log's line */
    CoStepWin big; CoStepPaths bp; CoStepQueue bq;
    memset(&big, 0xFF, sizeof big); memset(&bp, 0xFF, sizeof bp); memset(&bq, 0xFF, sizeof bq);
    const int64_t huge = 10000000000000LL;
    for (int k = 0; k < CO_STEP_PARTS; k++) { big.part[k].sum = big.part[k].most = big.outside[k].most = huge; big.worst.ticks[k] = 0; }
    big.whole.sum = big.whole.most = big.over = huge; bp.own = bp.stretch = huge;
    int n1 = co_stepstat_line1(out, sizeof out, &big, &bp, 1, FREQ);
    int n2 = co_stepstat_line2(out, sizeof out, &bq, -2147483647 - 1);
    CHECK(n1 > 0 && n1 + (int)strlen("living world: AI step (30 s): ") < 1024);
    CHECK(n2 > 0 && n2 + (int)strlen("living world: AI queue (30 s): ") < 1024);
    /* a typical window stays well inside the caller's 900 */
    CHECK(co_stepstat_line1(out, sizeof out, &w, &p, 1, FREQ) < 800);
}

int main(void)
{
    Steps(); Drive(); Outside(); Requests(); Queue(); Clock(); Lines();
    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("stepstat: all checks passed\n");
    return 0;
}
