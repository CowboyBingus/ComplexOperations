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
 * - Cap: a worn path follows the land. The gentle profile is pulled back to within each station's `earth` of the
 *   ground before the steeper hard limit is enforced, so deep cuts are made only where the hard limit needs them.
 * - Steep ground: where the hard limit would stand the road further off the ground than the cap again (a causeway
 *   down a hillside), the links there may climb more, up to `steep` (GradeRelax), and the profile is solved once
 *   more. A link between two pins has no limit: the pad's own ground holds there and no road is drawn.
 * - Pads that disagree: where two pins stand further apart in height than the limit joins, the links between them
 *   may climb more too (GradeRelax), so the road meets both pads and no step is left at a pad's edge.
 * - Landings: a link may carry a slope of its own it never exceeds (`most`), relaxed or not. The roads about a
 *   junction are held near level with it, so one road's grade is not the other's cross slope where their beds meet.
 * - Arterials first: a station may be marked `later` (a spur's). The others are solved without them and then stand;
 *   the later ones are solved against those heights (GradeSolveStaged). So a spur takes the whole difference to the
 *   road it joins, and no spur's hill lifts or tilts an arterial. A pin a settled station cannot reach comes to it.
 * - Bends: crests and dips are eased by a fixed count of averaging passes, the cap and the grade enforced after
 *   each, so easing rounds a crest only within the cap. */
#define GRADE_LINKS 8
#define GRADE_FAR 1.0e30f
#define GRADE_ROUNDS 4096u
#define GRADE_FREE 1.0e6f /* a link's own limit: none (between two pins) */
typedef struct GradeStation
{
    float ground, target, low, high;
    float floor; /* the target never lies under it (water); -GRADE_FAR: none */
    float cross; /* the ground's slope across the road here, as a tangent (sizes the side band) */
    float earth; /* GradeSolveCapped: the cut or fill the gentle profile may keep here; GRADE_FAR: any */
    float gap[GRADE_LINKS];
    float lim[GRADE_LINKS]; /* the slope this link may take where it is over the limit asked for (0: the limit) */
    float most[GRADE_LINKS]; /* the slope this link never exceeds (0: none): a junction's landing */
    uint32_t link[GRADE_LINKS];
    uint8_t links, pinned;
    uint8_t flat; /* it stands on a pad's flat part */
    uint8_t held; /* so do all the stations it is linked to: the pad's own ground holds there */
    uint8_t edge; /* a spur's end at its place's edge: it keeps its ground */
    uint8_t roads; /* its first links that are pieces of road; the rest tie it to a road beside it */
    uint8_t later; /* solved after the others, against their heights (a spur's own stations) */
    uint8_t fixed; /* GradeSolveStaged: its height is settled; `ground` holds it and no other pin moves it */
} GradeStation;

static int GradeLink(GradeStation* s, uint32_t a, uint32_t b, float metres)
{
    if (a == b || !(metres > 0.0f) || s[a].links >= GRADE_LINKS || s[b].links >= GRADE_LINKS) return 0;
    s[a].link[s[a].links] = b; s[a].gap[s[a].links++] = metres;
    s[b].link[s[b].links] = a; s[b].gap[s[b].links++] = metres;
    return 1;
}

/* v becomes the highest surface under it (side < 0) or the lowest over it (side > 0) that keeps the slope. */
static int GradeBound(float* v, const GradeStation* s, uint32_t n, float slope, int side, int relaxed)
{
    for (uint32_t round = 0; round < GRADE_ROUNDS; round++)
    {
        int changed = 0;
        for (uint32_t pass = 0; pass < 2 * n; pass++)
        {
            const uint32_t i = pass < n ? pass : 2 * n - 1 - pass;
            for (uint32_t k = 0; k < s[i].links; k++)
            {
                float most = relaxed && s[i].lim[k] > slope ? s[i].lim[k] : slope;
                if (s[i].most[k] > 0.0f && most > s[i].most[k] && !(relaxed && s[i].lim[k] >= GRADE_FREE)) most = s[i].most[k];
                const float reach = v[s[i].link[k]] - (float)side * most * s[i].gap[k];
                if (side < 0 ? reach < v[i] : reach > v[i]) { v[i] = reach; changed = 1; }
            }
        }
        if (!changed) return 1;
    }
    return 0;
}

static void GradeWithin(GradeStation* s, uint32_t n, float slope, float* under, float* over, int relaxed)
{
    for (uint32_t i = 0; i < n; i++) under[i] = over[i] = s[i].target;
    GradeBound(under, s, n, slope, -1, relaxed);
    GradeBound(over, s, n, slope, 1, relaxed);
    for (uint32_t i = 0; i < n; i++) s[i].target = 0.5f * (under[i] + over[i]);
}

/* What the pins allow at every station (low, high); returns the stations no profile can serve: the pins around them
 * differ by more than the road between them may climb. pin: room for n heights. settle: a pin the settled (fixed)
 * stations cannot reach is taken at the nearest height they can, so the whole lack falls on that pin and none on
 * them. Without it such a pin tears the stations between, which is what GradeRelax reads. */
static uint32_t GradePins(GradeStation* s, uint32_t n, float slope, float* under, float* over, float* pin, int settle)
{
    uint32_t torn = 0, fixed = 0;
    for (uint32_t i = 0; i < n; i++) { pin[i] = s[i].ground; fixed += s[i].fixed; }
    if (fixed != 0 && settle)
    {
        for (uint32_t i = 0; i < n; i++) { over[i] = s[i].fixed ? s[i].ground : GRADE_FAR; under[i] = s[i].fixed ? s[i].ground : -GRADE_FAR; }
        GradeBound(over, s, n, slope, -1, 1);
        GradeBound(under, s, n, slope, 1, 1);
        for (uint32_t i = 0; i < n; i++)
            if (s[i].pinned && !s[i].fixed) pin[i] = pin[i] > over[i] ? over[i] : pin[i] < under[i] ? under[i] : pin[i];
    }
    for (uint32_t i = 0; i < n; i++) { over[i] = s[i].pinned ? pin[i] : GRADE_FAR; under[i] = s[i].pinned ? pin[i] : s[i].floor; }
    GradeBound(over, s, n, slope, -1, 1);
    GradeBound(under, s, n, slope, 1, 1);
    for (uint32_t i = 0; i < n; i++)
    {
        if (s[i].fixed) under[i] = over[i] = s[i].ground; /* a settled station stands, whatever tears at it */
        s[i].low = under[i]; s[i].high = over[i]; torn += under[i] > over[i];
    }
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

/* The targets pulled to within each station's `earth` of its ground (a clamp is as continuous as its inputs). */
static void GradeCap(GradeStation* s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        const float low = s[i].ground - s[i].earth, high = s[i].ground + s[i].earth;
        s[i].target = s[i].target < low ? low : s[i].target > high ? high : s[i].target;
    }
}

/* From s[i].target as the start: moved into what the pins allow, so a road climbs to a pad instead of dipping after
 * it, then the limit and the easing. Every step (clamp, the two surfaces' mean, the fixed count of easing passes)
 * moves the result no further than its input moved: peers whose captures differ by a rounding get profiles that differ
 * by no more, and no step branches on a height. capped: an eased profile goes back into the cap before the limit, so
 * only the limit and the pins ever take a station past its cap. Returns the torn stations (for the report only). */
static uint32_t GradeSolveFrom(GradeStation* s, uint32_t n, float slope, uint32_t passes, float* under, int capped, int settle)
{
    float* over = under + n; float* next = over + n;
    const uint32_t torn = GradePins(s, n, slope, under, over, next, settle);
    GradeClamp(s, n);
    GradeWithin(s, n, slope, under, over, 1);
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
        if (capped) GradeCap(s, n);
        GradeWithin(s, n, slope, under, over, 1);
        GradeClamp(s, n);
    }
    return torn;
}

