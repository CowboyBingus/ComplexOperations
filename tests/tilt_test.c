/* The rotation's ray reader (game 0x620810) on extra large levels: a ray that starts under a live tile's ground reads
 * that ground above it (minus the depth) when it missed; hits, misses over the ground, other places and a busy tile
 * list keep the game's answer. A four-corner unit's hit on a prop reads the ray's length to the drawn ground; hits on
 * the ground, other modes, other levels and places with no drawn ground keep the game's. The game's call is always
 * made as the game made it.
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
int CoXlTestLeanSlot(float* xy);
int CoXlTestTiltIsProp(uint32_t owner, uint32_t body, uint32_t terrain);
int CoXlTestTiltOnProp(uintptr_t game, uintptr_t mgr, const float* p, float* z);
void CoXlTestTiltPropSample(uintptr_t mgr, uintptr_t R, uint32_t id, const float* q, const float* p, float fromUpright);
void CoXlTestTiltSunkLines(int n);
void CoXlTestTiltCost(long long ticks);
void CoXlTestTiltCostNote(long long perSecond, long frames);
long CoXlTestTiltCostCalls(void);
void CoXlTestTiltPropNote(void);
float CoXlTestTiltPropRead(float t, float len);
int CoXlTestTiltGroundAlong(const float* o, const float* dir, float* t);
void CoXlTestTiltProps(int on);
void CoXlTestTiltPropCounts(long* ground, long* kept);
void CoXlTestTiltNote(void);
void CoXlTestLeanTile(float cx, float cy, float z, float gx, float gy);
void CoXlTestFgWorld(float live);

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

/* A four-corner unit's ray that hit: record 2 of unit 7 (mode at R+0x54, full length at R+0x48), from (x, y, z) along
 * dir, its hit naming owner and body; the level terrain's body is 0x80300067. */
static uint8_t g_units[8][0x298], g_hit[0x2C], g_mgr[0x60];
static void PropRay(float x, float y, float z, float dx, float dy, float dz, uint32_t owner, uint32_t body)
{
    const uintptr_t recs = (uintptr_t)g_units, hit = (uintptr_t)g_hit; const uint32_t terrain = 0x80300067u, ids[2] = {owner, body};
    const float dir[3] = {dx, dy, dz};
    memcpy(g_mgr + 0x18, &terrain, 4); memcpy(g_mgr + 0x58, &recs, 8);
    memcpy(g_hit + 0x1C, ids, 8);
    Ray(2, x, y, z, 1);
    memcpy(g_rays[1] + 8, &hit, 8); memcpy(g_rays[1] + 0x1C, dir, 12);
}
static void Unit(int i, uint32_t mode, float len) { memcpy(g_units[i] + 0x54, &mode, 4); memcpy(g_units[i] + 0x48, &len, 4); }
static float PropCall(ReadFn read, uint32_t i, float answer)
{
    memset(&g_read, 0, sizeof g_read);
    g_read.answer = answer;
    return read((uintptr_t)g_mgr, i, (uintptr_t)g_rays, 2, 3);
}
/* The game's call made once as the game made it. */
static int PropMade(uint32_t i) { return g_read.calls == 1 && g_read.mgr == (uintptr_t)g_mgr && g_read.i == i && g_read.rays == (uintptr_t)g_rays && g_read.request == 2 && g_read.coarse == 3; }

/* The physics ray as the fake: what it was asked, and one hit (owner +0x1C, body +0x20) or none. */
static struct { int calls, hits, seq; uint32_t type, filter, max, owner, body; float origin[3], dir[3], length, z, zs[5], at[5][2]; } g_ray;
static uint32_t FakeRay(uint32_t type, const float* origin, const float* dir, float length, uint32_t filter, void* hits, uint32_t max)
{
    g_ray.calls++; g_ray.type = type; g_ray.length = length; g_ray.filter = filter; g_ray.max = max;
    memcpy(g_ray.origin, origin, 12); memcpy(g_ray.dir, dir, 12);
    if (g_ray.seq) { g_ray.z = g_ray.zs[(g_ray.calls - 1) % 5]; g_ray.at[(g_ray.calls - 1) % 5][0] = origin[0]; g_ray.at[(g_ray.calls - 1) % 5][1] = origin[1]; }
    if (g_ray.hits) { memcpy((uint8_t*)hits + 8, &g_ray.z, 4); memcpy((uint8_t*)hits + 0x1C, &g_ray.owner, 4); memcpy((uint8_t*)hits + 0x20, &g_ray.body, 4); }
    return (uint32_t)g_ray.hits;
}

