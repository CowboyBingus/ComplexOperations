/* The rotation's ray reader (game 0x620810) on extra large levels: a ray that starts under a live tile's ground reads
 * that ground at its start (just under 0, whatever its depth) when it missed; hits, misses over the ground, other places and a busy tile
 * list keep the game's answer. The game's call is always made as the game made it.
 * The diagnostic beside it sorts units 30 deg or more off the drawn ground by what their own corner rays read.
 * Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestTiltInstall(uintptr_t game);
void CoXlTestTiltTile(float cx, float cy, float z);
float CoXlTestTiltUnder(float d, uint32_t count, float originZ, float ground);
void CoXlTestTiltHold(int on);
void CoXlTestTiltCounts(long* reads, long* under);
int CoXlTestTiltClass(const float* corners, float len, float* residual);
enum { FITS, MISSED, UNDER, LOOSE }; /* the diagnostic's classes, in its order */

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define READ 0x620810u

typedef float (*ReadFn)(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse);

/* The game's reader as the fake: what it was handed, and the distance it answers. */
static struct { int calls; uintptr_t mgr, rays; uint32_t i, request, coarse; float answer; } g_read;
static float FakeRead(uintptr_t mgr, uint32_t i, uintptr_t rays, uint32_t request, uint32_t coarse)
{
    g_read.calls++; g_read.mgr = mgr; g_read.i = i; g_read.rays = rays; g_read.request = request; g_read.coarse = coarse;
    return g_read.answer;
}

static void Hex(uint8_t* out, const char* hex)
{
    unsigned v;
    while (sscanf(hex, " %2x", &v) == 1) { *out++ = (uint8_t)v; hex += hex[0] == ' ' ? 3 : 2; }
}

static void Pins(uint8_t* image)
{
    Hex(image + READ, "40 53 55 56 57 48 83 EC 78 0F 29 74 24 60");
    Hex(image + 0x620835u, "41 8D 71 FF 48 C1 E6 06 49 8B D8");
    Hex(image + 0x6208D4u, "89 44 1E 30");
    Hex(image + 0x6208ECu, "F3 0F 10 45 48");
    Hex(image + 0x620933u, "83 7C 1E 30 00 76 05 F3 0F 10 47 18");
    Hex(image + 0x6215DDu, "E8 2E F2 FF FF");
    Hex(image + 0x6227A8u, "E8 63 E0 FF FF");
}

/* Four ray records of 0x40 B; request r reads record r - 1: its start at +0x10, its hit count at +0x30. */
static uint8_t g_rays[4][0x40];
static void Ray(int request, float x, float y, float z, uint32_t count)
{
    float o[3] = {x, y, z};
    memcpy(g_rays[request - 1] + 0x10, o, sizeof o);
    memcpy(g_rays[request - 1] + 0x30, &count, 4);
}

static float Call(ReadFn read, uint32_t request, uint32_t coarse, float answer)
{
    memset(&g_read, 0, sizeof g_read);
    g_read.answer = answer;
    return read(0x1234, 7, (uintptr_t)g_rays, request, coarse);
}

/* The game's call, exactly as made, and its answer returned. */
static int Passed(float got, float answer, uint32_t request, uint32_t coarse)
{
    return got == answer && g_read.calls == 1 && g_read.mgr == 0x1234 && g_read.i == 7 && g_read.rays == (uintptr_t)g_rays &&
        g_read.request == request && g_read.coarse == coarse;
}

/* A four-corner unit's class and residual from its corners: 4 x (offset x, offset y, distance read). */
static int Class(const float corners[4][3], float len, float* residual) { return CoXlTestTiltClass(&corners[0][0], len, residual); }
static int Near(float a, float b, float within) { return fabsf(a - b) <= within; }

/* The diagnostic's sorting of units 30 deg or more off the drawn ground. The numbers are logged units of one run:
 * unit ids and times in the comments are its lines. */
