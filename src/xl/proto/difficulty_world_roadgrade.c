/* ---- Roads: the graded profile (game-free) --------------------------------------------------------------------------
 * A road is drivable when its height changes slowly along it. The profile is solved on the road graph (stations a few
 * metres apart, joined along arterials and spurs, a junction being one station), so roads that meet agree on a height.
 * - Grade: the target is the mean of the steepest-allowed surface below the ground and the one above it. That is the
 *   profile within the limit that strays least from the ground at its worst station (cut and fill balance).
 * - Pins: a station on a pad keeps the ground's height, and the target is clamped to what the pins allow around
 *   them. Pins too far apart in height for the road between them cannot be met: the road keeps the limit and passes
 *   each of the two by half of what is lacking. Those stations are counted, and the caller reports the place.
 * - Floor: a station may carry a height it must stay above. The range the pins give is raised to the lowest surface
 *   over the floors that keeps the slope, so the road climbs out of a low place within the limit.
 * - Bends: crests and dips are eased by a fixed count of averaging passes, the grade enforced after each. */
#define GRADE_LINKS 8
#define GRADE_FAR 1.0e30f
#define GRADE_ROUNDS 4096u
typedef struct GradeStation
{
    float ground, target, low, high;
    float floor; /* the target never lies under it (water); -GRADE_FAR: none */
    float cross; /* the ground's slope across the road here, as a tangent (sizes the side band) */
    float gap[GRADE_LINKS];
    uint32_t link[GRADE_LINKS];
    uint8_t links, pinned;
} GradeStation;

static int GradeLink(GradeStation* s, uint32_t a, uint32_t b, float metres)
{
    if (a == b || !(metres > 0.0f) || s[a].links >= GRADE_LINKS || s[b].links >= GRADE_LINKS) return 0;
    s[a].link[s[a].links] = b; s[a].gap[s[a].links++] = metres;
    s[b].link[s[b].links] = a; s[b].gap[s[b].links++] = metres;
    return 1;
}

/* v becomes the highest surface under it (side < 0) or the lowest over it (side > 0) that keeps the slope. */
static int GradeBound(float* v, const GradeStation* s, uint32_t n, float slope, int side)
{
    for (uint32_t round = 0; round < GRADE_ROUNDS; round++)
    {
        int changed = 0;
        for (uint32_t pass = 0; pass < 2 * n; pass++)
        {
            const uint32_t i = pass < n ? pass : 2 * n - 1 - pass;
            for (uint32_t k = 0; k < s[i].links; k++)
            {
                const float reach = v[s[i].link[k]] - (float)side * slope * s[i].gap[k];
                if (side < 0 ? reach < v[i] : reach > v[i]) { v[i] = reach; changed = 1; }
            }
        }
        if (!changed) return 1;
    }
    return 0;
}

static void GradeWithin(GradeStation* s, uint32_t n, float slope, float* under, float* over)
{
    for (uint32_t i = 0; i < n; i++) under[i] = over[i] = s[i].target;
    GradeBound(under, s, n, slope, -1);
    GradeBound(over, s, n, slope, 1);
    for (uint32_t i = 0; i < n; i++) s[i].target = 0.5f * (under[i] + over[i]);
}

/* What the pins allow at every station (low, high); returns the stations no profile can serve: the pins around them
 * differ by more than the road between them may climb. */
static uint32_t GradePins(GradeStation* s, uint32_t n, float slope, float* under, float* over)
{
    uint32_t torn = 0;
    for (uint32_t i = 0; i < n; i++) { over[i] = s[i].pinned ? s[i].ground : GRADE_FAR; under[i] = s[i].pinned ? s[i].ground : s[i].floor; }
    GradeBound(over, s, n, slope, -1);
    GradeBound(under, s, n, slope, 1);
    for (uint32_t i = 0; i < n; i++) { s[i].low = under[i]; s[i].high = over[i]; torn += under[i] > over[i]; }
    return torn;
}

/* The target moved into what the pins allow. Where the pins disagree (low > high) it is their middle, at a pinned
 * station too: the mean of the two clamps is as gentle as its inputs, so no step stands beside a pin the road
 * cannot reach, and the pin is missed by half of what the limit lacks (GradeMissed). */
