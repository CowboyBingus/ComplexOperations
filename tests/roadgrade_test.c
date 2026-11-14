/* The roads' graded profile (proto/difficulty_world_roadgrade.c): the grade limit on chains and junctions, pads kept
 * at their height, pins that cannot be met counted, crests eased, and the cut and fill it costs. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t CoXlTestGradeSolve(const float* ground, const uint8_t* pinned, uint32_t n, const uint32_t* links, const float* gaps, uint32_t count, float slope,
    uint32_t passes, float* target, double out[9]);

uint32_t CoXlTestGradeField(const float* pts, const uint32_t* counts, const uint32_t* ends, uint32_t chains, int spur, float (*ground)(float, float), float floor,
    float slope, uint32_t passes, float world, uint32_t* torn);
float CoXlTestGradeFieldAt(float x, float y, float* z, float* d);
float CoXlTestGradeStation(uint32_t i, float* xy, float* band, uint32_t* links);
uint32_t CoXlTestGradeFieldPieces(uint32_t* unlinked, uint32_t* capped);
void CoXlTestGradeRoad(float (*height)(void*, float, float), const float* pts, uint32_t n, double out[9]);
typedef void (*TriFn)(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material, const float* uv0,
    const float* uv1, const float* uv2);
uint32_t CoXlTestGradeDraw(float world, float playable, TriFn tri, float px, uint32_t most, uint32_t* cells);

static int g_failed;
static const char* g_case = "";
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define N 200
#define STEP 4.0f
#define SLOPE 0.0874887f /* tan 5 degrees */

static uint32_t g_links[2 * (N + 8)];
static float g_gaps[N + 8], g_ground[N + 8], g_target[N + 8];
static uint8_t g_pinned[N + 8];
static double g_out[9];

static uint32_t Chain(uint32_t n)
{
    for (uint32_t i = 0; i + 1 < n; i++) { g_links[2 * i] = i; g_links[2 * i + 1] = i + 1; g_gaps[i] = STEP; }
    return n - 1;
}

static void Flat(void)
{
    g_case = "level ground stays as it is";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) g_ground[i] = 12.5f;
    const uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 16, g_target, g_out);
    CHECK(torn == 0);
    for (uint32_t i = 0; i < N; i++) CHECK(g_target[i] == 12.5f);
    CHECK(g_out[0] == 0.0 && g_out[2] == 0.0 && g_out[3] == 0.0);
    CHECK(fabs(g_out[4] - (N - 1) * STEP) < 1.0e-3);
    g_case = "a slope within the limit stays as it is";
    for (uint32_t i = 0; i < N; i++) g_ground[i] = 0.05f * STEP * (float)i;
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    for (uint32_t i = 0; i < N; i++) CHECK(fabsf(g_target[i] - g_ground[i]) < 1.0e-3f);
    CHECK(fabs(g_out[0] - 2.8624) < 0.01);
}

static void Cliff(void)
{
    g_case = "a 20 m step becomes a ramp at the limit, half cut and half fill";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) g_ground[i] = i < N / 2 ? 0.0f : 20.0f;
    const uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(torn == 0);
    CHECK(g_out[0] <= 5.001 && g_out[0] > 4.99);
    CHECK(fabs(g_out[2] - 10.0) < 0.2 && fabs(g_out[3] - 10.0) < 0.2);
    CHECK(g_out[5] == 0.0 && g_out[6] == 0.0);
    CHECK(g_out[7] > 60.0 && g_out[7] < 110.0 && g_out[8] == 0.0);
    CHECK(g_target[0] == 0.0f && g_target[N - 1] == 20.0f);
    for (uint32_t i = 0; i + 1 < N; i++) CHECK(g_target[i + 1] >= g_target[i] && g_target[i + 1] - g_target[i] <= SLOPE * STEP + 1.0e-4f);
    g_case = "with a looser limit only the link at the step is over 5 degrees";
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), 0.14f, 0, g_target, g_out);
    CHECK(g_out[0] > 7.9 && g_out[0] <= 8.001);
    CHECK(fabs(g_out[5] - STEP) < 0.01 && g_out[6] == 0.0); /* the mean of the two surfaces climbs at half the limit, but for the link at the step */
}

