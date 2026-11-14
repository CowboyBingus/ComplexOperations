/* The rotation's ray reader (game 0x620810) on extra large levels: a ray that starts under a live tile's ground reads
 * that ground at its start (just under 0, whatever its depth) when it missed; hits, misses over the ground, other places and a busy tile
 * list keep the game's answer. The game's call is always made as the game made it.
 * The diagnostic beside it sorts units 30 deg or more off the drawn ground by what their own corner rays read.
 * Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestTiltInstall(uintptr_t game);
void CoXlTestTiltTile(float cx, float cy, float z);
float CoXlTestTiltUnder(float d, uint32_t count, float originZ, float ground);
void CoXlTestTiltHold(int on);
void CoXlTestTiltCounts(long* reads, long* under);
int CoXlTestTiltClass(const float* corners, float len, float* residual);
uint32_t CoXlTestTiltFlips(uint8_t* prev, const float* readings, float len);
int CoXlTestTiltTrack(void* table, uint32_t id, const float* p, float* first);
int CoXlTestTnKind(uint32_t flags, uintptr_t carver);
int CoXlTestTnOver(const float* m, const float* lo, const float* hi, const float* p, float grow, float* zlo, float* zhi);
int CoXlTestTnMiss(float z, float lo, float hi, float height);
int CoXlTestTnLate(const uint32_t* at, const uint32_t* now);
int CoXlTestTnAgentIndex(uintptr_t exe, uint32_t hash);
void CoXlTestTnObjects(uintptr_t nav, uint32_t (*cell)(uintptr_t, float, float), uint32_t (*gather)(uintptr_t, uint32_t, uint32_t, uintptr_t*, uint32_t),
    const float* p, uint32_t* counts, uint32_t* flags, uint32_t* material, int* read, float* carverZ, uint32_t* painters, uint64_t* owner);
enum { FITS, MISSED, UNDER, LOOSE, GROUND }; /* the diagnostic's classes, in its order */
enum { CARVER, GEOMETRY, PAINTER, INERT };   /* what a cell's build does with a nav object */

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

    g_case = "a corner that read the ground at its start: named before the fit";
    const float held[4][3] = {{0.5f, 0.7f, 0.71f}, {0.5f, -3.0f, -0.01f}, {-0.5f, -3.0f, -0.01f}, {-0.5f, 0.7f, 1.09f}}; /* 630 with its rear corners read as the ground */
    CHECK(Class(held, 4.0f, &r) == GROUND && r < 20.0f); /* it would fit */
    const float heldMiss[4][3] = {{0.5f, 0.7f, 4.0f}, {0.5f, -3.0f, -0.01f}, {-0.5f, -3.0f, 0.3f}, {-0.5f, 0.7f, 1.09f}};
    CHECK(Class(heldMiss, 4.0f, &r) == GROUND); /* and before a miss */
    const float shallow[4][3] = {{0.5f, 0.7f, 0.71f}, {0.5f, -3.0f, -0.02f}, {-0.5f, -3.0f, -0.02f}, {-0.5f, 0.7f, 1.09f}};
    CHECK(Class(shallow, 4.0f, &r) == FITS); /* the game's own reading just under 0 is not it */
    const float nought[4][3] = {{0.5f, 0.7f, 0.71f}, {0.5f, -3.0f, 0.0f}, {-0.5f, -3.0f, 0.0f}, {-0.5f, 0.7f, 1.09f}};
    CHECK(Class(nought, 4.0f, &r) == FITS);

    g_case = "neither a miss nor a start under a surface";
    const float loose[4][3] = {{0.5f, 0.7f, 0.24f}, {0.5f, -0.7f, 1.20f}, {-0.5f, -0.7f, 0.29f}, {-0.5f, 0.7f, 3.15f}}; /* 64D, 00:44:24 */
    CHECK(Class(loose, 4.0f, &r) == LOOSE && Near(r, 50.4f, 0.2f));
    const float touching[4][3] = {{0.5f, 0.7f, 0.0f}, {0.5f, -0.7f, 0.0f}, {-0.5f, -0.7f, 1.0f}, {-0.5f, 0.7f, 1.0f}};
    CHECK(Class(touching, 4.0f, &r) == LOOSE && Near(r, 45.0f, 0.05f)); /* a reading of 0 is not under a surface */

    g_case = "corners that span no area";
    const float point[4][3] = {{0.5f, 0.7f, 0.2f}, {0.5f, 0.7f, 0.9f}, {0.5f, 0.7f, 0.4f}, {0.5f, 0.7f, 0.1f}};
    CHECK(Class(point, 4.0f, &r) == LOOSE && r == -1.0f);
}

