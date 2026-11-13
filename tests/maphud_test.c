/* The in-mission map's zoom on a world larger than 2048 m: levels 3 and 4 at the game's element size, Tab held or not.
 * It never goes past level 4: the whole world is the M map's. A 2048 m world keeps the game's levels 2 to 4. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

uint32_t CoXlTestHudZoom(uintptr_t game, uintptr_t map, uint32_t level, float input, float* scale);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t g_map[0x3000], g_lg[0x3000], g_settings[0xAC600];

static void PutF(uint8_t* at, float v) { memcpy(at, &v, 4); }

/* A map on a world of `size` m, Tab held or not, the corner placement (448 px), the whole level playable. */
static void Map(uint8_t* game, float size, int expanded)
{
    memset(g_map, 0, sizeof g_map);
    PutF(g_map + 0x140, size);
    g_map[0x195] = (uint8_t)expanded;
    const uintptr_t lg = (uintptr_t)g_lg, settings = (uintptr_t)g_settings;
    memcpy(g_map + 0x118, &lg, 8);
    PutF(g_lg + 0x2D70, 0.99f);
    memcpy(game + 0x3326340u, &settings, 8);
    PutF(game + 0x21E7498u, 448.0f);
    PutF(game + 0x21E68F8u, 648.0f);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* game = g_fake.image;
    const uintptr_t g = (uintptr_t)game, m = (uintptr_t)g_map;
    float s = 0.0f;

    g_case = "a 16 km world, Tab held: out from 3 to 4, then it stays at 4";
    {
        Map(game, 16384.0f, 1);
        CHECK(CoXlTestHudZoom(g, m, 3, -1.0f, &s) == 4 && s > 0.218f && s < 0.219f); /* 448 / 2048 m */
        CHECK(CoXlTestHudZoom(g, m, 4, -1.0f, &s) == 4 && s > 0.218f && s < 0.219f);
        CHECK(CoXlTestHudZoom(g, m, 4, 1.0f, &s) == 3 && s == 0.45f);
        CHECK(CoXlTestHudZoom(g, m, 3, 1.0f, &s) == 3 && s == 0.45f);
    }

    g_case = "a 16 km world, Tab not held: the same two levels";
    {
        Map(game, 16384.0f, 0);
        CHECK(CoXlTestHudZoom(g, m, 4, -1.0f, &s) == 4);
        CHECK(CoXlTestHudZoom(g, m, 3, -1.0f, &s) == 4);
    }

    g_case = "a 2048 m world keeps the game's levels 2 to 4";
    {
        Map(game, 2048.0f, 1);
        PutF(g_map + 0x180, 448.0f); PutF(g_map + 0x184, 448.0f);
        CHECK(CoXlTestHudZoom(g, m, 3, 1.0f, &s) == 2 && s == 3.0f);
        CHECK(CoXlTestHudZoom(g, m, 2, -1.0f, &s) == 3 && s == 1.5f);
        CHECK(CoXlTestHudZoom(g, m, 3, -1.0f, &s) == 4);
        CHECK(CoXlTestHudZoom(g, m, 4, -1.0f, &s) == 4);
    }

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("maphud: all checks passed\n");
    return 0;
}