static void GradeClamp(GradeStation* s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        const float t = s[i].target, up = t > s[i].low ? t : s[i].low, down = t < s[i].high ? t : s[i].high;
        s[i].target = 0.5f * ((down > s[i].low ? down : s[i].low) + (up < s[i].high ? up : s[i].high));
    }
}

/* The pinned stations the profile leaves by more than `by` metres, and the furthest of them (*worst, *at). */
static uint32_t GradeMissed(const GradeStation* s, uint32_t n, float by, float* worst, uint32_t* at)
{
    uint32_t missed = 0;
    *worst = 0.0f; *at = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const float off = fabsf(s[i].target - s[i].ground);
        if (!s[i].pinned || !(off > by)) continue;
        missed++;
        if (off > *worst) { *worst = off; *at = i; }
    }
    return missed;
}

/* The sharpest change of grade at a station with two links, as a slope difference (rise over run). */
static float GradeBend(const GradeStation* s, uint32_t n)
{
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; i++)
    {
        if (s[i].links != 2) continue;
        const float in = (s[i].target - s[s[i].link[0]].target) / s[i].gap[0], out = (s[s[i].link[1]].target - s[i].target) / s[i].gap[1];
        if (fabsf(out - in) > worst) worst = fabsf(out - in);
    }
    return worst;
}

/* slope is a rise over run. The ground is first moved into what the pins allow, so a road climbs to a pad instead of
 * dipping after it. Every step (clamp, the two surfaces' mean, the fixed count of easing passes) moves the result no
 * further than its input moved: peers whose captures differ by a rounding get profiles that differ by no more, and no
 * step branches on a height. Returns the torn stations (a count for the report only). */
static uint32_t GradeSolve(GradeStation* s, uint32_t n, float slope, uint32_t passes)
{
    float* under = (float*)malloc(sizeof(float) * 3 * (n ? n : 1));
    if (under == NULL) { for (uint32_t i = 0; i < n; i++) s[i].target = s[i].ground; return n; }
    float* over = under + n; float* next = over + n;
    const uint32_t torn = GradePins(s, n, slope, under, over);
    for (uint32_t i = 0; i < n; i++) s[i].target = s[i].ground;
    GradeClamp(s, n);
    GradeWithin(s, n, slope, under, over);
    GradeClamp(s, n);
    for (uint32_t ease = 0; ease < passes; ease++)
    {
        for (uint32_t i = 0; i < n; i++)
        {
            next[i] = s[i].target;
            if (s[i].links != 2 || s[i].pinned) continue;
            const float a = s[i].gap[0], b = s[i].gap[1];
            const float line = (s[s[i].link[0]].target * b + s[s[i].link[1]].target * a) / (a + b);
            next[i] = 0.5f * (s[i].target + line);
        }
        for (uint32_t i = 0; i < n; i++) s[i].target = next[i];
        GradeWithin(s, n, slope, under, over);
        GradeClamp(s, n);
    }
    free(under);
    return torn;
}

typedef struct GradeStats { float steepest, bend, cut, fill; double metres, over5, over8, cut8, cut15; } GradeStats;
/* steepest and bend in degrees; cut and fill the deepest and highest in metres; the rest metres of road. A link's
 * cut or fill is its ends' larger one. */
static void GradeMeasure(const GradeStation* s, uint32_t n, GradeStats* out)
{
    const float deg = 57.29578f;
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < n; i++)
    {
        const float move = s[i].target - s[i].ground;
        if (-move > out->cut) out->cut = -move;
        if (move > out->fill) out->fill = move;
        for (uint32_t k = 0; k < s[i].links; k++)
        {
            const uint32_t j = s[i].link[k];
            if (j < i) continue;
            const float grade = atanf(fabsf(s[j].target - s[i].target) / s[i].gap[k]) * deg;
            const float other = fabsf(s[j].target - s[j].ground), moved = fabsf(move) > other ? fabsf(move) : other;
            out->metres += s[i].gap[k];
            if (grade > out->steepest) out->steepest = grade;
            if (grade > 5.0f) out->over5 += s[i].gap[k];
            if (grade > 8.0f) out->over8 += s[i].gap[k];
            if (moved > 8.0f) out->cut8 += s[i].gap[k];
            if (moved > 15.0f) out->cut15 += s[i].gap[k];
        }
    }
    out->bend = atanf(GradeBend(s, n)) * deg;
}