static void Pads(void)
{
    g_case = "a pad on the ramp keeps the ground's height and the road meets it within the limit";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) g_ground[i] = i < N / 2 ? 0.0f : 20.0f;
    for (uint32_t i = N / 2; i < N / 2 + 5; i++) g_pinned[i] = 1;
    uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(torn == 0);
    for (uint32_t i = N / 2; i < N / 2 + 5; i++) CHECK(g_target[i] == 20.0f);
    CHECK(g_out[0] <= 5.001);
    CHECK(g_out[2] < 0.01 && fabs(g_out[3] - (20.0 - SLOPE * STEP)) < 0.01);
    g_case = "a pad below the ramp keeps its height too: the road is cut down to it";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = N / 2 - 5; i < N / 2; i++) g_pinned[i] = 1;
    torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(torn == 0);
    for (uint32_t i = N / 2 - 5; i < N / 2; i++) CHECK(g_target[i] == 0.0f);
    CHECK(g_out[0] <= 5.001);
    CHECK(g_out[3] < 0.01 && fabs(g_out[2] - (20.0 - SLOPE * STEP)) < 0.01);
    g_case = "two pads the road cannot join within the limit are counted, and both keep their height";
    memset(g_pinned, 0, sizeof g_pinned);
    g_pinned[N / 2 - 1] = g_pinned[N / 2 + 2] = 1;
    torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(torn >= 4);
    CHECK(g_target[N / 2 - 1] == 0.0f && g_target[N / 2 + 2] == 20.0f);
    CHECK(g_out[0] > 45.0);
    CHECK(g_out[6] >= 3 * STEP - 0.01);
}

static void Junction(void)
{
    g_case = "three roads meeting at a station agree on its height, each within the limit";
    memset(g_pinned, 0, sizeof g_pinned);
    /* stations 0..59 one arm (ground 0), 60..119 a second (ground 20), 120..179 a third (ground 12); arms meet at 180 */
    uint32_t links = 0;
    for (uint32_t arm = 0; arm < 3; arm++)
    {
        for (uint32_t i = 0; i < 60; i++)
        {
            const uint32_t at = arm * 60 + i;
            g_ground[at] = arm == 0 ? 0.0f : arm == 1 ? 20.0f : 12.0f;
            g_links[2 * links] = at; g_links[2 * links + 1] = i + 1 < 60 ? at + 1 : 180; g_gaps[links++] = STEP;
        }
    }
    g_ground[180] = 12.0f;
    const uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, 181, g_links, g_gaps, links, SLOPE, 0, g_target, g_out);
    CHECK(torn == 0);
    CHECK(g_out[0] <= 5.001);
    CHECK(fabsf(g_target[180] - 10.0f) < 0.5f);
    CHECK(fabsf(g_target[59] - g_target[180]) <= SLOPE * STEP + 1.0e-4f && fabsf(g_target[119] - g_target[180]) <= SLOPE * STEP + 1.0e-4f &&
        fabsf(g_target[179] - g_target[180]) <= SLOPE * STEP + 1.0e-4f);
    CHECK(g_target[0] == 0.0f && g_target[60] == 20.0f);
    CHECK(fabs(g_out[4] - 180 * STEP) < 1.0e-2);
}

static void Crest(void)
{
    g_case = "a crest within the grade limit is eased under the bend limit by 16 passes, the grade kept";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) g_ground[i] = 0.08f * STEP * (float)(i < N / 2 ? i : N - 1 - i);
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(fabs(g_out[1] - 4.574) < 0.02 && g_out[2] < 1.0e-3);
    /* 2 degrees a 5 m is 1.6 degrees at 4 m stations */
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 16, g_target, g_out);
    CHECK(g_out[1] < 1.6 && g_out[1] > 0.3);
    CHECK(g_out[0] <= 5.001);
    CHECK(g_out[2] > 0.2 && g_out[2] < 3.0 && g_out[3] < 0.5);
    g_case = "a pad at the crest stays while the road beside it is eased";
    g_pinned[N / 2 - 1] = g_pinned[N / 2] = 1;
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 16, g_target, g_out);
    CHECK(g_target[N / 2] == g_ground[N / 2] && g_target[N / 2 - 1] == g_ground[N / 2 - 1]);
    CHECK(g_out[0] <= 5.001);
    g_case = "stations at uneven distances: easing does not take the road over the grade limit";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i + 1 < N; i++) g_gaps[i] = i % 2 ? 9.0f : 1.0f;
    float at = 0.0f;
    for (uint32_t i = 0; i < N; i++) { g_ground[i] = i < N / 2 ? SLOPE * at : SLOPE * at - 2.0f * SLOPE * (at - 5.0f * (float)(N / 2)); at += g_gaps[i < N - 1 ? i : 0]; }
    for (uint32_t i = 0; i + 1 < N; i++) { g_links[2 * i] = i; g_links[2 * i + 1] = i + 1; }
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, N - 1, SLOPE, 3, g_target, g_out);
    CHECK(g_out[0] <= 5.001);
    CHECK(g_out[2] > 0.05);
}

