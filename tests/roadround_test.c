/* Roads round the places (proto/difficulty_world_roadround.c and the keep-out of difficulty_world_roadbuild.c): a line
 * clear of every footprint, taut and without a corner, the same on every call; a node and a spur's end moved to a
 * place's edge; the narrower margins and the straight line when no way round is fair. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void CoXlTestRoadsKeep(const float* discs, uint32_t n);
uint32_t CoXlTestRoadsKeepLine(const float* a, const float* b, uint32_t own, uint32_t n, float spacing, uint32_t most, float* out, float* length, int* state);
void CoXlTestRoadsKeepNode(float* p);
void CoXlTestRoadsSpurStart(const float* centre, const float* to, uint32_t own, float* out);
float CoXlTestRoundTurn(const float* p, uint32_t n);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define NONE 0xFFFFFFFFu
#define KEEP 58.0f

static float g_line[2 * 1024];
/* the least distance of the line's points, and of its pieces' middles, from a point */
static float Clear(const float* p, uint32_t n, float x, float y)
{
    float least = 1.0e9f;
    for (uint32_t i = 0; i < n; i++)
    {
        float d = sqrtf((p[2 * i] - x) * (p[2 * i] - x) + (p[2 * i + 1] - y) * (p[2 * i + 1] - y));
        if (d < least) least = d;
        if (i + 1 == n) continue;
        const float mx = 0.5f * (p[2 * i] + p[2 * i + 2]), my = 0.5f * (p[2 * i + 1] + p[2 * i + 3]);
        d = sqrtf((mx - x) * (mx - x) + (my - y) * (my - y));
        if (d < least) least = d;
    }
    return least;
}
static float Longest(const float* p, uint32_t n)
{
    float most = 0.0f;
    for (uint32_t i = 1; i < n; i++)
    {
        const float d = sqrtf((p[2 * i] - p[2 * i - 2]) * (p[2 * i] - p[2 * i - 2]) + (p[2 * i + 1] - p[2 * i - 1]) * (p[2 * i + 1] - p[2 * i - 1]));
        if (d > most) most = d;
    }
    return most;
}

