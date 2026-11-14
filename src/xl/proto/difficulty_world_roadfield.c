/* ---- Roads: the graded field (game-free) ---------------------------------------------------------------------------
 * What the level's draw and a close tile take from the solved profile: at any point the height the roads set there and
 * how much of it. Stations lie on the roads' points; a route node and a spur's meeting point are one station, so
 * roads that meet share a height. The field is single-valued: where the bands of two roads reach the same ground the
 * height is their mean by nearness and weight, so no piece of ground is set twice to different heights (the level's
 * blend is not commutative). It is a continuous function of the targets and never chooses by a height.
 * - Weight: 1 to GRADE_FULL from a centreline (the core and one step of the draw's lattice, so the core is held
 *   whole), then down to 0 at the station's band (GradeBandWeight). The level's shader and a close tile ease it
 *   (GradeSmooth).
 * - Band: wide enough that the blend adds at most GRADE_SIDE to the ground's own side slope and bends at most
 *   GRADE_BEND at the bed's edge (GradeBandRamp). A worn path has shoulders, not banks. */
#define GRADE_NONE 0xFFFFFFFFu
#define GRADE_CORE 8.0f
#define GRADE_FULL 10.0f
#define GRADE_BAND_LEAST 24.0f /* a shoulder this wide bends under 10 degrees a 4 m span at its foot and top */
#define GRADE_BAND_MOST 160.0f
#define GRADE_SIDE 0.2125566f /* tan 12 degrees */
#define GRADE_BEND 0.0440817f /* tan 10 degrees over a 4 m span: the change of side slope a metre */
#define GRADE_JOIN 1.0f       /* a point this close a station is that station */
#define GRADE_CELL 64.0f
#define GRADE_REACH 8.0f      /* past its band a road is still listed this much further: a lattice cell's far corner */
typedef struct GradeSeg { float ax, ay, bx, by; uint32_t a, b; } GradeSeg;
typedef struct GradeField
{
    GradeStation* s; float* xy; float* band;
    GradeSeg* seg;
    uint32_t* start; uint32_t* item;
    uint32_t stations, stationRoom, segs, segRoom, unlinked, capped, ties, untied;
    int grid;
} GradeField;

static void GradeFieldFree(GradeField* f)
{
    free(f->s); free(f->xy); free(f->band); free(f->seg); free(f->start); free(f->item);
    memset(f, 0, sizeof *f);
}

static uint32_t GradeFieldStation(GradeField* f, float x, float y)
{
    if (f->stations == f->stationRoom)
    {
        const uint32_t room = f->stationRoom ? 2 * f->stationRoom : 4096;
        GradeStation* s = (GradeStation*)realloc(f->s, sizeof *s * room);
        if (s != NULL) f->s = s;
        float* xy = (float*)realloc(f->xy, 8 * (size_t)room);
        if (xy != NULL) f->xy = xy;
        if (s == NULL || xy == NULL) return GRADE_NONE;
        f->stationRoom = room;
    }
    memset(&f->s[f->stations], 0, sizeof *f->s);
    f->s[f->stations].floor = -GRADE_FAR; f->s[f->stations].earth = GRADE_FAR;
    f->xy[2 * f->stations] = x; f->xy[2 * f->stations + 1] = y;
    return f->stations++;
}

static int GradeFieldSeg(GradeField* f, uint32_t a, uint32_t b)
{
    if (a == GRADE_NONE || b == GRADE_NONE || a == b) return 0;
    if (f->segs == f->segRoom)
    {
        const uint32_t room = f->segRoom ? 2 * f->segRoom : 4096;
        GradeSeg* seg = (GradeSeg*)realloc(f->seg, sizeof *seg * room);
        if (seg == NULL) return 0;
        f->seg = seg; f->segRoom = room;
    }
    const GradeSeg g = {f->xy[2 * a], f->xy[2 * a + 1], f->xy[2 * b], f->xy[2 * b + 1], a, b};
    f->seg[f->segs++] = g;
    return 1;
}

static float GradeFar(const GradeField* f, uint32_t at, float x, float y)
{
    const float dx = f->xy[2 * at] - x, dy = f->xy[2 * at + 1] - y;
    return sqrtf(dx * dx + dy * dy);
}

/* A road's n points in path order. *first and *last name the stations its ends share with other roads (GRADE_NONE:
 * made here and written back). A point within GRADE_JOIN of the station before it is that station. */
