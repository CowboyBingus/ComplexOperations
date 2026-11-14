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
float CoXlTestGradeBandRamp(float move, float cross);
float CoXlTestGradeBandWeight(float u, float ramp, float move, float cross);
void CoXlTestGradePads(const float* pads, uint32_t n);
float CoXlTestGradeFree(float world, float playable, float x, float y, int* pinned);
uint32_t CoXlTestGradeTies(uint32_t* untied);
float CoXlTestGradeSharpen(float world, float playable, float wx, float wy, float coarse);
int CoXlTestGradeRestart(int graded);
uint32_t CoXlTestGradeDraw(float world, float playable, TriFn tri, float px, uint32_t most, uint32_t* cells);
uint32_t CoXlTestGradeCapped(const float* ground, const uint8_t* pinned, const float* earth, uint32_t n, const uint32_t* links, const float* gaps, uint32_t count,
    float soft, float hard, uint32_t passes, float by, float* target, double out[4]);
void CoXlTestGradeZones(const float* boxes, uint32_t n);
int CoXlTestGradeLevel(const float* pts, uint32_t n, float (*ground)(float, float), float world, float playable, double out[6]);
float CoXlTestGradeStationCap(uint32_t i, int* pinned);
float CoXlTestGradeMove(float x, float y);
void CoXlTestGradeShoulder(float (*height)(void*, float, float), const float* pts, uint32_t n, double out[6]);
int CoXlTestGradeInPlace(float world, float playable, float x, float y, float* open);
int CoXlTestGradeStationOn(uint32_t i);
void CoXlTestGradeRect(float cx, float cy, float a, float b, float ba, float bb);
void CoXlTestGradeMeasures(float (*height)(void*, float, float), float (*base)(void*, float, float), const float* pts, uint32_t n, double out[8]);
float CoXlTestGradeDetail(float world, float playable, float wx, float wy);
float CoXlTestNearFine(const float* heights, int side, int c, int r);

int CoXlTestGradeRoads(const float* pts, const uint32_t* counts, const uint32_t* ends, uint32_t chains, int spur, float (*ground)(float, float), float world,
    float playable, double out[6]);
int CoXlTestGradeStationPin(uint32_t i, float* xy, float* target, float* ground);
float CoXlTestGradeStationCross(uint32_t i);

static int g_failed, g_checked;
static const char* g_case = "";
#define CHECK(what) do { g_checked++; if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
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
    g_case = "two pads the road cannot join within the limit are counted; the road keeps the limit and leaves each by half the lack";
    memset(g_pinned, 0, sizeof g_pinned);
    g_pinned[N / 2 - 1] = g_pinned[N / 2 + 2] = 1;
    torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, 0, g_target, g_out);
    CHECK(torn >= 4);
    const float lack = 20.0f - SLOPE * 3 * STEP;
    CHECK(fabsf(g_target[N / 2 - 1] - lack / 2) < 1.0e-3f && fabsf(g_target[N / 2 + 2] - (20.0f - lack / 2)) < 1.0e-3f);
    CHECK(g_out[0] <= 5.001);
    for (uint32_t i = 0; i + 1 < N; i++) CHECK(fabsf(g_target[i + 1] - g_target[i]) <= SLOPE * STEP + 1.0e-4f);
}

/* Run 1's cliffs beside pins: two pads 312 m apart on a chain. 27 m apart in height the road joins them; at 40 and
 * 60 m it cannot, and it must still hold the limit on every link, pinned stations too, missing each pad alike. */
static void PinsApart(void)
{
    static const float rise[3] = {27.0f, 40.0f, 60.0f};
    for (int run = 0; run < 3; run++)
    {
        g_case = run == 0 ? "pins 312 m and 27 m apart: joined" : run == 1 ? "pins 312 m and 40 m apart: no step" : "pins 312 m and 60 m apart: no step";
        memset(g_pinned, 0, sizeof g_pinned);
        for (uint32_t i = 0; i < N; i++) g_ground[i] = i < 100 ? 0.0f : rise[run];
        g_pinned[40] = g_pinned[118] = 1; /* 78 links of 4 m */
        for (uint32_t passes = 0; passes <= 16; passes += 16)
        {
            const uint32_t torn = CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Chain(N), SLOPE, passes, g_target, g_out);
            const float lack = rise[run] - SLOPE * 312.0f;
            CHECK((run == 0) == (torn == 0));
            CHECK(g_out[0] <= 5.001);
            for (uint32_t i = 0; i + 1 < N; i++) CHECK(fabsf(g_target[i + 1] - g_target[i]) <= SLOPE * STEP + 1.0e-4f);
            if (run == 0) CHECK(g_target[40] == 0.0f && g_target[118] == rise[run]);
            else CHECK(fabsf(g_target[40] - lack / 2) < 2.0e-3f && fabsf(g_target[118] - (rise[run] - lack / 2)) < 2.0e-3f);
        }
    }
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
    CHECK(fabs(g_out[4] - 180 * STEP) < 1.0e-2);    g_case = "a bend is measured where two links meet, not at a junction";
    for (uint32_t i = 0; i < 180; i++) g_ground[i] = i < 120 ? 0.05f * STEP * (float)(60 - i % 60) : 0.0f; /* two arms fall to the junction, one is level */
    g_ground[180] = 0.0f;
    CoXlTestGradeSolve(g_ground, g_pinned, 181, g_links, g_gaps, links, SLOPE, 0, g_target, g_out);
    CHECK(g_out[1] < 1.0e-3 && fabs(g_out[0] - 2.8624) < 0.01);
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
/* What link k (station k to k + 1) may climb over the common limit (0: nothing more), and the way between two
 * stations at each link's own limit. */
static double g_lim[N], g_cum[N];
static double RefReach(uint32_t n, double slope, uint32_t i, uint32_t j)
{
    (void)n;
    double sum = 0.0;
    for (uint32_t k = i < j ? i : j; k < (i < j ? j : i); k++) sum += (g_lim[k] > slope ? g_lim[k] : slope) * g_gaps[k];
    (void)g_cum;
    return sum;
}
static void RefWithin(uint32_t n, double slope)
{
    static double under[N], over[N];
    for (uint32_t i = 0; i < n; i++)
    {
        under[i] = over[i] = g_ref[i];
        for (uint32_t j = 0; j < n; j++)
        {
            const double reach = RefReach(n, slope, i, j);
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
        if (g_low[i] > g_high[i]) g_ref[i] = 0.5 * (g_low[i] + g_high[i]); /* pins that disagree: their middle, pinned or not */
        else if (g_ref[i] < g_low[i]) g_ref[i] = g_low[i];
        else if (g_ref[i] > g_high[i]) g_ref[i] = g_high[i];
    }
}
static float g_earth[N + 8];
static void RefCap(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        const double low = (double)g_ground[i] - g_earth[i], high = (double)g_ground[i] + g_earth[i];
        g_ref[i] = g_ref[i] < low ? low : g_ref[i] > high ? high : g_ref[i];
    }
}
/* From g_ref as the start: the pins' range, the limit, the easing; capped: each eased profile back into the cap
 * before the limit. */
static uint32_t RefFrom(uint32_t n, double slope, uint32_t passes, int capped)
{
    static double next[N];
    uint32_t torn = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        g_low[i] = -1.0e30; g_high[i] = 1.0e30;
        for (uint32_t j = 0; j < n; j++)
        {
            if (!g_pinned[j]) continue;
            const double reach = RefReach(n, slope, i, j);
            if (g_ground[j] - reach > g_low[i]) g_low[i] = g_ground[j] - reach;
            if (g_ground[j] + reach < g_high[i]) g_high[i] = g_ground[j] + reach;
        }
        torn += g_low[i] > g_high[i] + 1.0e-4;
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
        if (capped) RefCap(n);
        RefWithin(n, slope); RefClamp(n);
    }
    return torn;
}
static void RefStart(uint32_t n)
{
    g_at[0] = 0.0;
    for (uint32_t i = 1; i < n; i++) g_at[i] = g_at[i - 1] + g_gaps[i - 1];
    for (uint32_t i = 0; i < n; i++) g_ref[i] = g_ground[i];
}
static uint32_t RefSolve(uint32_t n, double slope, uint32_t passes) { RefStart(n); return RefFrom(n, slope, passes, 0); }
/* The worn path by its definition: the gentle profile without pins, pulled to within each station's cap of the
 * ground, then the hard limit from there, the eased profile pulled back into the cap before the limit each pass. */
static uint32_t RefStage(uint32_t n, double soft, double hard, uint32_t passes)
{
    static double keep[N];
    RefStart(n);
    memcpy(keep, g_lim, sizeof keep); memset(g_lim, 0, sizeof g_lim); /* the gentle profile knows no link's own limit */
    RefWithin(n, soft);
    memcpy(g_lim, keep, sizeof keep);
    RefCap(n);
    return RefFrom(n, hard, passes, 1);
}
/* With steep > hard: solved twice. After the first solve a link may climb more as the worse of its ends stands
 * further than `cap` off its ground (eased, whole at twice the cap). A link between two pins has no limit. */
static uint32_t RefSteep(uint32_t n, double soft, double hard, double steep, double cap, uint32_t passes)
{
    for (uint32_t k = 0; k + 1 < n; k++) g_lim[k] = g_pinned[k] && g_pinned[k + 1] ? 1.0e6 : 0.0;
    uint32_t torn = RefStage(n, soft, hard, passes);
    if (steep > hard)
    {
        for (uint32_t k = 0; k + 1 < n; k++)
        {
            if (g_pinned[k] && g_pinned[k + 1]) continue;
            const double a = g_pinned[k] ? 0.0 : fabs(g_ref[k] - g_ground[k]) - cap, b = g_pinned[k + 1] ? 0.0 : fabs(g_ref[k + 1] - g_ground[k + 1]) - cap;
            double w = (a > b ? a : b) / cap;
            w = w < 0.0 ? 0.0 : w > 1.0 ? 1.0 : w;
            g_lim[k] = hard + (steep - hard) * w * w * (3.0 - 2.0 * w);
        }
        torn = RefStage(n, soft, hard, passes);
    }
    memset(g_lim, 0, sizeof g_lim);
    return torn;
}
static uint32_t RefCapped(uint32_t n, double soft, double hard, uint32_t passes) { return RefSteep(n, soft, hard, 0.0, 4.0, passes); }
/* GradeOverCap by its definition on a chain's g_ref: free stations more than their cap and `by` from the ground. */
static uint32_t RefOverCap(uint32_t n, double by, double* worst, uint32_t* at, double* metres)
{
    uint32_t count = 0;
    *worst = 0.0; *at = 0; *metres = 0.0;
    for (uint32_t i = 0; i < n; i++)
    {
        const double past = fabs(g_ref[i] - g_ground[i]) - g_earth[i];
        if (g_pinned[i] || !(past > by)) continue;
        count++;
        *metres += 0.5 * ((i > 0 ? g_gaps[i - 1] : 0.0) + (i + 1 < n ? g_gaps[i] : 0.0));
        if (past > *worst) { *worst = past; *at = i; }
    }
    return count;
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
        CHECK(steepest <= SLOPE + 1.0e-6 && g_out[0] <= 5.001);
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
static float GroundRise(float x, float y) { (void)x; return 0.05f * y; }
static uint32_t Line(float* out, float x0, float y0, float x1, float y1, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) { out[2 * k] = x0 + (x1 - x0) * (float)k / (float)(n - 1); out[2 * k + 1] = y0 + (y1 - y0) * (float)k / (float)(n - 1); }
    return n;
}

