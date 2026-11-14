/* The research build's FRV drive, game-free (co_frv.h). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "co_frv.h"

#define FRV_DEG 57.29578f
#define FRV_WINDOW 12u /* segments searched ahead of the last one: a frame moves the vehicle well under one */

uint32_t co_frv_nearest(const float (*pts)[2], uint32_t first, uint32_t end, float x, float y, float* distance)
{
    uint32_t best = end; float bd = 3.0e38f;
    for (uint32_t i = first; i < end; i++)
    {
        const float dx = pts[i][0] - x, dy = pts[i][1] - y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    if (distance != NULL) *distance = best == end ? 3.0e38f : sqrtf(bd);
    return best;
}

uint32_t co_frv_leg(const float (*pts)[2], uint32_t first, uint32_t end, uint32_t from, int step, float metres, CoFrvPath* out)
{
    out->count = 0;
    if (from < first || from >= end || (step != 1 && step != -1) || !(metres > 0.0f)) return 0;
    float along = 0.0f;
    for (uint32_t i = from;; i = (uint32_t)((int64_t)i + step))
    {
        const float x = pts[i][0], y = pts[i][1];
        if (!isfinite(x) || !isfinite(y)) break;
        if (out->count == 0) { out->xy[0][0] = x; out->xy[0][1] = y; out->at[0] = 0.0f; out->count = 1; }
        else
        {
            const float dx = x - out->xy[out->count - 1][0], dy = y - out->xy[out->count - 1][1], d = sqrtf(dx * dx + dy * dy);
            if (d > 0.01f)
            {
                along += d;
                out->xy[out->count][0] = x; out->xy[out->count][1] = y; out->at[out->count] = along; out->count++;
            }
        }
        if (along >= metres || out->count == CO_FRV_PATH) break;
        if (step > 0 ? i + 1 >= end : i == first) break;
    }
    return out->count;
}

void co_frv_point(const CoFrvPath* p, float along, float* xy)
{
    uint32_t i = 0;
    if (p->count < 2) return;
    while (i + 2 < p->count && p->at[i + 1] < along) i++;
    const float len = p->at[i + 1] - p->at[i];
    float t = len > 0.0f ? (along - p->at[i]) / len : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    xy[0] = p->xy[i][0] + (p->xy[i + 1][0] - p->xy[i][0]) * t;
    xy[1] = p->xy[i][1] + (p->xy[i + 1][1] - p->xy[i][1]) * t;
}

void co_frv_aim(const CoFrvPath* p, uint32_t* segment, const float* pos, const float* heading, float ahead, CoFrvAim* out)
{
    memset(out, 0, sizeof *out);
    if (p->count < 2) return;
    if (*segment > p->count - 2) *segment = p->count - 2;
    uint32_t best = *segment; float bd = 3.0e38f, bt = 0.0f;
    for (uint32_t s = *segment; s + 1 < p->count && s < *segment + FRV_WINDOW; s++)
    {
        const float ax = p->xy[s][0], ay = p->xy[s][1], ux = p->xy[s + 1][0] - ax, uy = p->xy[s + 1][1] - ay, l2 = ux * ux + uy * uy;
        float t = l2 > 0.0f ? ((pos[0] - ax) * ux + (pos[1] - ay) * uy) / l2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float dx = pos[0] - (ax + ux * t), dy = pos[1] - (ay + uy * t), d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = s; bt = t; }
    }
    *segment = best;
    const float ux = p->xy[best + 1][0] - p->xy[best][0], uy = p->xy[best + 1][1] - p->xy[best][1], len = sqrtf(ux * ux + uy * uy);
    out->station = bt >= 1.0f ? best + 1 : best;
    out->along = p->at[best] + len * bt;
    /* left of the line: the line's direction turned a quarter counter-clockwise points at the vehicle */
    out->cross = len > 0.0f ? ((pos[1] - p->xy[best][1]) * ux - (pos[0] - p->xy[best][0]) * uy) / len : 0.0f;
    co_frv_point(p, out->along + ahead, out->target);
    const float tx = out->target[0] - pos[0], ty = out->target[1] - pos[1];
    if (tx * tx + ty * ty > 1.0e-6f && heading[0] * heading[0] + heading[1] * heading[1] > 1.0e-6f)
        out->error = atan2f(heading[0] * ty - heading[1] * tx, heading[0] * tx + heading[1] * ty);
}

float co_frv_steer(float error, float full)
{
    const float s = full > 0.0f ? -error / full : 0.0f;
    return s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s;
}

float co_frv_throttle(float speed, float want, float error)
{
    float slow = 1.0f - fabsf(error) / 1.2f; /* a quarter turn off the aim: 0.4 of the speed */
    if (slow < 0.4f) slow = 0.4f;
    const float t = want * slow + 1.0f - speed;
    return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

int co_frv_shift(int gear, float throttle, float sincePress)
{
    return gear >= 0 && gear < 2 && throttle > 0.5f && sincePress >= CO_FRV_SHIFT_SECONDS;
}

int co_frv_sign(CoFrvSign* v, float steer, float turned, float speed)
{
    if (v->verdict != 0 || speed < 2.0f || fabsf(steer) < 0.2f || fabsf(turned) > 0.5f) return v->verdict;
    v->sum += steer * turned;
    if (v->sum < -0.1f) v->verdict = 1;
    else if (v->sum > 0.1f) v->verdict = -1;
    return v->verdict;
}

void co_frv_axes(const float* q, float* up, float* ahead)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    up[0] = 2.0f * (x * z + w * y); up[1] = 2.0f * (y * z - w * x); up[2] = 1.0f - 2.0f * (x * x + y * y);
    const float fx = 2.0f * (x * y - w * z), fy = 1.0f - 2.0f * (x * x + z * z), l = sqrtf(fx * fx + fy * fy);
    ahead[0] = l > 1.0e-3f ? fx / l : 0.0f; ahead[1] = l > 1.0e-3f ? fy / l : 0.0f;
}

