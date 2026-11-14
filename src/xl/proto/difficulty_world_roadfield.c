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
 *   GRADE_BEND at the bed's edge (GradeBandRamp). A worn path has shoulders, not banks.
 * - Beds: a line owns the ground within GRADE_OWN of it against every later line (GradeFieldRuns: arterials before
 *   spurs, then the order they were added). A later line's height counts nothing there and whole from
 *   GRADE_OWN_END, so a neighbour's grade is never a cross slope on the earlier line's bed. */
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
#define GRADE_OWN 6.0f        /* the checker reads a bed 3 to 6 m off its line */
#define GRADE_OWN_END 10.0f   /* GRADE_FULL: where the earlier line's own level bed ends */
#define GRADE_SPUR_RUN 0x80000000u
/* run: the line a piece belongs to, by its first piece (spurs after every arterial). zone: 1 a level piece or a
 * spur's piece in an arterial's bed, 2 and 3 the ramp pieces after such a stretch. */
typedef struct GradeSeg { float ax, ay, bx, by; uint32_t a, b, run; uint8_t zone; } GradeSeg;
typedef struct GradeField
{
    GradeStation* s; float* xy; float* band;
    GradeSeg* seg;
    uint32_t* start; uint32_t* item;
    uint32_t* piece; /* per station and link slot: the piece of road that link is (GradeFieldLinks) */
    uint32_t stations, stationRoom, segs, segRoom, unlinked, capped, ties, untied, runs, beds, ramps;
    int grid;
} GradeField;

static void GradeFieldFree(GradeField* f)
{
    free(f->s); free(f->xy); free(f->band); free(f->seg); free(f->start); free(f->item); free(f->piece);
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
    const GradeSeg g = {f->xy[2 * a], f->xy[2 * a + 1], f->xy[2 * b], f->xy[2 * b + 1], a, b, 0, 0};
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
    free(f->piece);
    f->piece = (uint32_t*)malloc(4 * (size_t)GRADE_LINKS * (f->stations ? f->stations : 1));
    for (uint32_t k = 0; k < f->segs; k++)
    {
        const GradeSeg* g = &f->seg[k];
        if (!GradeLink(f->s, g->a, g->b, sqrtf((g->bx - g->ax) * (g->bx - g->ax) + (g->by - g->ay) * (g->by - g->ay)))) { f->unlinked++; continue; }
        if (f->piece == NULL) continue;
        f->piece[GRADE_LINKS * g->a + f->s[g->a].links - 1] = k; f->piece[GRADE_LINKS * g->b + f->s[g->b].links - 1] = k;
    }
    for (uint32_t i = 0; i < f->stations; i++) f->s[i].roads = f->s[i].links;
}

/* The distance from (x, y) to a piece; *t the foot's share of the way from a to b. */
static float GradeSegFar(const GradeSeg* g, float x, float y, float* t)
{
    const float dx = g->bx - g->ax, dy = g->by - g->ay, l2 = dx * dx + dy * dy;
    float u = l2 > 1.0e-9f ? ((x - g->ax) * dx + (y - g->ay) * dy) / l2 : 0.0f;
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    const float qx = g->ax + u * dx - x, qy = g->ay + u * dy - y;
    *t = u;
    return sqrtf(qx * qx + qy * qy);
}
static int GradeSegSpur(const GradeField* f, const GradeSeg* g) { return f->s[g->a].later || f->s[g->b].later; }
static void GradeSegMost(GradeField* f, const GradeSeg* g, float most)
{
    GradeStation* a = &f->s[g->a]; GradeStation* b = &f->s[g->b];
    for (uint32_t k = 0; k < a->roads; k++) if (a->link[k] == g->b) a->most[k] = most;
    for (uint32_t k = 0; k < b->roads; k++) if (b->link[k] == g->a) b->most[k] = most;
}