static void Field(void)
{
    uint32_t torn = 0, counts[3], ends[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u}, links, unlinked, capped;
    float z = 0, d = 0, xy[2], band;
    g_case = "a road on a gentle slope: its own height on the line, full weight to 10 m, none past its band";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundTilt, FAR_BACK, SLOPE, 4, WORLD, &torn) == 201 && torn == 0);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 200 && unlinked == 0 && capped == 0);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1000.0f, &z, &d) == 1.0f && fabsf(z - 50.15f) < 0.02f && d < 1.0e-3f);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1009.9f, &z, &d) == 1.0f && fabsf(z - 50.15f) < 0.02f && fabsf(d - 9.9f) < 1.0e-3f);
    CHECK(fabsf(CoXlTestGradeFieldAt(1003.0f, 1022.0f, &z, &d) - 0.5f) < 1.0e-3f && fabsf(z - 50.15f) < 0.02f); /* the least band: 24 m past the full weight */
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1010.1f, &z, &d) < 1.0f && CoXlTestGradeFieldAt(1003.0f, 1033.5f, &z, &d) > 0.0f);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1034.5f, &z, &d) == 0.0f && fabsf(d - 34.5f) < 1.0e-3f);
    z = -7.0f;
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1400.0f, &z, &d) == 0.0f && z == -7.0f && d > 1.0e20f); /* no road listed: the height is left alone */
    CHECK(CoXlTestGradeFieldAt(-5.0f, 1000.0f, &z, &d) == 0.0f && CoXlTestGradeFieldAt(1003.0f, 4000.0f, &z, &d) == 0.0f);
    CoXlTestGradeStation(100, xy, &band, &links);
    CHECK(xy[0] == 1000.0f && band == 34.0f && links == 2);

    g_case = "a cut widens the band: the eased side slope of the move stays under 12 degrees";
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundStep, FAR_BACK, SLOPE, 0, WORLD, &torn);
    /* the two stations at the step move the most and alike, so the band is widest and even on the piece between
     * them: its middle is where no neighbour's wider band reaches past this one's edge */
    const float target = CoXlTestGradeStation(100, xy, &band, &links);
    const float moved = fabsf(target - GroundStep(xy[0], xy[1]));
    CHECK(moved > 9.0f && fabsf(band - (10.0f + 1.5f * moved / 0.2125566f)) < 0.01f);
    CHECK(1.5f * moved / (band - 10.0f) <= 0.2125566f + 1.0e-4f);
    CHECK(CoXlTestGradeFieldAt(xy[0] - 4.0f, 1000.0f + band - 0.5f, &z, &d) > 0.0f && CoXlTestGradeFieldAt(xy[0] - 4.0f, 1000.0f + band + 0.5f, &z, &d) == 0.0f);

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
    g_case = "on a road's line its own height holds, though another road's band reaches there";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    counts[1] = Line(g_road + 402, 1000.0f, 600.0f, 1000.0f, 1000.0f, 41);
    ends[1] = ends[2] = ~0u;
    CHECK(CoXlTestGradeField(g_road, counts, ends, 2, 1, GroundTilt, FAR_BACK, SLOPE, 0, WORLD, &torn) == 201 + 40);
    CHECK(fabsf(CoXlTestGradeFieldAt(1000.0f, 984.0f, &z, &d) - 1.0f) < 1.0e-6f && fabsf(z - 50.0f) < 0.02f);
    CHECK(CoXlTestGradeFieldAt(1016.0f, 1000.0f, &z, &d) == 1.0f && fabsf(z - 50.8f) < 0.02f); /* 16 m from the spur, inside its band */
    g_case = "a road whose band ends short of a point has no share of its height, though the cell lists it";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    counts[1] = Line(g_road + 402, 200.0f, 1060.0f, 1800.0f, 1060.0f, 201);
    CHECK(CoXlTestGradeField(g_road, counts, ends, 2, 0, GroundRise, FAR_BACK, SLOPE, 0, WORLD, &torn) == 402);
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1036.0f, &z, &d) > 0.2f && fabsf(z - 53.0f) < 1.0e-4f && fabsf(d - 24.0f) < 1.0e-3f); /* 36 m from the first, 24 from the second */
    CHECK(CoXlTestGradeFieldAt(1003.0f, 1030.0f, &z, &d) > 0.0f && z > 50.0f && z < 53.0f);                               /* both reach: between their heights */
    g_case = "on a road's line its own height holds, though another road's band reaches there";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1000.0f, 1000.0f, 101);
    counts[1] = Line(g_road + 202, 1000.3f, 1000.2f, 1000.0f, 1800.0f, 101);
    counts[2] = Line(g_road + 404, 604.0f, 600.0f, 604.0f, 1000.0f, 41);
    ends[1] = 7; ends[2] = 7;
    g_road[2 * 40] = g_road[2 * 39] + 0.4f; /* a point 0.4 m after the one before it is that station */
    CHECK(CoXlTestGradeField(g_road, counts, ends, 3, 1, GroundStep, FAR_BACK, SLOPE, 4, WORLD, &torn) == 101 + 100 + 40 && torn == 0);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 100 + 100 + 40 && unlinked == 0);
    g_road[2 * 40] = 520.0f;
    counts[2] = Line(g_road + 404, 600.5f, 600.0f, 600.5f, 1000.0f, 41); /* half a metre from a point of the first road */
    CHECK(CoXlTestGradeField(g_road, counts, ends, 3, 1, GroundStep, FAR_BACK, SLOPE, 4, WORLD, &torn) == 101 + 100 + 40);
    CoXlTestGradeStation(50, xy, &band, &links);
    CHECK(xy[0] == 600.0f && links == 3);
}

static float PlaneAlong(void* c, float x, float y) { (void)c; (void)y; return 0.1f * x; }
static float PlaneAcross(void* c, float x, float y) { (void)c; (void)x; return 0.05f * y; }
static float PlaneBent(void* c, float x, float y) { (void)c; (void)y; return x < 1000.0f ? 0.0f : 0.3f * (x - 1000.0f); }
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
    CoXlTestGradeRoad(PlaneBent, g_road, n, out); /* nine tenths level, the last tenth at 0.3: 95 of 100 samples lie under that grade only */
    CHECK(fabs(out[6] - 16.699) < 0.01 && out[7] == 16.75 && fabs(out[1] - 98.0) < 0.01 && fabs(out[2] - 96.0) < 0.01 && fabs(out[3] - 94.0) < 0.01 && out[4] == 0.0);
    g_case = "a height that is not there ends the run: no grade spans the gap";
    CoXlTestGradeRoad(PlaneGap, g_road, n, out);
    CHECK(out[6] == 0.0 && out[1] == 0.0 && out[0] > 950.0 && out[0] < 985.0);
}

static uint32_t g_tris, g_twice, g_bad, g_full, g_rim;
static float g_cellMost;
static uint8_t g_seen[512 * 512];
static float g_px;
static void Tri(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material, const float* uv0,
    const float* uv1, const float* uv2)
{
    const float* p[3] = {p0, p1, p2}; const float* uv[3] = {uv0, uv1, uv2};
    g_bad += gui != 1 || material != 2 || layer != 0 || colour[0] != 255.0f;
    /* a cell is its lowest corner; its first triangle starts there */
    const int second = g_tris % 2;
    if (!second) g_cellMost = 0.0f;
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
        /* a corner without a share takes a height from the cell's corners that have one: among the roads' own (0 to
         * 20 m here; at a band's end a far piece's height may be the only one with weight, so not the nearest's) */
        for (int q = 0; q < 3 && !(w > 0.0f); q++) if (uv[q][0] > 0.0f) g_bad += fabsf(p[k][2] - p[q][2]) > 25.0f;
        if (d <= 10.0f && rim == 1.0f) { g_full++; g_bad += w != 1.0f; }
        if (away >= 700.0f) { g_rim++; g_bad += w != 0.0f; }
        if (w > g_cellMost) g_cellMost = w;
    }
    if (second) g_bad += !(g_cellMost > 0.0f); /* a cell without weight is not drawn */
}
/* Two changes to the draw no case tells apart: walking every lattice cell instead of the marked ones gives the same
 * cells (the marks cover every band; they only save time), and the height given to a corner no road is listed at
 * is never used here, since a corner of a drawn cell is within a cell's diagonal of a band and so always listed. */
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

/* Only a pad's flat ground pins a road. The road is whole up to its first station on the flat part and gone from the
 * next one on, where every station stands at the pad's height. A stamp's footprint and a weak pad do neither. The
 * rim pins where the share falls. */
static void Pins(void)
{
    const float world = 2048.0f, playable = 696.0f, mid = 1024.0f;
    float pad[6] = {0.0f, 0.0f, 20.0f, 40.0f, 1.0f, 0.0f};
    double out[6]; int pinned = -1;
    const uint32_t n = Line(g_road, 200.0f, mid, 1792.0f, mid, N); /* a station every 8 m, 103 at the pad's centre */
    g_case = "a flatten pad: no road where its stations and their neighbours stand on the flat part, the whole road up to the first of them";
    CoXlTestGradePads(pad, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundFlat, world, playable, out) == 0);
    for (uint32_t i = 98; i <= 108; i++) CHECK(CoXlTestGradeStationOn(i) == (i >= 102 && i <= 104 ? 3 : i >= 101 && i <= 105 ? 1 : 0));
    CHECK(CoXlTestGradeFree(world, playable, mid, mid, &pinned) < 1.0e-6f && pinned == 1);
    CHECK(CoXlTestGradeFree(world, playable, mid + 8.0f, mid + 6.0f, &pinned) < 0.02f && pinned == 1); /* beside the line the pieces further out count a little */
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid + 12.0f, mid, &pinned) - 0.5f) < 1.0e-3f && pinned == 1); /* halfway to the last station on the flat part */
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid + 16.0f, mid, &pinned) - 1.0f) < 1.0e-4f && pinned == 1);
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid + 22.0f, mid, &pinned) - 1.0f) < 1.0e-3f && pinned == 1); /* weight 0.99275: the road, to its pin */
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid + 40.0f, mid, &pinned) - 1.0f) < 1.0e-4f && pinned == 0);
    CHECK(CoXlTestGradeFree(world, playable, mid + 61.0f, mid, &pinned) == 1.0f && pinned == 0);
    g_case = "a stamp's footprint: the road is whole on it and no station is pinned";
    pad[5] = 1.0f;
    CoXlTestGradePads(pad, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundFlat, world, playable, out) == 0);
    CHECK(CoXlTestGradeFree(world, playable, mid + 10.0f, mid, &pinned) == 1.0f && pinned == 0 && CoXlTestGradeStationOn(103) == 0);
    g_case = "a weak pad never pins; the rim does";
    pad[4] = 0.4f; pad[5] = 0.0f;
    CoXlTestGradePads(pad, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundFlat, world, playable, out) == 0);
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid, mid, &pinned) - 1.0f) < 1.0e-4f && pinned == 0 && CoXlTestGradeStationOn(103) == 0);
    CHECK(fabsf(CoXlTestGradeFree(world, playable, mid + playable - 32.0f, mid, &pinned) - 0.5f) < 1.0e-4f && pinned == 1);
    CHECK(CoXlTestGradeFree(world, playable, mid + playable, mid, &pinned) == 0.0f && pinned == 1);
    CHECK(CoXlTestGradeFree(world, playable, mid + playable - 65.0f, mid, &pinned) == 1.0f && pinned == 0);
    CoXlTestGradePads(pad, 0);
}

/* F3: the shoulder on a side-hill. The blend adds at most tan 12 degrees to the ground's own side slope, uphill and
 * downhill, with no crease at the bed's edge or the band's end; past what the widest band can hold it is counted. */
static float g_tilt = 0.4f, g_rise = 0.0f;
static float GroundSide(float x, float y) { return g_tilt * y + g_rise * x; }
static float Ease(float w) { return w * w * (3.0f - 2.0f * w); }
static float Blended(float y, float* w)
{
    float z = 0, d = 0;
    *w = CoXlTestGradeFieldAt(1003.0f, y, &z, &d);
    return GroundSide(1003.0f, y) + (*w > 0.0f ? Ease(*w) * (z - GroundSide(1003.0f, y)) : 0.0f);
}
/* The road along y = 1000 at `height`: out[side] the most the blend adds to the ground's side slope past the bed
 * (side 0: y below the road), out[2 + side] the sharpest change of side slope between 4 m spans. */
