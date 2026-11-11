/* The type registry's list of loadout card widgets against a stand-in image. A build for eight moves it to room for
 * eight with the counts, before the loadout sites: the game's loads, run here, then read the mod's list. A changed
 * byte, a card in the game's list or a failed write leave the loads and the counts as the game has them, and the list
 * goes back with the counts while it is empty. A build for four leaves the loads alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_ui_test_reset(void);
void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
int co_counts_test_players(void);
int co_counts_test_move_writes(void);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);
unsigned co_registry_test_sites(void);
uint32_t co_registry_test_rva(unsigned i);
int co_registry_test_on(void);
uintptr_t co_registry_test_list(void);
uintptr_t co_registry_test_slot(void);
unsigned co_registry_test_room(void);
int co_registry_test_reviewed(const uint8_t* image);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define CARD_LIST 0x2C00u           /* in the registry: the count, entries of 16 bytes from +8 */
#define NEXT_LIST 0x2C48u           /* the next list's count */
#define ENTRY 0x3480000u            /* a free place in the stand-in for the test's own code */

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*LoadFn)(uintptr_t);

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    const uint64_t none = 0;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    co_counts_test_reset();
    co_ui_test_reset();
    co_registry_test_reset();
    const HfApi* api = fake_host_reset(CO_GAME_BUILD);
    co_session_test_fill(g_fake.image);
    co_counts_test_fill(g_fake.image);
    co_registry_test_fill(g_fake.image);
    memcpy(g_fake.image + SESSION_GLOBAL, &none, 8);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
}

static void Init(void)
{
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
}

static uint64_t Qword(uintptr_t at)
{
    uint64_t value;
    memcpy(&value, (const void*)at, 8);
    return value;
}

/* Runs the game's load at a site: the test enters it with push rsi; push rdi; jmp rcx, and puts mov rax,<the load's
 * register>; pop rdi; pop rsi; ret behind it. */
static uint64_t Load(uint32_t rva)
{
    static const uint8_t kEnter[] = {0x56, 0x57, 0xFF, 0xE1};
    uint8_t* site = g_fake.image + rva;
    const uint8_t back[] = {(uint8_t)(0x48 | (site[0] & 4)), 0x89, (uint8_t)(0xC0 | (site[2] & 0x38)), 0x5F, 0x5E, 0xC3};
    DWORD before;
    memcpy(g_fake.image + ENTRY, kEnter, sizeof kEnter);
    memcpy(site + 7, back, sizeof back);
    if (!VirtualProtect(g_fake.image + ENTRY, sizeof kEnter, PAGE_EXECUTE_READWRITE, &before) ||
        !VirtualProtect(site, 7 + sizeof back, PAGE_EXECUTE_READWRITE, &before)) return 0;
    return ((LoadFn)(uintptr_t)(g_fake.image + ENTRY))((uintptr_t)site);
}

/* Every load reads `value`. */
static int AllRead(uint64_t value)
{
    for (unsigned i = 0; i < co_registry_test_sites(); i++)
        if (Load(co_registry_test_rva(i)) != value) return 0;
    return 1;
}

static uint64_t Registry(void) { return (uint64_t)(uintptr_t)(g_fake.image + FAKE_REGISTRY); }
static uint64_t Moved(void) { return (uint64_t)co_registry_test_list() - CARD_LIST; }

/* Off: the loads read the game's registry, the counts are off and the per-peer table is where the game keeps it. */
static int AsTheGameHasIt(void)
{
    return co_registry_test_on() == 0 && co_registry_test_reviewed(g_fake.image) && co_counts_test_on() == 0 &&
           Qword((uintptr_t)(g_fake.image + FAKE_SYNCED_POINTER)) == (uint64_t)(uintptr_t)(g_fake.image + FAKE_SYNCED_TABLE) && AllRead(Registry());
}

