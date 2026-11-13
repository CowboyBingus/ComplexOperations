/* A near tile's cones (proto/difficulty_world_terrain.c NearRecordEdit, NearPeaksCount, NearNodeHeight). The tile replays
 * the level's RenderZoneHeightmap (op 0x3D, game 0xF4A280) on a record copy whose cell count reads 0, so its zone fans
 * (1.5-3 m cones of a noise texture, about 1 m half width) are not drawn and it goes straight to its jobs. The count every
 * tile logs finds such cones and tells them from wider stamp mounds. The conform's grid nodes take the tile's height as
 * the mean under their interpolation's footprint, not one texel. Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestNearRecordEdit(uint8_t op, uint8_t* one);
uintptr_t CoXlTestNearNoCells(void);
void CoXlTestNearPeaks(const float* heights, float cx, float cy, int* narrow, int* wide, float* top, float* x, float* y);
float CoXlTestNearNodeHeight(const float* heights, float cx, float cy, int i, int j);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define CELLS_READ 0xF4A395u /* mov rax, [r14 + 0xF8]: the cell count's address */
#define CELLS_TEST 0xF4A3B1u /* mov ecx, [rax] ... test ecx, ecx / je 0xF4AAB3 */
static const uint8_t kRead[7] = {0x49, 0x8B, 0x86, 0xF8, 0x00, 0x00, 0x00};
static const uint8_t kTest[14] = {0x8B, 0x08, 0xF3, 0x0F, 0x5E, 0xF8, 0x85, 0xC9, 0x0F, 0x84, 0xF4, 0x06, 0x00, 0x00};

static void Start(const char* name, int code)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    CoXlTestGame((uintptr_t)g_fake.image);
    if (code) { memcpy(g_fake.image + CELLS_READ, kRead, sizeof kRead); memcpy(g_fake.image + CELLS_TEST, kTest, sizeof kTest); }
}

/* A record as the level holds it: every byte its own, the cell count's address at +0x100. */
static uint8_t g_one[0x19B0], g_was[0x19B0];
static void Record(void)
{
    for (size_t i = 0; i < sizeof g_one; i++) g_one[i] = (uint8_t)(i * 7 + 3);
    memcpy(g_was, g_one, sizeof g_one);
}
static int SameOutside(size_t at, size_t n) { return memcmp(g_one, g_was, at) == 0 && memcmp(g_one + at + n, g_was + at + n, sizeof g_one - at - n) == 0; }

/* A tile's heights, normalized as the texture ((m + 500) / 1000), and features on it. */
#define N 4096
#define T 0.5625f
static float* g_h;
static void Flat(float m) { for (size_t i = 0; i < (size_t)N * N; i++) g_h[i] = (m + 500.0f) / 1000.0f; }
static void Add(int r, int c, int radius, float (*shape)(float d, int radius), float height)
{
    for (int j = -radius - 1; j <= radius + 1; j++)
        for (int i = -radius - 1; i <= radius + 1; i++)
            g_h[(size_t)(r + j) * N + c + i] += height * shape(sqrtf((float)(i * i + j * j)), radius) / 1000.0f;
}
static float Cone(float d, int radius) { return d >= (float)radius ? 0.0f : 1.0f - d / (float)radius; }
static float Mesa(float d, int radius) { return d == 0.0f ? 1.004f : d <= (float)radius - 0.5f ? 1.0f : 0.0f; }

static int g_narrow, g_wide;
static float g_top, g_x, g_y;
static void Count(void) { CoXlTestNearPeaks(g_h, 100.0f, -200.0f, &g_narrow, &g_wide, &g_top, &g_x, &g_y); }
static float Node(int i, int j) { return CoXlTestNearNodeHeight(g_h, 100.0f, -200.0f, i, j); }
static void Pit(int r, int c) { g_h[(size_t)r * N + c] -= 3.0f / 1000.0f; }
static float X(int c) { return 100.0f - 1152.0f + T / 2 + T * (float)c; }
static float Y(int r) { return -200.0f + 1152.0f - T / 2 - T * (float)r; }