static void Shoulders(float height, float band, float out[4])
{
    float w;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    for (float e = 0.0f; e < band + 12.0f; e += 0.25f)
        for (int side = 0; side < 2; side++)
        {
            const float y = 1000.0f + (side ? e : -e), step = side ? 0.25f : -0.25f, span = side ? 4.0f : -4.0f;
            const float h0 = Blended(y, &w), full = w, h1 = Blended(y + step, &w);
            if (e <= 9.75f) CHECK(full == 1.0f && fabsf(h0 - height) < 0.02f); /* level across its full width */
            const float added = fabsf(h1 - h0) / 0.25f - g_tilt, bend = fabsf(Blended(y + 2.0f * span, &w) - 2.0f * Blended(y + span, &w) + h0) / 4.0f;
            if (e >= 10.0f && added > out[side]) out[side] = added;
            if (bend > out[2 + side]) out[2 + side] = bend;
        }
}
static void Banks(void)
{
    uint32_t torn = 0, counts[1], ends[2] = {~0u, ~0u}, links, unlinked, capped;
    float xy[2], band, out[4], w, before = 0.0f, z = 0, d = 0, bands[2], moves[2];
    g_case = "the ramp: 1.5 moves over tan 12 on level ground; across a slope the wider of what the side slope and the bend need; none from 32.5 degrees";
    /* bank = move + 10 cross; side 3 bank / (S + sqrt(S (S + cross)) - cross), bend (4 cross + sqrt(16 cross^2 + 24 B bank)) / 2B */
    CHECK(CoXlTestGradeBandRamp(0.0f, 0.0f) == 0.0f && fabsf(CoXlTestGradeBandRamp(4.0f, 0.0f) - 28.228f) < 0.01f);
    CHECK(fabsf(CoXlTestGradeBandRamp(0.0f, 0.1f) - 17.055f) < 0.01f && fabsf(CoXlTestGradeBandRamp(4.0f, 0.1f) - 40.507f) < 0.01f);
    CHECK(fabsf(CoXlTestGradeBandRamp(0.0f, 0.2f) - 27.904f) < 0.01f && fabsf(CoXlTestGradeBandRamp(1.0f, 0.3f) - 49.458f) < 0.01f);
    CHECK(fabsf(CoXlTestGradeBandRamp(0.0f, 0.4f) - 69.207f) < 0.02f && fabsf(CoXlTestGradeBandRamp(4.0f, 0.2f) - 58.312f) < 0.02f);
    CHECK(isinf(CoXlTestGradeBandRamp(0.0f, 0.7f)) && isinf(CoXlTestGradeBandRamp(20.0f, 3.0f)) && CoXlTestGradeBandRamp(0.0f, 0.63f) > 1000.0f); /* the field caps it */
    CHECK(CoXlTestGradeBandRamp(-1.0f, 0.0f) == 0.0f && CoXlTestGradeBandRamp(4.0f, -0.5f) == CoXlTestGradeBandRamp(4.0f, 0.0f) && CoXlTestGradeBandRamp(NAN, NAN) == 0.0f);
    CHECK(CoXlTestGradeBandRamp(-1.0f, 0.2f) == CoXlTestGradeBandRamp(0.0f, 0.2f) && CoXlTestGradeBandRamp(NAN, 0.2f) == CoXlTestGradeBandRamp(0.0f, 0.2f));

    g_case = "the weight before easing: straight on level ground whatever the move, held up across a slope, 1 and 0 at and past its ends";
    CHECK(fabsf(CoXlTestGradeBandWeight(0.5f, 28.228f, 4.0f, 0.0f) - 0.5f) < 1.0e-6f && fabsf(CoXlTestGradeBandWeight(0.2f, 24.0f, 0.0f, 0.0f) - 0.8f) < 1.0e-6f);
    /* eased: (bank (1 - u)^2 (1 + 2u) + cross ramp u (1 - u)^2) / (bank + cross ramp u): 0.66552, 0.30605, 0.10064 */
    CHECK(fabsf(CoXlTestGradeBandWeight(0.25f, 69.207f, 0.0f, 0.4f) - 0.61223f) < 1.0e-4f && fabsf(CoXlTestGradeBandWeight(0.5f, 69.207f, 0.0f, 0.4f) - 0.36761f) < 1.0e-4f);
    CHECK(fabsf(CoXlTestGradeBandWeight(0.75f, 58.312f, 4.0f, 0.2f) - 0.19648f) < 1.0e-4f);
    CHECK(CoXlTestGradeBandWeight(0.0f, 24.0f, 4.0f, 0.2f) == 1.0f && CoXlTestGradeBandWeight(-1.0f, 24.0f, 4.0f, 0.2f) == 1.0f);
    CHECK(CoXlTestGradeBandWeight(1.0f, 24.0f, 4.0f, 0.2f) == 0.0f && CoXlTestGradeBandWeight(2.0f, 24.0f, 4.0f, 0.2f) == 0.0f);
    /* a move or a cross slope that is below 0 or not a number counts as none */
    CHECK(fabsf(CoXlTestGradeBandWeight(0.5f, 24.0f, NAN, NAN) - 0.5f) < 1.0e-6f && fabsf(CoXlTestGradeBandWeight(0.5f, 69.207f, -3.0f, 0.4f) - 0.36761f) < 1.0e-4f);
    CHECK(fabsf(CoXlTestGradeBandWeight(0.5f, 69.207f, NAN, 0.4f) - 0.36761f) < 1.0e-4f && fabsf(CoXlTestGradeBandWeight(0.75f, 58.312f, 4.0f, -0.2f) - 0.25f) < 1.0e-6f);

    g_case = "a road along a side-hill of 21.8 degrees: a 69 m shoulder, the bank within tan 12 of the ground on both sides, no crease";
    g_tilt = 0.4f;
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundSide, FAR_BACK, SLOPE, 4, WORLD, &torn) == 201 && torn == 0);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 200 && capped == 0);
    CoXlTestGradeStation(100, xy, &band, &links);
    CHECK(fabsf(band - 79.207f) < 0.05f);
    Shoulders(400.0f, band, out);
    CHECK(out[0] <= 0.2126f + 0.004f && out[1] <= 0.2126f + 0.004f && out[0] > 0.205f && out[1] > 0.205f);
    CHECK(out[2] < 0.11f && out[3] < 0.11f && out[2] > 0.08f); /* the bed's edge bends 0.102 a 4 m span: under tan 10 degrees */
    /* no crease at either end: level just past the bed, the ground's own slope just inside the band's end, the ground itself beyond */
    CHECK(fabsf(Blended(1010.5f, &w) - Blended(1010.0f, &w)) / 0.5f < 0.02f && fabsf(Blended(990.0f, &w) - Blended(989.5f, &w)) / 0.5f < 0.02f);
    CHECK(fabsf((Blended(1000.0f + band, &w) - Blended(999.5f + band, &w)) / 0.5f - 0.4f) < 0.01f && fabsf((Blended(1000.5f - band, &w) - Blended(1000.0f - band, &w)) / 0.5f - 0.4f) < 0.01f);
    CHECK(Blended(1001.0f + band, &w) == GroundSide(1003.0f, 1001.0f + band) && w == 0.0f);
    /* the same hill with a straight weight over the same band, as on level ground: 0.69 of the cross slope on top, whatever the width */
    for (float e = 10.0f; e < band; e += 0.25f)
    {
        const float w0 = (band - e) / (band - 10.0f), w1 = (band - e - 0.25f) / (band - 10.0f);
        const float rise = 0.4f * (e + 0.25f) * (1.0f - Ease(w1 > 0.0f ? w1 : 0.0f)) - 0.4f * e * (1.0f - Ease(w0));
        if (rise / 0.25f - 0.4f > before) before = rise / 0.25f - 0.4f;
    }
    CHECK(before > 0.33f);

    g_case = "a 4 m fill on a side-hill of 11.3 degrees: the downhill bank, where the fill grows, holds tan 12; the uphill one is gentler";
    g_tilt = 0.2f;
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundSide, 204.0f, SLOPE, 4, WORLD, &torn) == 201);
    CHECK(CoXlTestGradeStation(100, xy, &band, &links) == 204.0f && fabsf(band - 68.312f) < 0.05f);
    Shoulders(204.0f, band, out);
    CHECK(out[0] <= 0.2126f + 0.004f && out[0] > 0.205f && out[1] < 0.09f && out[1] > 0.05f);
    CHECK(out[2] < 0.1f && out[3] < 0.1f);

    g_case = "between two stations the shoulder takes the move and the band between theirs";
    g_rise = 0.08f; /* the road climbs under a floor at 280 m: station 95 is raised 3.2 m, station 96 2.56 m */
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundSide, 280.0f, SLOPE, 4, WORLD, &torn) == 201);
    for (int k = 0; k < 2; k++) moves[k] = CoXlTestGradeStation(95 + (uint32_t)k, xy, &bands[k], &links) - GroundSide(xy[0], xy[1]);
    CHECK(moves[0] > moves[1] + 0.5f && moves[1] > 1.0f && bands[0] > bands[1] + 1.0f);
    {
        /* 30 m beside the middle of the piece from station 95: the most over the pieces around, each by its nearest
         * point's share of its two stations' bands and moves (a piece further back has the wider band) */
        const float here = CoXlTestGradeFieldAt(964.0f, 1030.0f, &z, &d);
        float want = 0.0f, mid_ = 0.0f;
        for (uint32_t i = 85; i < 105; i++)
        {
            float b2[2], m2[2], at[2][2];
            for (int k = 0; k < 2; k++) m2[k] = CoXlTestGradeStation(i + (uint32_t)k, at[k], &b2[k], &links) - GroundSide(at[k][0], at[k][1]);
            float t = (964.0f - at[0][0]) / (at[1][0] - at[0][0]);
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            const float px = at[0][0] + t * (at[1][0] - at[0][0]) - 964.0f, dist = sqrtf(px * px + 900.0f), ramp = b2[0] + t * (b2[1] - b2[0]) - 10.0f;
            const float one = dist - 10.0f >= ramp ? 0.0f : CoXlTestGradeBandWeight((dist - 10.0f) / ramp, ramp, m2[0] + t * (m2[1] - m2[0]), 0.2f);
            if (one > want) want = one;
            if (i == 95) mid_ = one;
        }
        CHECK(fabsf(d - 30.0f) < 1.0e-3f && fabsf(here - want) < 1.0e-4f && want > mid_ + 1.0e-3f && mid_ > 0.1f);
    }
    g_rise = 0.0f;

    g_case = "a side-hill of 35 degrees: no band holds tan 12; the widest is taken, every station counted, the bank 0.29 over the ground";
    g_tilt = 0.7f;
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundSide, FAR_BACK, SLOPE, 4, WORLD, &torn) == 201);
    CHECK(CoXlTestGradeFieldPieces(&unlinked, &capped) == 200 && capped == 201);
    CoXlTestGradeStation(100, xy, &band, &links);
    CHECK(band == 170.0f);
    Shoulders(700.0f, band, out);
    CHECK(out[0] > 0.28f && out[0] < 0.30f && out[1] > 0.28f && out[1] < 0.30f && out[2] < 0.1f && out[3] < 0.1f);
    g_tilt = 0.4f;
}

