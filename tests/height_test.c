#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xl/co_height.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define N 16
static float g_h[N * N]; /* a 64 m world: 4 m cells, row 0 at +y */

static void Set(int col, int rowFromBottom, float v) { g_h[(N - 1 - rowFromBottom) * N + col] = v; }

int main(void)
{
    CoHeightField f = {g_h, N, N, 64.0f};
    for (int i = 0; i < N * N; i++) g_h[i] = 10.0f;
    Set(1, 2, 3.0f);    /* centre (-26, -22) */
    Set(14, 13, 90.0f); /* (26, 22): far from the player */
    Set(9, 8, 40.0f);   /* (6, 2): 6.3 m from (0, 0) */
    Set(8, 8, 25.0f);   /* (2, 2): the player's cell */
    Set(8, 12, 70.0f);  /* (2, 18): 18.1 m from (0, 0) */

    g_case = "row 0 is +y and cells start at -world/2";
    CHECK(co_height_at(&f, 2.0f, 2.0f) == 25.0f);
    CHECK(co_height_at(&f, 0.0f, 0.0f) == 25.0f);      /* (0, 0) is the corner of cell (8, 8) */
    CHECK(co_height_at(&f, -0.01f, 0.0f) == 10.0f);    /* the cell to its west */
    CHECK(co_height_at(&f, 0.0f, -0.01f) == 10.0f);    /* and to its south */
    CHECK(co_height_at(&f, -26.0f, -22.0f) == 3.0f);
    CHECK(co_height_at(&f, 26.0f, 22.0f) == 90.0f);
    CHECK(co_height_at(&f, 26.0f, -22.0f) == 10.0f);   /* not flipped */
    CHECK(co_height_at(&f, -26.0f, 22.0f) == 10.0f);

    g_case = "outside the field";
    CHECK(isnan(co_height_at(&f, 32.0f, 0.0f)));
    CHECK(isnan(co_height_at(&f, 0.0f, 32.0f)));
    CHECK(isnan(co_height_at(&f, -32.01f, 0.0f)));
    CHECK(isnan(co_height_at(&f, 0.0f, -32.01f)));
    CHECK(co_height_at(&f, -32.0f, -32.0f) == 10.0f);
    CHECK(co_height_at(&f, 31.99f, 31.99f) == 10.0f);
    CHECK(isnan(co_height_at(&f, NAN, 0.0f)));

    CoHeightScan s;
    g_case = "the scan: range, here and the rise within the radius";
    CHECK(co_height_scan(&f, 1.0f, 1.0f, 10.0f, 1, 1, &s) == 1);
    CHECK(s.lo == 3.0f && s.hi == 90.0f);
    CHECK(s.here == 25.0f);
    CHECK(s.top == 40.0f && s.topX == 6.0f && s.topY == 2.0f); /* 70 m lies 17 m away, past 10 */

    g_case = "a wider radius reaches the higher cell";
    CHECK(co_height_scan(&f, 1.0f, 1.0f, 18.0f, 1, 1, &s) == 1);
    CHECK(s.top == 70.0f && s.topX == 2.0f && s.topY == 18.0f);

    g_case = "the radius is a circle, not a square";
    for (int i = 0; i < N * N; i++) g_h[i] = 10.0f;
    Set(12, 12, 50.0f); /* (18, 18) from (2, 2): 22.6 m away diagonally, 16 m on each axis */
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 20.0f, 1, 1, &s) == 1 && s.top == 10.0f);
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 23.0f, 1, 1, &s) == 1 && s.top == 50.0f);

    g_case = "non-finite samples are skipped";
    g_h[0] = NAN;
    g_h[1] = INFINITY;
    g_h[2] = -INFINITY;
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 23.0f, 1, 1, &s) == 1 && s.lo == 10.0f && s.hi == 50.0f);

    g_case = "strides sample every n-th cell";
    for (int i = 0; i < N * N; i++) g_h[i] = 10.0f;
    g_h[1 * N + 1] = 99.0f; /* row 1, column 1: skipped at a field stride of 2 */
    g_h[2 * N + 2] = -7.0f;
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 4.0f, 1, 2, &s) == 1 && s.hi == 10.0f && s.lo == -7.0f);
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 4.0f, 1, 1, &s) == 1 && s.hi == 99.0f);
    CHECK(co_height_scan(&f, 2.0f, 2.0f, 4.0f, 1, 0, &s) == 1 && s.hi == 99.0f); /* 0 is 1 */

    g_case = "no scan without a field or outside it";
    CoHeightField empty = {NULL, N, N, 64.0f};
    CHECK(co_height_scan(&empty, 0.0f, 0.0f, 10.0f, 1, 1, &s) == 0);
    CHECK(co_height_scan(&f, 40.0f, 0.0f, 10.0f, 1, 1, &s) == 0);
    CHECK(co_height_scan(&f, 0.0f, 0.0f, 10.0f, 1, 1, NULL) == 0);
    for (int i = 0; i < N * N; i++) g_h[i] = NAN;
    CHECK(co_height_scan(&f, 0.0f, 0.0f, 10.0f, 1, 1, &s) == 0);

    g_case = "the note";
    CoHeightLimits lim = {-480.0f, 8704.0f, 9216.0f};
    CoHeightScan t = {-6.8f, 89.8f, 8.3f, 64.4f, -1162.0f, 3514.0f};
    char line[512];
    int n = co_height_line(line, sizeof line, 2, &lim, &t, -869.5f, 3915.5f, 2000.0f);
    CHECK(n > 0 && (size_t)n == strlen(line));
    CHECK(strstr(line, "world height (level 2)") != NULL);
    CHECK(strstr(line, "units die below -480 m") != NULL);
    CHECK(strstr(line, "the physics world ends at +-8704 m") != NULL);
    CHECK(strstr(line, "network positions at +-9216 m") != NULL);
    CHECK(strstr(line, "no ceiling under 8704 m") != NULL);
    CHECK(strstr(line, "the ground -6.8 .. 89.8 m (8614 m under that ceiling)") != NULL);
    CHECK(strstr(line, "at the player (-870, 3916) 8.3 m") != NULL);
    CHECK(strstr(line, "within 2000 m it rises to 64.4 m (56.1 m above) at (-1162, 3514)") != NULL);
    lim.netHalf = 2000.0f; /* the lower of the two is the ceiling */
    CHECK(co_height_line(line, sizeof line, 2, &lim, &t, 0, 0, 2000.0f) > 0 && strstr(line, "no ceiling under 2000 m") != NULL);
    CHECK(strstr(line, "(1910 m under that ceiling)") != NULL);
    CHECK(co_height_line(line, 40, 2, &lim, &t, 0, 0, 2000.0f) == -1);
    CHECK(co_height_line(NULL, 40, 2, &lim, &t, 0, 0, 2000.0f) == -1);
    CHECK(co_height_line(line, sizeof line, 2, NULL, &t, 0, 0, 2000.0f) == -1);

    if (g_failed == 0) printf("height: all checks passed\n");
    return g_failed != 0;
}