int main(void)
{
    g_case = "the rule";
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 10.3f) == 10.0f - 10.3f); /* a miss under the ground: minus the depth */
    CHECK(CoXlTestTiltUnder(0.2f, 1, 10.0f, 10.3f) == 0.2f);          /* a hit stays the game's */
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 9.5f) == 4.0f);           /* a miss over the ground stays a miss */
    CHECK(CoXlTestTiltUnder(4.0f, 0, 10.0f, 10.0f) == 4.0f);          /* at the ground: the game's */

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
    CHECK(fabsf(d - (11.7f - 12.0f)) < 0.02f);
    CHECK(g_read.calls == 1 && g_read.request == 2 && g_read.coarse == 3); /* the game's call as made */
    float slot[2] = {0, 0}; /* the lean limit's place for this thread's unit: the ray's origin */
    CHECK(CoXlTestLeanSlot(slot) == 1 && slot[0] == 110.0f && slot[1] == -190.0f);
    Call(read, 0, 3, 4.0f); /* no ray: no place */
    CHECK(CoXlTestLeanSlot(slot) == 0);
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
    CHECK(fabsf(Call(read, 2, 3, 4.0f) - (11.0f - 12.0f)) < 0.02f);
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 1 && under == 1);

    g_case = "outside every tile's square: the game's";
    Ray(2, 100.0f + 1030.0f, -200.0f, -50.0f, 0); /* just past the hidden square's edge (1024.2 m) */
    CHECK(Passed(Call(read, 2, 3, 4.0f), 4.0f, 2, 3));
    CoXlTestTiltCounts(&reads, &under);
    CHECK(reads == 0 && under == 0);

    g_case = "no request: the game's call";
    CHECK(Passed(Call(read, 0, 3, 4.0f), 4.0f, 0, 3));

    g_case = "a prop's hit as the ground: at most the full length";
    CHECK(CoXlTestTiltPropRead(1.5f, 4.0f) == 1.5f && CoXlTestTiltPropRead(4.5f, 4.0f) == 4.0f && CoXlTestTiltPropRead(-0.4f, 4.0f) == -0.4f);
    CHECK(CoXlTestTiltPropRead(3.5f, 3.0f) == 3.0f);
    CHECK(CoXlTestTiltPropRead(-4.0f, 4.0f) == -4.0f && CoXlTestTiltPropRead(-5.0f, 4.0f) == -4.0f && CoXlTestTiltPropRead(-9.0f, 3.0f) == -3.0f); /* and at least minus it */

    g_case = "a four-corner hit on a prop reads the drawn ground";
    const uint32_t rock = 0x1188B, rockBody = 0x801122A3u, level = 0x80300067u;
    long pg, pk; float t = 0;
    CoXlTestFgWorld(8.0f);
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.5f, 0.0f); /* the ground rises 0.5 m a metre east: 14 at x 104 */
    Unit(7, 4, 4.0f); Unit(6, 3, 4.0f);
    CoXlTestTiltPropCounts(&pg, &pk);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    d = PropCall(read, 7, 0.3f); /* the rock 0.3 m under the ray's start, the ground 1.5 m */
    CHECK(fabsf(d - 1.5f) < 0.02f && PropMade(7));
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 1 && pk == 0);
    PropRay(104.0f, -200.0f, 15.5f, 0.6f, 0, -0.8f, rock, rockBody); /* leaning east, uphill: 15.5 - 0.8 t = 14 + 0.3 t */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f / 1.1f) < 0.05f);
    PropRay(104.0f, -200.0f, 15.5f, 1.2f, 0, -1.6f, rock, rockBody); /* the same direction, not a unit vector */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f / 1.1f) < 0.05f);
    PropRay(104.0f, -200.0f, 15.5f, -0.6f, 0, -0.8f, rock, rockBody); /* leaning west, downhill: 15.5 - 0.8 t = 14 - 0.3 t */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 3.0f) < 0.05f);
    PropRay(104.0f, -196.0f, 15.5f, 0, 0.6f, -0.8f, rock, rockBody); /* leaning north, along the slope: 1.5 / 0.8 */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.875f) < 0.02f);
    PropRay(104.0f, -200.0f, 20.0f, 0, 0, -1.0f, rock, rockBody); /* the ground 6 m under: a hole */
    CHECK(PropCall(read, 7, 0.3f) == 4.0f);
    Unit(7, 4, 7.0f);
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 6.0f) < 0.02f); /* within this unit's own length */
    Unit(7, 4, 4.0f);
    PropRay(104.0f, -200.0f, 13.6f, 0, 0, -1.0f, rock, rockBody); /* started under the drawn ground: minus the depth */
    CHECK(fabsf(PropCall(read, 7, -0.2f) + 0.4f) < 0.02f);
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.0f, 0.5f); /* rising north instead: 14 at y -196 */
    PropRay(100.0f, -196.0f, 15.5f, 0, 0.6f, -0.8f, rock, rockBody);
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f / 1.1f) < 0.05f);
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.5f, 0.0f);
    PropRay(104.0f, -200.0f, 8.0f, 0, 0, -1.0f, rock, rockBody); /* 6 m under the drawn ground: minus the full length */
    CHECK(PropCall(read, 7, -0.2f) == -4.0f);
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 9 && pk == 0);

    g_case = "a slanted ray on ground steeper than it: the game's unless the length settles";
    const long costBefore = CoXlTestTiltCostCalls();
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 1.0f, 0.0f); /* 45 deg, rising east: 16 at x 104 */
    PropRay(104.0f, -200.0f, 17.5f, 0.342f, 0, -0.940f, rock, rockBody); /* 20 deg uphill: 17.5 - 0.94 t = 16 + 0.342 t */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f / 1.282f) < 0.05f);
    PropRay(104.0f, -200.0f, 17.5f, 0.7071f, 0, -0.7071f, rock, rockBody); /* 45 deg uphill: the steps swing between 0 and 2.12 */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 17.5f, 0.819f, 0, -0.574f, rock, rockBody); /* 55 deg uphill: the steps grow */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 17.5f, -0.819f, 0, -0.574f, rock, rockBody); /* 55 deg downhill: the ray never meets the ground */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 1 && pk == 3);
    CHECK(CoXlTestTiltCostCalls() == costBefore + 4); /* research: each hit asked about is timed */
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.5f, 0.0f);

    g_case = "a hit on the ground, another mode, another level, the switch off: the game's";
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, 0, 0x80300099u); /* no owner: a tile's ground */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f && PropMade(7));
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, level); /* the level terrain's body */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    CHECK(PropCall(read, 6, 0.3f) == 0.3f && PropMade(6)); /* a bone-ray unit */
    Unit(7, 2, 4.0f);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    Unit(7, 4, 4.0f);
    CoXlTestFgWorld(1.0f);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f); /* not a complex level */
    CoXlTestFgWorld(8.0f);
    CoXlTestTiltProps(0);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    CoXlTestTiltProps(1);
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f) < 0.02f);
    memcpy(g_rays[1] + 0x30, &(uint32_t){0}, 4); /* a miss with a prop still named in its hit record: a miss */
    CHECK(PropCall(read, 7, 4.0f) == 4.0f);
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 1 && pk == 0);

    g_case = "a prop's hit with no drawn ground to read: the game's, counted";
    PropRay(104.0f, -200.0f, 15.5f, 0.9f, 0, -0.43f, rock, rockBody); /* over 60 deg from straight down */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, 0, rock, rockBody); /* no direction */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, 1.0f, rock, rockBody); /* upward */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(100.0f + 40.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody); /* past the tile's heights */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(131.5f, -200.0f, 30.0f, 0.6f, 0, -0.8f, rock, rockBody); /* starts over the heights and leaves them */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    CoXlTestTiltHold(1);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f); /* a writer holds the tiles */
    CoXlTestTiltHold(0);
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 0 && pk == 6);
    const float o1[3] = {104.0f, -200.0f, 15.5f}, down[3] = {0, 0, -2.0f}, flat[3] = {0.87f, 0, -0.49f};
    CHECK(CoXlTestTiltGroundAlong(o1, down, &t) && fabsf(t - 1.5f) < 0.02f);
    CHECK(!CoXlTestTiltGroundAlong(o1, flat, &t)); /* 60.6 deg from straight down */

    g_case = "the 30 s line";
    CoXlTestTiltNote();
    CHECK(!fake_host_logged("four-corner hits on a prop")); /* none: no line */
    PropCall(read, 7, 0.3f); PropCall(read, 7, 0.3f);
    PropRay(140.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    PropCall(read, 7, 0.3f);
    CoXlTestTiltNote();
    CHECK(fake_host_logged("tilt rays (30 s): 2 four-corner hits on a prop read the drawn ground, 1 stayed the game's with no drawn ground there"));
    CoXlTestTiltTile(0, 0, NAN);

    g_case = "research: a prop under a unit";
    CHECK(CoXlTestTiltIsProp(0x1188B, 0x801122A3u, 0x80300067u) == 1);
    CHECK(CoXlTestTiltIsProp(0, 0x80300067u, 0x80300067u) == 0);     /* the level terrain */
    CHECK(CoXlTestTiltIsProp(0, 0x80300099u, 0x80300067u) == 0);     /* a body with no owner: a tile's ground */
    CHECK(CoXlTestTiltIsProp(0x1188B, 0x80300067u, 0x80300067u) == 0);
    static uintptr_t phys[0x70 / 8]; static uint8_t settings[0xE0], mgr[0x20];
    const uint32_t type = 0x80000014u, terrain = 0x80300067u; const uintptr_t sp = (uintptr_t)settings, pp = (uintptr_t)phys;
    const float at[3] = {10.0f, -20.0f, 30.0f};
    phys[0x68 / 8] = (uintptr_t)FakeRay;
    memcpy(g_fake.image + 0x3326328u, &pp, 8);
    memcpy(mgr + 8, &sp, 8); memcpy(mgr + 0x18, &terrain, 4); memcpy(settings + 0xD4, &type, 4);
    float hz = 0;
    g_ray.hits = 1; g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.z = 31.25f;
    CHECK(CoXlTestTiltOnProp((uintptr_t)g_fake.image, (uintptr_t)mgr, at, &hz) == 1 && hz == 31.25f);
    CHECK(g_ray.calls == 1 && g_ray.type == type && g_ray.filter == 0 && g_ray.max == 8 && g_ray.length == 12.0f);
    CHECK(g_ray.origin[0] == 10.0f && g_ray.origin[1] == -20.0f && g_ray.origin[2] == 34.0f && g_ray.dir[0] == 0 && g_ray.dir[1] == 0 && g_ray.dir[2] == -1.0f);
    g_ray.owner = 0; g_ray.body = terrain;
    CHECK(CoXlTestTiltOnProp((uintptr_t)g_fake.image, (uintptr_t)mgr, at, &hz) == 0);
    g_ray.hits = 0;
    CHECK(CoXlTestTiltOnProp((uintptr_t)g_fake.image, (uintptr_t)mgr, at, &hz) == -1);

    g_case = "research: the share on a prop";
    static uint8_t R[0x298]; const float q0[4] = {0, 0, 0, 1}, corners[4][2] = {{0.5f, 0.7f}, {0.5f, -0.7f}, {-0.5f, -0.7f}, {-0.5f, 0.7f}};
    const uintptr_t Rp = (uintptr_t)R, mp = (uintptr_t)mgr;
    for (int k = 0; k < 4; k++) memcpy(R + 0x58 + 0x18 * k + 8, corners[k], 8);
    CoXlTestTiltPropNote();
    CHECK(!fake_host_logged("tilt props (30 s)")); /* no sample: no line */
    CoXlTestTiltSunkLines(16); /* no unit line here */
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 40.0f); /* no hit: not a sample */
    g_ray.hits = 1; g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.z = 30.0f;
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 40.0f);
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 29.0f);
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 30.0f);
    g_ray.owner = 0; g_ray.body = terrain;
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 31.0f);
    for (int k = 0; k < 3; k++) CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 2.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 7 four-corner samples with a hit under them, 3 on a prop (42.86 %); 30 deg or more from upright: 2 of those on a prop, 1 of the others; "
        "corners read for 3 of those on a prop: 0 with a corner over 0.5 m under the surface over it, 0 over 1.0 m, the deepest corner's median 0.00 m (of the first 3)"));
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 2.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 1 four-corner samples with a hit under them, 0 on a prop")); /* the counts start again */

    g_case = "research: a unit's corners under the surface over them";
    g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.seq = 1; g_ray.calls = 0;
    memcpy(g_ray.zs, (float[5]){31.25f, 30.2f, 30.6f, 31.2f, 29.5f}, sizeof g_ray.zs); /* under the unit, then its corners: +0.2 +0.6 +1.2 -0.5 */
    CoXlTestTiltSunkLines(15);
    CoXlTestTiltPropSample(mp, Rp, 0x701, q0, at, 12.0f);
    CHECK(g_ray.calls == 5 && fabsf(g_ray.at[1][0] - 10.5f) < 0.001f && fabsf(g_ray.at[1][1] + 19.3f) < 0.001f && fabsf(g_ray.at[3][0] - 9.5f) < 0.001f && fabsf(g_ray.at[3][1] + 20.7f) < 0.001f); /* at the corner offsets */
    CHECK(fake_host_logged("tilt props: sunk: unit 00000701 at (10.0, -20.0, 30.00): 12.0 deg from upright; corners under the surface over them (m, nan: none): "
        "+0.20 +0.60 +1.20 -0.50; the deepest under owner 0001188B, body 801122A3"));
    CoXlTestTiltPropSample(mp, Rp, 0x702, q0, at, 12.0f); /* the lines are used up */
    CHECK(!fake_host_logged("sunk: unit 00000702"));
    const float qz[4] = {0, 0, 0.70710678f, 0.70710678f}; /* a quarter turn about up: forward is north */
    memcpy(g_ray.zs, (float[5]){31.25f, 30.3f, 30.7f, 30.1f, 29.0f}, sizeof g_ray.zs); /* +0.3 +0.7 +0.1 -1.0: over 0.5, not over 1 */
    g_ray.calls = 0;
    CoXlTestTiltPropSample(mp, Rp, 0x703, qz, at, 12.0f);
    CHECK(fabsf(g_ray.at[1][0] - 9.3f) < 0.001f && fabsf(g_ray.at[1][1] + 19.5f) < 0.001f);
    memcpy(g_ray.zs, (float[5]){31.25f, 30.3f, 29.7f, 30.1f, 29.0f}, sizeof g_ray.zs); /* +0.3 at most */
    g_ray.calls = 0;
    CoXlTestTiltPropSample(mp, Rp, 0x704, q0, at, 12.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("4 on a prop (100.00 %); 30 deg or more from upright: 0 of those on a prop, 0 of the others; "
        "corners read for 4 of those on a prop: 3 with a corner over 0.5 m under the surface over it, 2 over 1.0 m, the deepest corner's median 1.20 m (of the first 4)"));
    g_ray.seq = 0;

    g_case = "research: the prop rule's cost";
    CoXlTestTiltCostNote(1000000, 5); /* clears what the hook's calls above left */
    CoXlTestTiltCost(100); CoXlTestTiltCost(300); CoXlTestTiltCost(200);
    CHECK(CoXlTestTiltCostCalls() == 3);
    CoXlTestTiltCostNote(1000000, 10);
    CHECK(fake_host_logged("tilt rays (30 s): the prop rule's check took 0.600 ms in 3 calls over 10 frames (0.0600 ms a frame), 300.0 us at worst"));
    CHECK(CoXlTestTiltCostCalls() == 0);
    CoXlTestTiltCostNote(1000000, 3);
    CHECK(!fake_host_logged("in 0 calls")); /* nothing asked: no line */
    CoXlTestTiltCost(50);
    CoXlTestTiltCostNote(1000000, 4);
    CHECK(fake_host_logged("took 0.050 ms in 1 calls over 4 frames (0.0125 ms a frame), 50.0 us at worst")); /* the worst starts again */

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tilt: all checks passed\n");
    return 0;
}