/* ---- roads side by side, a pad's band, a tile's core, a restart ---- */
static float GroundTwo(float x, float y) { (void)x; return y < 1002.0f ? 40.0f : 50.0f; }
static float g_probe[2][2], g_probeW[2], g_probeZ[2];
static void TriProbe(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material, const float* uv0,
    const float* uv1, const float* uv2)
{
    const float* p[3] = {p0, p1, p2}; const float* uv[3] = {uv0, uv1, uv2};
    (void)gui; (void)layer; (void)colour; (void)material;
    for (int k = 0; k < 3; k++)
        for (int q = 0; q < 2; q++)
            if (fabsf(p[k][0] / g_px - g_probe[q][0]) < 0.01f && fabsf(p[k][1] / g_px - g_probe[q][1]) < 0.01f) { g_probeW[q] = uv[k][0]; g_probeZ[q] = p[k][2]; }
}
static float FieldAtOf(void* c, float x, float y) { float z = NAN, d; (void)c; return CoXlTestGradeFieldAt(x, y, &z, &d) > 0.0f ? z : NAN; }
static void Together(void)
{
    uint32_t torn = 0, counts[2], ends[4] = {~0u, ~0u, ~0u, ~0u}, untied = 9, links, cells = 0;
    float xy[2], band;
    g_case = "two roads 4 m apart that share no station are tied: their heights agree within 3 degrees across the gap";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    counts[1] = Line(g_road + 402, 200.0f, 1004.0f, 1800.0f, 1004.0f, 201);
    CHECK(CoXlTestGradeField(g_road, counts, ends, 2, 0, GroundTwo, FAR_BACK, SLOPE, 4, WORLD, &torn) == 402 && torn == 0);
    CHECK(CoXlTestGradeTies(&untied) == 201 && untied == 0);
    for (uint32_t i = 0; i < 201; i += 20)
    {
        const float a = CoXlTestGradeStation(i, xy, &band, &links), b = CoXlTestGradeStation(201 + i, xy, &band, &links);
        CHECK(fabsf(a - b) <= SLOPE * 0.66f * 4.0f + 1.0e-3f && fabsf(0.5f * (a + b) - 45.0f) < 0.05f);
        CHECK(links == (i == 0 || i == 200 ? 2u : 3u));
    }
    g_case = "a road's own stations, one or two links apart, are not tied, however close";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 800.0f, 1000.0f, 201); /* 3 m apart: the next but one is 6 m away */
    CHECK(CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundTilt, FAR_BACK, SLOPE, 4, WORLD, &torn) == 201);
    CHECK(CoXlTestGradeTies(&untied) == 0 && untied == 0);

    g_case = "two roads 10 m apart on a side-hill of 11.3 degrees: tied to within 1 degree across, so neither bed is tilted by the other";
    {
        double out[6], m[8]; float low, high, g;
        counts[0] = Line(g_road, 200.0f, 1000.0f, 800.0f, 1000.0f, 101);
        counts[1] = Line(g_road + 202, 201.0f, 1010.0f, 801.0f, 1010.0f, 101);
        g_tilt = 0.2f; g_rise = 0.0f;
        CHECK(CoXlTestGradeRoads(g_road, counts, ends, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0);
        CHECK(CoXlTestGradeTies(&untied) == 101 && untied == 0);
        CoXlTestGradeStationPin(50, xy, &low, &g); CoXlTestGradeStationPin(151, xy, &high, &g);
        CHECK(high - low > 0.05f && high - low <= 0.0174551f * 10.05f + 1.0e-3f && fabsf(0.5f * (low + high) - 201.0f) < 0.1f);
        CoXlTestGradeMeasures(FieldAtOf, NULL, g_road, counts[0], m);
        printf("  10 m apart: %.2f m between them, %.2f degrees across the bed\n", (double)(high - low), m[5]);
        CHECK(m[5] <= 2.7);
        g_case = "16 m apart the tie is looser, 30 m apart there is none: each road keeps its own ground";
        counts[1] = Line(g_road + 202, 201.0f, 1016.0f, 801.0f, 1016.0f, 101);
        CHECK(CoXlTestGradeRoads(g_road, counts, ends, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0 && CoXlTestGradeTies(&untied) == 101);
        CoXlTestGradeStationPin(50, xy, &low, &g); CoXlTestGradeStationPin(151, xy, &high, &g);
        CHECK(high - low > 0.4f && high - low <= 0.0174551f * 16.04f * 2.05f + 1.0e-3f);
        CoXlTestGradeMeasures(FieldAtOf, NULL, g_road, counts[0], m);
        printf("  16 m apart: %.2f m between them, %.2f degrees across the bed\n", (double)(high - low), m[5]);
        CHECK(m[5] <= 2.7);
        counts[1] = Line(g_road + 202, 201.0f, 1030.0f, 801.0f, 1030.0f, 101);
        CHECK(CoXlTestGradeRoads(g_road, counts, ends, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0 && CoXlTestGradeTies(&untied) == 0);
        CoXlTestGradeStationPin(50, xy, &low, &g); CoXlTestGradeStationPin(151, xy, &high, &g);
        CHECK(fabsf(high - low - 6.0f) < 0.05f);
        g_case = "two roads that part at 20 degrees: tied where they run side by side, not at the node, and no bed is tilted over 4.5 degrees";
        const uint32_t fan[4] = {0, ~0u, 0, ~0u};
        counts[0] = Line(g_road, 400.0f, 1000.0f, 700.0f, 1000.0f, 51);
        counts[1] = Line(g_road + 102, 400.0f, 1000.0f, 400.0f + 300.0f * 0.9396926f, 1000.0f + 300.0f * 0.3420201f, 51);
        CHECK(CoXlTestGradeRoads(g_road, counts, fan, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0);
        const uint32_t tied = CoXlTestGradeTies(&untied);
        CHECK(tied >= 8 && tied <= 12 && untied == 0); /* to where they are 24 m apart, 69 m out */
        CoXlTestGradeMeasures(FieldAtOf, NULL, g_road, counts[0], m);
        printf("  a fan of 20 degrees: %u ties, %.2f degrees across the first bed", tied, m[5]);
        CHECK(m[5] <= 4.5);
        CoXlTestGradeMeasures(FieldAtOf, NULL, g_road + 102, counts[1], m);
        printf(", %.2f across the second\n", m[5]);
        CHECK(m[5] <= 4.5);
        g_case = "arms that meet at 90 degrees are not tied: the landing holds them";
        counts[1] = Line(g_road + 102, 400.0f, 1000.0f, 400.0f, 1300.0f, 51);
        CHECK(CoXlTestGradeRoads(g_road, counts, fan, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0 && CoXlTestGradeTies(&untied) == 0);
        g_case = "a tie points across the road: the ground's slope along two roads side by side is not their cross slope";
        g_tilt = 0.0f; g_rise = 0.05f;
        counts[0] = Line(g_road, 200.0f, 1000.0f, 800.0f, 1000.0f, 101);
        counts[1] = Line(g_road + 202, 201.0f, 1010.0f, 801.0f, 1010.0f, 101);
        CHECK(CoXlTestGradeRoads(g_road, counts, ends, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0 && CoXlTestGradeTies(&untied) == 101);
        CHECK(CoXlTestGradeStationCross(50) < 1.0e-4f && CoXlTestGradeStationCross(151) < 1.0e-4f);
        g_case = "a station on a pad is tied only within 8 m: a pad's height is not the next road's to choose";
        {
            const float pad[6] = {500.0f - 1024.0f, 1000.0f - 1024.0f, 2.0f, 1.0f, 1.0f, 0.0f};
            CoXlTestGradePads(pad, 1);
            CHECK(CoXlTestGradeRoads(g_road, counts, ends, 2, 0, GroundSide, WORLD, 4000.0f, out) >= 0 && CoXlTestGradeTies(&untied) == 100);
            CoXlTestGradePads(pad, 0);
        }
        g_tilt = 0.2f; g_rise = 0.0f;
        g_tilt = 0.4f;
    }

    g_case = "the draw at a pad: the whole road on its band (weight 0.78) and to its first station on the flat part, none at its centre";
    const float pad[6] = {0.0f, 0.0f, 20.0f, 40.0f, 1.0f, 0.0f};
    double level[6];
    counts[0] = Line(g_road, 200.0f, 1024.0f, 1800.0f, 1024.0f, 201);
    CoXlTestGradePads(pad, 1);
    CHECK(CoXlTestGradeLevel(g_road, counts[0], GroundTilt, WORLD, 1000.0f, level) == 0);
    g_px = 0.25f;
    g_probe[0][0] = 1056.0f; g_probe[0][1] = 1024.0f; g_probe[1][0] = 1024.0f; g_probe[1][1] = 1024.0f;
    g_probeW[0] = g_probeW[1] = -1.0f;
    CoXlTestGradeDraw(WORLD, 1000.0f, TriProbe, g_px, 4096, &cells);
    CHECK(fabsf(g_probeW[0] - 1.0f) < 1.0e-4f && fabsf(g_probeZ[0] - 0.05f * 1056.0f) < 0.05f);
    CHECK(g_probeW[1] >= 0.0f && g_probeW[1] < 1.0e-6f && isfinite(g_probeZ[1]));
    g_probe[0][0] = 1044.0f; g_probeW[0] = -1.0f; /* on the flat part, before the road's first station there */
    CoXlTestGradeDraw(WORLD, 1000.0f, TriProbe, g_px, 4096, &cells);
    CHECK(fabsf(g_probeW[0] - 1.0f) < 1.0e-4f);
    /* a near tile's core takes the road by the same share: half of it halfway between the two stations */
    CHECK(fabsf(CoXlTestGradeSharpen(WORLD, 1000.0f, 12.0f, 0.0f, 0.05f * 1036.0f + 0.3f) - (0.05f * 1036.0f + 0.3f - 0.5f * 0.784f * 0.3f)) < 2.0e-3f);
    CoXlTestGradePads(pad, 0);

    g_case = "a near tile's core takes the roads' height, but not where the level stands a metre off it (a stamp's terrain)";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundFlat, FAR_BACK, SLOPE, 4, WORLD, &torn);
    CHECK(fabsf(CoXlTestGradeSharpen(WORLD, 1000.0f, 0.0f, -24.0f, 40.3f) - (40.3f - 0.784f * 0.3f)) < 1.0e-3f);
    CHECK(CoXlTestGradeSharpen(WORLD, 1000.0f, 0.0f, -24.0f, 40.0f) == 40.0f);
    CHECK(CoXlTestGradeSharpen(WORLD, 1000.0f, 0.0f, -24.0f, 42.0f) == 42.0f);
    CHECK(CoXlTestGradeSharpen(WORLD, 1000.0f, 0.0f, -16.0f, 40.3f) == 40.3f); /* at the core's edge */

    g_case = "the merge beginning again on a graded level grades its roads again";
    CHECK(CoXlTestGradeRestart(1) == 1 && CoXlTestGradeRestart(0) == 0);
}

/* ---- the worn path: 4.5 degrees where that keeps the road within its cap of the ground, the hard limit everywhere ---- */
#define SOFT 0.0787017f /* tan 4.5 degrees */
#define HARD 0.1227846f /* tan 7 degrees */
uint32_t CoXlTestGradeSteep(const float* ground, const uint8_t* pinned, const float* earth, uint32_t n, const uint32_t* links, const float* gaps, uint32_t count,
    float soft, float hard, float steep, float cap, uint32_t passes, float* target, double out[7]);
float CoXlTestGradeFade(float world, float playable, float x, float y, float* cap);
static float g_soft[N + 8], g_hard[N + 8];
static double g_over[4];
static uint32_t Even(uint32_t n, float gap)
{
    for (uint32_t i = 0; i + 1 < n; i++) { g_links[2 * i] = i; g_links[2 * i + 1] = i + 1; g_gaps[i] = gap; }
    return n - 1;
}
/* The capped profile against its definition, every link within the hard limit, and GradeOverCap against its own:
 * the count and the metres between those of a threshold a centimetre to either side (floats against doubles), the
 * station it names the one furthest past its cap. */
static void CappedAgrees(uint32_t passes, float by)
{
    double worst, metres[2], far_ = 0.0; uint32_t at, count[2];
    const uint32_t torn = CoXlTestGradeCapped(g_ground, g_pinned, g_earth, N, g_links, g_gaps, N - 1, SOFT, HARD, passes, by, g_target, g_over);
    CHECK(torn == RefCapped(N, (double)SOFT, (double)HARD, passes));
    for (uint32_t i = 0; i < N; i++) if (fabs(g_target[i] - g_ref[i]) > far_) far_ = fabs(g_target[i] - g_ref[i]);
    CHECK(far_ < 5.0e-3);
    for (uint32_t i = 0; i + 1 < N; i++) CHECK(fabsf(g_target[i + 1] - g_target[i]) <= HARD * g_gaps[i] + 1.0e-4f);
    count[0] = RefOverCap(N, (double)by + 0.01, &worst, &at, &metres[0]);
    count[1] = RefOverCap(N, (double)by - 0.01, &worst, &at, &metres[1]);
    CHECK(g_over[0] >= count[0] && g_over[0] <= count[1] && g_over[3] >= metres[0] - 1.0e-3 && g_over[3] <= metres[1] + 1.0e-3);
    CHECK(fabs(g_over[1] - worst) < 5.0e-3);
    if (count[0] > 0)
    {
        const uint32_t named = (uint32_t)g_over[2];
        CHECK(named < N && !g_pinned[named] && fabs(fabsf(g_target[named] - g_ground[named]) - g_earth[named] - g_over[1]) < 1.0e-4);
    }
}

static void Capped(void)
{
    uint32_t same = 0, held = 0, gentle = 0;
    g_case = "a ridge no steeper than 7 degrees that 4.5 degrees would cut 10 m: the capped road stays within 4 m everywhere";
    memset(g_pinned, 0, sizeof g_pinned);
    /* 0.12 a metre up to 59.52 m at station 100, a station every 8 m; the 4.5 degree road cuts 59.52 (1 - 0.0787 / 0.12) / 2 */
    for (uint32_t i = 0; i < N; i++) { const float up = 0.12f * 8.0f * (62.0f - fabsf((float)i - 100.0f)); g_ground[i] = up > 0.0f ? up : 0.0f; g_earth[i] = 4.0f; }
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Even(N, 8.0f), SOFT, 0, g_soft, g_out);
    CHECK(fabs(g_out[2] - 10.242) < 0.01 && fabs(g_out[3] - 10.242) < 0.01);
    CHECK(CoXlTestGradeCapped(g_ground, g_pinned, g_earth, N, g_links, g_gaps, N - 1, SOFT, HARD, 0, 0.01f, g_target, g_over) == 0);
    CHECK(g_over[0] == 0.0 && g_over[1] == 0.0 && g_over[3] == 0.0);
    for (uint32_t i = 0; i < N; i++)
    {
        const float off = fabsf(g_target[i] - g_ground[i]);
        CHECK(off <= 4.0f + 1.0e-3f);
        /* the 4.5 degree profile where it lies within the cap, the cap itself where it does not */
        if (fabsf(g_soft[i] - g_ground[i]) <= 4.0f) { same++; CHECK(fabsf(g_target[i] - g_soft[i]) < 2.0e-3f); }
        else { held++; CHECK(fabsf(off - 4.0f) < 2.0e-3f && (g_target[i] > g_ground[i]) == (g_soft[i] > g_ground[i])); }
        gentle += off > 1.0f;
    }
    CHECK(same > 60 && held > 80 && gentle > 150); /* the ground alone keeps 7 degrees: the gentle limit is what moves it */
    CappedAgrees(0, 0.01f);
    g_case = "the same ridge eased 16 passes: the crest is not cut past the cap, and no station is counted past it";
    CappedAgrees(16, 0.01f);
    CHECK(g_over[0] == 0.0 && g_over[1] == 0.0 && g_over[3] == 0.0);
    for (uint32_t i = 0; i < N; i++) CHECK(fabsf(g_target[i] - g_ground[i]) <= 4.0f + 1.0e-3f);
    CHECK(fabsf(g_target[100] - (g_ground[100] - 4.0f)) < 1.0e-3f);
    /* where the gentle profile lies inside the cap the easing still rounds it: the bend at the ridge's foot */
    CoXlTestGradeCapped(g_ground, g_pinned, g_earth, N, g_links, g_gaps, N - 1, SOFT, HARD, 0, 0.01f, g_soft, g_over);
    float sharp[2] = {0.0f, 0.0f};
    for (uint32_t i = 1; i + 1 < N; i++)
    {
        const float raw = fabsf(g_soft[i + 1] - 2.0f * g_soft[i] + g_soft[i - 1]), eased = fabsf(g_target[i + 1] - 2.0f * g_target[i] + g_target[i - 1]);
        if (fabsf(g_soft[i] - g_ground[i]) < 3.0f && raw > sharp[0]) sharp[0] = raw;
        if (fabsf(g_soft[i] - g_ground[i]) < 3.0f && eased > sharp[1]) sharp[1] = eased;
    }
    CHECK(sharp[0] > 0.15f && sharp[1] < 0.5f * sharp[0]); /* 0.168 m before, 0.043 m after */

    g_case = "a ridge steeper than 7 degrees: the capped road leaves the cap only as far as 7 degrees needs, and those stations are counted";
    /* 0.25 a metre up to 30 m at station 100 and 0.5 a metre down, a station every 4 m: no two stations move alike */
    for (uint32_t i = 0; i < N; i++) { const float up = i <= 100 ? 30.0f - (100.0f - (float)i) : 30.0f - 2.0f * ((float)i - 100.0f); g_ground[i] = up > 0.0f ? up : 0.0f; }
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, Even(N, 4.0f), SOFT, 0, g_soft, g_out);
    CHECK(g_out[2] > 10.0);
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, N - 1, HARD, 0, g_hard, g_out);
    const double needed = g_out[2];
    CHECK(needed > 11.0 && needed < 11.6);
    CHECK(CoXlTestGradeCapped(g_ground, g_pinned, g_earth, N, g_links, g_gaps, N - 1, SOFT, HARD, 0, 0.25f, g_target, g_over) == 0);
    float deepest = 0.0f; held = 0;
    for (uint32_t i = 0; i < N; i++)
    {
        const float off = fabsf(g_target[i] - g_ground[i]), need = fabsf(g_hard[i] - g_ground[i]);
        CHECK(off <= (need > 4.0f ? need : 4.0f) + 2.0e-3f); /* never further than the larger of the cap and what 7 degrees takes */
        if (off > deepest) deepest = off;
        held += off > 4.25f;
    }
    CHECK(fabs(deepest - needed) < 2.0e-3 && held > 40 && g_over[0] == held && fabs(g_over[1] - (needed - 4.0)) < 2.0e-3 && g_over[2] == 100.0);
    CHECK(g_over[3] == 4.0 * held); /* inner stations: half of each of two 4 m links */
    CappedAgrees(0, 0.25f);
    CappedAgrees(16, 0.25f);
    for (uint32_t i = 0; i < N; i++) g_earth[i] = 9.0f; /* a wider cap takes more of the gentle profile: fewer stations past it */
    const double before = g_over[0];
    CappedAgrees(16, 0.25f);
    CHECK(g_over[0] < before);

    g_case = "no cap and one limit: the capped solve is the plain one";
    uint32_t seed = 0x51C0FFEu; float height = 30.0f;
    for (uint32_t i = 0; i < N; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        height += ((float)(seed >> 8 & 0xFFFF) / 65535.0f - 0.5f) * 6.0f;
        g_ground[i] = height;
        seed = seed * 1664525u + 1013904223u;
        if (i + 1 < N) g_gaps[i] = 1.0f + (float)(seed >> 8 & 0xFF) / 255.0f * 9.0f;
    }
    CoXlTestGradeSolve(g_ground, g_pinned, N, g_links, g_gaps, N - 1, SLOPE, 5, g_soft, g_out);
    CHECK(g_out[2] > 1.0);
    CHECK(CoXlTestGradeCapped(g_ground, g_pinned, NULL, N, g_links, g_gaps, N - 1, SLOPE, SLOPE, 5, 0.0f, g_target, g_over) == 0 && g_over[0] == 0.0);
    for (uint32_t i = 0; i < N; i++) CHECK(fabsf(g_target[i] - g_soft[i]) < 2.0e-3f);
    for (uint32_t i = 0; i + 1 < N; i++) CHECK(fabsf(g_target[i + 1] - g_target[i]) <= SLOPE * g_gaps[i] + 1.0e-4f);
}

/* A pin is not part of the gentle pass: it comes in with the hard limit, so the road reaches it at 7 degrees. */
static void CappedPins(void)
{
    g_case = "a pad on a ridge's top: met exactly, the road beside it falls at 7 degrees, on fill past the cap";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) { const float up = 30.0f - fabsf((float)i - 100.0f); g_ground[i] = up > 0.0f ? up : 0.0f; g_earth[i] = 4.0f; }
    Even(N, 4.0f);
    g_pinned[100] = 1; g_earth[100] = 0.0f;
    for (uint32_t run = 0; run < 3; run++)
    {
        if (run == 2) g_earth[100] = 2.0f; /* a pin at the rim has some cap left: the pin still holds */
        CappedAgrees(run ? 16 : 0, 0.25f);
        CHECK(g_target[100] == 30.0f);
        CHECK(fabsf(g_target[100] - g_target[99] - HARD * 4.0f) < 1.0e-3f && fabsf(g_target[100] - g_target[101] - HARD * 4.0f) < 1.0e-3f);
        /* at the ridge's foot, 120 m out, the road stands 30 - 120 tan 7 over the ground */
        CHECK(fabsf(g_target[70] - (30.0f - 120.0f * HARD)) < 2.0e-3f && fabs(g_over[1] - (30.0 - 120.0 * HARD - 4.0)) < 2.0e-3 && g_over[0] > 80.0);
    }
    g_case = "two pads 7 degrees cannot join: counted, each left by half the lack at 7 degrees, and no pad among the stations past the cap";
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) { g_ground[i] = i < N / 2 ? 0.0f : 20.0f; g_earth[i] = 4.0f; }
    g_pinned[N / 2 - 1] = g_pinned[N / 2 + 2] = 1; g_earth[N / 2 - 1] = g_earth[N / 2 + 2] = 0.0f;
    CappedAgrees(0, 0.25f);
    const float lack = 20.0f - HARD * 3 * STEP;
    CHECK(CoXlTestGradeCapped(g_ground, g_pinned, g_earth, N, g_links, g_gaps, N - 1, SOFT, HARD, 0, 0.25f, g_target, g_over) >= 4);
    CHECK(fabsf(g_target[N / 2 - 1] - lack / 2) < 1.0e-3f && fabsf(g_target[N / 2 + 2] - (20.0f - lack / 2)) < 1.0e-3f);
    CHECK(g_over[1] < lack / 2 - 1.0f); /* the pads are further off than any counted station */
}

