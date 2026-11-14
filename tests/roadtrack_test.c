/* The roads' worn track (proto/difficulty_world_roadtrack.c, difficulty_world_roadtracklevel.c): the profile against
 * its model (tests/roadtrack_model.py), the far profile as the terrain shader mixes it, the slot from the zone
 * types' painters, the mix into a material texel, the piece index, the places' flat ground and the plants' density. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double CoXlTestTrackWeight(double d, double x, double y);
double CoXlTestTrackCoarse(double d);
void CoXlTestTrackWarp(double x, double y, double* wx, double* wy);
uint32_t CoXlTestTrackSlot(uintptr_t a, uintptr_t b, uint32_t* state);
uint32_t CoXlTestTrackLevel(const uintptr_t* a, const uintptr_t* b, const double* metres, uint32_t n, uint32_t* road, double* clear);
uint32_t CoXlTestTrackMix(const uint32_t* id, const double* w, int n, int room, uint32_t slot, double c, uint32_t hole, double* sum);
int CoXlTestTrackIndex(const float* segs, const uint32_t* slots, uint32_t n, double world);
int CoXlTestTrackNearest(double x, double y, double reach, double* d, uint32_t* slot, uint32_t* tests);
int CoXlTestTrackFlats(const float* flats, uint32_t n, double world);
double CoXlTestTrackFlatShare(double x, double y);
uint32_t CoXlTestTrackAt(double world, double playable, int on, double x, double y, double texel, uint32_t ground, uint32_t* tally);
uint32_t CoXlTestTrackThin(double world, double playable, uint32_t* image, uint32_t side, uint32_t* bare);

static int g_failed, g_checked;
static const char* g_case = "";
#define CHECK(what) do { g_checked++; if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define REACH 3.2
#define FAR_REACH 3.6
#define WORLD 16384.0

static const struct { double d, x, y; uint64_t weight, wx, wy; } kModel[64] = {
    {0.0, -7142.209, 877.143, 0x3FEF9E6749350444ull, 0xC0BBE5E60806A279ull, 0x408B6AC2EFA2DCA4ull},
    {0.3, 7037.036, 88.12, 0x3FEE6A462DAA857Bull, 0x40BB7D248E622181ull, 0x405609C287B8612Dull},
    {0.9, -6079.075, -7789.638, 0x3FEE93374D3563E2ull, 0xC0B7BF2C0C2ADC89ull, 0xC0BE6D7BCD16DAF7ull},
    {1.2, 4640.973, -4319.052, 0x3FEE49E5015BF781ull, 0x40B2212338079E3Bull, 0xC0B0DF3C40E5AD55ull},
    {1.5, 7721.145, -5672.098, 0x3FE5E504B2D6096Eull, 0x40BE29464A071FF9ull, 0xC0B627AB01B89AE5ull},
    {1.7, 5714.9, 5447.58, 0x3FE331BA1359FCDDull, 0x40B6531F908E91F6ull, 0x40B547692FF9AB67ull},
    {1.9, 2515.6, 656.893, 0x3FE2D383F9FA63B4ull, 0x40A3A780E38EBCBAull, 0x4084873C9AA1F3CFull},
    {2.0, -1686.393, -3749.923, 0x3FDFF260BDF54355ull, 0xC09A5A302CF42513ull, 0xC0AD4BA91D66F764ull},
    {2.2, -4278.314, -5741.787, 0x3FCE73F121D18412ull, 0xC0B0B68FC1C57004ull, 0xC0B66DE295FC6258ull},
    {2.4, 7170.885, 1071.713, 0x3FB1FECFBF944790ull, 0x40BC02BBEE0BA351ull, 0x4090BDE206992BE1ull},
    {2.6, 6306.404, 5651.472, 0x3F9CF842D18ECA90ull, 0x40B8A26FDD8F070Full, 0x40B6130F6D136979ull},
    {2.8, 6841.704, -1725.975, 0x0000000000000000ull, 0x40BAB944975BE369ull, 0xC09AF635504B8251ull},
    {3.0, -313.267, 4522.858, 0x0000000000000000ull, 0xC07393150FDED5E6ull, 0x40B1AA80CE073B4Bull},
    {3.1, 3830.216, 2298.091, 0x0000000000000000ull, 0x40ADEC741B458735ull, 0x40A1F408384D0D47ull},
    {3.19, 7925.05, 7713.35, 0x0000000000000000ull, 0x40BEF4AE26D8B218ull, 0x40BE2129DD04CC66ull},
    {3.2, -2339.009, -5284.913, 0x0000000000000000ull, 0xC0A245AA7AA90178ull, 0xC0B4A4E94F34690Bull},
    {0.0, -5257.18, -3439.38, 0x3FEFBD0546223E5Cull, 0xC0B488F5903838F7ull, 0xC0AADEA4C891610Cull},
    {0.3, -7284.67, -1155.76, 0x3FEED2B26F9A483Cull, 0xC0BC74A2A48A0277ull, 0xC0920F774BD6B3A4ull},
    {0.9, -607.13, -1799.9, 0x3FEFB18950085465ull, 0xC082F64D7E799493ull, 0xC09C20F5C3D01771ull},
    {1.2, -924.5, -4312.66, 0x3FEB7916BF5D8A87ull, 0xC08CE74A50597936ull, 0xC0B0D8ED228C3955ull},
    {1.5, -7893.83, -2736.55, 0x3FEC948CF75A7EF5ull, 0xC0BED58C9272CF27ull, 0xC0A5614FB92BEB75ull},
    {1.7, -7574.76, -1147.82, 0x3FEA72682AABC051ull, 0xC0BD970DA346452Eull, 0xC091EF5BAB4D59B3ull},
    {1.9, -6609.26, -2307.19, 0x3FC8E63BCA8BA816ull, 0xC0B9D193B3BC309Full, 0xC0A2066D874BDA79ull},
    {2.0, -1221.83, -6016.9, 0x3FE37A91449F09B4ull, 0xC093169998C32811ull, 0xC0B78106992EF7E3ull},
    {2.2, -6683.6, -1849.46, 0x3FC7CBACBC7ECA6Dull, 0xC0BA1BD11AD6AF36ull, 0xC09CE5EFF997D323ull},
    {2.4, -498.29, -3461.75, 0x3FC7F955BFD347FAull, 0xC07F24E38FD0D0D1ull, 0xC0AB0B0B48061794ull},
    {2.6, -4137.77, -2921.88, 0x3F94D08DBCC8BC92ull, 0xC0B029E1DB7B5A3Bull, 0xC0A6D42811B3B50Eull},
    {2.8, -4948.46, -6956.77, 0x3FAB25E3A4137B4Eull, 0xC0B3546799C9D652ull, 0xC0BB2C82422624A4ull},
    {3.0, -2724.65, -2895.59, 0x0000000000000000ull, 0xC0A548F248631117ull, 0xC0A69E5D29F67A40ull},
    {3.1, -6248.6, -6431.55, 0x0000000000000000ull, 0xC0B8689C4847F83Full, 0xC0B91F5014864020ull},
    {3.19, -7841.95, -8181.8, 0x0000000000000000ull, 0xC0BEA1BC1C1F78D0ull, 0xC0BFF5FF4FED6439ull},
    {3.2, -4040.94, -1264.71, 0x0000000000000000ull, 0xC0AF91A1056A2E4Cull, 0xC093C29FABE42AFEull},
    {0.0, -14355.8, -753308.5, 0x3FEE3BAFA0C7EF82ull, 0xC0CC09F70494D21Eull, 0xC126FD3995B21D4Full},
    {0.3, 388326.1, 270491.7, 0x3FEF8936CA42AB0Bull, 0x4117B399BF534447ull, 0x4110826EA9EA911Eull},
    {0.9, 495550.4, -79388.5, 0x3FEC29E702992805ull, 0x411E3EF8D9B3CCB9ull, 0xC0F361C93A03A197ull},
    {1.2, 532705.3, 36862.1, 0x3FEC6546D2A6B19Cull, 0x412041C1F6059946ull, 0x40E1FFC6049A8736ull},
    {1.5, -491451.0, 215108.4, 0x3FEE82A5C1189900ull, 0xC11DFEEB39AB6058ull, 0x410A422131D03181ull},
    {1.7, -10144.0, 196241.0, 0x3FCAD42B89C9A733ull, 0xC0C3CFF3EBB82753ull, 0x4107F486AABF8516ull},
    {1.9, 882637.1, 325663.6, 0x3FCDDADB21F334E8ull, 0x412AEF9A5329E01Aull, 0x4113E07EE589FA7Bull},
    {2.0, 649347.2, 398968.1, 0x3FC5B357E691E7E4ull, 0x4123D106F25AF148ull, 0x411859DF71637776ull},
    {2.2, 420288.7, -348002.6, 0x3FA8A67C847FCFC2ull, 0x4119A703F719370Bull, 0xC1153D8C316421D1ull},
    {2.4, 774798.7, 353669.8, 0x3FC3D19E79F97888ull, 0x4127A51DFF7480DBull, 0x4115961634F9BE69ull},
    {2.6, 925296.2, -691287.3, 0x3FBAE0AA5FB0076Aull, 0x412C3CDFB6061590ull, 0xC12518AF75BD6C3Cull},
    {2.8, 924296.0, -485372.3, 0x0000000000000000ull, 0x412C350FEA03E0EAull, 0xC11D9FF1D3906379ull},
    {3.0, 631334.4, -257066.2, 0x0000000000000000ull, 0x4123444C30D3B0FAull, 0xC10F614FE40F4EA7ull},
    {3.1, 531643.7, -976391.6, 0x0000000000000000ull, 0x412039773FAA945Bull, 0xC12DCC0F670248CFull},
    {3.19, -189503.7, 748366.5, 0x0000000000000000ull, 0xC10721FEC67C6D2Dull, 0x4126D69D8648FE82ull},
    {3.2, 315798.2, -134530.0, 0x0000000000000000ull, 0x411346595E0ED34Eull, 0xC1006C10335A8B37ull},
    {0.0, -30.0316, -21.1487, 0x3FEDB2A9B6A7645Aull, 0xC03E4216957D0F8Bull, 0xC035802F3B68273Aull},
    {0.3, 29.4183, 1.4996, 0x3FEFC2AAE008FB53ull, 0x403D8077461171B2ull, 0x3FF5F2174A209F54ull},
    {0.9, 19.2219, 0.6288, 0x3FEFB5E2DD6910FDull, 0x4033497EB3568C54ull, 0x3FE1DAB33CA08324ull},
    {1.2, 28.324, 16.094, 0x3FEE3BA8A22F2BB5ull, 0x403C88D89DCA9B6Full, 0x402F92387F26EE08ull},
    {1.5, -17.6468, 18.9386, 0x3FEFFEF4DB5D47F3ull, 0xC031A1D917B3ED9Eull, 0x4033130C9D15B70Full},
    {1.7, 20.885, -18.2522, 0x3FE88C09850A7E93ull, 0x4034D21EDEE2DAEFull, 0xC0328A1DB23308E2ull},
    {1.9, 39.3051, -34.1648, 0x3FD512D30C416F01ull, 0x40438C12F97CF9D2ull, 0xC041319F3B6FFA15ull},
    {2.0, 30.4866, -37.7128, 0x3FADB0EC107E40FCull, 0x403E88C5BF6EE8F8ull, 0xC042E8D9CD62217Full},
    {2.2, -10.718, -4.4975, 0x3FC077F53943832Eull, 0xC025B4B6106852EBull, 0xC011BA8670DA2AA7ull},
    {2.4, 34.1675, -35.3963, 0x3F628A94ED493804ull, 0x40410BDB56B840AFull, 0xC041CA2D7281BCA8ull},
    {2.6, 28.91, 28.9854, 0x3F52138E3336603Eull, 0x403D28D990849811ull, 0x403C9E80ED50E818ull},
    {2.8, -18.7439, 24.5694, 0x3EA46328054867CAull, 0xC032C0DA0AFFF86Aull, 0x4038B76272523D0Bull},
    {3.0, -31.799, -1.005, 0x0000000000000000ull, 0xC03FACD2D606996Eull, 0xBFF0E5AC54844392ull},
    {3.1, -3.7835, 10.3468, 0x0000000000000000ull, 0xC00E697529BDD4BAull, 0x40252E34EC2C3713ull},
    {3.19, 17.7124, 4.4909, 0x0000000000000000ull, 0x4031C57F35B08E47ull, 0x4011B1F8BAEEED6Eull},
    {3.2, -32.9092, -24.1119, 0x0000000000000000ull, 0xC040969476889414ull, 0xC038740C741CB2BCull},
};

static uint64_t Bits(double v) { uint64_t b; memcpy(&b, &v, 8); return b; }
static uint32_t g_rand = 12345u;
static double Rand(void) { g_rand = g_rand * 1664525u + 1013904223u; return (g_rand >> 8) / 16777216.0; }

static void Profile(void)
{
    g_case = "no weight from the reach on, never outside 0 to 1";
    for (int k = 0; k < 10000; k++)
    {
        const double x = (Rand() - 0.5) * WORLD, y = (Rand() - 0.5) * WORLD, d = REACH + Rand() * 4.0, w = CoXlTestTrackWeight(Rand() * REACH, x, y);
        CHECK(CoXlTestTrackWeight(d, x, y) == 0.0 && w >= 0.0 && w <= 1.0);
    }
    CHECK(CoXlTestTrackWeight(REACH, 10.0, 20.0) == 0.0);
    g_case = "the core is nearly bare ground everywhere";
    double sum = 0.0, least = 1.0; int n = 0;
    for (int k = 0; k < 4000; k++)
        for (int q = 0; q < 3; q++)
        {
            const double w = CoXlTestTrackWeight(0.5 * q, 3.3 * k - 9000.0, 7.1 * k + 5.0);
            sum += w; n++; if (w < least) least = w;
        }
    CHECK(least >= 0.70 && sum / n >= 0.90);
    g_case = "the half-weight edge wanders: no straight edge, no gap";
    double mean = 0.0, sq = 0.0, lo = 9.0, hi = 0.0;
    for (int k = 0; k < 2000; k++)
    {
        double d = 0.0;
        while (d < REACH && CoXlTestTrackWeight(d, 2.9 * k, 1.3 * k) >= 0.5) d += 0.05;
        mean += d; sq += d * d; if (d < lo) lo = d; if (d > hi) hi = d;
    }
    mean /= 2000; const double sd = sqrt(sq / 2000 - mean * mean);
    CHECK(mean > 1.8 && mean < 2.1 && lo > 1.2 && hi < 2.7 && sd > 0.15 && sd < 0.35);
    g_case = "the weight and the meander are the model's to the last bit, also at negative and large coordinates";
    int nonzero = 0;
    for (int k = 0; k < 64; k++)
    {
        double wx, wy;
        const double w = CoXlTestTrackWeight(kModel[k].d, kModel[k].x, kModel[k].y);
        CoXlTestTrackWarp(kModel[k].x, kModel[k].y, &wx, &wy);
        CHECK(Bits(w) == kModel[k].weight && Bits(wx) == kModel[k].wx && Bits(wy) == kModel[k].wy);
        nonzero += w > 0.0 && w < 1.0;
    }
    CHECK(nonzero >= 30);
    g_case = "the line itself wanders by under 0.71 m, slowly: the track meanders";
    double shift = 0.0, most = 0.0, step = 0.0;
    for (int k = 0; k < 4000; k++)
    {
        double wx, wy, vx, vy;
        const double x = 0.5 * k - 700.0, y = 0.2 * k + 90.0;
        CoXlTestTrackWarp(x, y, &wx, &wy); CoXlTestTrackWarp(x + 0.5, y + 0.2, &vx, &vy);
        const double off = hypot(wx - x, wy - y), jump = hypot((vx - wx) - 0.5, (vy - wy) - 0.2);
        shift += off; if (off > most) most = off; if (jump > step) step = jump;
    }
    CHECK(most <= 0.7072 && shift / 4000 > 0.15 && step < 0.08);
}

/* The far terrain mixes the four texels about a pixel; the track shows where the mix passes the shader's cut. */
static double g_far[64][64];
static double FarMix(double x, double y)
{
    const double u = x / 4.0, v = y / 4.0, i = floor(u), j = floor(v), fu = u - i, fv = v - j;
    const int a = (int)i, b = (int)j;
    if (a < 0 || b < 0 || a > 62 || b > 62) return 0.0;
    return (g_far[b][a] * (1 - fu) + g_far[b][a + 1] * fu) * (1 - fv) + (g_far[b + 1][a] * (1 - fu) + g_far[b + 1][a + 1] * fu) * fv;
}
static void Far(void)
{
    g_case = "the far profile is whole within 2 m and gone from 3.6 m";
    CHECK(CoXlTestTrackCoarse(0.0) == 1.0 && CoXlTestTrackCoarse(2.0) == 1.0 && CoXlTestTrackCoarse(FAR_REACH) == 0.0 && CoXlTestTrackCoarse(9.0) == 0.0);
    CHECK(fabs(CoXlTestTrackCoarse(2.8) - 0.5) < 1e-9);
    g_case = "on the 4 m lattice the mixed peak across the road stays over 0.74 at every angle and offset: no gaps, no beads";
    static const double deg[5] = {0, 7, 15, 27, 45};
    double least = 1.0, narrow = 99.0, wide = 0.0;
    for (int a = 0; a < 5; a++)
        for (int o = 0; o < 3; o++)
        {
            const double dx = cos(deg[a] * 0.017453292519943295), dy = sin(deg[a] * 0.017453292519943295), nx = -dy, ny = dx, off = 40.0 + o;
            for (int r = 0; r < 64; r++)
                for (int c = 0; c < 64; c++) g_far[r][c] = CoXlTestTrackCoarse(fabs((c * 4.0 - 20.0) * nx + (r * 4.0 - off) * ny));
            for (double s = 0.0; s < 140.0; s += 0.5)
            {
                double peak = 0.0; int over = 0;
                for (int t = -40; t <= 40; t++)
                {
                    const double w = FarMix(20.0 + s * dx + nx * t * 0.25, off + s * dy + ny * t * 0.25);
                    if (w > peak) peak = w;
                    over += w > 0.5;
                }
                if (peak < least) least = peak;
                if (over * 0.25 < narrow) narrow = over * 0.25;
                if (over * 0.25 > wide) wide = over * 0.25;
            }
        }
    CHECK(least >= 0.74 && narrow >= 3.5 && wide <= 8.0);
}

