/* ---- Near terrain: the outlines of ground patches broken up (game-free) -------------------------------------------
 * The game's zone painters fill a tile with soft shares of its materials and the terrain pixel shader cuts each
 * layer where its share passes one half (PS 364ba1ef: alpha = sat(5 (2 g(a) - 2 + h - h_below) + 0.5), g(a) =
 * 4a^3 - 6a^2 + 4a). The cut of a smooth share is a smooth curve, so a patch reads as spilled paint wherever the
 * layers' height detail is flat. Here the mix a texel is packed from is taken a little aside (a warp of the plane)
 * and each share is nudged by a fine noise, both only where the ground is a mix: the cut wanders and breaks up.
 * Every term is a function of world metres and integer hash noise in doubles with + - * / and floor only, so a
 * point gets the same breakup from any tile and on every peer. tests/patches_test.c pins it to its model. */
#define PATCH_WARP 1.5         /* m an axis: how far the mix is taken aside */
#define PATCH_WARP_WL 9.0
#define PATCH_WARP_FINE 0.35   /* the fine wave's part of the warp */
#define PATCH_WARP_FINE_WL 3.0
#define PATCH_SHARE 0.3        /* the most a share is nudged, at one half */
#define PATCH_SHARE_WL 1.7
#define PATCH_KEEP 0.95        /* a texel whose largest share is this or more stays as it is */
#define PATCH_RAMP 0.20        /* the breakup is whole from this far under PATCH_KEEP */
#define PATCH_DEFAULT_ON 0

/* How far the mix of (x, y) is taken aside, at full strength 1. */
static void PatchField(double x, double y, double strength, double* dx, double* dy)
{
#pragma STDC FP_CONTRACT OFF /* a fused multiply-add would differ from another peer's in the last bit */
    *dx = PATCH_WARP * strength * ((1 - PATCH_WARP_FINE) * NearMatNoise(x, y, PATCH_WARP_WL, 41) + PATCH_WARP_FINE * NearMatNoise(x, y, PATCH_WARP_FINE_WL, 42));
    *dy = PATCH_WARP * strength * ((1 - PATCH_WARP_FINE) * NearMatNoise(x, y, PATCH_WARP_WL, 43) + PATCH_WARP_FINE * NearMatNoise(x, y, PATCH_WARP_FINE_WL, 44));
}

/* mix's ids and shares at (x, y) with the outlines broken up; returns their count. *broken is 1 when the texel is
 * a mix the breakup works on. The shares still sum to 1, so what is drawn over them (paths, the track) is not dimmed. */
static int PatchBreak(NearMixFn mix, const void* ctx, double x, double y, double strength, uint32_t* id, double* w, int* broken)
{
#pragma STDC FP_CONTRACT OFF
    int n = mix(ctx, x, y, id, w);
    double top = 0.0;
    *broken = 0;
    for (int q = 0; q < n; q++) if (w[q] > top) top = w[q];
    if (!(strength > 0.0) || top >= PATCH_KEEP) return n;
    double g = (PATCH_KEEP - top) / PATCH_RAMP, dx, dy;
    g = g > 1.0 ? 1.0 : g;
    PatchField(x, y, strength, &dx, &dy);
    n = mix(ctx, x + g * dx, y + g * dy, id, w);
    double sum = 0.0;
    for (int q = 0; q < n; q++)
    {
        /* 0 where a share is 0 or 1: the noise moves a cut, it never draws a patch of its own */
        const double v = w[q] + g * (PATCH_SHARE * strength) * NearMatNoise(x, y, PATCH_SHARE_WL, 64 + id[q]) * (4.0 * w[q] * (1.0 - w[q]));
        w[q] = v < 0.0 ? 0.0 : v;
        sum += w[q];
    }
    if (sum > 1e-9) for (int q = 0; q < n; q++) w[q] /= sum;
    *broken = 1;
    return n;
}

/* Research switches (a research build only; any other build reads them as unset, so peers draw alike):
 * HELLFORGE_XL_NEAR_BREAKUP=0 / 1 turns the breakup off / on, HELLFORGE_XL_NEAR_BREAKUP_STRENGTH=<1..300> is its
 * strength in percent, HELLFORGE_XL_NEAR_GROUND=level draws a tile's ground from the level's map alone. */
static double PatchStrength(void)
{
    static volatile LONG read; static double strength;
    if (!read)
    {
        char t[8] = "";
        int on = PATCH_DEFAULT_ON;
        double s = 1.0;
        if (GetEnvironmentVariableA("HELLFORGE_XL_NEAR_BREAKUP", t, sizeof t) > 0) on = t[0] != '0';
        if (GetEnvironmentVariableA("HELLFORGE_XL_NEAR_BREAKUP_STRENGTH", t, sizeof t) > 0)
        { const int v = atoi(t); if (v >= 1 && v <= 300) s = v / 100.0; }
        strength = on ? s : 0.0;
        MemoryBarrier();
        read = 1;
    }
    return strength;
}

/* The plants read the level's map, and a tile's painters do not repeat it: their noise starts at the corner of the
 * span they draw. With this on a tile's ground is the level's map too (as its band), at the price of the tile's
 * own 1.1 m painter detail: for a picture of plants on their own ground. */
static int PatchLevelGround(void)
{
    static int on = -1;
    if (on < 0) { char t[8] = ""; on = GetEnvironmentVariableA("HELLFORGE_XL_NEAR_GROUND", t, sizeof t) > 0 && t[0] == 'l'; }
    return on;
}

/* A tile's own texel: the mix with the breakup at the build's strength; *broken counts the texels it worked on. */
static int PatchMix(NearMixFn mix, const void* ctx, double x, double y, uint32_t* id, double* w, LONG* broken)
{
    int did = 0;
    const int n = PatchBreak(mix, ctx, x, y, PatchStrength(), id, w, &did);
    *broken += did;
    return n;
}

static void PatchNote(LONG broken, int own)
{
    if (PatchLevelGround()) Note("difficulty: near terrain: the tile's ground is the level's map alone (HELLFORGE_XL_NEAR_GROUND=level, research)");
    else if (!own) return;
    else if (PatchStrength() > 0.0)
        Note("difficulty: near terrain: patch outlines broken up: strength %.0f %% (warp %.2f m at %.0f m and %.0f m, share %.2f at %.1f m): %ld mixed texels",
            PatchStrength() * 100.0, PATCH_WARP * PatchStrength(), PATCH_WARP_WL, PATCH_WARP_FINE_WL, PATCH_SHARE * PatchStrength(), PATCH_SHARE_WL, (long)broken);
    else Note("difficulty: near terrain: patch outlines as the painters left them (the breakup is off)");
}