/* The solver against its definition, worked out the slow way on a chain (every pair of stations, in doubles): pins
 * give each station a range; the profile is the ground moved into the range, then the mean of the two surfaces at the
 * limit, then the range again; a pass eases every free inner station halfway to the line between its neighbours and
 * repeats the mean and the range. A pinned station is never eased: where pins cannot be joined its eased height would
 * shift both surfaces, and with them the stations beyond the torn piece.
 * Four changes to the solver that no case here tells apart, and why each leaves the result as it is:
 * - Easing without the mean of the two surfaces after it. Easing keeps the limit by itself: with m_k the mean slope
 *   of the two links at k (within the limit when both links are), the eased slope from i - 1 to i is
 *   (m_(i-1) + m_i) / 2 when both stations move and (slope + m_i) / 2 when one stays. The mean of the surfaces is
 *   then the profile itself. The solver keeps the step for the pieces pins tear, which start over the limit.
 * - Easing that also moves pinned stations. A free station's eased height reads its neighbours' heights from before
 *   the pass, so it is the same; the pin is put back by the range (its low and high are its ground). Where the
 *   limit holds the mean of the surfaces changes nothing in between. Where pins tear the road the definition
 *   above is what holds, and the third run compares it.
 * - The surfaces swept forward twice instead of forward and back: the sweeps repeat until nothing changes, so only
 *   their count differs.
 * - A link measured from its other end: its length, grade and the larger move of its two ends are the same. */
static double g_at[N], g_ref[N], g_low[N], g_high[N];
static void RefWithin(uint32_t n, double slope)
{
    static double under[N], over[N];
    for (uint32_t i = 0; i < n; i++)
    {
        under[i] = over[i] = g_ref[i];
        for (uint32_t j = 0; j < n; j++)
        {
            const double reach = slope * fabs(g_at[i] - g_at[j]);
            if (g_ref[j] + reach < under[i]) under[i] = g_ref[j] + reach;
            if (g_ref[j] - reach > over[i]) over[i] = g_ref[j] - reach;
        }
    }
    for (uint32_t i = 0; i < n; i++) g_ref[i] = 0.5 * (under[i] + over[i]);
}
static void RefClamp(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        if (g_low[i] > g_high[i]) g_ref[i] = g_pinned[i] ? (double)g_ground[i] : 0.5 * (g_low[i] + g_high[i]);
        else if (g_ref[i] < g_low[i]) g_ref[i] = g_low[i];
        else if (g_ref[i] > g_high[i]) g_ref[i] = g_high[i];
    }
}
static uint32_t RefSolve(uint32_t n, double slope, uint32_t passes)
{
    static double next[N];
    uint32_t torn = 0;
    g_at[0] = 0.0;
    for (uint32_t i = 1; i < n; i++) g_at[i] = g_at[i - 1] + g_gaps[i - 1];
    for (uint32_t i = 0; i < n; i++)
    {
        g_low[i] = -1.0e30; g_high[i] = 1.0e30;
        for (uint32_t j = 0; j < n; j++)
        {
            if (!g_pinned[j]) continue;
            const double reach = slope * fabs(g_at[i] - g_at[j]);
            if (g_ground[j] - reach > g_low[i]) g_low[i] = g_ground[j] - reach;
            if (g_ground[j] + reach < g_high[i]) g_high[i] = g_ground[j] + reach;
        }
        torn += g_low[i] > g_high[i] + 1.0e-4;
        g_ref[i] = g_ground[i];
    }
    RefClamp(n); RefWithin(n, slope); RefClamp(n);
    for (uint32_t pass = 0; pass < passes; pass++)
    {
        for (uint32_t i = 0; i < n; i++)
        {
            next[i] = g_ref[i];
            if (i == 0 || i + 1 == n || g_pinned[i]) continue;
            const double a = g_gaps[i - 1], b = g_gaps[i];
            next[i] = 0.5 * (g_ref[i] + (g_ref[i - 1] * b + g_ref[i + 1] * a) / (a + b));
        }
        for (uint32_t i = 0; i < n; i++) g_ref[i] = next[i];
        RefWithin(n, slope); RefClamp(n);
    }
    return torn;
}

