/* The living world as each player sees it (co_lw_view.h). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "co_lw_view.h"

void co_lw_view_level(CoLwView* v, long level, uint64_t nowMs)
{
    if (v->level == level) return;
    memset(v, 0, sizeof *v);
    v->level = level; v->sinceMs = nowMs;
}

void co_lw_view_player(CoLwView* v, uint32_t slot, const float* at, uint64_t nowMs)
{
    if (slot >= CO_LW_VIEW_PLAYERS || !isfinite(at[0]) || !isfinite(at[1])) return;
    CoLwViewPlayer* p = &v->player[slot];
    if (!p->known) { p->known = 1; p->last[0] = at[0]; p->last[1] = at[1]; p->lastMs = nowMs; p->v[0] = p->v[1] = 0; return; }
    if (nowMs < p->lastMs + 500) return; /* the tick count steps by about 16 ms: a shorter time gives a speed it never had */
    const float s = 1000.0f / (float)(nowMs - p->lastMs), vx = (at[0] - p->last[0]) * s, vy = (at[1] - p->last[1]) * s;
    const int jump = !(hypotf(vx, vy) <= CO_LW_VIEW_FAST) || nowMs - p->lastMs > CO_LW_VIEW_STEP_MS;
    p->v[0] = jump ? 0 : vx; p->v[1] = jump ? 0 : vy;
    if (!jump) p->pathM += hypotf(at[0] - p->last[0], at[1] - p->last[1]);
    p->last[0] = at[0]; p->last[1] = at[1]; p->lastMs = nowMs;
}

int co_lw_view_lead(const CoLwView* v, uint32_t slot, const float* at, float* lead)
{
    lead[0] = at[0]; lead[1] = at[1];
    if (slot >= CO_LW_VIEW_PLAYERS || !v->player[slot].known) return 0;
    const float vx = v->player[slot].v[0], vy = v->player[slot].v[1], speed = hypotf(vx, vy);
    if (!(speed > CO_LW_VIEW_LEAD_FROM)) return 0;
    float ahead = speed * CO_LW_VIEW_LEAD_S;
    if (ahead > CO_LW_VIEW_LEAD_MAX) ahead = CO_LW_VIEW_LEAD_MAX;
    lead[0] = at[0] + vx / speed * ahead; lead[1] = at[1] + vy / speed * ahead;
    return 1;
}

float co_lw_view_band2(float near2, float lead2, float rMin, float rOn)
{
    if (near2 < rOn * rOn || !(lead2 < rOn * rOn)) return near2; /* in a player's own band, or nearer: as it is */
    return lead2 > rMin * rMin ? lead2 : rMin * rMin; /* in the lead point's band: any distance the band holds */
}

void co_lw_view_sample(CoLwView* v, const CoLwViewSample* s)
{
    if (s->index >= CO_LW_VIEW_SQUADS || !isfinite(s->pos[0]) || !isfinite(s->pos[1])) return;
    CoLwViewSquad* q = &v->squad[s->index];
    CoLwViewSums* a = &v->sums;
    uint64_t dt = 0;
    if (!q->known)
    {
        memset(q, 0, sizeof *q);
        q->known = 1; q->lastMs = q->refMs = q->walkedMs = q->calmMs = s->nowMs; q->ref[0] = s->pos[0]; q->ref[1] = s->pos[1];
    }
    else if (s->nowMs > q->lastMs)
    {
        dt = s->nowMs - q->lastMs; q->lastMs = s->nowMs;
        if (dt > CO_LW_VIEW_STEP_MS) dt = CO_LW_VIEW_STEP_MS;
    }
    if (s->nowMs >= q->refMs + CO_LW_VIEW_WALK_MS)
    {
        const float way = hypotf(s->pos[0] - q->ref[0], s->pos[1] - q->ref[1]);
        q->walking = (uint8_t)(way * 1000.0f > CO_LW_VIEW_WALK * (float)(s->nowMs - q->refMs));
        q->ref[0] = s->pos[0]; q->ref[1] = s->pos[1]; q->refMs = s->nowMs;
    }
    if (q->walking) q->walkedMs = s->nowMs;
    a->onlineMs += (double)dt; a->unitMs += (double)dt * s->units;
    if (q->walking) a->movingMs += (double)dt;
    if (s->road) a->roadMs += (double)dt;
    if (s->sightline)
    {
        a->farUnitMs += (double)dt * s->units;
        if (!q->walking) a->farStandMs += (double)dt * s->units;
    }
    if (s->fighting && !q->fighting && s->patrol && s->nowMs - q->calmMs >= CO_LW_VIEW_CALM_MS)
    {
        a->fights++;
        if (s->harm > q->harm) a->fightsHit++; /* hurt since its last calm sample: it was shot at first */
    }
    q->harm = s->harm;
    if (!s->fighting) { if (q->fighting) q->calmMs = s->nowMs; }
    q->fighting = (uint8_t)(s->fighting != 0);
    for (uint32_t i = 0; i < s->players && i < CO_LW_VIEW_PLAYERS; i++)
    {
        CoLwViewPlayer* p = &v->player[i];
        const uint8_t bit = (uint8_t)(1u << i);
        if (s->seen[i] == CO_LW_VIEW_UNSEEN || !s->patrol)
        {
            if (q->seen & bit) { q->seen &= (uint8_t)~bit; q->unseenMs[i] = s->nowMs; }
            continue;
        }
        if (s->seen[i] == CO_LW_VIEW_SEEN_NEAR) { p->nearMs += (double)dt; p->nearUnitMs += (double)dt * s->units; }
        else { p->farMs += (double)dt; p->farUnitMs += (double)dt * s->units; }
        if (q->walking) p->movingMs += (double)dt;
        if (s->road) p->roadMs += (double)dt;
        /* behind a rock for a moment is the same sighting; out of sight for longer, the squad is met again */
        if (!(q->seen & bit) && (!(q->ever & bit) || s->nowMs - q->unseenMs[i] >= CO_LW_VIEW_GAP_MS))
        {
            p->sightings++;
            if (q->walking) p->movingSightings++;
        }
        q->seen |= bit; q->ever |= bit;
    }
}