/* A zone type's descriptor as the game lays it out: painters +0x60, their count +0x68; a painter 0x18 bytes
 * (resource, variables, count); a variable 0x1C bytes (name hash, value +4, type +0x18). */
#define PATH 0x8448ABC93DDACF85ull
#define FILL 0xD45834942A4A5581ull
#define SLOPE 0xB6E115979B70D004ull
#define MATERIAL 0xEAC0B497u
typedef struct Type { uint8_t desc[0x170]; uint8_t list[16 * 0x18]; uint8_t vars[16][2 * 0x1C]; uint32_t n; } Type;
static void TypeAdd(Type* t, uint64_t resource, float material)
{
    const uint64_t list = (uint64_t)(uintptr_t)t->list, vars = (uint64_t)(uintptr_t)t->vars[t->n];
    const uint32_t count = 2, other = 0x12345678u, name = MATERIAL;
    memcpy(t->vars[t->n], &other, 4);                 /* a variable before it, as the noise settings are */
    memcpy(t->vars[t->n] + 0x1C, &name, 4); memcpy(t->vars[t->n] + 0x1C + 4, &material, 4);
    memcpy(t->list + 0x18 * t->n, &resource, 8); memcpy(t->list + 0x18 * t->n + 8, &vars, 8); memcpy(t->list + 0x18 * t->n + 0x10, &count, 4);
    t->n++;
    memcpy(t->desc + 0x60, &list, 8); memcpy(t->desc + 0x68, &t->n, 4);
}
/* A biome's own type names its region input at +0x100; a faction's place has none. */
static void Nature(Type* t) { static const char input[] = "Primordial_RegionInput"; const uint64_t at = (uint64_t)(uintptr_t)input; memcpy(t->desc + 0x100, &at, 8); }
static Type g_t30, g_t65, g_t67, g_t68, g_bare, g_bad;
static uintptr_t At(Type* t) { return (uintptr_t)t->desc; }
static void Types(void)
{
    static const float f30[8] = {20, 20, 24, 2, 22, 23, 6, 6}, f67[5] = {13, 9, 3, 3, 12};
    for (int k = 0; k < 8; k++) TypeAdd(&g_t30, FILL, f30[k]);
    TypeAdd(&g_t30, PATH, 1); TypeAdd(&g_t30, PATH, 1); TypeAdd(&g_t30, PATH, 6); TypeAdd(&g_t30, FILL, 7);
    TypeAdd(&g_t65, FILL, 9); TypeAdd(&g_t65, PATH, 1);
    for (int k = 0; k < 5; k++) TypeAdd(&g_t67, FILL, f67[k]);
    TypeAdd(&g_t67, SLOPE, 5); TypeAdd(&g_t67, PATH, 1);
    TypeAdd(&g_t68, FILL, 1); TypeAdd(&g_t68, SLOPE, 5); TypeAdd(&g_t68, FILL, 3); TypeAdd(&g_t68, SLOPE, 4); TypeAdd(&g_t68, PATH, 6);
    TypeAdd(&g_bare, FILL, 9); TypeAdd(&g_bare, SLOPE, 5);
    Nature(&g_t65); Nature(&g_t67); Nature(&g_t68); Nature(&g_bare);
}
static void Slots(void)
{
    uint32_t state = 0;
    Types();
    g_case = "a road's own slot: the first path slot that is ground on neither side (the run's seven side pairs)";
    CHECK(CoXlTestTrackSlot(At(&g_t67), At(&g_t68), &state) == 6 && state == (7u | 1u << 8 | 6u << 16));
    CHECK(CoXlTestTrackSlot(At(&g_t68), 0, &state) == 6 && (state >> 16) == 4);
    CHECK(CoXlTestTrackSlot(At(&g_t67), 0, &state) == 1);
    CHECK(CoXlTestTrackSlot(At(&g_t65), At(&g_t67), &state) == 1);
    CHECK(CoXlTestTrackSlot(At(&g_t65), At(&g_t68), &state) == 6);
    CHECK(CoXlTestTrackSlot(At(&g_t65), 0, &state) == 1);
    CHECK(CoXlTestTrackSlot(At(&g_t30), At(&g_t65), &state) == 1 && ((state >> 8) & 255) == 3 && (state >> 16) == 9);
    CHECK(CoXlTestTrackSlot(At(&g_t30), At(&g_t68), &state) == 1); /* every path slot is ground on a side: the first */
    g_case = "a side without a path painter leaves the other's; neither side: no slot";
    CHECK(CoXlTestTrackSlot(At(&g_bare), At(&g_t68), &state) == 6 && CoXlTestTrackSlot(At(&g_bare), At(&g_t65), &state) == 1);
    CHECK(CoXlTestTrackSlot(At(&g_bare), 0, &state) == 0 && CoXlTestTrackSlot(At(&g_bare), At(&g_bare), &state) == 0);
    g_case = "a descriptor that is not the reviewed shape has no painters";
    g_bad = g_t68;
    { const uint32_t many = 65; memcpy(g_bad.desc + 0x68, &many, 4); }
    CHECK(CoXlTestTrackSlot(At(&g_bad), 0, &state) == 0 && (state & 1) == 0);
    g_bad = g_t68;
    { const uint64_t none = 0; memcpy(g_bad.desc + 0x60, &none, 8); }
    CHECK(CoXlTestTrackSlot(At(&g_bad), 0, &state) == 0 && (state & 1) == 0);
    { const uint64_t wild = 0x10; memcpy(g_bad.desc + 0x60, &wild, 8); }
    CHECK(CoXlTestTrackSlot(At(&g_bad), 0, &state) == 0 && (state & 1) == 0);
    g_bad = g_t68; /* its own copy of the list, the path painter's variables unreadable */
    { const uint64_t list = (uint64_t)(uintptr_t)g_bad.list, wild = 0x10; memcpy(g_bad.desc + 0x60, &list, 8); memcpy(g_bad.list + 0x18 * 4 + 8, &wild, 8); }
    CHECK(CoXlTestTrackSlot(At(&g_bad), 0, &state) == 0 && (state & 1) == 0);
    CHECK(CoXlTestTrackSlot(0, 0, &state) == 0 && CoXlTestTrackSlot(At(&g_bad), At(&g_t65), &state) == 1);
    g_case = "a material that is no slot is ignored";
    { static Type odd; TypeAdd(&odd, PATH, 0.0f); TypeAdd(&odd, PATH, 40.0f); TypeAdd(&odd, PATH, 12.0f); CHECK(CoXlTestTrackSlot(At(&odd), 0, &state) == 12 && ((state >> 8) & 255) == 1); }

    g_case = "one slot a level: ground on neither side along the most road (the run's level: 6), a road's own only where that is ground on both sides";
    const uintptr_t a[7] = {At(&g_t67), At(&g_t68), At(&g_t67), At(&g_t65), At(&g_t65), At(&g_t65), At(&g_t30)};
    const uintptr_t b[7] = {At(&g_t68), 0, 0, At(&g_t67), At(&g_t68), 0, 0};
    const double m[7] = {112, 55, 38, 10, 9, 5, 5};
    uint32_t road[16]; double clear = 0.0;
    CHECK(CoXlTestTrackLevel(a, b, m, 7, road, &clear) == 6 && clear == 229.0);
    for (int k = 0; k < 6; k++) CHECK(road[k] == 6);
    CHECK(road[6] == 1);
    { const uintptr_t a2[2] = {At(&g_t30), 0}, b2[2] = {At(&g_t68), 0}; const double m2[2] = {5, 3};
      CHECK(CoXlTestTrackLevel(a2, b2, m2, 2, road, &clear) == 6 && road[0] == 6 && road[1] == 6); } /* the colony's slot 1 is no candidate; a road without types takes the level's */
    g_case = "a place's length does not count: on a tie of clear path length, the path slot along the most; no path painter anywhere: no slot";
    { const double m3[7] = {1, 1, 1, 1, 1, 1, 500}; CHECK(CoXlTestTrackLevel(a, b, m3, 7, road, &clear) == 1 && road[1] == 6 && road[2] == 1 && road[0] == 1); }
    { const uintptr_t a4[1] = {At(&g_bare)}, b4[1] = {0}; const double m4[1] = {9}; CHECK(CoXlTestTrackLevel(a4, b4, m4, 1, road, &clear) == 0 && road[0] == 0); }
}

