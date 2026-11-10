/* The eight-slot player managers against a stand-in game image: the code changes only when every byte is the reviewed
 * one, and a refusal or a failed write leaves the image exactly as it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_managers_test_fill(uint8_t* image);
void co_managers_test_reset(void);
int co_managers_test_state(void);
uintptr_t co_managers_test_place(unsigned manager);
unsigned co_managers_test_sites(void);
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