static void Classes(void)
{
    float r;
    g_case = "settled on a rock: the same distance at every corner";
    const float settled[4][3] = {{0.5f, 0.7f, 0.61f}, {0.5f, -0.7f, 0.68f}, {-0.5f, -0.7f, 0.61f}, {-0.5f, 0.7f, 0.68f}}; /* 64D, 00:45:09 */
    CHECK(Class(settled, 4.0f, &r) == FITS && Near(r, 0.0f, 0.05f));
    const float nearly[4][3] = {{0.5f, 0.7f, 1.24f}, {0.5f, -0.7f, 1.24f}, {-0.5f, -0.7f, 0.74f}, {-0.5f, 0.7f, 1.67f}}; /* 649, 00:44:21 */
    CHECK(Class(nearly, 4.0f, &r) == FITS && Near(r, 18.5f, 0.2f));

    g_case = "a long unit: the corners are not centred on it";
    const float level[4][3] = {{0.5f, 0.7f, 1.5f}, {0.5f, -3.0f, 1.5f}, {-0.5f, -3.0f, 1.5f}, {-0.5f, 0.7f, 1.5f}};
    CHECK(Class(level, 4.0f, &r) == FITS && Near(r, 0.0f, 0.01f));
    const float pitched[4][3] = {{0.5f, 0.7f, 0.2f * 0.7f}, {0.5f, -3.0f, 0.2f * -3.0f}, {-0.5f, -3.0f, 0.2f * -3.0f}, {-0.5f, 0.7f, 0.2f * 0.7f}};
    CHECK(Class(pitched, 4.0f, &r) == FITS && Near(r, 11.31f, 0.05f)); /* atan 0.2: it fits, whatever the signs read */
    const float rolled[4][3] = {{0.5f, 0.7f, 1.0f + 0.5f}, {0.5f, -3.0f, 1.0f + 0.5f}, {-0.5f, -3.0f, 1.0f - 0.5f}, {-0.5f, 0.7f, 1.0f - 0.5f}};
    CHECK(Class(rolled, 4.0f, &r) == LOOSE && Near(r, 45.0f, 0.05f)); /* 1 m across 1 m */

    g_case = "corners that are no rectangle: the plane d = 0.3 x + 0.2 y";
    const float skew[4][3] = {{1.0f, 0.0f, 0.3f}, {0.0f, 1.0f, 0.2f}, {-1.0f, 0.5f, -0.2f}, {0.0f, -1.0f, -0.2f}};
    CHECK(Class(skew, 4.0f, &r) == FITS && Near(r, 19.83f, 0.05f));
    const float steeper[4][3] = {{1.0f, 0.0f, 1.3f}, {0.0f, 1.0f, 1.25f}, {-1.0f, 0.5f, 0.825f}, {0.0f, -1.0f, 0.75f}}; /* 1 + 0.3 x + 0.25 y */
    CHECK(Class(steeper, 4.0f, &r) == LOOSE && Near(r, 21.33f, 0.05f));

    g_case = "a corner that missed reads the full length";
    const float missed[4][3] = {{0.5f, 0.7f, 0.47f}, {0.5f, -0.7f, 0.62f}, {-0.5f, -0.7f, 0.61f}, {-0.5f, 0.7f, 4.0f}}; /* 64E, 00:45:42 */
    CHECK(Class(missed, 4.0f, &r) == MISSED && Near(r, 64.6f, 0.2f));
    CHECK(Class(missed, 3.0f, &r) == LOOSE); /* the same readings on a 3 m ray are a far hit */
    const float both[4][3] = {{0.5f, 0.7f, 4.0f}, {0.5f, -0.7f, -1.0f}, {-0.5f, -0.7f, 0.6f}, {-0.5f, 0.7f, 0.6f}};
    CHECK(Class(both, 4.0f, &r) == MISSED); /* the miss is named first */
    const float holes[4][3] = {{0.5f, 0.7f, 4.0f}, {0.5f, -0.7f, 4.0f}, {-0.5f, -0.7f, 4.0f}, {-0.5f, 0.7f, 4.0f}};
    CHECK(Class(holes, 4.0f, &r) == FITS); /* every corner over a hole: no plane to lean to */

    g_case = "a corner whose ray started under a surface reads below 0";
    const float inside[4][3] = {{0.5f, 0.7f, 0.71f}, {0.5f, -3.0f, -1.69f}, {-0.5f, -3.0f, -1.18f}, {-0.5f, 0.7f, 1.09f}}; /* 630, 00:44:04 */
    CHECK(Class(inside, 4.0f, &r) == UNDER && Near(r, 37.7f, 0.2f));
    const float edge[4][3] = {{0.5f, 0.7f, 0.39f}, {0.5f, -3.0f, 0.0f}, {-0.5f, -3.0f, 0.38f}, {-0.5f, 0.7f, -0.73f}}; /* 668, 00:46:12 */
    CHECK(Class(edge, 4.0f, &r) == UNDER && Near(r, 20.9f, 0.2f)); /* just past the limit */

    g_case = "neither a miss nor a start under a surface";
    const float loose[4][3] = {{0.5f, 0.7f, 0.24f}, {0.5f, -0.7f, 1.20f}, {-0.5f, -0.7f, 0.29f}, {-0.5f, 0.7f, 3.15f}}; /* 64D, 00:44:24 */
    CHECK(Class(loose, 4.0f, &r) == LOOSE && Near(r, 50.4f, 0.2f));
    const float touching[4][3] = {{0.5f, 0.7f, 0.0f}, {0.5f, -0.7f, 0.0f}, {-0.5f, -0.7f, 1.0f}, {-0.5f, 0.7f, 1.0f}};
    CHECK(Class(touching, 4.0f, &r) == LOOSE && Near(r, 45.0f, 0.05f)); /* a reading of 0 is not under a surface */

    g_case = "corners that span no area";
    const float point[4][3] = {{0.5f, 0.7f, 0.2f}, {0.5f, 0.7f, 0.9f}, {0.5f, 0.7f, 0.4f}, {0.5f, 0.7f, 0.1f}};
    CHECK(Class(point, 4.0f, &r) == LOOSE && r == -1.0f);
}

