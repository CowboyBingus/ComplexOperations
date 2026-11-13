/* The far-grass overlay's ground heights (proto/difficulty_world_fargrass_ground.c): each 4 m texel, centred as the
 * overlay samples it, holds the highest drawn ground within its bilinear reach. Inside a tile's hidden square that is the
 * tile alone; across its edge the tile and the coarse terrain; outside every tile's span the coarse terrain alone. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestFgGroundJob(const uint16_t* coarse, uint32_t cn, float csize, const uint16_t* tile, uint32_t tn, float tsize, float tx, float ty, int tiles);
float CoXlTestFgGroundTexel(int c, int r);
void CoXlTestFgGroundRows(float* out, int r0, int r1);
float CoXlTestFgSamplesMax(const uint16_t* v, uint32_t n, float size, float cx, float cy, float x0, float y0, float x1, float y1);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint16_t Enc(float m) { return (uint16_t)lrintf((m + 500.0f) / 1000.0f * 65535.0f); }
static int Near(float a, float b) { return fabsf(a - b) < 0.02f; }

#define HIDE (512.0f * (16383.5f / 4095.0f))
#define SPAN (2304.0f * 4095.0f / 4096.0f)

/* The texel whose centre is nearest (x, y): column from the west edge, row from the north. */
static int Col(float x) { return (int)floorf((x + 8192.0f) / 4.0f); }
static int Row(float y) { return (int)floorf((8192.0f - y) / 4.0f); }

