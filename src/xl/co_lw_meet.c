/* A player on or near a road meets the road patrols (co_lw_meet.h). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "co_lw_meet.h"

static float Dist(const float* a, const float* b) { return hypotf(a[0] - b[0], a[1] - b[1]); }

int co_lw_meet_enters(const float* at, const float* to, const float (*players)[3], int n, float edge)
{
    for (int i = 0; i < n; i++)
    {
        const float d1 = Dist(to, players[i]);
        if (d1 < edge && d1 < Dist(at, players[i])) return 1;
    }
    return 0;
}

int co_lw_meet_back(const float* at, const float* back, const float (*players)[3], int n, float edge, float* out)
{
    const float len = Dist(at, back);
    if (!(len > 0.0f)) return 0;
    const int steps = (int)ceilf(len / CO_LW_MEET_STEP);
    for (int k = 1; k <= steps; k++)
    {
        const float t = k == steps ? 1.0f : (float)k * CO_LW_MEET_STEP / len, p[2] = {at[0] + (back[0] - at[0]) * t, at[1] + (back[1] - at[1]) * t};
        int clear = 1;
        for (int i = 0; i < n && clear; i++) if (Dist(p, players[i]) < edge) clear = 0;
        if (clear) { out[0] = p[0]; out[1] = p[1]; return 1; }
    }
    return 0;
}

int co_lw_meet_next(uint64_t sinceMs, uint64_t nowMs, float nearest2, float ring)
{
    if (nowMs >= sinceMs + CO_LW_MEET_WAIT_MS) return CO_LW_MEET_TURNS;
    return nearest2 < ring * ring ? CO_LW_MEET_BACK : CO_LW_MEET_WAITS;
}

int co_lw_meet_keeps(int road, float near2, float rOn, int roadUnits, int budget)
{
    return road && near2 < rOn * rOn && roadUnits <= budget / CO_LW_MEET_ROAD_SHARE;
}

int co_lw_meet_on_way(const float (*way)[2], int n, const float* player, float width)
{
    for (int k = 0; k < n; k++)
    {
        const float* a = way[k]; const float* b = way[k + 1 < n ? k + 1 : k];
        const float ex = b[0] - a[0], ey = b[1] - a[1], len2 = ex * ex + ey * ey;
        float t = len2 > 0.0f ? ((player[0] - a[0]) * ex + (player[1] - a[1]) * ey) / len2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float p[2] = {a[0] + ex * t, a[1] + ey * t};
        if (Dist(p, player) <= width) return 1;
    }
    return 0;
}

int co_lw_meet_act(int onRoad, float d, float rMin, float rOn, float over, int hidden, int aheadSeen, int cover, int measured)
{
    if (d < rMin || over >= CO_LW_MEET_OUTER) return CO_LW_MEET_WALK;
    if (over >= 0.0f) return onRoad && !hidden && measured ? CO_LW_MEET_ASK_BEYOND : CO_LW_MEET_WALK; /* hidden: on to where its cover ends */
    if (!hidden) return cover ? CO_LW_MEET_ASK_COVER : CO_LW_MEET_WALK;
    return d >= rOn && onRoad && aheadSeen ? CO_LW_MEET_ASK : CO_LW_MEET_WALK; /* nearer, the near band's own request asks */
}

void co_lw_meet_cover_set(CoLwMeetCover* c, const float* at, uint16_t node, uint16_t next, uint8_t pathAt, uint64_t nowMs)
{
    c->at[0] = at[0]; c->at[1] = at[1]; c->node = node; c->next = next; c->pathAt = pathAt; c->ms = nowMs; c->known = 1;
}

int co_lw_meet_cover_ok(const CoLwMeetCover* c, const float* at, const uint16_t* path, uint8_t pathAt, uint8_t pathLen, const float (*players)[3], int n, float edge, uint64_t nowMs)
{
    if (!c->known || c->steps >= CO_LW_MEET_COVER_STEPS || nowMs >= c->ms + CO_LW_MEET_COVER_MS) return 0;
    if (c->pathAt > pathAt || c->pathAt >= pathLen || path[c->pathAt] != c->next) return 0; /* another plan since */
    if (Dist(c->at, at) < 1.0f) return 0;
    for (int i = 0; i < n; i++) if (Dist(c->at, players[i]) < edge) return 0;
    return 1;
}

int co_lw_meet_road_room(int roadUnits, int members, int budget) { return roadUnits + members <= budget / CO_LW_MEET_ROAD_SHARE; }

int co_lw_meet_far_room(int farUnits, int roadFar, int members, int limit, int onRoad, int reserve, int roadLimit)
{
    if (onRoad && roadLimit > 0) return roadFar + members <= roadLimit;
    if (onRoad || !reserve) return farUnits + members <= limit;
    return farUnits - roadFar + members <= limit - limit / CO_LW_MEET_FAR_RESERVE;
}

