/* The Havok AI step's anatomy and the navmesh queue by kind (co_stepstat.h). */
#include <stdio.h>
#include <string.h>

#include "co_stepstat.h"

static void Sum(CoStepSum* s, int64_t ticks)
{
    s->n++; s->sum += ticks;
    if (ticks > s->most) s->most = ticks;
}

void co_stepstat_part(CoStepOne* one, int part, int64_t ticks)
{
    if (part < 0 || part >= CO_STEP_PARTS) return;
    one->calls[part]++; one->ticks[part] += ticks;
}

void co_stepstat_drive(CoStepOne* one, int64_t ticks, float seconds, int64_t freq)
{
    co_stepstat_part(one, CO_STEP_DRIVE, ticks);
    /* the pool's deadline is the budget from the drive's start (0xA10C50); a run that ends sooner is not past it */
    int64_t past = ticks - (int64_t)((double)seconds * (double)freq);
    if (past > one->over) one->over = past;
}

void co_stepstat_step(CoStepWin* w, const CoStepOne* one, int64_t whole, int on, int atOnce)
{
    if (on < 0 || on >= CO_STEP_ONS) return;
    w->on[on]++;
    if (on == CO_STEP_ON_FOREIGN) return;
    w->atOnce += atOnce != 0;
    if (whole > w->whole.most || w->whole.n == 0) w->worst = *one;
    Sum(&w->whole, whole);
    for (int k = 0; k < CO_STEP_PARTS; k++)
        if (one->calls[k]) Sum(&w->part[k], one->ticks[k]);
    if (one->over > w->over) w->over = one->over;
}

void co_stepstat_outside(CoStepWin* w, int part, int64_t ticks)
{
    if (part >= 0 && part < CO_STEP_PARTS) Sum(&w->outside[part], ticks);
}

void co_stepstat_req_begin(CoStepReq* r, int64_t now) { r->mark = now; r->yielded = 0; r->stretch = 0; }

void co_stepstat_req_yield(CoStepReq* r, int64_t enter, int64_t leave)
{
    if (enter - r->mark > r->stretch) r->stretch = enter - r->mark;
    r->yielded += leave - enter; r->mark = leave;
}

int64_t co_stepstat_req_end(CoStepReq* r, int64_t start, int64_t now)
{
    if (now - r->mark > r->stretch) r->stretch = now - r->mark;
    int64_t own = now - start - r->yielded;
    return own > 0 ? own : 0;
}

void co_stepstat_paths(CoStepPaths* p, int64_t own, int64_t stretch)
{
    p->n++;
    if (own > p->own) p->own = own;
    if (stretch > p->stretch) p->stretch = stretch;
}

void co_stepstat_cutter(CoStepQueue* q, int remove, int onGame)
{
    uint32_t* n = remove ? q->removed : q->added;
    n[0]++; n[1] += onGame != 0;
}

void co_stepstat_applied(CoStepQueue* q, const int32_t before[CO_STEP_TYPES], const int32_t after[CO_STEP_TYPES])
{
    q->runs++;
    for (int k = 0; k < CO_STEP_TYPES; k++)
        if (after[k] > before[k]) q->types[k] += (uint32_t)(after[k] - before[k]);
}

void co_stepstat_gens(CoStepQueue* q, int32_t now)
{
    q->gens = now;
    if (now > q->gensMost) q->gensMost = now;
}

int co_stepstat_tick(CoStepClock* c, long mission, uint64_t nowMs)
{
    int newMission = !c->seen || mission != c->mission;
    int fresh = newMission || nowMs - c->last > CO_STEP_GAP_MS;
    c->seen = 1; c->last = nowMs;
    if (newMission) { c->mission = mission; c->lines = 0; }
    if (fresh) { c->at = nowMs; return CO_STEP_FRESH; }
    if (nowMs - c->at < CO_STEP_PERIOD_MS) return CO_STEP_IDLE;
    c->at = nowMs;
    if (c->lines >= CO_STEP_LINES) return CO_STEP_QUIET;
    return ++c->lines == CO_STEP_LINES ? CO_STEP_LAST : CO_STEP_DUE;
}