static uint32_t Texel(uint32_t a, uint32_t b, uint32_t c, uint32_t wb, uint32_t wc) { return a << 26 | b << 21 | c << 16 | wb << 8 | wc; }
static void Mix(void)
{
    double sum = 0.0;
    g_case = "the track's byte does not depend on the ground under it";
    const uint32_t idA[2] = {9, 13}, idB[2] = {3, 12}; const double wA[2] = {0.7, 0.3}, wB[2] = {0.2, 0.8};
    const uint32_t ta = CoXlTestTrackMix(idA, wA, 2, 14, 6, 0.6, 0, &sum), tb = CoXlTestTrackMix(idB, wB, 2, 14, 1, 0.6, 0, &sum);
    CHECK(((ta >> 26) & 31) == 6 && ((tb >> 26) & 31) == 1 && fabs(sum - 1.0) < 1e-12);
    CHECK(255 - ((ta >> 8) & 255) - (ta & 255) == 153 && 255 - ((tb >> 8) & 255) - (tb & 255) == 153);
    const uint32_t tc = CoXlTestTrackMix(idA, wA, 2, 14, 20, 0.6, 0, &sum), td = CoXlTestTrackMix(idB, wB, 2, 14, 20, 0.6, 0, &sum);
    CHECK(((tc >> 16) & 31) == 20 && (tc & 255) == 153 && (td & 255) == 153);
    g_case = "a whole track is the slot alone; none leaves the texel as it was; the hole bit stays";
    CHECK(CoXlTestTrackMix(idA, wA, 2, 14, 6, 1.0, 0x80000000u, &sum) == (Texel(6, 0, 0, 0, 0) | 0x80000000u) && fabs(sum - 1.0) < 1e-12);
    CHECK(CoXlTestTrackMix(idA, wA, 2, 14, 6, 2.5, 0, &sum) == Texel(6, 0, 0, 0, 0));
    CHECK(CoXlTestTrackMix(idA, wA, 2, 14, 6, 0.0, 0x80000000u, &sum) == (Texel(9, 13, 0, 77, 0) | 0x80000000u));
    CHECK(CoXlTestTrackMix(idA, wA, 2, 14, 0, 0.5, 0, &sum) == Texel(9, 13, 0, 77, 0) && CoXlTestTrackMix(idA, wA, 2, 14, 32, 0.5, 0, &sum) == Texel(9, 13, 0, 77, 0));
    g_case = "the slot already in the ground gains the share; the smallest of four is dropped";
    CHECK(CoXlTestTrackMix(idA, wA, 2, 14, 13, 0.5, 0, &sum) == Texel(9, 13, 0, 166, 0)); /* 0.35 and 0.65 */
    const uint32_t id3[3] = {3, 9, 13}; const double w3[3] = {0.5, 0.3, 0.2};
    const uint32_t t4 = CoXlTestTrackMix(id3, w3, 3, 14, 6, 0.5, 0, &sum); /* 0.25, 0.15, 0.10 and 0.5: 13 goes */
    CHECK(((t4 >> 26) & 31) == 3 && ((t4 >> 21) & 31) == 6 && ((t4 >> 16) & 31) == 9 && ((t4 >> 8) & 255) == 142 && abs((int)(t4 & 255) - 43) <= 1);
    g_case = "no place left for another id: the texel stays as it was";
    CHECK(CoXlTestTrackMix(id3, w3, 3, 3, 6, 0.5, 0, &sum) == Texel(3, 9, 13, 77, 51) && fabs(sum - 1.0) < 1e-12);
    CHECK(CoXlTestTrackMix(id3, w3, 3, 3, 9, 1.0, 0, &sum) == Texel(9, 0, 0, 0, 0));
}

