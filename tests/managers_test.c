/* The eight-slot player managers against a stand-in game image: the code changes only when every byte is the reviewed
 * one, and a refusal or a failed write leaves the image exactly as it was. The score manager's loot rows move behind it
 * with the rest, their loops count to eight, and the mission report's outcome code stays four. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_managers_test_fill(uint8_t* image);
void co_managers_test_reset(void);
int co_managers_test_state(void);
uintptr_t co_managers_test_place(unsigned manager);
unsigned co_managers_test_sites(void);
int co_managers_test_write_of(uint32_t rva);
int co_managers_moved(void);
void co_pods_test_reset(void);

#define STATE_CTOR 0xFDA190u
#define STATE_BLOCK 0x40u
#define FIRST_STATE 0x100000u       /* where the stand-in entity state lies in the image */
#define SECOND_STATE 0x200000u
#define CAPACITY_SITE 0x51AC08u     /* ship_customization's cmp eax,4: the value is the third byte */
#define BLOCK_SITE 0x568448u        /* lea rax,[block + ship_modules_sync]: disp32 at +3 */
#define BLOCK_MANAGER 5u
#define BLOCK_OFFSET 0x4148u
#define ARRAY_SITE 0xACC65Bu        /* lea r12,[player manager + 0x2C8]: the array is at 0x828 now */
#define MOVED_BLOCK_SITE 0x5704E5u  /* score's count of the most slots used, from the block; nothing moves inside */
#define LOOT_SLOT_SITE 0x6308F9u    /* lea r8,[score + 0xC440]: the loot rows by score slot, at 0x37F20 now */
#define LOOT_INDEX_SITE 0x6312A2u   /* mov [rbp+0xC4A0],rsi: the loot rows by player index, at 0x37FE0 now */
#define LOOT_LOOP_SITE 0x630980u    /* lea r8d,[rax+4]: a sum's loop count, the value is the fourth byte */
#define REPORT_LOOP_SITE 0x1380C68u /* mov r14d,4: the report's loop count, also its outcome code */
#define OUTCOME_SITE 0x1380F0Bu     /* mov eax,r14d: the outcome code */
#define SCORE_SIZE 0x380A0u

typedef uint64_t (*CtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    const HfApi* api;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_managers_test_fill(g_fake.image);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
}

static void BuildState(uintptr_t at)
{
    CtorFn ctor = (CtorFn)fake_host_detour(STATE_CTOR);
    if (ctor != NULL) ctor((uint64_t)(uintptr_t)(g_fake.image + at), 0, 0, 0, 0, 0);
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0; }
static uint32_t U32(uintptr_t rva) { uint32_t v; memcpy(&v, g_fake.image + rva, 4); return v; }

/* The block site's displacement must lead from the block to the mod's copy of that manager. */
static int ReachesManager(uintptr_t state)
{
    const uintptr_t block = (uintptr_t)g_fake.image + state + STATE_BLOCK;
    return co_managers_test_place(BLOCK_MANAGER) != 0 && block + (int32_t)U32(BLOCK_SITE + 3) == co_managers_test_place(BLOCK_MANAGER);
}

