/* The session lists' move against a stand-in game image: sites and stubs lead to the extension, what the arrays held
 * goes along, and a refusal or a failed write leaves the image exactly as it was. The same for the lobby's member
 * lists, whose copy stubs are run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
int co_session_test_state(void);
uintptr_t co_session_test_extension(void);
uintptr_t co_session_test_stubs(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
int co_session_test_write_of(uint32_t rva);
void co_pods_test_reset(void);
void co_managers_test_reset(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u           /* where the stand-in session context lies in the image */
#define FLAG_SITE 0x108C2A7u        /* lea r13,[hs + 0x100]: the index flags, disp32 at +3; hs = context + 0x159B0 */
#define INDEX_SITE 0x134EE33u       /* add rax,0x1635: list 3 as an index of 16-byte units from the context */
#define STUB_SITE 0x108367Du        /* mov rsi,[cs + rax*8 + 0x18]: the ready peers; cs = context + 0x162C8 */
#define FIRST_STUB 0xB909B0u
#define READY 0x162E0u
/* The lobby objects in the context, their member lists and where these go in the extension. */
#define PREV 0x17210u
#define JOINER 0x18DC8u
#define MAIN 0x1D470u
#define MEMBERS 0x1B58u
#define JOIN_COMPONENT 0x171C8u
#define PREV_PLACE 0x1000u
#define JOINER_PLACE 0x2BB8u
#define MAIN_PLACE 0x7260u
#define OBJECT_SITE 0x108A650u      /* cmp [r13+rcx*8+0x1B58],r14: a member of any lobby object, disp32 at +4 */
#define MAIN_SITE 0xB92032u         /* the main lobby's members from the context */
#define JOINER_SITE 0xB92243u
#define PREV_SITE 0xB92212u
#define COMPONENT_SITE 0x108FB92u   /* the previous lobby's members from the join component */
#define COPY_MAIN 0x134F79Cu        /* main <- rdx, rbx the context; 7 bytes replaced */
#define COPY_PREV 0xB94E97u         /* previous <- main, rdi the join component; 11 bytes replaced */
#define TRAMPOLINE 0x3480000u       /* free room in the stand-in image for the test's own code */

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    const HfApi* api;
    const uint64_t context = (uint64_t)(uintptr_t)0;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_session_test_fill(g_fake.image);
    g_fake.noMatch[1] = 0x108B600u;                      /* the counts stay off: this is the move alone */
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
}

