/* The rotation's ray reader (game 0x620810) on extra large levels: a ray that starts under a live tile's ground reads
 * that ground above it (minus the depth) when it missed; hits, misses over the ground, other places and a busy tile
 * list keep the game's answer. A four-corner unit's hit on a prop reads the ray's length to the drawn ground; hits on
 * the ground, other modes, other levels and places with no drawn ground keep the game's. A unit whose feet are on a
 * tall prop keeps the game's reading, by height or by one balanced body. The game's call is always made as the game
 * made it.
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
void CoXlTestTiltTurnScan(void);
void CoXlTestTiltKind(uint32_t id, uint32_t mode);
void CoXlTestTiltFooting(int mode);
int CoXlTestTiltFootingNow(void);
void CoXlTestTiltNow(uint32_t ms);
void CoXlTestTiltFeetClear(void);
float CoXlTestTiltFootHigh(uint32_t i, float len, uint32_t now);
float CoXlTestTiltFootSame(uint32_t i, uint32_t now);
void CoXlTestTiltFootNote(uint32_t i, uint32_t frame, int kind, float h, float d, uint32_t body, const float* o, uint32_t now);
void CoXlTestTiltKept(long* by);
void CoXlTestTiltFootNoteOf(uint32_t i, uint32_t id, uint32_t frame, int kind, float h, const float* o, uint32_t now);
int CoXlTestTiltFootOnProp(uint32_t i, uint32_t now);
float CoXlTestTiltFootShare(float mean, float len, float* top);
void CoXlTestTiltScanCost(long long ticks);
void CoXlTestTiltScanCostNote(long long perSecond);
int CoXlTestTiltTallLines(void);
int CoXlTestLeanProp(void);
void CoXlTestTiltFooted(long* footed, long* part);
int CoXlTestTiltWhy(const float* o, const float* dir);
void CoXlTestLeanTile(float cx, float cy, float z, float gx, float gy);
void CoXlTestFgWorld(float live);
void CoXlTestTiltFootFace(uint32_t i, uint32_t frame, int kind, const float* n, const float* o, uint32_t now);
float CoXlTestTiltFootFlat(uint32_t i, uint32_t now);
int CoXlTestTiltTileCovers(float x, float y);
int CoXlTestTiltLevelHit(uintptr_t rays, uint32_t coarse, uint32_t terrain, const float* o, float* zn);
int CoXlTestTiltLevelAlong(const float* zn, const float* o, const float* dir, float* t);
float CoXlTestTiltLevelGround(const float* zn, const float* o, float x, float y);
long CoXlTestTiltLevelCount(void);
float CoXlTestLeanLevelNow(void);
int CoXlTestTiltInsideAt(uintptr_t game, uintptr_t mgr, const float* p, float* depth);
void CoXlTestTiltClient(void);

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
static uint8_t g_units[8][0x298], g_hit[0x2C], g_mgr[0x60], g_entity[0x10];
static uintptr_t g_inst[8];
/* The entity in slot i ([mgr+0x50] instances, +8 its id); 0: the slot does not read. */
static void Entity(int i, uint32_t id)
{
    const uintptr_t inst = (uintptr_t)g_inst;
    memcpy(g_mgr + 0x50, &inst, 8); memcpy(g_entity + 8, &id, 4);
    g_inst[i] = id ? (uintptr_t)g_entity : 0;
}
static void HitAt(float x, float y, float z) { const float at[3] = {x, y, z}; memcpy(g_hit, at, sizeof at); } /* the hit's place, hit +0 */
static void Face(float x, float y, float z) { const float n[3] = {x, y, z}; memcpy(g_hit + 0xC, n, sizeof n); } /* its normal, hit +0xC */
/* The game's second ray of a corner, record 3: a body ray (1 at +0x40) at the level terrain (+0x3C) from (x, y, 5000)
 * that met it at z on a face with this normal. */
