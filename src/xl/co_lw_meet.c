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

int co_lw_meet_keeps(int road, float near2, float rOn) { return road && near2 < rOn * rOn; }

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