static void Reference(void)
{
    static const char* const names[3] = { "definition: rough ground, uneven stations, no pads", "definition: pads the road can join",
        "definition: pads the road cannot join, eased" };
    uint32_t seed = 0x2F6E2B1u;
    for (uint32_t run = 0; run < 3; run++)
    {
        g_case = names[run];
        memset(g_pinned, 0, sizeof g_pinned);
        float height = 30.0f;
        for (uint32_t i = 0; i < N; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            height += ((float)(seed >> 8 & 0xFFFF) / 65535.0f - 0.5f) * 6.0f;
            g_ground[i] = height + (i > N / 2 ? 14.0f : 0.0f);
            seed = seed * 1664525u + 1013904223u;
            if (i + 1 < N) { g_links[2 * i] = i; g_links[2 * i + 1] = i + 1; g_gaps[i] = 1.0f + (float)(seed >> 8 & 0xFF) / 255.0f * 9.0f; }
        }
        if (run == 1) { g_pinned[40] = g_pinned[41] = g_pinned[150] = 1; g_ground[41] = g_ground[40]; }
        if (run == 2) { g_pinned[N / 2 - 3] = g_pinned[N / 2 + 2] = g_pinned[20] = 1; g_ground[N / 2 + 2] = g_ground[N / 2 - 3] + 25.0f; }
        const uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, N - 1, SLOPE, 5, g_target, g_out);
        const uint32_t expected = RefSolve(N, (double)SLOPE, 5);
        CHECK(torn == expected && (run == 2) == (torn > 0));
        double worst = 0.0, steepest = 0.0;
        for (uint32_t i = 0; i < N; i++) if (fabs(g_target[i] - g_ref[i]) > worst) worst = fabs(g_target[i] - g_ref[i]);
        CHECK(worst < 5.0e-3);
        for (uint32_t i = 0; i + 1 < N; i++) if (fabs(g_ref[i + 1] - g_ref[i]) / g_gaps[i] > steepest) steepest = fabs(g_ref[i + 1] - g_ref[i]) / g_gaps[i];
        if (run < 2) CHECK(steepest <= SLOPE + 1.0e-6 && g_out[0] <= 5.001);
        if (run == 1) CHECK(g_target[40] == g_ground[40] && g_target[150] == g_ground[150]);
    }
}

/* ---- the field ---- */
#define WORLD 2048.0f
#define FAR_BACK -1.0e30f
static float g_road[2 * 512];
static float GroundFlat(float x, float y) { (void)x; (void)y; return 40.0f; }
static float GroundStep(float x, float y) { (void)y; return x < 1000.0f ? 0.0f : 20.0f; }
static float GroundTilt(float x, float y) { (void)y; return 0.05f * x; }
static uint32_t Line(float* out, float x0, float y0, float x1, float y1, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) { out[2 * k] = x0 + (x1 - x0) * (float)k / (float)(n - 1); out[2 * k + 1] = y0 + (y1 - y0) * (float)k / (float)(n - 1); }
    return n;
}

