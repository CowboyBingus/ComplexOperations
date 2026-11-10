/* The tables a build for eight moves at run time, against a stand-in game image. At install the guards for four go in;
 * when the state is built the galactic map's cursors move to the mod's block: the displacements lead there from the
 * map object and its system, the counts and bounds say eight and the guards bound eight. A changed byte or a failed
 * write leaves the layout before it exactly as it was. A build for four leaves all of it to its counts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_ui_test_fill(uint8_t* image);
void co_ui_test_reset(void);
int co_ui_test_layout(unsigned block);
uintptr_t co_ui_test_table(unsigned block);
uintptr_t co_ui_test_room(unsigned block);
unsigned co_ui_test_sites(unsigned block);
unsigned co_ui_test_kept(void);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_session_test_reset(void);
void co_counts_test_reset(void);

#define STATE_CTOR 0xFDA190u
#define STATE_BLOCK 0x40u
#define FIRST_STATE 0x100000u
#define SECOND_STATE 0x200000u
#define MAP 0u
#define KEPT_ROOM 99u
#define MAP_SYSTEM 0x1F7AE8u        /* S in the component block */
#define MAP_OBJECT 0x4CD1F0u        /* G in S */
#define CURSOR_G 0x142D7D2u         /* lea rbx,[rdi+0x80E18]: cursor 0 from G */
#define CURSOR_S 0x72F719u          /* lea rcx,[rbx+0x54E1A8]: cursor 0 + 0x1A0 from S */
#define HOVER_TARGET 0x142F9D3u     /* mov eax,[rcx+r8*4+0x8D6C4]: a hover target from G */
#define HOVER_KIND 0x142FF11u       /* mov eax,[rbx+r9*4+0x8D6D4]: a hover kind from G */
#define CURSOR_COUNT 0x142BB94u     /* mov r12d,4: the map object's constructor over its cursors */
#define CURSOR_BOUND 0x72F9D0u      /* cmp r8d,4: an index bound the game has */
#define OWN_CURSOR 0x18C0F2Fu       /* cmp edi,-1; je: the own cursor */
#define CURSOR_FN 0x72F700u         /* a function: the cursor of the player index in edx */
#define HOVER_RESET 0x142C867u      /* mov qword [rbx+0x8D6C4],-1: the hover arrays cleared, two qwords each */
#define ENTER 0x34C0000u            /* free in the stand-in: the test's way into a cave */

