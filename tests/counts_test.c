/* The session lists' counts against a stand-in game image: sites, flag clears and caves are written behind the move,
 * a failed write takes all of them back, and the mod's own slot and record code handles entries 4 to 7. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
int co_session_test_state(void);
uintptr_t co_session_test_extension(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
uintptr_t co_counts_test_caves(void);
unsigned co_counts_test_sites(void);
uint32_t co_counts_test_ready(uintptr_t sync);
uint32_t co_counts_test_free(uintptr_t sync);
uint64_t co_counts_test_find(uint64_t key);
void co_counts_test_gone(uintptr_t sync, uint64_t key);
uint64_t co_counts_test_last(uintptr_t context);
void co_pods_test_reset(void);
void co_managers_test_reset(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define LOOP_SITE 0x108AEE7u        /* cmp eax,4 of a slot loop */
#define CLEAR_SITE 0x1088B8Au       /* xor eax,eax; mov [rdi+flags],eax */
#define SIZE_SITE 0x108984Cu        /* mov [r13+flags],ecx: the session step rewrites its distance */
#define CAVE_SITE 0x108A3F8u        /* test rcx,rcx; jne: the party record lookup */
#define LAST_CAVE 0x108D368u

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    const HfApi* api;
    const uint64_t none = 0;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    co_counts_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_session_test_fill(g_fake.image);
    co_counts_test_fill(g_fake.image);
    memcpy(g_fake.image + SESSION_GLOBAL, &none, 8);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
}

static void Init(uint32_t at)
{
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + at);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
}

static int AsTheGameHasIt(void)
{
    const uint8_t* image = g_fake.image;
    return image[LOOP_SITE + 2] == 4 && image[CLEAR_SITE] == 0x33 && image[CLEAR_SITE + 2] == 0x89 && image[SIZE_SITE] == 0x41 &&
           image[CAVE_SITE] == 0x48 && image[LAST_CAVE] == 0x48 && image[LAST_CAVE + 7] == 0;
}