int main(void)
{
    g_case = "samples whose cells meet the box, rows from +y";
    {
        uint16_t v[25];
        for (int k = 0; k < 25; k++) v[k] = Enc(10.0f);
        v[0 * 5 + 2] = Enc(40.0f); /* row 0: y = +8, x = 0 */
        v[2 * 5 + 2] = Enc(30.0f); /* x = 0, y = 0 */
        CHECK(Near(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, 1.0f, 1.0f, 3.0f, 3.0f), 30.0f));    /* the cell (0..4, 0..4) */
        CHECK(Near(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, 4.5f, -3.0f, 7.0f, -1.0f), 10.0f));  /* x 4..8: neither peak */
        CHECK(Near(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, -1.0f, 5.0f, 1.0f, 7.0f), 40.0f));   /* north: row 0 */
        CHECK(Near(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, -1.0f, -7.0f, 1.0f, -5.0f), 10.0f)); /* south: not row 0 */
        CHECK(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, 20.0f, 0.0f, 30.0f, 1.0f) < -9000.0f);    /* off the grid */
        CHECK(Near(CoXlTestFgSamplesMax(v, 5, 16.0f, 0, 0, 7.0f, -1.0f, 30.0f, 1.0f), 10.0f));  /* clamped to it */
    }

    /* the world: a coarse grid of 2 m flat with a 9 m bump at (-4000, 3000); one tile at (1000, -1000), 5 m flat with a
     * 7 m peak at its centre and 12 m beyond its hidden square's east edge */
    const uint32_t cn = 4096, tn = 2049;
    const float csize = 16383.5f, cstep = csize / (float)(cn - 1), tstep = SPAN / (float)(tn - 1);
    uint16_t* coarse = (uint16_t*)malloc((size_t)cn * cn * 2);
    uint16_t* tile = (uint16_t*)malloc((size_t)tn * tn * 2);
    for (size_t k = 0; k < (size_t)cn * cn; k++) coarse[k] = Enc(2.0f);
    for (size_t k = 0; k < (size_t)tn * tn; k++) tile[k] = Enc(5.0f);
    int bc = (int)lrintf((-4000.0f + csize / 2) / cstep), br = (int)lrintf((csize / 2 - 3000.0f) / cstep);
    coarse[(size_t)br * cn + bc] = Enc(9.0f);
    tile[(size_t)(tn / 2) * tn + tn / 2] = Enc(7.0f);
    int ec = (int)lrintf((HIDE / 2 + 40.0f + SPAN / 2) / tstep); /* 40 m east of the hidden square, inside the span */
    tile[(size_t)(tn / 2) * tn + ec] = Enc(12.0f);
    float bx = -csize / 2 + bc * cstep, by = csize / 2 - br * cstep;
    float px = 1000.0f, py = -1000.0f, ex = px - SPAN / 2 + ec * tstep;

    g_case = "outside every tile: the coarse ground, its bump on the texels that reach it";
    CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 1);
    CHECK(Near(CoXlTestFgGroundTexel(Col(bx), Row(by)), 9.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(bx) + 3, Row(by)), 2.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(bx), Row(by) + 3), 2.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(-6000.0f), Row(6000.0f)), 2.0f));

    g_case = "inside a hidden square: the tile alone, its peak on the texels that reach it";
    CHECK(Near(CoXlTestFgGroundTexel(Col(px), Row(py)), 7.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(px + 300.0f), Row(py - 300.0f)), 5.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(px) + 3, Row(py)), 5.0f));
    {
        int c = Col(px + 300.0f), r = Row(py - 300.0f);
        coarse[(size_t)lrintf((csize / 2 - (py - 300.0f)) / cstep) * cn + (size_t)lrintf((px + 300.0f + csize / 2) / cstep)] = Enc(60.0f);
        CHECK(Near(CoXlTestFgGroundTexel(c, r), 5.0f)); /* the hidden coarse terrain does not count */
        coarse[(size_t)lrintf((csize / 2 - (py - 300.0f)) / cstep) * cn + (size_t)lrintf((px + 300.0f + csize / 2) / cstep)] = Enc(2.0f);
    }

    g_case = "outside the hidden square but in the tile's span: the higher of tile and coarse";
    CHECK(Near(CoXlTestFgGroundTexel(Col(ex), Row(py)), 12.0f));
    CHECK(Near(CoXlTestFgGroundTexel(Col(ex) + 4, Row(py)), 5.0f));
    {
        size_t at = (size_t)lrintf((csize / 2 - py) / cstep) * cn + (size_t)lrintf((ex + 8.0f + csize / 2) / cstep);
        coarse[at] = Enc(20.0f);
        CHECK(Near(CoXlTestFgGroundTexel(Col(ex + 8.0f), Row(py)), 20.0f));
        coarse[at] = Enc(2.0f);
    }

    g_case = "a tile's reach past its span: the coarse ground";
    CHECK(Near(CoXlTestFgGroundTexel(Col(px + SPAN / 2 + 20.0f), Row(py)), 2.0f));

    g_case = "with no tiles the square is coarse ground";
    CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 0);
    CHECK(Near(CoXlTestFgGroundTexel(Col(px), Row(py)), 2.0f));

    g_case = "the texels as the shader decodes them: (h + 500) / 1000, a row at a time";
    {
        CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 1);
        float* out = (float*)calloc((size_t)4096 * 4096, 4);
        int r = Row(py);
        CoXlTestFgGroundRows(out, r, r + 1);
        CHECK(Near(out[(size_t)r * 4096 + Col(px)] * 1000.0f - 500.0f, 7.0f));
        CHECK(Near(out[(size_t)r * 4096 + Col(-6000.0f)] * 1000.0f - 500.0f, 2.0f));
        CHECK(out[(size_t)(r + 1) * 4096 + Col(px)] == 0.0f && out[(size_t)(r - 1) * 4096 + Col(px)] == 0.0f);
        free(out);
    }

    g_case = "texel centres are 2 m off the coarse samples: a texel reaches the samples on both sides";
    {
        CoXlTestFgGroundJob(coarse, cn, csize, tile, tn, SPAN, px, py, 0);
        /* texel 0's centre is x = -8190; its reach -8194..-8186 meets the cells of samples 0, 1 and 2 */
        coarse[(size_t)2048 * cn + 2] = Enc(30.0f);
        CHECK(Near(CoXlTestFgGroundTexel(0, Row(csize / 2 - 2048 * cstep)), 30.0f));
        CHECK(Near(CoXlTestFgGroundTexel(2, Row(csize / 2 - 2048 * cstep)), 30.0f));
        CHECK(Near(CoXlTestFgGroundTexel(4, Row(csize / 2 - 2048 * cstep)), 2.0f));
        coarse[(size_t)2048 * cn + 2] = Enc(2.0f);
    }

    free(coarse); free(tile);
    if (g_failed) { printf("fargrassground: %d check(s) failed\n", g_failed); return 1; }
    printf("fargrassground: all checks passed\n");
    return 0;
}