static float MeanV(float a, float b, float c, float d, float len) { return 0.25f * ((1.0f - a / len) + (1.0f - b / len) + (1.0f - c / len) + (1.0f - d / len)); }

int main(void)
{
    Classes();
    g_case = "the rule";
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 10.3f) == -0.01f);        /* a miss under the ground: the ground at its start */
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 25.0f) == -0.01f);        /* however deep */
    CHECK(CoXlTestTiltUnder(0.2f, 1, 10.0f, 10.3f) == 0.2f);          /* a hit stays the game's */
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 9.5f) == 4.0f);           /* a miss over the ground stays a miss */
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 10.0f) == 4.0f);          /* at the ground: the game's */

    /* The four-corner mode's mean of 1 - h / length over the corners (game 0x6227BC, 0x622C46): over 1 the unit is put
     * world up (0x622CBB). */
    g_case = "a long unit level on a 34 deg slope: its two buried rear corners do not put it world up";
    const float front[2] = {0.71f, 1.09f}; /* unit 630 of run 4: start z 39.10, the ground at its rear corners 40.70 and 40.19 */
    float rear[2] = {CoXlTestTiltUnder(4.0f, 0, 39.10f, 40.70f), CoXlTestTiltUnder(4.0f, 0, 39.10f, 40.19f)};
    CHECK(MeanV(front[0], rear[0], rear[1], front[1], 4.0f) < 1.0f);
    CHECK(MeanV(front[0], 39.10f - 40.70f, 39.10f - 40.19f, front[1], 4.0f) > 1.0f); /* its depths did */
    g_case = "every corner buried: world up, as the game has a unit with no ground";
    float deep = CoXlTestTiltUnder(4.0f, 0, 10.0f, 13.0f);
    CHECK(MeanV(deep, deep, deep, deep, 4.0f) > 1.0f);
    CHECK(MeanV(deep, deep, deep, deep, 2.0f) > 1.0f);

    g_case = "not the reviewed code";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    Pins(g_fake.image);
    g_fake.image[0x620933u + 4] = 0x01; /* the count test is not the one read */
    CHECK(!CoXlTestTiltInstall((uintptr_t)g_fake.image));
    CHECK(fake_host_detour(READ) == NULL);
    CHECK(fake_host_logged("the rotation's ray reader is not the reviewed code"));

    g_case = "hooked";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    Pins(g_fake.image);
    g_fake.originals[0].target = (uintptr_t)g_fake.image + READ;
    g_fake.originals[0].original = (void*)FakeRead;
    CHECK(CoXlTestTiltInstall((uintptr_t)g_fake.image));
    ReadFn read = (ReadFn)fake_host_detour(READ);
    CHECK(read != NULL);
    if (read == NULL) return 1;
    long reads, under;

    g_case = "no moving tiles: the game's call";
    CoXlTestTiltTile(0, 0, NAN);
    Ray(2, 5.0f, 5.0f, -20.0f, 0);
    CHECK(Passed(Call(read, 2, 3, 4.0f), 4.0f, 2, 3));

    g_case = "on a tile, a miss that started under its ground";
    CoXlTestTiltTile(100.0f, -200.0f, 12.0f);
    Ray(1, 0, 0, 40.0f, 0); /* another record: over the ground */
    Ray(2, 110.0f, -190.0f, 11.7f, 0);
    float d = Call(read, 2, 3, 4.0f);
    CHECK(d == -0.01f);
    CHECK(g_read.calls == 1 && g_read.request == 2 && g_read.coarse == 3); /* the game's call as made */
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 1 && under == 1);

    g_case = "on a tile, a hit that started under its ground: the game's";
    Ray(2, 110.0f, -190.0f, 9.0f, 1);
    CHECK(Passed(Call(read, 2, 3, 0.2f), 0.2f, 2, 3));
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 0 && under == 0);

    g_case = "on a tile, a miss over its ground: the game's";
    Ray(1, 0, 0, -400.0f, 0); /* another record: under the ground */
    Ray(2, 110.0f, -190.0f, 12.6f, 0);
    CHECK(Passed(Call(read, 2, 3, 4.0f), 4.0f, 2, 3));
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 1 && under == 0);

    g_case = "a writer holds the tiles: the game's";
    Ray(2, 110.0f, -190.0f, 11.0f, 0);
    CoXlTestTiltHold(1);
    CHECK(Passed(Call(read, 2, 3, 4.0f), 4.0f, 2, 3));
    CoXlTestTiltHold(0);
    CHECK(Call(read, 2, 3, 4.0f) == -0.01f);
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 1 && under == 1);

    g_case = "outside every tile's square: the game's";
    Ray(2, 100.0f + 1030.0f, -200.0f, -50.0f, 0); /* just past the hidden square's edge (1024.2 m) */
    CHECK(Passed(Call(read, 2, 3, 4.0f), 4.0f, 2, 3));
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 0 && under == 0);

    g_case = "no request: the game's call";
    CHECK(Passed(Call(read, 0, 3, 4.0f), 4.0f, 0, 3));

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tilt: all checks passed\n");
    return 0;
}
