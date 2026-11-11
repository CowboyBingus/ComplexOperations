/* The avatar manager against a stand-in game image. A build for eight moves it to sixteen slots with the other managers:
 * its capacity checks say 16, the block's sites lead to the new place, and a changed byte or a failed write leaves the
 * image as it was. A build for four leaves every avatar site as the game has it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_managers_test_fill(uint8_t* image);
void co_managers_test_reset(void);
int co_managers_test_state(void);
uintptr_t co_managers_test_place(unsigned manager);
unsigned co_managers_test_sites(void);
unsigned co_managers_test_caves(void);
int co_managers_test_index(const char* name);
int co_managers_test_write_of(uint32_t rva);
int co_managers_moved(void);
void co_pods_test_reset(void);

#define STATE_CTOR 0xFDA190u
#define STATE_BLOCK 0x40u
#define FIRST_STATE 0x100000u
#define SECOND_STATE 0x200000u
#define AVATAR_OFFSET 0x956128u     /* the avatar manager in the component block */
#define BLOCK_SITE 0x568E66u        /* lea rax,[rbx + avatar]: the bind of the global, disp32 at +3 */
#define INNER_SITE 0x5758CBu        /* lea rcx,[r13 + avatar + 0x547D08]: a field that stays where it is in the manager */
#define ARRAY_SITE 0x825E1Au        /* mov edx,[rcx+rdx*1+0x547898]: in A3, which moves behind the old end */
#define ARRAY_NEW 0xFD85F8u
#define FOLD_SITE 0xA4F4FDu         /* add r9,0xE6: the slot's start folded into one constant with the array behind it */
#define FOLD_NEW 0x2A59Au
#define OTHER_CAP_SITE 0x51AC08u    /* ship_customization's cmp eax,4: the other managers say 8 in both builds */
static const uint32_t kCaps[] = {0x53F27Bu, 0x53F3CAu, 0x83AF1Au, 0x83B0ABu}; /* cmp eax,8: the value is the third byte */

typedef uint64_t (*CtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static uint8_t* g_vanilla;
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


/* Every avatar site probed here holds the game's bytes. */
static int AvatarAsGame(void)
{
    static const uint32_t kSites[] = {BLOCK_SITE, INNER_SITE, ARRAY_SITE, FOLD_SITE};
    for (size_t i = 0; i < sizeof kSites / sizeof kSites[0]; i++)
        if (memcmp(g_fake.image + kSites[i], g_vanilla + kSites[i], 8) != 0) return 0;
    for (size_t i = 0; i < sizeof kCaps / sizeof kCaps[0]; i++)
        if (memcmp(g_fake.image + kCaps[i], g_vanilla + kCaps[i], 3) != 0) return 0;
    return 1;
}

#if CO_PLAYERS >= 8
static int Unchanged(void) { return memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0; }
static uint32_t U32(uintptr_t rva) { uint32_t v; memcpy(&v, g_fake.image + rva, 4); return v; }

/* The avatar sites as the move writes them for the state at `state`. */
static int AvatarMoved(uintptr_t state, int avatar)
{
    const uintptr_t block = (uintptr_t)g_fake.image + state + STATE_BLOCK, place = co_managers_test_place((unsigned)avatar);
    int ok = 1;
    for (size_t i = 0; i < sizeof kCaps / sizeof kCaps[0]; i++) ok &= g_fake.image[kCaps[i] + 2] == 16;
    ok &= block + (int32_t)U32(BLOCK_SITE + 3) == place;
    ok &= block + (int32_t)U32(INNER_SITE + 3) == place + (0xE9DE30u - AVATAR_OFFSET);
    ok &= U32(ARRAY_SITE + 3) == ARRAY_NEW;
    ok &= U32(FOLD_SITE + 3) == FOLD_NEW;
    return ok;
}
#endif

int main(void)
{
    const int writes = (int)(co_managers_test_sites() + co_managers_test_caves());
    const int avatar = co_managers_test_index("avatar");
    g_before = malloc(FAKE_IMAGE_SIZE);
    g_vanilla = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL || g_vanilla == NULL) return 2;

    Start("the reviewed game");
    memcpy(g_vanilla, g_fake.image, FAKE_IMAGE_SIZE);
    BuildState(FIRST_STATE);
    CHECK(co_managers_test_state() == 1 && g_fake.writes == writes && g_fake.image[OTHER_CAP_SITE + 2] == 8);
    CHECK(fake_host_logged("players: 7 component managers have 8 slots"));
#if CO_PLAYERS >= 8
    CHECK(avatar == 7 && AvatarMoved(FIRST_STATE, avatar));
    CHECK(fake_host_logged("players: the avatar manager has 16 slots for players and their bodies (2700 code sites changed)"));
    /* the managers' places do not overlap */
    CHECK(co_managers_test_place((unsigned)avatar) >= co_managers_test_place(6) + 0x9E8u);
#else
    CHECK(avatar == -1 && AvatarAsGame() && !fake_host_logged("avatar manager"));
#endif

    /* A state built elsewhere gets the sites written again; the old place's are undone first. */
    BuildState(SECOND_STATE);
    CHECK(co_managers_test_state() == 1);
#if CO_PLAYERS >= 8
    CHECK(AvatarMoved(SECOND_STATE, avatar));
#else
    CHECK(AvatarAsGame());
#endif

    /* A changed byte at an avatar site, in the value or before it: a build for eight refuses the whole move, a build for
     * four does not read it. */
    {
        static const struct { uint32_t rva, at; } kChanged[] = {{0x53F27Bu, 2}, {0x83B0ABu, 0}, {BLOCK_SITE, 1}, {ARRAY_SITE, 5}, {FOLD_SITE, 3}};
        for (size_t i = 0; i < sizeof kChanged / sizeof kChanged[0]; i++)
        {
            static char line[96];
            Start("one changed avatar site");
            g_fake.image[kChanged[i].rva + kChanged[i].at] ^= 0x01;
            memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
            BuildState(FIRST_STATE);
            snprintf(line, sizeof line, "players: the code at game+0x%X is not the reviewed one; four player slots as before", kChanged[i].rva);
#if CO_PLAYERS >= 8
            CHECK(co_managers_test_state() == -1 && !co_managers_moved() && Unchanged() && g_fake.writes == 0 && fake_host_logged(line));
#else
            CHECK(co_managers_test_state() == 1 && !fake_host_logged(line));
            CHECK(memcmp(g_fake.image + kChanged[i].rva, g_before + kChanged[i].rva, 8) == 0);
#endif
        }
    }

    /* A failed write at an avatar site puts back every site written before it. */
    {
        const uint32_t kWrites[] = {0x53F27Bu, BLOCK_SITE, ARRAY_SITE, FOLD_SITE, 0x83B0ABu};
        for (size_t i = 0; i < sizeof kWrites / sizeof kWrites[0]; i++)
        {
            Start("an avatar site's write fails");
            g_fake.failWriteAt = co_managers_test_write_of(kWrites[i]);
#if CO_PLAYERS >= 8
            CHECK(g_fake.failWriteAt >= 0);
            BuildState(FIRST_STATE);
            CHECK(co_managers_test_state() == -1 && !co_managers_moved() && Unchanged() && AvatarAsGame());
            CHECK(fake_host_logged("could not be written; four player slots as before"));
#else
            CHECK(g_fake.failWriteAt == -1);
#endif
        }
    }

    printf("%s\n", g_failed ? "avatars: failures" : "avatars: all cases passed");
    return g_failed != 0;
}