/* ---- the level: which stations a pad pins and what cap each has ---- */
static float GroundRidge(float x, float y) { (void)y; const float up = x < 1000.0f ? 30.0f - 0.25f * (1000.0f - x) : 30.0f - 0.5f * (x - 1000.0f); return up > 0.0f ? up : 0.0f; }
static float GroundMound(float x, float y) { (void)y; const float up = 0.12f * (496.0f - fabsf(x - 1000.0f)); return up > 0.0f ? up : 0.0f; }
static float MoveAt(void* c, float x, float y) { (void)c; return CoXlTestGradeMove(x, y); }
/* Run 3's causeway (Automaton level, spur 0 from the plateau at (-4001, 5055) down to the junction at (-4143, 4784)
 * and on along arterial 1072): the ground of grade-stations-2.tsv along the line, a station every 10 m. */
static float Hillside(float s)
{
    static const float k[16][2] = {{0, 93.5f}, {30, 93.4f}, {60, 91.2f}, {80, 86.8f}, {139, 71.4f}, {198, 53.8f}, {257, 33.8f}, {311, 22.2f}, {355, 20.2f},
        {406, 15.8f}, {457, 13.8f}, {508, 15.3f}, {558, 19.9f}, {609, 26.1f}, {659, 31.9f}, {710, 35.8f}};
    for (int i = 1; i < 16; i++)
        if (s <= k[i][0]) return k[i - 1][1] + (k[i][1] - k[i - 1][1]) * (s - k[i - 1][0]) / (k[i][0] - k[i - 1][0]);
    return k[15][1];
}
#define STEEP 0.3249197f /* tan 18 degrees */
static void Steep(void)
{
    enum { M = 72 };
    static float steep[M], plain[M], moved[M];
    double out[7], flat[7];
    for (uint32_t i = 0; i < M; i++)
    {
        g_ground[i] = Hillside(10.0f * (float)i); g_pinned[i] = i <= 6;
        g_earth[i] = i <= 6 ? 0.0f : i < 11 ? 4.0f * (float)(i - 6) / 5.0f : 4.0f;
    }
    Even(M, 10.0f);
    g_case = "a hillside of 17 to 19 degrees under a pinned plateau: 7 degrees stands the road 38 m over the ground";
    CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, HARD, 4.0f, 16, plain, out);
    CHECK(out[0] == 0 && out[5] > 37.0 && out[5] < 39.0 && out[4] < 7.01); /* the run measured 38.3 m */
    g_case = "the same with links up to 18 degrees where 7 stood over the cap: the road follows the hillside";
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, STEEP, 4.0f, 16, steep, out) == 0);
    CHECK(out[5] < 5.0 && out[6] < 1.0 && out[4] > 7.5 && out[4] < 18.01 && out[0] >= 40 && out[0] <= 60 && out[1] == 10.0 * out[0]);
    CHECK(fabs(atan(out[2]) * 57.29578 - out[4]) < 0.01 && out[3] >= 7 && out[3] < 32); /* the steepest is on the hillside, and a link that may */
    for (uint32_t i = 0; i < M; i++)
    {
        CHECK(fabsf(steep[i] - g_ground[i]) < 8.0f);
        if (i <= 6) CHECK(steep[i] == g_ground[i]);
        if (i + 1 < M && i >= 6) CHECK(fabsf(steep[i + 1] - steep[i]) <= STEEP * 10.0f + 1.0e-3f);
    }
    /* past the hillside's foot, where 7 degrees stayed within the cap, no link climbs more than 7 */
    for (uint32_t i = 50; i + 1 < M; i++) CHECK(fabsf(steep[i + 1] - steep[i]) <= HARD * 10.0f + 1.0e-3f);
    g_case = "the steep rule has no branch on a height: a millimetre of ground moves the road no further";
    g_ground[30] += 0.001f;
    CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, STEEP, 4.0f, 16, moved, flat);
    { float far_ = 0.0f; for (uint32_t i = 0; i < M; i++) if (fabsf(moved[i] - steep[i]) > far_) far_ = fabsf(moved[i] - steep[i]); CHECK(far_ < 0.002f); }
    g_ground[30] -= 0.001f;
    g_case = "ground 7 degrees serves within the cap: no link may climb more and the profile is the capped one's";
    for (uint32_t i = 0; i < M; i++) { g_ground[i] = 3.0f * sinf(0.05f * (float)i * 10.0f) + 0.06f * 10.0f * (float)i; g_pinned[i] = 0; g_earth[i] = 4.0f; }
    CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, STEEP, 4.0f, 16, steep, out);
    CoXlTestGradeCapped(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, 16, 0.25f, plain, g_over);
    CHECK(out[0] == 0 && memcmp(steep, plain, sizeof steep) == 0 && out[4] < 7.01);
    g_case = "pins on a pad that is not level (10 degrees): each keeps its ground, none is torn, and the road leaves the last within the limit";
    for (uint32_t i = 0; i < M; i++) { g_ground[i] = i < 6 ? 1.7633f * (float)i : 1.7633f * 5.0f; g_pinned[i] = i < 6; g_earth[i] = i < 6 ? 0.0f : 4.0f; }
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, STEEP, 4.0f, 16, steep, out) == 0);
    for (uint32_t i = 0; i < 6; i++) CHECK(steep[i] == g_ground[i]);
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, g_earth, M, g_links, g_gaps, M - 1, SOFT, HARD, 0.0f, 4.0f, 16, moved, flat) == 0); /* and with no steep rule */
    CHECK(out[0] == 0 && out[4] < 7.01); /* the links between pins are the pad's own ground, not road: not measured */
    for (uint32_t i = 0; i < M; i++) { g_pinned[i] = 0; g_earth[i] = 4.0f; }

    g_case = "a place's footprint: a road's share and cap fall to none at its edge over 48 m, and it pins nothing";
    {
        const float world = 4096.0f, playable = 2000.0f, mid = 2048.0f; float cap = 0.0f; int pinned = 1;
        float pad[6] = {0.0f, 0.0f, 40.0f, 20.0f, 1.0f, 1.0f}; /* a footprint of 40 m whose tile band is 20 m */
        CoXlTestGradePads(pad, 1);
        CHECK(CoXlTestGradeFade(world, playable, mid + 39.0f, mid, &cap) == 1.0f && cap == 0.0f);
        CHECK(fabsf(CoXlTestGradeFade(world, playable, mid + 64.0f, mid, &cap) - 0.5f) < 1.0e-4f); /* halfway: 24 of 48 m */
        CHECK(CoXlTestGradeFade(world, playable, mid + 88.5f, mid, &cap) == 0.0f && cap > 3.85f && cap < 3.9f); /* the ring of 10 m still touches the fade */
        CHECK(CoXlTestGradeFade(world, playable, mid + 98.5f, mid, &cap) == 0.0f && cap == 4.0f); /* 58 m from the footprint: whole */
        CoXlTestGradeFree(world, playable, mid + 10.0f, mid, &pinned);
        CHECK(pinned == 0);
        pad[5] = 0.0f; pad[3] = 48.0f;
        CoXlTestGradePads(pad, 1);
        CHECK(fabsf(CoXlTestGradeFade(world, playable, mid + 64.0f, mid, &cap) - 0.5f) < 1.0e-4f); /* as a pad of the same band */
        CoXlTestGradePads(NULL, 0);
    }
}