void co_frv_tilt(const float* up, const float* dir, float* pitch, float* roll)
{
    /* nose up tips the roof back along the travel; right side down tips it to the right of the travel */
    *pitch = atan2f(-(up[0] * dir[0] + up[1] * dir[1]), up[2]) * FRV_DEG;
    *roll = atan2f(up[0] * dir[1] - up[1] * dir[0], up[2]) * FRV_DEG;
}

void co_frv_stats_start(CoFrvStats* s)
{
    memset(s, 0, sizeof *s);
    s->fewestWheels = -1; s->slowSince = -1.0f; s->markT = -1.0f;
}

int co_frv_stats_add(CoFrvStats* s, const CoFrvSample* a)
{
    int began = 0;
    if (s->samples == 0)
    {
        s->t0 = a->t; s->pitchLow = s->pitchHigh = a->pitch; s->rollLow = s->rollHigh = a->roll;
        s->markT = a->t; s->markAlong = a->along;
    }
    else
    {
        const float dx = a->pos[0] - s->last[0], dy = a->pos[1] - s->last[1], dz = a->pos[2] - s->last[2];
        s->distance += sqrtf(dx * dx + dy * dy + dz * dz);
    }
    memcpy(s->last, a->pos, sizeof s->last);
    s->samples++; s->t1 = a->t; s->along = a->along;
    if (a->speed > s->topSpeed) s->topSpeed = a->speed;
    if (a->pitch < s->pitchLow) s->pitchLow = a->pitch;
    if (a->pitch > s->pitchHigh) s->pitchHigh = a->pitch;
    if (a->roll < s->rollLow) s->rollLow = a->roll;
    if (a->roll > s->rollHigh) s->rollHigh = a->roll;
    if (fabsf(a->cross) > s->farthestOff) s->farthestOff = fabsf(a->cross);
    if (a->wheels >= 0)
    {
        if (a->wheels == 0) s->airborne++;
        if (s->fewestWheels < 0 || a->wheels < s->fewestWheels) s->fewestWheels = a->wheels;
    }
    if (fabsf(a->pitch) > 90.0f || fabsf(a->roll) > 90.0f) s->upsideDown++;
    const int driven = a->throttle > 0.5f;
    /* a stop: standing for a second with the throttle down; counted once until the vehicle moves again */
    if (driven && a->speed < CO_FRV_STOP_SPEED)
    {
        if (s->slowSince < 0.0f) s->slowSince = a->t;
        if (!s->stopped && a->t - s->slowSince >= CO_FRV_STOP_SECONDS) { s->stopped = 1; s->stops++; }
    }
    else if (a->speed >= 2.0f * CO_FRV_STOP_SPEED || !driven) { s->slowSince = -1.0f; s->stopped = 0; }
    /* stuck: the throttle down and under a metre of the leg gained in five seconds; counted once until it gains one */
    if (!driven || a->along >= s->markAlong + CO_FRV_STUCK_METRES) { s->markT = a->t; s->markAlong = a->along; s->stuckNow = 0; }
    else if (!s->stuckNow && a->t - s->markT >= CO_FRV_STUCK_SECONDS)
    {
        s->stuckNow = 1; s->stuck++; s->stuckAt[0] = a->pos[0]; s->stuckAt[1] = a->pos[1];
        began = 1;
    }
    return began;
}

int co_frv_pose_text(char* out, size_t size, const CoFrvSample* a)
{
    return snprintf(out, size, "frv: pose t %.1f s at (%.1f, %.1f, %.1f) speed %.1f m/s (game %.1f) pitch %+.1f roll %+.1f deg wheels %d of %d "
        "station %u of %u (%.0f m of %.0f, %+.1f m off) steer %+.2f throttle %.2f gear %d heading off %+.0f deg",
        (double)a->t, (double)a->pos[0], (double)a->pos[1], (double)a->pos[2], (double)a->speed, (double)a->gameSpeed, (double)a->pitch, (double)a->roll,
        a->wheels, a->wheelCount, a->station, a->stations, (double)a->along, (double)a->legMetres, (double)a->cross, (double)a->steer, (double)a->throttle,
        a->gear, (double)a->headingOff);
}

int co_frv_summary_text(char* out, size_t size, const CoFrvStats* s, const char* leg, float legMetres, const char* ended, int drove)
{
    const float seconds = s->t1 - s->t0;
    return snprintf(out, size, "frv: summary: %s: drove %.0f m of the leg's %.0f (%.0f m over the ground) in %.1f s, mean %.1f m/s, top %.1f, %u stops, "
        "%u stuck, pitch %+.1f to %+.1f deg, roll %+.1f to %+.1f deg, %u of %u samples with no wheel on the ground, fewest wheels %d, %u samples "
        "past 90 deg, farthest off the road %.1f m; ended: %s; verdict: %s",
        leg, (double)s->along, (double)legMetres, (double)s->distance, (double)seconds, seconds > 0.0f ? (double)(s->distance / seconds) : 0.0,
        (double)s->topSpeed, s->stops, s->stuck, (double)s->pitchLow, (double)s->pitchHigh, (double)s->rollLow, (double)s->rollHigh, s->airborne,
        s->samples, s->fewestWheels, s->upsideDown, (double)s->farthestOff, ended, drove ? "DROVE" : "FAILED");
}
