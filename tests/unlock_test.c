/* A main objective waits for the steps of its own mission on a complex operation: the rule, the note it reads, the
 * instance's mission found as the game finds an instance's location, the two stubs (whose normal path is the game's
 * bytes) and the war table's lines from a step to its own mission's mains only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"
#include "xl/co_unlock.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* Free places in the image for the game's objects: the world (its level generator at +0xAE288, the preview records
 * from +0xAE2A0), the generator, the unit lookup's table, the location records' units, the objective manager. */
#define WORLD 0x3500000u
#define LEVEL 0x3600000u
#define UNITS 0x3700000u
#define UNIT_RECORDS 0x3800000u
#define MANAGER 0x3900000u
#define PREVIEWS 0x3000000u
#define GEN 0x100000u
#define LIST 0x1000u
#define UNIT(handle) (0xABC00000ull + (handle))
#define CLUSTER(l) (0xC1000000ull + 0xE0u * (l)) /* each location's own cluster (+0xE0) */

static uint8_t* g_image;

static void Put64(uint32_t at, uint64_t v) { memcpy(g_image + at, &v, 8); }
static void Put32(uint32_t at, uint32_t v) { memcpy(g_image + at, &v, 4); }
static uint64_t At(uint32_t at) { return (uint64_t)(uintptr_t)(g_image + at); }

static int g_noUnits; /* the unit lookup finds no unit yet */
static uint64_t TestUnitOf(uint32_t handle) { return handle != 0 && handle < 100 && !g_noUnits ? UNIT(handle) : 0; }

/* Location l: its unit's handle (0 none), objective id, x/y and its own cluster. */
static void Location(uint32_t l, uint32_t handle, uint32_t id, float x, float y)
{
    const uint32_t loc = GEN + LIST + 0x108u * l, record = UNIT_RECORDS + 0x100u * l;
    Put64(loc + 0x98, handle ? At(record) : 0);
    Put64(record + 0xA8, UNIT(handle));
    memcpy(g_image + loc + 0xA4, &x, 4); memcpy(g_image + loc + 0xA8, &y, 4);
    Put32(loc + 0xD8, id);
    Put64(loc + 0xE0, CLUSTER(l));
    Put32(GEN + 0x11A715Cu, l + 1 > *(uint32_t*)(g_image + GEN + 0x11A715Cu) ? l + 1 : *(uint32_t*)(g_image + GEN + 0x11A715Cu));
}
static void Instance(uint32_t i, uint32_t handle)
{
    const uint32_t entity = MANAGER + 0x1000u + 0x20u * i;
    Put64(MANAGER + 0x100u + 8u * i, At(entity));
    Put32(entity + 8, 0x500u + i); Put32(entity + 0xC, handle);
}

/* The hologram's originals: what the marker source gives, and which slots the draw would line to. */
static uint32_t g_outputs;
static uint64_t g_pos[8];
static uint32_t g_ids[8];
static uint32_t g_drawn;
static float g_alpha;
static uint32_t TestMarkers(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out)
{
    memcpy(pos, g_pos, 8ull * g_outputs); memcpy(out, g_ids, 4ull * g_outputs);
    return g_outputs;
}
static uint8_t* g_holo;
#define SLOT(s) (g_holo + 0x886B0u + 0x180u * (s))
static uint64_t TestLines(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha)
{
    g_drawn = 0;
    for (uint32_t s = 0; s < *(uint32_t*)(g_holo + 0x8B730u); s++) if (SLOT(s)[0x17C] == 0) g_drawn |= 1u << s;
    g_alpha = alpha;
    return 0;
}

static uint64_t Pos(float x, float y) { uint64_t v; memcpy(&v, &x, 4); memcpy((uint8_t*)&v + 4, &y, 4); return v; }