int co_lw_meet_road_far(int budget) { return budget * CO_LW_MEET_ROAD_FAR_8THS / 8; }

uint64_t co_lw_meet_stay_ms(float keep, float rOff) { return keep > rOff ? (uint64_t)((keep - rOff) / CO_LW_MEET_STAY_PACE * 1000.0f) : 0; }

int co_lw_meet_watched(uint8_t* run, int seen)
{
    if (*run == 255) return 0;
    if (!seen) { *run = 0; return 0; }
    if (++*run < CO_LW_MEET_WATCH_S) return 0;
    *run = 255;
    return 1;
}

uint32_t co_lw_meet_call_ms(float roadFar, int members, int roadPlayers, float walk)
{
    float kept = roadFar;
    if (kept < (float)members) kept = (float)members; /* one squad walks in on any share */
    const float ms = walk / CO_LW_MEET_PACE * 1000.0f * (float)members * (float)(roadPlayers > 0 ? roadPlayers : 1) / kept;
    return ms < (float)CO_LW_MEET_CALL_MIN_MS ? CO_LW_MEET_CALL_MIN_MS : ms > (float)CO_LW_MEET_CALL_MAX_MS ? CO_LW_MEET_CALL_MAX_MS : (uint32_t)ms;
}

int co_lw_meet_call_ahead(const float* v, const float (*dirs)[2], int n, int* only)
{
    const float speed = sqrtf(v[0] * v[0] + v[1] * v[1]);
    int best = -1; float most = 0.0f;
    *only = 0;
    if (!(speed >= CO_LW_MEET_CALL_MOVING)) return -1;
    for (int k = 0; k < n; k++)
    {
        const float len = sqrtf(dirs[k][0] * dirs[k][0] + dirs[k][1] * dirs[k][1]), along = len > 0.0f ? (v[0] * dirs[k][0] + v[1] * dirs[k][1]) / len : 0.0f;
        if (along > most) { most = along; best = k; }
    }
    if (best >= 0) *only = speed >= CO_LW_MEET_PACE;
    return best;
}

int co_lw_meet_call_lost(float d, float* least)
{
    if (!(*least > 0.0f) || d < *least) *least = d;
    return d > *least + CO_LW_MEET_CALL_LOST;
}

uint32_t co_lw_meet_call_first(uint32_t periodMs, int slot, int players)
{
    return players > 1 && slot > 0 ? (uint32_t)((uint64_t)periodMs * (uint64_t)(slot % players) / (uint64_t)players) : 0;
}

int co_lw_meet_call_better(int hops, int onSide, int bestHops, int bestOnSide)
{
    if (bestHops < 0) return 1;
    if (onSide != bestOnSide) return onSide;
    return hops < bestHops;
}

int co_lw_meet_zone_waits(uint64_t sinceMs, uint64_t refusedMs, uint64_t nowMs)
{
    return refusedMs != 0 && nowMs < refusedMs + CO_LW_MEET_ZONE_ASK_MS && nowMs < sinceMs + CO_LW_MEET_ZONE_WAIT_MS;
}

int co_lw_meet_road_members(int size) { return size < CO_LW_MEET_ROAD_MEMBERS ? size : CO_LW_MEET_ROAD_MEMBERS; }

int co_lw_meet_beside(const uint8_t* clear, const uint16_t* foes, int n, int* least)
{
    *least = -1;
    for (int k = 0; k < n; k++)
    {
        if (!clear[k]) continue;
        if (foes[k] == 0) return k;
        if (*least < 0 || foes[k] < foes[*least]) *least = k;
    }
    return -1;
}

float co_lw_meet_travel_step(float v, float dt)
{
    const float step = v * (dt > 0.1f ? 0.1f : dt);
    return step > 2.0f ? 2.0f : step;
}

uint32_t co_lw_meet_held(const CoLwMeetHolds* h, const int* bubble, int requester, int inBubble, uint64_t nowMs)
{
    uint32_t units = 0;
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++)
        if (nowMs < h->until[k] && h->idx[k] != requester && bubble[k] == inBubble) units += h->units[k];
    return units;
}

int co_lw_meet_hold(CoLwMeetHolds* h, const int* bubble, int idx, uint32_t units, int inBubble, uint64_t nowMs, uint64_t forMs)
{
    int at = -1;
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++)
    {
        if (nowMs >= h->until[k]) { if (at < 0) at = k; continue; }
        if (bubble[k] == inBubble) return 0;
    }
    if (at < 0) return 0;
    h->idx[at] = idx; h->units[at] = units; h->until[at] = nowMs + forMs;
    return 1;
}

void co_lw_meet_hold_end(CoLwMeetHolds* h, int idx)
{
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++) if (h->idx[k] == idx) h->until[k] = 0;
}

