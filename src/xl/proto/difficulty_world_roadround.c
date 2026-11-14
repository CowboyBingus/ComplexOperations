/* ---- Roads: round the places (game-free) ---------------------------------------------------------------------------
 * A place's own terrain (the stamp terrain op 0x63 draws inside its footprint, radius [item+0xD8]) is drawn on the GPU
 * after the roads are graded, and no CPU data tells how steep it is. So a road line keeps out of every footprint: a
 * straight line from a to b becomes the taut line round the discs in its way, and a point inside a disc is moved to
 * its edge. Everything here is a function of positions and radii alone, in single precision with sqrtf as the only
 * library call, so every machine of a session gets the same line. */
#pragma STDC FP_CONTRACT OFF
#define ROUND_DISCS 1024u
#define ROUND_NEAR 64u    /* discs one line is rounded about */
#define ROUND_POINTS 1024u
#define ROUND_FINE 4.0f   /* metres between the samples a line is rounded at */
#define ROUND_REACH 400.0f /* a disc further from the straight line than this never takes part */
typedef struct RoundDisc { float x, y, r; } RoundDisc;
static uint32_t g_roundCrowded; /* discs a line had no room for (past ROUND_NEAR): it may cross them; logged */

/* Sine and cosine by their series in double precision: add, multiply and floor only, so every machine gets the same
 * bits (a library sine may use the processor's own). Good to 1e-9 for any angle a float holds near a turn. */
static void RoundSinCos(float angle, float* sine, float* cosine)
{
    const double turn = 6.283185307179586;
    double x = (double)angle;
    x -= turn * floor(x / turn + 0.5);
    double s = x, c = 1.0, ts = x, tc = 1.0;
    for (int k = 1; k <= 12; k++)
    {
        tc = -tc * x * x / (double)((2 * k - 1) * (2 * k));
        ts = -ts * x * x / (double)((2 * k) * (2 * k + 1));
        c += tc; s += ts;
    }
    *sine = (float)s; *cosine = (float)c;
}

/* p moved out of every disc it lies in, to that disc's edge (from a centre: along nx, ny). Discs that overlap are
 * left one after the other, in order, a fixed count of rounds. */
static void RoundPush(float p[2], const RoundDisc* d, uint32_t discs, float nx, float ny)
{
    for (int round = 0; round < 16; round++)
    {
        int moved = 0;
        for (uint32_t i = 0; i < discs; i++)
        {
            float dx = p[0] - d[i].x, dy = p[1] - d[i].y, away = sqrtf(dx * dx + dy * dy);
            if (!(away < d[i].r)) continue;
            if (away < 1.0e-3f) { dx = nx; dy = ny; away = 1.0f; }
            p[0] = d[i].x + dx / away * d[i].r; p[1] = d[i].y + dy / away * d[i].r;
            moved = 1;
        }
        if (!moved) return;
    }
}
/* How far p lies inside the disc it is deepest in (0: in none). */
static float RoundDepth(const float p[2], const RoundDisc* d, uint32_t discs)
{
    float worst = 0.0f;
    for (uint32_t i = 0; i < discs; i++)
    {
        const float dx = p[0] - d[i].x, dy = p[1] - d[i].y, in = d[i].r - sqrtf(dx * dx + dy * dy);
        if (in > worst) worst = in;
    }
    return worst;
}
static float RoundLength(const float* p, uint32_t n)
{
    float sum = 0.0f;
    for (uint32_t i = 1; i < n; i++) sum += sqrtf((p[2 * i] - p[2 * i - 2]) * (p[2 * i] - p[2 * i - 2]) + (p[2 * i + 1] - p[2 * i - 1]) * (p[2 * i + 1] - p[2 * i - 1]));
    return sum;
}
/* out: n points evenly along the m points of p, its ends kept. out must not be p. */
static void RoundSpread(const float* p, uint32_t m, uint32_t n, float* out)
{
    const float whole = RoundLength(p, m);
    float before = 0.0f, piece = 0.0f; uint32_t j = 0;
    if (m >= 2) piece = sqrtf((p[2] - p[0]) * (p[2] - p[0]) + (p[3] - p[1]) * (p[3] - p[1]));
    for (uint32_t k = 0; k < n; k++)
    {
        const float s = n > 1 ? whole * (float)k / (float)(n - 1) : 0.0f;
        while (j + 2 < m && before + piece < s)
        {
            before += piece; j++;
            piece = sqrtf((p[2 * j + 2] - p[2 * j]) * (p[2 * j + 2] - p[2 * j]) + (p[2 * j + 3] - p[2 * j + 1]) * (p[2 * j + 3] - p[2 * j + 1]));
        }
        float t = piece > 0.0f ? (s - before) / piece : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        out[2 * k] = p[2 * j] + (p[2 * j + 2] - p[2 * j]) * t; out[2 * k + 1] = p[2 * j + 1] + (p[2 * j + 3] - p[2 * j + 1]) * t;
    }
    if (n > 0 && m > 0) { out[0] = p[0]; out[1] = p[1]; out[2 * n - 2] = p[2 * m - 2]; out[2 * n - 1] = p[2 * m - 1]; }
}
/* The offset to the side (+1 or -1 along the normal) that takes the sample at `along` clear of the discs: each disc
 * the point lies in lifts it to that disc's edge, until it lies in none. */