static void World(int install)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    co_unlock_test_reset();
    Put64(0x3326340u, At(WORLD)); Put64(WORLD + 0xAE288u, At(LEVEL)); Put64(LEVEL + 0x2C8u, At(GEN));
    Put64(0x3326308u, At(UNITS)); Put64(UNITS + 0x18u, At(UNITS + 0x100u)); Put64(UNITS + 0x100u + 0x710u, (uint64_t)(uintptr_t)TestUnitOf);
    Put64(MANAGER + 0x50u, At(MANAGER + 0x100u));
    /* two missions: 0 has a main and two steps, 1 a main and a step; a side objective; a main with no note */
    Location(0, 1, 0xA0, 100, 200); Location(1, 2, 0xA1, 110, 210); Location(2, 3, 0xA1, 120, 220);
    Location(3, 4, 0xB0, 300, 400); Location(4, 5, 0xB1, 310, 410); Location(5, 6, 0xC0, 500, 600); Location(6, 7, 0xD0, 700, 800);
    for (uint32_t i = 0; i < 7; i++) Instance(i, i + 1);
    Instance(7, 42); /* a unit no location holds */
    memcpy(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA);
    memcpy(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB);
    /* the hologram's originals, which the hooks call */
    g_fake.originals[0].target = (uintptr_t)(g_image + CO_UNLOCK_MARKERS); g_fake.originals[0].original = (void*)TestMarkers;
    g_fake.originals[1].target = (uintptr_t)(g_image + CO_UNLOCK_LINES); g_fake.originals[1].original = (void*)TestLines;
    if (install) CHECK(co_unlock_install((uintptr_t)g_image, LIST) == NULL); /* the game's base, as the stubs know it */
}
static void Note(void)
{
    const uint32_t location[6] = {0, 1, 2, 3, 4, 5}, id[6] = {0xA0, 0xA1, 0xA1, 0xB0, 0xB1, 0xC0};
    const uint64_t cluster[6] = {CLUSTER(0), CLUSTER(1), CLUSTER(2), CLUSTER(3), CLUSTER(4), CLUSTER(5)};
    const uint8_t mission[6] = {0, 0, 0, 1, 1, CO_UNLOCK_NONE};
    co_unlock_note((uintptr_t)At(LEVEL), (uintptr_t)At(GEN), location, id, cluster, mission, 6);
}
static void Swap(uint32_t l, uint32_t k) /* two location records change places, as a reorder would leave them */
{
    uint8_t t[0x108];
    memcpy(t, g_image + GEN + LIST + 0x108u * l, sizeof t);
    memcpy(g_image + GEN + LIST + 0x108u * l, g_image + GEN + LIST + 0x108u * k, sizeof t);
    memcpy(g_image + GEN + LIST + 0x108u * k, t, sizeof t);
}

/* Where the rel32 at `at` of a stub placed at `code` leads, as an offset in the image. */
static uint32_t Leads(const uint8_t* stub, size_t at) { int32_t d; memcpy(&d, stub + at, 4); return (uint32_t)((uintptr_t)stub + at + 4u + (intptr_t)d - (uintptr_t)g_image); }