void co_lw_view_gone(CoLwView* v, uint32_t index)
{
    if (index < CO_LW_VIEW_SQUADS) memset(&v->squad[index], 0, sizeof v->squad[index]);
}

uint64_t co_lw_view_stood(const CoLwView* v, uint32_t index, uint64_t nowMs)
{
    if (index >= CO_LW_VIEW_SQUADS || !v->squad[index].known || nowMs <= v->squad[index].walkedMs) return 0;
    return nowMs - v->squad[index].walkedMs;
}

int co_lw_view_far(int farOn, float dist2, float rOn, uint64_t stoodMs)
{
    if (!farOn || !(dist2 >= rOn * rOn)) return CO_LW_VIEW_FAR_NONE;
    return stoodMs < CO_LW_VIEW_STAND_MS ? CO_LW_VIEW_FAR_WALKS : CO_LW_VIEW_FAR_STANDS;
}

int co_lw_view_far_sum(int walking, int standing, int limit)
{
    return walking + (standing > limit ? standing - limit : 0);
}

int co_lw_view_gave_up(uint64_t stoodMs, uint32_t stallTries, int hasGoal)
{
    if (stoodMs < 2 * CO_LW_VIEW_STAND_MS) return 0;
    return hasGoal ? stallTries >= 3 : stoodMs >= 4 * CO_LW_VIEW_STAND_MS;
}

uint32_t co_lw_view_scan_at(uint32_t start, uint32_t k, uint32_t count)
{
    return count ? (uint32_t)(((uint64_t)start + k) % count) : 0;
}


int co_lw_view_note(const CoLwView* v, uint64_t nowMs, char* out, size_t size)
{
    if (nowMs <= v->sinceMs || size == 0) return 0;
    const double ms = (double)(nowMs - v->sinceMs);
    const CoLwViewSums* a = &v->sums;
    snprintf(out, size, "patrols (%.0f s): %.1f squads online on average (%.1f units), %.1f of them moving, %.1f by a road; "
        "far units %.1f, %.0f%% of them standing; %u fights began for patrol squads (%u after the squad was hurt first)",
        ms / 1000.0, a->onlineMs / ms, a->unitMs / ms, a->movingMs / ms, a->roadMs / ms, a->farUnitMs / ms,
        a->farUnitMs > 0 ? 100.0 * a->farStandMs / a->farUnitMs : 0.0, a->fights, a->fightsHit);
    return 1;
}

int co_lw_view_player_note(const CoLwView* v, uint32_t slot, uint64_t nowMs, char* out, size_t size)
{
    if (nowMs <= v->sinceMs || size == 0 || slot >= CO_LW_VIEW_PLAYERS || !v->player[slot].known) return 0;
    const double ms = (double)(nowMs - v->sinceMs);
    const CoLwViewPlayer* p = &v->player[slot];
    snprintf(out, size, "view of player %u (%.0f s, %.1f m/s): patrol squads in a clear line from the eye %.1f within %.0f m "
        "(%.1f units), %.1f from there to the draw limit (%.1f units); %.1f of them moving, %.1f by a road; %u sightings began "
        "(%u of a moving squad)",
        slot + 1, ms / 1000.0, p->pathM * 1000.0 / ms, p->nearMs / ms, (double)CO_LW_VIEW_NEAR, p->nearUnitMs / ms, p->farMs / ms,
        p->farUnitMs / ms, p->movingMs / ms, p->roadMs / ms, p->sightings, p->movingSightings);
    return 1;
}

