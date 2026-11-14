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
int CoXlTestTnReach(float flo, float fhi, float lo, float hi, float height);
int CoXlTestTnAgainst(float face, float surface);
int CoXlTestTnInBox(const float* box, float x, float y);
int CoXlTestTnLate(const uint32_t* at, const uint32_t* now);
int CoXlTestTnAgentIndex(uintptr_t exe, uint32_t hash);
void CoXlTestTnObjects(uintptr_t nav, uint32_t (*cell)(uintptr_t, float, float), uint32_t (*gather)(uintptr_t, uint32_t, uint32_t, uintptr_t*, uint32_t),
    const float* p, uint32_t* counts, uint32_t* flags, uint32_t* material, int* read, float* carverZ, uint32_t* painters, uint64_t* owner, float* volumeZ, uint32_t* volumes);
float CoXlTestNhCh(float cs, float ch, float zlo, float zhi, float want);
int CoXlTestNhCells(float ch, int climb);
int CoXlTestNhClamped(const uintptr_t* spans, int columns);
void CoXlTestNhCounts(long* changed, long* tall, long* clamped);
void CoXlTestNhLift(float* verts, int n, float by);
void CoXlTestNhSet(float ch, float error, float dist, void* raster, void* compact, void* detail);
int CoXlTestTnKept(uint64_t* slot, uint32_t size, uint32_t* used, uint64_t id, uint32_t cell, uint32_t build, int add);
uint64_t CoXlTestTnId(const uint8_t* o);
int CoXlTestTnBuilt(uintptr_t nav, uint32_t (*cell)(uintptr_t, float, float), uint32_t (*gather)(uintptr_t, uint32_t, uint32_t, uintptr_t*, uint32_t),
    uint32_t cellId, const float* box, int start, const float* p);