int main(void)
{
    g_case = "the rule";
    CHECK(co_unlock_rule(0, 0) && co_unlock_rule(2, 2) && !co_unlock_rule(0, 1) && !co_unlock_rule(1, 0));
    CHECK(co_unlock_rule(CO_UNLOCK_NONE, 1) && co_unlock_rule(1, CO_UNLOCK_NONE) && co_unlock_rule(CO_UNLOCK_NONE, CO_UNLOCK_NONE));

    g_case = "the note";
    World(0);
    Note();
    const uintptr_t lg = (uintptr_t)At(LEVEL), gen = (uintptr_t)At(GEN);
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1, CLUSTER(1)) == 0 && co_unlock_noted(lg, gen, 4, 0xB1, CLUSTER(4)) == 1);
    CHECK(co_unlock_noted(lg, gen, 4, 0xB0, CLUSTER(4)) == CO_UNLOCK_NONE);   /* another objective at the record now */
    CHECK(co_unlock_noted(lg, gen, 5, 0xC0, CLUSTER(5)) == CO_UNLOCK_NONE);   /* a side objective: no mission */
    CHECK(co_unlock_noted(lg + 8, gen, 1, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen + 8, 1, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE);
    co_unlock_forget();
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE);

    g_case = "another mission's copy of the id at the record";
    {
        /* mission 1 has a step of mission 0's id 0xA1 (location 4); a reorder brings mission 0's copy to record 4 */
        const uint32_t location[4] = {0, 1, 3, 4}, id[4] = {0xA0, 0xA1, 0xB0, 0xA1};
        const uint64_t cluster[4] = {CLUSTER(0), CLUSTER(1), CLUSTER(3), CLUSTER(4)}, shared[4] = {CLUSTER(0), CLUSTER(1), CLUSTER(3), CLUSTER(1)};
        const uint8_t mission[4] = {0, 0, 1, 1};
        co_unlock_note(lg, gen, location, id, cluster, mission, 4);
        CHECK(co_unlock_noted(lg, gen, 4, 0xA1, CLUSTER(4)) == 1 && co_unlock_noted(lg, gen, 1, 0xA1, CLUSTER(1)) == 0);
        CHECK(co_unlock_noted(lg, gen, 4, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen, 1, 0xA1, CLUSTER(4)) == CO_UNLOCK_NONE);
        /* both copies in one cluster: neither can be told from the other, so neither is noted */
        co_unlock_note(lg, gen, location, id, shared, mission, 4);
        CHECK(co_unlock_noted(lg, gen, 4, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen, 1, 0xA1, CLUSTER(1)) == CO_UNLOCK_NONE);
        CHECK(co_unlock_noted(lg, gen, 0, 0xA0, CLUSTER(0)) == 0 && co_unlock_noted(lg, gen, 3, 0xB0, CLUSTER(3)) == 1);
        /* in play: the main of mission 0 (instance 0) with its step (instance 1, unit 2) moved from record 1 to 4 */
        World(1);
        Put32(GEN + LIST + 0x108u * 4 + 0xD8, 0xA1);
        co_unlock_note(lg, gen, location, id, cluster, mission, 4);
        const uintptr_t m = (uintptr_t)At(MANAGER);
        CHECK(co_unlock_holds(m, 3, 1) == 0 && co_unlock_holds(m, 0, 4) == 0); /* in place: each its own mission */
        co_unlock_note(lg, gen, location, id, cluster, mission, 4);
        Swap(1, 4);
        CHECK(co_unlock_holds(m, 0, 1) == 1 && co_unlock_holds(m, 3, 4) == 1); /* neither taken for the other mission's step */
    }
    co_unlock_forget();

    g_case = "a main waits for its own mission's steps";
    World(1);
    Note();
    const uintptr_t m = (uintptr_t)At(MANAGER);
    CHECK(co_unlock_holds(m, 0, 1) == 1 && co_unlock_holds(m, 0, 2) == 1 && co_unlock_holds(m, 3, 4) == 1);
    CHECK(co_unlock_holds(m, 0, 4) == 0 && co_unlock_holds(m, 3, 1) == 0 && co_unlock_holds(m, 3, 2) == 0);
    /* the game's answer where a mission is not known: a side objective, a location with no note, a unit with none */
    CHECK(co_unlock_holds(m, 0, 5) == 1 && co_unlock_holds(m, 6, 1) == 1 && co_unlock_holds(m, 3, 7) == 1 && co_unlock_holds(m, 7, 4) == 1);
    Put32(GEN + LIST + 0x108u * 4 + 0xD8, 0xEE); /* the record holds another objective now: asked again after a new note */
    Note();
    CHECK(co_unlock_holds(m, 0, 4) == 1);
    co_unlock_forget();
    CHECK(co_unlock_holds(m, 3, 1) == 1); /* no note: every step holds every main */
    Put32(GEN + LIST + 0x108u * 4 + 0xD8, 0xB1);
    Note();
    Put64(LEVEL + 0x2C8u, At(GEN + 0x10000u)); /* another generation block than the noted one */
    CHECK(co_unlock_holds(m, 3, 1) == 1);

    g_case = "a miss for want of a unit or a location is asked again";
    World(1);
    Note();
    Put64(GEN + LIST + 0x108u * 4 + 0x98, 0); /* mission 1's step: its location does not hold the unit yet */
    g_noUnits = 1;                            /* and no unit is found yet */
    CHECK(co_unlock_holds(m, 0, 4) == 1);     /* the game's answer meanwhile */
    g_noUnits = 0;
    CHECK(co_unlock_holds(m, 0, 4) == 1);
    Put64(GEN + LIST + 0x108u * 4 + 0x98, At(UNIT_RECORDS + 0x100u * 4));
    CHECK(co_unlock_holds(m, 0, 4) == 0);     /* found now */
    Put64(GEN + LIST + 0x108u * 4 + 0x98, 0);
    CHECK(co_unlock_holds(m, 0, 4) == 0);     /* a hit is kept */

    g_case = "the activation pass's stub";
    World(0);
    uint8_t* a = g_image + 0x3100000u; /* within a jump of the sites, as the install's page is */
    const uintptr_t flag = (uintptr_t)(g_image + 0x3001140u), holds = (uintptr_t)co_unlock_holds, game = (uintptr_t)g_image;
    CHECK(co_unlock_stub_a(a, (uintptr_t)a, game, flag, holds) == CO_UNLOCK_A_STUB);
    CHECK(a[0] == 0x80 && a[1] == 0x3D && Leads(a, 2) + 1u == 0x3001140u && a[7] == 0x75 && a[8] == 26);
    CHECK(memcmp(a + 9, kCoUnlockA, sizeof kCoUnlockA) == 0 && a[30] == 0xE9 && Leads(a, 31) == CO_UNLOCK_A_BACK); /* a normal mission: the game's */
    CHECK(memcmp(a + 35, kCoUnlockA, 8) == 0 && memcmp(a + 43, kCoUnlockA + 8, 5) == 0 && a[48] == 0x0F && a[49] == 0x84 && Leads(a, 50) == CO_UNLOCK_A_BACK);
    CHECK(a[54] == 0x52 && a[84] == 0x5A && memcmp(a + 70, &holds, 8) == 0 && a[87] == 0x0F && a[88] == 0x84 && Leads(a, 89) == CO_UNLOCK_A_BACK);
    CHECK(a[93] == 0xBE && a[94] == 1 && a[98] == 0xE9 && Leads(a, 99) == CO_UNLOCK_A_BACK);

    g_case = "the waiting pass's stub";
    uint8_t* b = a + CO_UNLOCK_A_STUB;
    CHECK(co_unlock_stub_b(b, (uintptr_t)b, game, flag, holds) == CO_UNLOCK_B_STUB);
    CHECK(b[0] == 0x80 && Leads(b, 2) + 1u == 0x3001140u && b[7] == 0x75 && b[8] == 20);
    CHECK(memcmp(b + 9, kCoUnlockB, 9) == 0 && b[18] == 0x0F && b[19] == 0x85 && Leads(b, 20) == CO_UNLOCK_B_HOLD && b[24] == 0xE9 && Leads(b, 25) == CO_UNLOCK_B_NEXT);
    CHECK(CO_UNLOCK_B_SITE + sizeof kCoUnlockB + kCoUnlockB[10] == CO_UNLOCK_B_HOLD); /* the game's jne leads where the stub's does */
    CHECK(memcmp(b + 29, kCoUnlockB, 9) == 0 && b[38] == 0x0F && b[39] == 0x84 && Leads(b, 40) == CO_UNLOCK_B_NEXT && memcmp(b + 74, &holds, 8) == 0);
    CHECK(b[64] == 0x48 && b[68] == 0x80 && b[100] == 0x0F && b[101] == 0x85 && Leads(b, 102) == CO_UNLOCK_B_HOLD && b[106] == 0xE9 && Leads(b, 107) == CO_UNLOCK_B_NEXT);

    g_case = "the install";
    World(0);
    CHECK(co_unlock_install(game, 0) != NULL && g_fake.wroteCount == 0);
    CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL);
    {
        const uint8_t* page = co_unlock_test_page();
        int32_t d;
        CHECK(g_image[CO_UNLOCK_A_SITE] == 0xE9 && g_image[CO_UNLOCK_A_SITE + 20] == 0x90 && g_image[CO_UNLOCK_B_SITE] == 0xE9 && g_image[CO_UNLOCK_B_SITE + 10] == 0x90);
        memcpy(&d, g_image + CO_UNLOCK_A_SITE + 1, 4); CHECK(g_image + CO_UNLOCK_A_SITE + 5 + d == page);
        memcpy(&d, g_image + CO_UNLOCK_B_SITE + 1, 4); CHECK(g_image + CO_UNLOCK_B_SITE + 5 + d == page + 0x80);
        CHECK(page[0x1000] == 0 && memcmp(page + 9, kCoUnlockA, sizeof kCoUnlockA) == 0);
        co_unlock_live(1); CHECK(page[0x1000] == 1);
        co_unlock_live(0); CHECK(page[0x1000] == 0); /* a normal mission and the ship: the game's bytes run */
    }
    CHECK(fake_host_detour(CO_UNLOCK_MARKERS) != NULL && fake_host_detour(CO_UNLOCK_LINES) != NULL);
    {
        World(0);
        memset(g_image + CO_UNLOCK_B_SITE, 0xCC, 1); /* another's code at the waiting pass: nothing is written */
        CHECK(co_unlock_install(game, LIST) != NULL && g_fake.wroteCount == 0 && co_unlock_test_page() == NULL);
    }

    g_case = "the war table's lines";
    World(1);
    Note();
    /* the shown preview is record 0: [R] 3, its ids at R+0x2234 */
    const uint32_t record = WORLD + 0xAE2A0u;
    Put64(0x347CEE8u, At(PREVIEWS)); Put32(PREVIEWS + 0x4168CCu, 0); Put32(record, 3);
    /* a descriptor objective with no place, mission 0's main and step, mission 1's main and step */
    g_outputs = 5;
    g_pos[0] = 0; g_pos[1] = Pos(100, 200); g_pos[2] = Pos(110, 210); g_pos[3] = Pos(300, 400); g_pos[4] = Pos(310, 410);
    for (uint32_t k = 0; k < 5; k++) g_ids[k] = 0x900u + k;
    uint64_t pos[8];
    CHECK(co_unlock_test_markers(lg, pos, NULL, (uint32_t*)(g_image + record + 0x2234u)) == 5);
    g_holo = calloc(1, 0x8B740u);
    *(uint32_t*)(g_holo + 0x8B730u) = 5;
    SLOT(1)[0x17C] = 1; /* the game's own no-line slot stays one */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x05u && g_alpha == 0.5f);              /* mission 0's step: not to mission 1's main and step */
    CHECK(SLOT(3)[0x17C] == 0 && SLOT(4)[0x17C] == 0 && SLOT(1)[0x17C] == 1);
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(4), 0, 0.5f);
    CHECK(g_drawn == 0x19u);                                 /* mission 1's step: slots 0, 3, 4 */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(0), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);                                 /* a slot of no mission hides nothing */
    *(uint32_t*)(g_image + record + 0x2234u + 12) = 0x777;   /* slot 3 is another marker now */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x0Du);
    Put32(record, 2);                                        /* not a shown record: the game's lines */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);
    Put32(record, 3);
    co_unlock_forget();                                      /* a generation with no note */
    CHECK(co_unlock_test_markers(lg, pos, NULL, (uint32_t*)(g_image + record + 0x2234u)) == 5);
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);
    free(g_holo);

    g_case = "which slots a step skips";
    {
        const uint8_t mission[6] = {0, 0, 1, 1, CO_UNLOCK_NONE, 2};
        CHECK(co_unlock_hide(mission, 6, 0) == 0x2Cu && co_unlock_hide(mission, 6, 2) == 0x23u && co_unlock_hide(mission, 6, 4) == 0);
        CHECK(co_unlock_hide(mission, 6, 6) == 0 && co_unlock_hide(mission, 33, 0) == 0);
    }

    if (g_failed == 0) printf("unlock: all checks passed\n");
    return g_failed != 0;
}
