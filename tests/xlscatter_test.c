/* The engine's scatter update on an extra large level: the far-scatter bake's views (op 0x4A), the water camera's view
 * (ops 0x0A and 0x0B) and the fog volumes camera's view (ops 0x43..0x45), all orthographic with near 1 and far 1001 at
 * any aspect, are left out; any other view or op (the in-game map's 0x4F among them), and any vanilla-size level, runs
 * the engine's update. The bake's cut notes once per level, the others once per record; all end at the ship. In play
 * (op 0x22), the same views are left out while a moving tile's merge window is open and for 2 s after it closes, noted
 * once per build. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGenLevel(float current);
int CoXlTestScatterUpdate(LONG op, LONG record, uint8_t* view);
int CoXlTestScatterUpdateAsIs(uint8_t* view);
void CoXlTestWorldShip(void);
void CoXlTestScatterMerge(int action, LONG64 agoMs);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const char kWater[] = "the water camera's view is left out of the scatter update";
static const char kBake[] = "the far-scatter bake's tile views are left out of the scatter update";
static const char kFog[] = "the fog volumes camera's view is left out of the scatter update";
static const char kMerge[] = "the moving tile's merge camera view is left out of the scatter update";

/* A render camera as 0x212F10 reads it: near +0x20, far +0x24, projection +0x28 (0 orthographic), aspect +0x278. */
static uint8_t g_view[0x300];
static uint8_t* View(float nearZ, float farZ, uint32_t projection, float aspect)
{
    memset(g_view, 0, sizeof g_view);
    memcpy(g_view + 0x20, &nearZ, 4); memcpy(g_view + 0x24, &farZ, 4); memcpy(g_view + 0x28, &projection, 4); memcpy(g_view + 0x278, &aspect, 4);
    return g_view;
}
static uint8_t* Ortho(void) { return View(1.0f, 1001.0f, 0, 1.0f); }

static int Count(const char* text) { int n = 0; for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p++) n++; return n; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;

    g_case = "a complex level's water camera";
    g_fake.log[0] = 0;
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 104, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, Ortho()) == 0);
    CHECK(Count(kWater) == 3);
    CHECK(fake_host_logged("view is left out of the scatter update (op 0x0A, record 159)"));
    CHECK(fake_host_logged("view is left out of the scatter update (op 0x0B, record 160)"));
    CHECK(Count(kBake) == 0);

    g_case = "a complex level's bake views";
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 0);
    CHECK(Count(kBake) == 1);

    g_case = "a complex level's fog volumes camera";
    CHECK(CoXlTestScatterUpdate(0x43, 162, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x44, 163, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x44, 163, View(1.0f, 1001.0f, 0, 16.0f / 9.0f)) == 0);
    CHECK(CoXlTestScatterUpdate(0x45, 164, Ortho()) == 0);
    CHECK(Count(kFog) == 3);
    CHECK(fake_host_logged("view is left out of the scatter update (op 0x44, record 163)"));
    CHECK(CoXlTestScatterUpdate(0x44, 163, View(1.0f, 1001.0f, 1, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x44, 163, View(0.1f, 3000.0f, 1, 16.0f / 9.0f)) == 1);
    CHECK(Count(kWater) == 3);

    g_case = "the same view at other ops";
    CHECK(CoXlTestScatterUpdate(0x4F, 165, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x50, 166, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x46, 154, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x47, 155, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x42, 150, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x4B, 158, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0xB5, 161, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x38, 78, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x22, 175, Ortho()) == 1);

    g_case = "other views at op 0x0A and 0x0B";
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(1.0f, 1001.0f, 1, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(1.0f, 1001.0f, 1, 16.0f / 9.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, View(0.1f, 1001.0f, 0, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, View(1.0f, 2000.0f, 0, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(0.1f, 3000.0f, 1, 21.0f / 9.0f)) == 1);

    g_case = "the water camera with a player's own aspect_ratio";
    CHECK(CoXlTestScatterUpdate(0x0B, 160, View(1.0f, 1001.0f, 0, 16.0f / 9.0f)) == 0);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(1.0f, 1001.0f, 0, 21.0f / 9.0f)) == 0);

    g_case = "the next complex level notes again";
    g_fake.log[0] = 0;
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x44, 163, Ortho()) == 0);
    CHECK(Count(kWater) == 1 && Count(kBake) == 1 && Count(kFog) == 1);

    g_case = "a vanilla-size level";
    g_fake.log[0] = 0;
    CoXlTestGenLevel(1.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 104, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x44, 163, Ortho()) == 1);
    CHECK(Count(kWater) == 0 && Count(kBake) == 0 && Count(kFog) == 0);

    g_case = "a generation cut short at op 0x0A, then the ship";
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 0);
    CoXlTestWorldShip();
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 1);

    g_case = "a generation cut short at op 0x44, then the ship";
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x44, 163, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 0);
    CoXlTestWorldShip();
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 1);

    g_case = "in play with no tile build: the view keeps its update";
    g_fake.log[0] = 0;
    CoXlTestScatterMerge(3, 0);
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 1);
    CHECK(Count(kMerge) == 0);

    g_case = "a moving tile's build in play";
    CoXlTestScatterMerge(0, 0);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x22, 183, View(1.0f, 1001.0f, 0, 16.0f / 9.0f)) == 0);
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 0);
    CHECK(Count(kMerge) == 1);
    CHECK(fake_host_logged("merge camera view is left out of the scatter update (build 1 of this run)"));

    g_case = "the player's and other views during a build";
    CHECK(CoXlTestScatterUpdate(0x22, 183, View(0.1f, 3000.0f, 1, 16.0f / 9.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x22, 183, View(1.0f, 1001.0f, 1, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x22, 183, View(1.0f, 2000.0f, 0, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x22, 183, View(0.5f, 1001.0f, 0, 1.0f)) == 1);

    g_case = "the grace after the teardown";
    CoXlTestScatterMerge(1, 0);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 0);
    CoXlTestScatterMerge(2, 1500);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 0);
    CoXlTestScatterMerge(2, 2500);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 1);
    CHECK(Count(kMerge) == 1);

    g_case = "the next build notes again";
    CoXlTestScatterMerge(0, 0);
    CoXlTestScatterMerge(0, 0);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 0);
    CHECK(fake_host_logged("(build 2 of this run)"));
    CHECK(Count(kMerge) == 2);

    g_case = "a build's window on a vanilla-size level";
    CoXlTestGenLevel(1.0f);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 1);

    g_case = "the ship ends a build's window";
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x22, 183, Ortho()) == 0);
    CoXlTestWorldShip();
    CHECK(CoXlTestScatterUpdateAsIs(Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x4F, 165, Ortho()) == 1);

    if (g_failed) { printf("xlscatter: %d check(s) failed\n", g_failed); return 1; }
    printf("xlscatter: all checks passed\n");
    return 0;
}