/* slope is a rise over run: the profile within it that starts from the ground. */
static uint32_t GradeSolve(GradeStation* s, uint32_t n, float slope, uint32_t passes)
{
    float* under = (float*)malloc(sizeof(float) * 3 * (n ? n : 1));
    for (uint32_t i = 0; i < n; i++) s[i].target = s[i].ground;
    if (under == NULL) return n;
    const uint32_t torn = GradeSolveFrom(s, n, slope, passes, under, 0, 1);
    free(under);
    return torn;
}

/* The links' own limits after a solve within `hard`: none between two pins; `hard` rising to `steep` as the worse
 * of a link's ends stands further than `cap` off its ground, whole at twice the cap, or as the pins about an end
 * disagree (low over high), whole at GRADE_TORN. Eased, and a function of the heights without a branch on them. */
#define GRADE_TORN 1.0f
static void GradeRelax(GradeStation* s, uint32_t n, float hard, float steep, float cap)
{
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = 0; k < s[i].links; k++)
        {
            const uint32_t j = s[i].link[k];
            if (s[i].pinned && s[j].pinned) { s[i].lim[k] = GRADE_FREE; continue; }
            const float a = s[i].pinned ? 0.0f : fabsf(s[i].target - s[i].ground) - cap, b = s[j].pinned ? 0.0f : fabsf(s[j].target - s[j].ground) - cap;
            const float ta = s[i].low - s[i].high, tb = s[j].low - s[j].high;
            float w = (a > b ? a : b) / cap, torn = (ta > tb ? ta : tb) / GRADE_TORN;
            w = w > torn ? w : torn;
            w = w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
            s[i].lim[k] = hard + (steep - hard) * w * w * (3.0f - 2.0f * w);
            /* a landing is level only where the land is: from half the cap off its ground it gives way to the
             * link's own limit, whole at the cap or between pins that disagree, so it builds no platform */
            if (k >= s[i].roads || !(s[i].most[k] > 0.0f)) continue;
            const float offA = s[i].pinned ? 0.0f : a + cap, offB = s[j].pinned ? 0.0f : b + cap;
            float u = ((offA > offB ? offA : offB) - 0.5f * cap) / (0.5f * cap);
            u = u > torn ? u : torn;
            u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
            if (s[i].lim[k] > s[i].most[k]) s[i].most[k] += (s[i].lim[k] - s[i].most[k]) * u * u * (3.0f - 2.0f * u);
        }
}
/* The worn path: the profile within `soft` wherever that keeps each station within its `earth` of the ground, and
 * within `hard` everywhere. The gentle profile is pulled into the cap and the hard limit then starts from that, so
 * it cuts deeper than the cap only where `hard` or a pin cannot be held without. */
