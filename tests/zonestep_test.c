/* The game code a replayed near-tile step runs under. RenderZoneHeightmap (op 0x3D, game 0xF4A280) maps each zone's
 * height materials with the texture coordinate position x [0xF4A39C's constant] / world size; the tile keeps the level's
 * 1.0, so its zone heights are the coarse terrain's at the tile's resolution (at 8.0 a jungle material repeated every
 * 64 m as cones 1.5-4.5 m tall). 0x3C still runs without the replayed level's caps. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
int CoXlTestNearStepPatch(uint8_t op, int on);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define ZONE_LOAD 0xF4A39Cu /* movss xmm10, [rip + rel32]: the texture coordinates' scale */
#define ONE 0x23C6D70u
#define EIGHT 0x23C7524u
#define CAPS 0xF4A07Bu      /* test r13, r13 / je: a subregion type's cap height */

/* The constant the zone pass loads, as the game resolves its rip-relative operand. */
static float ZoneScale(void)
{
    int32_t rel;
    memcpy(&rel, g_fake.image + ZONE_LOAD + 5, 4);
    float v;
    memcpy(&v, g_fake.image + ZONE_LOAD + 9 + rel, 4);
    return v;
}

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    static const uint8_t movss[5] = {0xF3, 0x44, 0x0F, 0x10, 0x15};
    int32_t rel = (int32_t)(ONE - (ZONE_LOAD + 9));
    memcpy(g_fake.image + ZONE_LOAD, movss, 5);
    memcpy(g_fake.image + ZONE_LOAD + 5, &rel, 4);
    const float one = 1.0f, eight = 8.0f;
    memcpy(g_fake.image + ONE, &one, 4);
    memcpy(g_fake.image + EIGHT, &eight, 4);
    static const uint8_t je[5] = {0x4D, 0x85, 0xED, 0x74, 0x6B};
    memcpy(g_fake.image + CAPS, je, 5);
}

int main(void)
{
    Start("the tile's zone heights take the level's texture scale");
    CHECK(!CoXlTestNearStepPatch(0x3D, 1));
    CHECK(ZoneScale() == 1.0f);
    CHECK(!CoXlTestNearStepPatch(0x3D, 0));
    CHECK(ZoneScale() == 1.0f);

    Start("the replayed Voronoi pass still skips the level's caps, and gets them back after");
    CHECK(CoXlTestNearStepPatch(0x3C, 1));
    CHECK(g_fake.image[CAPS + 3] == 0xEB);
    CHECK(ZoneScale() == 1.0f);
    CHECK(CoXlTestNearStepPatch(0x3C, 0));
    CHECK(g_fake.image[CAPS + 3] == 0x74);

    Start("other steps run the game's code as it is");
    for (int op = 0; op < 256; op++)
        if (op != 0x3C) CHECK(!CoXlTestNearStepPatch((uint8_t)op, 1));
    CHECK(ZoneScale() == 1.0f && g_fake.image[CAPS + 3] == 0x74);

    if (g_failed) { printf("zonestep: %d checks failed\n", g_failed); return 1; }
    printf("zonestep: all cases passed\n");
    return 0;
}