/* Runs: the pieces that go on as one line. At a station the two pieces of one kind (arterial, or a spur's own) that
 * pass straightest go on when they turn under GRADE_LAND_TURN; a spur starts at its arterial's station. A run is
 * named by its first piece, spurs after every arterial: the order of beds (GradeFieldAt). Positions and the order
 * the roads were added only: the same on every machine. After GradeFieldLinks. */
#define GRADE_LAND_TURN 0.8660254f /* the cosine of 30 degrees */
static uint32_t GradeRunRoot(uint32_t* up, uint32_t k)
{
    while (up[k] != k) { up[k] = up[up[k]]; k = up[k]; }
    return k;
}
static void GradeFieldRuns(GradeField* f)
{
    f->runs = 0;
    uint32_t* up = (uint32_t*)malloc(4 * (size_t)(f->segs ? f->segs : 1));
    if (up == NULL || f->piece == NULL) { free(up); for (uint32_t k = 0; k < f->segs; k++) f->seg[k].run = k; return; }
    for (uint32_t k = 0; k < f->segs; k++) up[k] = k;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        const GradeStation* s = &f->s[i];
        for (int spur = 0; spur < 2; spur++)
        {
            if (spur && !s->later) continue;
            float best = -2.0f; uint32_t pa = 0, pb = 0;
            for (uint32_t x = 0; x < s->roads; x++)
                for (uint32_t y = x + 1; y < s->roads; y++)
                {
                    const uint32_t ka = f->piece[GRADE_LINKS * i + x], kb = f->piece[GRADE_LINKS * i + y];
                    if (GradeSegSpur(f, &f->seg[ka]) != spur || GradeSegSpur(f, &f->seg[kb]) != spur) continue;
                    const float ax = f->xy[2 * i] - f->xy[2 * s->link[x]], ay = f->xy[2 * i + 1] - f->xy[2 * s->link[x] + 1];
                    const float bx = f->xy[2 * s->link[y]] - f->xy[2 * i], by = f->xy[2 * s->link[y] + 1] - f->xy[2 * i + 1];
                    const float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
                    if (!(la > 0.0f) || !(lb > 0.0f)) continue;
                    const float straight = (ax * bx + ay * by) / (la * lb);
                    if (straight > best) { best = straight; pa = ka; pb = kb; }
                }
            if (!(best >= GRADE_LAND_TURN)) continue;
            const uint32_t ra = GradeRunRoot(up, pa), rb = GradeRunRoot(up, pb);
            if (ra != rb) up[ra > rb ? ra : rb] = ra < rb ? ra : rb; /* the root is the run's first piece */
        }
    }
    for (uint32_t k = 0; k < f->segs; k++)
    {
        const uint32_t root = GradeRunRoot(up, k);
        f->runs += root == k;
        f->seg[k].run = root | (GradeSegSpur(f, &f->seg[k]) ? GRADE_SPUR_RUN : 0u);
    }
    free(up);
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

/* Landings. Where two runs of one kind meet (a corner over 30 degrees, an arterials' junction) the later run's
 * height counts nothing in the earlier one's bed (GradeFieldAt), so there the two must agree: every piece of either
 * run with an end within GRADE_OWN of the other run is level (GRADE_LEVEL), and never gives way. Arms that leave
 * the station within 60 degrees of each other run side by side instead: the ties hold those. A piece with a pinned
 * end is left alone: a pad's height is not the road's to choose. Reads positions, runs and pins only; before the
 * ties. Returns the meeting stations, *landed the level pieces. */