static void Field(void)
{
    uint32_t torn = 0, counts[3], ends[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u}, links, unlinked, capped;
    float z = 0, d = 0, xy[2], band;
    g_case = "a road on a gentle slope: its own height on the line, full weight to 12 m, none past its band";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundTilt, FAR_BACK, SLOPE, 4, WORLD, &torn) == 201 && torn == 0);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 200 && unlinked == 0 && capped == 0);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1000.0f, &z, &d) == 1.0f && fabsf(z - 50.15f) < 0.02f && d < 1.0e-3f);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1011.9f, &z, &d) == 1.0f && fabsf(z - 50.15f) < 0.02f && fabsf(d - 11.9f) < 1.0e-3f);
    CHECK(fabsf(CoXlTestGradeFieldAt(1003.0f, 1018.0f, &z, &d) - 0.5f) < 1.0e-3f && fabsf(z - 50.15f) < 0.02f); /* the least band: 12 m past the full weight */
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1024.5f, &z, &d) == 0.0f && fabsf(d - 24.5f) < 1.0e-3f);
    z = -7.0f;
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1400.0f, &z, &d) == 0.0f && z == -7.0f && d > 1.0e20f); /* no road listed: the height is left alone */
    CHECK(CoXlTestGradeFieldAt(-5.0f, 1000.0f, &z, &d) == 0.0f && CoXlTestGradeFieldAt(1003.0f, 4000.0f, &z, &d) == 0.0f);
    CoXlTestGradeStation(100, xy, &band, &links);
    CHECK(xy[0] == 1000.0f && band == 24.0f && links == 2);

    g_case = "a cut widens the band: the eased side slope stays under 25 degrees";
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundStep, FAR_BACK, SLOPE, 0, WORLD, &torn);
    const float target = CoXlTestGradeStation(101, xy, &band, &links);
    const float moved = fabsf(target - GroundStep(xy[0], xy[1]));
    CHECK(moved > 9.0f && fabsf(band - (12.0f + 1.5f * moved / 0.4663077f)) < 0.01f);
    CHECK(1.5f * moved / (band - 12.0f) <= 0.4663077f + 1.0e-4f);
    CHECK(CoXlTestGradeFieldAt(xy[0], 1000.0f + band - 0.5f, &z, &d) > 0.0f && CoXlTestGradeFieldAt(xy[0], 1000.0f + band + 0.5f, &z, &d) == 0.0f);

    g_case = "a floor: the road stays above it, and without one the ground is kept";
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundFlat, 45.0f, SLOPE, 4, WORLD, &torn);
    CHECK(CoXlTestGradeStation(50, xy, &band, &links) == 45.0f && torn == 0);
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundFlat, FAR_BACK, SLOPE, 4, WORLD, &torn);
    CHECK(CoXlTestGradeStation(50, xy, &band, &links) == 40.0f);

    g_case = "two roads on one node share its station; a spur parts the piece it meets, or takes its end";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1000.0f, 1000.0f, 101);
    counts[1] = Line(g_road + 202, 1000.3f, 1000.2f, 1000.0f, 1800.0f, 101); /* its first point half a metre off the node */
    counts[2] = Line(g_road + 404, 604.0f, 600.0f, 604.0f, 1000.0f, 41);     /* meets the first road between two points */
    ends[1] = 7; ends[2] = 7;
    CHECK(CoXlTestGradeField(g_road, counts, ends, 3, 1, GroundStep, FAR_BACK, SLOPE, 4, WORLD, &torn) == 101 + 100 + 40 + 1 && torn == 0);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 100 + 100 + 40 + 1 && unlinked == 0);
    CoXlTestGradeStation(100, xy, &band, &links);
    CHECK(xy[0] == 1000.0f && xy[1] == 1000.0f && links == 2); /* the node: one station for both roads */
    const float meet = CoXlTestGradeStation(201, xy, &band, &links);
    CHECK(xy[0] == 604.0f && xy[1] == 1000.0f && links == 3); /* the spur's meeting point, made first */
    CHECK(CoXlTestGradeFieldAt(604.0f, 1000.0f, &z, &d) == 1.0f && fabsf(z - meet) < 1.0e-3f);
    /* beside the meeting point both roads count, by nearness: the height stays within the two roads' own there */
    CHECK(CoXlTestGradeFieldAt(610.0f, 994.0f, &z, &d) == 1.0f && fabsf(z - meet) < SLOPE * 12.0f);
    counts[2] = Line(g_road + 404, 600.5f, 600.0f, 600.5f, 1000.0f, 41); /* half a metre from a point of the first road */
    CHECK(CoXlTestGradeField(g_road, counts, ends, 3, 1, GroundStep, FAR_BACK, SLOPE, 4, WORLD, &torn) == 101 + 100 + 40);
    CoXlTestGradeStation(50, xy, &band, &links);
    CHECK(xy[0] == 600.0f && links == 3);
}