static void Lines(void)
{
    static const float a[2] = {1000.0f, 5000.0f}, b[2] = {2000.0f, 5000.0f};
    float length = 0.0f; int state = -1;

    g_case = "no place: the straight line, a point every 10 m";
    CoXlTestRoadsKeep(NULL, 0);
    uint32_t n = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(n == 101 && state == 0 && fabsf(length - 1000.0f) < 0.01f);
    CHECK(g_line[0] == 1000.0f && g_line[1] == 5000.0f && g_line[200] == 2000.0f && g_line[201] == 5000.0f && fabsf(g_line[100] - 1500.0f) < 0.01f);

    g_case = "a place beside the line, its margin clear of it: the straight line";
    { static const float d[3] = {1500.0f, 5100.0f, 40.0f}; CoXlTestRoadsKeep(d, 1); }
    n = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(n == 101 && state == 0 && fabsf(g_line[101] - 5000.0f) < 1.0e-3f);

    g_case = "a place on the line: round it at 58 m from its footprint, taut, no corner";
    { static const float d[3] = {1500.0f, 5000.0f, 42.0f}; CoXlTestRoadsKeep(d, 1); }
    n = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(state == 1 && n == (uint32_t)ceilf(length / 10.0f) + 1);
    CHECK(g_line[0] == 1000.0f && g_line[1] == 5000.0f && g_line[2 * n - 2] == 2000.0f && g_line[2 * n - 1] == 5000.0f);
    CHECK(Clear(g_line, n, 1500.0f, 5000.0f) > 42.0f + KEEP - 0.6f); /* a 10 m piece sags 0.13 m inside a 100 m circle */
    {
        /* the taut line: two tangents of sqrt(500^2 - 100^2) and the arc between them */
        const double K = 100.0, D = 500.0, taut = 2.0 * sqrt(D * D - K * K) + K * (3.14159265 - 2.0 * acos(K / D));
        CHECK(length > taut - 0.5 && length < taut * 1.005);
    }
    CHECK(CoXlTestRoundTurn(g_line, n) > 0.98f); /* under 11.5 degrees between 10 m pieces: a 100 m circle turns 5.7 */
    CHECK(Longest(g_line, n) < 10.01f);
    /* a tie between the two sides goes to the left of a to b, on every call */
    CHECK(g_line[2 * (n / 2) + 1] > 5000.0f + 95.0f);
    {
        static float again[2 * 1024]; float l2; int s2;
        const uint32_t n2 = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, again, &l2, &s2);
        CHECK(n2 == n && l2 == length && memcmp(again, g_line, 8 * (size_t)n) == 0);
    }

    g_case = "a place off the line's middle: round it on the nearer side";
    { static const float d[3] = {1500.0f, 5030.0f, 42.0f}; CoXlTestRoadsKeep(d, 1); }
    n = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(state == 1 && g_line[2 * (n / 2) + 1] < 5000.0f - 60.0f && Clear(g_line, n, 1500.0f, 5030.0f) > 99.4f);

    g_case = "a fixed count of points: the same count, evenly along the longer line";
    n = CoXlTestRoadsKeepLine(a, b, NONE, 101, 0.0f, 101, g_line, &length, &state);
    CHECK(n == 101 && state == 1 && fabsf(Longest(g_line, n) - length / 100.0f) < 0.05f && Clear(g_line, n, 1500.0f, 5030.0f) > 99.4f);

    g_case = "places that overlap: round them all";
    { static const float d[9] = {1400.0f, 5010.0f, 30.0f, 1500.0f, 4990.0f, 50.0f, 1560.0f, 5060.0f, 20.0f}; CoXlTestRoadsKeep(d, 3); }
    n = CoXlTestRoadsKeepLine(a, b, NONE, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(state == 1 && Clear(g_line, n, 1400.0f, 5010.0f) > 87.4f && Clear(g_line, n, 1500.0f, 4990.0f) > 107.4f && Clear(g_line, n, 1560.0f, 5060.0f) > 77.4f);
    CHECK(CoXlTestRoundTurn(g_line, n) > 0.9f && length < 1100.0f);

    g_case = "a spur's own place is no obstacle, and one that holds an end is left at that end";
    { static const float d[6] = {1000.0f, 5000.0f, 40.0f, 1990.0f, 5000.0f, 30.0f}; CoXlTestRoadsKeep(d, 2); }
    n = CoXlTestRoadsKeepLine(a, b, 0, 0, 10.0f, 128, g_line, &length, &state);
    CHECK(n == 101 && state == 0 && g_line[200] == 2000.0f);

    g_case = "no fair way round at 58 m: the narrower margin";
    /* two places 150 m apart: 34 m from each footprint leaves a way between them, 58 m does not, and the way round
     * both takes the 20 points over 16 m apart */
    { static const float d[6] = {1500.0f, 5075.0f, 30.0f, 1500.0f, 4925.0f, 30.0f}; CoXlTestRoadsKeep(d, 2); }
    n = CoXlTestRoadsKeepLine(a, b, NONE, 101, 0.0f, 101, g_line, &length, &state);
    CHECK(n == 101 && state == 1 && Clear(g_line, n, 1500.0f, 5075.0f) > 87.4f && Clear(g_line, n, 1500.0f, 4925.0f) > 87.4f); /* the long edge has the points to go round both */
    {
        static const float c[2] = {1300.0f, 5000.0f}, e[2] = {1700.0f, 5000.0f};
        n = CoXlTestRoadsKeepLine(c, e, NONE, 30, 0.0f, 30, g_line, &length, &state);
        CHECK(n == 30 && state == 2 && Clear(g_line, n, 1500.0f, 5075.0f) > 63.0f && Clear(g_line, n, 1500.0f, 4925.0f) > 63.0f && Longest(g_line, n) < 16.0f);
    }

    g_case = "no fair way round at any margin: the straight line, counted";
    { static const float d[3] = {1500.0f, 5000.0f, 300.0f}; CoXlTestRoadsKeep(d, 1); }
    {
        static const float c[2] = {1150.0f, 5000.0f}, e[2] = {1850.0f, 5000.0f};
        n = CoXlTestRoadsKeepLine(c, e, NONE, 30, 0.0f, 30, g_line, &length, &state);
        CHECK(n == 30 && state == 3 && fabsf(length - 700.0f) < 0.01f && fabsf(g_line[2 * 15 + 1] - 5000.0f) < 1.0e-3f);
    }
}