int main(void)
{
    const int eight = co_counts_test_players() == 8;
    const unsigned sites = co_registry_test_sites();
    /* The session step's writes, then the per-peer table's; the list's sites follow. */
    const int first = (int)(co_session_test_sites() + co_session_test_stub_count()) + co_counts_test_move_writes();
    static char line[160];

    Start("the reviewed game");
    CHECK(sites == 7 && AllRead(Registry()));
    Init();
    CHECK(co_counts_test_on() == 1 && co_registry_test_on() == eight);
    CHECK(fake_host_logged("players: the type registry's list of loadout cards has room for eight (7 code sites changed)") == eight);
    if (!eight) CHECK(co_registry_test_reviewed(g_fake.image) && co_registry_test_list() == 0 && AllRead(Registry()));
    if (eight)
    {
        /* Only the displacement changed; every load reads the qword, which leads to the mod's list. */
        const uintptr_t list = co_registry_test_list(), slot = co_registry_test_slot();
        for (unsigned i = 0; i < sites; i++)
        {
            const uint8_t* site = g_fake.image + co_registry_test_rva(i);
            int32_t to;
            memcpy(&to, site + 3, 4);
            CHECK(site[1] == 0x8B && (site[2] & 0xC7) == 0x05 && (uintptr_t)(site + 7 + to) == slot);
        }
        CHECK(list % 8 == 0 && Qword(slot) == Moved() && AllRead(Moved()) && co_registry_test_room() == 8);
        /* Eight cards as the game adds them, through the add's load: all in the mod's list, the next list untouched. */
        const uint64_t base = Load(co_registry_test_rva(0));
        for (uint32_t card = 0; card < 8; card++)
        {
            uint32_t* count = (uint32_t*)(uintptr_t)(base + CARD_LIST);
            const uint64_t widget = 0x5B78u + card;
            const uint32_t type = 0xCA;
            CHECK(base + CARD_LIST + 8u + (*count + 1u) * 16u <= list + 8u + 8u * 16u);
            memcpy((void*)(uintptr_t)(base + CARD_LIST + 8u + *count * 16u), &widget, 8);
            memcpy((void*)(uintptr_t)(base + CARD_LIST + 8u + *count * 16u + 8u), &type, 4);
            ++*count;
        }
        CHECK(*(const uint32_t*)list == 8 && Qword(list + 8u + 7u * 16u) == 0x5B7Fu);
        CHECK(Qword((uintptr_t)(g_fake.image + FAKE_REGISTRY + CARD_LIST)) == 0 && Qword((uintptr_t)(g_fake.image + FAKE_REGISTRY + NEXT_LIST)) == 0);
        /* Out of the list again, the list goes back with the counts. */
        memset((void*)list, 0, 8u + 8u * 16u);
        const int writes = g_fake.writes;
        CHECK(co_registry_cards((uintptr_t)g_fake.image) == 1 && g_fake.writes == writes);
        co_counts_undo();
        CHECK(AsTheGameHasIt());
    }

    for (unsigned i = 0; i < sites; i++)
    {
        static const uint8_t kAt[] = {1, 4};
        snprintf(line, sizeof line, "a changed byte at game+0x%X", co_registry_test_rva(i));
        Start(line);
        g_fake.image[co_registry_test_rva(i) + kAt[i % 2]] ^= 0x01;
        Init();
        CHECK(co_registry_test_on() == 0 && co_counts_test_on() == !eight);
        CHECK(!co_registry_test_reviewed(g_fake.image));
        g_fake.image[co_registry_test_rva(i) + kAt[i % 2]] ^= 0x01;
        CHECK(co_registry_test_reviewed(g_fake.image));
        snprintf(line, sizeof line, "players: the code at game+0x%X is not the reviewed one; the loadout screen keeps four cards", co_registry_test_rva(i));
        CHECK(fake_host_logged(line) == eight);
        CHECK(fake_host_logged("session: the session lists count to four as before") == eight);
        if (eight) CHECK(AsTheGameHasIt());
    }

    if (!eight)
    {
        printf("%s\n", g_failed ? "registry: failures" : "registry: all cases passed");
        return g_failed != 0;
    }

    /* A card in the game's list belongs to a screen built for four: refused, not copied. */
    Start("a card in the game's list");
    {
        const uint32_t one = 1;
        memcpy(g_fake.image + FAKE_REGISTRY + CARD_LIST, &one, 4);
    }
    Init();
    CHECK(AsTheGameHasIt() && co_registry_test_list() == 0);
    CHECK(fake_host_logged("players: the type registry's list of loadout cards is not empty (1); the loadout screen keeps four cards"));
    CHECK(fake_host_logged("session: the session lists count to four as before"));

    for (unsigned k = 0; k < sites; k++)
    {
        snprintf(line, sizeof line, "the list's write %u fails", k);
        Start(line);
        g_fake.failWriteAt = first + (int)k;
        Init();
        CHECK(AsTheGameHasIt());
        snprintf(line, sizeof line, "players: game+0x%X could not be written; the loadout screen keeps four cards", co_registry_test_rva(k));
        CHECK(fake_host_logged(line) && fake_host_logged("session: the session lists count to four as before"));
    }

    Start("the first count site's write fails");
    g_fake.failWriteAt = first + (int)sites;
    Init();
    CHECK(AsTheGameHasIt() && fake_host_logged("a count site could not be written; the session lists count to four as before"));

    /* A card registered when the counts go back keeps the loads on the mod's list; once out, it goes back. */
    Start("a card at the counts' undo");
    Init();
    CHECK(co_registry_test_on() == 1);
    *(uint32_t*)co_registry_test_list() = 1;
    co_counts_undo();
    CHECK(co_counts_test_on() == 0 && co_registry_test_on() == 1 && AllRead(Moved()));
    CHECK(fake_host_logged("players: the loadout cards' list is not empty (1); the loadout cards' list stays in the mod's memory"));
    *(uint32_t*)co_registry_test_list() = 0;
    co_registry_back();
    CHECK(AsTheGameHasIt());

    Start("a failed write on the way back");
    Init();
    g_fake.failWriteAt = g_fake.writes + 3;
    co_registry_back();
    CHECK(co_registry_test_on() == 1 && AllRead(Moved()));
    snprintf(line, sizeof line, "players: game+0x%X could not be written; the loadout cards' list stays in the mod's memory", co_registry_test_rva(3));
    CHECK(fake_host_logged(line));

    /* Before the engine builds the registry nothing can be in it. */
    Start("no registry yet");
    memset(g_fake.image + FAKE_REGISTRY_GLOBAL, 0, 8);
    CHECK(co_registry_cards((uintptr_t)g_fake.image) == 1 && co_registry_test_on() == 1 && AllRead(Moved()));
    co_registry_back();
    CHECK(co_registry_test_on() == 0 && co_registry_test_reviewed(g_fake.image));

    printf("%s\n", g_failed ? "registry: failures" : "registry: all cases passed");
    return g_failed != 0;
}