#define PIECES 400
static float g_seg[4 * PIECES]; static uint32_t g_slot[PIECES];
static double Brute(double x, double y, uint32_t n)
{
    double best = 1e30;
    for (uint32_t k = 0; k < n; k++)
    {
        const double ax = g_seg[4 * k], ay = g_seg[4 * k + 1], dx = g_seg[4 * k + 2] - ax, dy = g_seg[4 * k + 3] - ay, l2 = dx * dx + dy * dy;
        double u = l2 > 1e-9 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0.0;
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        const double d = hypot(ax + u * dx - x, ay + u * dy - y);
        if (d < best) best = d;
    }
    return best;
}
static void Index(void)
{
    g_case = "the nearest piece is the brute-force nearest, in every quarter of the world";
    for (uint32_t k = 0; k < PIECES; k++)
    {
        const float x = (float)(k % 4 < 2 ? -1 : 1) * (200.0f + 7.5f * (float)(k / 4)), y = (float)(k % 2 ? -1 : 1) * (150.0f + 60.0f * sinf(0.05f * (float)k));
        g_seg[4 * k] = x; g_seg[4 * k + 1] = y; g_seg[4 * k + 2] = x + 7.0f; g_seg[4 * k + 3] = y + 4.0f * cosf(0.3f * (float)k);
        g_slot[k] = 6;
    }
    CHECK(CoXlTestTrackIndex(g_seg, g_slot, PIECES, WORLD) == 1);
    int near = 0;
    for (int k = 0; k < 1000; k++)
    {
        const uint32_t piece = (uint32_t)(Rand() * PIECES);
        const double x = g_seg[4 * piece] + Rand() * 16.0 - 4.0, y = g_seg[4 * piece + 1] + Rand() * 12.0 - 6.0, brute = Brute(x, y, PIECES);
        double d = -1.0; uint32_t slot = 0;
        const int found = CoXlTestTrackNearest(x, y, FAR_REACH, &d, &slot, NULL);
        CHECK(found == (brute < FAR_REACH) && (!found || (fabs(d - brute) < 1e-9 && slot == 6)));
        near += found;
    }
    CHECK(near > 200 && near < 900);
    g_case = "a cell no piece reaches costs no distance; off the world: nothing";
    double d; uint32_t slot, tests = 0;
    CHECK(CoXlTestTrackNearest(5000.0, 5000.0, FAR_REACH, &d, &slot, &tests) == 0 && tests == 0);
    CHECK(CoXlTestTrackNearest(-9000.0, 0.0, FAR_REACH, &d, &slot, &tests) == 0 && CoXlTestTrackNearest(0.0, 8192.0, FAR_REACH, &d, &slot, &tests) == 0 && tests == 0);
    CHECK(CoXlTestTrackNearest(g_seg[0] + 1.0, g_seg[1], FAR_REACH, &d, &slot, &tests) == 1 && tests > 0 && tests < 40);
    g_case = "a piece without a slot is not indexed; no pieces: nothing";
    { const float one[4] = {0, 0, 10, 0}; const uint32_t none = 0, wild = 32;
      CHECK(CoXlTestTrackIndex(one, &none, 1, WORLD) == 0 && CoXlTestTrackNearest(5.0, 0.0, FAR_REACH, &d, &slot, NULL) == 0);
      CHECK(CoXlTestTrackIndex(one, &wild, 1, WORLD) == 0); }
}