int CoXlTestNhInstall(void);
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
/* A carver (0x28 B: its volume at +0x18) whose volume's world box (+0x30 / +0x40) is (x0, y0, z0)..(x1, y1, z1). */
static uint8_t g_volumes[3][0xD0], g_carvers[3][0x28];
static uintptr_t Carver(int k, float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float lo[4] = {x0, y0, z0, 0}, hi[4] = {x1, y1, z1, 0}; uintptr_t v = (uintptr_t)g_volumes[k];
    memcpy(g_volumes[k] + 0x30, lo, sizeof lo); memcpy(g_volumes[k] + 0x40, hi, sizeof hi); memcpy(g_carvers[k] + 0x18, &v, 8);
    return (uintptr_t)g_carvers[k];
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

    g_case = "the navmesh's heights against the carvers' boxes";
    CHECK(CoXlTestTnReach(7.0f, 7.0f, 5.0f, 6.2f, 2.0f) == 1);   /* the face floats over the boxes' top */
    CHECK(CoXlTestTnReach(6.2f, 6.2f, 5.0f, 6.2f, 2.0f) == 0);
    CHECK(CoXlTestTnReach(6.0f, 9.0f, 5.0f, 6.2f, 2.0f) == 0);   /* a triangle that dips to the boxes meets them */
    CHECK(CoXlTestTnReach(6.3f, 9.0f, 5.0f, 6.2f, 2.0f) == 1);
    CHECK(CoXlTestTnReach(1.0f, 3.0f, 5.0f, 6.2f, 2.0f) == 0);   /* the character's height over its top reaches them */
    CHECK(CoXlTestTnReach(1.0f, 2.9f, 5.0f, 6.2f, 2.0f) == -1);
    CHECK(CoXlTestTnReach(1.0f, 2.9f, 5.0f, 6.2f, 2.5f) == 0);

    g_case = "a face against the prop's surface";
    CHECK(CoXlTestTnAgainst(6.6f, 6.2f) == 1 && CoXlTestTnAgainst(6.4f, 6.2f) == 0 && CoXlTestTnAgainst(6.0f, 6.2f) == 0 && CoXlTestTnAgainst(5.8f, 6.2f) == -1);

    g_case = "a point in a built cell's box";
    const float cellBox[4] = {-10, 20, 30, 60};
    CHECK(CoXlTestTnInBox(cellBox, 0, 40) && CoXlTestTnInBox(cellBox, -10, 20) && CoXlTestTnInBox(cellBox, 30, 60));
    CHECK(!CoXlTestTnInBox(cellBox, -10.1f, 40) && !CoXlTestTnInBox(cellBox, 30.1f, 40) && !CoXlTestTnInBox(cellBox, 0, 19.9f) && !CoXlTestTnInBox(cellBox, 0, 60.1f));

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
    uint32_t counts[4], flags, material, painters, volumes; int read; float cz[2], vz[2]; uint64_t owner;
    const float p[3] = {10.5f, 20.2f, 6.2f};
    Object(0, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x1122334455667788ull, 0, 0x2000);  /* geometry, with a painter set */
    Object(1, 10, 20, 5, 0, 1.2f, 0x6, 0xBB, 0x99ull, Carver(0, 9.2f, 19.1f, 5.1f, 10.8f, 20.9f, 5.9f), 0); /* a carver, its box z 5 to 6.2, its volume's 5.1 to 5.9 */
    Object(2, 50, 50, 5, 0, 9.0f, 0x6, 0xCC, 0x98ull, Carver(1, 49, 49, 5, 51, 51, 14), 0);                  /* a carver elsewhere in the cell */
    Object(3, 10.4f, 19.5f, 3, 0, 1.0f, 0x6, 0xDD, 0x97ull, Carver(2, 9.4f, 18.5f, 3.2f, 10.45f, 20.5f, 3.8f), 0); /* under it; its volume's box ends 5 cm short of the point */
    Object(4, 12.4f, 20, 5, 0, 1.0f, 0x30, 0xEE, 0x96ull, 0, 0x2000);            /* a painter 0.9 m beside the point */
    g_gather.n = 5;
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(read == 1 && g_gather.grid == (uintptr_t)nav + 0x6710u && g_gather.x == 10.5f && g_gather.y == 20.2f && g_gather.lo == 77 && g_gather.hi == 77);
    CHECK(counts[CARVER] == 2 && counts[GEOMETRY] == 1 && counts[PAINTER] == 1 && counts[INERT] == 0);
    CHECK(flags == 0x1 && material == 0xAA && owner == 0x1122334455667788ull); /* the first over the point */
    CHECK(cz[0] == 3.0f && cz[1] == 6.2f && painters == 2);
    CHECK(volumes == 1 && vz[0] == 5.1f && vz[1] == 5.9f); /* the volume's own box, not the object's */
    Carver(2, 9.4f, 18.5f, 3.2f, 10.5f, 20.5f, 3.8f);
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(volumes == 2 && vz[0] == 3.2f && vz[1] == 5.9f);
    memset(g_carvers[0] + 0x18, 0, 8); /* a carver with no volume */
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(volumes == 1 && vz[0] == 3.2f && vz[1] == 3.8f && counts[CARVER] == 2);
    g_gather.n = 0;
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(read == 1 && counts[CARVER] + counts[GEOMETRY] + counts[PAINTER] + counts[INERT] == 0 && painters == 0);
    g_gather.n = 200000; /* more than the grid can hold: not read */
    CoXlTestTnObjects((uintptr_t)nav, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(read == 0 && counts[CARVER] == 0);
    CoXlTestTnObjects(0, FakeCell, FakeGather, p, counts, &flags, &material, &read, cz, &painters, &owner, vz, &volumes);
    CHECK(read == 0);
}

static float MeanV(float a, float b, float c, float d, float len) { return 0.25f * ((1.0f - a / len) + (1.0f - b / len) + (1.0f - c / len) + (1.0f - d / len)); }

/* The nav heights' reviewed code in the engine image. */
static void NhPins(uint8_t* exe)
{
    Hex(exe + 0x89B630u, "48 8B C4 4C 89 48 20 89 50 10 53 55 57 41 54 41");
    Hex(exe + 0x829590u, "4C 89 4C 24 20 44 89 44 24 18 89 54 24 10 48 89");
    Hex(exe + 0x8B53B0u, "48 8B C4 4C 89 40 18 48 89 50 10 48 89 48 08 55");
    Hex(exe + 0x89AA86u, "F3 44 0F 11 40 20 F3 44 0F 11 40 24");
    Hex(exe + 0x89AB53u, "E8 D8 0A 00 00");
    Hex(exe + 0x89ABA0u, "E8 EB E9 F8 FF");
    Hex(exe + 0x89AEB0u, "E8 FB A4 01 00");
    Hex(exe + 0x89B78Au, "F3 0F 5E 7B 24");
    Hex(exe + 0x89B862u, "B8 FF 1F 00 00");
    Hex(exe + 0x89B882u, "B9 FF 1F 00 00");
    Hex(exe + 0x8BCDF8u, "0F B7 4C 24 50 48 89 43 38 0F B6 44 24 58 C1 E1 0D");
    Hex(exe + 0x82968Eu, "41 8B 46 20 41 89 47 34 41 8B 46 24");
    Hex(exe + 0x8B5CE2u, "F3 0F 10 50 38");
    Hex(exe + 0x89B08Fu, "44 0F 57 05 D9 F7 E0 00");
}

/* The Recast stages as the fakes: what each was handed. The detail stage hands back two vertices at heights 10 and 20. */
typedef uintptr_t (*RasterFn)(uintptr_t terrain, uintptr_t b, const float* bounds, float* field);
typedef uint8_t (*CompactFn)(uintptr_t ctx, int height, int climb, float* field, uintptr_t compact);
typedef uint8_t (*DetailFn)(uintptr_t ctx, uintptr_t mesh, const float* compact, float dist, float error, uintptr_t detail);
static struct { int calls; uintptr_t a, b, c; const float* bounds; float ch, dist, error; int height, climb; uint8_t ok; uint32_t top; } g_nh;
static float g_nhVerts[6];
static uint32_t g_nhSpan[4][4]; static uintptr_t g_nhSpans[4];
/* Its four columns get a span each, the last one ending at `top` cell heights. */
static uintptr_t FakeRaster(uintptr_t terrain, uintptr_t b, const float* bounds, float* field)
{
    g_nh.calls++; g_nh.a = terrain; g_nh.b = b; g_nh.bounds = bounds; g_nh.ch = field[9];
    for (int k = 0; k < 4; k++) { g_nhSpan[k][0] = 5u | ((k == 3 ? g_nh.top : 6u) << 13) | (63u << 26); g_nhSpans[k] = (uintptr_t)g_nhSpan[k]; }
    uintptr_t* spans = g_nhSpans; memcpy((uint8_t*)field + 0x28, &spans, 8);
    return 0x77;
}
static uint8_t FakeCompact(uintptr_t ctx, int height, int climb, float* field, uintptr_t compact)
{
    g_nh.calls++; g_nh.a = ctx; g_nh.height = height; g_nh.climb = climb; g_nh.ch = field[9]; g_nh.c = compact;
    return 1;
}
static uint8_t FakeDetail(uintptr_t ctx, uintptr_t mesh, const float* compact, float dist, float error, uintptr_t detail)
{
    const float v[6] = {1.0f, 10.0f, 2.0f, 3.0f, 20.0f, 4.0f};
    g_nh.calls++; g_nh.a = ctx; g_nh.b = mesh; g_nh.dist = dist; g_nh.error = error; g_nh.c = detail;
    memcpy(g_nhVerts, v, sizeof v);
    return g_nh.ok;
}

/* A heightfield (0x40 B): cell size at +0x20, cell height at +0x24, its heights' range at +0xC and +0x18. */
static void Field(float* f, float cs, float ch, float zlo, float zhi)
{
    int32_t w[2] = {2, 2};
    memset(f, 0, 16 * sizeof *f); memcpy(f, w, 8);
    f[8] = cs; f[9] = ch; f[3] = zlo; f[6] = zhi;
}

static void NavHeights(void)
{
    g_case = "the cell height a heightfield gets";
    CHECK(CoXlTestNhCh(4.0f, 4.0f, -16.0f, 500.0f, 0.5f) == 0.5f);   /* an extra large level's */
    CHECK(CoXlTestNhCh(4.0f, 4.0f, -16.0f, 500.0f, 0.0f) == 4.0f);   /* switched off */
    CHECK(CoXlTestNhCh(0.5f, 0.5f, -16.0f, 500.0f, 0.25f) == 0.5f);  /* the game's maps are theirs */
    CHECK(CoXlTestNhCh(0.6f, 0.6f, -16.0f, 500.0f, 0.5f) == 0.5f);   /* any cell over 0.5 m */
    CHECK(CoXlTestNhCh(4.0f, 4.0f, -16.0f, 500.0f, 4.0f) == 4.0f && CoXlTestNhCh(4.0f, 4.0f, -16.0f, 500.0f, 6.0f) == 4.0f); /* never coarser */
    CHECK(CoXlTestNhCh(4.0f, 2.0f, -16.0f, 500.0f, 0.5f) == 2.0f);   /* not the game's one value: left */
    CHECK(CoXlTestNhCh(4.0f, 4.0f, 100.0f, 4195.5f, 0.5f) == 0.5f);  /* 8191 cell heights: a span's last count */
    CHECK(CoXlTestNhCh(4.0f, 4.0f, 100.0f, 4196.0f, 0.5f) == 4.0f);  /* 8192: past it */

    g_case = "the walkable counts from the cell height";
    CHECK(CoXlTestNhCells(0.5f, 0) == 4 && CoXlTestNhCells(0.5f, 1) == 10); /* the game's 2 m and 5 m */
    CHECK(CoXlTestNhCells(1.5f, 0) == 2 && CoXlTestNhCells(1.5f, 1) == 3);  /* the height up, the climb down */

    g_case = "columns that end at a span's last count";
    uint32_t span[3] = {5u | (0x1FFFu << 13), 0x1FFFu | (0x1FFEu << 13) | (63u << 26), 7u | (0x1FFFu << 13) | (63u << 26)};
    const uintptr_t columns[4] = {(uintptr_t)&span[0], 0, (uintptr_t)&span[1], (uintptr_t)&span[2]};
    CHECK(CoXlTestNhClamped(columns, 4) == 2 && CoXlTestNhClamped(columns, 3) == 1 && CoXlTestNhClamped(columns + 1, 2) == 0);

    g_case = "the detail vertices raised";
    float v[6] = {1.0f, 10.0f, 2.0f, 3.0f, 20.0f, 4.0f};
    CoXlTestNhLift(v, 2, 3.5f);
    CHECK(v[0] == 1.0f && v[1] == 13.5f && v[2] == 2.0f && v[3] == 3.0f && v[4] == 23.5f && v[5] == 4.0f);
    CoXlTestNhLift(v, 1, 1.0f);
    CHECK(v[1] == 14.5f && v[4] == 23.5f);

    g_case = "not the reviewed code";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    uint8_t* exe = fake_host_exe();
    const uintptr_t off = (uintptr_t)exe - (uintptr_t)g_fake.image;
    NhPins(exe);
    exe[0x8B5CE2u + 4] = 0x34; /* the detail vertices raised by another field */
    CHECK(!CoXlTestNhInstall());
    CHECK(fake_host_detour(off + 0x89B630u) == NULL && fake_host_detour(off + 0x829590u) == NULL && fake_host_detour(off + 0x8B53B0u) == NULL);
    CHECK(fake_host_logged("nav heights: exe+0x8B5CE2 is not the reviewed code") && fake_host_logged("nav heights not installed"));

    g_case = "another build of the game";
    g_coApi = fake_host_reset("25480439");
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    exe = fake_host_exe();
    NhPins(exe);
    CHECK(!CoXlTestNhInstall() && g_fake.hookCount == 0);

    g_case = "the heights' entry cannot be hooked";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    exe = fake_host_exe();
    NhPins(exe);
    g_fake.noHook = (uintptr_t)exe + 0x89B630u - (uintptr_t)g_fake.image;
    CHECK(!CoXlTestNhInstall());
    CHECK(fake_host_logged("the heights' entry could not be hooked"));

    g_case = "hooked";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    exe = fake_host_exe();
    NhPins(exe);
    g_fake.originals[0].target = (uintptr_t)exe + 0x89B630u; g_fake.originals[0].original = (void*)FakeRaster;
    g_fake.originals[1].target = (uintptr_t)exe + 0x829590u; g_fake.originals[1].original = (void*)FakeCompact;
    g_fake.originals[2].target = (uintptr_t)exe + 0x8B53B0u; g_fake.originals[2].original = (void*)FakeDetail;
    CHECK(CoXlTestNhInstall());
    const uintptr_t at = (uintptr_t)exe - (uintptr_t)g_fake.image;
    RasterFn raster = (RasterFn)fake_host_detour(at + 0x89B630u);
    CompactFn compact = (CompactFn)fake_host_detour(at + 0x829590u);
    DetailFn detail = (DetailFn)fake_host_detour(at + 0x8B53B0u);
    CHECK(raster != NULL && compact != NULL && detail != NULL);
    if (raster == NULL || compact == NULL || detail == NULL) return;
    CHECK(fake_host_logged("nav heights installed: where a level's nav cells are over 0.5 m the navmesh's terrain has a cell height of 0.50 m (0: as the game has it), detail error 0.50 m, sampled every 16.0 m"));
    CHECK(g_fake.hooks[0].target == at + 0x8B53B0u); /* the detail mesh first: it undoes what the others change */

    float field[16], chf[16] = {0}, bounds[6] = {0};
    uint8_t mesh[0x28] = {0};
    float* verts = g_nhVerts; int32_t two = 2, tris = 1;
    memcpy(mesh + 8, &verts, sizeof verts); memcpy(mesh + 0x1C, &two, 4); memcpy(mesh + 0x20, &tris, 4);

    long changed, tall, clamped;
    g_case = "cells of 4 m: the heights at the cell height";
    CoXlTestNhSet(0.5f, 0.5f, 4.0f, (void*)FakeRaster, (void*)FakeCompact, (void*)FakeDetail);
    CoXlTestNhCounts(&changed, &tall, &clamped);
    Field(field, 4.0f, 4.0f, -16.0f, 500.0f);
    memset(&g_nh, 0, sizeof g_nh); g_nh.top = 1032;
    CHECK(raster(0x11, 0x22, bounds, field) == 0x77 && g_nh.calls == 1 && g_nh.a == 0x11 && g_nh.b == 0x22 && g_nh.bounds == bounds);
    CHECK(g_nh.ch == 0.5f && field[9] == 0.5f && field[8] == 4.0f); /* set before the game's stage; the cell size stays */
    CHECK(fake_host_logged("the first nav cell's terrain box spans z -16.0 to 500.0 m: 1032 of a span's 8191 cell heights of 0.50 m"));
    CoXlTestNhCounts(&changed, &tall, &clamped);
    CHECK(changed == 1 && tall == 0 && clamped == 0);
    memset(&g_nh, 0, sizeof g_nh);
    CHECK(compact(0x11, 1, 1, field, 0x55) == 1 && g_nh.calls == 1 && g_nh.a == 0x11 && g_nh.c == 0x55);
    CHECK(g_nh.height == 4 && g_nh.climb == 10); /* 2 m and 5 m, as on the game's maps */
    chf[13] = 4.0f; chf[14] = 0.5f;
    memset(&g_nh, 0, sizeof g_nh); g_nh.ok = 1;
    CHECK(detail(0x11, 0x22, chf, 24.0f, 4.0f, (uintptr_t)mesh) == 1 && g_nh.calls == 1 && g_nh.a == 0x11 && g_nh.b == 0x22 && g_nh.c == (uintptr_t)mesh);
    CHECK(g_nh.dist == 4.0f && g_nh.error == 0.5f);
    CHECK(g_nhVerts[1] == 13.5f && g_nhVerts[4] == 23.5f && g_nhVerts[0] == 1.0f && g_nhVerts[5] == 4.0f); /* the game lowers them 4 m, not 0.5 */

    g_case = "a column at a span's last count is counted";
    Field(field, 4.0f, 4.0f, -16.0f, 500.0f);
    memset(&g_nh, 0, sizeof g_nh); g_nh.top = 0x1FFF;
    CHECK(raster(0x11, 0x22, bounds, field) == 0x77);
    CoXlTestNhCounts(&changed, &tall, &clamped);
    CHECK(changed == 1 && tall == 0 && clamped == 1);

    g_case = "the detail stage failed: nothing raised";
    memset(&g_nh, 0, sizeof g_nh);
    CHECK(detail(0x11, 0x22, chf, 24.0f, 4.0f, (uintptr_t)mesh) == 0 && g_nhVerts[1] == 10.0f && g_nhVerts[4] == 20.0f);

    g_case = "the switch at 0: the game's";
    CoXlTestNhSet(0.0f, 0.5f, 4.0f, (void*)FakeRaster, (void*)FakeCompact, (void*)FakeDetail);
    Field(field, 4.0f, 4.0f, -16.0f, 500.0f);
    memset(&g_nh, 0, sizeof g_nh); g_nh.ok = 1;
    CHECK(raster(0x11, 0x22, bounds, field) == 0x77 && g_nh.calls == 1 && g_nh.ch == 4.0f && field[9] == 4.0f);
    CHECK(compact(0x11, 1, 1, field, 0x55) == 1 && g_nh.height == 1 && g_nh.climb == 1);
    chf[13] = 4.0f; chf[14] = 4.0f;
    CHECK(detail(0x11, 0x22, chf, 24.0f, 4.0f, (uintptr_t)mesh) == 1 && g_nh.dist == 24.0f && g_nh.error == 4.0f);
    CHECK(g_nhVerts[1] == 10.0f && g_nhVerts[4] == 20.0f);
    CoXlTestNhCounts(&changed, &tall, &clamped);
    CHECK(changed == 0 && tall == 0 && clamped == 0);

    g_case = "cells of 0.5 m: the game's, whatever the switch";
    CoXlTestNhSet(0.25f, 0.5f, 4.0f, (void*)FakeRaster, (void*)FakeCompact, (void*)FakeDetail);
    Field(field, 0.5f, 0.5f, -2.0f, 300.0f);
    memset(&g_nh, 0, sizeof g_nh); g_nh.ok = 1; g_nh.top = 0x1FFF;
    CHECK(raster(0x11, 0x22, bounds, field) == 0x77 && g_nh.ch == 0.5f && field[9] == 0.5f);
    CHECK(compact(0x11, 4, 10, field, 0x55) == 1 && g_nh.height == 4 && g_nh.climb == 10);
    field[9] = 0.25f; /* even a heightfield that came finer than its cells */
    CHECK(compact(0x11, 5, 11, field, 0x55) == 1 && g_nh.height == 5 && g_nh.climb == 11);
    chf[13] = 0.5f; chf[14] = 0.5f;
    CHECK(detail(0x11, 0x22, chf, 3.0f, 0.5f, (uintptr_t)mesh) == 1 && g_nh.dist == 3.0f && g_nh.error == 0.5f);
    chf[14] = 0.25f;
    CHECK(detail(0x11, 0x22, chf, 3.0f, 0.5f, (uintptr_t)mesh) == 1 && g_nh.dist == 3.0f && g_nh.error == 0.5f);
    CHECK(g_nhVerts[1] == 10.0f && g_nhVerts[4] == 20.0f);
    CoXlTestNhCounts(&changed, &tall, &clamped);
    CHECK(changed == 0 && tall == 0 && clamped == 0); /* its columns are not looked at */

    g_case = "heights past a span's count: the game's, and counted";
    CoXlTestNhSet(0.5f, 0.5f, 4.0f, (void*)FakeRaster, (void*)FakeCompact, (void*)FakeDetail);
    Field(field, 4.0f, 4.0f, 0.0f, 4100.0f);
    memset(&g_nh, 0, sizeof g_nh);
    CHECK(raster(0x11, 0x22, bounds, field) == 0x77 && g_nh.calls == 1 && g_nh.ch == 4.0f && field[9] == 4.0f);
    CoXlTestNhCounts(&changed, &tall, &clamped);
    CHECK(changed == 0 && tall == 1 && clamped == 0);
    CoXlTestNhSet(0.5f, 0.5f, 4.0f, NULL, NULL, NULL);
}

/* Whether a prop's nav objects were handed to its cells' builds. */
static void Builds(void)
{
    g_case = "the objects kept by cell and build";
    uint64_t slot[8] = {0}; uint32_t used = 0;
    CHECK(!CoXlTestTnKept(slot, 8, &used, 0xA1, 3, 1, 0));
    CHECK(CoXlTestTnKept(slot, 8, &used, 0xA1, 3, 1, 1) && used == 1 && CoXlTestTnKept(slot, 8, &used, 0xA1, 3, 1, 0));
    CHECK(!CoXlTestTnKept(slot, 8, &used, 0xA1, 3, 2, 0) && !CoXlTestTnKept(slot, 8, &used, 0xA1, 4, 1, 0) && !CoXlTestTnKept(slot, 8, &used, 0xA2, 3, 1, 0));
    CHECK(CoXlTestTnKept(slot, 8, &used, 0xA1, 3, 1, 1) && used == 1); /* once */
    for (uint64_t id = 1; id <= 5; id++) CHECK(CoXlTestTnKept(slot, 8, &used, id, 9, 1, 1));
    CHECK(used == 6 && !CoXlTestTnKept(slot, 8, &used, 77, 9, 1, 1) && used == 6); /* three quarters: no more */
    for (uint64_t id = 1; id <= 5; id++) CHECK(CoXlTestTnKept(slot, 8, &used, id, 9, 1, 0));
    CHECK(!CoXlTestTnKept(slot, 8, &used, 77, 9, 1, 0));
    for (uint64_t id = 100; id < 120; id++) CHECK(!CoXlTestTnKept(slot, 8, &used, id, 9, 1, 0)); /* a taken place is not the object */

    g_case = "an object is its place and resource";
    Object(0, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x11ull, 0, 0);
    Object(1, 10, 20, 5, 0, 3.0f, 0x6, 0xBB, 0x11ull, 0x1000, 0x2000); /* the same, in another array place */
    Object(2, 10, 20.5f, 5, 0, 1.2f, 0x1, 0xAA, 0x11ull, 0, 0);
    Object(3, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x12ull, 0, 0);
    CHECK(CoXlTestTnId(g_objects[0]) == CoXlTestTnId(g_objects[1]));
    CHECK(CoXlTestTnId(g_objects[0]) != CoXlTestTnId(g_objects[2]) && CoXlTestTnId(g_objects[0]) != CoXlTestTnId(g_objects[3]));

    g_case = "a prop handed to its cell's build, and one that came after it";
    static uint8_t nav[0x6800];
    const float p[3] = {10.5f, 20.2f, 6.2f}, away[3] = {300, 300, 0}, box[4] = {0, 0, 64, 64}, other[4] = {-64, 0, 11, 64}, apart[4] = {100, 100, 164, 164};
    Object(0, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x11ull, 0, 0);    /* a rock */
    Object(1, 50, 50, 5, 0, 1.2f, 0x1, 0xAA, 0x12ull, 0, 0);    /* another, elsewhere */
    Object(2, 10, 20, 5, 0, 1.0f, 0x30, 0xEE, 0x13ull, 0, 0x2000); /* a painter at the rock: not looked up */
    g_gather.n = 0;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, -1, p) == -1);  /* no object over the point */
    g_gather.n = 3;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, -1, p) == 1);   /* built with the rock */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, 0, away) == -1); /* no built cell holds that point */
    g_gather.n = 2;
    Object(0, 50, 50, 5, 0, 1.2f, 0x1, 0xAA, 0x12ull, 0, 0); Object(1, 10, 20, 5, 0, 1.0f, 0x30, 0xEE, 0x13ull, 0, 0x2000);
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, -1, p) == -1);  /* built before the rock: a painter alone is no prop */
    g_gather.n = 3;
    Object(2, 10, 20, 5, 0, 1.2f, 0x1, 0xAA, 0x11ull, 0, 0);    /* the rock comes */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, 0, p) == 0);    /* not in the cell's build */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 9, apart, 1, p) == 0);    /* another cell's build does not count */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, 1, p) == 1);    /* built again with it */
    g_gather.n = 2;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 8, other, 1, p) == -1); /* a second cell holding the point, built without it */
    g_gather.n = 3;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 8, other, 0, p) == 0);  /* every cell that holds it counts */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 8, other, 1, p) == 1);
    g_gather.n = 2;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, 1, p) == -1);
    g_gather.n = 3;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, 0, p) == 0);    /* an older build that had it does not count */
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 7, box, -1, p) == 1);
    g_gather.n = 2;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 9, apart, 1, p) == -1);
    g_gather.n = 3;
    CHECK(CoXlTestTnBuilt((uintptr_t)nav, FakeCell, FakeGather, 9, apart, 0, p) == 1);    /* a cell that does not hold the point is not asked */
}

int main(void)
{
    Classes();
    Flips();
    NavObjects();
    Builds();
    NavHeights();
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