static void GradeFieldChain(GradeField* f, const float* pts, uint32_t n, uint32_t* first, uint32_t* last)
{
    if (n < 2) return;
    if (*first == GRADE_NONE) *first = GradeFieldStation(f, pts[0], pts[1]);
    uint32_t before = *first;
    for (uint32_t p = 1; p < n && before != GRADE_NONE; p++)
    {
        const float x = pts[2 * p], y = pts[2 * p + 1];
        uint32_t at;
        if (p + 1 == n)
        {
            if (*last == GRADE_NONE) *last = GradeFar(f, before, x, y) < GRADE_JOIN ? before : GradeFieldStation(f, x, y);
            at = *last;
        }
        else
        {
            if (GradeFar(f, before, x, y) < GRADE_JOIN) continue;
            at = GradeFieldStation(f, x, y);
        }
        GradeFieldSeg(f, before, at);
        before = at;
    }
}

/* The station where a spur meets the roads made so dist (the first `among` pieces): the nearest piece's end when the
 * point is within GRADE_JOIN of it, else a new station that parts the piece. GRADE_NONE when there is no piece. */
static uint32_t GradeFieldAttach(GradeField* f, float x, float y, uint32_t among)
{
    float best = GRADE_FAR, bestT = 0.0f; uint32_t at = GRADE_NONE;
    for (uint32_t k = 0; k < among && k < f->segs; k++)
    {
        const GradeSeg* g = &f->seg[k];
        const float dx = g->bx - g->ax, dy = g->by - g->ay, l2 = dx * dx + dy * dy;
        float t = l2 > 1.0e-9f ? ((x - g->ax) * dx + (y - g->ay) * dy) / l2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float qx = g->ax + t * dx - x, qy = g->ay + t * dy - y, d = qx * qx + qy * qy;
        if (d < best) { best = d; bestT = t; at = k; }
    }
    if (at == GRADE_NONE) return GRADE_NONE;
    const GradeSeg g = f->seg[at];
    const float length = sqrtf((g.bx - g.ax) * (g.bx - g.ax) + (g.by - g.ay) * (g.by - g.ay));
    if (bestT * length < GRADE_JOIN) return g.a;
    if ((1.0f - bestT) * length < GRADE_JOIN) return g.b;
    const uint32_t m = GradeFieldStation(f, g.ax + bestT * (g.bx - g.ax), g.ay + bestT * (g.by - g.ay));
    if (m == GRADE_NONE) return g.a;
    f->seg[at].bx = f->xy[2 * m]; f->seg[at].by = f->xy[2 * m + 1]; f->seg[at].b = m;
    GradeFieldSeg(f, m, g.b);
    return m;
}

/* Once every road is in: the solver's links, a piece each. unlinked counts the pieces a station had no room for. */
static void GradeFieldLinks(GradeField* f)
{
    f->unlinked = 0;
    for (uint32_t k = 0; k < f->segs; k++)
    {
        const GradeSeg* g = &f->seg[k];
        f->unlinked += !GradeLink(f->s, g->a, g->b, sqrtf((g->bx - g->ax) * (g->bx - g->ax) + (g->by - g->ay) * (g->by - g->ay)));
    }
    for (uint32_t i = 0; i < f->stations; i++) f->s[i].roads = f->s[i].links;
}

/* Roads side by side that share no station (a fan of spurs into one junction, a spur along an arterial) would each
 * take their own height, and the field's hand-over between two lines is up to 2.6 times as steep as their heights'
 * difference over their distance, in the middle of both beds. So after the links, two stations within GRADE_TIE are
 * tied when each is the other's nearest along its road and the way between them along the roads is over
 * GRADE_TIE_PATH times their distance (not a road's own next stations, not arms that meet at 60 degrees or more: a
 * landing holds those). A tie holds the two within GRADE_TIE_SLOPE across their distance, whatever a link may
 * climb; past GRADE_TIE_BED apart, where the hand-over leaves the beds, it loosens fast. A pair with a pinned end
 * is tied only within GRADE_TIE_NEAR and by a link like any other: a pad's height is not the road's to choose. It
 * reads positions and pins only. ties counts them, untied the pairs a full station had no room for. */
#define GRADE_TIE 24.0f
#define GRADE_TIE_NEAR 8.0f
#define GRADE_TIE_RATIO 0.66f
#define GRADE_TIE_LEAST 0.5f
#define GRADE_TIE_SLOPE 0.0174551f /* tan 1 degree: read across a bed through the hand-over, 2.6 degrees at most */
#define GRADE_TIE_BED 12.0f        /* two beds' cores side by side */
#define GRADE_TIE_PATH 2.0f
#define GRADE_TIE_SEEN 128u
#define GRADE_TIE_HASH 16384u
static uint32_t GradeTieCell(int cx, int cy) { return ((uint32_t)cx * 73856093u ^ (uint32_t)cy * 19349663u) % GRADE_TIE_HASH; }
/* The stations within `most` metres of `from` along the roads, nearest first, and how far (id and len: room for
 * GRADE_TIE_SEEN). */
