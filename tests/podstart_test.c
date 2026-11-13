/* A reinforcement hellpod's start on a complex level: brought to 1024 m from its drop point along the same line, its
 * height kept; off complex levels, inside the reach and for a start above the point, the game's start as it is. The
 * install compares the game's bytes at the start, its drop point read and its destroyer lookup. Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_podstart.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The reviewed build's bytes at the guard's three pins (game.dll 25480438). */
static const struct { uint32_t rva; const char* hex; } kPins[] = {
    {0x929E40u, "40535657 4883EC50 488B05C121D101 4833C4 4889442440"},
    {0x929E78u, "8B4740 F20F104738 89442428 488B4748"},
    {0x929FDCu, "488B0D5DC39F02 488D542430 4881C158FA0100 448BC0 E809766D00"},
};

static void WritePins(uint8_t* image)
{
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
    {
        uint8_t* at = image + kPins[i].rva;
        for (const char* c = kPins[i].hex; *c;)
        {
            if (*c == ' ') { c++; continue; }
            unsigned v = 0;
            sscanf(c, "%2x", &v);
            *at++ = (uint8_t)v;
            c += 2;
        }
    }
}

typedef float* (*StartFn)(void* manager, float* out, void* record, uint8_t* params);
static float g_gameStart[3];
static int g_calls;
static void *g_manager, *g_record;
static uint8_t* g_params;
static float* EngineStart(void* manager, float* out, void* record, uint8_t* params)
{
    g_calls++; g_manager = manager; g_record = record; g_params = params;
    memcpy(out, g_gameStart, sizeof g_gameStart);
    return out;
}

static float g_liveScale = 8.0f;
static float Scale(void) { return g_liveScale; }

static float Across(const float* a, const float* b) { return hypotf(a[0] - b[0], a[1] - b[1]); }