static void Zone(void)
{
    const float world = 2048.0f, mid = 1024.0f;
    const float pad[6] = {296.0f, 0.0f, 20.0f, 40.0f, 1.0f, 0.0f}, zone[4] = {-440.0f, -40.0f, -360.0f, 40.0f};
    double out[6]; int pinned = -1; uint32_t links; float xy[2], band;
    g_case = "a flatten zone pins nothing and leaves the whole cap; a pad's flat part pins with no cap, its band has a part of it, the rim its share";
    CoXlTestGradePads(pad, 1);
    CoXlTestGradeZones(zone, 1);
    const uint32_t n = Line(g_road, 200.0f, mid, 1792.0f, mid, N); /* a station every 8 m: 53 at the zone's centre, 140 at the pad's */
    CHECK(CoXlTestGradeLevel(g_road, n, GroundTilt, world, 696.0f, out) == 0);
    /* pinned: 24 and 17 stations within 64 m of the playable radius, 5 within 16 m of the pad's centre */
    CHECK(out[0] == 46.0 && out[1] == 0.0);
    for (uint32_t i = 46; i <= 60; i++) CHECK(CoXlTestGradeStationCap(i, &pinned) == 4.0f && pinned == 0); /* the zone's box and its bands */
    CHECK(CoXlTestGradeFree(world, 696.0f, 624.0f, mid, &pinned) == 1.0f && pinned == 0);
    CHECK(CoXlTestGradeStationCap(140, &pinned) == 0.0f && pinned == 1);
    CHECK(CoXlTestGradeStationCap(142, &pinned) == 0.0f && pinned == 1);
    /* the cap takes the largest pad weight within 10 m, the pad's band 48 m wide at least: 4 (1 - smoothstep(weight / 0.98)) */
    CHECK(CoXlTestGradeStationCap(143, &pinned) == 0.0f && pinned == 0);                        /* 24 m out: the flat part is within the bed */
    CHECK(fabsf(CoXlTestGradeStationCap(145, &pinned) - 0.0996f) < 5.0e-3f && pinned == 0);     /* 40 m out: weight 0.888 at 30 m */
    CHECK(fabsf(CoXlTestGradeStationCap(148, &pinned) - 3.5456f) < 5.0e-3f && pinned == 0);     /* 64 m out: weight 0.206 at 54 m */
    CHECK(CoXlTestGradeStationCap(150, &pinned) == 4.0f && pinned == 0);
    CHECK(fabsf(CoXlTestGradeStationCap(186, &pinned) - 2.0f) < 1.0e-4f && pinned == 1); /* 32 m inside the playable radius */
    CHECK(CoXlTestGradeStationCap(24, &pinned) == 4.0f && pinned == 0 && CoXlTestGradeStationCap(23, &pinned) == 3.5f && pinned == 1);
    CHECK(fabsf(CoXlTestGradeStation(53, xy, &band, &links) - 0.05f * 624.0f) < 1.0e-3f && xy[0] == 624.0f);
    CoXlTestGradePads(pad, 0);

    g_case = "the level's solve: 4.5 degrees within 4 m of the ground, 7 degrees and up to 18 where 7 leaves the cap, 16 passes, the stations past the cap reported";
    CHECK(CoXlTestGradeLevel(g_road, n, GroundRidge, world, 1000.0f, out) == 0 && out[0] == 0.0);
    memset(g_pinned, 0, sizeof g_pinned);
    for (uint32_t i = 0; i < N; i++) { g_ground[i] = GroundRidge(g_road[2 * i], mid); g_earth[i] = 4.0f; }
    Even(N, 8.0f);
    RefSteep(N, (double)SOFT, (double)HARD, 0.3249197, 4.0, 16);
    double far_ = 0.0, steepest = 0.0, worst, metres[2]; uint32_t at, count[2];
    for (uint32_t i = 0; i < N; i++)
    {
        g_target[i] = CoXlTestGradeStation(i, xy, &band, &links);
        if (fabs(g_target[i] - g_ref[i]) > far_) far_ = fabs(g_target[i] - g_ref[i]);
        if (i > 0 && fabsf(g_target[i] - g_target[i - 1]) / 8.0f > steepest) steepest = fabsf(g_target[i] - g_target[i - 1]) / 8.0f;
    }
    CHECK(far_ < 5.0e-3 && steepest <= 0.3249197 + 1.0e-5 && steepest > 2.0 * HARD);
    count[0] = RefOverCap(N, 0.26, &worst, &at, &metres[0]);
    count[1] = RefOverCap(N, 0.24, &worst, &at, &metres[1]);
    CHECK(count[0] > 10 && out[1] >= count[0] && out[1] <= count[1] && out[5] >= metres[0] - 1.0e-3 && out[5] <= metres[1] + 1.0e-3);
    /* 3.3 m past the cap at the top, where 7 degrees alone took 7.07 m */
    CHECK(fabs(out[2] - worst) < 5.0e-3 && worst > 3.2 && worst < 3.4 && at == 100 && out[3] == 1000.0 && out[4] == mid); /* the ridge's top */

    g_case = "what the grading adds: nothing off the band, the whole cut on the bed, eased between; a 4 m cut's shoulder within 12 degrees";
    CHECK(CoXlTestGradeLevel(g_road, n, GroundMound, world, 1000.0f, out) == 0);
    /* station 90 (x 920): the cut is the cap for 40 m to either side, the band 10 + 1.5 x 4 / tan 12 = 38.23 m */
    CHECK(fabsf(CoXlTestGradeStation(90, xy, &band, &links) - (GroundMound(920.0f, mid) - 4.0f)) < 0.01f && fabsf(band - 38.228f) < 0.05f);
    CHECK(fabsf(CoXlTestGradeMove(920.0f, mid) + 4.0f) < 0.02f && fabsf(CoXlTestGradeMove(920.0f, mid - 9.5f) + 4.0f) < 0.05f);
    CHECK(CoXlTestGradeMove(920.0f, mid + 40.0f) == 0.0f && CoXlTestGradeMove(920.0f, mid - 300.0f) == 0.0f);
    CHECK(fabsf(CoXlTestGradeMove(920.0f, mid + 24.114f) + 2.0f) < 0.05f);   /* weight 0.5 */
    CHECK(fabsf(CoXlTestGradeMove(920.0f, mid - 31.171f) + 0.625f) < 0.05f); /* weight 0.25 eased: 0.15625 */
    const float across[4] = {920.0f, mid, 922.0f, mid};
    CoXlTestGradeShoulder(MoveAt, across, 2, out);
    CHECK(out[0] > 11.0 && out[0] <= 12.0 * 1.05 && out[3] > 3.0 && out[3] < 10.0);
    /* a small pad whose flat part lies on the bed, 6 m off the line: nothing is added on it, the stations beside it
     * lose their cap, and the cut comes up to it along the road and across: no wall stands at the pad's edge */
    const float small[6] = {-100.0f, 6.0f, 3.0f, 1.0f, 1.0f, 0.0f};
    CoXlTestGradePads(small, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundMound, world, 1000.0f, out) == 0);
    CHECK(CoXlTestGradeStationCap(90, &pinned) == 0.0f && pinned == 0 && CoXlTestGradeStationCap(91, &pinned) == 0.0f);
    CHECK(CoXlTestGradeStationCap(84, &pinned) > 3.0f && CoXlTestGradeStationCap(80, &pinned) == 4.0f);
    CHECK(fabsf(CoXlTestGradeMove(924.0f, mid + 6.0f)) < 1.0e-4f && fabsf(CoXlTestGradeMove(925.5f, mid + 7.5f)) < 1.0e-4f);
    CHECK(fabsf(CoXlTestGradeMove(924.0f, mid)) < 2.5f); /* what 7 degrees still takes beside the pad; 4 m without the cap's fall */
    float step = 0.0f;
    for (float x = 880.0f; x <= 968.0f; x += 4.0f)
        for (float y = mid - 44.0f; y < mid + 44.0f; y += 0.5f)
        {
            const float rise = fabsf(CoXlTestGradeMove(x, y + 2.0f) - CoXlTestGradeMove(x, y));
            if (rise > step) step = rise;
        }
    CHECK(step < 0.45f); /* a 4 m cut's own shoulder is 0.425 m in 2 m (tan 12 degrees); the pillar was 4 m */
    CoXlTestGradePads(small, 0);
}

/* A pad beside a road and a road onto a pad, on sloping land: the ground is the land eased to the pad's height by
 * the pad's weight, as op 0x3F leaves it. */
static struct { float x, y, r, band, alongX, alongY; } g_site;
static float SiteWeight(float x, float y)
{
    float t = (sqrtf((x - g_site.x) * (x - g_site.x) + (y - g_site.y) * (y - g_site.y)) - g_site.r) / g_site.band;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
static float GroundSite(float x, float y) { return (g_site.alongX * (x - g_site.x) + g_site.alongY * (y - g_site.y)) * (1.0f - SiteWeight(x, y)) + 60.0f; }
static void Site(void)
{
    const float world = 2048.0f, mid = 1024.0f;
    double out[6]; int pinned = -1; uint32_t links; float xy[2], band;
    g_case = "a pad's flat ground 20 m beside a road on a side-hill of 11.3 degrees, its band 16 m: no wall at its edge";
    g_site.x = 1000.0f; g_site.y = mid + 30.0f; g_site.r = 10.0f; g_site.band = 16.0f; g_site.alongX = 0.0f; g_site.alongY = 0.2f;
    const float beside[6] = {1000.0f - mid, 30.0f, 10.0f, 16.0f, 1.0f, 0.0f};
    uint32_t n = Line(g_road, 200.0f, mid, 1792.0f, mid, N);
    CoXlTestGradePads(beside, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundSite, world, 1000.0f, out) == 0 && out[0] == 0.0);
    CHECK(fabsf(CoXlTestGradeMove(1000.0f, mid + 30.0f)) < 1.0e-4f && fabsf(CoXlTestGradeMove(1004.0f, mid + 24.0f)) < 1.0e-4f); /* the flat part */
    CHECK(CoXlTestGradeMove(1000.0f, mid - 9.0f) > 1.0f); /* the bed is level across the hill: filled on its low side */
    float wall = 0.0f;
    for (float x = 960.0f; x <= 1040.0f; x += 4.0f)
        for (float y = mid - 60.0f; y < mid + 60.0f; y += 0.5f)
        {
            /* over what a level bed (the hill's 0.2 a metre) and a 12 degree shoulder change the move in 2 m */
            const float rise = fabsf(CoXlTestGradeMove(x, y + 2.0f) - CoXlTestGradeMove(x, y)) - 2.0f * (0.2f + 0.2125566f);
            if (rise > wall) wall = rise;
        }
    CHECK(wall < 0.35f); /* the road drawn whole to the pad's skirt left 8 m in 2 m here */
    CoXlTestGradePads(beside, 0);

    g_case = "a rectangle pad with 8 m bands 30 m ahead of a road's end: its band counts 48 m wide, so the cap falls before it";
    n = Line(g_road, 200.0f, mid, 960.0f, mid, 96);
    CoXlTestGradePads(beside, 0);
    CoXlTestGradeRect(1000.0f - mid, 0.0f, 10.0f, 30.0f, 8.0f, 8.0f);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundFlat, world, 1000.0f, out) == 0 && out[0] == 0.0);
    CHECK(fabsf(CoXlTestGradeStationCap(95, &pinned) - 1.2f) < 0.1f && pinned == 0 && CoXlTestGradeStationCap(85, &pinned) == 4.0f); /* weight 0.62 at 30 m */
    CoXlTestGradePads(beside, 0);

    g_case = "a road onto a pad on land that falls 11.3 degrees along it: with the ground, under 18 degrees, to the pad's first station; the pad met";
    g_site.x = mid; g_site.y = mid; g_site.r = 20.0f; g_site.band = 97.0f; g_site.alongX = 0.2f; g_site.alongY = 0.0f;
    const float onto[6] = {0.0f, 0.0f, 20.0f, 97.0f, 1.0f, 0.0f};
    n = Line(g_road, 200.0f, mid, mid, mid, 104); /* a station every 8 m, the last at the pad's centre */
    CoXlTestGradePads(onto, 1);
    CHECK(CoXlTestGradeLevel(g_road, n, GroundSite, world, 1000.0f, out) == 0);
    CHECK(CoXlTestGradeStation(103, xy, &band, &links) == 60.0f && CoXlTestGradeStationOn(103) == 3 && CoXlTestGradeStationOn(100) == 1);
    CHECK(fabsf(CoXlTestGradeStation(100, xy, &band, &links) - GroundSite(1000.0f, mid)) < 1.0e-3f);
    float steepest = 0.0f, beside2 = 0.0f;
    for (float x = 940.0f; x <= 1010.0f; x += 0.5f)
        for (int side = 0; side < 3; side++)
        {
            const float y = mid + 4.5f * (float)side;
            const float rise = fabsf(GroundSite(x + 2.0f, y) + CoXlTestGradeMove(x + 2.0f, y) - GroundSite(x, y) - CoXlTestGradeMove(x, y)) / 2.0f;
            if (side == 0 && rise > steepest) steepest = rise;
            if (side != 0 && rise > beside2) beside2 = rise;
        }
    CHECK(steepest < 0.25f && steepest > 0.2f); /* the land's own fall where 7 degrees would cut over the cap; the skirt left 20 degrees here */
    CHECK(beside2 < 0.28f);
    CHECK(fabsf(CoXlTestGradeMove(mid, mid)) < 1.0e-6f && CoXlTestGradeFree(world, 1000.0f, 998.0f, mid, &pinned) > 0.99f);
    CoXlTestGradePads(onto, 0);
}

/* ---- the measures the checker reads ---- */
static float WallAcross(void* c, float x, float y) { (void)c; (void)y; return x < 500.0f ? 3.0f : 4.2f; }
static float StepBeside(void* c, float x, float y) { (void)c; (void)x; return y > 304.5f ? 1.0f : 0.0f; }
static float HillGround(void* c, float x, float y) { (void)c; (void)x; return 0.3f * (y - 300.0f); }
/* a level bed across the hill, eased back to the ground over 40 m on either side */
static float HillMove(void* c, float x, float y)
{
    const float out = fabsf(y - 300.0f), edge = y > 300.0f ? -3.0f : 3.0f;
    return out <= 10.0f ? -HillGround(c, x, y) : out >= 50.0f ? 0.0f : edge * (50.0f - out) / 40.0f;
}
static void Measures(void)
{
    double out[8];
    const uint32_t n = Line(g_road, 100.0f, 300.0f, 1100.0f, 300.0f, 126);
    g_case = "a 1.2 m wall across the road: 31 degrees between neighbouring samples, 6.8 on the 10 m baseline";
    CoXlTestGradeMeasures(WallAcross, NULL, g_road, n, out);
    CHECK(fabs(out[2] - 30.96) < 0.05 && out[3] >= 500.0 && out[3] <= 502.0 && out[4] == 300.0 && out[5] == 0.0);
    double road[9];
    CoXlTestGradeRoad(WallAcross, g_road, n, road);
    CHECK(fabs(road[6] - 6.84) < 0.05);
    g_case = "a 1 m step 4.5 m beside the line: seen across the bed between 3 and 6 m, by neither the cross slope nor the shoulder";
    CoXlTestGradeMeasures(StepBeside, NULL, g_road, n, out);
    CHECK(fabs(out[5] - 18.43) < 0.05 && out[6] == 100.0 && out[2] == 0.0 && out[0] == 0.0);
    CoXlTestGradeRoad(StepBeside, g_road, n, road);
    CHECK(road[8] == 0.0);
    g_case = "a level bed on a side-hill of 16.7 degrees: the move alone reads the hill, over the ground it reads what the blend adds";
    CoXlTestGradeMeasures(HillMove, NULL, g_road, n, out);
    CHECK(fabs(out[0] - 16.70) < 0.05);
    CoXlTestGradeMeasures(HillMove, HillGround, g_road, n, out);
    CHECK(fabs(out[0] - 3.86) < 0.05 && fabs(out[1] - 16.70 - 4.29) < 0.1); /* atan 0.375 - atan 0.3; the crease is the move's own */
}