static uint32_t GradeTieReach(const GradeField* f, uint32_t from, float most, uint32_t* id, float* len)
{
    uint32_t n = 1, done = 0;
    id[0] = from; len[0] = 0.0f;
    while (done < n)
    {
        uint32_t best = done;
        for (uint32_t q = done + 1; q < n; q++) if (len[q] < len[best]) best = q;
        const uint32_t at = id[best]; const float gone = len[best];
        id[best] = id[done]; len[best] = len[done]; id[done] = at; len[done++] = gone;
        const GradeStation* s = &f->s[at];
        for (uint32_t k = 0; k < s->roads; k++)
        {
            const float far_ = gone + s->gap[k];
            uint32_t q = 0;
            if (!(far_ < most)) continue;
            while (q < n && id[q] != s->link[k]) q++;
            if (q < n) { if (q >= done && far_ < len[q]) len[q] = far_; }
            else if (n < GRADE_TIE_SEEN) { id[n] = s->link[k]; len[n++] = far_; }
        }
    }
    return n;
}
/* 1 when no station next to `of` along its road is as near `to` as `of` is. */
static int GradeTieFoot(const GradeField* f, uint32_t of, uint32_t to, float far_)
{
    for (uint32_t k = 0; k < f->s[of].roads; k++)
        if (f->s[of].link[k] != to && GradeFar(f, f->s[of].link[k], f->xy[2 * to], f->xy[2 * to + 1]) <= far_) return 0;
    return 1;
}
static void GradeFieldTies(GradeField* f)
{
    f->ties = f->untied = 0;
    uint32_t* head = (uint32_t*)malloc(4 * (size_t)(GRADE_TIE_HASH + (f->stations ? f->stations : 1)));
    if (head == NULL) return;
    uint32_t* next = head + GRADE_TIE_HASH;
    memset(head, 0xFF, 4 * (size_t)GRADE_TIE_HASH);
    for (uint32_t i = f->stations; i-- > 0;)
    {
        const uint32_t cell = GradeTieCell((int)floorf(f->xy[2 * i] / GRADE_TIE), (int)floorf(f->xy[2 * i + 1] / GRADE_TIE));
        next[i] = head[cell]; head[cell] = i;
    }
    for (uint32_t i = 0; i < f->stations; i++)
    {
        static uint32_t id[GRADE_TIE_SEEN]; static float len[GRADE_TIE_SEEN];
        const int cx = (int)floorf(f->xy[2 * i] / GRADE_TIE), cy = (int)floorf(f->xy[2 * i + 1] / GRADE_TIE);
        uint32_t seen = 0;
        for (int q = 0; q < 9; q++)
            for (uint32_t j = head[GradeTieCell(cx + q % 3 - 1, cy + q / 3 - 1)]; j != GRADE_NONE; j = next[j])
            {
                const float far_ = GradeFar(f, j, f->xy[2 * i], f->xy[2 * i + 1]);
                const int pin = f->s[i].pinned || f->s[j].pinned;
                if (j <= i || !(far_ < (pin ? GRADE_TIE_NEAR : GRADE_TIE))) continue;
                if (!GradeTieFoot(f, i, j, far_) || !GradeTieFoot(f, j, i, far_)) continue;
                if (seen == 0) seen = GradeTieReach(f, i, GRADE_TIE_PATH * GRADE_TIE, id, len);
                uint32_t q2 = 0;
                while (q2 < seen && id[q2] != j) q2++;
                if (q2 < seen && len[q2] <= GRADE_TIE_PATH * far_) continue;
                /* the difference a tie allows is its slope times this length */
                const float wide = far_ > GRADE_TIE_BED ? (far_ - GRADE_TIE_BED) / 4.0f : 0.0f;
                const float gap = pin ? far_ * GRADE_TIE_RATIO : far_ * (1.0f + wide * wide * wide);
                if (!GradeLink(f->s, i, j, gap > GRADE_TIE_LEAST ? gap : GRADE_TIE_LEAST)) { f->untied++; continue; }
                f->ties++;
                if (!pin) f->s[i].most[f->s[i].links - 1] = f->s[j].most[f->s[j].links - 1] = GRADE_TIE_SLOPE;
            }
    }
    free(head);
}

