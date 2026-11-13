/* The eight-pod code against a stand-in game image: it changes the code only when every byte is the reviewed one,
 * and a refusal or a failed write leaves the image exactly as it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_pods_test_fill(uint8_t* image);
void co_pods_test_reset(void);
int co_pods_test_state(void);
uintptr_t co_pods_test_manager(void);

#define STATE_CTOR 0xFDA190u
#define STATE_GLOBAL 0x346BF98u
#define POD_LAUNCH 0x11CE740u
#define CAPACITY_SITE 0x54AAF8u     /* cmp eax,4: the value is the third byte */
#define BLOCK_SITE 0x556AB2u        /* reaches the manager from the component block: disp32 at +3 */
#define REWRITE_SITE 0x660CA9u
#define CAMERA_PICK 0x11BBF97u
#define MANAGER 0xEEF968u
#define STATE_BLOCK 0x40u
#define FIRST_STATE 0x100000u       /* where the stand-in entity state lies in the image */
#define SECOND_STATE 0x200000u
#define SITES 131
#define REWRITES 4

typedef uint64_t (*CtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* A game with the reviewed code at every place the mod looks, and nothing loaded yet. */
static const HfApi* Start(const char* name, const char* build)
{
    const HfApi* api;
    g_case = name;
    co_pods_test_reset();
    api = fake_host_reset(build);
    co_pods_test_fill(g_fake.image);
    return api;
}

/* The mod's init and the game-loaded event; then the image is remembered as "before". */
static int Load(const HfApi* api)
{
    const int result = hellforge_mod_init(api, g_mod);
    if (result == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    return result;
}

static void BuildState(uintptr_t at)
{
    CtorFn ctor = (CtorFn)fake_host_detour(STATE_CTOR);
    if (ctor != NULL) ctor((uint64_t)(uintptr_t)(g_fake.image + at), 0, 0, 0, 0, 0);
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0; }

static uint32_t U32(uintptr_t rva) { uint32_t v; memcpy(&v, g_fake.image + rva, 4); return v; }

/* The block site's displacement must lead from the block to the mod's manager. */
static int ReachesManager(uintptr_t state)
{
    const uintptr_t block = (uintptr_t)g_fake.image + state + STATE_BLOCK;
    return block + (int32_t)U32(BLOCK_SITE + 3) == co_pods_test_manager() && co_pods_test_manager() != 0;
}

int main(void)
{
    const HfApi* api;
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;

    api = Start("another game build", "26251122");
    CHECK(Load(api) == 1);
    CHECK(fake_host_logged("needs game build 25480438; this is 26251122"));
    CHECK(g_fake.loaded == NULL && g_fake.hookCount == 0 && g_fake.writes == 0);

    api = Start("the reviewed game", CO_GAME_BUILD);
    CHECK(Load(api) == 0);
    CHECK(g_fake.hookCount == 14 && g_fake.writes == 0); /* six for the pods, four for the lists' counts, one for the session lists, three for the voice log */
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == 1);
    CHECK(fake_host_logged("have 8 slots (135 code sites changed)"));
    CHECK(g_fake.image[CAPACITY_SITE + 2] == 8);
    CHECK(ReachesManager(FIRST_STATE));
    CHECK(g_fake.image[CAMERA_PICK] == 0x83 && g_fake.image[CAMERA_PICK + 1] == 0xF9 && g_fake.image[CAMERA_PICK + 2] == 0x08);
    CHECK(g_fake.writes == SITES + REWRITES + 1);

    /* the game builds its state again at the same place: nothing is written twice */
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == 1 && g_fake.writes == SITES + REWRITES + 1);
    /* and at another place: the code goes back and is written for the new block */
    BuildState(SECOND_STATE);
    CHECK(co_pods_test_state() == 1 && ReachesManager(SECOND_STATE));
    CHECK(g_fake.image[CAPACITY_SITE + 2] == 8);

    api = Start("one changed site", CO_GAME_BUILD);
    g_fake.image[CAPACITY_SITE + 2] = 5;
    CHECK(Load(api) == 0);
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("the code at game+0x54AAF8 is not the reviewed one; four pods as before"));

    api = Start("one changed instruction before a site", CO_GAME_BUILD);
    g_fake.image[BLOCK_SITE] ^= 0x01;
    CHECK(Load(api) == 0);
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("game+0x556AB2 is not the reviewed one"));

    api = Start("one changed rewritten instruction", CO_GAME_BUILD);
    g_fake.image[REWRITE_SITE + 4] = 0x90;
    CHECK(Load(api) == 0);
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("game+0x660CA9 is not the reviewed one"));

    /* a write that fails anywhere in the sequence: everything written before it goes back */
    for (int at = 0; at < SITES + REWRITES; at += (at < 3 || at >= SITES - 2) ? 1 : 16)
    {
        static char name[64];
        snprintf(name, sizeof name, "write %d fails", at);
        api = Start(name, CO_GAME_BUILD);
        CHECK(Load(api) == 0);
        g_fake.failWriteAt = at;
        BuildState(FIRST_STATE);
        CHECK(co_pods_test_state() == -1 && Unchanged());
        CHECK(fake_host_logged("could not be written; four pods as before"));
    }

    api = Start("the camera choice differs", CO_GAME_BUILD);
    g_fake.image[CAMERA_PICK + 10] ^= 0xFF;
    CHECK(Load(api) == 0);
    BuildState(FIRST_STATE);
    CHECK(co_pods_test_state() == 1 && g_fake.image[CAPACITY_SITE + 2] == 8);
    CHECK(fake_host_logged("pods 5 to 8 have no briefing camera"));
    CHECK(memcmp(g_before + CAMERA_PICK, g_fake.image + CAMERA_PICK, 69) == 0);

    api = Start("the game already runs", CO_GAME_BUILD);
    g_fake.image[STATE_GLOBAL] = 1;
    CHECK(Load(api) == 0);
    CHECK(g_fake.hookCount == 8 && fake_host_logged("already running when the mod loaded"));

    api = Start("the constructor is other code", CO_GAME_BUILD);
    g_fake.noMatch[0] = STATE_CTOR;
    CHECK(Load(api) == 0);
    CHECK(g_fake.hookCount == 8 && fake_host_logged("not the game code the mod was made for"));

    api = Start("the launch function is other code", CO_GAME_BUILD);
    g_fake.noMatch[0] = POD_LAUNCH;
    CHECK(Load(api) == 0);
    CHECK(g_fake.hookCount == 9 && fake_host_logged("no new pods"));
    BuildState(FIRST_STATE);
    /* eight slots without new pods: the camera choice stays the game's */
    CHECK(co_pods_test_state() == 1 && memcmp(g_before + CAMERA_PICK, g_fake.image + CAMERA_PICK, 69) == 0);

    api = Start("a pod hook cannot be placed", CO_GAME_BUILD);
    g_fake.noHook = POD_LAUNCH;
    CHECK(Load(api) == 0);
    CHECK(fake_host_logged("a hook for the new pods could not be placed; no new pods"));

    api = Start("the constructor cannot be hooked", CO_GAME_BUILD);
    g_fake.noHook = STATE_CTOR;
    CHECK(Load(api) == 0);
    CHECK(co_pods_test_state() == -1 && fake_host_logged("the constructor could not be hooked"));
    CHECK(Unchanged());

    printf("%s\n", g_failed ? "pods: failures" : "pods: all cases passed");
    return g_failed != 0;
}
