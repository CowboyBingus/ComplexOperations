/* The FRV drive's game-free part: a leg is cut from a road in either direction and ends at its length or the road's
 * end; the aim follows the leg in order, reads the side and the turn with the right signs and steers the game's way;
 * the tilt reads pitch and roll about the travel; a drive's sums count stops and stuck events once each; the lines
 * read as the harness script expects. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_frv.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NEAR(a, b, eps) (fabsf((float)(a) - (float)(b)) <= (eps))

static float g_road[64][2];
static CoFrvPath g_path;

/* q for a turn of `deg` about a unit axis */
static void Quat(float ax, float ay, float az, float deg, float* q)
{
    const float h = deg * 0.5f / 57.29578f, s = sinf(h);
    q[0] = ax * s; q[1] = ay * s; q[2] = az * s; q[3] = cosf(h);
}

int main(void)
{
    float d = 0.0f;
    for (int i = 0; i < 64; i++) { g_road[i][0] = 8.0f * (float)i; g_road[i][1] = 100.0f; } /* east, 8 m apart */

    g_case = "nearest";
    CHECK(co_frv_nearest((const float (*)[2])g_road, 0, 64, 83.0f, 103.0f, &d) == 10 && NEAR(d, sqrtf(18.0f), 1e-3f));
    CHECK(co_frv_nearest((const float (*)[2])g_road, 20, 64, 83.0f, 103.0f, &d) == 20);
    CHECK(co_frv_nearest((const float (*)[2])g_road, 5, 5, 0.0f, 0.0f, &d) == 5 && d > 1e30f);

    g_case = "leg";
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 10, 1, 100.0f, &g_path) == 14); /* 13 gaps of 8 m: 104 m */
    CHECK(NEAR(g_path.at[13], 104.0f, 1e-3f) && NEAR(g_path.xy[0][0], 80.0f, 1e-3f) && NEAR(g_path.xy[13][0], 184.0f, 1e-3f));
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 10, -1, 100.0f, &g_path) == 11); /* the road ends after 80 m */
    CHECK(NEAR(g_path.at[10], 80.0f, 1e-3f) && NEAR(g_path.xy[10][0], 0.0f, 1e-3f));
    CHECK(co_frv_leg((const float (*)[2])g_road, 4, 64, 10, -1, 100.0f, &g_path) == 7); /* never below `first` */
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 64, 1, 100.0f, &g_path) == 0);
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 10, 2, 100.0f, &g_path) == 0);
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 63, 1, 100.0f, &g_path) == 1); /* one point is no leg */
    g_road[12][0] = g_road[11][0]; /* a repeated point is left out */
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 10, 1, 100.0f, &g_path) == 13 && NEAR(g_path.at[2], 24.0f, 1e-3f));
    g_road[12][0] = 96.0f;
    CHECK(co_frv_leg((const float (*)[2])g_road, 0, 64, 0, 1, 1.0e6f, &g_path) == 64 && NEAR(g_path.at[63], 504.0f, 1e-2f));

    g_case = "aim";
    {
        CoFrvAim a; uint32_t seg = 0;
        const float east[2] = {1, 0}, north[2] = {0, 1};
        float pos[2] = {20.0f, 102.0f}; /* 2 m left of an eastward leg */
        co_frv_aim(&g_path, &seg, pos, east, 10.0f, &a);
        CHECK(seg == 2 && a.station == 2 && NEAR(a.along, 20.0f, 1e-3f) && NEAR(a.cross, 2.0f, 1e-3f));
        CHECK(NEAR(a.target[0], 30.0f, 1e-3f) && NEAR(a.target[1], 100.0f, 1e-3f));
        CHECK(a.error < 0.0f && NEAR(a.error, -atan2f(2.0f, 10.0f), 1e-3f)); /* the target is to the right */
        CHECK(co_frv_steer(a.error, 0.5f) > 0.0f);                          /* so the input turns right: positive */
        pos[1] = 97.0f;
        co_frv_aim(&g_path, &seg, pos, east, 10.0f, &a);
        CHECK(NEAR(a.cross, -3.0f, 1e-3f) && a.error > 0.0f && co_frv_steer(a.error, 0.5f) < 0.0f);
        co_frv_aim(&g_path, &seg, pos, north, 10.0f, &a); /* heading north, the target east: a right turn */
        CHECK(a.error < -1.0f && co_frv_steer(a.error, 0.5f) == 1.0f);
        /* the search never goes back, and looks only a window ahead */
        pos[0] = 4.0f; pos[1] = 100.0f; seg = 5;
        co_frv_aim(&g_path, &seg, pos, east, 10.0f, &a);
        CHECK(seg == 5 && NEAR(a.along, 40.0f, 1e-3f));
        /* the target stops at the leg's end */
        pos[0] = 500.0f; seg = 60;
        co_frv_aim(&g_path, &seg, pos, east, 10.0f, &a);
        CHECK(seg == 62 && NEAR(a.along, 500.0f, 1e-2f) && NEAR(a.target[0], 504.0f, 1e-2f) && a.station == 62);
        pos[0] = 510.0f;
        co_frv_aim(&g_path, &seg, pos, east, 10.0f, &a);
        CHECK(NEAR(a.along, 504.0f, 1e-2f) && a.station == 63);
        CoFrvPath none = {.count = 1}; seg = 3;
        co_frv_aim(&none, &seg, pos, east, 10.0f, &a);
        CHECK(a.along == 0.0f && a.error == 0.0f);
    }

    g_case = "steer and throttle";
    CHECK(co_frv_steer(0.0f, 0.5f) == 0.0f && co_frv_steer(0.25f, 0.5f) == -0.5f && co_frv_steer(-2.0f, 0.5f) == 1.0f && co_frv_steer(1.0f, 0.0f) == 0.0f);
    CHECK(co_frv_throttle(0.0f, 8.0f, 0.0f) == 1.0f && co_frv_throttle(8.5f, 8.0f, 0.0f) == 0.5f && co_frv_throttle(9.5f, 8.0f, 0.0f) == 0.0f);
    CHECK(co_frv_throttle(4.0f, 8.0f, 1.2f) < 0.3f && co_frv_throttle(2.0f, 8.0f, 3.0f) == 1.0f); /* slower into a turn, never under 0.4 of it */

    g_case = "steering sign";
    {
        CoFrvSign v = {0};
        CHECK(co_frv_sign(&v, 1.0f, -0.05f, 1.0f) == 0 && v.sum == 0.0f); /* too slow to count */
        CHECK(co_frv_sign(&v, 0.1f, -0.05f, 5.0f) == 0 && v.sum == 0.0f); /* hardly steered */
        for (int i = 0; i < 2; i++) CHECK(co_frv_sign(&v, 1.0f, -0.04f, 5.0f) == 0);
        CHECK(co_frv_sign(&v, -1.0f, 0.04f, 5.0f) == 1);  /* right input, right turn: as assumed */
        CHECK(co_frv_sign(&v, 1.0f, 0.4f, 5.0f) == 1);    /* decided: it stays */
        CoFrvSign w = {0};
        for (int i = 0; i < 2; i++) CHECK(co_frv_sign(&w, 1.0f, 0.04f, 5.0f) == 0);
        CHECK(co_frv_sign(&w, 1.0f, 0.04f, 5.0f) == -1);  /* right input, left turn: the other way round */
    }

    g_case = "axes and tilt";
    {
        float q[4], up[3], ahead[2], pitch, roll;
        const float east[2] = {1, 0}, north[2] = {0, 1};
        Quat(0, 0, 1, 0, q); co_frv_axes(q, up, ahead);
        CHECK(NEAR(up[2], 1, 1e-5f) && NEAR(ahead[0], 0, 1e-5f) && NEAR(ahead[1], 1, 1e-5f));
        Quat(0, 0, 1, 90, q); co_frv_axes(q, up, ahead); /* a quarter turn left: the y axis points west */
        CHECK(NEAR(ahead[0], -1, 1e-5f) && NEAR(ahead[1], 0, 1e-5f) && NEAR(up[2], 1, 1e-5f));
        co_frv_tilt(up, north, &pitch, &roll);
        CHECK(NEAR(pitch, 0, 1e-3f) && NEAR(roll, 0, 1e-3f));
        Quat(1, 0, 0, 10, q); co_frv_axes(q, up, ahead); /* about x: the y axis rises, the nose of a car facing north */
        co_frv_tilt(up, north, &pitch, &roll);
        CHECK(NEAR(pitch, 10, 1e-2f) && NEAR(roll, 0, 1e-2f));
        co_frv_tilt(up, east, &pitch, &roll);             /* the same tilt for a car travelling east: its right side is down */
        CHECK(NEAR(pitch, 0, 1e-2f) && NEAR(roll, 10, 1e-2f));
        Quat(0, 1, 0, 15, q); co_frv_axes(q, up, ahead); /* about y: the x side (right of a car facing north) goes down */
        co_frv_tilt(up, north, &pitch, &roll);
        CHECK(NEAR(pitch, 0, 1e-2f) && NEAR(roll, 15, 1e-2f));
        Quat(1, 0, 0, 180, q); co_frv_axes(q, up, ahead); /* on its roof */
        co_frv_tilt(up, north, &pitch, &roll);
        CHECK(fabsf(pitch) > 179.0f || fabsf(roll) > 179.0f);
        Quat(1, 0, 0, 90, q); co_frv_axes(q, up, ahead);  /* nose straight up: no heading across the ground */
        CHECK(ahead[0] == 0.0f && ahead[1] == 0.0f);
    }

    g_case = "stats";
    {
        CoFrvStats s; CoFrvSample a; memset(&a, 0, sizeof a);
        co_frv_stats_start(&s);
        a.wheels = 4; a.wheelCount = 4; a.throttle = 1.0f;
        int began = 0;
        /* 10 s at 8 m/s east, a sample every 0.1 s */
        for (int i = 0; i <= 100; i++)
        {
            a.t = 0.1f * (float)i; a.pos[0] = 0.8f * (float)i; a.along = a.pos[0]; a.speed = 8.0f; a.pitch = i == 40 ? 6.0f : i == 50 ? -4.0f : 1.0f;
            a.roll = i == 60 ? -3.0f : 0.5f; a.cross = i == 70 ? -1.5f : 0.2f; a.wheels = i == 80 ? 0 : i == 81 ? 2 : 4;
            began += co_frv_stats_add(&s, &a);
        }
        CHECK(s.samples == 101 && NEAR(s.distance, 80.0f, 1e-2f) && NEAR(s.t1 - s.t0, 10.0f, 1e-3f) && s.stops == 0 && s.stuck == 0 && began == 0);
        CHECK(s.pitchHigh == 6.0f && s.pitchLow == -4.0f && s.rollLow == -3.0f && s.rollHigh == 0.5f && s.farthestOff == 1.5f);
        CHECK(s.airborne == 1 && s.fewestWheels == 0 && s.topSpeed == 8.0f && s.upsideDown == 0);
        /* it stands against something for 7 s under throttle: one stop after 1 s, one stuck event after 5 s */
        for (int i = 1; i <= 70; i++) { a.t = 10.0f + 0.1f * (float)i; a.speed = 0.1f; a.wheels = 4; began += co_frv_stats_add(&s, &a); }
        CHECK(s.stops == 1 && s.stuck == 1 && began == 1 && NEAR(s.stuckAt[0], 80.0f, 1e-3f));
        /* it creeps on under a metre: still the same event; then it drives off and stops again: a second stop, no new stuck */
        for (int i = 1; i <= 30; i++) { a.t = 17.0f + 0.1f * (float)i; a.speed = 0.2f; a.along += 0.01f; began += co_frv_stats_add(&s, &a); }
        CHECK(s.stops == 1 && s.stuck == 1 && began == 1);
        for (int i = 1; i <= 20; i++) { a.t = 20.0f + 0.1f * (float)i; a.speed = 5.0f; a.along += 0.5f; a.pos[0] += 0.5f; began += co_frv_stats_add(&s, &a); }
        for (int i = 1; i <= 20; i++) { a.t = 22.0f + 0.1f * (float)i; a.speed = 0.0f; began += co_frv_stats_add(&s, &a); }
        CHECK(s.stops == 2 && s.stuck == 1 && began == 1);
        /* standing with the throttle up is neither */
        a.throttle = 0.0f;
        for (int i = 1; i <= 80; i++) { a.t = 24.0f + 0.1f * (float)i; began += co_frv_stats_add(&s, &a); }
        CHECK(s.stops == 2 && s.stuck == 1 && began == 1);
        a.pitch = 120.0f; a.t += 0.1f; co_frv_stats_add(&s, &a);
        CHECK(s.upsideDown == 1);
        a.wheels = -1; a.t += 0.1f; co_frv_stats_add(&s, &a); /* contact not read: not counted as airborne */
        CHECK(s.airborne == 1 && s.fewestWheels == 0);

        g_case = "lines";
        char line[640];
        CoFrvSample p = {.t = 12.34f, .pos = {1500.5f, -220.3f, 31.0f}, .speed = 8.2f, .gameSpeed = 8.1f, .pitch = 3.2f, .roll = -1.1f, .steer = -0.12f,
            .throttle = 1.0f, .along = 136.0f, .cross = 0.8f, .headingOff = 2.0f, .wheels = 4, .wheelCount = 4, .gear = 2, .station = 17, .stations = 64, .legMetres = 500.0f};
        CHECK(co_frv_pose_text(line, sizeof line, &p) > 0);
        CHECK(!strcmp(line, "frv: pose t 12.3 s at (1500.5, -220.3, 31.0) speed 8.2 m/s (game 8.1) pitch +3.2 roll -1.1 deg wheels 4 of 4 "
            "station 17 of 64 (136 m of 500, +0.8 m off) steer -0.12 throttle 1.00 gear 2 heading off +2 deg"));
        CoFrvStats t; co_frv_stats_start(&t);
        t.samples = 600; t.t0 = 1.0f; t.t1 = 61.0f; t.distance = 480.0f; t.along = 478.0f; t.topSpeed = 12.0f; t.stops = 1; t.stuck = 0; t.pitchLow = -4.2f;
        t.pitchHigh = 6.1f; t.rollLow = -3.0f; t.rollHigh = 2.2f; t.airborne = 3; t.fewestWheels = 2; t.farthestOff = 1.9f;
        CHECK(co_frv_summary_text(line, sizeof line, &t, "arterial edge 12 from (10, 20)", 500.0f, "reached the end", 1) > 0);
        CHECK(!strcmp(line, "frv: summary: arterial edge 12 from (10, 20): drove 478 m of the leg's 500 (480 m over the ground) in 60.0 s, mean 8.0 m/s, "
            "top 12.0, 1 stops, 0 stuck, pitch -4.2 to +6.1 deg, roll -3.0 to +2.2 deg, 3 of 600 samples with no wheel on the ground, fewest wheels 2, "
            "0 samples past 90 deg, farthest off the road 1.9 m; ended: reached the end; verdict: DROVE"));
        CHECK(co_frv_summary_text(line, sizeof line, &t, "spur 3", 90.0f, "stuck twice", 0) > 0 && strstr(line, "verdict: FAILED") != NULL);
    }

    if (g_failed) { printf("%d checks failed\n", g_failed); return 1; }
    printf("frv: all checks passed\n");
    return 0;
}
