/* The map's markers on a complex mission are the game's 16 slots and 10 trackers behind HellForge's guards: the same
 * limits on both kinds of level, nothing written, and the part's check refuses a map or a guard that is another's. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_markers.h"
#include "xl/co_marker_sites.h"
#include "xl/co_normal.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    return g_fake.image;
}
static int Quiet(void) { return g_fake.writes == 0 && g_fake.hookCount == 0; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

/* The updater's slot hand-out as its code reads (0x18B42F0): the instances in their order, each test before a slot
 * is touched, a hidden instance taking none; then the entities and the route marker behind HellForge's guards.
 * Returns the highest slot written + 1. */
static uint32_t HandOut(const uint8_t* hidden, const uint8_t* tracked, uint32_t instances, uint32_t entities, int route, uint32_t* shown, uint32_t* trackers)
{
    const uint32_t slots = co_level_limit(CO_LIMIT_MARKER_SLOTS, 1), most = co_level_limit(CO_LIMIT_MARKER_TRACKERS, 1);
    uint32_t used = 0, track = 0, touched = 0;
    for (uint32_t i = 0; i < instances; i++)
    {
        if (used >= slots || track >= most) break;   /* 0x18B44A3 / 0x18B44AC */
        if (touched < used + 1) touched = used + 1;  /* the slot is written, shown or hidden */
        if (hidden[i]) continue;                     /* 0x18B4548: no slot taken */
        if (tracked[i]) track++;                     /* 0x18B50DE */
        shown[used++] = i;                           /* 0x18B5144 */
    }
    for (uint32_t e = 0; e < entities; e++)
    {
        if (used >= slots) break;                    /* HellForge's guard at 0x18B51B0 */
        if (touched < used + 1) touched = used + 1;
        shown[used++] = 100 + e;                     /* 0x18B59F1 */
    }
    if (route && used < slots) { if (touched < used + 1) touched = used + 1; shown[used] = 200; } /* its guard at 0x18B5FF4 */
    *trackers = track;
    return touched;
}

int main(void)
{
    uint8_t* image = Reset("the limits are the game's on both kinds of level");
    uintptr_t game = (uintptr_t)image;
    CHECK(co_level_limit(CO_LIMIT_MARKER_SLOTS, 0) == CO_MARKER_SLOTS && co_level_limit(CO_LIMIT_MARKER_SLOTS, 1) == CO_MARKER_SLOTS && CO_MARKER_SLOTS == 16u);
    CHECK(co_level_limit(CO_LIMIT_MARKER_TRACKERS, 0) == CO_MARKER_TRACKERS && co_level_limit(CO_LIMIT_MARKER_TRACKERS, 1) == CO_MARKER_TRACKERS && CO_MARKER_TRACKERS == 10u);
    for (size_t i = 0; i < sizeof kCoMarkerLimits / sizeof kCoMarkerLimits[0]; i++)
        CHECK(kCoMarkerLimits[i].bytes[kCoMarkerLimits[i].at] == co_level_limit(i % 2 ? CO_LIMIT_MARKER_TRACKERS : CO_LIMIT_MARKER_SLOTS, 1));

    g_case = "HellForge's level 11 as it is";
    CHECK(co_markers_check(game) == NULL && Quiet());
    for (int i = 0; i < 2; i++)
    {
        /* the fake's guard is HellForge's form: the jump, the test of 16, the way out, the site's bytes, the way back */
        const CoMarkerGuard* g = &kCoMarkerGuards[i];
        const uint8_t* stub = (const uint8_t*)Leads(image + g->rva + 1);
        CHECK(image[g->rva] == 0xE9 && stub == image + FAKE_STOCK_MARKERS + 0x20 * i && stub[4] == 16);
        CHECK(Leads(stub + 7) == game + g->leave && memcmp(stub + 11, g->game, g->length) == 0 && Leads(stub + 12 + g->length) == game + g->rva + g->length);
    }

    g_case = "30 objectives";
    {
        uint8_t hidden[30] = {0}, tracked[30] = {0};
        uint32_t shown[17], trackers = 0;
        memset(shown, 0xFF, sizeof shown);
        /* all shown, a route mission with entities: the first 16 instances in their order, slot 16 never touched */
        CHECK(HandOut(hidden, tracked, 30, 4, 1, shown, &trackers) == 16 && shown[0] == 0 && shown[15] == 15 && shown[16] == 0xFFFFFFFFu);
        /* three of the first sixteen done and hidden: the next three move up, still no entity and no route marker */
        hidden[2] = hidden[5] = hidden[9] = 1;
        memset(shown, 0xFF, sizeof shown);
        CHECK(HandOut(hidden, tracked, 30, 4, 1, shown, &trackers) == 16 && shown[2] == 3 && shown[15] == 18 && shown[16] == 0xFFFFFFFFu);
        /* ten tracked ones among the first twelve end the instance pass early; the entities and the route take the rest */
        for (int i = 0; i < 12; i++) { hidden[i] = 0; tracked[i] = i != 3 && i != 7; }
        memset(shown, 0xFF, sizeof shown);
        CHECK(HandOut(hidden, tracked, 30, 2, 1, shown, &trackers) == 15 && trackers == 10 && shown[11] == 11 && shown[12] == 100 && shown[14] == 200 && shown[15] == 0xFFFFFFFFu);
        /* 17 hidden: the 13 shown ones each have a slot, with room for the route marker */
        memset(hidden, 0, sizeof hidden); memset(tracked, 0, sizeof tracked);
        for (int i = 0; i < 17; i++) hidden[i] = 1;
        memset(shown, 0xFF, sizeof shown);
        CHECK(HandOut(hidden, tracked, 30, 0, 1, shown, &trackers) == 14 && shown[0] == 17 && shown[12] == 29 && shown[13] == 200);
    }

    for (size_t i = 0; i < sizeof kCoMarkerLimits / sizeof kCoMarkerLimits[0]; i++)
    {
        image = Reset("a map with other counts");
        image[kCoMarkerLimits[i].rva + kCoMarkerLimits[i].at] = i % 2 ? 32 : 64; /* the arrays moved elsewhere, 64 and 32 */
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
    }
    for (int i = 0; i < 2; i++)
    {
        const CoMarkerGuard* g = &kCoMarkerGuards[i];
        const uint32_t stub = FAKE_STOCK_MARKERS + 0x20u * (uint32_t)i;
        image = Reset("the game without HellForge's guard");
        memcpy(image + g->rva, g->game, g->length);
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
        image = Reset("a jump that is not padded");
        image[g->rva + 5] = g->game[5];
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
        image = Reset("a guard of 64 slots");
        image[stub + 4] = 64;
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
        image = Reset("a guard that leaves elsewhere");
        image[stub + 7]++;
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
        image = Reset("a guard without the site's instructions");
        image[stub + 11] ^= 1;
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
        image = Reset("a guard that comes back elsewhere");
        image[stub + 12 + g->length]++;
        CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());
    }
    image = Reset("an empty image");
    memset(image + 0x18A0000u, 0, 0x20000);
    CHECK(co_markers_check((uintptr_t)image) != NULL && Quiet());

    if (g_failed) { printf("markers: %d checks failed\n", g_failed); return 1; }
    printf("markers: all cases passed\n");
    return 0;
}
