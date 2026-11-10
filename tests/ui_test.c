/* The tables a build for eight moves at run time, against a stand-in game image. At install the guards for four go in;
 * when the state is built the galactic map's cursors move to the mod's block: the displacements lead there from the
 * map object and its system, the counts and bounds say eight and the guards bound eight. At the HUD's init the squad
 * HUD's teammate panel and the name tags' rows move the same way, from H2 and the HUD. A changed byte or a failed
 * write leaves that table's layout before it exactly as it was. A build for four leaves all of it to its counts. */
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
int co_ui_test_as(unsigned block, int layout);
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
#define HUD 1u
#define TAG 2u
#define STATE_GLOBAL 0x346BF98u     /* the entity state: zero until the game builds it */
#define HUD_GLOBAL 0x346D538u
#define HUD_INIT 0x12E96D0u         /* H2's init, rcx = H2 */
#define HUD_OBJECT 0x2000000u       /* where the stand-in HUD lies in the image */
#define H2_IN_HUD 0x24E340u
#define T_IN_H2 0x1A0E28u           /* the tag widget T */
#define PANEL_FROM_H2 0x12E9D75u    /* lea rcx,[rbx+0x1F8C8]: the panel widget P from H2 */
#define PANEL_FROM_HUD 0x60ACE9u    /* lea rcx,[rax+0x278914]: panel 0 + 0xA66C from the HUD */
#define PANEL_COUNT 0x12EA9EEu      /* lea r15d,[rbx+3]: the reset's teammate panels */
#define PANEL_IN_P 0x182D7E3u       /* lea rcx,[rdi+0x6A0]: panel i inside P */
#define TAG_B 0x18A7132u            /* lea rcx,[r14+0x5B8A0]: tag row B_0 from T */
#define TAG_POSITION 0x18AF5C7u     /* movsd [r14+rcx*4+0x5D9A4],xmm12: a row's screen position from T */
#define TAG_A_FROM_B 0x18A7160u     /* lea rax,[rcx-0x440]: A_i from B_i */
#define TAG_GUARD 0x18AF049u        /* cmp dword [r13+0x84],0: the tag row by tag index */
#define TAG_LOOP 0x18A7118u         /* mov dword [rbp-0x58],0x1F2: before the tag rows' loop */