static float PlaneAlong(void* c, float x, float y) { (void)c; (void)y; return 0.1f * x; }
static float PlaneAcross(void* c, float x, float y) { (void)c; (void)x; return 0.05f * y; }
static float PlaneGap(void* c, float x, float y) { (void)c; (void)y; return x > 500.0f && x < 520.0f ? NAN : x < 510.0f ? 0.0f : 300.0f; }
static void Slopes(void)
{
    double out[9];
    g_case = "a road's slopes: a 10 m baseline, samples every 2 m, the cross slope over 6 m";
    const uint32_t n = Line(g_road, 100.0f, 300.0f, 1100.0f, 300.0f, 126); /* 8 m a pair */
    CoXlTestGradeRoad(PlaneAlong, g_road, n, out);
    CHECK(fabs(out[0] - 1000.0) < 0.01 && fabs(out[1] - 992.0) < 0.01 /* the first five samples have no baseline behind them */ && out[2] == 0.0 && out[3] == 0.0 && out[4] == 0.0);
    CHECK(fabs(out[5] - 100.0) < 0.05 && fabs(out[6] - 5.7106) < 0.01 && out[7] == 5.75 && out[8] < 1.0e-3);
    CoXlTestGradeRoad(PlaneAcross, g_road, n, out);
    CHECK(out[6] == 0.0 && out[1] == 0.0 && fabs(out[8] - 2.8624) < 0.01 && out[5] == 0.0);
    g_case = "a height that is not there ends the run: no grade spans the gap";
    CoXlTestGradeRoad(PlaneGap, g_road, n, out);
    CHECK(out[6] == 0.0 && out[1] == 0.0 && out[0] > 950.0 && out[0] < 985.0);
}

static uint32_t g_tris, g_twice, g_bad, g_full, g_rim;
static uint8_t g_seen[512 * 512];
static float g_px;
static void Tri(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material, const float* uv0,
    const float* uv1, const float* uv2)
{
    const float* p[3] = {p0, p1, p2}; const float* uv[3] = {uv0, uv1, uv2};
    g_bad += gui != 1 || material != 2 || layer != 0 || colour[0] != 255.0f;
    /* a cell is its lowest corner; its first triangle starts there */
    if (g_tris++ % 2 == 0)
    {
        const uint32_t c = (uint32_t)(p0[0] / g_px / 4.0f + 0.5f), r = (uint32_t)(p0[1] / g_px / 4.0f + 0.5f);
        if (c >= 512 || r >= 512 || g_seen[r * 512 + c]++) g_twice++;
        g_bad += p1[0] <= p0[0] || p2[1] <= p0[1];
    }
    for (int k = 0; k < 3; k++)
    {
        const float x = p[k][0] / g_px, y = p[k][1] / g_px, w = uv[k][0];
        float z = 0, d = 0;
        const float field = CoXlTestGradeFieldAt(x, y, &z, &d);
        const float away = sqrtf((x - 1024.0f) * (x - 1024.0f) + (y - 1024.0f) * (y - 1024.0f));
        const float rim = away >= 700.0f ? 0.0f : away <= 636.0f ? 1.0f : (700.0f - away) / 64.0f;
        g_bad += !(w >= 0.0f && w <= 1.0f) || fabsf(w - field * rim) > 1.0e-4f || uv[k][1] != 0.0f || !isfinite(p[k][2]);
        if (w > 0.0f) g_bad += fabsf(p[k][2] - z) > 1.0e-3f;
        if (d <= 12.0f && rim == 1.0f) { g_full++; g_bad += w != 1.0f; }
        if (away >= 700.0f) { g_rim++; g_bad += w != 0.0f; }
    }
}
static void Draw(void)
{
    uint32_t torn = 0, counts[2], ends[4] = {~0u, 3, 3, ~0u}, cells = 0;
    g_case = "the level's draw: every lattice cell once, the weight and the height the field's, none past the rim";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1000.0f, 1000.0f, 101);
    counts[1] = Line(g_road + 202, 1000.0f, 1000.0f, 1000.0f, 1800.0f, 101);
    CoXlTestGradeField(g_road, counts, ends, 2, 0, GroundStep, FAR_BACK, SLOPE, 4, WORLD, &torn);
    g_px = 0.25f;
    const uint32_t given = CoXlTestGradeDraw(WORLD, 700.0f, Tri, g_px, 4096, &cells);
    CHECK(given == g_tris && given == 2 * cells && cells > 2000 && cells < 512 * 512 / 8);
    CHECK(g_twice == 0 && g_bad == 0 && g_full > 1000 && g_rim > 0);
    /* a batch smaller than the work gives the same cells */
    const uint32_t first = g_tris;
    memset(g_seen, 0, sizeof g_seen);
    CHECK(CoXlTestGradeDraw(WORLD, 700.0f, Tri, g_px, 2, &cells) == first && g_twice == 0 && g_bad == 0);
}

int main(void)
{
    Flat();
    Cliff();
    Pads();
    Junction();
    Crest();
    Reference();
    Field();
    Slopes();
    Draw();
    if (g_failed) { printf("roadgrade: %d check(s) failed\n", g_failed); return 1; }
    printf("roadgrade: all checks passed\n");
    return 0;
}
