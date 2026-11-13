/* The engine's scatter update on an extra large level: the far-scatter bake's views (op 0x4A) and the water camera's
 * view (ops 0x0A and 0x0B), both near 1, far 1001, aspect 1, are left out; any other view or op, and any vanilla-size
 * level, runs the engine's update. Each cut notes once per level, the water's once per record. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGenLevel(float current);
int CoXlTestScatterUpdate(LONG op, LONG record, uint8_t* view);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const char kWater[] = "the water camera's view is left out of the scatter update";
static const char kBake[] = "the far-scatter bake's tile views are left out of the scatter update";

/* A render camera as 0x212F10 reads it: near +0x20, far +0x24, aspect +0x278. */
static uint8_t g_view[0x300];
static uint8_t* View(float nearZ, float farZ, float aspect)
{
    memset(g_view, 0, sizeof g_view);
    memcpy(g_view + 0x20, &nearZ, 4); memcpy(g_view + 0x24, &farZ, 4); memcpy(g_view + 0x278, &aspect, 4);
    return g_view;
}
static uint8_t* Ortho(void) { return View(1.0f, 1001.0f, 1.0f); }

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

    g_case = "the same view at other ops";
    CHECK(CoXlTestScatterUpdate(0x4B, 158, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0xB5, 161, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x38, 78, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x22, 175, Ortho()) == 1);

    g_case = "other views at op 0x0A and 0x0B";
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(1.0f, 1001.0f, 16.0f / 9.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, View(0.1f, 1001.0f, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, View(1.0f, 2000.0f, 1.0f)) == 1);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, View(0.1f, 3000.0f, 21.0f / 9.0f)) == 1);

    g_case = "the next complex level notes again";
    g_fake.log[0] = 0;
    CoXlTestGenLevel(8.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 159, Ortho()) == 0);
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 0);
    CHECK(Count(kWater) == 1 && Count(kBake) == 1);

    g_case = "a vanilla-size level";
    g_fake.log[0] = 0;
    CoXlTestGenLevel(1.0f);
    CHECK(CoXlTestScatterUpdate(0x0A, 104, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x0B, 160, Ortho()) == 1);
    CHECK(CoXlTestScatterUpdate(0x4A, 157, Ortho()) == 1);
    CHECK(Count(kWater) == 0 && Count(kBake) == 0);

    if (g_failed) { printf("xlscatter: %d check(s) failed\n", g_failed); return 1; }
    printf("xlscatter: all checks passed\n");
    return 0;
}