/* The fine relief a road keeps on a near tile: none on the worn core, a part of it from there to the bed's edge. */
static void Detail(void)
{
    uint32_t torn = 0, counts[1], ends[2] = {~0u, ~0u};
    g_case = "a road's share of a tile's fine relief: none to 4 m, 0.35 at the bed's edge, with the tile's share to the band's end";
    counts[0] = Line(g_road, 200.0f, 1000.0f, 1800.0f, 1000.0f, 201);
    CoXlTestGradeField(g_road, counts, ends, 1, 0, GroundFlat, FAR_BACK, SLOPE, 4, WORLD, &torn);
    CHECK(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -24.0f) == 0.0f && CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -21.0f) == 0.0f);
    CHECK(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -20.0f) == 0.0f && CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -28.0f) == 0.0f); /* 4 m out, either side */
    CHECK(fabsf(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -17.0f) - 0.175f) < 1.0e-3f);  /* 7 m out: halfway */
    CHECK(fabsf(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -14.0f) - 0.35f) < 1.0e-3f);   /* the bed's edge */
    CHECK(fabsf(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -13.9f) - 0.35f) < 1.0e-3f);   /* no crease there */
    CHECK(fabsf(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, -2.0f) - 0.175f) < 1.0e-3f);   /* 22 m out: the level's share is a half */
    CHECK(CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, 10.5f) == 0.0f && CoXlTestGradeDetail(WORLD, 1000.0f, 0.0f, 300.0f) == 0.0f);
    g_case = "fine relief: a texel's height over the mean of the 15 x 15 about it, a metre at most; a plane has none";
    static float h[32 * 32];
    for (int r = 0; r < 32; r++) for (int c = 0; c < 32; c++) h[r * 32 + c] = 0.5f + 0.0003f * (float)c - 0.0002f * (float)r;
    CHECK(fabsf(CoXlTestNearFine(h, 32, 16, 16)) < 1.0e-3f);
    h[16 * 32 + 16] += 0.0005f; /* half a metre */
    CHECK(fabsf(CoXlTestNearFine(h, 32, 16, 16) - 0.5f * 224.0f / 225.0f) < 2.0e-3f);
    CHECK(fabsf(CoXlTestNearFine(h, 32, 12, 13) + 0.5f / 225.0f) < 2.0e-3f); /* a neighbour within the window */
    CHECK(fabsf(CoXlTestNearFine(h, 32, 16, 8)) < 1.0e-3f);                   /* 8 texels away: outside it */
    h[16 * 32 + 16] += 0.005f;
    CHECK(CoXlTestNearFine(h, 32, 16, 16) == 1.0f);
    h[16 * 32 + 16] -= 0.02f;
    CHECK(CoXlTestNearFine(h, 32, 16, 16) == -1.0f);
    h[24 * 32 + 7] += 0.0005f; /* the last texel with a whole window */
    CHECK(CoXlTestNearFine(h, 32, 6, 16) == 0.0f && CoXlTestNearFine(h, 32, 16, 25) == 0.0f && CoXlTestNearFine(NULL, 32, 16, 16) == 0.0f);
    CHECK(fabsf(CoXlTestNearFine(h, 32, 7, 24) - 0.5f * 224.0f / 225.0f) < 2.0e-3f);
}