/* Corner readings between two scans: under 0 to a miss or back is a flip; a hit between them is not. */
static void Flips(void)
{
    uint8_t prev[4] = {0, 0, 0, 0};
    g_case = "corner flips";
    const float a[4] = {0.71f, -0.01f, -0.01f, 1.09f}, b[4] = {0.71f, 4.0f, -0.01f, 1.09f}, c[4] = {0.5f, -0.01f, 4.0f, 4.0f}, d[4] = {0.5f, 0.3f, 4.0f, -1.2f}, e[4] = {0.5f, 4.0f, 4.0f, 4.0f};
    CHECK(CoXlTestTiltFlips(prev, a, 4.0f) == 0 && prev[0] == 0 && prev[1] == 1 && prev[2] == 1 && prev[3] == 0); /* the first readings */
    CHECK(CoXlTestTiltFlips(prev, b, 4.0f) == 1 && prev[1] == 2);
    CHECK(CoXlTestTiltFlips(prev, c, 4.0f) == 2 && prev[3] == 2); /* corners 1 and 2; corner 3 came from a hit */
    CHECK(CoXlTestTiltFlips(prev, d, 4.0f) == 1 && prev[1] == 0); /* corner 3: a miss to under 0 */
    CHECK(CoXlTestTiltFlips(prev, e, 4.0f) == 1);                 /* corner 3 back; corner 1 came from a hit */
    CHECK(CoXlTestTiltFlips(prev, e, 4.0f) == 0);
    uint8_t under[4] = {1, 1, 1, 1};
    CHECK(CoXlTestTiltFlips(under, e, 3.0f) == 0 && under[1] == 0); /* 4.0 on a 3 m ray is a hit */

    g_case = "units followed";
    void* table = calloc(1024, 64);
    const float p[3] = {1, 2, 3}, q[3] = {9, 9, 9}; float first[3] = {0, 0, 0};
    int at = CoXlTestTiltTrack(table, 5, p, first);
    CHECK(at >= 0 && first[0] == 1 && first[1] == 2 && first[2] == 3);
    CHECK(CoXlTestTiltTrack(table, 5, q, first) == at && first[0] == 1 && first[2] == 3); /* where it was first seen stays */
    int other = CoXlTestTiltTrack(table, 6, q, first);
    CHECK(other >= 0 && other != at && first[0] == 9);
    for (uint32_t k = 1; k < 16; k++) CHECK(CoXlTestTiltTrack(table, 5 + 1024 * k, q, first) >= 0); /* the ids that share its places */
    CHECK(CoXlTestTiltTrack(table, 5 + 1024 * 16, q, first) == -1);
    CHECK(CoXlTestTiltTrack(table, 5 + 1024 * 3, p, first) >= 0 && first[0] == 9);
    free(table);
}

/* The nav objects of the test's grid: the engine's cell and gather steps as fakes. */
static uint8_t g_objects[5][0x80];
static struct { uintptr_t grid; uint32_t lo, hi, limit, n; float x, y; } g_gather;
static uint32_t FakeCell(uintptr_t grid, float x, float y) { g_gather.grid = grid; g_gather.x = x; g_gather.y = y; return 77; }
static uint32_t FakeGather(uintptr_t grid, uint32_t lo, uint32_t hi, uintptr_t* buf, uint32_t limit)
{
    g_gather.lo = lo; g_gather.hi = hi; g_gather.limit = limit;
    if (grid != g_gather.grid) return 0;
    for (uint32_t k = 0; k < g_gather.n && k < 5; k++) buf[k] = (uintptr_t)g_objects[k];
    return g_gather.n;
}
static void Object(int k, float x, float y, float z, float zlo, float zhi, uint32_t flags, uint32_t material, uint64_t owner, uintptr_t carver, uintptr_t painter)
{
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1}, box[6] = {-1, -1, zlo, 1, 1, zhi};
    memset(g_objects[k], 0, 0x80);
    memcpy(g_objects[k], m, sizeof m); memcpy(g_objects[k] + 0x40, &flags, 4); memcpy(g_objects[k] + 0x44, &material, 4);
    memcpy(g_objects[k] + 0x48, box, sizeof box); memcpy(g_objects[k] + 0x60, &owner, 8); memcpy(g_objects[k] + 0x68, &carver, 8); memcpy(g_objects[k] + 0x70, &painter, 8);
}