static void Ends(void)
{
    g_case = "a node inside a place moves to 58 m from its footprint, one outside stays";
    { static const float d[3] = {1500.0f, 5000.0f, 42.0f}; CoXlTestRoadsKeep(d, 1); }
    float p[2] = {1530.0f, 5040.0f};
    CoXlTestRoadsKeepNode(p);
    CHECK(fabsf(p[0] - 1560.0f) < 0.01f && fabsf(p[1] - 5080.0f) < 0.01f);
    float q[2] = {1700.0f, 5000.0f};
    CoXlTestRoadsKeepNode(q);
    CHECK(q[0] == 1700.0f && q[1] == 5000.0f);
    float c[2] = {1500.0f, 5000.0f};
    CoXlTestRoadsKeepNode(c);
    CHECK(fabsf(c[0] - 1600.0f) < 0.01f && c[1] == 5000.0f); /* from the very middle: east */

    g_case = "a spur ends at its place's edge towards the arterial";
    static const float centre[2] = {1500.0f, 5000.0f}, to[2] = {1500.0f, 5300.0f};
    float s[2];
    CoXlTestRoadsSpurStart(centre, to, 0, s);
    CHECK(fabsf(s[0] - 1500.0f) < 1.0e-3f && fabsf(s[1] - 5042.0f) < 1.0e-3f);

    g_case = "another place's footprint over that point: the nearest free direction, 22.5 degrees on";
    { static const float d[6] = {1500.0f, 5000.0f, 42.0f, 1505.0f, 5045.0f, 10.0f}; CoXlTestRoadsKeep(d, 2); }
    CoXlTestRoadsSpurStart(centre, to, 0, s);
    CHECK(fabsf(sqrtf((s[0] - 1500.0f) * (s[0] - 1500.0f) + (s[1] - 5000.0f) * (s[1] - 5000.0f)) - 42.0f) < 1.0e-3f);
    CHECK(fabsf(s[0] - (1500.0f - 42.0f * 0.3826834f)) < 0.01f && fabsf(s[1] - (5000.0f + 42.0f * 0.9238795f)) < 0.01f);

    g_case = "footprints all round: the point towards the arterial all the same";
    { static const float d[6] = {1500.0f, 5000.0f, 42.0f, 1500.0f, 5000.0f, 60.0f}; CoXlTestRoadsKeep(d, 2); }
    CoXlTestRoadsSpurStart(centre, to, 0, s);
    CHECK(fabsf(s[0] - 1500.0f) < 1.0e-3f && fabsf(s[1] - 5042.0f) < 1.0e-3f);

    g_case = "the arterial inside the place: the spur runs from the middle";
    { static const float d[3] = {1500.0f, 5000.0f, 42.0f}; CoXlTestRoadsKeep(d, 1); }
    { static const float in[2] = {1500.0f, 5030.0f}; CoXlTestRoadsSpurStart(centre, in, 0, s); }
    CHECK(s[0] == 1500.0f && s[1] == 5000.0f);
}

int main(void)
{
    Lines();
    Ends();
    if (g_failed) { printf("roadround: %d check(s) failed\n", g_failed); return 1; }
    printf("roadround: all checks passed\n");
    return 0;
}