/* Landings. Where roads meet (a station with three or more pieces) or a line turns sharply (GRADE_LAND_TURN), the
 * beds overlap and each takes the other's height by nearness: one road's grade becomes the other's cross slope. So
 * every link within GRADE_LAND along the roads from such a station takes GRADE_LAND_SLOPE at most, and the meeting
 * place is near level. At a spur's junction only the spur's own links are landed: the arterial is as it would be
 * without the spur. A link with a pinned end is left alone: a pad's height is not the road's to choose. Before
 * the ties: it reads the pieces' links and positions only. landings counts the stations, landed the links. */
#define GRADE_LAND 20.0f
#define GRADE_LAND_SLOPE 0.0305530f /* tan 1.75 degrees: read across the other bed through the hand-over, 4 degrees */
#define GRADE_LAND_TURN 0.8660254f  /* the cosine of 30 degrees */
static int GradeLandTurn(const GradeField* f, uint32_t i, uint32_t from, uint32_t to)
{
    const float ax = f->xy[2 * i] - f->xy[2 * from], ay = f->xy[2 * i + 1] - f->xy[2 * from + 1];
    const float bx = f->xy[2 * to] - f->xy[2 * i], by = f->xy[2 * to + 1] - f->xy[2 * i + 1];
    const float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
    return la > 0.0f && lb > 0.0f && ax * bx + ay * by < GRADE_LAND_TURN * la * lb;
}
static uint32_t GradeFieldLand(GradeField* f, uint32_t* landed)
{
    uint32_t landings = 0;
    *landed = 0;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        const GradeStation* s = &f->s[i];
        /* the arterials' arms are judged without the spurs, so no spur decides where an arterial is level */
        uint32_t own[2] = {0, 0}, arms = 0;
        for (uint32_t k = 0; k < s->links; k++) if (!f->s[s->link[k]].later) { if (arms < 2) own[arms] = s->link[k]; arms++; }
        const int first = s->later ? 0 : arms >= 3 || (arms == 2 && GradeLandTurn(f, i, own[0], own[1]));
        const int meet = s->links >= 3 || (s->links == 2 && GradeLandTurn(f, i, s->link[0], s->link[1]));
        if (!meet) continue;
        landings++;
        for (uint32_t k = 0; k < s->links; k++)
        {
            uint32_t from = i, slot = k; float gone = 0.0f;
            const int arterial = !f->s[s->link[k]].later && !s->later;
            if (arterial && !first) continue;
            /* along the road until GRADE_LAND is behind or the road meets another */
            for (uint32_t step = 0; step < 64 && gone < GRADE_LAND; step++)
            {
                GradeStation* a = &f->s[from]; const uint32_t to = a->link[slot]; GradeStation* b = &f->s[to];
                gone += a->gap[slot];
                if (!a->pinned && !b->pinned)
                {
                    if (!(a->most[slot] > 0.0f)) (*landed)++;
                    a->most[slot] = GRADE_LAND_SLOPE;
                    for (uint32_t m = 0; m < b->links; m++) if (b->link[m] == from) b->most[m] = GRADE_LAND_SLOPE;
                }
                /* on: an arterial's arm passes a spur's junction as if the spur were not there */
                uint32_t on = GRADE_LINKS, ways = 0;
                for (uint32_t m = 0; m < b->links; m++)
                    if (!(arterial && f->s[b->link[m]].later)) { ways++; if (b->link[m] != from) on = m; }
                if (ways != 2 || on == GRADE_LINKS) break;
                slot = on; from = to;
            }
        }
    }
    return landings;
}

/* The side band's ramp for a road moved `move` metres at its middle on ground that slopes `cross` (a tangent) across
 * it. On the side where the move grows outwards (a cut's uphill side, a fill's downhill side) what the blend adds
 * falls from bank = move + GRADE_FULL cross, still growing by `cross` a metre at the bed's edge, to nothing as a
 * cubic (GradeBandWeight). Its steepest fall is (p - cross)^2 / 4p with p = 6 bank / ramp + 3 cross, its sharpest
 * bend 6 bank / ramp^2 + 4 cross / ramp at the bed's edge: the ramp is the wider of the two that hold GRADE_SIDE and
 * GRADE_BEND. From cross = 3 GRADE_SIDE (32.5 degrees) no width holds the side slope: infinite, the field caps it. */