int main(void)
{
    g_case = "the cap alone";
    {
        const float target[3] = {-1602.0f, -2388.0f, 30.0f};
        float start[3] = {1026.0f, 2.0f, 700.0f};
        const float was = Across(start, target);
        CHECK(co_podstart_cap(start, target, 1024.0f) == 1);
        CHECK(fabsf(Across(start, target) - 1024.0f) < 0.05f);
        CHECK(start[2] == 700.0f);
        /* the same line: the start lies on the segment from the drop point to the destroyer */
        CHECK(fabsf((start[0] - target[0]) * (2.0f + 2388.0f) - (start[1] - target[1]) * (1026.0f + 1602.0f)) < 1.0f);
        CHECK(fabsf((start[0] - target[0]) / (1026.0f + 1602.0f) - 1024.0f / was) < 1e-4f);
        float inside[3] = {-1000.0f, -2000.0f, 700.0f}, keep[3];
        memcpy(keep, inside, sizeof keep);
        CHECK(co_podstart_cap(inside, target, 1024.0f) == 0 && memcmp(inside, keep, sizeof keep) == 0);
        float edge[3] = {target[0] + 1024.0f, target[1], 5.0f};
        memcpy(keep, edge, sizeof keep);
        CHECK(co_podstart_cap(edge, target, 1024.0f) == 0 && memcmp(edge, keep, sizeof keep) == 0);
        float above[3] = {target[0], target[1], 1030.0f};
        CHECK(co_podstart_cap(above, target, 1024.0f) == 0 && above[0] == target[0] && above[1] == target[1]);
        float bad[3] = {NAN, 0.0f, 0.0f};
        CHECK(co_podstart_cap(bad, target, 1024.0f) == 0 && isnan(bad[0]) && bad[1] == 0.0f);
    }

    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t game = (uintptr_t)g_fake.image;
    WritePins(g_fake.image);
    g_fake.strictCode = 1;
    g_fake.originals[0].target = game + CO_PODSTART; g_fake.originals[0].original = (void*)EngineStart;

    g_case = "another game's bytes";
    co_podstart_test_reset();
    g_fake.image[0x929FDCu + 23] ^= 0xFF; /* the destroyer lookup's call moved */
    CHECK(co_podstart_install(game, Scale) != NULL && g_fake.hookCount == 0);
    g_fake.image[0x929FDCu + 23] ^= 0xFF;
    g_fake.image[0x929E78u + 7] ^= 0xFF; /* the drop point read from elsewhere */
    CHECK(co_podstart_install(game, Scale) != NULL && g_fake.hookCount == 0);
    g_fake.image[0x929E78u + 7] ^= 0xFF;
    g_fake.image[0x929E40u + 6] ^= 0xFF;
    CHECK(co_podstart_install(game, Scale) != NULL && g_fake.hookCount == 0);
    g_fake.image[0x929E40u + 6] ^= 0xFF;
    CHECK(co_podstart_install(0, Scale) != NULL && co_podstart_install(game, NULL) != NULL && g_fake.hookCount == 0);

    g_case = "a hook the loader refuses";
    g_fake.noHook = CO_PODSTART;
    CHECK(co_podstart_install(game, Scale) != NULL);
    g_fake.noHook = 0;

    g_case = "the install";
    CHECK(co_podstart_install(game, Scale) == NULL && g_fake.hookCount == 1);
    StartFn start = (StartFn)fake_host_detour(CO_PODSTART);
    CHECK(start != NULL);
    if (start == NULL) { printf("podstart: %d failed\n", g_failed); return 1; }

    uint8_t params[0x80] = {0};
    const float target[3] = {-1602.0f, -2388.0f, 30.0f};
    memcpy(params + 0x38, target, sizeof target); /* the game's own offset (0x929E78) */
    float out[3];
    int manager, record;

    g_case = "a complex level: the destroyer's start brought to 1024 m";
    g_gameStart[0] = 1026.0f; g_gameStart[1] = 2.0f; g_gameStart[2] = 650.0f;
    float* got = start(&manager, out, &record, params);
    CHECK(got == out && g_calls == 1 && g_manager == &manager && g_record == &record && g_params == params);
    CHECK(fabsf(Across(out, target) - 1024.0f) < 0.05f && out[2] == 650.0f);
    CHECK(out[0] > target[0] && out[1] > target[1]); /* toward the destroyer */
    CHECK(fake_host_logged("reinforcement pod 1 started 3552 m from its drop point (-1602, -2388); it starts 1024 m away, 620 m above it"));
    CHECK(co_podstart_starts() == 1);

    g_case = "a complex level, the destroyer within reach";
    g_gameStart[0] = -900.0f; g_gameStart[1] = -1800.0f;
    start(&manager, out, &record, params);
    CHECK(out[0] == -900.0f && out[1] == -1800.0f && out[2] == 650.0f);

    g_case = "a complex level, a start above the point (the initial drop)";
    g_gameStart[0] = target[0]; g_gameStart[1] = target[1]; g_gameStart[2] = 1030.0f;
    start(&manager, out, &record, params);
    CHECK(out[0] == target[0] && out[1] == target[1] && out[2] == 1030.0f);
    CHECK(co_podstart_starts() == 3); /* every start on a complex level starts a frame watch, capped or not */

    g_case = "the game's map: the start as it is";
    g_liveScale = 1.0f;
    g_gameStart[0] = 1026.0f; g_gameStart[1] = 2.0f; g_gameStart[2] = 650.0f;
    start(&manager, out, &record, params);
    CHECK(out[0] == 1026.0f && out[1] == 2.0f && out[2] == 650.0f);
    CHECK(co_podstart_starts() == 3);
    g_liveScale = 8.0f;

    g_case = "a second install keeps the first";
    const int hooks = g_fake.hookCount;
    CHECK(co_podstart_install(game, Scale) == NULL && g_fake.hookCount == hooks);

    if (g_failed) { printf("podstart: %d failed\n", g_failed); return 1; }
    printf("podstart: all passed\n");
    return 0;
}
