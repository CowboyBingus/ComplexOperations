/* The director's patrol gate: the game's jae becomes a stub that runs the game's own test and, while the flag is set,
 * takes the same jump a cap of 0 takes. Run as code: the gate's compare with alive patrol units and a scaled cap. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_patrols.h"

void co_patrols_test_reset(void);
const uint8_t* co_patrols_test_page(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define RUN 0x3300000u
enum { LOOP = 1, PAST = 2 };

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_patrols_test_reset();
    memcpy(g_fake.image + CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp);
    memcpy(g_fake.image + CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate);
    memcpy(g_fake.image + CO_PATROL_NEXT, kCoPatrolNext, sizeof kCoPatrolNext);
    memcpy(g_fake.image + CO_PATROL_SKIP, kCoPatrolSkip, sizeof kCoPatrolSkip);
    g_fake.writes = 0;
    return g_fake.image;
}

/* The gate as the director reaches it: rax the director (alive patrol units at +0x614), r12d the scaled cap. Where
 * the game goes on (the loop's way in, or past it) the harness returns which; r12 is the caller's and kept. */
static int Gate(uint8_t* image, uint32_t alive, uint32_t cap)
{
    static uint8_t director[0x700];
    static const uint8_t enter[] = {0x41, 0x54, 0x48, 0x89, 0xC8, 0x41, 0x89, 0xD4, 0xE9, 0, 0, 0, 0};
    static const uint8_t loop[] = {0xB8, LOOP, 0, 0, 0, 0x41, 0x5C, 0xC3}, past[] = {0xB8, PAST, 0, 0, 0, 0x41, 0x5C, 0xC3};
    const int32_t rel = (int32_t)(CO_PATROL_CMP - (RUN + sizeof enter));
    memcpy(image + RUN, enter, sizeof enter); memcpy(image + RUN + 9, &rel, 4);
    memcpy(image + CO_PATROL_NEXT, loop, sizeof loop);
    memcpy(image + CO_PATROL_SKIP, past, sizeof past);
    memcpy(director + 0x614, &alive, 4);
    DWORD old;
    if (!VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old)) return 0;
    return ((int (*)(void*, uint32_t))(void*)(image + RUN))(director, cap);
}
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

int main(void)
{
    static const uint32_t cases[][2] = {{0, 0}, {0, 41}, {40, 41}, {41, 41}, {42, 41}, {0x80000000u, 1}, {7, 0}};
    int game[sizeof cases / sizeof cases[0]], none[sizeof cases / sizeof cases[0]];

    uint8_t* image = Reset("the game's gate");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        image = Reset("the game's gate");
        game[i] = Gate(image, cases[i][0], cases[i][1]);
        none[i] = Gate(image, cases[i][0], 0); /* what a row with a cap of 0 gives */
        CHECK(game[i] == (cases[i][0] < cases[i][1] ? LOOP : PAST) && none[i] == PAST);
    }

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        image = Reset("the stub");
        co_patrols_live(1);
        CHECK(g_fake.writes == 0); /* nothing before the install */
        CHECK(co_patrols_check((uintptr_t)image) == NULL && co_patrols_install((uintptr_t)image) == NULL && g_fake.writes == 1);
        const uint8_t* page = co_patrols_test_page();
        CHECK(page != NULL);
        if (page == NULL) continue;
        CHECK(image[CO_PATROL_GATE] == 0xE9 && Leads(image + CO_PATROL_GATE + 1) == (uintptr_t)page && image[CO_PATROL_GATE + 5] == 0x90);
        CHECK(memcmp(image + CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp) == 0);
        CHECK(Leads(page + 2) == (uintptr_t)image + CO_PATROL_SKIP && Leads(page + 15) == (uintptr_t)image + CO_PATROL_SKIP &&
            Leads(page + 20) == (uintptr_t)image + CO_PATROL_NEXT && Leads(page + 8) + 1u == (uintptr_t)page + 0x1000u);
        /* the flag starts at 0: every mission's gate is the game's */
        CHECK(page[0x1000] == 0 && Gate(image, cases[i][0], cases[i][1]) == game[i]);
        co_patrols_live(1); /* a complex mission: what a cap of 0 gives, whatever the row's */
        CHECK(page[0x1000] == 1 && Gate(image, cases[i][0], cases[i][1]) == none[i]);
        co_patrols_live(0); /* the ship and any other mission: the game's again */
        CHECK(page[0x1000] == 0 && Gate(image, cases[i][0], cases[i][1]) == game[i]);
        CHECK(co_patrols_install((uintptr_t)image) == NULL && g_fake.writes == 1); /* once */
    }

    const struct { uint32_t rva; size_t size; } pins[] = {{CO_PATROL_CMP, 7}, {CO_PATROL_GATE, 6}, {CO_PATROL_NEXT, 3}, {CO_PATROL_SKIP, 3}};
    for (size_t p = 0; p < sizeof pins / sizeof pins[0]; p++)
        for (size_t i = 0; i < pins[p].size; i++)
        {
            image = Reset("a site is another's code");
            image[pins[p].rva + i] ^= 0x10;
            uint8_t before[6];
            memcpy(before, image + CO_PATROL_GATE, sizeof before);
            CHECK(co_patrols_check((uintptr_t)image) != NULL && co_patrols_install((uintptr_t)image) != NULL && g_fake.writes == 0);
            CHECK(memcmp(before, image + CO_PATROL_GATE, sizeof before) == 0 && co_patrols_test_page() == NULL);
        }

    image = Reset("the write is refused");
    g_fake.failWriteAt = 0;
    CHECK(co_patrols_install((uintptr_t)image) != NULL && memcmp(image + CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate) == 0 && co_patrols_test_page() == NULL);
    co_patrols_live(1);
    CHECK(Gate(image, 3, 41) == LOOP);

    if (g_failed) { printf("patrols: %d checks failed\n", g_failed); return 1; }
    printf("patrols: all cases passed\n");
    return 0;
}