#define GRADE_LAND_WALK 32.0f /* how far along a run its pieces are tested against the other run */
#define GRADE_LAND_FAN 0.5f   /* the cosine of 60 degrees */
#define GRADE_LAND_SEEN 16u
/* The pieces of `run` within GRADE_LAND_WALK along it from station i, both ways; returns their count. */
static uint32_t GradeLandWalk(const GradeField* f, uint32_t i, uint32_t run, uint32_t* out)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < f->s[i].roads; k++)
    {
        uint32_t piece = f->piece[GRADE_LINKS * i + k], from = i, slot = k; float gone = 0.0f;
        if (f->seg[piece].run != run) continue;
        while (n < GRADE_LAND_SEEN)
        {
            const GradeStation* a = &f->s[from]; const uint32_t to = a->link[slot];
            out[n++] = piece; gone += a->gap[slot];
            if (!(gone < GRADE_LAND_WALK)) break;
            uint32_t on = GRADE_LINKS;
            for (uint32_t m = 0; m < f->s[to].roads; m++)
            {
                const uint32_t next = f->piece[GRADE_LINKS * to + m];
                if (next != piece && f->seg[next].run == run) on = m;
            }
            if (on == GRADE_LINKS) break;
            piece = f->piece[GRADE_LINKS * to + on]; slot = on; from = to;
        }
    }
    return n;
}
/* 1 when two arms of the runs leave station i within 60 degrees of each other. */
static int GradeLandFan(const GradeField* f, uint32_t i, uint32_t g, uint32_t h)
{
    const GradeStation* s = &f->s[i];
    for (uint32_t x = 0; x < s->roads; x++)
        for (uint32_t y = 0; y < s->roads; y++)
        {
            if (f->seg[f->piece[GRADE_LINKS * i + x]].run != g || f->seg[f->piece[GRADE_LINKS * i + y]].run != h) continue;
            const float ax = f->xy[2 * s->link[x]] - f->xy[2 * i], ay = f->xy[2 * s->link[x] + 1] - f->xy[2 * i + 1];
            const float bx = f->xy[2 * s->link[y]] - f->xy[2 * i], by = f->xy[2 * s->link[y] + 1] - f->xy[2 * i + 1];
            const float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
            if (la > 0.0f && lb > 0.0f && ax * bx + ay * by > GRADE_LAND_FAN * la * lb) return 1;
        }
    return 0;
}
static uint32_t GradeFieldLand(GradeField* f, uint32_t* landed)
{
    uint32_t landings = 0;
    *landed = 0;
    if (f->piece == NULL) return 0;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        const GradeStation* s = &f->s[i];
        uint32_t run[GRADE_LINKS], runs = 0; int hit = 0;
        for (uint32_t k = 0; k < s->roads; k++)
        {
            const uint32_t r = f->seg[f->piece[GRADE_LINKS * i + k]].run; uint32_t q = 0;
            while (q < runs && run[q] != r) q++;
            if (q == runs) run[runs++] = r;
        }
        for (uint32_t x = 0; x < runs; x++)
            for (uint32_t y = x + 1; y < runs; y++)
            {
                /* an arterial and a spur: the spur stands at the arterial's height in its bed instead (GradeFieldBeds) */
                if ((run[x] ^ run[y]) & GRADE_SPUR_RUN || GradeLandFan(f, i, run[x], run[y])) continue;
                uint32_t walk[2][GRADE_LAND_SEEN];
                const uint32_t count[2] = {GradeLandWalk(f, i, run[x], walk[0]), GradeLandWalk(f, i, run[y], walk[1])};
                hit = 1;
                for (int side = 0; side < 2; side++)
                    for (uint32_t p = 0; p < count[side]; p++)
                    {
                        GradeSeg* g = &f->seg[walk[side][p]];
                        float near_ = GRADE_FAR, t;
                        if (f->s[g->a].pinned || f->s[g->b].pinned || g->zone == 1) continue;
                        for (uint32_t q = 0; q < count[!side]; q++)
                        {
                            const GradeSeg* o = &f->seg[walk[!side][q]];
                            const float da = GradeSegFar(o, g->ax, g->ay, &t), db = GradeSegFar(o, g->bx, g->by, &t);
                            near_ = da < near_ ? da : near_; near_ = db < near_ ? db : near_;
                        }
                        if (!(near_ <= GRADE_OWN)) continue;
                        g->zone = 1; (*landed)++;
                        GradeSegMost(f, g, GRADE_LEVEL);
                    }
            }
        landings += (uint32_t)hit;
    }
    return landings;
}
/* A spur in an arterial's bed. A spur's piece with an end within GRADE_OWN of an arterial's piece lies on ground the
 * arterial owns: its own stations (not a pinned one: a pad holds) are marked `bed` and take the arterial's surface
 * there, the mean of the up to four nearest arterial pieces' heights at their feet by the field's shares. So the
 * spur's height equals the arterial's wherever the field ignores the spur, and the spur leaves the bed's edge at
 * the arterial's height. Positions and pins only. Returns the stations marked. */