static void Boundary(void)
{
    g_case = "where the slot changes along a road both slots show within 6 m and the change is no line across the track";
    int both = 0, straight = 0; double span = 0.0;
    for (int k = 0; k < 200; k++)
    {
        const float ox = 37.3f * (float)k - 3700.0f, oy = 21.9f * (float)k - 2000.0f;
        const float seg[8] = {ox - 40.0f, oy, ox, oy, ox, oy, ox + 40.0f, oy};
        const uint32_t slots[2] = {1, 6};
        CoXlTestTrackIndex(seg, slots, 2, WORLD);
        int seen1 = 0, seen6 = 0, run = 0, longest = 0; double last = 0.0, lo = 99.0, hi = -99.0;
        for (int j = -19; j <= 19; j++)
        {
            int ones = 0;
            for (int i = -60; i <= 60; i++)
            {
                double d; uint32_t slot = 0;
                CHECK(CoXlTestTrackNearest(ox + 0.1 * i, oy + 0.1 * j, REACH, &d, &slot, NULL) == 1 && (slot == 1 || slot == 6));
                ones += slot == 1; seen1 |= slot == 1; seen6 |= slot == 6;
            }
            const double at = 0.1 * ones - 6.0;
            if (j > -19) { if (fabs(at - last) < 0.05) { run++; if (run > longest) longest = run; } else run = 0; }
            last = at; if (at < lo) lo = at; if (at > hi) hi = at;
        }
        both += seen1 && seen6; straight += longest * 0.1 > 2.0; span += hi - lo;
        double d; uint32_t slot = 0; /* away from the change each piece keeps its own */
        CHECK(CoXlTestTrackNearest(ox - 20.0, oy + 1.0, REACH, &d, &slot, NULL) == 1 && slot == 1);
        CHECK(CoXlTestTrackNearest(ox + 20.0, oy - 1.0, REACH, &d, &slot, NULL) == 1 && slot == 6);
    }
    CHECK(both == 200 && straight == 0 && span / 200 > 1.0);
}