static uint8_t g_levelHit[0x2C];
static void Level(float x, float y, float z, float nx, float ny, float nz)
{
    const uintptr_t hit = (uintptr_t)g_levelHit; const uint32_t terrain = 0x80300067u, one = 1; const float at[6] = {x, y, z, nx, ny, nz};
    memcpy(g_levelHit, at, sizeof at);
    Ray(3, x, y, 5000.0f, 1);
    memcpy(g_rays[2] + 8, &hit, 8); memcpy(g_rays[2] + 0x3C, &terrain, 4); memcpy(g_rays[3], &one, 4);
}
static void Frame(uint32_t frame) { memcpy(g_mgr, &frame, 4); } /* [mgr]: its low two bits are the corner read */
static void PropRay(float x, float y, float z, float dx, float dy, float dz, uint32_t owner, uint32_t body)
{
    const uintptr_t recs = (uintptr_t)g_units, hit = (uintptr_t)g_hit; const uint32_t terrain = 0x80300067u, ids[2] = {owner, body};
    const float dir[3] = {dx, dy, dz}, n = sqrtf(dx * dx + dy * dy + dz * dz);
    memcpy(g_mgr + 0x18, &terrain, 4); memcpy(g_mgr + 0x58, &recs, 8);
    if (n > 0) HitAt(x + dx / n * 0.3f, y + dy / n * 0.3f, z + dz / n * 0.3f); else HitAt(x, y, z); /* 0.3 m along the ray, as the answers below */
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
static struct { int calls, hits, seq; uint32_t type, filter, max, owner, body, owners[5]; float origin[3], dir[3], length, z, zs[5], at[5][2], n[3]; } g_ray;
/* A ray that asks for one hit is the research's test for a point inside a body: one body from bot to top (NAN: none)
 * answers it, a start inside it with bit 0 at hit +0x28; call number `miss` meets nothing. Counted apart. */
static struct { int calls, miss; uint32_t type, max; float top, bot, length, from[2][3]; } g_in = {0, 0, 0, 0, NAN, NAN, 0, {{0, 0, 0}, {0, 0, 0}}};
static uint32_t FakeRay(uint32_t type, const float* origin, const float* dir, float length, uint32_t filter, void* hits, uint32_t max)
{
    if (max == 1)
    {
        g_in.calls++; g_in.type = type; g_in.max = max; g_in.length = length; memcpy(g_in.from[(g_in.calls - 1) & 1], origin, 12);
        if (isnan(g_in.top) || !(origin[2] > g_in.bot) || g_in.calls == g_in.miss) return 0;
        const uint32_t inside = origin[2] < g_in.top; const float z = inside ? g_in.bot : g_in.top; /* a start inside has no place worth reading */
        memcpy((uint8_t*)hits + 8, &z, 4); memcpy((uint8_t*)hits + 0x28, &inside, 4);
        return 1;
    }
    g_ray.calls++; g_ray.type = type; g_ray.length = length; g_ray.filter = filter; g_ray.max = max;
    memcpy(g_ray.origin, origin, 12); memcpy(g_ray.dir, dir, 12);
    if (g_ray.seq) { g_ray.z = g_ray.zs[(g_ray.calls - 1) % 5]; g_ray.at[(g_ray.calls - 1) % 5][0] = origin[0]; g_ray.at[(g_ray.calls - 1) % 5][1] = origin[1]; }
    if (g_ray.seq == 2) g_ray.owner = g_ray.owners[(g_ray.calls - 1) % 5]; /* an owner a column: 0 is the ground, all ones no hit */
    if (g_ray.seq == 2 && g_ray.owner == 0xFFFFFFFFu) return 0;
    if (g_ray.hits) { memcpy((uint8_t*)hits + 8, &g_ray.z, 4); memcpy((uint8_t*)hits + 0xC, g_ray.n, 12); memcpy((uint8_t*)hits + 0x1C, &g_ray.owner, 4); memcpy((uint8_t*)hits + 0x20, &g_ray.body, 4); }
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
    CHECK(CoXlTestTiltFootingNow() == 2); /* the default: a footing on a tall prop is kept by height */
    CoXlTestTiltFooting(0); /* first the rule alone */
    CoXlTestTiltNow(1000);
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
    long why[3];
    CoXlTestTiltKept(why); /* by reason: no live tile, over 60 deg, not settled */
    CHECK(why[0] + why[2] == 3 && why[1] == 0 && why[2] >= 2);
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
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    HitAt(160.0f, -200.0f, 15.2f); /* the ray reads its ground, the hit's own place has none */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f);
    CoXlTestTiltKept(why);
    CHECK(why[0] == 4 && why[1] == 3 && why[2] == 0); /* no live tile (a held lock too), a ray over 60 deg or with no way down, none unsettled */
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 0 && pk == 7);
    CHECK(CoXlTestTiltWhy((const float[3]){104.0f, -200.0f, 15.5f}, (const float[3]){0.9f, 0, -0.43f}) == 2 && CoXlTestTiltWhy((const float[3]){140.0f, -200.0f, 15.5f}, (const float[3]){0, 0, -1.0f}) == 1);
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    const float o1[3] = {104.0f, -200.0f, 15.5f}, down[3] = {0, 0, -2.0f}, flat[3] = {0.87f, 0, -0.49f};
    CHECK(CoXlTestTiltGroundAlong(o1, down, &t) && fabsf(t - 1.5f) < 0.02f);
    CHECK(!CoXlTestTiltGroundAlong(o1, flat, &t)); /* 60.6 deg from straight down */
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.0f, 0.0f); /* level ground: only the ray's angle decides */
    const float o2[3] = {104.0f, -200.0f, 13.0f}, near60[3] = {0.85f, 0, -0.53f};
    CHECK(!CoXlTestTiltGroundAlong(o2, flat, &t));
    CHECK(CoXlTestTiltGroundAlong(o2, near60, &t) && fabsf(t - 1.0f / 0.5291f) < 0.02f); /* 58 deg: 1 m down along a ray of length 1.0017 */
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.5f, 0.0f);

    g_case = "the 30 s line";
    CoXlTestTiltNote();
    CHECK(!fake_host_logged("four-corner hits on a prop")); /* none: no line */
    PropCall(read, 7, 0.3f); PropCall(read, 7, 0.3f);
    PropRay(140.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    PropCall(read, 7, 0.3f);
    CoXlTestTiltNote();
    CHECK(fake_host_logged("tilt rays (30 s): 2 four-corner hits on a prop read the drawn ground, 0 kept the game's reading for a footing on the prop and 0 part of it (footing 0); "
        "1 stayed the game's: 1 with no live tile there, 0 on a ray over 60 deg from straight down, 0 where the length to the ground did not settle"));

    g_case = "footing by height: run 2's units";
    /* the corners' hits over the drawn ground (m), the game's readings, the bodies (0: the ground), as run 2 logged them */
    const float o0[3] = {104.0f, -200.0f, 15.5f};
#define FEET(i, h0, h1, h2, h3, d0, d1, d2, d3, b0, b1, b2, b3) do { const float h_[4] = {h0, h1, h2, h3}, d_[4] = {d0, d1, d2, d3}; const uint32_t b_[4] = {b0, b1, b2, b3}; \
    CoXlTestTiltFeetClear(); for (uint32_t k_ = 0; k_ < 4; k_++) CoXlTestTiltFootNote(i, 4 + k_, b_[k_] ? 2 : 1, h_[k_], d_[k_], b_[k_], o0, 1000); } while (0)
    FEET(3, 3.56f, 3.75f, 3.76f, 3.57f, 0.56f, 0.53f, 0.50f, 0.51f, 0xD7B6, 0xD7B6, 0xD7B6, 0xD7B6); /* 0000070B: 28 deg on one rock 3.6 m up, length 3 */
    CHECK(CoXlTestTiltFootHigh(3, 3.0f, 1000) == 1.0f && CoXlTestTiltFootSame(3, 1000) == 1.0f);
    FEET(3, 3.11f, 2.63f, 2.58f, 2.82f, 0.16f, 0.30f, 0.16f, 0.30f, 0x0932, 0x0967, 0x0967, 0x0932); /* 00000701: 8 deg on two rocks 2.6 to 3.1 m up, length 4 */
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.535f) < 0.01f && CoXlTestTiltFootSame(3, 1000) == 0.0f); /* half-way by height; two bodies */
    FEET(3, 2.21f, 0, 0, 2.23f, 1.49f, 1.69f, 1.49f, 1.41f, 0xD7E3, 0, 0, 0xD7E3); /* 00000710: 35 deg, two corners on a rock 2.2 m up, two on the ground */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f && CoXlTestTiltFootSame(3, 1000) == 0.0f);
    FEET(3, 1.18f, 2.02f, 3.13f, 0.75f, 0.62f, 0.82f, 0.23f, 1.08f, 0xD7B6, 0xD7B6, 0xD7B6, 0xD7B6); /* 0000070D: 36 deg on one rock's side */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f && CoXlTestTiltFootSame(3, 1000) == 0.0f); /* low on average; readings 0.85 m apart */
    FEET(3, 0.31f, 0.35f, 0, 0, 0.43f, 0.40f, 0.42f, 0.44f, 0x6AE2, 0x6AE2, 0, 0); /* 0000070C: half on a rock 0.3 m up, length 3 */
    CHECK(CoXlTestTiltFootHigh(3, 3.0f, 1000) == 0.0f && CoXlTestTiltFootSame(3, 1000) == 0.0f);

    g_case = "footing by height: no step anywhere";
    float was = 0;
    for (int k = 0; k <= 60; k++) /* mean heights 0 to 6 m in 0.1 m steps: never down, never a jump */
    {
        const float m = 0.1f * (float)k;
        FEET(3, m, m, m, m, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
        const float w = CoXlTestTiltFootHigh(3, 4.0f, 1000);
        CHECK(w >= was && w - was < 0.11f && (m > 2.0f || w == 0.0f) && (m < 3.5f || w == 1.0f));
        was = w;
    }
    FEET(3, 2.75f, 2.75f, 2.75f, 2.75f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.5f) < 0.001f); /* length 4: from 2 m to its reach, 3.5 m */
    FEET(3, 2.0f, 2.0f, 2.0f, 2.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 3.0f, 1000) - 0.5f) < 0.001f && CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f); /* length 3: from 1.5 m to 2.5 m */
    CHECK(CoXlTestTiltFootHigh(3, 0.4f, 1000) == 0.0f && CoXlTestTiltFootHigh(3, 0.5f, 1000) == 0.0f && CoXlTestTiltFootHigh(2000, 4.0f, 1000) == 0.0f); /* no reach, no slot: the rule alone */
    FEET(3, 6.0f, 6.0f, 6.0f, 6.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1400) == 1.0f && CoXlTestTiltFootHigh(3, 4.0f, 1501) == 0.0f); /* hits older than 0.5 s say nothing */
    CoXlTestTiltFootNote(3, 5, 3, 0, 4.0f, 0, o0, 1000); /* one corner over a hole, at a rock's edge: three of four count 3.5 m, 2.63 on average */
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.376f) < 0.002f);
    CoXlTestTiltFootNote(3, 6, 3, 0, 4.0f, 0, o0, 1000); CoXlTestTiltFootNote(3, 7, 3, 0, 4.0f, 0, o0, 1000); /* three over a hole: 1.5 m on average */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f);
    FEET(3, 3.0f, 3.0f, 3.0f, 3.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.741f) < 0.002f);
    CoXlTestTiltFootNote(3, 4, 0, 0, 0.5f, 7, o0, 1000); /* a corner with no drawn ground to measure from is left out, not counted as 0 */
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.741f) < 0.002f);
    FEET(3, 6.0f, 6.0f, 6.0f, 6.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    const float far0[3] = {104.0f + 10.5f, -200.0f, 15.5f}, far1[3] = {104.0f + 10.5f, -200.0f - 10.5f, 15.5f}, near1[3] = {104.0f + 10.5f, -200.0f - 20.0f, 15.5f};
    CoXlTestTiltFootNote(3, 4, 2, 0.2f, 0.5f, 9, far0, 1000); /* a ray 10.5 m east of the slot's last: another unit, its corners start again */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f);
    FEET(3, 6.0f, 6.0f, 6.0f, 6.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CoXlTestTiltFootNote(3, 4, 2, 6.0f, 0.5f, 9, far0, 1000); CoXlTestTiltFootNote(3, 5, 2, 6.0f, 0.5f, 9, far0, 1000);
    CoXlTestTiltFootNote(3, 6, 2, 0.2f, 0.5f, 9, far1, 1000); /* 10.5 m north of it: the same */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f);
    CoXlTestTiltFootNote(3, 7, 2, 5.0f, 0.5f, 9, near1, 1000); /* 9.5 m on: the same unit's far corner, its 0.2 m corner kept */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f); /* the same slot: 5.0 counts as the band's top, (0.2 + 3.5) / 2 = 1.85 m */
    CoXlTestTiltFootNote(3, 4, 2, 5.0f, 0.5f, 9, near1, 1000);
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.175f) < 0.002f); /* a second one: (0.2 + 3.5 + 3.5) / 3 = 2.4 m, so the 0.2 m corner was kept */
    CoXlTestTiltFootNote(3, 4, 2, 0.2f, 0.5f, 9, far0, 1000);
    CoXlTestTiltFootNote(3, 5, 2, -3.0f, 0.5f, 9, far0, 1000); /* a hit under the drawn ground counts 0, not less */
    CoXlTestTiltFootNote(3, 6, 2, 9.4f, 0.5f, 9, far0, 1000);
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f);  /* one corner over a pit counts 3.5, not 9.4: (0.2 + 0 + 3.5) / 3 = 1.23 m */
    CHECK(CoXlTestTiltFootHigh(3, 15.0f, 1000) == 0.0f); /* a long unit too: its corners count 3.5 at most, not its reach */
    CoXlTestTiltFootNote(3, 5, 2, 0.0f, 0.5f, 9, far0, 1000);
    CoXlTestTiltFootNote(3, 4, 2, 9.4f, 0.5f, 9, far0, 1000);
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.126f) < 0.002f); /* two of three over it: (3.5 + 0 + 3.5) / 3 = 2.33 m */

    FEET(3, -3.0f, 6.0f, 6.0f, 6.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7); /* a hit 3 m under the drawn ground beside three on a tall rock: 0, not -3 */
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.376f) < 0.002f);

    g_case = "footing by height: every ray length in the units' data";
    float top = 0;
    CHECK(CoXlTestTiltFootShare(9.0f, 2.0f, &top) == 0.0f && top == 1.5f);  /* length 2: reach 1.5, no band, the rule alone at any height */
    CHECK(CoXlTestTiltFootShare(1.2f, 2.0f, &top) == 0.0f && CoXlTestTiltFootShare(1.5f, 2.0f, &top) == 0.0f); /* not from 0.9 m, where low rocks are */
    CHECK(CoXlTestTiltFootShare(1.5f, 3.0f, &top) == 0.0f && top == 2.5f && fabsf(CoXlTestTiltFootShare(2.0f, 3.0f, &top) - 0.5f) < 0.001f && CoXlTestTiltFootShare(2.5f, 3.0f, &top) == 1.0f);
    CHECK(CoXlTestTiltFootShare(2.0f, 4.0f, &top) == 0.0f && top == 3.5f && fabsf(CoXlTestTiltFootShare(2.75f, 4.0f, &top) - 0.5f) < 0.001f && CoXlTestTiltFootShare(3.5f, 4.0f, &top) == 1.0f);
    CHECK(CoXlTestTiltFootShare(2.0f, 10.0f, &top) == 0.0f && top == 3.5f && fabsf(CoXlTestTiltFootShare(2.75f, 10.0f, &top) - 0.5f) < 0.001f && CoXlTestTiltFootShare(3.5f, 10.0f, &top) == 1.0f);
    CHECK(CoXlTestTiltFootShare(2.0f, 15.0f, &top) == 0.0f && top == 3.5f && fabsf(CoXlTestTiltFootShare(2.75f, 15.0f, &top) - 0.5f) < 0.001f && CoXlTestTiltFootShare(3.5f, 15.0f, &top) == 1.0f); /* not 2.0 to 14.5 */
    CHECK(CoXlTestTiltFootShare(1.9f, 3.6f, &top) > 0.0f && CoXlTestTiltFootShare(1.86f, 3.6f, &top) == 0.0f && fabsf(top - 3.1f) < 0.001f); /* between: 0.6 of the reach to the reach */
    CHECK(CoXlTestTiltFootShare(1.5f, 2.6f, &top) == 0.0f && fabsf(top - 2.1f) < 0.001f && CoXlTestTiltFootShare(1.6f, 2.6f, &top) > 0.0f); /* never under 1.5 m */
    FEET(3, 6.0f, 6.0f, 6.0f, 6.0f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7); /* wholly on a plateau 6 m up */
    CHECK(CoXlTestTiltFootHigh(3, 15.0f, 1000) == 1.0f && CoXlTestTiltFootHigh(3, 10.0f, 1000) == 1.0f && CoXlTestTiltFootHigh(3, 2.0f, 1000) == 0.0f);
    FEET(3, 0.9f, 0.9f, 0.9f, 0.9f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7); /* a short unit wholly on a 0.9 m rock: along the ground */
    CHECK(CoXlTestTiltFootHigh(3, 2.0f, 1000) == 0.0f);

    g_case = "footing: another entity in the slot starts again";
    CoXlTestTiltFeetClear();
    for (uint32_t k = 0; k < 4; k++) CoXlTestTiltFootNoteOf(3, 0x711, 4 + k, 2, 6.0f, o0, 1000);
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 1.0f && CoXlTestTiltFootOnProp(3, 1000) == 1);
    CoXlTestTiltFootNoteOf(3, 0x711, 4, 2, 6.0f, o0, 1000); /* the same entity: its other corners stay */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 1.0f);
    CoXlTestTiltFootNoteOf(3, 0x711, 5, 1, 0.0f, o0, 1000); /* one of them on the ground now: three of four, not the ground alone */
    CHECK(fabsf(CoXlTestTiltFootHigh(3, 4.0f, 1000) - 0.376f) < 0.002f);
    CoXlTestTiltFootNoteOf(3, 0x712, 4, 1, 0.0f, o0, 1000); /* another at the same place, on the ground: nothing of the first's */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f && CoXlTestTiltFootOnProp(3, 1000) == 0);
    CoXlTestTiltFootNoteOf(3, 0x712, 5, 2, 0.3f, o0, 1000);
    CHECK(CoXlTestTiltFootOnProp(3, 1000) == 1 && CoXlTestTiltFootOnProp(3, 1500) == 1 && CoXlTestTiltFootOnProp(3, 1501) == 0); /* a corner on a prop, while it is fresh */
    CHECK(CoXlTestTiltFootOnProp(4, 1000) == 0 && CoXlTestTiltFootOnProp(2000, 1000) == 0);
    CoXlTestTiltFootNoteOf(3, 0x712, 5, 3, 0.0f, o0, 1000); /* that corner over a hole now */
    CHECK(CoXlTestTiltFootOnProp(3, 1000) == 0);
    CoXlTestTiltFootNoteOf(3, 0x712, 5, 0, 0.0f, o0, 1000); /* or on a prop with no ground to measure from */
    CHECK(CoXlTestTiltFootOnProp(3, 1000) == 0);

    g_case = "footing by one body: balanced readings only";
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.50f, 0.60f, 0.74f, 0.55f, 7, 7, 7, 7); /* 0.24 m apart: lying along it */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 1.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.50f, 0.60f, 0.875f, 0.55f, 7, 7, 7, 7); /* 0.375 m apart: half */
    CHECK(fabsf(CoXlTestTiltFootSame(3, 1000) - 0.5f) < 0.001f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.50f, 0.60f, 1.0f, 0.55f, 7, 7, 7, 7); /* 0.5 m apart: none */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 0.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, -0.20f, 0.0f, -0.10f, 0.02f, 7, 7, 7, 7); /* readings that started inside the body (negative) */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 1.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, -0.20f, 0.0f, -0.10f, 0.25f, 7, 7, 7, 7); /* 0.45 m apart across 0 */
    CHECK(CoXlTestTiltFootSame(3, 1000) > 0.0f && CoXlTestTiltFootSame(3, 1000) < 0.2f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 8, 7); /* another body under one corner */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 0.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CHECK(CoXlTestTiltFootSame(3, 1000) == 1.0f && CoXlTestTiltFootSame(3, 1501) == 0.0f && CoXlTestTiltFootSame(2000, 1000) == 0.0f);
    CoXlTestTiltFootNote(3, 6, 3, 0, 4.0f, 0, o0, 1000); /* one corner over a hole */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 0.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 0); /* one corner on the ground */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 0.0f);
    FEET(3, 0.4f, 0.4f, 0.4f, 0.4f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CoXlTestTiltFootNote(3, 5, 0, 0, 0.5f, 7, o0, 1000); /* one corner not measured */
    CHECK(CoXlTestTiltFootSame(3, 1000) == 0.0f);

    g_case = "footing through the reader";
    long footed, part;
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.0f, 0.0f); /* level ground at 12 */
    CoXlTestTiltFeetClear(); CoXlTestTiltPropCounts(&pg, &pk); CoXlTestTiltFooted(&footed, &part);
    PropRay(104.0f, -200.0f, 18.2f, 0, 0, -1.0f, rock, rockBody); /* a roof 5.7 m up, the corner dipped 0.2 m into it: the ground 6.2 m down */
    HitAt(104.0f, -200.0f, 17.7f);
    CHECK(PropCall(read, 7, -0.2f) == 4.0f && PropMade(7)); /* the rule alone: a hole, the unit upright whatever the roof's slope */
    CoXlTestTiltFooting(2);
    CHECK(PropCall(read, 7, -0.2f) == -0.2f && PropMade(7)); /* by height: the game's reading, a started-inside one too */
    CoXlTestTiltFooting(1);
    CHECK(PropCall(read, 7, -0.2f) == 4.0f); /* one body: three corners not read yet */
    for (uint32_t k = 1; k < 4; k++) { Frame(k); PropCall(read, 7, -0.2f + 0.05f * (float)k); } /* the other corners, one a frame: -0.15 -0.10 -0.05 */
    Frame(4);
    CHECK(PropCall(read, 7, -0.2f) == -0.2f); /* all four on the body, 0.15 m apart */
    Frame(5);
    CHECK(PropCall(read, 7, 0.3f) == 4.0f); /* the second corner 0.5 m from the first: the ground's again... */
    Frame(9);
    CHECK(fabsf(PropCall(read, 7, 0.175f) - (4.0f + (0.175f - 4.0f) * 0.5f)) < 0.01f); /* ...and half-way at 0.375 m: no step between */
    CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(footed == 3 && part == 1 && pg == 5 && pk == 0);
    Frame(0); CoXlTestTiltFeetClear();
    for (uint32_t k = 0; k < 4; k++) /* one unit's two bodies, a corner a frame: not one footing */
    {
        Frame(k);
        PropRay(104.0f, -200.0f, 18.2f, 0, 0, -1.0f, rock, k == 2 ? rockBody + 1 : rockBody); HitAt(104.0f, -200.0f, 17.7f);
        CHECK(PropCall(read, 7, -0.2f) == 4.0f);
    }
    CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(footed == 0 && part == 0 && pg == 4 && pk == 0);
    Frame(0); CoXlTestTiltFeetClear(); CoXlTestTiltFooting(2);
    PropRay(104.0f, -200.0f, 15.25f, 0, 0, -1.0f, rock, rockBody); /* a rock top 2.75 m up, the corner 0.5 m over it */
    HitAt(104.0f, -200.0f, 14.75f);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.25f + (0.5f - 3.25f) * 0.5f)) < 0.01f); /* half-way from the ground's 3.25 to the rock's 0.5 */
    Entity(7, 0x7B0); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody); /* a rock top 3 m up: 0.74 of the way to the rock's reading, not 0.26 */
    HitAt(104.0f, -200.0f, 15.0f);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.5f + (0.5f - 3.5f) * 0.741f)) < 0.01f);
    Frame(1); Ray(2, 104.0f, -201.0f, 15.5f, 0); /* its next corner over a hole: the same entity, so the rock corner is still its own */
    CHECK(PropCall(read, 7, 4.0f) == 4.0f && CoXlTestLeanProp() == 1);
    Frame(0); Entity(7, 0); CoXlTestTiltFeetClear(); CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    PropRay(104.0f, -200.0f, 15.25f, 0, 0, -1.0f, rock, rockBody);
    HitAt(104.0f, -200.0f, 14.75f);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.25f + (0.5f - 3.25f) * 0.5f)) < 0.01f);
    Frame(1);
    PropRay(104.0f, -201.0f, 12.5f, 0, 0, -1.0f, 0, 0x80300099u); /* the next corner on the ground */
    HitAt(104.0f, -201.0f, 12.0f);
    CHECK(PropCall(read, 7, 0.5f) == 0.5f);
    Frame(4);
    PropRay(104.0f, -200.0f, 15.25f, 0, 0, -1.0f, rock, rockBody);
    HitAt(104.0f, -200.0f, 14.75f);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - 3.25f) < 0.01f); /* one corner on the rock, one on the ground: 1.4 m on average, the ground's reading */
    CHECK(CoXlTestLeanProp() == 1); /* the limit is told the unit has a corner on a prop */
    Entity(7, 0x7A1); Frame(5);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.25f + (0.5f - 3.25f) * 0.5f)) < 0.01f); /* another entity in the slot, same place: the ground corner is not its own */
    Frame(6);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.25f + (0.5f - 3.25f) * 0.5f)) < 0.01f); /* the same entity: its corners add up */
    Entity(7, 0x7A2); Frame(7);
    PropRay(104.0f, -201.0f, 12.5f, 0, 0, -1.0f, 0, 0x80300099u); HitAt(104.0f, -201.0f, 12.0f);
    CHECK(PropCall(read, 7, 0.5f) == 0.5f && CoXlTestLeanProp() == 0); /* a third, on the ground: no prop corner is its own */
    Frame(8); Ray(2, 104.0f, -200.0f, 15.25f, 0);
    Entity(7, 0x7A3);
    CHECK(PropCall(read, 7, 4.0f) == 4.0f && CoXlTestLeanProp() == 0); /* a miss by a fourth */
    Entity(7, 0);
    Frame(0); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 18.2f, 0, 0, -1.0f, rock, rockBody); /* the roof again, 5.7 m up */
    HitAt(104.0f, -200.0f, 17.7f);
    CHECK(PropCall(read, 7, 0.5f) == 0.5f);
    Frame(1);
    memcpy(g_rays[1] + 0x30, &(uint32_t){0}, 4); /* the next corner over the roof's edge: a miss */
    CHECK(PropCall(read, 7, 4.0f) == 4.0f);
    Frame(4);
    memcpy(g_rays[1] + 0x30, &(uint32_t){1}, 4);
    CHECK(PropCall(read, 7, 0.5f) == 4.0f); /* the roof counts 3.5 m, the hole 0: 1.75 m on average, the hole the ground under it reads */
    PropRay(104.0f, -200.0f, 18.2f, 0.9f, 0, -0.43f, rock, rockBody); /* the same corner on a ray that cannot be read: what it met before no longer counts */
    CHECK(PropCall(read, 7, 0.5f) == 0.5f && CoXlTestTiltFootHigh(7, 4.0f, 1000) == 0.0f && CoXlTestLeanProp() == 1); /* no height to measure, and still a corner on a prop */
    Unit(6, 3, 4.0f); Frame(8); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 18.2f, 0, 0, -1.0f, rock, rockBody);
    CHECK(PropCall(read, 6, 0.3f) == 0.3f && CoXlTestTiltFootHigh(6, 4.0f, 1000) == 0.0f); /* a bone-ray unit: the game's, nothing noted */
    CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(footed == 1 && part == 3 && pg == 2 && pk == 1);
    Frame(0); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 18.2f, 0, 0, -1.0f, rock, rockBody);
    HitAt(104.0f, -200.0f, 17.7f);
    PropCall(read, 7, 0.5f);
    CoXlTestTiltNote();
    CHECK(fake_host_logged("tilt rays (30 s): 0 four-corner hits on a prop read the drawn ground, 1 kept the game's reading for a footing on the prop and 0 part of it (footing 2); "
        "0 stayed the game's: 0 with no live tile there, 0 on a ray over 60 deg from straight down, 0 where the length to the ground did not settle"));
    CoXlTestTiltFooting(0); Frame(0);
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 1.0f, 0.0f); /* why hits stayed the game's, each reason its own count */
    PropRay(104.0f, -200.0f, 17.5f, 0.7071f, 0, -0.7071f, rock, rockBody);
    for (int k = 0; k < 3; k++) PropCall(read, 7, 0.3f);
    PropRay(104.0f, -200.0f, 15.5f, 0.9f, 0, -0.43f, rock, rockBody);
    for (int k = 0; k < 2; k++) PropCall(read, 7, 0.3f);
    PropRay(140.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    PropCall(read, 7, 0.3f);
    CoXlTestTiltNote();
    CHECK(fake_host_logged("(footing 0); 6 stayed the game's: 1 with no live tile there, 2 on a ray over 60 deg from straight down, 3 where the length to the ground did not settle"));
    CoXlTestTiltTile(0, 0, NAN);

    g_case = "a prop's hit that stayed the game's: a prop corner with no height";
    CoXlTestLeanTile(100.0f, -200.0f, 12.0f, 0.0f, 0.0f);
    CoXlTestTiltFooting(2); Frame(0); Entity(7, 0); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 15.5f, 0.9f, 0, -0.43f, rock, rockBody); /* a ray that cannot be read */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f && CoXlTestLeanProp() == 1); /* the limit holds it without the hidden ground's slope */
    CHECK(CoXlTestTiltFootOnProp(7, 1000) == 1 && CoXlTestTiltFootOnProp(7, 1500) == 1 && CoXlTestTiltFootOnProp(7, 1501) == 0);
    FEET(3, 3.5f, 3.5f, 3.5f, 3.5f, 0.5f, 0.5f, 0.5f, 0.5f, 7, 7, 7, 7);
    CoXlTestTiltFootNote(3, 7, 4, 3.5f, 0.5f, 7, o0, 1000); /* one corner's hit kept: three corners 3.5 m up, not four with one at 0 */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 1.0f && CoXlTestTiltFootOnProp(3, 1000) == 1);
    CoXlTestTiltFeetClear();
    CoXlTestTiltFootNote(3, 4, 4, 3.5f, 0.5f, 7, o0, 1000); /* kept hits alone: no height at all */
    CHECK(CoXlTestTiltFootHigh(3, 4.0f, 1000) == 0.0f && CoXlTestTiltFootOnProp(3, 1000) == 1 && CoXlTestTiltFootSame(3, 1000) == 0.0f);

    g_case = "footing by normals";
    const float RADN = 57.29578f, nUp[3] = {0, 0, 1}, n125[3] = {sinf(12.5f / RADN), 0, cosf(12.5f / RADN)}, n10[3] = {0, sinf(9.9f / RADN), cosf(9.9f / RADN)},
        n15[3] = {sinf(15.0f / RADN), 0, cosf(15.0f / RADN)}, n30[3] = {sinf(30.0f / RADN), 0, cosf(30.0f / RADN)}, n349[3] = {0, -sinf(34.9f / RADN), cosf(34.9f / RADN)},
        n375[3] = {sinf(37.5f / RADN), 0, cosf(37.5f / RADN)}, n40[3] = {sinf(40.0f / RADN), 0, cosf(40.0f / RADN)}, nShort[3] = {0, 0, 0.9f};