#define GRADE_BED_REACH 16.0f
static uint32_t GradeFieldBeds(GradeField* f)
{
    uint32_t beds = 0;
    float* near_ = (float*)malloc(4 * (size_t)(f->stations ? f->stations : 1));
    if (near_ == NULL || f->piece == NULL) { free(near_); return 0; }
    for (uint32_t i = 0; i < f->stations; i++)
    {
        float t;
        near_[i] = f->s[i].later ? GRADE_FAR : 0.0f;
        for (uint32_t k = 0; f->s[i].later && k < f->segs; k++)
        {
            if (f->seg[k].run & GRADE_SPUR_RUN) continue;
            const float d = GradeSegFar(&f->seg[k], f->xy[2 * i], f->xy[2 * i + 1], &t);
            if (d < near_[i]) near_[i] = d;
        }
    }
    for (uint32_t k = 0; k < f->segs; k++)
        if (f->seg[k].run & GRADE_SPUR_RUN && (near_[f->seg[k].a] <= GRADE_OWN || near_[f->seg[k].b] <= GRADE_OWN)) f->seg[k].zone = 1;
    for (uint32_t i = 0; i < f->stations; i++)
    {
        GradeStation* s = &f->s[i];
        int reach = 0;
        for (uint32_t k = 0; s->later && !s->pinned && k < s->roads; k++) reach |= f->seg[f->piece[GRADE_LINKS * i + k]].zone == 1;
        if (!reach) continue;
        float far_[GRADE_FEET / 2], at[GRADE_FEET / 2]; uint32_t piece[GRADE_FEET / 2], n = 0;
        for (uint32_t k = 0; k < f->segs; k++)
        {
            float t;
            if (f->seg[k].run & GRADE_SPUR_RUN) continue;
            const float d = GradeSegFar(&f->seg[k], f->xy[2 * i], f->xy[2 * i + 1], &t);
            if (!(d <= GRADE_BED_REACH)) continue;
            uint32_t q = n; /* nearest first; past the room the furthest drops out */
            if (n == GRADE_FEET / 2) { if (!(d < far_[n - 1])) continue; q = n - 1; } else n++;
            for (; q > 0 && far_[q - 1] > d; q--) { far_[q] = far_[q - 1]; at[q] = at[q - 1]; piece[q] = piece[q - 1]; }
            far_[q] = d; at[q] = t; piece[q] = k;
        }
        if (n == 0) continue;
        double sum = 0.0;
        for (uint32_t q = 0; q < n; q++) { const double close = (double)far_[q] * far_[q] + 1.0; sum += 1.0 / (close * close); }
        for (uint32_t q = 0; q < GRADE_FEET; q++) { s->foot[q] = 0; s->footW[q] = 0.0f; }
        for (uint32_t q = 0; q < n; q++)
        {
            const double close = (double)far_[q] * far_[q] + 1.0, share = 1.0 / (close * close) / sum;
            s->foot[2 * q] = f->seg[piece[q]].a; s->footW[2 * q] = (float)(share * (1.0 - at[q]));
            s->foot[2 * q + 1] = f->seg[piece[q]].b; s->footW[2 * q + 1] = (float)(share * at[q]);
        }
        s->bed = 1; beds++;
    }
    free(near_);
    return beds;
}
/* Ramps. The pieces next to a level stretch or a spur's stretch in a bed (zone 1) take GRADE_RAMP at most, the
 * pieces after those GRADE_RAMP2, so a road leaves such a stretch by a step of grade a station and not by the
 * whole of what a relaxed link may climb. Only where no slope is set, never on a piece between two pins, and a
 * spur's stretch never caps an arterial. Returns the pieces capped. */