typedef void (*CursorFn)(uint32_t*, uint32_t);
typedef void (*EnterFn)(uintptr_t, uint64_t, const void*);
typedef uint64_t (*CtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
int co_ui_guarded(void);

static int g_failed;
static const char* g_case;
static uint8_t *g_before, *g_installed;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The reviewed bytes at every site (but for a changed byte at `flip`, 0 for none), then the mod as the loader starts
 * it; `failAt` is the write that fails. A copy of the image before the install and one after it. */
static void Start(const char* name, int failAt, uint32_t flip)
{
    const HfApi* api;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    co_counts_test_reset();
    co_ui_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_ui_test_fill(g_fake.image);
    if (flip != 0) g_fake.image[flip] ^= 0x01;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    g_fake.failWriteAt = failAt;
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
    memcpy(g_installed, g_fake.image, FAKE_IMAGE_SIZE);
}

static void BuildState(uintptr_t at)
{
    CtorFn ctor = (CtorFn)fake_host_detour(STATE_CTOR);
    if (ctor != NULL) ctor((uint64_t)(uintptr_t)(g_fake.image + at), 0, 0, 0, 0, 0);
}

static int Same(const uint8_t* copy) { return memcmp(copy, g_fake.image, FAKE_IMAGE_SIZE) == 0; }
static int32_t I32(const uint8_t* at) { int32_t v; memcpy(&v, at, 4); return v; }

/* The jump at a site, nops behind it; the cave it leads to, inside the room. */
static const uint8_t* Cave(uint32_t site, uint32_t size, uintptr_t room)
{
    const uint8_t* image = g_fake.image;
    if (image[site] != 0xE9 || room == 0) return NULL;
    for (uint32_t i = 5; i < size; i++)
        if (image[site + i] != 0x90) return NULL;
    const uint8_t* cave = image + site + 5 + I32(image + site + 1);
    return (uintptr_t)cave >= room && (uintptr_t)cave < room + 0x1000u ? cave : NULL;
}

static void Landing(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* Runs the cursor function's cave with indexes 0 to 8 and -1: where the cave goes back the test undoes the replaced
 * push and sub and notes the index (add rsp,0x20; pop rbx; mov [rcx],edx; ret). */
static void RunCursorGuard(const uint8_t* cave, uint32_t bound)
{
    static const uint8_t kNote[] = {0x48, 0x83, 0xC4, 0x20, 0x5B, 0x89, 0x11, 0xC3};
    CHECK(cave != NULL);
    if (cave == NULL) return;
    Landing(CURSOR_FN + 6u, kNote, sizeof kNote);
    for (uint32_t i = 0; i < 10; i++)
    {
        const uint32_t n = i == 9 ? 0xFFFFFFFFu : i;
        uint32_t seen = 99;
        ((CursorFn)(uintptr_t)cave)(&seen, n);
        CHECK(seen == (n < bound ? n : 99));
    }
}

/* The map object of the state at `state`, and the place a displacement at `site` + `at` leads to from `base`. */
static uintptr_t MapObject(uintptr_t state) { return (uintptr_t)g_fake.image + state + STATE_BLOCK + MAP_SYSTEM + MAP_OBJECT; }
static uintptr_t Leads(uintptr_t base, uint32_t site, uint32_t at) { return base + I32(g_fake.image + site + at); }

static void CheckMoved(uintptr_t state)
{
    const uintptr_t g = MapObject(state), s = g - MAP_OBJECT, table = co_ui_test_table(MAP);
    const uint8_t* image = g_fake.image;
    static const uint8_t kOwn[] = {0x83, 0xFF, 0x08, 0x0F, 0x83};
    CHECK(co_ui_test_layout(MAP) == 2 && table != 0 && table % 0x100u == (g + 0x80E18u) % 0x100u);
    CHECK(Leads(g, CURSOR_G, 3) == table && Leads(s, CURSOR_S, 3) == table + 0x1A0u);
    CHECK(Leads(g, HOVER_TARGET, 4) == table + 0x14C0u && Leads(g, HOVER_KIND, 4) == table + 0x14E0u);
    CHECK(image[CURSOR_COUNT + 2] == 8 && image[CURSOR_BOUND + 3] == 8 && memcmp(image + OWN_CURSOR, kOwn, sizeof kOwn) == 0);
    CHECK(co_ui_guarded());
}

int main(void)
{
    const int players = CO_PLAYERS;
    g_before = malloc(FAKE_IMAGE_SIZE);
    g_installed = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL || g_installed == NULL) return 2;

    if (players == 4)
    {
        /* The build for four has the map's guards in its counts: nothing here writes, and the map stays. */
        Start("a build for four", -1, 0);
        CHECK(g_fake.writes == 0 && co_ui_test_layout(MAP) == 0);
        BuildState(FIRST_STATE);
        CHECK(Same(g_before) && co_ui_test_layout(MAP) == 0 && !fake_host_logged("galactic map"));
        printf("%s\n", g_failed ? "ui: failures" : "ui: all cases passed");
        return g_failed != 0;
    }

    Start("the reviewed game", -1, 0);
    const int kept = g_fake.writes;
    CHECK(kept == (int)co_ui_test_kept() && kept == 24 && co_ui_test_layout(MAP) == 1 && co_ui_guarded());
    {
        /* KEPT: four cursors with the guards, cmp edi,4; jae for the own one, the displacements as the game has them. */
        static const uint8_t kOwn[] = {0x83, 0xFF, 0x04, 0x0F, 0x83};
        CHECK(memcmp(g_fake.image + OWN_CURSOR, kOwn, sizeof kOwn) == 0 && g_fake.image[CURSOR_COUNT + 2] == 4);
        CHECK(memcmp(g_fake.image + CURSOR_G, g_before + CURSOR_G, 7) == 0 && g_fake.image[HOVER_RESET] == 0x48);
        RunCursorGuard(Cave(CURSOR_FN, 6, co_ui_test_room(KEPT_ROOM)), 4);
    }
    const int before = g_fake.writes;
    BuildState(FIRST_STATE);
    const int moves = g_fake.writes - before;
    CHECK(moves == (int)co_ui_test_sites(MAP) && moves == 186);
    CHECK(fake_host_logged("players: the galactic map has eight cursors (186 code sites changed)"));
    CheckMoved(FIRST_STATE);
    RunCursorGuard(Cave(CURSOR_FN, 6, co_ui_test_room(MAP)), 8);
    {
        /* The hover arrays' clear: eight entries each, at the table's place for them. Run from a stub that loads rbx
         * with the map object and the value for the kinds in rcx (push rbx; mov rbx,rcx; mov rcx,rdx; jmp r8); where
         * the cave goes back the test returns (pop rbx; ret). */
        static const uint8_t kEnter[] = {0x53, 0x48, 0x89, 0xCB, 0x48, 0x89, 0xD1, 0x41, 0xFF, 0xE0}, kBack[] = {0x5B, 0xC3};
        const uint8_t* cave = Cave(HOVER_RESET, 11, co_ui_test_room(MAP));
        uint8_t* hover = (uint8_t*)co_ui_test_table(MAP) + 0x14C0u;
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            Landing(ENTER, kEnter, sizeof kEnter);
            Landing(0x142C88Bu, kBack, sizeof kBack);
            memset(hover, 0x77, 0x40);
            ((EnterFn)(uintptr_t)(g_fake.image + ENTER))(MapObject(FIRST_STATE), 0, cave);
            int targets = 1, kinds = 1;
            for (int i = 0; i < 0x20; i++) { targets &= hover[i] == 0xFF; kinds &= hover[0x20 + i] == 0; }
            CHECK(targets && kinds);
        }
    }
    /* The same state again: same table, zeroed. Another state: the table moves along. */
    {
        const uintptr_t table = co_ui_test_table(MAP);
        *(uint8_t*)table = 1;
        BuildState(FIRST_STATE);
        CHECK(co_ui_test_table(MAP) == table && *(uint8_t*)table == 0 && g_fake.writes == before + moves);
        BuildState(SECOND_STATE);
        CheckMoved(SECOND_STATE);
        CHECK(g_fake.writes == before + 3 * moves);
    }

    Start("one changed site", -1, CURSOR_G + 1);
    CHECK(g_fake.writes == 0 && Same(g_before) && co_ui_test_layout(MAP) == 0 && !co_ui_guarded());
    CHECK(fake_host_logged("players: the code at game+0x142D7D2 is not the reviewed one; the galactic map keeps four cursors"));
    BuildState(FIRST_STATE);
    CHECK(g_fake.writes == 0 && Same(g_before) && co_ui_test_layout(MAP) == 0);

    Start("a site changed before the move", -1, 0);
    g_fake.image[CURSOR_COUNT + 2] ^= 0x01;
    memcpy(g_installed, g_fake.image, FAKE_IMAGE_SIZE);
    BuildState(FIRST_STATE);
    CHECK(g_fake.writes == kept && Same(g_installed) && co_ui_test_layout(MAP) == 1 && co_ui_test_table(MAP) == 0 && co_ui_guarded());
    CHECK(fake_host_logged("players: the code at game+0x142BB94 is not the reviewed one; the galactic map keeps four cursors"));
    g_fake.log[0] = 0;
    BuildState(SECOND_STATE);
    CHECK(g_fake.writes == kept && Same(g_installed) && !fake_host_logged("game+0x142BB94"));

    /* A failed write: the guards' install leaves the game's bytes and the join limit at four; the move leaves KEPT. */
    for (int at = 0; at < kept; at++)
    {
        static char name[64];
        snprintf(name, sizeof name, "install write %d fails", at);
        Start(name, at, 0);
        CHECK(Same(g_before) && co_ui_test_layout(MAP) == 0 && !co_ui_guarded());
        CHECK(fake_host_logged("could not be written; the galactic map keeps four cursors"));
    }
    for (int at = 0; at < moves; at += (at < 3 || at >= moves - 3) ? 1 : 23)
    {
        static char name[64];
        snprintf(name, sizeof name, "move write %d fails", at);
        Start(name, -1, 0);
        g_fake.failWriteAt = g_fake.writes + at;
        BuildState(FIRST_STATE);
        CHECK(Same(g_installed) && co_ui_test_layout(MAP) == 1 && co_ui_test_table(MAP) == 0 && co_ui_guarded());
        CHECK(fake_host_logged("could not be written; the galactic map keeps four cursors"));
    }

    printf("%s\n", g_failed ? "ui: failures" : "ui: all cases passed");
    return g_failed != 0;
}