/* ---- a road's shoulders: 4 m spans from 6 m out to 170 m, both sides, a section every 8 m ---- */
#define TAN20 0.3639702f
static struct { float from; int left, right; float x0, x1; } g_bank;
static float Bank(void* c, float x, float y)
{
    (void)c;
    const float out = y - 300.0f;
    if (x < g_bank.x0 || x > g_bank.x1 || (out > 0.0f ? !g_bank.left : !g_bank.right) || fabsf(out) <= g_bank.from) return 0.0f;
    return TAN20 * (fabsf(out) - g_bank.from);
}
static void BankIs(float from, int left, int right, float x0, float x1) { g_bank.from = from; g_bank.left = left; g_bank.right = right; g_bank.x0 = x0; g_bank.x1 = x1; }
static void Shoulder(void)
{
    double out[6];
    const uint32_t n = Line(g_road, 100.0f, 300.0f, 1100.0f, 300.0f, 126);
    g_case = "a level bed with banks of 20 degrees from its edge: the side slope is 20 degrees, the crease 20 degrees at the bank's foot";
    BankIs(10.0f, 1, 1, 0.0f, 2000.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(fabs(out[0] - 20.0) < 0.2 && fabs(out[3] - 20.0) < 0.2);
    CHECK(out[4] == 100.0 && fabs(fabs(out[5] - 300.0) - 14.0) < 0.01 && fabs(out[2] - 300.0) > 13.99 && fabs(out[2] - 300.0) < 170.01);
    g_case = "a bank on one side only is read on that side, whichever it is";
    BankIs(10.0f, 1, 0, 0.0f, 2000.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(fabs(out[0] - 20.0) < 0.2 && fabs(out[3] - 20.0) < 0.2 && fabs(out[5] - 314.0) < 0.01);
    BankIs(10.0f, 0, 1, 0.0f, 2000.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(fabs(out[0] - 20.0) < 0.2 && fabs(out[3] - 20.0) < 0.2 && fabs(out[5] - 286.0) < 0.01);
    g_case = "one plane across the road: its slope is the side slope and there is no crease";
    CoXlTestGradeShoulder(PlaneAcross, g_road, n, out);
    CHECK(fabs(out[0] - 2.8624) < 0.01 && out[3] < 1.0e-3);
    g_case = "the last span ends 170 m out, the widest band's end: a bank from 166 m is read, one from 170 m is not";
    BankIs(166.0f, 1, 1, 0.0f, 2000.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(fabs(out[0] - 20.0) < 0.2 && fabs(out[3] - 20.0) < 0.2 && fabs(fabs(out[5] - 300.0) - 170.0) < 0.01);
    BankIs(170.0f, 1, 1, 0.0f, 2000.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(out[0] == 0.0 && out[3] == 0.0);
    g_case = "a section every fourth sample: a bank between two sections is not read, one at a section is";
    BankIs(10.0f, 1, 1, 101.0f, 107.0f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(out[0] == 0.0 && out[3] == 0.0);
    BankIs(10.0f, 1, 1, 107.5f, 108.5f);
    CoXlTestGradeShoulder(Bank, g_road, n, out);
    CHECK(fabs(out[0] - 20.0) < 0.2 && out[1] == 108.0 && out[4] == 108.0);
}

/* A place is its stamp's own circle, not the band around it, and no other kind of pad. */
static void InPlace(void)
{
    const float world = 2048.0f, playable = 1000.0f, mid = 1024.0f;
    float pad[6] = {0.0f, 0.0f, 20.0f, 40.0f, 1.0f, 1.0f}, open = 0.0f;
    const float zone[4] = {-30.0f, -30.0f, 30.0f, 30.0f};
    g_case = "inside a stamp's footprint: a place, and no surface up to the places' edges; its band and beyond are open";
    CoXlTestGradePads(pad, 1);
    CHECK(CoXlTestGradeInPlace(world, playable, mid + 10.0f, mid, &open) == 1 && !isfinite(open));
    CHECK(CoXlTestGradeInPlace(world, playable, mid, mid + 20.0f, &open) == 1 && !isfinite(open));
    CHECK(CoXlTestGradeInPlace(world, playable, mid - 21.0f, mid, &open) == 0 && open == 7.0f);
    CHECK(CoXlTestGradeInPlace(world, playable, mid + 15.0f, mid + 15.0f, &open) == 0 && open == 7.0f); /* 21.2 m out */
    CHECK(CoXlTestGradeInPlace(world, playable, mid, mid - 50.0f, &open) == 0 && open == 7.0f);
    CHECK(CoXlTestGradeInPlace(world, playable, mid + 70.0f, mid, &open) == 0 && open == 7.0f);
    CHECK(CoXlTestGradeInPlace(world, playable, -5.0f, mid, &open) == 0 && CoXlTestGradeInPlace(world, playable, mid, 4000.0f, &open) == 0);
    g_case = "a flatten pad and a flatten zone are not places";
    pad[5] = 0.0f;
    CoXlTestGradePads(pad, 1);
    CoXlTestGradeZones(zone, 1);
    CHECK(CoXlTestGradeInPlace(world, playable, mid + 10.0f, mid, &open) == 0 && open == 7.0f);
    CoXlTestGradePads(pad, 0);
    CHECK(CoXlTestGradeInPlace(world, playable, mid + 10.0f, mid, &open) == 0 && open == 7.0f);
}

/* ---- where roads meet ---- */
static float g_meetAlong, g_meetSpur;
/* a hillside that falls along the arterial (x) and, north of it, towards it along the spur (y) */
static float GroundMeet(float x, float y) { return 100.0f + g_meetAlong * (x - 1200.0f) + (y > 2000.0f ? g_meetSpur * (y - 2000.0f) : 0.0f); }
static float FieldAt(void* c, float x, float y) { float z = NAN, d; (void)c; return CoXlTestGradeFieldAt(x, y, &z, &d) > 0.0f ? z : NAN; }
static void Meet(void)
{
    static float pts[2 * 128];
    static const uint32_t counts[2] = {68, 31}, ends[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    double out[6], m[8];
    /* an arterial west to east, a point every 6 m; a spur from the north, every 10 m, onto its middle */
    for (uint32_t i = 0; i < 68; i++) { pts[2 * i] = 999.0f + 6.0f * (float)i; pts[2 * i + 1] = 2000.0f; }
    for (uint32_t i = 0; i < 31; i++) { pts[2 * (68 + i)] = 1200.0f; pts[2 * (68 + i) + 1] = 2300.0f - 10.0f * (float)i; }
    CoXlTestGradePads(NULL, 0);

    g_case = "junction: the spur stands at the arterial's height in its bed; the arterial is as it is without the spur";
    static float alone[68], with[68], steep[68];
    g_meetAlong = 0.06f; g_meetSpur = 0.11f; /* 3.4 degrees along the arterial, 6.3 down the spur */
    { static const uint32_t one[1] = {68}; CHECK(CoXlTestGradeRoads(pts, one, ends, 1, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 0.0); }
    for (uint32_t i = 0; i < 68; i++) { float xy[2], g; CoXlTestGradeStationPin(i, xy, &alone[i], &g); }
    CoXlTestGradeMeasures(FieldAt, NULL, pts, 68, m);
    const double bare = m[5]; /* across the arterial's bed with no spur: what the pieces' own hand-over reads */
    CHECK(CoXlTestGradeRoads(pts, counts, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0);
    CHECK(out[0] == 0.0 && out[1] == 0.0); /* a spur's junction is no landing: nothing of the arterial is levelled for it */
    uint32_t node = 0xFFFFFFFFu, beds = 0, own = 0; float moved = 0.0f, top = 0.0f;
    for (uint32_t i = 0; i < 68 + 31; i++)
    {
        float xy[2], t, g;
        const int marks = CoXlTestGradeStationPin(i, xy, &t, &g);
        if (fabsf(xy[0] - 1200.0f) < 0.5f && fabsf(xy[1] - 2000.0f) < 0.5f) { node = i; top = t; CHECK(!(marks & 4)); }
        if (xy[1] > 2000.5f) { own++; CHECK(marks & 4); } else CHECK(!(marks & 4));
        CHECK(!(marks & 1) && !(marks & 16)); /* a settled station is held for the spurs' solve only */
        if (i < 68) { with[i] = t; if (fabsf(t - alone[i]) > moved) moved = fabsf(t - alone[i]); }
    }
    CHECK(node != 0xFFFFFFFFu && own == 30);
    CHECK(moved < 0.02f); /* against the arterial alone: only the junction's station parts one piece in two */
    float first = 0.0f, second = 0.0f;
    for (uint32_t i = 68; i < 68 + 31; i++)
    {
        float xy[2], t, g;
        const int marks = CoXlTestGradeStationPin(i, xy, &t, &g);
        if (!(marks & 4)) continue;
        /* the piece from the junction lies in the bed: its far end, 10 m out, is the arterial's surface there */
        if (fabsf(xy[1] - 2010.0f) < 0.5f) { beds++; first = t; CHECK((marks & 8) && fabsf(t - top) < 0.02f); } else CHECK(!(marks & 8));
        if (fabsf(xy[1] - 2020.0f) < 0.5f) second = t;
    }
    CHECK(beds == 1 && second - first > 0.3f && second - first <= 0.1228f * 10.0f + 1.0e-3f); /* then it takes its hill, by the first step */
    CoXlTestGradeMeasures(FieldAt, NULL, pts, 68, m);
    printf("  mild: the arterial %.2f degrees across its bed (tilt %.2f), %.2f on 2 m\n", m[5], m[1], m[2]);
    CHECK(m[5] <= bare + 0.05 && m[2] <= 7.5); /* the spur's hill is no cross slope on the arterial's bed */
    CoXlTestGradeMeasures(FieldAt, NULL, pts + 2 * 68, 31, m);
    printf("  mild: the spur %.2f degrees across its bed (tilt %.2f), %.2f on 2 m\n", m[5], m[1], m[2]);
    CHECK(m[5] <= 1.7 * 3.43 && m[2] <= 7.5); /* on the arterial's bed it reads the arterial's grade, 3.4 degrees */
    {
        /* the field in the arterial's bed is the arterial's alone, beside the spur too; past the bed's edge the spur's */
        float z = 0.0f, d = 0.0f, zs = 0.0f, south = 0.0f;
        CHECK(CoXlTestGradeFieldAt(1205.0f, 1994.5f, &south, &d) == 1.0f && fabsf(south - (top + 0.06f * 5.0f)) < 0.06f);
        CHECK(CoXlTestGradeFieldAt(1205.0f, 2005.5f, &z, &d) == 1.0f && fabsf(z - south) < 0.005f); /* as on the side with no spur */
        CHECK(CoXlTestGradeFieldAt(1200.0f, 2020.0f, &zs, &d) == 1.0f && fabsf(zs - second) < 0.02f);
    }

    g_case = "junction on a hillside the hard limit cannot follow: the spur takes the whole difference, the arterial not a bit";
    g_meetSpur = 0.2f; /* 11.3 degrees down the spur, the arterial's ground as before */
    CHECK(CoXlTestGradeRoads(pts, counts, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 0.0 && out[5] <= 0.3250);
    for (uint32_t i = 0; i < 68; i++) { float xy[2], g; CoXlTestGradeStationPin(i, xy, &steep[i], &g); }
    CHECK(memcmp(steep, with, sizeof with) == 0); /* the same bits as with the spur on the mild hill */
    g_meetSpur = 0.5f; /* 26.6 degrees: steeper than any link may climb */
    CHECK(CoXlTestGradeRoads(pts, counts, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[5] <= 0.3250);
    for (uint32_t i = 0; i < 68; i++) { float xy[2], g; CoXlTestGradeStationPin(i, xy, &steep[i], &g); }
    CHECK(memcmp(steep, with, sizeof with) == 0);
    {
        /* the steepness sits on the spur past the bed's edge, and the grade turns by a step a station */
        float turn = 0.0f, last = 0.0f, before = 0.0f, most = 0.0f; int have = 0;
        for (uint32_t i = 68 + 31; i-- > 68;)
        {
            float xy[2], t, g;
            if (!(CoXlTestGradeStationPin(i, xy, &t, &g) & 4)) continue;
            if (have >= 1)
            {
                const float grade = atanf((t - last) / 10.0f) * 57.29578f;
                if (have >= 2 && fabsf(grade - before) > turn) turn = fabsf(grade - before);
                if (fabsf(grade) > most) most = fabsf(grade);
                before = grade;
            }
            last = t; have++;
        }
        printf("  the spur on 26.6 degrees: the steepest link %.1f degrees, the sharpest turn of grade %.1f degrees a station\n", (double)most, (double)turn);
        CHECK(most > 17.0f && turn <= 7.5f);
    }
    CoXlTestGradeMeasures(FieldAt, NULL, pts, 68, m);
    printf("  the arterial there: %.2f degrees across its bed, %.2f on 2 m\n", m[5], m[2]);
    CHECK(m[5] <= bare + 0.05 && m[2] <= 7.5);

    g_case = "a short spur to a pinned end: the steps give way by what is lacking between pins that disagree";
    g_meetAlong = 0.0f; g_meetSpur = 0.2f; /* the spur's end stands 8 m over the road, 30 m past the bed: 14.9 degrees */
    {
        static float brief[2 * 73]; static const uint32_t two[2] = {68, 5};
        const float pad[6] = {1200.0f - 2048.0f, 2040.0f - 2048.0f, 3.0f, 1.0f, 1.0f, 0.0f};
        float h[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        memcpy(brief, pts, sizeof(float) * 2 * 68);
        for (uint32_t i = 0; i < 5; i++) { brief[2 * (68 + i)] = 1200.0f; brief[2 * (68 + i) + 1] = 2040.0f - 10.0f * (float)i; }
        CoXlTestGradePads(pad, 1);
        CHECK(CoXlTestGradeRoads(brief, two, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[2] == 0.0 && out[5] > 0.2 && out[5] <= 0.3250);
        for (uint32_t i = 0; i < 68 + 5; i++)
        {
            float xy[2], t, g;
            const int marks = CoXlTestGradeStationPin(i, xy, &t, &g);
            if (!(marks & 4)) CHECK(t == 100.0f); /* the arterial on its level ground, to the bit */
            if (xy[1] > 2039.5f) CHECK((marks & 1) && fabsf(t - 108.0f) < 0.01f);
            if (xy[1] > 2000.5f) h[(int)((xy[1] - 2000.0f) / 10.0f + 0.5f)] = t;
        }
        /* 7 + 14 + 18 degrees over the three links reach 6.97 m: the outer step goes to 18, the inner takes the rest */
        printf("  the short spur: %.2f, %.2f, %.2f m over its three links\n", (double)(h[2] - h[1]), (double)(h[3] - h[2]), (double)(h[4] - h[3]));
        CHECK(fabsf(h[1] - 100.0f) < 0.01f && h[2] - h[1] > 1.3f && h[2] - h[1] < 1.7f && h[3] - h[2] > 3.0f && h[3] - h[2] <= 3.26f);
        g_case = "a spur's pinned end no link can reach: the arterial stands, the pin is missed by the whole lack, no step at the junction";
        g_meetSpur = 0.8f; /* 32 m over the road, 40 m away */
        CHECK(CoXlTestGradeRoads(brief, two, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[2] == 1.0 && out[3] > 18.0);
        float before = 100.0f;
        for (uint32_t i = 68 + 5; i-- > 0;)
        {
            float xy[2], t, g;
            const int marks = CoXlTestGradeStationPin(i, xy, &t, &g);
            if (!(marks & 4)) { CHECK(t == 100.0f); continue; }
            if (marks & 8) { CHECK(fabsf(t - 100.0f) < 0.01f); continue; } /* in the bed: the arterial's height */
            CHECK(t - before <= 0.3250f * 10.0f + 1.0e-3f && t - before > 2.5f); /* from the bed's edge up: each link at its limit */
            before = t;
        }
        CHECK(before > 109.0f && before < 110.5f); /* 30 m at 18 degrees is 9.75 m: the pad at 132 m is not met */
        CoXlTestGradePads(pad, 0);
    }
    g_case = "an arterial on ground steeper than 7 degrees keeps its steeper links when a spur hangs on it";
    g_meetAlong = 0.3f; g_meetSpur = 0.0f;
    { static const uint32_t one[1] = {68}; CHECK(CoXlTestGradeRoads(pts, one, ends, 1, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[4] >= 60.0); }
    const double relaxed = out[4];
    CHECK(CoXlTestGradeRoads(pts, counts, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[4] >= relaxed);
    g_case = "an arterial's corner: the pieces in each other's bed are level, also with a spur's junction 12 m from it";
    {
        static float bent[2 * 100], cornerAlone[68], t, g; float xy[2];
        static const uint32_t one[1] = {68}, two[2] = {68, 31};
        g_meetAlong = 0.06f; g_meetSpur = 0.0f;
        for (uint32_t i = 0; i < 68; i++) { bent[2 * i] = i < 34 ? 999.0f + 6.0f * (float)i : 1197.0f; bent[2 * i + 1] = i < 34 ? 2000.0f : 2000.0f - 6.0f * (float)(i - 33); }
        for (uint32_t i = 0; i < 31; i++) { bent[2 * (68 + i)] = 1185.0f; bent[2 * (68 + i) + 1] = 2300.0f - 10.0f * (float)i; }
        CHECK(CoXlTestGradeRoads(bent, one, ends, 1, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 1.0 && out[1] >= 2.0 && out[1] <= 4.0);
        const double links = out[1];
        for (uint32_t i = 0; i < 68; i++) { const int marks = CoXlTestGradeStationPin(i, xy, &cornerAlone[i], &g); CHECK(((marks & 16) != 0) == (i >= 31 && i <= 35) || i == 31 || i == 35); }
        CHECK(fabsf(cornerAlone[33] - cornerAlone[32]) <= 0.0087269f * 6.0f + 1.0e-3f && fabsf(cornerAlone[30] - cornerAlone[27]) > 0.03f * 18.0f);
        CoXlTestGradeMeasures(FieldAt, NULL, bent, 68, m);
        printf("  a 90 degree corner on 3.4 degrees: %.2f degrees across the bed (tilt %.2f), %.2f on 2 m\n", m[5], m[1], m[2]);
        CHECK(m[5] <= 4.0 && m[2] <= 7.5);
        CHECK(CoXlTestGradeRoads(bent, two, ends, 2, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 1.0 && out[1] == links);
        float moved2 = 0.0f;
        for (uint32_t i = 0; i < 68; i++) { CoXlTestGradeStationPin(i, xy, &t, &g); if (fabsf(t - cornerAlone[i]) > moved2) moved2 = fabsf(t - cornerAlone[i]); }
        CHECK(moved2 < 0.02f);
    }
    g_case = "a spur tied to a second arterial far above it moves neither arterial: a settled station stands";
    {
        static float both[2 * 180], lone[136]; float xy[2], t, g; uint32_t untied = 0;
        static const uint32_t two[2] = {68, 68}, three[3] = {68, 68, 6};
        static const uint32_t six[6] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        g_meetAlong = 0.0f; g_meetSpur = 0.5f; /* the second arterial runs 60 m north, 30 m higher */
        memcpy(both, pts, sizeof(float) * 2 * 34);
        for (uint32_t i = 0; i < 68; i++) { both[2 * i] = 999.0f + 6.0f * (float)i; both[2 * i + 1] = 2000.0f; both[2 * (68 + i)] = 1002.0f + 6.0f * (float)i; both[2 * (68 + i) + 1] = 2060.0f; }
        for (uint32_t i = 0; i < 6; i++) { both[2 * (136 + i)] = 1200.0f; both[2 * (136 + i) + 1] = i < 5 ? 2045.0f - 10.0f * (float)i : 2000.0f; }
        CHECK(CoXlTestGradeRoads(both, two, six, 2, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0);
        for (uint32_t i = 0; i < 136; i++) CoXlTestGradeStationPin(i, xy, &lone[i], &g);
        CHECK(CoXlTestGradeRoads(both, three, six, 3, 1, GroundMeet, 4096.0f, 4000.0f, out) >= 0);
        CHECK(CoXlTestGradeTies(&untied) >= 1 && untied == 0); /* the spur's start is tied to the upper arterial, 15 m from it */
        float moved3 = 0.0f, spurTop = 0.0f;
        for (uint32_t i = 0; i < 136 + 8; i++)
        {
            const int marks = CoXlTestGradeStationPin(i, xy, &t, &g);
            if (marks & 4) { if (xy[1] > 2044.0f) spurTop = t; continue; }
            if (xy[0] == 1200.0f && xy[1] == 2000.0f) continue; /* the junction's own station */
            if (i < 136 && fabsf(t - lone[i]) > moved3) moved3 = fabsf(t - lone[i]);
        }
        printf("  a spur between arterials 30 m apart in height: the arterials move %.3f m, the spur's top stands at %.1f m\n", (double)moved3, (double)spurTop);
        CHECK(moved3 < 0.02f && spurTop > 100.0f && spurTop < 129.0f);
    }
    g_meetAlong = 0.06f; g_meetSpur = 0.11f;

    g_case = "junction: a road that only bends gently has no landing";
    { static const uint32_t one[1] = {68}; CHECK(CoXlTestGradeRoads(pts, one, ends, 1, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 0.0 && out[1] == 0.0); }

    g_case = "junction: a corner of over 30 degrees is one";
    for (uint32_t i = 34; i < 68; i++) { pts[2 * i] = pts[2 * 33] + 4.0f * (float)(i - 33); pts[2 * i + 1] = 2000.0f + 4.5f * (float)(i - 33); }
    { static const uint32_t one[1] = {68}; CHECK(CoXlTestGradeRoads(pts, one, ends, 1, 0, GroundMeet, 4096.0f, 4000.0f, out) >= 0 && out[0] == 1.0 && out[1] >= 2.0 && out[1] <= 4.0); }
}

/* ---- pads the hard limit cannot join ---- */
static void PadsApart(void)
{
    enum { K = 24 };
    static float target[K]; double out[7];
    const uint32_t count = Chain(K);
    for (uint32_t i = 0; i < K; i++) g_gaps[i] = 6.0f;

    g_case = "two pads 96 m apart and 14 m in height: the road between them climbs more than 7 degrees and meets both";
    for (uint32_t i = 0; i < K; i++)
    {
        g_pinned[i] = i < 4 || i >= 20;
        g_ground[i] = i < 4 ? 10.0f : i >= 20 ? 24.0f : 10.0f + 14.0f * (float)(i - 3) / 17.0f;
    }
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, NULL, K, g_links, g_gaps, count, 0.0787017f, 0.1227846f, 0.3249197f, 4.0f, 16, target, out) == 0);
    CHECK(fabsf(target[3] - 10.0f) < 0.01f && fabsf(target[20] - 24.0f) < 0.01f && fabsf(target[0] - 10.0f) < 0.01f);
    CHECK(out[0] >= 16.0 && out[4] > 8.3 && out[4] < 12.5); /* the 16 links between them may; 14 m in 96 m is 8.3 degrees, steeper at the pads where the worn path eases in */
    for (uint32_t i = 4; i < 20; i++) CHECK(fabsf(target[i] - g_ground[i]) < 1.1f);

    g_case = "the same pads with the hard limit alone: both are missed, as before";
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, NULL, K, g_links, g_gaps, count, 0.0787017f, 0.1227846f, 0.1227846f, 4.0f, 16, target, out) > 0);
    CHECK(target[3] - 10.0f > 0.5f && 24.0f - target[20] > 0.5f);

    g_case = "pads further apart in height than 18 degrees joins stay missed and counted";
    for (uint32_t i = 20; i < K; i++) g_ground[i] = 60.0f;
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, NULL, K, g_links, g_gaps, count, 0.0787017f, 0.1227846f, 0.3249197f, 4.0f, 16, target, out) > 0);
    CHECK(target[3] - 10.0f > 1.0f && out[4] <= 18.05);

    g_case = "pads that need every link at 18 degrees are met: the links at the pads' own edges relax too";
    for (uint32_t i = 0; i < K; i++) g_ground[i] = i < 4 ? 10.0f : i >= 20 ? 40.0f : 10.0f + 30.0f * (float)(i - 3) / 17.0f;
    CHECK(CoXlTestGradeSteep(g_ground, g_pinned, NULL, K, g_links, g_gaps, count, 0.0787017f, 0.1227846f, 0.3249197f, 4.0f, 16, target, out) == 0);
    CHECK(fabsf(target[3] - 10.0f) < 0.01f && fabsf(target[20] - 40.0f) < 0.01f); /* 30 m in 96 m: 31.2 m at most */
    memset(g_pinned, 0, sizeof g_pinned);
}

int main(void)
{
    Meet();
    PadsApart();
    Pins();
    Banks();
    Flat();
    Cliff();
    Pads();
    PinsApart();
    Junction();
    Crest();
    Reference();
    Field();
    Slopes();
    Draw();
    Together();
    Capped();
    CappedPins();
    Steep();
    Zone();
    Site();
    Measures();
    Detail();
    Shoulder();
    InPlace();
    if (g_failed) { printf("roadgrade: %d of %d check(s) failed\n", g_failed, g_checked); return 1; }
    printf("roadgrade: all checks passed (%d)\n", g_checked);
    return 0;
}