static void Places(void)
{
    g_case = "the track fades in over 8 m from a place's flat ground, a circle's or a rectangle's";
    const float flats[12] = {100, 0, 30, 0, 0, 0, /* a circle of 30 m */ -200, 50, 40, 10, 0.5235988f, 1 /* 80 x 20 m turned 30 degrees */};
    CHECK(CoXlTestTrackFlats(flats, 2, WORLD) == 1);
    CHECK(CoXlTestTrackFlatShare(100.0, 0.0) == 0.0 && CoXlTestTrackFlatShare(129.9, 0.0) == 0.0 && CoXlTestTrackFlatShare(100.0, -38.0) == 1.0 && CoXlTestTrackFlatShare(0.0, 0.0) == 1.0);
    CHECK(fabs(CoXlTestTrackFlatShare(134.0, 0.0) - 0.5) < 1e-6);
    double last = 0.0;
    for (int k = 0; k <= 100; k++) { const double s = CoXlTestTrackFlatShare(128.0 + 0.12 * k, 0.0); CHECK(s >= last && s - last < 0.03); last = s; }
    const double c = cos(0.5235988), s = sin(0.5235988);
    CHECK(CoXlTestTrackFlatShare(-200.0 + 39.0 * c, 50.0 + 39.0 * s) == 0.0 && CoXlTestTrackFlatShare(-200.0 + 48.1 * c, 50.0 + 48.1 * s) == 1.0);
    CHECK(fabs(CoXlTestTrackFlatShare(-200.0 + 44.0 * c, 50.0 + 44.0 * s) - 0.5) < 1e-4);
    CHECK(CoXlTestTrackFlatShare(-200.0 - 9.0 * s, 50.0 + 9.0 * c) == 0.0 && fabs(CoXlTestTrackFlatShare(-200.0 - 14.0 * s, 50.0 + 14.0 * c) - 0.5) < 1e-4);

    g_case = "a texel of the level's map and of a tile on a road: the slot mixed in, counted; off: untouched";
    const float road[4] = {-300, 0, 300, 0}; const uint32_t six = 6;
    CoXlTestTrackIndex(road, &six, 1, WORLD);
    const uint32_t ground = Texel(3, 9, 13, 77, 51);
    uint32_t tally[32] = {0};
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 1, 0.0, 1.0, 4.0, ground, tally) == Texel(6, 0, 0, 0, 0) && tally[6] == 1);
    const uint32_t far = CoXlTestTrackAt(WORLD, 7000.0, 1, 0.0, 2.8, 4.0, ground, tally); /* half: 0.25, 0.15 and 0.5; 13 dropped */
    CHECK(((far >> 26) & 31) == 3 && ((far >> 21) & 31) == 6 && ((far >> 16) & 31) == 9 && tally[6] == 2);
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 1, 0.0, 3.7, 4.0, ground, tally) == ground && tally[6] == 2);
    const uint32_t tile = CoXlTestTrackAt(WORLD, 7000.0, 1, 0.0, 0.2, 0.5625, ground, tally);
    CHECK(((tile >> 26) & 31) == 3 || ((tile >> 26) & 31) == 6);
    CHECK((((tile >> 26) & 31) == 6 ? 255 - ((tile >> 8) & 255) - (tile & 255) : (tile >> 8) & 255) >= 178 && tally[6] == 3); /* the core: 0.70 at least */
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 1, 0.0, 3.95, 0.5625, ground, tally) == ground); /* past the reach and the meander */
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 0, 0.0, 1.0, 4.0, ground, tally) == ground && CoXlTestTrackAt(WORLD, 7000.0, 0, 0.0, 0.2, 0.5625, ground, tally) == ground && tally[6] == 3);
    g_case = "on a place's flat ground and past the playable radius the ground stays, and the texel is counted as left";
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 1, 100.0, 1.0, 4.0, ground, tally) == ground && tally[0] == 1 && tally[6] == 3);
    CHECK(CoXlTestTrackAt(WORLD, 7000.0, 1, 100.0, 0.2, 0.5625, ground, tally) == ground && tally[0] == 2);
    CHECK(CoXlTestTrackAt(WORLD, 200.0, 1, 250.0, 1.0, 4.0, ground, tally) == ground && tally[0] == 3);
    const uint32_t rim = CoXlTestTrackAt(WORLD, 282.0, 1, 250.0, 0.0, 4.0, ground, tally); /* 32 m inside a 64 m rim: half */
    CHECK(((rim >> 21) & 31) == 6 && abs((int)((rim >> 8) & 255) - 142) <= 1);
    const uint32_t edge = CoXlTestTrackAt(WORLD, 7000.0, 1, 134.0, 0.0, 4.0, ground, tally); /* 4 m off the circle: half */
    CHECK(((edge >> 21) & 31) == 6 && abs((int)((edge >> 8) & 255) - 142) <= 1);
}