static float GradeBandRamp(float move, float cross)
{
    if (!(cross >= 0.0f)) cross = 0.0f;
    if (!(move >= 0.0f)) move = 0.0f;
    const float bank = move + GRADE_FULL * cross, room = GRADE_SIDE + sqrtf(GRADE_SIDE * (GRADE_SIDE + cross)) - cross;
    if (!(bank > 0.0f)) return 0.0f;
    const float side = room > 0.0f ? 3.0f * bank / room : INFINITY;
    const float bend = (4.0f * cross + sqrtf(16.0f * cross * cross + 24.0f * GRADE_BEND * bank)) / (2.0f * GRADE_BEND);
    return side > bend ? side : bend;
}
/* The weight before easing at the share u of a ramp `ramp` metres wide. Eased, it is the cubic that leaves the bed's
 * edge with the bank still growing by `cross` a metre and ends level, over the move there: so the surface has no
 * crease at either end on a side-hill. On level ground that is 1 - u. The other side of the road moves less. */
static float GradeBandWeight(float u, float ramp, float move, float cross)
{
    if (!(u > 0.0f)) return 1.0f;
    if (!(u < 1.0f)) return 0.0f;
    if (!(cross >= 0.0f)) cross = 0.0f;
    if (!(move >= 0.0f)) move = 0.0f;
    const double bank = (double)move + (double)GRADE_FULL * cross + 1.0e-6, grown = (double)cross * ramp * u, v = 1.0 - u;
    double eased = (bank * v * v * (1.0 + 2.0 * u) + grown * v * v) / (bank + grown);
    eased = eased < 0.0 ? 0.0 : eased > 1.0 ? 1.0 : eased;
    return (float)(0.5 - sin(asin(1.0 - 2.0 * eased) / 3.0)); /* GradeSmooth's inverse */
}
/* The ground's slope across the road at station i (a tangent): the steepest over its pieces of road, between the two points
 * GRADE_FULL to either side. 0 where the ground cannot be read. */
static float GradeFieldCross(const GradeField* f, uint32_t i, float (*ground)(void*, float, float), void* ctx)
{
    float most = 0.0f;
    for (uint32_t k = 0; k < f->s[i].roads; k++) /* a tie points across the road, not along it */
    {
        const uint32_t j = f->s[i].link[k];
        float dx = f->xy[2 * j] - f->xy[2 * i], dy = f->xy[2 * j + 1] - f->xy[2 * i + 1];
        const float n = sqrtf(dx * dx + dy * dy);
        if (!(n > 0.0f)) continue;
        dx /= n; dy /= n;
        const float a = ground(ctx, f->xy[2 * i] - dy * GRADE_FULL, f->xy[2 * i + 1] + dx * GRADE_FULL);
        const float b = ground(ctx, f->xy[2 * i] + dy * GRADE_FULL, f->xy[2 * i + 1] - dx * GRADE_FULL);
        if (!isfinite(a) || !isfinite(b)) continue;
        const float t = fabsf(a - b) / (2.0f * GRADE_FULL);
        if (t > most) most = t;
    }
    return most;
}
/* After the solve: each station's band from what it cuts or fills, and the pieces in a grid of GRADE_CELL cells, each
 * in every cell its box, grown by its band and GRADE_REACH, touches. world: the map's side in metres. 0: no memory. */
static int GradeFieldIndex(GradeField* f, float world)
{
    free(f->band); free(f->start); free(f->item); f->start = f->item = NULL;
    f->band = (float*)malloc(4 * (size_t)(f->stations ? f->stations : 1));
    f->grid = (int)ceilf(world / GRADE_CELL);
    if (f->grid < 1) f->grid = 1;
    const size_t cells = (size_t)f->grid * f->grid;
    f->start = (uint32_t*)calloc(cells + 1, 4);
    if (f->band == NULL || f->start == NULL) return 0;
    f->capped = 0;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        float ramp = GradeBandRamp(fabsf(f->s[i].target - f->s[i].ground), f->s[i].cross);
        if (ramp < GRADE_BAND_LEAST) ramp = GRADE_BAND_LEAST;
        if (ramp > GRADE_BAND_MOST) { ramp = GRADE_BAND_MOST; f->capped++; }
        f->band[i] = GRADE_FULL + ramp;
    }
    for (int fill = 0; fill < 2; fill++)
    {
        for (uint32_t k = 0; k < f->segs; k++)
        {
            const GradeSeg* g = &f->seg[k];
            const float r = (f->band[g->a] > f->band[g->b] ? f->band[g->a] : f->band[g->b]) + GRADE_REACH;
            int c0 = (int)floorf((fminf(g->ax, g->bx) - r) / GRADE_CELL), c1 = (int)floorf((fmaxf(g->ax, g->bx) + r) / GRADE_CELL);
            int r0 = (int)floorf((fminf(g->ay, g->by) - r) / GRADE_CELL), r1 = (int)floorf((fmaxf(g->ay, g->by) + r) / GRADE_CELL);
            c0 = c0 < 0 ? 0 : c0; r0 = r0 < 0 ? 0 : r0; c1 = c1 >= f->grid ? f->grid - 1 : c1; r1 = r1 >= f->grid ? f->grid - 1 : r1;
            for (int r_ = r0; r_ <= r1; r_++)
                for (int c = c0; c <= c1; c++)
                {
                    const size_t cell = (size_t)r_ * f->grid + c;
                    if (fill) f->item[f->start[cell]++] = k; else f->start[cell + 1]++;
                }
        }
        if (fill) break;
        for (size_t c = 0; c < cells; c++) f->start[c + 1] += f->start[c];
        f->item = (uint32_t*)malloc(4 * (size_t)(f->start[cells] ? f->start[cells] : 1));
        if (f->item == NULL) return 0;
    }
    /* filling moved each cell's start to its end: back by one cell */
    for (size_t c = cells; c > 0; c--) f->start[c] = f->start[c - 1];
    f->start[0] = 0;
    return 1;
}