void co_lw_view_restart(CoLwView* v, uint64_t nowMs)
{
    memset(&v->sums, 0, sizeof v->sums);
    for (int i = 0; i < CO_LW_VIEW_PLAYERS; i++)
    {
        CoLwViewPlayer* p = &v->player[i];
        p->nearMs = p->farMs = p->nearUnitMs = p->farUnitMs = p->movingMs = p->roadMs = p->pathM = 0;
        p->sightings = p->movingSightings = 0;
    }
    v->sinceMs = nowMs;
}

int co_lw_move_state(int patrol, int calm, int hasGoal, float left, float arrive)
{
    if (!patrol || !calm) return CO_LW_MOVE_OTHER;
    return !hasGoal ? CO_LW_MOVE_NO_GOAL : left > arrive ? CO_LW_MOVE_GOAL : CO_LW_MOVE_THERE;
}

int co_lw_move_sample(CoLwMove* m, const float* at, int state, float* way5)
{
    if (!m->on) { m->on = 1; m->whole = CO_LW_MOVE_OTHER; m->way = m->way5 = 0; m->n5 = 0; }
    else
    {
        const float dx = at[0] - m->last[0], dy = at[1] - m->last[1], d = sqrtf(dx * dx + dy * dy);
        m->way += d; m->way5 += d;
        if (m->whole != state) m->whole = CO_LW_MOVE_OTHER;
    }
    m->state = (uint8_t)state; m->last[0] = at[0]; m->last[1] = at[1];
    if (++m->n5 < CO_LW_MOVE_EVERY) return 0;
    *way5 = m->way5; m->n5 = 0; m->way5 = 0;
    return 1;
}

void co_lw_move_gone(CoLwMove* m) { m->on = 0; m->whole = m->state = CO_LW_MOVE_OTHER; }

static void MoveName(char* who, size_t size, int* at, int listed, uint32_t index)
{
    if (listed <= CO_LW_MOVE_NAMED && (size_t)*at < size) *at += snprintf(who + *at, size - (size_t)*at, " %u", index);
}
void co_lw_move_note(CoLwMove* m, uint32_t count, char* out, size_t size)
{
    int n = 0, low = 0, still = 0, noGoal = 0, there = 0, at = 0, at2 = 0, at3 = 0;
    float least = 0; char who[80] = "", who2[80] = "", who3[80] = "";
    for (uint32_t i = 0; i < count; i++)
    {
        if (!m[i].on) continue;
        if (m[i].whole == CO_LW_MOVE_GOAL)
        {
            if (n++ == 0 || m[i].way < least) least = m[i].way;
            if (m[i].way < CO_LW_MOVE_STILL) MoveName(who, sizeof who, &at, ++still, i);
        }
        else if (m[i].whole == CO_LW_MOVE_NO_GOAL) MoveName(who2, sizeof who2, &at2, ++noGoal, i);
        else if (m[i].whole == CO_LW_MOVE_THERE) MoveName(who3, sizeof who3, &at3, ++there, i);
    }
    float median = 0;
    for (uint32_t i = 0; i < count && n; i++) /* the median without a table: the ways under each candidate, counted */
    {
        if (!m[i].on || m[i].whole != CO_LW_MOVE_GOAL) continue;
        low = 0;
        int same = 0;
        for (uint32_t k = 0; k < count; k++)
            if (m[k].on && m[k].whole == CO_LW_MOVE_GOAL) { low += m[k].way < m[i].way; same += m[k].way == m[i].way; }
        if (low <= n / 2 && n / 2 < low + same) { median = m[i].way; break; }
    }
    snprintf(out, size, "patrol goals (30 s): %d squads calm with a goal throughout, their way least %.0f m, median %.0f m; %d under %.0f m%s%s; "
        "%d calm without a goal throughout%s%s; %d calm at their goal throughout%s%s", n, (double)least, (double)median, still, (double)CO_LW_MOVE_STILL,
        still ? ": squads" : "", who, noGoal, noGoal ? ": squads" : "", who2, there, there ? ": squads" : "", who3);
    for (uint32_t i = 0; i < count; i++) if (m[i].on) { m[i].whole = m[i].state; m[i].way = 0; }
}

int co_lw_view_switch(const char* text, unsigned long length, int fallback) { return length > 0 ? text[0] != '0' : fallback; }
