/* The session lists' move against a stand-in game image: sites and stubs lead to the extension, what the arrays held
 * goes along, and a refusal or a failed write leaves the image exactly as it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
int co_session_test_state(void);
uintptr_t co_session_test_extension(void);
uintptr_t co_session_test_stubs(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
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
    Init();
    CHECK(co_session_test_state() == 1 && g_fake.writes == sites + stubs);
    CHECK(fake_host_logged("session: 7 arrays of the session lists moved"));
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