static float RoundOver(const RoundDisc* d, uint32_t discs, const float a[2], float ux, float uy, float along, float side)
{
    float off = 0.0f;
    for (int round = 0; round < 32; round++)
    {
        int hit = 0;
        for (uint32_t i = 0; i < discs; i++)
        {
            const float s = (d[i].x - a[0]) * ux + (d[i].y - a[1]) * uy - along, h = (d[i].y - a[1]) * ux - (d[i].x - a[0]) * uy;
            if (!(s * s + (h - off) * (h - off) < d[i].r * d[i].r)) continue;
            const float rest = d[i].r * d[i].r - s * s;
            off = h + side * sqrtf(rest > 0.0f ? rest : 0.0f);
            hit = 1;
        }
        if (!hit) break;
    }
    return off;
}
/* The line from a to b round the discs, as ROUND_FINE samples in `out` (ROUND_POINTS x 2 floats); returns their count
 * and *bent: 1 when a disc lay in the way. a and b must lie in no disc. First each stretch the straight line has
 * inside the discs goes over them on the side that strays less, then the line is pulled taut: evenly spread, eased
 * and pushed out again, from 64 m between its points down to 4 (the coarse rounds carry the pull along the whole
 * line, the fine ones clear the discs again), so it leaves the straight line and meets it without a corner. */