static float GradeSmooth(float w) { w = w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w; return w * w * (3.0f - 2.0f * w); }

/* What a point's roads say of the pads they run on, each by its share of the height: held the mean of the stations'
 * `held` along the pieces, foot the mean of pad() at the point's foot on each centreline (by the shares squared:
 * beside a station the next piece's end is a foot too, and must not blur the nearest one's). */
typedef struct GradeFoot { float (*pad)(float x, float y); float held, foot; } GradeFoot;
/* The field at (x, y), metres from the map's corner: the weight before easing (0: no road reaches here), *z the
 * height the roads set (the nearer a road and the more of it, the more it counts; left alone where none has weight)
 * and *d the distance to the nearest centreline. Only a road with weight shares the height, so which roads a cell
 * lists (their bands, from the capture) never changes a height. on: may be NULL. */
static float GradeFieldAt(const GradeField* f, float x, float y, float* z, float* d, GradeFoot* on)
{
    *d = GRADE_FAR;
    if (on != NULL) on->held = on->foot = 0.0f;
    if (f->start == NULL || !(x >= 0.0f) || !(y >= 0.0f)) return 0.0f;
    const int c = (int)(x / GRADE_CELL), r = (int)(y / GRADE_CELL);
    if (c >= f->grid || r >= f->grid) return 0.0f;
    const size_t cell = (size_t)r * f->grid + c;
    float weight = 0.0f; double sum = 0.0, heights = 0.0, held = 0.0, foot = 0.0, feet = 0.0;
    for (uint32_t q = f->start[cell]; q < f->start[cell + 1]; q++)
    {
        const GradeSeg* g = &f->seg[f->item[q]];
        const float dx = g->bx - g->ax, dy = g->by - g->ay, l2 = dx * dx + dy * dy;
        float t = l2 > 1.0e-9f ? ((x - g->ax) * dx + (y - g->ay) * dy) / l2 : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float qx = g->ax + t * dx - x, qy = g->ay + t * dy - y, dist = sqrtf(qx * qx + qy * qy);
        const float band = f->band[g->a] + t * (f->band[g->b] - f->band[g->a]);
        const GradeStation* sa = &f->s[g->a]; const GradeStation* sb = &f->s[g->b];
        const float ma = fabsf(sa->target - sa->ground), mb = fabsf(sb->target - sb->ground);
        const float w = dist <= GRADE_FULL ? 1.0f : dist >= band ? 0.0f
            : GradeBandWeight((dist - GRADE_FULL) / (band - GRADE_FULL), band - GRADE_FULL, ma + t * (mb - ma), sa->cross + t * (sb->cross - sa->cross));
        if (dist < *d) *d = dist;
        if (!(w > 0.0f)) continue;
        const double close = (double)dist * dist + 1.0, share = w / (close * close);
        sum += share; heights += share * (sa->target + t * (sb->target - sa->target));
        if (on != NULL)
        {
            held += share * ((float)sa->held + t * ((float)sb->held - (float)sa->held));
            if (on->pad != NULL) { foot += share * share * on->pad(x + qx, y + qy); feet += share * share; }
        }
        if (w > weight) weight = w;
    }
    if (!(sum > 0.0)) return 0.0f;
    *z = (float)(heights / sum);
    if (on != NULL) { on->held = (float)(held / sum); on->foot = feet > 0.0 ? (float)(foot / feet) : 0.0f; }
    return weight;
}

