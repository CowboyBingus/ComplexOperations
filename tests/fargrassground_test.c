/* The far-grass overlay's ground heights (proto/difficulty_world_fargrass_ground.c): the drawn ground by
 * NearVisibleAt's rule (the tile alone inside its hidden square, the higher of tile and coarse terrain over the rest of
 * its span, the coarse terrain elsewhere), and the 4 m texture the overlay interpolates: never under that ground at any
 * sample of a drawn grid, and on a plane of any slope the ground itself, as u16 the shader decodes as v * 1000 - 500. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestFgGroundJob(const uint16_t* coarse, uint32_t cn, float csize, const uint16_t* tile, uint32_t tn, float tsize, float tx, float ty, int tiles);
float CoXlTestFgGroundAt(float x, float y);
void CoXlTestFgGroundRows(uint16_t* out, int r0, int r1);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define N 4096
#define HIDE (512.0f * (16383.5f / 4095.0f))
#define SPAN (2304.0f * 4095.0f / 4096.0f)

static uint16_t Enc(float m) { return (uint16_t)lrintf((m + 500.0f) / 1000.0f * 65535.0f); }
static int Near(float a, float b) { return fabsf(a - b) < 0.02f; }
static float Dec(uint16_t q) { return (float)q / 65535.0f * 1000.0f - 500.0f; } /* as the shader reads it */
static float X(int c) { return (c + 0.5f) * 4.0f - 8192.0f; }
static float Y(int r) { return 8192.0f - (r + 0.5f) * 4.0f; }
static int Col(float x) { return (int)floorf((x + 8192.0f) / 4.0f); }
static int Row(float y) { return (int)floorf((8192.0f - y) / 4.0f); }

static uint16_t* g_out;
/* The overlay's height at (x, y): bilinear between the four texel centres around it (their rows built). */
static float H(float x, float y)
{
    int c = (int)floorf((x - X(0)) / 4.0f), r = (int)floorf((Y(0) - y) / 4.0f);
    float u = (x - X(c)) / 4.0f, v = (Y(r) - y) / 4.0f;
    const uint16_t* a = g_out + (size_t)r * N, *b = a + N;
    return (Dec(a[c]) * (1 - u) + Dec(a[c + 1]) * u) * (1 - v) + (Dec(b[c]) * (1 - u) + Dec(b[c + 1]) * u) * v;
}
static void Build(float yNorth, float ySouth) { CoXlTestFgGroundRows(g_out, Row(yNorth) - 1, Row(ySouth) + 2); }

static uint32_t g_seed = 12345;
static float Noise(float lo, float hi) { g_seed = g_seed * 1664525u + 1013904223u; return lo + (hi - lo) * (float)(g_seed >> 8) / 16777216.0f; }