static uint32_t GradeSolveCapped(GradeStation* s, uint32_t n, float soft, float hard, float steep, float cap, uint32_t passes)
{
    float* under = (float*)malloc(sizeof(float) * 3 * (n ? n : 1));
    uint32_t torn = n;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = 0; k < s[i].links; k++) s[i].lim[k] = s[i].pinned && s[s[i].link[k]].pinned ? GRADE_FREE : 0.0f;
    for (int again = 0; under != NULL && again < 2; again++)
    {
        for (uint32_t i = 0; i < n; i++) s[i].target = s[i].ground;
        GradeWithin(s, n, soft, under, under + n, 0);
        GradeCap(s, n);
        /* the first of two solves only measures what tears, for GradeRelax: its pins are taken as they are */
        const int last = again || !(steep > hard) || !(cap > 0.0f);
        torn = GradeSolveFrom(s, n, hard, passes, under, 1, last);
        if (last) break;
        GradeRelax(s, n, hard, steep, cap);
    }
    free(under);
    return torn;
}
/* Two stages: the stations not marked `later` among themselves, as if the later ones were not there; then all of
 * them with the first stage's heights standing. A link between two stations of the first stage keeps the limit
 * that stage gave it. Returns both stages' torn stations. Without a later station, or without memory, one solve. */