void co_lw_meet_seen(CoLwMeet* m, uint32_t slot, uint32_t road, uint32_t roadNew, uint32_t artNew, uint32_t other, uint32_t otherNew, float angle)
{
    if (slot >= CO_LW_MEET_PLAYERS) return;
    CoLwMeetPlayer* p = &m->player[slot];
    p->seenN++; p->roadSeen += road; p->roadAny += road > 0; p->roadNew += roadNew; p->artNew += artNew; p->otherSeen += other; p->otherNew += otherNew;
    if (angle >= 0.0f) { p->angleN++; p->angle += (double)angle; }
}

int co_lw_meet_sighting(uint8_t* gap, int seen)
{
    if (!seen) { if (*gap < 255) (*gap)++; return 0; }
    const int fresh = *gap >= CO_LW_MEET_SEEN_GAP;
    *gap = 0;
    return fresh;
}

int co_lw_meet_seen_note(const CoLwMeet* m, uint32_t slot, char* out, size_t size)
{
    if (size == 0 || slot >= CO_LW_MEET_PLAYERS || m->player[slot].seenN == 0) return 0;
    const CoLwMeetPlayer* p = &m->player[slot];
    char angle[64] = "not read";
    if (p->angleN) snprintf(angle, sizeof angle, "%.0f deg on average (%u samples)", p->angle / p->angleN, p->angleN);
    snprintf(out, size, "seen by player %u (%u samples): road patrols in view and walking %.2f on average (any %.0f%% of the time), %u new sightings "
        "(%u of the arterials'); other patrols in view and walking %.2f on average, %u new sightings; the view off the nearest road's direction: %s",
        slot + 1, p->seenN, (double)p->roadSeen / p->seenN, 100.0 * p->roadAny / p->seenN, p->roadNew, p->artNew, (double)p->otherSeen / p->seenN, p->otherNew, angle);
    return 1;
}

void co_lw_meet_sample(CoLwMeet* m, uint32_t slot, uint64_t nowMs, int byRoad, const float* dist, const uint8_t* units, const uint8_t* road, uint32_t n)
{
    static const float kRing[CO_LW_MEET_RINGS] = {50.0f, 100.0f, 150.0f};
    if (slot >= CO_LW_MEET_PLAYERS) return;
    CoLwMeetPlayer* p = &m->player[slot];
    double dt = p->known && nowMs > p->lastMs ? (double)(nowMs - p->lastMs) : 0.0;
    if (dt > 3000.0) dt = 3000.0; /* a longer gap is a player who was away, not time with nothing near */
    p->known = 1; p->lastMs = nowMs;
    if (dt <= 0.0) return;
    uint32_t squads = 0, ring[CO_LW_MEET_RINGS] = {0, 0, 0}; float nearest = 0;
    for (uint32_t k = 0; k < n; k++)
    {
        for (int r = 0; r < CO_LW_MEET_RINGS; r++) if (dist[k] <= kRing[r]) ring[r] += units[k];
        if (!road[k] || dist[k] > CO_LW_MEET_ROAD) continue;
        if (squads++ == 0 || dist[k] < nearest) nearest = dist[k];
    }
    p->ms += dt; if (byRoad) p->roadMs += dt;
    p->squadMs += dt * squads;
    for (int r = 0; r < CO_LW_MEET_RINGS; r++) p->unitMs[r] += dt * ring[r];
    if (squads == 0) return;
    if (p->anyMs <= 0.0 || nearest < p->least) p->least = nearest;
    p->anyMs += dt; p->nearestMs += dt * nearest;
}

int co_lw_meet_note(const CoLwMeet* m, uint32_t slot, char* out, size_t size)
{
    if (size == 0 || slot >= CO_LW_MEET_PLAYERS || !(m->player[slot].ms > 0.0)) return 0;
    const CoLwMeetPlayer* p = &m->player[slot];
    char nearest[64] = "no such squad";
    if (p->anyMs > 0.0) snprintf(nearest, sizeof nearest, "nearest %.0f m on average, least %.0f m", p->nearestMs / p->anyMs, (double)p->least);
    snprintf(out, size, "near player %u (%.0f s, by a road %.0f%%): road squads online within %.0f m %.2f on average (%s; none %.0f%% of the time); "
        "living-world units within 50 / 100 / 150 m %.1f / %.1f / %.1f on average (a unit counts at its squad's leader)", slot + 1, p->ms / 1000.0,
        100.0 * p->roadMs / p->ms, (double)CO_LW_MEET_ROAD, p->squadMs / p->ms, nearest, 100.0 * (p->ms - p->anyMs) / p->ms, p->unitMs[0] / p->ms,
        p->unitMs[1] / p->ms, p->unitMs[2] / p->ms);
    return 1;
}

void co_lw_meet_restart(CoLwMeet* m)
{
    for (int i = 0; i < CO_LW_MEET_PLAYERS; i++)
    {
        CoLwMeetPlayer* p = &m->player[i];
        const uint64_t last = p->lastMs; const uint8_t known = p->known;
        memset(p, 0, sizeof *p);
        p->lastMs = last; p->known = known;
    }
}