int main(void)
{
    const int sites = (int)co_managers_test_sites();
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;

    Start("the reviewed game");
    CHECK(g_fake.writes == 0);
    BuildState(FIRST_STATE);
    CHECK(co_managers_test_state() == 1 && g_fake.writes == sites);
    CHECK(fake_host_logged("players: 7 component managers have 8 slots"));
    CHECK(g_fake.image[CAPACITY_SITE + 2] == 8 && U32(ARRAY_SITE + 3) == 0x828u);
    CHECK(ReachesManager(FIRST_STATE));
    /* the loot rows: eight of each behind the manager, loops of eight, and push 4; pop rax for the outcome code */
    {
        static const uint8_t kOutcome[] = {0x6A, 0x04, 0x58, 0x89};
        CHECK(U32(LOOT_SLOT_SITE + 3) == 0x37F20u && U32(LOOT_INDEX_SITE + 3) == 0x37FE0u && 0x37FE0u + 8 * 0x18u == SCORE_SIZE);
        CHECK(g_fake.image[LOOT_LOOP_SITE + 3] == 8 && g_fake.image[REPORT_LOOP_SITE + 2] == 8 && U32(REPORT_LOOP_SITE + 2) == 8);
        CHECK(memcmp(g_fake.image + OUTCOME_SITE, kOutcome, sizeof kOutcome) == 0 && co_managers_moved());
        CHECK(co_managers_test_place(1) - co_managers_test_place(0) >= SCORE_SIZE);
    }
    /* every place of one manager keeps its distance to the others */
    CHECK(U32(MOVED_BLOCK_SITE + 2) - 0x7FF06Cu == (uint32_t)(co_managers_test_place(0) - ((uintptr_t)g_fake.image + FIRST_STATE + STATE_BLOCK + 0x7C7368u)));

    BuildState(FIRST_STATE);
    CHECK(co_managers_test_state() == 1 && g_fake.writes == sites);
    BuildState(SECOND_STATE);
    CHECK(co_managers_test_state() == 1 && ReachesManager(SECOND_STATE) && g_fake.image[CAPACITY_SITE + 2] == 8);

    Start("one changed site");
    g_fake.image[CAPACITY_SITE + 2] = 5;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    BuildState(FIRST_STATE);
    CHECK(co_managers_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("players: the code at game+0x51AC08 is not the reviewed one; four player slots as before"));

    Start("one changed instruction before a site");
    g_fake.image[ARRAY_SITE] ^= 0x01;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    BuildState(FIRST_STATE);
    CHECK(co_managers_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("game+0xACC65B is not the reviewed one"));

    /* The loot rows' sites are refused as the others: a changed byte anywhere leaves the managers and the outcome code. */
    {
        static const struct { uint32_t rva, at; } kLoot[] = {{LOOT_SLOT_SITE, 1}, {LOOT_INDEX_SITE, 4}, {LOOT_LOOP_SITE, 3}, {OUTCOME_SITE, 2}, {OUTCOME_SITE, 3}};
        for (size_t i = 0; i < sizeof kLoot / sizeof kLoot[0]; i++)
        {
            static char line[80];
            Start("one changed loot site");
            g_fake.image[kLoot[i].rva + kLoot[i].at] ^= 0x01;
            memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
            BuildState(FIRST_STATE);
            snprintf(line, sizeof line, "game+0x%X is not the reviewed one", kLoot[i].rva);
            CHECK(co_managers_test_state() == -1 && !co_managers_moved() && Unchanged() && g_fake.writes == 0 && fake_host_logged(line));
        }
        const uint32_t kWrites[] = {LOOT_SLOT_SITE, LOOT_INDEX_SITE, LOOT_LOOP_SITE, REPORT_LOOP_SITE, OUTCOME_SITE};
        for (size_t i = 0; i < sizeof kWrites / sizeof kWrites[0]; i++)
        {
            Start("a loot site's write fails");
            g_fake.failWriteAt = co_managers_test_write_of(kWrites[i]);
            CHECK(g_fake.failWriteAt >= 0);
            BuildState(FIRST_STATE);
            CHECK(co_managers_test_state() == -1 && !co_managers_moved() && Unchanged());
            CHECK(fake_host_logged("could not be written; four player slots as before"));
        }
    }

    for (int at = 0; at < sites; at += (at < 3 || at >= sites - 3) ? 1 : 97)
    {
        static char name[64];
        snprintf(name, sizeof name, "write %d fails", at);
        Start(name);
        g_fake.failWriteAt = at;
        BuildState(FIRST_STATE);
        CHECK(co_managers_test_state() == -1 && Unchanged());
        CHECK(fake_host_logged("could not be written; four player slots as before"));
    }

    printf("%s\n", g_failed ? "managers: failures" : "managers: all cases passed");
    return g_failed != 0;
}
