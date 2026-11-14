/* The patch outlines' breakup (proto/difficulty_world_patches.c): the warp and the texels against the model
 * (tests/patches_model.py); the same result from any tile; the texel format; pure ground untouched; the slots'
 * shares kept; more outline and no new straight edges; the cost. Over painted ground of the test's own and, when
 * CO_PATCH_DUMP names a dumped near-tile-materials.bin, over that tile too. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void CoXlTestPatchField(double x, double y, double strength, double* dx, double* dy);
int CoXlTestPatchMix(const uint32_t* own, double x0, double y0, double x, double y, double strength, uint32_t* id, double* w, int* broken);
uint32_t CoXlTestPatchTexel(const uint32_t* own, double x0, double y0, double x, double y, double strength, uint32_t hole);
double CoXlTestPatchStrength(void);
int CoXlTestPatchLevelGround(void);

static int g_failed, g_checked;
static const char* g_case = "";
#define CHECK(what) do { g_checked++; if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define TEXEL (2304.0 / 4096.0)
#define X0 (2.0 - 1152.0 + TEXEL / 2)
#define Y0 (2.0 + 1152.0 - TEXEL / 2)
#define SIDE 4096

static const struct { double x, y; uint64_t dx, dy; int r, c; uint32_t plain, whole, half; } kModel[48] = {
    {-7142.209, 877.143, 0xBFBB6606CCA3251Cull, 0x3FDAB39337C9CD3Eull, 2583, 2670, 0x25803700u, 0x25801700u, 0x25802600u},
    {4333.131, -46.05, 0x3FDFD26028DBC6C1ull, 0x3FE3D80AC81D9B80ull, 2392, 1568, 0x0D209400u, 0x0D209F00u, 0x0D209900u},
    {-1148.912, 3933.614, 0x3FD0D0A8EF213898ull, 0xBF7E540BF2AA8D18ull, 708, 2323, 0x0D2CCD14u, 0x0D2CD10Bu, 0x0D2CCF0Fu},
    {5447.58, 2515.6, 0x3FE0B3794DCD1636ull, 0x3FC1CB24C66C0554ull, 2812, 2152, 0x0D209800u, 0x0D208800u, 0x0D209100u},
    {-1686.393, -3749.923, 0x3FD5529E2945D3A8ull, 0xBFC3CF04C00A0BEEull, 1578, 2464, 0x24000000u, 0x24000000u, 0x24000000u},
    {-5741.787, 7170.885, 0xBFDEC48CA58591E8ull, 0xBFBEB7C457D5084Bull, 2915, 1464, 0x0D2CD71Cu, 0x0D2CCB25u, 0x0D2CD121u},
    {6306.404, 5651.472, 0x3F97D3AFFE121956ull, 0xBFE1E8A2362DB9DAull, 1391, 2216, 0x0D2C01DBu, 0x0D80E800u, 0x0D2C01E2u},
    {7733.939, -3423.341, 0x3FA414DF6500EED0ull, 0xBFE3F6FC13B9D618ull, 1111, 3222, 0x0D2C237Cu, 0x0D2C205Fu, 0x0D2C226Eu},
    {-5168.393, 1043.65, 0xBFB059C232A4114Eull, 0xBFE46DFAC69793C5ull, 2063, 1326, 0x24000000u, 0x24000000u, 0x24000000u},
    {-698.29, 6499.048, 0x3F8444E0AD01FF24ull, 0x3FD4163BB2E2CD94ull, 3199, 3179, 0x0D2C0EF0u, 0x0D2C0EF0u, 0x0D2C0EF0u},
    {-5880.471, -6977.745, 0x3FD4465DD8999936ull, 0x3FE8E1DBAD9A2B83ull, 1499, 1351, 0x0D80FA00u, 0x0D80FA00u, 0x0D80FA00u},
    {-6342.993, 433.322, 0xBF7C0C36E0862254ull, 0x3FD49C4873BA23FBull, 3219, 1968, 0x0D2C5D06u, 0x0D2C7703u, 0x0D2C6B05u},
    {2945.691, 5161.152, 0x3FC65B9E875CD5AAull, 0xBFD337A2813F5EA6ull, 2392, 1753, 0x25802900u, 0x25802600u, 0x25802700u},
    {-5305.354, -2033.532, 0x3FD04757ED5C8AE1ull, 0x3FE0DC1FD7F022C2ull, 1684, 1207, 0x0D807100u, 0x0D808F00u, 0x0D808000u},
    {-4493.088, -7195.428, 0x3FD3CA99BB488BB4ull, 0x3FD6F327D2257076ull, 2330, 2555, 0x24000000u, 0x24000000u, 0x24000000u},
    {83.546, -2348.238, 0xBFCD7569B1623EC3ull, 0xBFD90AA521DA77B4ull, 3074, 1216, 0x25802200u, 0x25803000u, 0x25802900u},
    {-334802.0, -293068.1, 0xBFDB8FC457028846ull, 0x3FC9D8BE070D28F1ull, 2083, 2756, 0x0D804600u, 0x0D804500u, 0x0D804500u},
    {914538.3, 997509.1, 0xBFC1F3570AE3A099ull, 0x3FE271E6A8BCDB74ull, 2620, 1232, 0x0D809100u, 0x0D809B00u, 0x0D809600u},
    {-356908.5, 601775.5, 0xBFCA67DDB0BEE64Dull, 0x3FC31944D510CDF2ull, 3443, 2698, 0x0D2CF407u, 0x0D2CF407u, 0x0D2CF407u},
    {270491.7, 495550.4, 0x3FDF973B1F470AA4ull, 0x3FE684C0163726A1ull, 2485, 1595, 0x0D2C2A3Fu, 0x0D2C2043u, 0x0D2C2541u},
    {532705.3, 36862.1, 0xBF84A970C8C3ACBCull, 0x3FD2DED8B67B158Eull, 1641, 1433, 0x0D200B00u, 0x0D200B00u, 0x0D200B00u},
    {215108.4, -10144.0, 0xBFC6237BE61C1B90ull, 0xBFD54FEEFF3CDFFAull, 3049, 3362, 0x0D2C802Cu, 0x0D2C8309u, 0x0D2C811Au},
    {882637.1, 325663.6, 0x3FD028CDDAA280BCull, 0x3F605048B36B5C90ull, 3465, 2172, 0x0D2C4265u, 0x0D2C5E62u, 0x0D2C5163u},
    {-348002.6, 774798.7, 0x3FE799F5292B6946ull, 0x3FE3B57F282558D6ull, 3372, 2339, 0x0D80FC00u, 0x0D80FC00u, 0x0D80FC00u},
    {-691287.3, 924296.0, 0x3FC2399A58A4B9A4ull, 0xBFE4195B66B13AF7ull, 1653, 2791, 0x0D80A700u, 0x0D80C500u, 0x0D80B400u},
    {631334.4, -257066.2, 0x3FD74C631CCC70C8ull, 0xBFDEED260E8717F2ull, 648, 1048, 0x24000000u, 0x24000000u, 0x24000000u},
    {-189503.7, 748366.5, 0xBFE3814F81CB0DAAull, 0x3FF12974B26F3E2Aull, 3294, 2397, 0x0D2CC70Eu, 0x0D2CCD06u, 0x0D2CCA0Au},
    {-134530.0, -750790.4, 0x3FDF943B6E6DABBAull, 0x3FD8550937CC39FAull, 1565, 2403, 0x0D803900u, 0x0D803D00u, 0x0D803B00u},
    {735456.4, 37489.9, 0x3FB09B5149AC87A8ull, 0xBFBDBEED80094294ull, 1291, 2680, 0x0D20C600u, 0x0D20CC00u, 0x0D20C900u},
    {227676.7, 612633.8, 0xBFDA043BBE0CB9DCull, 0xBFCEB6825A94BBF2ull, 1744, 1713, 0x0D203600u, 0x0D203900u, 0x0D203700u},
    {-100381.7, 843675.5, 0xBFD2914ECAFE43FAull, 0x3FE2922D80490329ull, 3226, 830, 0x2580ED00u, 0x2580EE00u, 0x2580EE00u},
    {-942819.3, -267950.3, 0xBFBEC510515B713Eull, 0x3FD3EDD89E6DB24Dull, 2417, 1924, 0x0D2CC03Du, 0x0D2CD22Au, 0x0D2CC934u},
    {34.1675, -35.3963, 0xBFE9AA84D42A91A5ull, 0xBFD6369493D80670ull, 1180, 1688, 0x24000000u, 0x24000000u, 0x24000000u},
    {-36.8522, 21.6581, 0xBFDC0CB44F5E5094ull, 0x3FD7AE32958F7E1Eull, 2596, 2454, 0x24000000u, 0x24000000u, 0x24000000u},
    {-15.5374, -1.506, 0x3FE53F90D1C35695ull, 0x3FCEDC84F4C6F487ull, 1798, 2877, 0x0D2CAD01u, 0x0D20B300u, 0x0D20B000u},
    {6.2183, -5.3592, 0xBFE56A50EBBB13E2ull, 0x3FD05C3CD635C09Bull, 2092, 1810, 0x0D2CBE20u, 0x0D2CB729u, 0x0D2CBA25u},
    {36.2267, 16.3243, 0xBFF63561570417A0ull, 0x3FD57E6CC30BDC77ull, 1840, 3326, 0x0D2C3A31u, 0x0D2C3549u, 0x0D2C373Eu},
    {4.542, 12.7982, 0xBFE03417AB88F784ull, 0x3FC7861F2E40BE12ull, 2922, 2693, 0x0D20FA00u, 0x0D20FA00u, 0x0D20FA00u},
    {-18.4263, -26.9139, 0x3FE43532E866B0B6ull, 0xBFF43234AC47BEFEull, 1317, 1322, 0x0D809400u, 0x0D80A500u, 0x0D809C00u},
    {-9.8406, 7.7078, 0x3FE1DFC9DC644816ull, 0x3FE367DD258C7C54ull, 1566, 1063, 0x25800100u, 0x25800100u, 0x25800100u},
    {23.0367, -37.5607, 0x3FCAE97C09F93FB6ull, 0x3FE301DFAE432D84ull, 680, 2966, 0x0D2C1EB1u, 0x0D2C298Cu, 0x0D2C259Du},
    {-19.9049, -15.9997, 0xBFE00575F2E14AEAull, 0x3FC2EAB5A6F8EEEEull, 1636, 1717, 0x0D2C01C1u, 0x0D80BB00u, 0x0D2C01BEu},
    {-5.3342, 24.6373, 0xBFE2F22F521460DCull, 0xBFD45558EFD73985ull, 755, 1292, 0x24000000u, 0x24000000u, 0x24000000u},
    {6.5151, -10.1569, 0xBFE952135BD1FF63ull, 0x3FE30C8EEA65840Eull, 2118, 2282, 0x0D2C9A63u, 0x0D2CD826u, 0x0D2CB945u},
    {-18.0298, -19.6955, 0x3FC167AD069DC1D2ull, 0x3FD19DB78A382034ull, 3162, 2956, 0x25800900u, 0x25800900u, 0x25800900u},
    {9.9558, 28.1521, 0x3FDAE9877F2C78D1ull, 0x3FDBA751AC2098D4ull, 1253, 1846, 0x24000000u, 0x24000000u, 0x24000000u},
    {-1.3891, 31.0332, 0xBFC4E79CCE0950FCull, 0xBFE6B29A9292349Dull, 2657, 3275, 0x0D20CA00u, 0x0D20CE00u, 0x0D20CC00u},
    {-2.6529, 5.744, 0xBFC0B8E0D54A74B0ull, 0xBFCFCB655CF3E864ull, 2480, 2335, 0x25803F00u, 0x25801C00u, 0x25802D00u},
};

static uint64_t Bits(double v) { uint64_t b; memcpy(&b, &v, 8); return b; }

/* The model's noise, for the test's ground. */
static double Hash(int64_t ix, int64_t iy, uint32_t seed)
{
    uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iy * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (double)((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.0;
}
static double Noise(double x, double y, double wl, uint32_t seed)
{
    double u = x / wl, v = y / wl, i = floor(u), j = floor(v), fu = u - i, fv = v - j;
    fu = fu * fu * (3 - 2 * fu); fv = fv * fv * (3 - 2 * fv);
    const int64_t a = (int64_t)i, b = (int64_t)j;
    return ((Hash(a, b, seed) * (1 - fu) + Hash(a + 1, b, seed) * fu) * (1 - fv) + (Hash(a, b + 1, seed) * (1 - fu) + Hash(a + 1, b + 1, seed) * fu) * fv) * 2 - 1;
}
static double Clamp(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* A tile's map as the replay leaves it (a 2 x 2 upsample of 2048 x 2048 weight texels): soft patches of slots 3, 9
 * and 12, a function of the world point, so two tiles paint the same ground. */
static uint32_t* Painted(double x0, double y0)
{
    uint32_t* own = (uint32_t*)malloc((size_t)SIDE * SIDE * 4);
    if (own == NULL) return NULL;
    for (int I = 0; I < SIDE / 2; I++)
        for (int J = 0; J < SIDE / 2; J++)
        {
            const double x = x0 + (2 * J + 0.5) * TEXEL, y = y0 - (2 * I + 0.5) * TEXEL;
            const double w9 = Clamp(0.5 + 1.2 * Noise(x, y, 40.0, 5)), w12 = (1 - w9) * Clamp(0.5 + 1.2 * Noise(x, y, 15.0, 6));
            uint32_t vb = (uint32_t)floor(w9 * 255 + 0.5), vc = (uint32_t)floor(w12 * 255 + 0.5);
            if (vb + vc > 255) vc = 255 - vb;
            const uint32_t slot[3] = {3, 9, 12}, share[3] = {255 - vb - vc, vb, vc};
            uint32_t ids[3] = {0, 0, 0}, vs[3] = {0, 0, 0}; int n = 0;
            for (int k = 0; k < 3; k++) if (share[k] > 0) { ids[n] = slot[k]; vs[n] = share[k]; n++; }
            const uint32_t t = ids[0] << 26 | ids[1] << 21 | ids[2] << 16 | vs[1] << 8 | vs[2];
            for (int k = 0; k < 4; k++) own[(size_t)(2 * I + (k >> 1)) * SIDE + (size_t)(2 * J + (k & 1))] = t;
        }
    return own;
}

/* The slot the terrain pixel shader draws from a texel with its height detail neutral: the layers in id order, the
 * highest on top, each cut at one half of its share of itself and what lies below. */
static uint32_t Drawn(uint32_t t)
{
    const uint32_t a = (t >> 26) & 31, b = (t >> 21) & 31, c = (t >> 16) & 31;
    const double wb = ((t >> 8) & 255) / 255.0, wc = (t & 255) / 255.0, wa = 1 - wb - wc;
    if (c != 0) return wc >= 0.5 ? c : wb >= 0.5 * (wa + wb) ? b : a;
    if (b != 0) return wb >= 0.5 ? b : a;
    return a;
}
static int FormatOk(uint32_t t)
{
    const uint32_t a = (t >> 26) & 31, b = (t >> 21) & 31, c = (t >> 16) & 31, vb = (t >> 8) & 255, vc = t & 255;
    return a != 0 && (b == 0 || a < b) && (c == 0 || (b != 0 && b < c)) && vb + vc <= 255 && (b != 0 || vb == 0) && (c != 0 || vc == 0);
}

typedef struct { double outline, area[32], share[32]; long runs, kept, keptChanged, broken, bad; } Measure;
/* n x n texels from (r0, c0): `plain` and `out` get the texels without and with the breakup at `strength`. */
static void Region(const uint32_t* own, double x0, double y0, int r0, int c0, int n, double strength, uint32_t* plain, uint32_t* out, Measure* before, Measure* after)
{
    memset(before, 0, sizeof *before); memset(after, 0, sizeof *after);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
        {
            const double x = x0 + TEXEL * (c0 + c), y = y0 - TEXEL * (r0 + r);
            const uint32_t hole = own[(size_t)(r0 + r) * SIDE + (size_t)(c0 + c)] & 0x80000000u;
            uint32_t id[16]; double w[16], top = 0, sum = 0; int broken = 0;
            int m = CoXlTestPatchMix(own, x0, y0, x, y, 0.0, id, w, &broken);
            for (int q = 0; q < m; q++) { before->share[id[q]] += w[q]; if (w[q] > top) top = w[q]; }
            const uint32_t p = CoXlTestPatchTexel(own, x0, y0, x, y, 0.0, hole), t = CoXlTestPatchTexel(own, x0, y0, x, y, strength, hole);
            m = CoXlTestPatchMix(own, x0, y0, x, y, strength, id, w, &broken);
            for (int q = 0; q < m; q++) { after->share[id[q]] += w[q]; sum += w[q]; if (!(w[q] >= 0.0)) after->bad++; }
            if (fabs(sum - 1.0) > 1e-9 && m > 0) after->bad++;
            if (!FormatOk(t) || (t & 0x80000000u) != hole) after->bad++;
            if (top >= 0.95) { after->kept++; if (t != p || broken) after->keptChanged++; }
            after->broken += broken;
            plain[(size_t)r * n + c] = p; out[(size_t)r * n + c] = t;
        }
    for (int pass = 0; pass < 2; pass++)
    {
        const uint32_t* img = pass ? out : plain; Measure* ms = pass ? after : before;
        long edges = 0;
        for (int k = 0; k < 32; k++) ms->share[k] /= (double)n * n;
        for (int r = 0; r < n; r++) for (int c = 0; c < n; c++) ms->area[Drawn(img[(size_t)r * n + c])] += 1.0 / ((double)n * n);
        for (int axis = 0; axis < 2; axis++) /* boundaries between line i and i + 1, walked along j */
            for (int i = 0; i + 1 < n; i++)
            {
                int run = 0; uint32_t pair = 0;
                for (int j = 0; j < n; j++)
                {
                    const uint32_t u = Drawn(img[axis ? (size_t)j * n + i : (size_t)i * n + j]), v = Drawn(img[axis ? (size_t)j * n + i + 1 : (size_t)(i + 1) * n + j]);
                    if (u == v) { run = 0; continue; }
                    edges++;
                    run = run > 0 && pair == (u << 8 | v) ? run + 1 : 1;
                    pair = u << 8 | v;
                    if (run == 8) ms->runs++; /* a straight edge of 4.5 m */
                }
            }
        ms->outline = (double)edges * TEXEL / ((double)n * n * TEXEL * TEXEL);
    }
}

static double Worst(const double* a, const double* b) { double m = 0; for (int k = 0; k < 32; k++) if (fabs(a[k] - b[k]) > m) m = fabs(a[k] - b[k]); return m; }

int main(void)
{
    g_case = "the warp and the texels are the model's, to the last bit";
    uint32_t* own = Painted(X0, Y0);
    if (own == NULL) { printf("patches: no memory\n"); return 1; }
    int differ = 0;
    for (int k = 0; k < 48; k++)
    {
        double dx, dy;
        CoXlTestPatchField(kModel[k].x, kModel[k].y, 1.0, &dx, &dy);
        CHECK(Bits(dx) == kModel[k].dx && Bits(dy) == kModel[k].dy);
        const double x = X0 + TEXEL * kModel[k].c, y = Y0 - TEXEL * kModel[k].r;
        CHECK(CoXlTestPatchTexel(own, X0, Y0, x, y, 0.0, 0) == kModel[k].plain);
        CHECK(CoXlTestPatchTexel(own, X0, Y0, x, y, 1.0, 0) == kModel[k].whole);
        CHECK(CoXlTestPatchTexel(own, X0, Y0, x, y, 0.5, 0) == kModel[k].half);
        differ += kModel[k].whole != kModel[k].plain;
    }
    CHECK(differ >= 16);
    { double dx, dy, ex, ey; CoXlTestPatchField(10.0, 20.0, 1.0, &dx, &dy); CoXlTestPatchField(10.0, 20.0, 2.0, &ex, &ey); CHECK(ex == 2 * dx && ey == 2 * dy); }
    for (int k = 0; k < 20000; k++) /* never past the reach its comment gives, on either axis */
    {
        double dx, dy;
        CoXlTestPatchField(-8000.0 + k * 0.731, 7000.0 - k * 0.577, 1.0, &dx, &dy);
        CHECK(fabs(dx) <= 1.5 && fabs(dy) <= 1.5);
    }

    const int n = 1024, r0 = 1536, c0 = 1536;
    uint32_t* plain = (uint32_t*)malloc((size_t)n * n * 4); uint32_t* out = (uint32_t*)malloc((size_t)n * n * 4); uint32_t* again = (uint32_t*)malloc((size_t)n * n * 4);
    Measure before, after, b2, a2;
    if (plain == NULL || out == NULL || again == NULL) { printf("patches: no memory\n"); return 1; }
    const clock_t t0 = clock();
    Region(own, X0, Y0, r0, c0, n, 1.0, plain, out, &before, &after);
    const double seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;

    g_case = "1: the same bytes twice, and from a tile 288 m away";
    Region(own, X0, Y0, r0, c0, n, 1.0, plain, again, &b2, &a2);
    CHECK(memcmp(out, again, (size_t)n * n * 4) == 0);
    {
        uint32_t* other = Painted(X0 + 288.0, Y0 - 288.0); /* 512 texels east and south: the region lies in both */
        int same = other != NULL;
        for (int r = 0; r < n && same; r += 3)
            for (int c = 0; c < n; c += 3)
            {
                const double x = X0 + TEXEL * (c0 + c), y = Y0 - TEXEL * (r0 + r);
                if (CoXlTestPatchTexel(other, X0 + 288.0, Y0 - 288.0, x, y, 1.0, 0) != out[(size_t)r * n + c]) { same = 0; break; }
            }
        CHECK(same);
        free(other);
    }

    g_case = "2: every texel is the game's format, the hole bit kept, the shares sum to 1";
    CHECK(after.bad == 0);
    own[(size_t)(r0 + 5) * SIDE + c0 + 5] |= 0x80000000u;
    CHECK(CoXlTestPatchTexel(own, X0, Y0, X0 + TEXEL * (c0 + 5), Y0 - TEXEL * (r0 + 5), 1.0, 0x80000000u) >> 31 == 1);
    own[(size_t)(r0 + 5) * SIDE + c0 + 5] &= 0x7FFFFFFFu;

    g_case = "3: ground that is 95 % one slot stays as it is; off changes nothing";
    CHECK(after.kept > 1000 && after.keptChanged == 0);
    CHECK(after.broken > n * n / 4 && after.broken + after.kept == (long)n * n);
    CHECK(memcmp(plain, out, (size_t)n * n * 4) != 0);
    Region(own, X0, Y0, r0, c0, 256, 0.0, plain, again, &b2, &a2);
    CHECK(memcmp(plain, again, (size_t)256 * 256 * 4) == 0 && a2.broken == 0);
    Region(own, X0, Y0, r0, c0, 256, -1.0, plain, again, &b2, &a2);
    CHECK(memcmp(plain, again, (size_t)256 * 256 * 4) == 0 && a2.broken == 0);

    g_case = "4: every slot's share stays within a point, drawn and mixed";
    CHECK(Worst(before.area, after.area) < 0.01 && Worst(before.share, after.share) < 0.01);

    g_case = "5: half as much outline again, and no more straight edges than before";
    CHECK(after.outline >= 1.5 * before.outline);
    CHECK(after.runs <= before.runs);
    Region(own, X0, Y0, r0, c0, 512, 0.5, plain, again, &b2, &a2);
    Measure b3, a3;
    Region(own, X0, Y0, r0, c0, 512, 1.0, plain, again, &b3, &a3);
    CHECK(a2.outline > b2.outline && a2.outline < a3.outline); /* the strength scales it */

    g_case = "6: a texel costs under a microsecond and a half on one core";
    CHECK(seconds / ((double)n * n) < 1.5e-6 * 4); /* Region runs the mix four times a texel */
    printf("patches: painted ground: outline %.4f -> %.4f m per m2 (x %.2f), straight edges %ld -> %ld, worst share change %.2f / %.2f points, %ld of %d texels mixed, %.0f ns a texel\n",
        before.outline, after.outline, after.outline / before.outline, before.runs, after.runs, 100 * Worst(before.area, after.area),
        100 * Worst(before.share, after.share), after.broken, n * n, seconds / ((double)n * n) / 4 * 1e9);

    g_case = "a players' build: the breakup off until a run confirms it, the ground the tile's own";
    CHECK(CoXlTestPatchStrength() == 0.0 && CoXlTestPatchLevelGround() == 0);

    const char* dump = getenv("CO_PATCH_DUMP");
    FILE* f = dump != NULL && dump[0] ? fopen(dump, "rb") : NULL;
    if (f == NULL) printf("patches: no dumped tile (CO_PATCH_DUMP): its cases are skipped\n");
    else
    {
        g_case = "a dumped tile: format, pure ground, shares, straight edges";
        CHECK(fread(own, 4, (size_t)SIDE * SIDE, f) == (size_t)SIDE * SIDE);
        fclose(f);
        Region(own, X0, Y0, r0, c0, n, 1.0, plain, out, &before, &after);
        CHECK(after.bad == 0 && after.keptChanged == 0 && after.kept > 0);
        CHECK(Worst(before.area, after.area) < 0.01 && Worst(before.share, after.share) < 0.01);
        CHECK(after.outline > 1.2 * before.outline && after.runs <= before.runs);
        printf("patches: dumped tile: outline %.4f -> %.4f m per m2 (x %.2f), straight edges %ld -> %ld, worst share change %.2f / %.2f points, %ld of %d texels mixed\n",
            before.outline, after.outline, after.outline / before.outline, before.runs, after.runs, 100 * Worst(before.area, after.area),
            100 * Worst(before.share, after.share), after.broken, n * n);
    }
    free(own); free(plain); free(out); free(again);
    if (g_failed) { printf("patches: %d of %d check(s) failed\n", g_failed, g_checked); return 1; }
    printf("patches: all checks passed (%d)\n", g_checked);
    return 0;
}