/* The game sets its global, then runs the init. */
static void Init(void)
{
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    memcpy(g_before + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
}

static int Unchanged(void) { return memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0; }
static uint32_t U32(uintptr_t rva) { uint32_t v; memcpy(&v, g_fake.image + rva, 4); return v; }
static uintptr_t Disp(uintptr_t rva) { return (uintptr_t)(intptr_t)(int32_t)U32(rva); }

/* The stand-in has no code: the test puts its own where a stub goes back into the game, and makes it runnable. */
static void Landing(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* Runs the copy stub behind the jump at `site` with the game's rbx (or rdi) set to `base` and rdx to `source`: a
 * trampoline saves the register, loads it and jumps to the stub; where the stub goes back the register is restored. */
static void RunCopy(uint32_t site, uint32_t size, int rdi, uintptr_t base, uintptr_t source)
{
    typedef void (*CopyFn)(uintptr_t, uintptr_t);
    int32_t to;
    memcpy(&to, g_fake.image + site + 1, 4);
    const uint64_t stub = (uint64_t)(uintptr_t)(g_fake.image + site + 5 + to);
    uint8_t enter[] = {rdi ? 0x57 : 0x53, 0x48, 0x89, rdi ? 0xCF : 0xCB, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    const uint8_t leave[] = {rdi ? 0x5F : 0x5B, 0xC3};
    memcpy(enter + 6, &stub, 8);
    Landing(TRAMPOLINE, enter, sizeof enter);
    Landing(site + size, leave, sizeof leave);
    ((CopyFn)(uintptr_t)(g_fake.image + TRAMPOLINE))(base, source);
}

int main(void)
{
    const int sites = (int)co_session_test_sites(), stubs = (int)co_session_test_stub_count();
    const uintptr_t context = 0;
    (void)context;
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;

    Start("the reviewed game");
    CHECK(g_fake.writes == 0);
    g_fake.image[CONTEXT + 0xB388u] = 1;                 /* the init has run: what the arrays hold stays */
    g_before[CONTEXT + 0xB388u] = 1;
    memset(g_fake.image + CONTEXT + READY, 0x5A, 0x20);
    memset(g_before + CONTEXT + READY, 0x5A, 0x20);
    memset(g_fake.image + CONTEXT + MAIN + MEMBERS, 0x6B, 0x20);
    memset(g_before + CONTEXT + MAIN + MEMBERS, 0x6B, 0x20);
    Init();
    CHECK(co_session_test_state() == 1 && g_fake.writes == sites + stubs);
    CHECK(fake_host_logged("session: 10 arrays of the session lists and the lobby's member lists moved"));
    {
        const uintptr_t ctx = (uintptr_t)g_fake.image + CONTEXT, ext = co_session_test_extension();
        const uint8_t* site = g_fake.image + STUB_SITE;
        int32_t to, back;
        CHECK(ctx + 0x159B0u + (int32_t)U32(FLAG_SITE + 3) == ext);
        CHECK(ctx + (uintptr_t)U32(INDEX_SITE + 2) * 16u == ext + 0x50u);
        CHECK(site[0] == 0xE9);
        memcpy(&to, site + 1, 4);
        const uint8_t* stub = site + 5 + to;
        CHECK((uintptr_t)stub >= co_session_test_stubs() && (uintptr_t)stub < co_session_test_stubs() + 0x1000u);
        CHECK(stub[0] == 0x48 && stub[1] == 0x8B && stub[2] == 0xB4 && stub[3] == 0xC7 && stub[8] == 0xE9);
        memcpy(&to, stub + 4, 4);
        CHECK(ctx + 0x162C8u + to == ext + 0x10u);
        memcpy(&back, stub + 9, 4);
        CHECK(stub + 13 + back == site + 5);
        CHECK(*(uint8_t*)(ext + 0x10u) == 0x5A && *(uint8_t*)(ext + 0x2Fu) == 0x5A && *(uint8_t*)(ext + 0x30u) == 0);
    }
    {
        /* The member lists: one distance from each lobby object, and from the context or the join component to its
         * own list. What the main list held goes along; its old place is zero. */
        const uintptr_t ctx = (uintptr_t)g_fake.image + CONTEXT, ext = co_session_test_extension();
        const uint8_t* members = (const uint8_t*)(ext + MAIN_PLACE);
        CHECK(ctx + PREV + Disp(OBJECT_SITE + 4) == ext + PREV_PLACE);
        CHECK(ctx + JOINER + Disp(OBJECT_SITE + 4) == ext + JOINER_PLACE && ctx + MAIN + Disp(OBJECT_SITE + 4) == ext + MAIN_PLACE);
        CHECK(ctx + Disp(MAIN_SITE + 4) == ext + MAIN_PLACE && ctx + Disp(JOINER_SITE + 4) == ext + JOINER_PLACE);
        CHECK(ctx + Disp(PREV_SITE + 4) == ext + PREV_PLACE && ctx + JOIN_COMPONENT + Disp(COMPONENT_SITE + 4) == ext + PREV_PLACE);
        CHECK(members[0] == 0x6B && members[0x1F] == 0x6B && members[0x20] == 0 && g_fake.image[CONTEXT + MAIN + MEMBERS] == 0);

        /* Run: main <- joiner, then previous <- main. Each copies all eight entries of the moved list. */
        uint8_t* joiner = (uint8_t*)(ext + JOINER_PLACE);
        for (int i = 0; i < 0x40; i++) joiner[i] = (uint8_t)(i + 1);
        CHECK(g_fake.image[COPY_MAIN] == 0xE9 && g_fake.image[COPY_MAIN + 5] == 0x90 && g_fake.image[COPY_PREV + 10] == 0x90);
        RunCopy(COPY_MAIN, 7, 0, ctx, ctx + JOINER);
        CHECK(memcmp(members, joiner, 0x40) == 0 && members[0x40] == 0);
        RunCopy(COPY_PREV, 11, 1, ctx + JOIN_COMPONENT, ctx);
        CHECK(memcmp((const void*)(ext + PREV_PLACE), joiner, 0x40) == 0);
    }
    Init();
    CHECK(co_session_test_state() == 1 && g_fake.writes == sites + stubs);

    Start("a fresh context");
    memset(g_fake.image + CONTEXT + READY, 0x5A, 0x20);
    memset(g_before + CONTEXT + READY, 0x5A, 0x20);
    Init();
    CHECK(co_session_test_state() == 1 && *(uint8_t*)(co_session_test_extension() + 0x10u) == 0);

    Start("one changed site");
    g_fake.image[FLAG_SITE + 1] ^= 0x01;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    Init();
    CHECK(co_session_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("session: the code at game+0x108C2A7 is not the reviewed one; the session lists stay as they are"));

    Start("one changed stub site");
    g_fake.image[FIRST_STUB + 4] = 0x20;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    Init();
    CHECK(co_session_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("game+0xB909B0 is not the reviewed one"));

    Start("one changed member list site");
    g_fake.image[OBJECT_SITE + 2] ^= 0x01;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    Init();
    CHECK(co_session_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("session: the code at game+0x108A650 is not the reviewed one; the session lists stay as they are"));

    Start("one changed copy site");
    g_fake.image[COPY_PREV + 10] ^= 0x01;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    Init();
    CHECK(co_session_test_state() == -1 && Unchanged() && g_fake.writes == 0);
    CHECK(fake_host_logged("game+0xB94E97 is not the reviewed one"));

    /* A failed write at a member list's site or copy takes everything back too. */
    {
        const uint32_t kLobby[] = {OBJECT_SITE, MAIN_SITE, COMPONENT_SITE, COPY_PREV, COPY_MAIN};
        for (size_t i = 0; i < sizeof kLobby / sizeof kLobby[0]; i++)
        {
            static char name[64];
            const int at = co_session_test_write_of(kLobby[i]);
            snprintf(name, sizeof name, "the write at game+0x%X fails", kLobby[i]);
            Start(name);
            CHECK(at >= 0);
            g_fake.failWriteAt = at;
            Init();
            CHECK(co_session_test_state() == -1 && Unchanged() && g_fake.writes == at + 1 + at);
            CHECK(fake_host_logged("could not be written; the session lists stay as they are"));
        }
    }

    for (int at = 0; at < sites + stubs; at += (at < 2 || at >= sites - 2) ? 1 : 41)
    {
        static char name[64];
        snprintf(name, sizeof name, "write %d fails", at);
        Start(name);
        g_fake.failWriteAt = at;
        Init();
        CHECK(co_session_test_state() == -1 && Unchanged());
        CHECK(fake_host_logged("could not be written; the session lists stay as they are"));
    }

    Start("the init is other code");
    co_session_test_reset();
    {
        const HfApi* api = fake_host_reset(CO_GAME_BUILD);
        g_fake.noMatch[0] = SESSION_INIT;
        hellforge_mod_init(api, g_mod);
        if (g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
        CHECK(fake_host_logged("session: this is not the game code the mod was made for"));
        CHECK(fake_host_detour(SESSION_INIT) == NULL);
    }

    printf("%s\n", g_failed ? "session: failures" : "session: all cases passed");
    return g_failed != 0;
}