typedef void (*CursorFn)(uint32_t*, uint32_t);
typedef void (*EnterFn)(uintptr_t, uint64_t, const void*);
typedef uint64_t (*CtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*HudInitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
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

/* The game's HUD global set to the stand-in HUD, and H2's init run for `h2`. */
static void HudInit(uintptr_t h2)
{
    const uint64_t hud = (uint64_t)(uintptr_t)(g_fake.image + HUD_OBJECT);
    HudInitFn init = (HudInitFn)fake_host_detour(HUD_INIT);
    memcpy(g_fake.image + HUD_GLOBAL, &hud, 8);
    if (init != NULL) init(h2, 0, 0, 0);
}

static uintptr_t H2(void) { return (uintptr_t)g_fake.image + HUD_OBJECT + H2_IN_HUD; }

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
    CHECK(kept == (int)co_ui_test_kept() && kept == 25 && co_ui_test_layout(MAP) == 1 && co_ui_guarded());
    CHECK(co_ui_test_layout(HUD) == 1 && co_ui_test_layout(TAG) == 1 && fake_host_detour(HUD_INIT) != NULL);
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
    CHECK(g_fake.writes == 1 && g_fake.image[CURSOR_FN] == 0x40 && co_ui_test_layout(MAP) == 0 && co_ui_test_layout(TAG) == 1 && !co_ui_guarded());
    CHECK(fake_host_logged("players: the code at game+0x142D7D2 is not the reviewed one; the galactic map keeps four cursors"));
    BuildState(FIRST_STATE);
    CHECK(g_fake.writes == 1 && g_fake.image[CURSOR_COUNT + 2] == 4 && co_ui_test_layout(MAP) == 0);

    Start("a site changed before the move", -1, 0);
    g_fake.image[CURSOR_COUNT + 2] ^= 0x01;
    memcpy(g_installed, g_fake.image, FAKE_IMAGE_SIZE);
    BuildState(FIRST_STATE);
    CHECK(g_fake.writes == kept && Same(g_installed) && co_ui_test_layout(MAP) == 1 && co_ui_test_table(MAP) == 0 && co_ui_guarded());
    CHECK(fake_host_logged("players: the code at game+0x142BB94 is not the reviewed one; the galactic map keeps four cursors"));
    g_fake.log[0] = 0;
    BuildState(SECOND_STATE);
    CHECK(g_fake.writes == kept && Same(g_installed) && !fake_host_logged("game+0x142BB94"));

    /* A failed write: the guards' install leaves that table as the game has it and the join limit at four; the move
     * leaves KEPT. The map's guards are written first, then the tags'. */
    for (int at = 0; at < kept; at++)
    {
        static char name[64];
        const unsigned failed = at < kept - 1 ? MAP : TAG, other = failed == MAP ? TAG : MAP;
        snprintf(name, sizeof name, "install write %d fails", at);
        Start(name, at, 0);
        CHECK(co_ui_test_layout(failed) == 0 && co_ui_test_as(failed, 0) && co_ui_test_layout(other) == 1 && co_ui_test_as(other, 1));
        CHECK(!co_ui_guarded() && fake_host_logged(failed == MAP ? "could not be written; the galactic map keeps four cursors" :
                                                                     "could not be written; four players have name tags"));
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

    Start("the HUD", -1, 0);
    {
        /* KEPT: the tag guard's cave, cmp dword [rsp+0x20],4; jae the row's skip. */
        const uint8_t* guard = Cave(TAG_GUARD, 8, co_ui_test_room(KEPT_ROOM));
        CHECK(guard != NULL && guard[4] == 4 && guard[5] == 0x0F && guard[6] == 0x83 && guard + 11 + I32(guard + 7) == g_fake.image + 0x18AEDB9u);
        CHECK(co_ui_test_as(HUD, 1) && co_ui_test_as(TAG, 1));
    }
    const int ready = g_fake.writes;
    HudInit(H2());
    const int hudWrites = (int)co_ui_test_sites(HUD), tagWrites = (int)co_ui_test_sites(TAG);
    CHECK(hudWrites == 40 && tagWrites == 34 && g_fake.writes == ready + hudWrites + tagWrites);
    CHECK(fake_host_logged("players: the squad HUD has seven teammate panels (40 code sites changed)"));
    CHECK(fake_host_logged("players: eight players have name tags (34 code sites changed)"));
    CHECK(co_ui_test_layout(HUD) == 2 && co_ui_test_as(HUD, 2) && co_ui_test_layout(TAG) == 2 && co_ui_test_as(TAG, 2));
    CHECK(co_ui_test_layout(MAP) == 1 && co_ui_test_as(MAP, 1) && co_ui_guarded());
    {
        const uintptr_t h2 = H2(), hud = h2 - H2_IN_HUD, t = h2 + T_IN_H2;
        const uintptr_t panel = co_ui_test_table(HUD), rows = co_ui_test_table(TAG);
        const uint8_t* image = g_fake.image;
        CHECK(panel != 0 && panel % 0x100u == (h2 + 0x1F8C8u) % 0x100u && rows != 0 && rows % 0x100u == (t + 0x5B460u) % 0x100u);
        CHECK(Leads(h2, PANEL_FROM_H2, 3) == panel && Leads(hud, PANEL_FROM_HUD, 3) == panel + 0xC20u + 0xA66Cu);
        CHECK(image[PANEL_COUNT + 3] == 7 && I32(image + PANEL_IN_P + 3) == 0xC20);
        CHECK(Leads(t, TAG_B, 3) == rows + 0x880u && Leads(t, TAG_POSITION, 6) == rows + 0x4A94u && I32(image + TAG_A_FROM_B + 3) == -0x880);
        /* MOVED: the guard bounds eight; the rows' loop counts eight (mov qword [rbp-0x70],8). */
        static const uint8_t kLoop[] = {0x48, 0xC7, 0x45, 0x90, 0x08, 0x00, 0x00, 0x00};
        const uint8_t* guard = Cave(TAG_GUARD, 8, co_ui_test_room(TAG));
        const uint8_t* loop = Cave(TAG_LOOP, 7, co_ui_test_room(TAG));
        CHECK(guard != NULL && guard[4] == 8 && loop != NULL && memcmp(loop, kLoop, sizeof kLoop) == 0);
        /* The next init: the same tables, not zeroed; nothing written. */
        *(uint8_t*)panel = 5;
        HudInit(h2);
        CHECK(co_ui_test_table(HUD) == panel && co_ui_test_table(TAG) == rows && *(uint8_t*)panel == 5);
        CHECK(g_fake.writes == ready + hudWrites + tagWrites);
    }

    Start("H2 elsewhere", -1, 0);
    HudInit(H2() + 0x1000u);
    CHECK(co_ui_test_layout(HUD) == 1 && co_ui_test_layout(TAG) == 1 && g_fake.writes == kept);
    CHECK(fake_host_logged("players: the HUD is not where the game keeps it; the squad HUD keeps three teammate panels and four players have name tags"));

    Start("the game already running", -1, STATE_GLOBAL);
    CHECK(fake_host_detour(HUD_INIT) == NULL && co_ui_test_layout(TAG) == 1 && co_ui_test_as(TAG, 1) && co_ui_guarded());
    CHECK(fake_host_logged("players: the game was already running when the mod loaded; the squad HUD keeps three teammate panels and four players have name tags"));

    Start("a HUD site changed before the move", -1, 0);
    g_fake.image[PANEL_COUNT + 3] ^= 0x01;
    HudInit(H2());
    CHECK(co_ui_test_layout(HUD) == 1 && co_ui_test_table(HUD) == 0 && co_ui_test_layout(TAG) == 2 && co_ui_test_as(TAG, 2));
    CHECK(fake_host_logged("players: the code at game+0x12EA9EE is not the reviewed one; the squad HUD keeps three teammate panels"));

    /* A failed write in one table's move leaves that table at KEPT; the other moves. */
    for (int at = 0; at < hudWrites + tagWrites; at += (at < 2 || (at >= hudWrites - 2 && at < hudWrites + 2) || at >= hudWrites + tagWrites - 2) ? 1 : 7)
    {
        static char name[64];
        const unsigned failed = at < hudWrites ? HUD : TAG, other = failed == HUD ? TAG : HUD;
        snprintf(name, sizeof name, "HUD move write %d fails", at);
        Start(name, -1, 0);
        g_fake.failWriteAt = g_fake.writes + at;
        HudInit(H2());
        CHECK(co_ui_test_layout(failed) == 1 && co_ui_test_as(failed, 1) && co_ui_test_table(failed) == 0);
        CHECK(co_ui_test_layout(other) == 2 && co_ui_test_as(other, 2) && co_ui_guarded());
    }

    printf("%s\n", g_failed ? "ui: failures" : "ui: all cases passed");
    return g_failed != 0;
}