double co_stepstat_ms(int64_t ticks, int64_t freq) { return freq > 0 ? (double)ticks * 1000.0 / (double)freq : 0.0; }

static double Mean(const CoStepSum* s, int64_t freq) { return s->n ? co_stepstat_ms(s->sum, freq) / s->n : 0.0; }

int co_stepstat_line1(char* out, size_t size, const CoStepWin* w, const CoStepPaths* p, int paths, int64_t freq)
{
    const CoStepSum* s = w->part;
    int64_t rest = w->whole.most;
    for (int k = 0; k < CO_STEP_PARTS; k++) rest -= w->worst.ticks[k];
    char req[160] = "path requests not measured";
    if (paths)
        snprintf(req, sizeof req, "path requests %u, the longest %.2f ms of own work, %.2f ms at most between two yields", p->n,
            co_stepstat_ms(p->own, freq), co_stepstat_ms(p->stretch, freq));
#define CO_STEP_PART(k) s[k].n, Mean(&s[k], freq), co_stepstat_ms(s[k].most, freq)
#define CO_STEP_OUT(k) w->outside[k].n, co_stepstat_ms(w->outside[k].most, freq)
    return snprintf(out, size,
        "%u steps (%u on the game thread, %u on other threads, %u overlapping, %u of other AI worlds), mean %.2f ms, worst %.2f ms; "
        "run start %u, mean %.2f, worst %.2f; drive %u, mean %.2f, worst %.2f, worst past its budget %.2f; commit %u, mean %.2f, worst %.2f; "
        "navigators %u, mean %.2f, worst %.2f; the worst step: start %.2f, drive %.2f, commit %.2f, navigators %.2f, rest %.2f ms; %s; "
        "outside a step: start %u (worst %.2f ms), drive %u (%.2f), commit %u (%.2f), navigators %u (%.2f)",
        w->whole.n, w->on[CO_STEP_ON_GAME], w->on[CO_STEP_ON_OTHER], w->atOnce, w->on[CO_STEP_ON_FOREIGN], Mean(&w->whole, freq),
        co_stepstat_ms(w->whole.most, freq), CO_STEP_PART(CO_STEP_START), CO_STEP_PART(CO_STEP_DRIVE), co_stepstat_ms(w->over, freq),
        CO_STEP_PART(CO_STEP_COMMIT), CO_STEP_PART(CO_STEP_NAV), co_stepstat_ms(w->worst.ticks[CO_STEP_START], freq),
        co_stepstat_ms(w->worst.ticks[CO_STEP_DRIVE], freq), co_stepstat_ms(w->worst.ticks[CO_STEP_COMMIT], freq),
        co_stepstat_ms(w->worst.ticks[CO_STEP_NAV], freq), co_stepstat_ms(rest > 0 ? rest : 0, freq), req,
        CO_STEP_OUT(CO_STEP_START), CO_STEP_OUT(CO_STEP_DRIVE), CO_STEP_OUT(CO_STEP_COMMIT), CO_STEP_OUT(CO_STEP_NAV));
#undef CO_STEP_PART
#undef CO_STEP_OUT
}

int co_stepstat_line2(char* out, size_t size, const CoStepQueue* q, int queued)
{
    const uint32_t* t = q->types;
    return snprintf(out, size,
        "cutters added %u (%u on the game thread), removed %u (%u on the game thread); generators %d now, most %d (the remove's scan); "
        "%u runs took user-edge pairs (7) %u, user-edge setups (8) %u, type 9 %u, cutter adds (10) %u, cutter removes (11) %u; %d changes queued now",
        q->added[0], q->added[1], q->removed[0], q->removed[1], q->gens, q->gensMost, q->runs, t[0], t[1], t[2], t[3], t[4], queued);
}