int main(void)
{
    Start("0x3D's copy reads no cells: its cell count's address is a 0", 1);
    Record();
    CoXlTestNearRecordEdit(0x3D, g_one);
    uint64_t at;
    memcpy(&at, g_one + 0x100, 8);
    CHECK(at == CoXlTestNearNoCells() && at != 0 && *(const uint32_t*)(uintptr_t)at == 0);
    CHECK(SameOutside(0x100, 8));

    Start("a game whose zone pass is not the reviewed one: the record as it is", 0);
    Record();
    CoXlTestNearRecordEdit(0x3D, g_one);
    CHECK(memcmp(g_one, g_was, sizeof g_one) == 0);
    Start("only the read in place: the record as it is", 0);
    memcpy(g_fake.image + CELLS_READ, kRead, sizeof kRead);
    Record();
    CoXlTestNearRecordEdit(0x3D, g_one);
    CHECK(memcmp(g_one, g_was, sizeof g_one) == 0);
    Start("only the test in place: the record as it is", 0);
    memcpy(g_fake.image + CELLS_TEST, kTest, sizeof kTest);
    Record();
    CoXlTestNearRecordEdit(0x3D, g_one);
    CHECK(memcmp(g_one, g_was, sizeof g_one) == 0);

    Start("the other steps' copies as before", 1);
    Record(); CoXlTestNearRecordEdit(0x3C, g_one); CHECK(SameOutside(0x110, 8)); for (int k = 0; k < 8; k++) CHECK(g_one[0x110 + k] == 0);
    Record(); CoXlTestNearRecordEdit(0x94, g_one); CHECK(SameOutside(0xB0, 4)); for (int k = 0; k < 4; k++) CHECK(g_one[0xB0 + k] == 0);
    Record(); CoXlTestNearRecordEdit(0x3E, g_one); CHECK(SameOutside(0x80, 0x40)); for (int k = 0; k < 0x40; k++) CHECK(g_one[0x80 + k] == 0);
    Record(); CoXlTestNearRecordEdit(0x3F, g_one); CHECK(SameOutside(0x128, 0x40)); for (int k = 0; k < 0x40; k++) CHECK(g_one[0x128 + k] == 0);
    for (int op = 0; op < 256; op++)
    {
        if (op == 0x3C || op == 0x94 || op == 0x3E || op == 0x3F || op == 0x3D) continue;
        Record(); CoXlTestNearRecordEdit((uint8_t)op, g_one);
        CHECK(memcmp(g_one, g_was, sizeof g_one) == 0);
    }

    g_h = (float*)malloc((size_t)N * N * sizeof(float));
    if (g_h == NULL) { printf("nearcones: no memory\n"); return 1; }

    Start("flat ground and gentle slopes: no cones", 1);
    Flat(30.0f); Count(); CHECK(g_narrow == 0 && g_wide == 0);
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) g_h[(size_t)r * N + c] = (30.0f + 0.2f * (float)c + 0.1f * (float)r + 500.0f) / 1000.0f;
    Count(); CHECK(g_narrow == 0 && g_wide == 0);

    Start("a zone fan's cone: 3 m over 3 texels, found where it stands", 1);
    Flat(30.0f); Add(1000, 2000, 3, Cone, 3.0f); Count();
    CHECK(g_narrow == 1 && g_wide == 0);
    CHECK(fabsf(g_top - 3.0f) < 0.01f);
    CHECK(fabsf(g_x - X(2000)) < 0.01f && fabsf(g_y - Y(1000)) < 0.01f);

    Start("at the rise's edge: 1.6 m is a cone, 1.4 m is not", 1);
    Flat(0.0f); Add(500, 500, 3, Cone, 1.6f); Add(1500, 1500, 3, Cone, 1.4f); Count();
    CHECK(g_narrow == 1 && fabsf(g_x - X(500)) < 0.01f && fabsf(g_y - Y(500)) < 0.01f);

    Start("the ring is 5 texels out on all 8 sides: a pit under one of them raises the rise", 1);
    Flat(0.0f); Add(800, 800, 3, Cone, 1.4f);
    static const int side[8][2] = {{5, 0}, {-5, 0}, {0, 5}, {0, -5}, {5, 5}, {5, -5}, {-5, 5}, {-5, -5}};
    for (int k = 0; k < 8; k++)
    {
        Flat(0.0f); Add(800, 800, 3, Cone, 1.4f);
        g_h[(size_t)(800 + side[k][0]) * N + 800 + side[k][1]] -= 1.0f / 1000.0f; /* the mean 0.125 m lower */
        Count(); CHECK(g_narrow == 1);
        g_h[(size_t)(800 + side[k][0]) * N + 800 + side[k][1]] += 1.0f / 1000.0f;
        g_h[(size_t)(800 + side[k][0] * 2) * N + 800 + side[k][1] * 2] -= 1.0f / 1000.0f; /* 10 texels out: not the ring */
        Count(); CHECK(g_narrow == 0);
    }

    Start("a mound wider than 2 m at half its rise: counted apart", 1);
    Flat(10.0f); Add(2000, 1000, 5, Mesa, 3.0f); Count();
    CHECK(g_narrow == 0 && g_wide == 1 && g_top == 0.0f);

    Start("two cones within one square: only the higher one; the tallest of several is reported", 1);
    Flat(0.0f); Add(1200, 1200, 2, Cone, 2.0f); Add(1200, 1204, 2, Cone, 2.5f); Count();
    CHECK(g_narrow == 1 && fabsf(g_x - X(1204)) < 0.01f);
    Flat(0.0f); Add(300, 300, 3, Cone, 2.0f); Add(3000, 3500, 3, Cone, 2.8f); Add(2000, 100, 3, Cone, 2.2f); Count();
    CHECK(g_narrow == 3 && fabsf(g_top - 2.8f) < 0.01f && fabsf(g_x - X(3500)) < 0.01f && fabsf(g_y - Y(3000)) < 0.01f);
    Flat(0.0f); Add(1500, 100, 3, Cone, 2.8f); Add(1500, 3000, 3, Cone, 2.2f); Count(); /* one row: one thread's scan */
    CHECK(g_narrow == 2 && fabsf(g_top - 2.8f) < 0.01f && fabsf(g_x - X(100)) < 0.01f);

    Start("a flat-topped cone (two equal texels): counted once", 1);
    Flat(0.0f); Add(2500, 2500, 3, Cone, 2.5f); Add(2500, 2501, 3, Cone, 2.5f);
    g_h[(size_t)2500 * N + 2501] = g_h[(size_t)2500 * N + 2500];
    Count(); CHECK(g_narrow == 1);

    Start("cones near the tile's edge and the field's last rows", 1);
    Flat(0.0f); Add(6, 2000, 1, Cone, 2.0f); Add(4089, 2000, 1, Cone, 2.0f); Add(2000, 6, 1, Cone, 2.0f); Add(2000, 4089, 1, Cone, 2.0f); Count();
    CHECK(g_narrow == 4);

    /* The conform's nodes: grid node k at texel 4k (k = 1..1023), cells split from (i, j) to (i + 1, j + 1); the hat's
     * weights over a node's 9 x 9 texels sum to 16. Node 57 is on the hidden square's edge (the per-texel strip). */
    Start("a node's height: flat ground and slopes as they are", 1);
    Flat(30.0f);
    CHECK(fabsf(Node(512, 512) - 30.0f) < 1e-3f && fabsf(Node(0, 0) - 30.0f) < 1e-3f && fabsf(Node(1024, 1024) - 30.0f) < 1e-3f && fabsf(Node(0, 1024) - 30.0f) < 1e-3f);
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) g_h[(size_t)r * N + c] = (0.2f * (float)c - 0.1f * (float)r + 500.0f) / 1000.0f;
    CHECK(fabsf(Node(512, 512) - (0.2f * 2048 - 0.1f * 2048)) < 2e-3f && fabsf(Node(300, 700) - (0.2f * 1200 - 0.1f * 2800)) < 2e-3f);

    Start("a node on a crater's floor texel takes a sixteenth of it, not all of it", 1);
    Flat(0.0f); Pit(2048, 2048);
    CHECK(fabsf(Node(512, 512) + 3.0f / 16) < 1e-4f);
    CHECK(fabsf(Node(513, 512)) < 1e-6f && fabsf(Node(512, 511)) < 1e-6f);
    Flat(0.0f); Pit(600 * 4, 100 * 4); /* in the band: 98 m inside the hidden square's edge */
    CHECK(fabsf(Node(100, 600) + 3.0f / 16) < 1e-4f);

    Start("the footprint is the draw's triangles: (i, j) to (i + 1, j + 1)", 1);
    static const int along[4][2] = {{2, 2}, {-2, -2}, {2, -2}, {-2, 2}};
    static const float share[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    for (int k = 0; k < 4; k++) { Flat(0.0f); Pit(2048 + along[k][1], 2048 + along[k][0]); CHECK(fabsf(Node(512, 512) + 3.0f * share[k] / 16) < 1e-4f); }
    Flat(0.0f); Pit(2048, 2051); CHECK(fabsf(Node(512, 512) + 3.0f * 0.25f / 16) < 1e-4f);
    Flat(0.0f); Pit(2048, 2052); CHECK(fabsf(Node(512, 512)) < 1e-6f);
    Flat(0.0f); Pit(2045, 2049); CHECK(fabsf(Node(512, 512) + 3.0f * 0.0f / 16) < 1e-4f); /* u = 1/4, v = -3/4 */
    Flat(0.0f); Pit(2047, 2046); CHECK(fabsf(Node(512, 512) + 3.0f * 0.5f / 16) < 1e-4f); /* u = -1/2, v = -1/4 */

    Start("the tile's first and last nodes: their texels only", 1);
    Flat(0.0f); Pit(0, 0); CHECK(fabsf(Node(0, 0) + 0.4f) < 1e-4f); /* texels 0..3, from -0.5 to 4: 8/9 of 20/3 */
    Flat(0.0f); Pit(4095, 4095); CHECK(fabsf(Node(1024, 1024) + 3.0f * (6.0f / 7) / 4) < 1e-4f); /* 4092..4095, from 4095.5 to 4092 */
    Flat(0.0f); Pit(0, 4); CHECK(Node(0, 0) == 0.0f);
    Flat(0.0f); Pit(2048, 0); CHECK(fabsf(Node(1, 512) + 3.0f / 154) < 1e-5f); /* from -0.5 to 4: 1/9 of 154/9 */

    Start("a node of the per-texel strip keeps its texel", 1);
    Flat(0.0f); Pit(2048, 228);
    CHECK(fabsf(Node(57, 512) + 3.0f) < 1e-4f);
    Flat(0.0f); Pit(2048, 229);
    CHECK(Node(57, 512) == 0.0f);
    Flat(0.0f); Pit(2048, 244); CHECK(fabsf(Node(61, 512) + 3.0f) < 1e-4f); /* the strip's last cells: 53-60 */
    Flat(0.0f); Pit(244, 2048); CHECK(fabsf(Node(512, 61) + 3.0f) < 1e-4f);
    Flat(0.0f); Pit(2048, 248); CHECK(fabsf(Node(62, 512) + 3.0f / 16) < 1e-4f);

    free(g_h);
    if (g_failed) { printf("nearcones: %d check(s) failed\n", g_failed); return 1; }
    printf("nearcones: all checks passed\n");
    return 0;
}