#define SIDE 512
static uint32_t g_image[SIDE * SIDE];
static void Plants(void)
{
    g_case = "the plants' density falls to none within 2 m of the line, is whole from 3.6 m, and the ground ids stay";
    const double world = 2048.0, texel = world / (SIDE - 1);
    const uint32_t six = 6;
    g_seg[0] = -300.0f; g_seg[1] = -80.0f; g_seg[2] = 300.0f; g_seg[3] = 120.0f;
    CoXlTestTrackIndex(g_seg, &six, 1, world);
    CoXlTestTrackFlats(NULL, 0, world);
    for (uint32_t k = 0; k < SIDE * SIDE; k++) g_image[k] = 0xABCDE000u | 100u;
    uint32_t bare = 0;
    const uint32_t thinned = CoXlTestTrackThin(world, 900.0, g_image, SIDE, &bare);
    uint32_t wrong = 0, bareSeen = 0, changed = 0;
    for (uint32_t r = 0; r < SIDE; r++)
        for (uint32_t c = 0; c < SIDE; c++)
        {
            const double x = ((c + 0.5) / (SIDE - 1) - 0.5) * world, y = ((SIDE - 1 - r + 0.5) / (SIDE - 1) - 0.5) * world;
            const double d = Brute(x, y, 1);
            const uint32_t t = g_image[r * SIDE + c], density = t & 127u;
            wrong += (t & ~127u) != 0xABCDE000u;
            wrong += d <= 2.0 ? density != 0 : d >= 3.6 ? density != 100 : density >= 100;
            bareSeen += density == 0; changed += density != 100;
        }
    CHECK(wrong == 0 && thinned == changed && bare == bareSeen && bare > 100 && thinned > bare);
    CHECK(fabs((double)bare * texel * texel / (hypot(600.0, 200.0) * 4.0 + 12.6) - 1.0) < 0.15); /* a 4 m strip along the road */
}

/* Cases the mutants showed were not pinned. */
static void More(void)
{
    uint32_t state = 0, road[16]; double clear = 0.0, sum = 0.0;
    g_case = "a list longer than the reviewed 64 is refused even when every record reads";
    { static uint8_t desc[0x70], list[65 * 0x18]; const uint64_t at = (uint64_t)(uintptr_t)list; uint32_t n = 65;
      memcpy(desc + 0x60, &at, 8); memcpy(desc + 0x68, &n, 4);
      CHECK(CoXlTestTrackSlot((uintptr_t)desc, 0, &state) == 0 && (state & 1) == 0);
      n = 64; memcpy(desc + 0x68, &n, 4);
      CHECK(CoXlTestTrackSlot((uintptr_t)desc, 0, &state) == 0 && (state & 1) == 1); }
    g_case = "a material variable of type 1 holds its value at +8";
    { static Type one; const uint32_t type = 1; const float wrong = 3.0f, right = 12.0f;
      TypeAdd(&one, PATH, 0.0f);
      memcpy(one.vars[0] + 0x1C + 4, &wrong, 4); memcpy(one.vars[0] + 0x1C + 8, &right, 4); memcpy(one.vars[0] + 0x1C + 0x18, &type, 4);
      CHECK(CoXlTestTrackSlot(At(&one), 0, &state) == 12); }
    g_case = "the level's slot on a tie of clear length: the path slot along the most road";
    { static Type p1, p6, both; /* slots 1 and 6 are ground nowhere; 6 is a path slot along 30 m, 1 along 20 m */
      TypeAdd(&p1, PATH, 1); TypeAdd(&p6, PATH, 6); TypeAdd(&both, PATH, 1); TypeAdd(&both, PATH, 6);
      Nature(&p1); Nature(&p6); Nature(&both);
      const uintptr_t a[3] = {At(&p1), At(&p6), At(&both)}, b[3] = {0, 0, 0}; const double m[3] = {10, 20, 10};
      CHECK(CoXlTestTrackLevel(a, b, m, 3, road, &clear) == 6 && clear == 40.0);
      const double m2[3] = {20, 10, 10};
      CHECK(CoXlTestTrackLevel(a, b, m2, 3, road, &clear) == 1); }
    g_case = "the level's slot on ties: the path slot along the most road, then ground on neither side along the most, then the lower";
    { static Type A, B, D, p1, p6; /* B's ground is slot 1; D's ground is its own path slot 6 */
      TypeAdd(&A, FILL, 9); TypeAdd(&A, PATH, 1); TypeAdd(&B, FILL, 1); TypeAdd(&B, PATH, 6); TypeAdd(&D, FILL, 6); TypeAdd(&D, PATH, 6);
      TypeAdd(&p1, PATH, 1); TypeAdd(&p6, PATH, 6);
      Nature(&A); Nature(&B); Nature(&D); Nature(&p1); Nature(&p6);
      const uintptr_t a[3] = {At(&A), At(&p6), At(&D)}, b[3] = {0, 0, 0}; const double m[3] = {10, 10, 5};
      CHECK(CoXlTestTrackLevel(a, b, m, 3, road, &clear) == 6); /* 10 m each clear and a path slot; 6 a path slot along 15 m */
      const uintptr_t a2[2] = {At(&A), At(&B)}; const double m2[2] = {10, 10};
      CHECK(CoXlTestTrackLevel(a2, b, m2, 2, road, &clear) == 6 && clear == 20.0); /* 1 is ground beside B's road */
      const uintptr_t a3[2] = {At(&p6), At(&p1)};
      CHECK(CoXlTestTrackLevel(a3, b, m2, 2, road, &clear) == 1); }
    g_case = "a share over 1 is the whole texel, the weights still sum to 1";
    { const uint32_t id[2] = {9, 13}; const double w[2] = {0.7, 0.3};
      CHECK(CoXlTestTrackMix(id, w, 2, 14, 6, 2.5, 0, &sum) == Texel(6, 0, 0, 0, 0) && fabs(sum - 1.0) < 1e-12); }
    g_case = "the flat fade is eased, not straight";
    { static const float flat[6] = {100, 0, 30, 0, 0, 0};
      CHECK(CoXlTestTrackFlats(flat, 1, WORLD) == 1 && fabs(CoXlTestTrackFlatShare(132.0, 0.0) - 0.15625) < 1e-6 && fabs(CoXlTestTrackFlatShare(136.0, 0.0) - 0.84375) < 1e-6); }
    g_case = "beside a change of slot the nearer piece's slot wins unless the noise is at its end";
    { static float segs[4 * 40]; static uint32_t slots[40]; int near1 = 0, near6 = 0;
      for (int k = 0; k < 20; k++)
      {
          const float x = -4000.0f + 400.0f * k, y = 137.0f * k - 1200.0f;
          const float two[8] = {x - 100, y, x, y, x, y, x + 100, y};
          memcpy(segs + 8 * k, two, sizeof two); slots[2 * k] = 1; slots[2 * k + 1] = 6;
      }
      CHECK(CoXlTestTrackIndex(segs, slots, 40, WORLD) == 1);
      for (int k = 0; k < 20; k++)
      {
          double d = 0.0; uint32_t slot = 0;
          const double x = -4000.0 + 400.0 * k, y = 137.0 * k - 1200.0;
          near1 += CoXlTestTrackNearest(x - 2.5, y, REACH, &d, &slot, NULL) && slot == 1 && d == 0.0;
          near6 += CoXlTestTrackNearest(x + 2.5, y, REACH, &d, &slot, NULL) && slot == 6 && d == 0.0;
      }
      CHECK(near1 >= 16 && near6 >= 16); }
}