static uint32_t GradeSolveStaged(GradeStation* s, uint32_t n, float soft, float hard, float steep, float cap, uint32_t passes)
{
    uint32_t first = 0;
    for (uint32_t i = 0; i < n; i++) first += !s[i].later;
    uint32_t* map = first != 0 && first != n ? (uint32_t*)malloc(4 * (size_t)n) : NULL;
    GradeStation* a = map != NULL ? (GradeStation*)malloc(sizeof *a * first) : NULL;
    if (a == NULL) { free(map); return GradeSolveCapped(s, n, soft, hard, steep, cap, passes); }
    for (uint32_t i = 0, m = 0; i < n; i++) map[i] = s[i].later ? 0xFFFFFFFFu : m++;
    for (uint32_t i = 0; i < n; i++)
    {
        if (s[i].later) continue;
        GradeStation* t = &a[map[i]];
        *t = s[i]; t->links = 0;
        for (uint32_t k = 0; k < s[i].links; k++)
        {
            if (s[s[i].link[k]].later) continue;
            t->link[t->links] = map[s[i].link[k]]; t->gap[t->links] = s[i].gap[k]; t->most[t->links++] = s[i].most[k];
        }
    }
    uint32_t torn = GradeSolveCapped(a, first, soft, hard, steep, cap, passes);
    /* the second stage: `a` keeps each settled station's own ground and pin for after it */
    for (uint32_t i = 0; i < n; i++)
    {
        if (s[i].later) continue;
        s[i].ground = a[map[i]].target; s[i].pinned = 1; s[i].fixed = 1;
    }
    torn += GradeSolveCapped(s, n, soft, hard, steep, cap, passes);
    for (uint32_t i = 0; i < n; i++)
    {
        if (s[i].later) continue;
        const GradeStation* t = &a[map[i]];
        s[i].ground = t->ground; s[i].pinned = t->pinned; s[i].fixed = 0; s[i].low = t->low; s[i].high = t->high;
        for (uint32_t k = 0, q = 0; k < s[i].links; k++) if (!s[s[i].link[k]].later) s[i].lim[k] = t->lim[q++];
    }
    free(a); free(map);
    return torn;
}
/* The links that may climb more than `hard`: their count, metres and the steepest the profile takes on one (as a
 * slope, with the station at its lower-numbered end). */
static uint32_t GradeRelaxed(const GradeStation* s, uint32_t n, float hard, double* metres, float* steepest, uint32_t* at)
{
    uint32_t count = 0;
    *metres = 0.0; *steepest = 0.0f; *at = 0;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = 0; k < s[i].links; k++)
        {
            const uint32_t j = s[i].link[k];
            if (j < i || !(s[i].lim[k] > hard * 1.001f) || s[i].lim[k] >= GRADE_FREE) continue;
            count++; *metres += s[i].gap[k];
            const float grade = fabsf(s[j].target - s[i].target) / s[i].gap[k];
            if (grade > *steepest) { *steepest = grade; *at = i; }
        }
    return count;
}

/* The stations the profile moved more than their cap and `by` metres (the hard limit needed it), the furthest of
 * them (*worst metres past the cap, *at) and the metres of road at them. */
static uint32_t GradeOverCap(const GradeStation* s, uint32_t n, float by, float* worst, uint32_t* at, double* metres)
{
    uint32_t count = 0;
    *worst = 0.0f; *at = 0; *metres = 0.0;
    for (uint32_t i = 0; i < n; i++)
    {
        const float past = fabsf(s[i].target - s[i].ground) - s[i].earth;
        if (s[i].pinned || !(past > by)) continue;
        count++;
        for (uint32_t k = 0; k < s[i].links; k++) *metres += 0.5 * s[i].gap[k];
        if (past > *worst) { *worst = past; *at = i; }
    }
    return count;
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
            if (j < i || s[i].lim[k] >= GRADE_FREE) continue; /* between two pins the pad's ground holds: no road */
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