static uint32_t GradeFieldRamps(GradeField* f)
{
    uint32_t ramps = 0;
    if (f->piece == NULL) return 0;
    for (uint8_t step = 1; step <= 2; step++)
        for (uint32_t k = 0; k < f->segs; k++)
        {
            if (f->seg[k].zone != step) continue;
            const int spur = GradeSegSpur(f, &f->seg[k]);
            const uint32_t end[2] = {f->seg[k].a, f->seg[k].b};
            for (int e = 0; e < 2; e++)
                for (uint32_t m = 0; m < f->s[end[e]].roads; m++)
                {
                    GradeSeg* g = &f->seg[f->piece[GRADE_LINKS * end[e] + m]];
                    if (g->zone != 0 || GradeSegSpur(f, g) != spur || (f->s[g->a].pinned && f->s[g->b].pinned)) continue;
                    g->zone = (uint8_t)(step + 1); ramps++;
                    GradeSegMost(f, g, step == 1 ? GRADE_RAMP : GRADE_RAMP2);
                }
        }
    return ramps;
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
#define GRADE_BEDS 24u /* pieces within GRADE_OWN_END of one point: a junction of several roads has under ten */
/* The field at (x, y), metres from the map's corner: the weight before easing (0: no road reaches here), *z the
 * height the roads set (the nearer a road and the more of it, the more it counts; left alone where none has weight)
 * and *d the distance to the nearest centreline. Only a road with weight shares the height, so which roads a cell
 * lists (their bands, from the capture) never changes a height. A piece in an earlier run's bed is left out by how
 * deep it lies there. on: may be NULL. */
static float GradeFieldAt(const GradeField* f, float x, float y, float* z, float* d, GradeFoot* on)
{
    *d = GRADE_FAR;
    if (on != NULL) on->held = on->foot = 0.0f;
    if (f->start == NULL || !(x >= 0.0f) || !(y >= 0.0f)) return 0.0f;
    const int c = (int)(x / GRADE_CELL), r = (int)(y / GRADE_CELL);
    if (c >= f->grid || r >= f->grid) return 0.0f;
    const size_t cell = (size_t)r * f->grid + c;
    float weight = 0.0f; double sum = 0.0, heights = 0.0, held = 0.0, foot = 0.0, feet = 0.0;
    /* the beds here: every piece within GRADE_OWN_END, its run and what a later run still counts for beside it */
    uint32_t bedRun[GRADE_BEDS], beds = 0, lone = 1; float bedShare[GRADE_BEDS];
    for (uint32_t q = f->start[cell]; q < f->start[cell + 1] && beds < GRADE_BEDS; q++)
    {
        float t;
        const GradeSeg* g = &f->seg[f->item[q]];
        const float dist = GradeSegFar(g, x, y, &t);
        if (!(dist < GRADE_OWN_END)) continue;
        if (beds > 0 && g->run != bedRun[0]) lone = 0;
        bedRun[beds] = g->run; bedShare[beds++] = GradeSmooth((dist - GRADE_OWN) / (GRADE_OWN_END - GRADE_OWN));
    }
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
        if (w > weight) weight = w;
        const double close = (double)dist * dist + 1.0;
        double share = w / (close * close);
        /* in the bed of an earlier run this piece's height counts nothing, and whole from the bed's edge */
        for (uint32_t b = 0; !lone && b < beds; b++) if (bedRun[b] < g->run) share *= bedShare[b];
        if (!(share > 0.0)) continue;
        sum += share; heights += share * (sa->target + t * (sb->target - sa->target));
        if (on != NULL)
        {
            held += share * ((float)sa->held + t * ((float)sb->held - (float)sa->held));
            if (on->pad != NULL) { foot += share * share * on->pad(x + qx, y + qy); feet += share * share; }
        }
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