int main(void)
{
    const int moves = (int)(co_session_test_sites() + co_session_test_stub_count());
    int writes;

    Start("the reviewed game");
    CHECK(g_fake.writes == 0 && AsTheGameHasIt());
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1);
    CHECK(fake_host_logged("session: the session lists count to eight"));
    writes = g_fake.writes;
    CHECK(writes >= moves + (int)co_counts_test_sites());
    {
        const uint8_t* image = g_fake.image;
        const uintptr_t ctx = (uintptr_t)image + CONTEXT, ext = co_session_test_extension(), caves = co_counts_test_caves();
        int32_t distance, to;
        CHECK(image[LOOP_SITE] == 0x83 && image[LOOP_SITE + 2] == 8);
        /* and qword [rdi+flags],0 with the distance the move gave the instruction */
        CHECK(image[CLEAR_SITE] == 0x48 && image[CLEAR_SITE + 1] == 0x83 && image[CLEAR_SITE + 2] == 0xA7 && image[CLEAR_SITE + 7] == 0);
        memcpy(&distance, image + CLEAR_SITE + 3, 4);
        CHECK(ctx + 0x159B0u + distance == ext);
        CHECK(image[SIZE_SITE] == 0x49);
        memcpy(&distance, image + SIZE_SITE + 3, 4);
        CHECK(ctx + 0x159B0u + distance == ext);
        CHECK(image[CAVE_SITE] == 0xE9);
        memcpy(&to, image + CAVE_SITE + 1, 4);
        const uint8_t* cave = image + CAVE_SITE + 5 + to;
        CHECK((uintptr_t)cave >= caves && (uintptr_t)cave < caves + 0x2000u && cave[0] == 0x9C);
        /* behind the call: test rcx,rcx; jne game+0x108A426; jmp game+0x108A3FD; the callback's address */
        CHECK(cave[0x86] == 0x48 && cave[0x89] == 0x0F && cave[0x8F] == 0xE9);
        memcpy(&to, cave + 0x8B, 4);
        CHECK(cave + 0x8F + to == image + 0x108A426u);
        memcpy(&to, cave + 0x90, 4);
        CHECK(cave + 0x94 + to == image + 0x108A3FDu);
        CHECK(*(uint64_t*)(cave + 0x98) != 0 && (*(uint64_t*)(cave + 0x98) >> 40) != 0xC0DE00u >> 8);
    }
    {
        /* A stand-in host synchronizer: slot i at +0x20 + i * 0x38 with key, player index +0x14, state +0x20. */
        static uint8_t sync[0x200];
        const uintptr_t ext = co_session_test_extension();
        memset(sync, 0, sizeof sync);
        *(uint64_t*)(sync + 0x20) = 11; *(uint32_t*)(sync + 0x40) = 9;
        *(uint64_t*)(sync + 0x20 + 2 * 0x38) = 12; *(uint32_t*)(sync + 0x40 + 2 * 0x38) = 5; *(uint32_t*)(sync + 0x34 + 2 * 0x38) = 1;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2 && co_counts_test_free((uintptr_t)sync) == 2);
        *(uint64_t*)(sync + 0x20 + 5 * 0x38) = 13; *(uint32_t*)(sync + 0x40 + 5 * 0x38) = 4;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2 && co_counts_test_free((uintptr_t)sync) == 1);
        *(uint32_t*)(sync + 0x40 + 5 * 0x38) = 7; *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 0xFFFFFFFFu;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2);
        *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 3;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 3);

        /* Party record 6 of peer 77 lists the keys 5 and 9 and one entry of peer 9. */
        uint8_t* record = (uint8_t*)(ext + 0x200u + 6 * 0x1C0u);
        *(uint64_t*)record = 77; *(uint32_t*)(record + 8) = 2;
        *(uint64_t*)(record + 0x10) = 9; *(uint64_t*)(record + 0x18) = 5;
        *(uint32_t*)(record + 0x38) = 1; *(uint64_t*)(record + 0x40) = 9;
        CHECK(co_counts_test_find(77) == (uint64_t)(uintptr_t)record + 8 && co_counts_test_find(78) == 0);
        co_counts_test_gone((uintptr_t)sync, 9);
        CHECK(*(uint32_t*)(record + 8) == 1 && *(uint64_t*)(record + 0x10) == 5 && *(uint32_t*)(record + 0x38) == 0 && *(uint64_t*)record == 77);
        co_counts_test_gone((uintptr_t)sync, 5);
        CHECK(*(uint64_t*)record == 0 && co_counts_test_find(77) == 0);
    }
    {
        /* The occupied slot with the highest number decides: state 6 to 8 or not. */
        static uint8_t context[0x15C00];
        uint8_t* sync = context + 0x159B0u;
        CHECK(co_counts_test_last((uintptr_t)context) == 0);
        *(uint64_t*)(sync + 0x20) = 11; *(uint32_t*)(sync + 0x40) = 7;
        CHECK(co_counts_test_last((uintptr_t)context) == 1);
        *(uint64_t*)(sync + 0x20 + 6 * 0x38) = 12; *(uint32_t*)(sync + 0x40 + 6 * 0x38) = 5;
        CHECK(co_counts_test_last((uintptr_t)context) == 0);
        *(uint32_t*)(sync + 0x40 + 6 * 0x38) = 8;
        CHECK(co_counts_test_last((uintptr_t)context) == 1);
        *(uint32_t*)(sync + 0x40 + 6 * 0x38) = 9;
        CHECK(co_counts_test_last((uintptr_t)context) == 0);
    }
    /* Another context: counts and move are taken back and made again for it. */
    Init(CONTEXT + 0x100000u);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1 && g_fake.image[LOOP_SITE + 2] == 8 && g_fake.image[CAVE_SITE] == 0xE9);

    Start("one changed count site");
    g_fake.image[LOOP_SITE + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("session: the code at game+0x108AEE7 is not the reviewed one; the session lists count to four as before"));

    Start("one changed cave site");
    g_fake.image[LAST_CAVE + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("game+0x108D368 is not the reviewed one"));

    for (int at = moves; at < writes; at += (at < moves + 2 || at >= writes - 3) ? 1 : 29)
    {
        static char name[64];
        snprintf(name, sizeof name, "write %d fails", at);
        Start(name);
        g_fake.failWriteAt = at;
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
        CHECK(fake_host_logged("a count site could not be written; the session lists count to four as before"));
    }

    Start("a function is other code");
    co_counts_test_reset();
    co_session_test_reset();
    {
        const HfApi* api = fake_host_reset(CO_GAME_BUILD);
        co_session_test_fill(g_fake.image);
        co_counts_test_fill(g_fake.image);
        g_fake.noMatch[0] = 0x108B6B0u;
        hellforge_mod_init(api, g_mod);
        if (g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
        CHECK(fake_host_logged("the session lists count to four as before"));
    }

    printf("%s\n", g_failed ? "counts: failures" : "counts: all cases passed");
    return g_failed != 0;
}