static uint32_t RoundLine(const float a[2], const float b[2], const RoundDisc* all, uint32_t discs, float* out, int* bent)
{
    static RoundDisc d[ROUND_NEAR];
    static float work[2 * ROUND_POINTS], up[ROUND_POINTS], down[ROUND_POINTS];
    const float lx = b[0] - a[0], ly = b[1] - a[1], length = sqrtf(lx * lx + ly * ly);
    *bent = 0;
    out[0] = a[0]; out[1] = a[1]; out[2] = b[0]; out[3] = b[1];
    if (!(length > 1.0e-3f)) return 2;
    const float ux = lx / length, uy = ly / length;
    uint32_t beside = 0;
    for (uint32_t i = 0; i < discs; i++)
    {
        float s = (all[i].x - a[0]) * ux + (all[i].y - a[1]) * uy;
        s = s < 0.0f ? 0.0f : s > length ? length : s;
        const float dx = all[i].x - (a[0] + ux * s), dy = all[i].y - (a[1] + uy * s);
        if (!(all[i].r > 0.0f && sqrtf(dx * dx + dy * dy) < all[i].r + ROUND_REACH)) continue;
        if (beside < ROUND_NEAR) d[beside++] = all[i]; else g_roundCrowded++;
    }
    uint32_t m = (uint32_t)ceilf(length / ROUND_FINE) + 1;
    m = m < 2 ? 2 : m > ROUND_POINTS ? ROUND_POINTS : m;
    for (uint32_t i = 0; i < m; i++)
    {
        const float along = length * (float)i / (float)(m - 1);
        const int end = i == 0 || i + 1 == m;
        up[i] = end ? 0.0f : RoundOver(d, beside, a, ux, uy, along, 1.0f);
        down[i] = end ? 0.0f : RoundOver(d, beside, a, ux, uy, along, -1.0f);
        if (up[i] != 0.0f || down[i] != 0.0f) *bent = 1;
    }
    for (uint32_t i = 0; i < m;)
    {
        uint32_t j = i; float most[2] = {0.0f, 0.0f};
        while (j < m && (up[j] != 0.0f || down[j] != 0.0f))
        {
            if (fabsf(up[j]) > most[0]) most[0] = fabsf(up[j]);
            if (fabsf(down[j]) > most[1]) most[1] = fabsf(down[j]);
            j++;
        }
        const float* off = most[0] <= most[1] ? up : down;
        for (uint32_t k = i; k < j; k++)
        {
            const float along = length * (float)k / (float)(m - 1);
            out[2 * k] = a[0] + ux * along - uy * off[k]; out[2 * k + 1] = a[1] + uy * along + ux * off[k];
        }
        if (j < m) { const float along = length * (float)j / (float)(m - 1); out[2 * j] = a[0] + ux * along; out[2 * j + 1] = a[1] + uy * along; }
        i = j + 1;
    }
    out[0] = a[0]; out[1] = a[1]; out[2 * m - 2] = b[0]; out[2 * m - 1] = b[1];
    if (!*bent) return m;
    static const float kSpacing[5] = {64.0f, 32.0f, 16.0f, 8.0f, ROUND_FINE};
    for (int level = 0; level < 5; level++)
        for (int round = 0; round < 6; round++)
        {
            uint32_t n = (uint32_t)ceilf(RoundLength(out, m) / kSpacing[level]) + 1;
            n = n < 2 ? 2 : n > ROUND_POINTS ? ROUND_POINTS : n;
            RoundSpread(out, m, n, work);
            m = n;
            for (int pass = 0; pass < 8; pass++)
            {
                out[0] = work[0]; out[1] = work[1]; out[2 * m - 2] = work[2 * m - 2]; out[2 * m - 1] = work[2 * m - 1];
                for (uint32_t i = 1; i + 1 < m; i++)
                {
                    out[2 * i] = 0.25f * (work[2 * i - 2] + 2.0f * work[2 * i] + work[2 * i + 2]);
                    out[2 * i + 1] = 0.25f * (work[2 * i - 1] + 2.0f * work[2 * i + 1] + work[2 * i + 3]);
                    RoundPush(&out[2 * i], d, beside, -uy, ux);
                }
                memcpy(work, out, 8 * (size_t)m);
            }
        }
    return m;
}
/* The cosine of the sharpest turn between neighbouring pieces of a line (1: straight). */
static float RoundTurn(const float* p, uint32_t n)
{
    float least = 1.0f;
    for (uint32_t i = 1; i + 1 < n; i++)
    {
        const float ax = p[2 * i] - p[2 * i - 2], ay = p[2 * i + 1] - p[2 * i - 1], bx = p[2 * i + 2] - p[2 * i], by = p[2 * i + 3] - p[2 * i + 1];
        const float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        if (la < 1.0e-3f || lb < 1.0e-3f) continue;
        const float c = (ax * bx + ay * by) / (la * lb);
        if (c < least) least = c;
    }
    return least;
}