int main(void)
{
    const uint32_t cn = 4096, tn = 2049;
    const float csize = 16383.5f, cstep = csize / (float)(cn - 1), tstep = SPAN / (float)(tn - 1);
    uint16_t* coarse = (uint16_t*)malloc((size_t)cn * cn * 2);
    uint16_t* tile = (uint16_t*)malloc((size_t)tn * tn * 2);
    g_out = (uint16_t*)calloc((size_t)N * N, 2);
    for (size_t k = 0; k < (size_t)cn * cn; k++) coarse[k] = Enc(2.0f);
    for (size_t k = 0; k < (size_t)tn * tn; k++) tile[k] = Enc(5.0f);
    const float px = 1000.0f, py = -1000.0f;
#define CO(x, y) coarse[(size_t)lrintf((csize / 2 - (y)) / cstep) * cn + (size_t)lrintf(((x) + csize / 2) / cstep)]
#define TI(x, y) tile[(size_t)lrintf((SPAN / 2 - ((y) - py)) / tstep) * tn + (size_t)lrintf(((x) - px + SPAN / 2) / tstep)]
    CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 1);

    g_case = "the drawn ground: coarse off the tile, the tile alone in its hidden square, the higher of the two past it";
    CHECK(Near(CoXlTestFgGroundAt(-6000.0f, 6000.0f), 2.0f));
    CHECK(Near(CoXlTestFgGroundAt(px + 300.0f, py - 300.0f), 5.0f));
    CO(px + 300.0f, py - 300.0f) = Enc(60.0f);
    CHECK(Near(CoXlTestFgGroundAt(px + 300.0f, py - 300.0f), 5.0f));
    CO(px + 300.0f, py - 300.0f) = Enc(2.0f);
    /* 40 m east of the hidden square, inside the span: at a coarse sample, then at a tile sample */
    const float ex = -csize / 2 + lrintf((px + HIDE / 2 + 40.0f + csize / 2) / cstep) * cstep, ey = csize / 2 - lrintf((csize / 2 - py) / cstep) * cstep;
    const float fx = px - SPAN / 2 + lrintf((px + HIDE / 2 + 60.0f - px + SPAN / 2) / tstep) * tstep;
    CHECK(Near(CoXlTestFgGroundAt(ex, ey), 5.0f));
    CO(ex, ey) = Enc(20.0f);
    CHECK(Near(CoXlTestFgGroundAt(ex, ey), 20.0f));
    CO(ex, ey) = Enc(2.0f);
    TI(fx, py) = Enc(12.0f);
    CHECK(Near(CoXlTestFgGroundAt(fx, py), 12.0f));
    TI(fx, py) = Enc(5.0f);
    CHECK(Near(CoXlTestFgGroundAt(px + SPAN / 2 + 20.0f, py), 2.0f));

    g_case = "the asked rows only, and flat ground as itself";
    {
        int r = Row(-6000.0f);
        CoXlTestFgGroundRows(g_out, r, r + 1);
        CHECK(g_out[(size_t)(r - 1) * N + 100] == 0 && g_out[(size_t)(r + 1) * N + 100] == 0);
        float h = Dec(g_out[(size_t)r * N + Col(-6000.0f)]);
        CHECK(h >= CoXlTestFgGroundAt(-6000.0f, 6000.0f) - 0.001f && h < 2.0f + 0.03f);
    }

    g_case = "a 45 degree plane: the texels are the ground, not the highest ground within 4 m";
    {
        /* coarse ramp rising 1 m a metre eastward from x = -6400 to -6100, flat beyond */
        for (uint32_t r = 0; r < cn; r++)
            for (uint32_t c = 0; c < cn; c++)
            {
                float x = -csize / 2 + c * cstep;
                if (x > -6420.0f && x < -6080.0f) coarse[(size_t)r * cn + c] = Enc(fminf(fmaxf(x + 6400.0f, 0.0f), 300.0f));
            }
        Build(5020.0f, 4980.0f);
        float worstOver = 0.0f, worstUnder = 0.0f;
        for (float y = 4990.0f; y <= 5010.0f; y += 0.7f)
            for (float x = -6380.0f; x <= -6120.0f; x += 0.9f)
            {
                float d = H(x, y) - CoXlTestFgGroundAt(x, y);
                worstOver = fmaxf(worstOver, d); worstUnder = fminf(worstUnder, d);
            }
        printf("  plane: overlay height minus ground %.3f .. %.3f m\n", worstUnder, worstOver);
        CHECK(worstUnder > -0.002f && worstOver < 0.08f);
    }

    g_case = "a coarse spike: the overlay reaches it at its sample and stays flat a few texels off";
    {
        float sx = -4000.0f, sy = 3000.0f;
        int sc = (int)lrintf((sx + csize / 2) / cstep), sr = (int)lrintf((csize / 2 - sy) / cstep);
        float vx = -csize / 2 + sc * cstep, vy = csize / 2 - sr * cstep;
        coarse[(size_t)sr * cn + sc] = Enc(9.0f);
        Build(vy + 40.0f, vy - 40.0f);
        CHECK(H(vx, vy) >= CoXlTestFgGroundAt(vx, vy) - 0.002f);
        CHECK(CoXlTestFgGroundAt(vx, vy) > 8.9f);
        CHECK(H(vx + 20.0f, vy) < 2.1f && H(vx, vy - 20.0f) < 2.1f);
        coarse[(size_t)sr * cn + sc] = Enc(2.0f);
    }

    g_case = "rough ground: never under any sample of a drawn grid (coarse, tile, both across the hidden square's edge)";
    {
        /* noise on the coarse grid around the tile's east edge and on the tile there */
        float x0 = px + HIDE / 2 - 60.0f, x1 = px + HIDE / 2 + 60.0f, y0 = py - 40.0f, y1 = py + 40.0f;
        for (float y = y0 - 8.0f; y <= y1 + 8.0f; y += cstep * 0.5f)
            for (float x = x0 - 8.0f; x <= x1 + 8.0f; x += cstep * 0.5f) CO(x, y) = Enc(Noise(0.0f, 30.0f));
        for (float y = y0 - 8.0f; y <= y1 + 8.0f; y += tstep * 0.5f)
            for (float x = x0 - 8.0f; x <= x1 + 8.0f; x += tstep * 0.5f) TI(x, y) = Enc(Noise(0.0f, 30.0f));
        Build(y1 + 8.0f, y0 - 8.0f);
        float worst = 0.0f; int samples = 0;
        for (float y = y0; y <= y1; y += tstep * 0.25f)
            for (float x = x0; x <= x1; x += tstep * 0.25f)
            {
                /* the grids' own samples: the tile's and the coarse terrain's nearest */
                float tx = px - SPAN / 2 + lrintf((x - px + SPAN / 2) / tstep) * tstep, ty = py + SPAN / 2 - lrintf((py + SPAN / 2 - y) / tstep) * tstep;
                float cx = -csize / 2 + lrintf((x + csize / 2) / cstep) * cstep, cy = csize / 2 - lrintf((csize / 2 - y) / cstep) * cstep;
                worst = fminf(worst, H(tx, ty) - CoXlTestFgGroundAt(tx, ty));
                worst = fminf(worst, H(cx, cy) - CoXlTestFgGroundAt(cx, cy));
                samples += 2;
            }
        printf("  rough: %d samples, worst overlay height minus ground %.4f m\n", samples, worst);
        CHECK(worst > -0.002f);
    }

    g_case = "a tile spike between the texel centres and the coarse samples: the overlay still reaches it";
    {
        /* a tile sample whose x is >= 1.1 m from every texel centre's and whose y is >= 1.1 m from every coarse sample's,
         * so neither lattice sees the spike: only the tile's own samples do */
        float sx = 0.0f, sy = 0.0f;
        for (int k = 0; k < 64 && sx == 0.0f; k++)
        {
            float x = px - SPAN / 2 + (lrintf((px + 200.0f - px + SPAN / 2) / tstep) + k) * tstep;
            float d = fmodf(x + 8192.0f + 400000.0f, 4.0f);
            if (fabsf(d - 2.0f) >= 1.1f && fabsf(d - 2.0f) <= 2.9f) sx = x;
        }
        for (int k = 0; k < 64 && sy == 0.0f; k++)
        {
            float y = py + SPAN / 2 - (lrintf((py + SPAN / 2 - (py + 200.0f)) / tstep) + k) * tstep;
            float f = (csize / 2 - y) / cstep, d = fabsf(f - floorf(f + 0.5f)) * cstep;
            if (d >= 1.1f) sy = y;
        }
        CHECK(sx != 0.0f && sy != 0.0f);
        TI(sx, sy) = Enc(15.0f);
        Build(sy + 20.0f, sy - 20.0f);
        CHECK(Near(CoXlTestFgGroundAt(sx, sy), 15.0f));
        CHECK(H(sx, sy) >= CoXlTestFgGroundAt(sx, sy) - 0.002f);
        TI(sx, sy) = Enc(5.0f);
    }

    g_case = "inside the hidden square a coarse spike does not lift the overlay";
    {
        float sx = px - 500.0f, sy = py + 500.0f;
        CO(sx, sy) = Enc(60.0f);
        Build(sy + 20.0f, sy - 20.0f);
        CHECK(H(sx, sy) < 5.1f && H(sx, sy) >= CoXlTestFgGroundAt(sx, sy) - 0.002f);
        CO(sx, sy) = Enc(2.0f);
    }

    g_case = "with no tiles the square is coarse ground";
    CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 0);
    Build(py + 20.0f, py - 20.0f);
    CHECK(H(px, py) >= CoXlTestFgGroundAt(px, py) - 0.002f && H(px, py) < 2.1f);

    free(coarse); free(tile); free(g_out);
    if (g_failed) { printf("fargrassground: %d check(s) failed\n", g_failed); return 1; }
    printf("fargrassground: all checks passed\n");
    return 0;
}