static void NavObjects(void)
{
    g_case = "what a build does with an object";
    CHECK(CoXlTestTnKind(0x6, 0x1000) == CARVER && CoXlTestTnKind(0x1, 0x1000) == CARVER); /* the carver is handed whatever the flags */
    CHECK(CoXlTestTnKind(0x1, 0) == GEOMETRY && CoXlTestTnKind(0x31, 0) == GEOMETRY);
    CHECK(CoXlTestTnKind(0x10, 0) == PAINTER && CoXlTestTnKind(0x20, 0) == PAINTER);
    CHECK(CoXlTestTnKind(0x6, 0) == INERT && CoXlTestTnKind(0x40, 0) == INERT);

    g_case = "a box over a point";
    float zlo, zhi;
    const float turned[16] = {0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, lo[3] = {-1, -0.5f, 0}, hi[3] = {1, 0.5f, 3}; /* 4 m along world y, 2 m along x */
    const float within[3] = {0.9f, 1.9f, 10.0f}, beside[3] = {1.9f, 0, 0}, away[3] = {2.1f, 0, 0}, along[3] = {0, 2.1f, 0};
    CHECK(CoXlTestTnOver(turned, lo, hi, within, 0, &zlo, &zhi) && zlo == 0.0f && zhi == 3.0f); /* however high the point */
    CHECK(!CoXlTestTnOver(turned, lo, hi, beside, 0, &zlo, &zhi));
    CHECK(CoXlTestTnOver(turned, lo, hi, beside, 1.0f, &zlo, &zhi)); /* grown by a metre of the world's */
    CHECK(!CoXlTestTnOver(turned, lo, hi, away, 1.0f, &zlo, &zhi));
    CHECK(!CoXlTestTnOver(turned, lo, hi, along, 0, &zlo, &zhi));
    const float lying[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 2, 1}, llo[3] = {-1, 0, -1}, lhi[3] = {1, 5, 1}; /* its second row is world up */
    const float over[3] = {0, 0, 100}, out[3] = {0, 3, 0};
    CHECK(CoXlTestTnOver(lying, llo, lhi, over, 0, &zlo, &zhi) && zlo == 2.0f && zhi == 7.0f);
    CHECK(!CoXlTestTnOver(lying, llo, lhi, out, 0, &zlo, &zhi));
    const float flat[3] = {1, 0.5f, -1};
    CHECK(!CoXlTestTnOver(turned, lo, flat, within, 0, &zlo, &zhi)); /* a box turned inside out */

    g_case = "a face against the carvers under it";
    CHECK(CoXlTestTnMiss(7.0f, 5.0f, 6.2f, 2.0f) == 1);  /* the face floats over the carver's top */
    CHECK(CoXlTestTnMiss(6.2f, 5.0f, 6.2f, 2.0f) == 0);
    CHECK(CoXlTestTnMiss(5.5f, 5.0f, 6.2f, 2.0f) == 0);
    CHECK(CoXlTestTnMiss(3.0f, 5.0f, 6.2f, 2.0f) == 0);  /* the character's height over the face reaches it */
    CHECK(CoXlTestTnMiss(2.9f, 5.0f, 6.2f, 2.0f) == -1);
    CHECK(CoXlTestTnMiss(2.9f, 5.0f, 6.2f, 2.5f) == 0);

    g_case = "objects filed after a cell's build";
    const uint32_t then[4] = {3, 2, 1, 9}, same[4] = {3, 2, 1, 12}, fewer[4] = {2, 1, 0, 0}, c1[4] = {4, 2, 1, 9}, g1[4] = {3, 3, 1, 9}, p1[4] = {3, 2, 2, 9};
    CHECK(!CoXlTestTnLate(then, same) && !CoXlTestTnLate(then, fewer)); /* inert objects reach no navmesh */
    CHECK(CoXlTestTnLate(then, c1) && CoXlTestTnLate(then, g1) && CoXlTestTnLate(then, p1));

    g_case = "an agent type's place";
    uint8_t* exe = calloc(1, 0x27C5D00u); uint32_t types[32];
    memset(types, 0, sizeof types);
    types[0x34 / 4] = 0x40; types[0x40 / 4] = 3; types[0x48 / 4] = 0x111; types[0x4C / 4] = 0x222; types[0x50 / 4] = 0x333; types[0x54 / 4] = 0x444;
    uintptr_t R = (uintptr_t)types;
    memcpy(exe + 0x27C5CF8u, &R, 8);
    CHECK(CoXlTestTnAgentIndex((uintptr_t)exe, 0x111) == 0 && CoXlTestTnAgentIndex((uintptr_t)exe, 0x222) == 1 && CoXlTestTnAgentIndex((uintptr_t)exe, 0x333) == 2);
    CHECK(CoXlTestTnAgentIndex((uintptr_t)exe, 0x444) == -1); /* past the count */
    CHECK(CoXlTestTnAgentIndex((uintptr_t)exe, 0) == -1 && CoXlTestTnAgentIndex(0, 0x111) == -1);
    types[0x40 / 4] = 65;
    CHECK(CoXlTestTnAgentIndex((uintptr_t)exe, 0x111) == -1); /* not a type table */
    free(exe);

    g_case = "the nav objects over a prop's point";
    static uint8_t nav[0x6800];
    uint32_t counts[4], flags, material, painters; int read; float cz[2]; uint64_t owner;
    const float p[3] = {10.5f, 20.2f, 6.2f};
    Object(0, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x1122334455667788ull, 0, 0x2000);  /* geometry, with a painter set */
    Object(1, 10, 20, 5, 0, 1.2f, 0x6, 0xBB, 0x99ull, 0x1000, 0);                /* a carver, z 5 to 6.2 */
    Object(2, 50, 50, 5, 0, 9.0f, 0x6, 0xCC, 0x98ull, 0x1000, 0);                /* a carver elsewhere in the cell */
    Object(3, 10.4f, 19.5f, 3, 0, 1.0f, 0x6, 0xDD, 0x97ull, 0x1000, 0);          /* a carver under it, z 3 to 4 */
    Object(4, 12.4f, 20, 5, 0, 1.0f, 0x30, 0xEE, 0x96ull, 0, 0x2000);            /* a painter 0.9 m beside the point */
    g_gather.n = 5;
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner);
    CHECK(read == 1 && g_gather.grid == (uintptr_t)nav + 0x6710u && g_gather.x == 10.5f && g_gather.y == 20.2f && g_gather.lo == 77 && g_gather.hi == 77);
    CHECK(counts[CARVER] == 2 && counts[GEOMETRY] == 1 && counts[PAINTER] == 1 && counts[INERT] == 0);
    CHECK(flags == 0x1 && material == 0xAA && owner == 0x1122334455667788ull); /* the first over the point */
    CHECK(cz[0] == 3.0f && cz[1] == 6.2f && painters == 2);
    g_gather.n = 0;
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner);
    CHECK(read == 1 && counts[CARVER] + counts[GEOMETRY] + counts[PAINTER] + counts[INERT] == 0 && painters == 0);
    g_gather.n = 200000; /* more than the grid can hold: not read */
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner);
    CHECK(read == 0 && counts[CARVER] == 0);
    CoXlTestTnObjects(0, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner);
    CHECK(read == 0);
}

static float MeanV(float a, float b, float c, float d, float len) { return 0.25f * ((1.0f - a / len) + (1.0f - b / len) + (1.0f - c / len) + (1.0f - d / len)); }

int main(void)
{
    Classes();
    Flips();
    NavObjects();
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