/* The level's slot comes from the biome's own types, never from a faction's place beside a road. */
static void Biomes(void)
{
    static Type conifer, colony, snowA, snowB, outpost, unnamed;
    uint32_t road[16], state = 0; double clear = 0.0;
    g_case = "Coniferous with a colony: the biome's slot 6, though the colony fills its ground with it";
    TypeAdd(&conifer, FILL, 3); TypeAdd(&conifer, PATH, 6); Nature(&conifer);
    TypeAdd(&colony, FILL, 20); TypeAdd(&colony, FILL, 6); TypeAdd(&colony, PATH, 1); TypeAdd(&colony, PATH, 1); TypeAdd(&colony, PATH, 6);
    CHECK(CoXlTestTrackSlot(At(&conifer), 0, &state) == 6 && (state & 4) == 4 && CoXlTestTrackSlot(At(&colony), 0, &state) == 1 && (state & 4) == 0);
    { const uintptr_t a[3] = {At(&conifer), At(&colony), At(&colony)}, b[3] = {0, At(&conifer), 0}; const double m[3] = {100, 10, 5};
      CHECK(CoXlTestTrackLevel(a, b, m, 3, road, &clear) == 6 && clear == 100.0 && road[0] == 6 && road[1] == 6 && road[2] == 1);
      const double most[3] = {1, 10, 500}; /* however much road lies in the colony */
      CHECK(CoXlTestTrackLevel(a, b, most, 3, road, &clear) == 6); }
    g_case = "Arctic with a bug outpost: a snow type's path slot, never the outpost's 23";
    TypeAdd(&snowA, FILL, 9); TypeAdd(&snowA, PATH, 13); Nature(&snowA);
    TypeAdd(&snowB, FILL, 1); TypeAdd(&snowB, PATH, 1); Nature(&snowB);
    TypeAdd(&outpost, FILL, 20); TypeAdd(&outpost, PATH, 23);
    { const uintptr_t a[4] = {At(&snowA), At(&snowB), At(&snowB), At(&snowA)}, b[4] = {At(&snowB), 0, At(&outpost), 0}; const double m[4] = {50, 60, 10, 30};
      CHECK(CoXlTestTrackLevel(a, b, m, 4, road, &clear) == 13 && clear == 150.0 && road[0] == 13 && road[2] == 13);
      const uintptr_t a2[2] = {At(&snowB), At(&outpost)}, b2[2] = {At(&outpost), 0}; const double m2[2] = {10, 90}; /* slot 1 is ground beside every road, still the biome's */
      CHECK(CoXlTestTrackLevel(a2, b2, m2, 2, road, &clear) == 1 && road[0] == 1 && road[1] == 1);
      const uintptr_t a3[1] = {At(&outpost)}, b3[1] = {0}; const double m3[1] = {40}; /* no type of the biome beside a road */
      CHECK(CoXlTestTrackLevel(a3, b3, m3, 1, road, &clear) == 0 && road[0] == 0); }
    g_case = "a type whose region input is an empty or unreadable string is no type of the biome";
    TypeAdd(&unnamed, PATH, 9);
    { static const char empty[] = ""; uint64_t at = (uint64_t)(uintptr_t)empty; memcpy(unnamed.desc + 0x100, &at, 8);
      CHECK(CoXlTestTrackSlot(At(&unnamed), 0, &state) == 9 && (state & 4) == 0);
      at = 0x10; memcpy(unnamed.desc + 0x100, &at, 8);
      CHECK(CoXlTestTrackSlot(At(&unnamed), 0, &state) == 9 && (state & 5) == 1); }
    g_case = "where three slots meet the nearest piece's slot or the next one's, never the third";
    { static const float segs[12] = {-50, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 50}; static const uint32_t slots[3] = {1, 6, 9}; int seen[32] = {0};
      CHECK(CoXlTestTrackIndex(segs, slots, 3, WORLD) == 1);
      for (int k = 0; k < 200; k++)
      {
          double d = 0.0; uint32_t slot = 0;
          const double x = -3.0 + 6.0 * Rand(), y = -1.0 - 2.0 * Rand(); /* below the node: pieces 1 and 6 are nearer than 9 */
          CHECK(CoXlTestTrackNearest(x, y, REACH, &d, &slot, NULL) && (slot == 1 || slot == 6 || slot == 9));
          seen[slot]++;
      }
      CHECK(seen[1] > 20 && seen[6] > 20 && seen[9] < 40); }
}

int main(void)
{
    Profile();
    Far();
    Slots();
    Mix();
    Index();
    Boundary();
    Places();
    Plants();
    More();
    Biomes();
    if (g_failed) { printf("roadtrack: %d of %d check(s) failed\n", g_failed, g_checked); return 1; }
    printf("roadtrack: all checks passed (%d)\n", g_checked);
    return 0;
}