/* ---- A road's slopes on any surface (game-free) ----
 * Heights are taken every GRADE_SAMPLE along the points; a grade is the rise between samples GRADE_BASE apart, the
 * cross slope the rise between the points GRADE_ACROSS to each side. A height that is not finite (off the surface)
 * ends the run of samples: grades never span a gap. */
#define GRADE_SAMPLE 2.0f
#define GRADE_BASE 10.0f
#define GRADE_ACROSS 3.0f
#define GRADE_BINS 360 /* quarter degrees */
typedef float (*GradeHeightFn)(void* ctx, float x, float y);
#define GRADE_SIDE_STEP 4.0f  /* a shoulder span: the level's sample spacing */
#define GRADE_SIDE_SPANS 41u  /* from GRADE_FULL - GRADE_SIDE_STEP out to the widest band's end */
#define GRADE_EDGE 6.0f       /* the bed between GRADE_ACROSS and here is read for a step across it */
#define GRADE_SIDE_EVERY 4u   /* a shoulder section every fourth sample along */
/* over: 5, 8, 12, 18 degrees; At: where the worst is. side: the steepest shoulder span (degrees across the road, from
 * the bed's edge out; with a base surface, what the surface adds to the base's own side slope there); crease: the
 * sharpest change of side slope between neighbouring spans. brief: the steepest between neighbouring samples (a short
 * step the 10 m baseline averages away); edge: the steepest across the bed between GRADE_ACROSS and GRADE_EDGE. */
typedef struct GradeRoad
{
    double metres, over[4], rise;
    float steepest, cross, steepAt[2], crossAt[2], side, crease, sideAt[2], creaseAt[2], brief, briefAt[2], edge, edgeAt[2];
    uint32_t bin[GRADE_BINS];
} GradeRoad;

/* One shoulder section at (x, y), (ux, uy) the unit vector across the road: both sides. base (may be NULL): the
 * ground `height` is a move of; a side slope is then what ground and move together have over the ground's own, so a
 * level bed on a side-hill reads 0 and not the hill. */
static void GradeRoadShoulder(GradeHeightFn height, GradeHeightFn base, void* ctx, float x, float y, float ux, float uy, GradeRoad* out)
{
    const float deg = 57.29578f;
    for (int side = -1; side <= 1; side += 2)
    {
        float before = 0.0f, last = 0.0f, under = 0.0f;
        for (uint32_t k = 0; k <= GRADE_SIDE_SPANS; k++)
        {
            const float off = (float)side * (GRADE_FULL - GRADE_SIDE_STEP + GRADE_SIDE_STEP * (float)k), px = x + ux * off, py = y + uy * off;
            const float h = height(ctx, px, py), g = base != NULL ? base(ctx, px, py) : 0.0f;
            if (!isfinite(h) || !isfinite(g)) break;
            if (k > 0)
            {
                const float slope = atanf((h - last) / GRADE_SIDE_STEP) * deg;
                const float own = base != NULL ? fabsf(atanf((g - under) / GRADE_SIDE_STEP)) * deg : 0.0f;
                const float adds = base != NULL ? fabsf(atanf((g + h - under - last) / GRADE_SIDE_STEP)) * deg - own : fabsf(slope);
                if (adds > out->side) { out->side = adds; out->sideAt[0] = px; out->sideAt[1] = py; }
                if (k > 1 && fabsf(slope - before) > out->crease) { out->crease = fabsf(slope - before); out->creaseAt[0] = px; out->creaseAt[1] = py; }
                before = slope;
            }
            last = h; under = g;
        }
    }
}