#define FACES(i, a, b, c, e) do { const float* f_[4] = {a, b, c, e}; CoXlTestTiltFeetClear(); for (uint32_t k_ = 0; k_ < 4; k_++) CoXlTestTiltFootFace(i, 4 + k_, 2, f_[k_], o0, 1000); } while (0)
    FACES(3, nUp, nUp, nUp, nUp); /* a floor */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 1.0f && CoXlTestTiltFootFlat(3, 1500) == 1.0f && CoXlTestTiltFootFlat(3, 1501) == 0.0f && CoXlTestTiltFootFlat(2000, 1000) == 0.0f);
    FACES(3, n30, n30, n30, n30); /* a ramp of 30 deg */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 1.0f);
    FACES(3, n349, n349, n349, n349);
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 1.0f);
    FACES(3, n375, n375, n375, n375); /* half-way to a boulder's side */
    CHECK(fabsf(CoXlTestTiltFootFlat(3, 1000) - 0.5f) < 0.01f);
    FACES(3, n40, n40, n40, n40);
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nUp, nUp, nUp, n10); /* faces 9.9 deg apart: one footing */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 1.0f);
    FACES(3, nUp, n125, nUp, nUp); /* 12.5 deg: half */
    CHECK(fabsf(CoXlTestTiltFootFlat(3, 1000) - 0.5f) < 0.01f);
    FACES(3, n15, nUp, nUp, nUp); /* 15 deg: a rock's broken top */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nUp, nUp, n15, nUp);
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, n30, nUp, nUp, n30); /* each near level, 30 deg apart */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nUp, nUp, nUp, nShort); /* not a normal */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nShort, nShort, nShort, nShort); /* four alike that are no normals */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    const float n38[3] = {sinf(38.0f / RADN), 0, cosf(38.0f / RADN)}, nP9[3] = {sinf(9.0f / RADN), 0, cosf(9.0f / RADN)}, nM9[3] = {-sinf(9.0f / RADN), 0, cosf(9.0f / RADN)};
    FACES(3, n38, n38, n38, n30); /* the steepest face counts, whichever corner it is under */
    CHECK(CoXlTestTiltFootFlat(3, 1000) > 0.2f && CoXlTestTiltFootFlat(3, 1000) < 0.5f);
    FACES(3, nUp, nP9, nUp, nM9); /* each 9 deg from the first, 18 from each other */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nUp, nUp, nUp, nUp);
    CoXlTestTiltFootFace(3, 6, 4, nUp, o0, 1000); /* a kept hit is a face too */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 1.0f);
    CoXlTestTiltFootFace(3, 6, 1, nUp, o0, 1000); /* one corner on the ground: the unit is half off it */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    FACES(3, nUp, nUp, nUp, nUp);
    CoXlTestTiltFootNote(3, 5, 3, 0, 4.0f, 0, o0, 1000); /* one over a hole */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);
    CoXlTestTiltFeetClear();
    for (uint32_t k = 0; k < 3; k++) CoXlTestTiltFootFace(3, k, 2, nUp, o0, 1000); /* three corners read */
    CHECK(CoXlTestTiltFootFlat(3, 1000) == 0.0f);

    g_case = "footing by normals through the reader";
    CoXlTestTiltFooting(3); CoXlTestTiltFeetClear(); CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    for (uint32_t k = 0; k < 4; k++) /* a slab 0.5 m up, too low to count by height: the game's reading once all four faces are known */
    {
        Frame(k);
        PropRay(104.0f, -200.0f, 13.0f, 0, 0, -1.0f, rock, rockBody); HitAt(104.0f, -200.0f, 12.5f); Face(0, 0, 1.0f);
        d = PropCall(read, 7, 0.5f);
        CHECK(k < 3 ? fabsf(d - 1.0f) < 0.02f : d == 0.5f);
    }
    Frame(4); CoXlTestTiltFooting(2);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - 1.0f) < 0.02f); /* by height alone: the ground's */
    CoXlTestTiltFooting(3);
    CHECK(PropCall(read, 7, 0.5f) == 0.5f);
    Frame(5); Face(sinf(40.0f / RADN), 0, cosf(40.0f / RADN)); /* the next corner on a side of 40 deg */
    CHECK(fabsf(PropCall(read, 7, 0.5f) - 1.0f) < 0.02f);
    CoXlTestTiltFooted(&footed, &part); CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(footed == 2 && part == 0 && pg == 5 && pk == 0);
    Frame(0); CoXlTestTiltFeetClear();
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody); HitAt(104.0f, -200.0f, 15.0f); Face(sinf(40.0f / RADN), 0, cosf(40.0f / RADN));
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.5f + (0.5f - 3.5f) * 0.741f)) < 0.01f); /* 3 m up on a steep face: by height, as footing 2 */
    Face(0, 0, 0); CoXlTestTiltFooting(0); CoXlTestTiltFeetClear();

    g_case = "which squares a live tile covers";
    CHECK(CoXlTestTiltTileCovers(104.0f, -200.0f) == 1 && CoXlTestTiltTileCovers(100.0f + 1024.0f, -200.0f - 1024.0f) == 1);
    CHECK(CoXlTestTiltTileCovers(100.0f + 1025.0f, -200.0f) == 0 && CoXlTestTiltTileCovers(100.0f, -200.0f - 1025.0f) == 0 && CoXlTestTiltTileCovers(100.0f - 1025.0f, -200.0f) == 0);
    CoXlTestTiltHold(1);
    CHECK(CoXlTestTiltTileCovers(104.0f, -200.0f) == -1 && CoXlTestTiltTileCovers(5000.0f, 5000.0f) == -1); /* a writer holds the tiles: not known */
    CoXlTestTiltHold(0);

    g_case = "the level terrain's hit under a corner";
    float zn[4]; const float offTile[3] = {5000.0f, 5000.0f, 21.5f}, slope[3] = {-0.4472136f, 0, 0.8944272f}; /* rising 0.5 m a metre east */
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 1 && zn[0] == 20.0f && zn[1] == slope[0] && zn[2] == 0 && zn[3] == slope[2]);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 0, level, offTile, zn) == 0); /* no second ray asked */
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays[3], 0, level, offTile, zn) == 0); /* whatever lies before the first record */
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 2, level, offTile, zn) == 0); /* another record */
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level + 1, offTile, zn) == 0); /* a ray at another body */
    CHECK(CoXlTestTiltLevelHit(0x10, 3, level, offTile, zn) == 0); /* no record to read */
    memcpy(g_rays[2] + 0x3C, &(uint32_t){0xFFFFFFFFu}, 4);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, 0xFFFFFFFFu, offTile, zn) == 0); /* no level terrain */
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    memcpy(g_rays[2] + 0x30, &(uint32_t){0}, 4);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* it met nothing */
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    memcpy(g_rays[3], &(uint32_t){0}, 4);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* a filter ray, not a body ray */
    Level(5000.02f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* not this corner's column */
    Level(5000.0f, 4999.98f, 20.0f, slope[0], slope[1], slope[2]);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0);
    Level(5000.005f, 4999.995f, 20.0f, slope[0], slope[1], slope[2]);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 1);
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    memset(g_rays[2] + 8, 0, 8);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* no hit to read */
    Level(5000.0f, 5000.0f, -500.0f, 0, 0, 1.0f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* the collision lowered under a tile */
    Level(5000.0f, 5000.0f, -498.0f, 0, 0, 1.0f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 1 && zn[0] == -498.0f);
    Level(5000.0f, 5000.0f, 20.0f, 0.9849f, 0, 0.1736f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* the lowered square's wall */
    Level(5000.0f, 5000.0f, 20.0f, 0.9682f, 0, 0.25f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 1);
    Level(5000.0f, 5000.0f, 20.0f, 0, 0, 0.9f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0); /* not a normal */
    Level(5000.0f, 5000.0f, 20.0f, 0, 0.3f, 1.0f);
    CHECK(CoXlTestTiltLevelHit((uintptr_t)g_rays, 3, level, offTile, zn) == 0);
    const float flatZn[4] = {20.0f, 0, 0, 1.0f}, slopeZn[4] = {20.0f, slope[0], slope[1], slope[2]}, wallZn[4] = {20.0f, -0.7f, 0, 0.714f};
    const float lean[3] = {0.6f, 0, -0.8f}, lean2[3] = {1.2f, 0, -1.6f}, none[3] = {0, 0, 0}, flatRay[3] = {0.9f, 0, -0.43f}, along[3] = {-0.8f, 0, -0.6f}, under0[3] = {5000.0f, 5000.0f, 19.0f};
    CHECK(CoXlTestTiltLevelAlong(flatZn, offTile, down, &t) && fabsf(t - 1.5f) < 0.001f);
    CHECK(CoXlTestTiltLevelAlong(slopeZn, offTile, down, &t) && fabsf(t - 1.5f) < 0.001f); /* straight down: the height over it */
    CHECK(CoXlTestTiltLevelAlong(slopeZn, offTile, lean, &t) && fabsf(t - 1.5f / 1.1f) < 0.001f); /* uphill: 21.5 - 0.8 t = 20 + 0.3 t */
    CHECK(CoXlTestTiltLevelAlong(slopeZn, offTile, lean2, &t) && fabsf(t - 1.5f / 1.1f) < 0.001f); /* not a unit vector */
    CHECK(CoXlTestTiltLevelAlong(flatZn, offTile, lean, &t) && fabsf(t - 1.5f / 0.8f) < 0.001f);
    CHECK(CoXlTestTiltLevelAlong(flatZn, under0, down, &t) && fabsf(t + 1.0f) < 0.001f); /* a start under it */
    CHECK(!CoXlTestTiltLevelAlong(flatZn, offTile, none, &t) && !CoXlTestTiltLevelAlong(flatZn, offTile, flatRay, &t)); /* no way down; over 60 deg from it */
    CHECK(!CoXlTestTiltLevelAlong(wallZn, offTile, along, &t)); /* a ray that runs along the face */
    CHECK(fabsf(CoXlTestTiltLevelGround(slopeZn, offTile, 5002.0f, 5000.0f) - 21.0f) < 0.001f && fabsf(CoXlTestTiltLevelGround(slopeZn, offTile, 5000.0f, 5003.0f) - 20.0f) < 0.001f);
    CHECK(fabsf(CoXlTestTiltLevelGround(slopeZn, offTile, 4999.0f, 5000.0f) - 19.5f) < 0.001f);
    const float northZn[4] = {20.0f, 0, -0.4472136f, 0.8944272f};
    CHECK(fabsf(CoXlTestTiltLevelGround(northZn, offTile, 5000.0f, 5002.0f) - 21.0f) < 0.001f);

    g_case = "off every tile a prop's hit reads the level terrain";
    CoXlTestTiltNote(); CoXlTestTiltPropCounts(&pg, &pk); CoXlTestTiltLevelCount();
    Level(5000.0f, 5000.0f, 20.0f, 0, 0, 1.0f);
    PropRay(5000.0f, 5000.0f, 21.5f, 0, 0, -1.0f, rock, rockBody); /* a rock 0.3 m under the corner, the terrain 1.5 m */
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f) < 0.001f && PropMade(7) && CoXlTestLeanLevelNow() == 0.0f);
    CoXlTestTiltNote();
    CHECK(fake_host_logged("tilt rays (30 s): 1 four-corner hits on a prop read the drawn ground, 0 kept the game's reading for a footing on the prop and 0 part of it (footing 0); "
        "0 stayed the game's: 0 with no live tile there, 0 on a ray over 60 deg from straight down, 0 where the length to the ground did not settle; "
        "of the hits answered 1 read the level's coarse terrain, where no detailed tile is live"));
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    PropRay(5000.0f, 5000.0f, 21.5f, 0.6f, 0, -0.8f, rock, rockBody);
    CHECK(fabsf(PropCall(read, 7, 0.3f) - 1.5f / 1.1f) < 0.001f && fabsf(CoXlTestLeanLevelNow() - 26.565f) < 0.01f); /* the limit is given the terrain's slope */
    PropRay(5000.0f, 5000.0f, 21.5f, 0.9f, 0, -0.43f, rock, rockBody);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f); /* a ray over 60 deg from straight down: the game's, as on a tile */
    CoXlTestTiltKept(why); CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 1 && pk == 1 && why[1] >= 1 && CoXlTestTiltLevelCount() == 1);
    PropRay(5000.0f, 5000.0f, 21.5f, 0, 0, -1.0f, rock, rockBody);
    memcpy(g_rays[2] + 0x30, &(uint32_t){0}, 4); /* the second ray met nothing */
    CHECK(PropCall(read, 7, 0.3f) == 0.3f && CoXlTestLeanLevelNow() == -1.0f && CoXlTestTiltLevelCount() == 0);
    Level(5000.0f, 5000.0f, 20.0f, 0, 0, 1.0f);
    CoXlTestTiltHold(1);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f && CoXlTestLeanLevelNow() == -1.0f); /* a writer holds the tiles: a tile may cover it */
    CoXlTestTiltHold(0);
    PropRay(140.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody); /* in a live tile's square past its heights: the terrain there is lowered */
    Level(140.0f, -200.0f, 14.0f, 0, 0, 1.0f);
    CHECK(PropCall(read, 7, 0.3f) == 0.3f && CoXlTestLeanLevelNow() == -1.0f && CoXlTestTiltLevelCount() == 0);
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    PropRay(5000.0f, 5000.0f, 20.5f, 0, 0, -1.0f, 0, level); /* a corner on the terrain itself: the game's, the slope told */
    CHECK(PropCall(read, 7, 0.5f) == 0.5f && fabsf(CoXlTestLeanLevelNow() - 26.565f) < 0.01f && CoXlTestTiltLevelCount() == 0 && CoXlTestLeanProp() == 0);
    Ray(2, 5000.0f, 5000.0f, 20.5f, 0); /* a corner that missed: the slope all the same */
    CHECK(PropCall(read, 7, 4.0f) == 4.0f && fabsf(CoXlTestLeanLevelNow() - 26.565f) < 0.01f);
    CoXlTestTiltFooting(2); CoXlTestTiltFeetClear(); CoXlTestTiltFooted(&footed, &part);
    Level(5000.0f, 5000.0f, 20.0f, slope[0], slope[1], slope[2]);
    PropRay(5002.0f, 5000.0f, 24.5f, 0, 0, -1.0f, rock, rockBody); /* a rock top 3 m over the terrain 2 m east of the column: its plane there */
    Level(5002.0f, 5000.0f, 21.0f, slope[0], slope[1], slope[2]);
    HitAt(5002.0f, 5000.0f, 24.0f);
    CHECK(fabsf(PropCall(read, 7, 0.5f) - (3.5f + (0.5f - 3.5f) * 0.741f)) < 0.01f); /* the footing by height, as on a tile */
    HitAt(5004.0f, 5000.0f, 24.0f); /* the hit 2 m up the slope from the column: 2 m over the plane there, none of the game's */
    CHECK(fabsf(PropCall(read, 7, 0.5f) - 3.5f) < 0.01f);
    CoXlTestFgWorld(1.0f);
    CHECK(PropCall(read, 7, 0.5f) == 0.5f); /* another level: the game's */
    CoXlTestFgWorld(8.0f);
    CoXlTestTiltFooting(0); CoXlTestTiltFeetClear(); memset(g_rays[2], 0, sizeof g_rays[2]); memset(g_rays[3], 0, 4);
    CoXlTestTiltNote(); CoXlTestTiltPropCounts(&pg, &pk); CoXlTestTiltLevelCount();

    g_case = "a client's 30 s lines";
    PropRay(104.0f, -200.0f, 15.5f, 0, 0, -1.0f, rock, rockBody);
    PropCall(read, 7, 0.3f);
    CoXlTestTiltClient();
    CHECK(fake_host_logged("living world: tilt: on a client: the corner and lean rules run here on the units this game poses; their counts follow, or none when it posed none"));
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 0); /* the counts went out with it */
    PropCall(read, 7, 0.3f);
    CoXlTestTiltClient(); /* within 30 s: no second line, the count waits */
    CoXlTestTiltPropCounts(&pg, &pk);
    CHECK(pg == 1);
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
    CHECK(g_ray.origin[0] == 10.0f && g_ray.origin[1] == -20.0f && g_ray.origin[2] == 31.0f && g_ray.dir[0] == 0 && g_ray.dir[1] == 0 && g_ray.dir[2] == -1.0f);
    g_ray.owner = 0; g_ray.body = terrain;
    CHECK(CoXlTestTiltOnProp((uintptr_t)g_fake.image, (uintptr_t)mgr, at, &hz) == 0);
    g_ray.hits = 0;
    CHECK(CoXlTestTiltOnProp((uintptr_t)g_fake.image, (uintptr_t)mgr, at, &hz) == -1);

    g_case = "research: the share on a prop";
    static uint8_t R[0x298], R2[0x298]; const float q0[4] = {0, 0, 0, 1}, corners[4][2] = {{0.5f, 0.7f}, {0.5f, -0.7f}, {-0.5f, -0.7f}, {-0.5f, 0.7f}};
    const uintptr_t Rp = (uintptr_t)R, mp = (uintptr_t)mgr;
    for (int k = 0; k < 4; k++) { memcpy(R + 0x58 + 0x18 * k + 8, corners[k], 8); memcpy(R2 + 0x58 + 0x18 * k + 8, corners[0], 8); }
    const float len4 = 4.0f; memcpy(R + 0x48, &len4, 4); memcpy(R2 + 0x48, &len4, 4); /* the band: 2.0 to 3.5 m */
    CoXlTestLeanTile(10.0f, -20.0f, 28.0f, 0.0f, 0.0f); /* the drawn ground under the unit: level at 27.99, the nearest height a tile holds */
    CoXlTestTiltPropNote();
    CHECK(!fake_host_logged("tilt props (30 s)")); /* no sample: no line */
    CoXlTestTiltSunkLines(16); /* no unit line here */
    g_ray.calls = 0;
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 40.0f); /* no hit under it or its corners: not a sample */
    CHECK(g_ray.calls == 5); /* the centre and the four corners, whatever the centre met */
    g_ray.hits = 1; g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.z = 30.0f;
    CoXlTestTiltPropSample(mp, Rp, 0x700, q0, at, 40.0f);
    CoXlTestTiltPropSample(mp, Rp, 0x701, q0, at, 29.0f);
    g_ray.seq = 2; g_ray.calls = 0; /* the centre on the ground, one corner on a rock: on a prop */
    for (int k = 0; k < 5; k++) { g_ray.zs[k] = 30.0f; g_ray.owners[k] = k == 1 ? 0x1188B : 0; }
    CoXlTestTiltPropSample(mp, Rp, 0x702, q0, at, 30.0f);
    g_ray.seq = 0; g_ray.owner = 0; g_ray.body = terrain;
    CoXlTestTiltPropSample(mp, Rp, 0x703, q0, at, 31.0f);
    for (int k = 0; k < 3; k++) CoXlTestTiltPropSample(mp, Rp, 0x704, q0, at, 2.0f);
    g_ray.seq = 2; g_ray.calls = 0; /* nothing under the centre, the ground under the corners: a sample, not on a prop */
    for (int k = 0; k < 5; k++) g_ray.owners[k] = k == 0 ? 0xFFFFFFFFu : 0;
    CoXlTestTiltPropSample(mp, Rp, 0x705, q0, at, 2.0f);
    g_ray.seq = 0; g_ray.owner = 0;
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 8 four-corner samples with a hit under them, 3 on a prop (37.50 %); 30 deg or more from upright: 2 of those on a prop, 1 of the others; "
        "on a prop two scans running 0: up turned 0.0 deg on average, 0.0 at most, 0 over 10 deg; 0 with corners that span no plane"));
    CHECK(fake_host_logged("tilt footing (30 s): footing with part of the game's reading, footed: 2 samples, 1 of them 30 deg or more from upright; off their footing 0.0 deg on average, 0 20 deg or more; "
        "the deepest corner 0.00 m under the surface on average, 0 over 0.5 m; the highest corner 0.00 m over it on average, 0 over 0.5 m"));
    CHECK(fake_host_logged("tilt footing (30 s): footing with none of the game's reading, not footed: 1 samples, 1 of them 30 deg or more from upright")); /* one corner on the rock: 0.5 m on average, as the rule counts it */
    CHECK(fake_host_logged("tilt props: tall: unit 00000700 at (10.0, -20.0, 30.00) stands on a prop 2.01 m over the drawn ground, share 0.00 of the game's reading"));
    CHECK(!fake_host_logged("tall: unit 00000702") && CoXlTestTiltTallLines() == 2);
    CoXlTestTiltPropSample(mp, Rp, 0x704, q0, at, 2.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 1 four-corner samples with a hit under them, 0 on a prop")); /* the counts start again */
    CHECK(!fake_host_logged("sunk: unit")); /* level on level rock: no unit line */
    g_ray.seq = 2; g_ray.calls = 0; g_ray.body = 0x801122A3u; /* the centre alone on a rock 2.5 m up, the corners on the ground beside it: on a prop */
    for (int k = 0; k < 5; k++) { g_ray.zs[k] = k == 0 ? 30.5f : 28.0f; g_ray.owners[k] = k == 0 ? 0x1188B : 0; }
    CoXlTestTiltPropSample(mp, Rp, 0x706, q0, at, 2.0f);
    g_ray.seq = 0; g_ray.owner = 0; g_ray.body = terrain;
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 1 four-corner samples with a hit under them, 1 on a prop (100.00 %)"));
    CHECK(fake_host_logged("tilt footing (30 s): footing with none of the game's reading, not footed: 1 samples, 0 of them")); /* a unit beside a tall rock does not stand on it */
    CHECK(!fake_host_logged(": 0 samples, 0 of them")); /* a height with no sample has no line */

    g_case = "research: a unit against its footing";
    g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.seq = 1; g_ray.calls = 0;
    memcpy(g_ray.zs, (float[5]){30.0f, 30.25f, 30.25f, 29.75f, 29.75f}, sizeof g_ray.zs); /* a face rising 0.5 m a metre east, 2 m up */
    CoXlTestTiltPropSample(mp, Rp, 0x710, q0, at, 5.0f);
    CHECK(g_ray.calls == 5 && fabsf(g_ray.at[1][0] - 10.5f) < 0.001f && fabsf(g_ray.at[1][1] + 19.3f) < 0.001f && fabsf(g_ray.at[3][0] - 9.5f) < 0.001f && fabsf(g_ray.at[3][1] + 20.7f) < 0.001f); /* at the corner offsets */
    memcpy(g_ray.zs, (float[5]){31.0f, 31.0f, 31.0f, 31.0f, 31.0f}, sizeof g_ray.zs); /* 3 m up, the unit 1 m in it */
    CoXlTestTiltPropSample(mp, Rp, 0x711, q0, at, 5.0f);
    CHECK(!fake_host_logged("sunk: unit")); /* the lines are used up */
    CHECK(!fake_host_logged("tall: unit 00000711")); /* 1 m in a rock is not standing on it */
    CoXlTestTiltSunkLines(15);
    memcpy(g_ray.zs, (float[5]){32.0f, 32.2f, 32.2f, 32.2f, 32.2f}, sizeof g_ray.zs); /* 4.2 m up, 2.2 m in it */
    CoXlTestTiltPropSample(mp, Rp, 0x712, q0, at, 5.0f);
    CHECK(fake_host_logged("tilt props: sunk: unit 00000712 (resource 0000000000000000) at (10.0, -20.0, 30.00): 5.0 deg from upright, 0.0 deg off its footing, footing 3.50 m over the drawn ground (share 1.00, not footed); "
        "corners under the surface over them (m, nan: none): +2.20 +2.20 +2.20 +2.20; the deepest under owner 0001188B (resource AB0000000001188B), body 801122A3"));
    memcpy(g_ray.zs, (float[5]){28.5f, 28.4f, 28.4f, 28.4f, 28.4f}, sizeof g_ray.zs); /* 0.4 m up, the unit 1.6 m over it */
    CoXlTestTiltPropSample(mp, Rp, 0x713, q0, at, 5.0f);
    CHECK(!fake_host_logged("sunk: unit 00000713")); /* the lines are used up */
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("footing with none of the game's reading, not footed: 1 samples, 0 of them 30 deg or more from upright; off their footing 0.0 deg on average, 0 20 deg or more; "
        "the deepest corner 0.00 m under the surface on average, 0 over 0.5 m; the highest corner 1.60 m over it on average, 1 over 0.5 m"));
    CHECK(fake_host_logged("footing with part of the game's reading, footed: 1 samples, 0 of them 30 deg or more from upright; off their footing 26.6 deg on average, 1 20 deg or more; "
        "the deepest corner 0.25 m under the surface on average, 0 over 0.5 m; the highest corner 0.25 m over it on average, 0 over 0.5 m"));
    CHECK(fake_host_logged("footing with part of the game's reading, not footed: 1 samples, 0 of them 30 deg or more from upright; off their footing 0.0 deg on average, 0 20 deg or more; "
        "the deepest corner 1.00 m under the surface on average, 1 over 0.5 m; the highest corner 0.00 m over it on average, 0 over 0.5 m"));
    CHECK(fake_host_logged("footing with all of the game's reading, not footed: 1 samples, 0 of them 30 deg or more from upright; off their footing 0.0 deg on average, 0 20 deg or more; "
        "the deepest corner 2.20 m under the surface on average, 1 over 0.5 m; the highest corner 0.00 m over it on average, 0 over 0.5 m"));
    CoXlTestTiltSunkLines(16);
    memcpy(g_ray.zs, (float[5]){30.75f, 30.75f, 30.75f, 30.75f, 30.75f}, sizeof g_ray.zs); /* 0.75 m in a rock 2.76 m up: still its footing */
    CoXlTestTiltPropSample(mp, Rp, 0x718, q0, at, 5.0f);
    memcpy(g_ray.zs, (float[5]){30.75f, 30.75f, 30.75f, 30.75f, 30.80f}, sizeof g_ray.zs); /* one corner deeper: three are enough */
    CoXlTestTiltPropSample(mp, Rp, 0x719, q0, at, 5.0f);
    memcpy(g_ray.zs, (float[5]){30.75f, 30.75f, 30.75f, 30.80f, 30.80f}, sizeof g_ray.zs); /* two: not footed */
    CoXlTestTiltPropSample(mp, Rp, 0x71A, q0, at, 5.0f);
    g_ray.seq = 2; g_ray.calls = 0; /* three corners on the rock, one on the ground beside it: footed, 2.07 m on average */
    memcpy(g_ray.zs, (float[5]){30.75f, 30.75f, 30.75f, 30.75f, 28.0f}, sizeof g_ray.zs);
    for (int k = 0; k < 5; k++) g_ray.owners[k] = k == 4 ? 0 : 0x1188B;
    CoXlTestTiltPropSample(mp, Rp, 0x71B, q0, at, 5.0f);
    g_ray.owners[4] = 0xFFFFFFFFu; g_ray.calls = 0; /* that corner over a hole: the same, a hole counts 0 */
    CoXlTestTiltPropSample(mp, Rp, 0x71C, q0, at, 5.0f);
    g_ray.seq = 1; g_ray.owner = 0x1188B; g_ray.calls = 0;
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("footing with part of the game's reading, footed: 4 samples") && fake_host_logged("footing with part of the game's reading, not footed: 1 samples"));
    CHECK(fake_host_logged("tall: unit 00000718") && fake_host_logged("tall: unit 00000719") && !fake_host_logged("tall: unit 0000071A"));
    CHECK(fake_host_logged("tall: unit 0000071B at (10.0, -20.0, 30.00) stands on a prop 2.07 m over the drawn ground") && fake_host_logged("tall: unit 0000071C at (10.0, -20.0, 30.00) stands on a prop 2.07 m"));
    CoXlTestTiltSunkLines(15);
    memcpy(g_ray.zs, (float[5]){28.5f, 28.4f, 28.4f, 28.4f, 28.4f}, sizeof g_ray.zs);
    const float qz[4] = {0, 0, 0.70710678f, 0.70710678f}; /* a quarter turn about up: forward is north */
    g_ray.calls = 0;
    CoXlTestTiltPropSample(mp, Rp, 0x714, qz, at, 5.0f);
    CHECK(fabsf(g_ray.at[1][0] - 9.3f) < 0.001f && fabsf(g_ray.at[1][1] + 19.5f) < 0.001f);
    CoXlTestTiltSunkLines(14);
    memcpy(g_ray.zs, (float[5]){30.0f, 30.4f, 30.4f, 29.6f, 29.6f}, sizeof g_ray.zs); /* a face of 38.7 deg under a level unit */
    CoXlTestTiltPropSample(mp, Rp, 0x715, q0, at, 5.0f);
    CHECK(fake_host_logged("sunk: unit 00000715 (resource 0000000000000000) at (10.0, -20.0, 30.00): 5.0 deg from upright, 38.7 deg off its footing, footing 2.01 m over the drawn ground")); /* 30 deg off its footing makes a line too */
    memcpy(g_ray.zs, (float[5]){28.5f, 28.4f, 28.4f, 28.4f, 28.4f}, sizeof g_ray.zs);
    CoXlTestTiltPropSample(mp, Rp, 0x716, q0, at, 5.0f);
    CHECK(fake_host_logged("sunk: unit 00000716 (resource 0000000000000000) at (10.0, -20.0, 30.00): 5.0 deg from upright, 0.0 deg off its footing, footing 0.41 m over the drawn ground (share 0.00, not footed); "
        "corners under the surface over them (m, nan: none): -1.60 -1.60 -1.60 -1.60")); /* and so does a corner over 1 m in the air */
    CoXlTestTiltSunkLines(15);
    memcpy(g_ray.zs, (float[5]){30.6f, 30.6f, 30.6f, 30.6f, 30.6f}, sizeof g_ray.zs);
    CoXlTestTiltPropSample(mp, Rp, 0x71D, q0, at, 5.0f);
    CHECK(fake_host_logged("sunk: unit 0000071D") && fake_host_logged("(share 0.36, footed); corners under the surface over them (m, nan: none): +0.60")); /* and a corner over 0.5 m under the surface */
    CoXlTestTiltSunkLines(15);
    memcpy(g_ray.zs, (float[5]){30.5f, 30.5f, 30.5f, 30.5f, 30.5f}, sizeof g_ray.zs);
    CoXlTestTiltPropSample(mp, Rp, 0x71E, q0, at, 5.0f);
    CHECK(!fake_host_logged("sunk: unit 0000071E")); /* 0.5 m is not over it */
    for (int k = 0; k < 20; k++) CoXlTestTiltPropSample(mp, Rp, 0x730 + (uint32_t)k, q0, at, 5.0f);
    CHECK(CoXlTestTiltTallLines() == 16 && fake_host_logged("tall: unit 0000073E") && !fake_host_logged("tall: unit 0000073F")); /* 16 places a level are enough */
    memcpy(g_ray.zs, (float[5]){28.5f, 28.4f, 28.4f, 28.4f, 28.4f}, sizeof g_ray.zs);
    CoXlTestTiltPropNote();
    const float qx[4] = {0.17364818f, 0, 0, 0.98480775f}; /* 20 deg about the unit's x: its north side up */
    memcpy(g_ray.zs, (float[5]){30.0f, 30.2394f, 29.7606f, 29.7606f, 30.2394f}, sizeof g_ray.zs); /* a face rising north by the same 20 deg */
    CoXlTestTiltPropSample(mp, Rp, 0x717, qx, at, 20.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("footing with part of the game's reading, footed: 1 samples, 0 of them 30 deg or more from upright; off their footing 0.0 deg on average, 0 20 deg or more")); /* it lies along its footing */
    g_ray.seq = 0;

    static uint8_t R3[0x298]; const float len3 = 3.0f, low[3] = {10.0f, -20.0f, 28.5f};
    memcpy(R3, R, sizeof R3); memcpy(R3 + 0x48, &len3, 4);
    g_ray.seq = 1; g_ray.calls = 0; CoXlTestTiltSunkLines(16);
    memcpy(g_ray.zs, (float[5]){30.6f, 30.6f, 30.6f, 30.6f, 30.6f}, sizeof g_ray.zs); /* 2.61 m up: all of the reading for a length of 3, part for 4 */
    CoXlTestTiltPropSample(mp, (uintptr_t)R3, 0x740, q0, at, 5.0f);
    memcpy(g_ray.zs, (float[5]){28.5f, 28.5f, 28.5f, 28.5f, 28.5f}, sizeof g_ray.zs); /* footed on a rock 0.5 m up: no place to stand a Helldiver by */
    CoXlTestTiltPropSample(mp, Rp, 0x741, q0, low, 5.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("footing with all of the game's reading, footed: 1 samples") && fake_host_logged("footing with none of the game's reading, footed: 1 samples"));
    CHECK(fake_host_logged("tall: unit 00000740") && !fake_host_logged("tall: unit 00000741"));
    g_ray.seq = 0;

    g_case = "research: the turn of units on a prop";
    g_ray.z = 30.0f;
    CoXlTestTiltSunkLines(16);
    CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x720, q0, at, 0.0f);
    CoXlTestTiltPropSample(mp, Rp, 0x721, q0, at, 0.0f);
    CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x720, qx, at, 20.0f); /* the scan after: 20 deg */
    CoXlTestTiltTurnScan(); CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x720, q0, at, 0.0f); /* a scan missed: no pair */
    CoXlTestTiltPropSample(mp, (uintptr_t)R2, 0x722, q0, at, 0.0f); /* four corners at one place */
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt props (30 s): 5 four-corner samples with a hit under them, 5 on a prop (100.00 %); 30 deg or more from upright: 0 of those on a prop, 0 of the others; "
        "on a prop two scans running 1: up turned 20.0 deg on average, 20.0 at most, 1 over 10 deg; 1 with corners that span no plane"));

    g_case = "research: a corner inside a body";
    float depth = -1.0f;
    g_in.calls = 0;
    CHECK(CoXlTestTiltInsideAt((uintptr_t)g_fake.image, mp, at, &depth) == 0 && g_in.calls == 1); /* nothing there */
    g_in.calls = 0; g_in.top = 30.8f; g_in.bot = 28.0f; /* a body from 28 to 30.8 m: the point at 30 is 0.8 m under its top */
    CHECK(CoXlTestTiltInsideAt((uintptr_t)g_fake.image, mp, at, &depth) == 1 && fabsf(depth - 0.8f) < 0.001f && g_in.calls == 2);
    CHECK(g_in.max == 1 && g_in.type == type && g_in.length == 12.0f && g_in.from[0][2] == 30.0f && g_in.from[1][2] == 34.0f && g_in.from[1][0] == 10.0f && g_in.from[1][1] == -20.0f);
    g_in.top = 40.0f; /* its top over the second column's start too: as deep as that column starts */
    CHECK(CoXlTestTiltInsideAt((uintptr_t)g_fake.image, mp, at, &depth) == 1 && depth == 4.0f);
    g_in.top = 29.5f; g_in.calls = 0; /* the point over the body: a hit under it, not inside */
    CHECK(CoXlTestTiltInsideAt((uintptr_t)g_fake.image, mp, at, &depth) == 0 && g_in.calls == 1);
    g_in.top = 30.8f; g_in.calls = 0; g_in.miss = 2; /* the second column meets nothing: inside, no depth known */
    CHECK(CoXlTestTiltInsideAt((uintptr_t)g_fake.image, mp, at, &depth) == 1 && depth == 0.0f);
    g_in.miss = 0;
    CoXlTestTiltPropNote(); CoXlTestTiltSunkLines(0);
    g_ray.seq = 0; g_ray.hits = 1; g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.z = 30.0f; g_ray.calls = 0;
    g_ray.n[2] = 1.0f;
    CoXlTestTiltPropSample(mp, Rp, 0x750, q0, at, 2.0f); /* on a prop, a corner 0.8 m inside one */
    CHECK(g_ray.calls == 5);
    CHECK(fake_host_logged("sunk: unit 00000750") && fake_host_logged("faces under the corners (normals, nan: none): (0.00 0.00 1.00) (0.00 0.00 1.00) (0.00 0.00 1.00) (0.00 0.00 1.00), by normals a share of 1.00; a corner inside a body by 0.80 m (-1: none)"));
    g_in.top = 31.2f;
    CoXlTestTiltPropSample(mp, Rp, 0x751, q0, at, 2.0f); /* 1.2 m */
    g_in.top = 30.5f;
    CoXlTestTiltPropSample(mp, Rp, 0x752, q0, at, 2.0f); /* 0.5 m: inside, not over 0.5 */
    CHECK(!fake_host_logged("sunk: unit 00000752"));
    g_ray.owner = 0; g_ray.body = terrain; g_in.top = 30.6f;
    CoXlTestTiltPropSample(mp, Rp, 0x753, q0, at, 2.0f); /* not on a prop, 0.6 m inside a body */
    g_in.top = NAN;
    CoXlTestTiltPropSample(mp, Rp, 0x754, q0, at, 2.0f); /* in nothing */
    g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u; g_ray.n[2] = 0.0f; g_ray.n[0] = 1.0f; g_ray.z = 28.0f;
    CoXlTestTiltPropSample(mp, Rp, 0x755, q0, at, 2.0f); /* 2 m over what is under it, in nothing, on faces that are no footing */
    CHECK(fake_host_logged("sunk: unit 00000755") && fake_host_logged("(1.00 0.00 0.00), by normals a share of 0.00; a corner inside a body by -1.00 m (-1: none)"));
    g_ray.n[0] = 0.0f; g_ray.n[2] = 1.0f; g_ray.seq = 2;
    for (int k = 0; k < 5; k++) { g_ray.zs[k] = 28.0f; g_ray.owners[k] = k == 4 ? 0 : 0x1188B; }
    CoXlTestTiltPropSample(mp, Rp, 0x756, q0, at, 2.0f); /* level faces, one corner on the ground: no footing by normals */
    CHECK(fake_host_logged("sunk: unit 00000756") && fake_host_logged("(0.00 0.00 1.00), by normals a share of 0.00; a corner inside a body by -1.00 m (-1: none)"));
    g_ray.seq = 0; g_ray.n[2] = 0.0f; g_ray.z = 30.0f;
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("living world: tilt inside (30 s): of 7 four-corner samples 4 with a corner inside a body, 3 over 0.5 m under its top, 1 over 1.0 m; of the 5 on a prop: 3, 2, 1"));
    CoXlTestTiltPropNote();
    CHECK(!fake_host_logged("tilt inside (30 s): of 0")); /* no sample: no line */
    CoXlTestTiltPropSample(mp, Rp, 0x754, q0, at, 2.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("tilt inside (30 s): of 1 four-corner samples 0 with a corner inside a body, 0 over 0.5 m under its top, 0 over 1.0 m; of the 1 on a prop: 0, 0, 0")); /* the counts start again */

    g_case = "research: the turn of units whose lean limit changed";
    CoXlTestTiltSunkLines(16); CoXlTestTiltFeetClear();
    g_ray.owner = 0; g_ray.body = terrain;
    CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x760, q0, at, 0.0f); CoXlTestTiltPropSample(mp, Rp, 0x761, q0, at, 0.0f); CoXlTestTiltPropSample(mp, Rp, 0x762, q0, at, 0.0f);
    CoXlTestTiltFootNote(5, 4, 2, 1.0f, 0.5f, 7, at, 1000); /* the slot's unit has a corner on a prop now */
    CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x760, qx, at, 20.0f); /* the limit lost its slope: 20 deg since */
    CoXlTestTiltPropSample(mp, Rp, 0x761, q0, at, 0.0f); /* changed, not turned */
    CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x760, q0, at, 0.0f); /* turned back, the limit the same: not counted */
    CoXlTestTiltTurnScan(); CoXlTestTiltTurnScan(); CoXlTestTiltPropSample(mp, Rp, 0x762, qx, at, 20.0f); /* changed over a scan missed: no pair */
    CoXlTestTiltFeetClear();
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("0 with corners that span no plane; 2 changed between a lean limit with their ground's slope and one without since the scan before: up turned 10.0 deg on average, 20.0 at most, 1 over 10 deg"));
    CoXlTestTiltPropSample(mp, Rp, 0x763, q0, at, 0.0f);
    CoXlTestTiltPropNote();
    CHECK(fake_host_logged("0 changed between a lean limit with their ground's slope and one without since the scan before: up turned 0.0 deg on average, 0.0 at most, 0 over 10 deg"));
    g_ray.owner = 0x1188B; g_ray.body = 0x801122A3u;

    g_case = "research: which units carry which rotation mode";
    CoXlTestTiltKind(0x701, 4); CoXlTestTiltKind(0x702, 4); CoXlTestTiltKind(0x703, 2);
    CHECK(fake_host_logged("tilt: kind: resource 0000000000000000 (entity resource 0000000000000000) has rotation mode 4: unit 00000701, not an enemy the host's AI holds"));
    CHECK(!fake_host_logged("has rotation mode 4: unit 00000702")); /* once a resource and mode */
    CHECK(fake_host_logged("has rotation mode 2: unit 00000703"));
    CoXlTestTiltTile(0, 0, NAN);

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

    g_case = "research: what a scan costs";
    CoXlTestTiltScanCostNote(1000000);
    CHECK(!fake_host_logged("tilt scan (30 s)")); /* no scan: no line */
    CoXlTestTiltScanCost(1500); CoXlTestTiltScanCost(4500); CoXlTestTiltScanCost(3000);
    CoXlTestTiltScanCostNote(1000000);
    CHECK(fake_host_logged("tilt scan (30 s): 3 scans took 9.00 ms, 3.000 ms a scan on average, 4.500 ms at worst (each in one frame, the research columns in it)"));
    CoXlTestTiltScanCost(500);
    CoXlTestTiltScanCostNote(1000000);
    CHECK(fake_host_logged("tilt scan (30 s): 1 scans took 0.50 ms, 0.500 ms a scan on average, 0.500 ms at worst")); /* the sums start again */

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tilt: all checks passed\n");
    return 0;
}
