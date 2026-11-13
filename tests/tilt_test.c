/* The rotation's ray reader (game 0x620810) on extra large levels: a ray that starts under a live tile's ground reads
 * that ground above it (minus the depth) when it missed; hits, misses over the ground, other places and a busy tile
 * list keep the game's answer. The game's call is always made as the game made it.
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

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("tilt: all checks passed\n");
    return 0;
}
