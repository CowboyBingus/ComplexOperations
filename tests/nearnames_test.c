/* The near terrain's render_world wrapper (proto/difficulty_world_terrain.c NearRunAt) while a tile replays: the
 * player's view and every other call outside the merge's job consumer draws with the level's maps under
 * generated_heightmap, painted_roads_height and generated_materials; the consumer's jobs draw with the tile's; the
 * tile's are back after each view call so the replay's next op finds them; with no replay the names are not touched.
 * The view's terrain layers bind those names, and the tile's maps under them tinted the whole frame for a build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
uint64_t CoXlTestNearRun(int consumer, int active, uint64_t tile0, uint64_t tile1, uint64_t oneMip, void* original);
void CoXlTestNearReplaying(int active);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3700000u   /* free room in the fake image: the api and its +0x10 table */
#define TABLE 0x3700100u
#define HEIGHT 0x2A9E3232u
#define MATERIALS 0x95E7E3ACu
#define ROADS 0xBF45B677u
#define TILE0 0x1000ull
#define TILE1 0x2000ull
#define ONEMIP 0x3000ull

/* The published-name overlay: a name's resource, 0 when unpublished (the render config's own). */
static struct { uint32_t name; uint64_t resource; } g_names[8];
static int g_publishes, g_unpublishes;
static uint64_t g_seen[3]; /* what render_world saw under HEIGHT, MATERIALS, ROADS */
static int g_runs, g_endInside;

static uint64_t* Slot(uint32_t name)
{
    for (int k = 0; k < 8; k++) if (g_names[k].name == name) return &g_names[k].resource;
    for (int k = 0; k < 8; k++) if (g_names[k].name == 0) { g_names[k].name = name; return &g_names[k].resource; }
    return NULL;
}
static void Publish(uint32_t name, uint64_t resource) { *Slot(name) = resource; g_publishes++; }
static void Unpublish(uint32_t name) { *Slot(name) = 0; g_unpublishes++; }

static uint64_t Original(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    g_seen[0] = *Slot(HEIGHT); g_seen[1] = *Slot(MATERIALS); g_seen[2] = *Slot(ROADS);
    g_runs++;
    if (g_endInside) CoXlTestNearReplaying(0); /* the replay's end inside the call (NearReplayEnd puts the level's back itself) */
    return a + b + c + d + e + f + g + h;
}

/* The names as the replay leaves them (NEAR_START: the tile's published), or as play has them. */
static void Setup(const char* name, int replaying)
{
    static int made;
    g_case = name;
    if (!made)
    {
        made = 1;
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        g_coMod = (HfMod*)&g_fake;
        uint8_t* image = g_fake.image;
        CoXlTestGame((uintptr_t)image);
        uint64_t api = (uint64_t)(uintptr_t)(image + API), table = (uint64_t)(uintptr_t)(image + TABLE);
        memcpy(image + 0x3326308u, &api, 8);
        memcpy(image + API + 0x10, &table, 8);
        void (*publish)(uint32_t, uint64_t) = Publish; memcpy(image + TABLE + 0x210, &publish, 8);
        void (*unpublish)(uint32_t) = Unpublish; memcpy(image + TABLE + 0x218, &unpublish, 8);
    }
    memset(g_names, 0, sizeof g_names);
    *Slot(HEIGHT) = replaying ? TILE0 : ONEMIP; *Slot(ROADS) = replaying ? TILE0 : ONEMIP; *Slot(MATERIALS) = replaying ? TILE1 : 0;
    g_publishes = g_unpublishes = 0; g_runs = 0; g_endInside = 0;
    memset(g_seen, 0xFF, sizeof g_seen);
}

int main(void)
{
    Setup("the player's view while a tile replays", 1);
    CHECK(CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original) == 1 + 2 + 3 + 0 + 5 + 6 + 7 + 8);
    CHECK(g_runs == 1);
    CHECK(g_seen[0] == ONEMIP && g_seen[2] == ONEMIP); /* the level's one-mip heightmap, its alias too */
    CHECK(g_seen[1] == 0);                             /* the level's material map: the render config's own */
    CHECK(*Slot(HEIGHT) == TILE0 && *Slot(ROADS) == TILE0 && *Slot(MATERIALS) == TILE1); /* the replay's again after */

    Setup("the merge's job consumer while a tile replays", 1);
    CoXlTestNearRun(1, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_runs == 1);
    CHECK(g_seen[0] == TILE0 && g_seen[2] == TILE0 && g_seen[1] == TILE1);
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("the player's view, a level with no one-mip copy", 1);
    CoXlTestNearRun(0, 1, TILE0, TILE1, 0, (void*)Original);
    CHECK(g_seen[0] == 0 && g_seen[2] == 0 && g_seen[1] == 0); /* every name the render config's own */
    CHECK(*Slot(HEIGHT) == TILE0 && *Slot(ROADS) == TILE0 && *Slot(MATERIALS) == TILE1);

    Setup("a view call with no replay", 0);
    CoXlTestNearRun(0, 0, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_seen[0] == ONEMIP && g_seen[2] == ONEMIP && g_seen[1] == 0);
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("a replay with no textures yet", 0);
    CoXlTestNearRun(0, 1, 0, 0, ONEMIP, (void*)Original);
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("the replay ends inside the view call", 1);
    g_endInside = 1;
    CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_seen[0] == ONEMIP && g_seen[1] == 0);
    CHECK(*Slot(HEIGHT) == ONEMIP && *Slot(ROADS) == ONEMIP && *Slot(MATERIALS) == 0); /* not the tile's again */

    Setup("view, consumer, view: one frame of a build", 1);
    CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_seen[1] == 0);
    CoXlTestNearRun(1, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_seen[0] == TILE0 && g_seen[1] == TILE1);
    CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(g_seen[0] == ONEMIP && g_seen[1] == 0 && g_runs == 3);

    CoXlTestNearReplaying(0);
    if (g_failed) { printf("nearnames: %d check(s) failed\n", g_failed); return 1; }
    printf("nearnames: all checks passed\n");
    return 0;
}