static void GradeRoadAlong(GradeHeightFn height, GradeHeightFn base, void* ctx, const float* pts, uint32_t n, GradeRoad* out)
{
    const float deg = 57.29578f;
    const uint32_t lag = (uint32_t)(GRADE_BASE / GRADE_SAMPLE);
    float ring[8]; uint32_t run = 0; /* the last `lag` samples' heights */
    float carry = 0.0f;              /* metres from the last sample to the pair's start */
    uint32_t sample = 0;
    for (uint32_t p = 1; p < n; p++)
    {
        const float ax = pts[2 * p - 2], ay = pts[2 * p - 1], dx = pts[2 * p] - ax, dy = pts[2 * p + 1] - ay, length = sqrtf(dx * dx + dy * dy);
        if (!(length > 1.0e-3f) || !(length < 1.0e5f)) continue;
        const float nx = -dy / length * GRADE_ACROSS, ny = dx / length * GRADE_ACROSS;
        float at = p == 1 ? 0.0f : GRADE_SAMPLE - carry;
        for (; at <= length; at += GRADE_SAMPLE)
        {
            const float x = ax + dx * at / length, y = ay + dy * at / length, h = height(ctx, x, y);
            if (!isfinite(h)) { run = 0; continue; }
            const float left = height(ctx, x + nx, y + ny), right = height(ctx, x - nx, y - ny);
            if (sample++ % GRADE_SIDE_EVERY == 0) GradeRoadShoulder(height, base, ctx, x, y, nx / GRADE_ACROSS, ny / GRADE_ACROSS, out);
            if (isfinite(left) && isfinite(right))
            {
                const float cross = atanf(fabsf(left - right) / (2.0f * GRADE_ACROSS)) * deg;
                if (cross > out->cross) { out->cross = cross; out->crossAt[0] = x; out->crossAt[1] = y; }
                /* a step 3 to 6 m off the line lies between the cross slope's points and the shoulder's first */
                const float ex = nx * (GRADE_EDGE / GRADE_ACROSS), ey = ny * (GRADE_EDGE / GRADE_ACROSS);
                const float outer[2] = {height(ctx, x + ex, y + ey) - left, height(ctx, x - ex, y - ey) - right};
                for (int q = 0; q < 2; q++)
                {
                    const float edge = atanf(fabsf(outer[q]) / (GRADE_EDGE - GRADE_ACROSS)) * deg;
                    if (isfinite(edge) && edge > out->edge) { out->edge = edge; out->edgeAt[0] = x; out->edgeAt[1] = y; }
                }
            }
            if (run > 0)
            {
                const float brief = atanf(fabsf(h - ring[(run - 1) % lag]) / GRADE_SAMPLE) * deg;
                if (brief > out->brief) { out->brief = brief; out->briefAt[0] = x; out->briefAt[1] = y; }
                out->metres += GRADE_SAMPLE; out->rise += fabsf(h - ring[(run - 1) % lag]);
            }
            if (run >= lag)
            {
                const float grade = atanf(fabsf(h - ring[run % lag]) / GRADE_BASE) * deg;
                uint32_t bin = (uint32_t)(grade * 4.0f);
                out->bin[bin < GRADE_BINS ? bin : GRADE_BINS - 1]++;
                if (grade > out->steepest) { out->steepest = grade; out->steepAt[0] = x; out->steepAt[1] = y; }
                if (grade > 5.0f) out->over[0] += GRADE_SAMPLE;
                if (grade > 8.0f) out->over[1] += GRADE_SAMPLE;
                if (grade > 12.0f) out->over[2] += GRADE_SAMPLE;
                if (grade > 18.0f) out->over[3] += GRADE_SAMPLE;
            }
            ring[run % lag] = h; run++;
        }
        carry = length - (at - GRADE_SAMPLE);
    }
}

/* The grade 95 of 100 samples stay under, degrees (a bin's upper edge). */
static float GradeRoad95(const GradeRoad* g)
{
    uint64_t total = 0, seen = 0;
    for (uint32_t b = 0; b < GRADE_BINS; b++) total += g->bin[b];
    if (total == 0) return 0.0f;
    for (uint32_t b = 0; b < GRADE_BINS; b++)
    {
        seen += g->bin[b];
        if (seen * 100 >= total * 95) return (float)(b + 1) * 0.25f;
    }
    return 90.0f;
}

static void GradeRoadAdd(GradeRoad* sum, const GradeRoad* g)
{
    sum->metres += g->metres; sum->rise += g->rise;
    for (int k = 0; k < 4; k++) sum->over[k] += g->over[k];
    for (uint32_t b = 0; b < GRADE_BINS; b++) sum->bin[b] += g->bin[b];
    if (g->steepest > sum->steepest) { sum->steepest = g->steepest; sum->steepAt[0] = g->steepAt[0]; sum->steepAt[1] = g->steepAt[1]; }
    if (g->cross > sum->cross) { sum->cross = g->cross; sum->crossAt[0] = g->crossAt[0]; sum->crossAt[1] = g->crossAt[1]; }
    if (g->side > sum->side) { sum->side = g->side; sum->sideAt[0] = g->sideAt[0]; sum->sideAt[1] = g->sideAt[1]; }
    if (g->crease > sum->crease) { sum->crease = g->crease; sum->creaseAt[0] = g->creaseAt[0]; sum->creaseAt[1] = g->creaseAt[1]; }
    if (g->brief > sum->brief) { sum->brief = g->brief; sum->briefAt[0] = g->briefAt[0]; sum->briefAt[1] = g->briefAt[1]; }
    if (g->edge > sum->edge) { sum->edge = g->edge; sum->edgeAt[0] = g->edgeAt[0]; sum->edgeAt[1] = g->edgeAt[1]; }
}
