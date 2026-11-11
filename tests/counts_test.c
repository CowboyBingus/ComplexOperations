/* The session lists' counts against a stand-in game image: sites, flag clears and caves are written behind the move,
 * a failed write takes all of them back, and the mod's own slot and record code handles entries 4 to 7. The guards
 * of the game's four-entry tables go the same way; some of them are run with player indexes 0 to 7. The message
 * guards are run with lists that fit and lists that do not. The build's player count decides which sites belong to
 * it, and the test option's slot and index search is checked against a stand-in synchronizer. The lobby's member
 * lists fill eight in every build; the lobby takes eight only in a build for eight. The own lobby's record stops its
 * entries at four in every build. A build for eight gives the loadout screen eight records and cards in place of the
 * guards that kept players 5 to 8 off it, and moves the per-peer loadout table to room for eight before it writes that
 * table's bounds; a failed write or a changed byte leaves all of it as the game has it. The score manager's loot rows
 * are bounded where the managers keep them now, the mission result adds the loot of slots 4 to 7 without a row for them,
 * the Activity record's lists stop at their room while its post lists every participant with the local peer in the
 * record and it waits for every non-local one's answer; without the post's hook or with one of its sites changed it
 * keeps four and the rest goes in. A build for eight takes four players while the managers have four slots. The clan records' and the clan station's
 * handlers skip a player index of four or more in every build; the galactic map's cursors do in a build for four (a
 * build for eight moves them, tests/ui_test.c). A build for eight gives the mission summary eight rows
 * and cards and bounds its rows at eight; a build for four bounds them at four. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

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
int co_counts_test_reviewed(const uint8_t* image);
unsigned co_counts_test_guards(void);
uintptr_t co_counts_test_caves(void);
unsigned co_counts_test_sites(void);
uint32_t co_counts_test_ready(uintptr_t sync);
uint32_t co_counts_test_free(uintptr_t sync);
uint64_t co_counts_test_find(uint64_t key);
void co_counts_test_gone(uintptr_t sync, uint64_t key);
uint64_t co_counts_test_last(uintptr_t context);
unsigned co_counts_test_net(void);
int co_counts_test_players(void);
int co_counts_test_first_slot(void);
int co_counts_test_fits(uint32_t shape, const void* arguments);
int co_counts_test_join_slot(uintptr_t sync);
uint32_t co_counts_test_join_index(uint8_t* flags);
int co_counts_test_write_of(uint32_t rva);
unsigned co_counts_test_loadout(void);
unsigned co_counts_test_synced(void);
uintptr_t co_counts_test_synced_table(void);
int co_counts_test_move_writes(void);
unsigned co_counts_test_room(void);
unsigned co_counts_test_summary(void);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_managers_test_force(int state);
void co_ui_test_reset(void);
void co_ui_test_force(int layout);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);
int co_registry_test_reviewed(const uint8_t* image);
uint32_t co_counts_test_loot_row(uintptr_t manager, uint32_t slot);
uint64_t co_counts_test_loot_index(uint32_t index, uint64_t flags);
typedef uint64_t (*PostFn)(uint64_t, uint64_t, uint64_t, const uint64_t*, uint32_t);
void co_counts_test_participant(uintptr_t frame, uintptr_t context, uint32_t place, uint64_t id);
void co_counts_test_record(uintptr_t table, uint32_t offset);
void co_counts_test_post_original(PostFn original);
int co_counts_test_activity(void);
unsigned co_counts_test_activity_sites(void);
int co_counts_test_waiting(uint64_t key);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define LOOP_SITE 0x108AEE7u        /* cmp eax,4 of a slot loop */
#define CLEAR_SITE 0x1088B8Au       /* xor eax,eax; mov [rdi+flags],eax */
#define SIZE_SITE 0x108984Cu        /* mov [r13+flags],ecx: the session step rewrites its distance */
#define CAVE_SITE 0x108A3F8u        /* test rcx,rcx; jne: the party record lookup */
#define LAST_CAVE 0x108D368u
#define GUARD_SITE 0x14670B8u       /* cmp ecx,-1; je: the loadout screen's own index */
#define GUARD_NEXT 0x146C3D5u       /* jae to the game's fail-fast: the loadout screen's loop over manager slots */
#define GUARD_ROW 0xB8F681u         /* movsd xmm0,[r13]: a row of the game state's table, index in eax */
#define GUARD_PEER 0x146CC05u       /* the loadout screen's card for a peer, index in r13d */
#define GUARD_CARD 0x146D320u       /* a function: the card of the player index in edx, -1 for none */
#define GUARD_CURSOR 0x72F700u      /* a function: the map cursor of the player index in edx */
#define LAST_GUARD 0x1471D30u
#define GUARD_ENTRY 0x139D300u      /* a function: an entry of 0x1320 for the player index in r8d */
#define CURSOR_EDX 0x72F5F0u       /* a function: writes the map cursor of the player index in edx */
#define CURSOR_R8 0x730080u         /* the same with the index in r8d */
#define GUARD_MAP 0x14335F0u        /* a function: a map cursor's position for the player index in edx */
#define GUARD_BITS 0x13D1119u       /* a record's bits by the player index in eax */
#define GUARD_INPUT 0x18C0F2Fu      /* cmp edi,-1; je: the own map cursor */
#define EIGHT_INDEX 0x134EF11u      /* cmp eax,4: a client's own player index is valid */
#define EIGHT_SOS 0x66CA22u         /* cmp eax,4: the SOS beacon can be called */
#define NET_SCORES 0xBAFF70u        /* a message handler: two lists of five */
#define NET_LOBBY 0xBBD6F0u         /* a message handler: twelve values, then lists of four */
#define NET_ACCEPTED 0xBAC74Du      /* mov ecx,[rdx+0x18]: a list's count where its handler reads it */
#define NET_SENDER 0x1339313u       /* mov ecx,[r15+0x5d0]: the member count a sender loops over */
#define NET_APPEND 0x108ABF2u       /* a sender's append to two lists of five */
#define TEST_SLOT 0x108C382u        /* the join request's search for a free slot */
#define TEST_INDEX 0x108C3CAu       /* and for a free player index */
#define MIRROR_CAP 0x1093CB0u       /* mov r8d,4: the PlayFab members copied into the main lobby's list */
#define MIRROR_COUNT 0x1093C96u     /* mov dword [rdi+0x1BB0],4: the LAN count clamp */
#define LOBBY_FULL 0x108AA56u       /* cmp ecx,4: the host slots taken when the lobby closes */
#define LOBBY_SIZE 0x10919D1u       /* mov ecx,4: the Steam lobby's size */
#define LOBBY_POST 0x12CB9BAu       /* je: the friends-list post's re-post of a count of at most four */
#define LOBBY_FRIEND 0x103D11Du     /* mov edx,4; mov [r14+0x34C],edx: a friend's lobby size */
#define RECORD_COUNT 0x133EF93u     /* mov [rbx+0x5D0],esi: the own lobby record's entries */
#define RECORD_POST 0x12CB379u      /* mov r8d,[rdi+0x5D0]: numLobbyMembers in the friends post */
#define RECORD_ENTER 0x133EFA0u     /* free in the stand-in: the test's way into the record's cave */
#define LOADOUT_SIZE 0x1498AE8u     /* mov edx,size: the loadout screen's allocation */
#define LOADOUT_BOUND 0x14674BBu    /* mov edi,4: a loop over the loadout screen's cards */
#define LOADOUT_MAP 0xB901EBu       /* mov eax,[r9+0x27d8]: the loadout screen's card of a player */
#define LOADOUT_CAVE 0x1471848u     /* lea rbx,[rsi+0x53a78]: the cards, with the loop count set in the cave */
#define SYNCED_CLEAR 0x1381254u     /* mov r8d,0x27e0: the memset of the per-peer loadout table */
#define SYNCED_SEARCH 0x1395221u    /* cmp ecx,4: a search of that table by peer id */
#define SYNCED_GONE 0x1087122u      /* cmp eax,4: the search for a peer that left */
#define SYNCED_ENTRY 0x9F8u         /* a record of 0x9F0 and the peer's id */
#define RESULT_GATE 0x1347AC0u      /* cmp eax,3: the mission result for player counts 1 to 4 */
#define LOOT_INDEX 0x6311D4u        /* cmp eax,ebx; je: a loot row by player index, -1 for none */
#define LOOT_RESULT 0x62F2DBu       /* the mission result's rows by score slot, before the load */
#define LOOT_LOAD 0x62F2E5u         /* that load: the managers move its offset */
#define ACTIVITY 0xA56D88u          /* mov [rbp+rax*8-0x68],rdx: the Activity record's participants */
#define ACTIVITY_MADE 0xA56F0Eu     /* inc dword [rsi+0x1100]: the record is in the table */
#define ACTIVITY_POST 0x12C2940u    /* a function: posts a record's participants (r9, count fifth) */
#define ACTIVITY_TABLE 0x346D5C8u   /* the global of the records' table: 32 of 0x88 */
#define ACTIVITY_WAIT 0xA56E91u     /* mov [rbp+rax*8-0x18],rcx: the peers whose answer the record waits for */
#define ANSWER_A 0xB9A648u          /* the record of an answer found (rax, the peer in rsi): its count's first test */
#define ANSWER_A_ZERO 0xB9A680u     /* test r9d,r9d; jne: the count after the answer's swap-remove */
#define ANSWER_B 0x12E6B20u         /* the same in a second answer function: the record r10, the peer r11 */
#define ANSWER_B_ZERO 0x12E6B57u
#define ANSWER_C 0x12E6C15u         /* and in a third over every waiting record: the record rax, the peer rdi */
#define ANSWER_C_ZERO 0x12E6C4Du
#define ACTIVITY_DROP 0x12E6A0Fu    /* lea ecx,[rdx-1]: a record in state 2 or 3 leaves the table (rax) */
#define ENTER 0x34C0000u            /* free in the stand-in: the test's ways into the caves */
#define MANAGER_SIZE 0x380A0u       /* the score manager with its loot rows moved */
#define CLAN_PAIR 0x12919A0u        /* a function: a clan pair by the player index in r8d */
#define CLAN_RECORD 0x1291AB0u      /* a function: a clan record by the player index in r8d */
#define CLAN_OWN 0x1291BEFu         /* cmp ecx,-1; je: the own clan pair */
#define CLAN_OWN_NAME 0x1291C7Fu    /* cmp ecx,-1; je: the own clan record's second string */
#define CLAN_ROW 0xB65880u          /* a function: the clan station's row by the player index in edx */
#define CLAN_ROW_AGAIN 0xFEC3E8u
#define SUMMARY_SIZE 0x14989FBu     /* mov edx,size: the mission summary screen's allocation */
#define SUMMARY_ROWS 0x1498A3Bu     /* mov ebp,4: the rows of both row blocks */
#define SUMMARY_GUARD 0x199422Cu    /* imul rdx,rax,0x24C: the own row by player index */
#define SUMMARY_CARDS 0x1993578u    /* cmp [rsi+card 0 + 0x12538],1: four card stores written out */    /* mov eax,r8d; lea r9,[rax+rax*4]: the same row, inside the function */

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint32_t (*CardFn)(void*, uint32_t);
typedef void (*CursorFn)(uint32_t*, uint32_t);
typedef void (*EntryFn)(uint32_t*, void*, uint32_t, uint32_t);
typedef uint32_t (*HandlerFn)(void*, const void*);
typedef void (*RecordFn)(void*, uint32_t, const void*);
typedef uint32_t (*IndexFn)(uint32_t, const void*);
typedef uint32_t (*ResultFn)(const void*, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*MadeFn)(void*, uint32_t, const void*);
typedef uint32_t (*StoreFn)(void*, uint32_t, uint64_t, const void*);
typedef void (*ClanFn)(uint32_t*, void*, uint32_t);
typedef void (*RowFn)(uint32_t*, uint32_t);
typedef void (*RowAgainFn)(uint32_t*, const void*, uint32_t);
typedef uint32_t (*AnswerFn)(const void*, uint64_t, uint64_t, const void*);

/* A handler's arguments as the game hands them over: 16 bytes each, the value at +8. `lists` are the counts of the
 * lists in order; `lead` values stand before the first list. Every list's elements follow its count. */
static const void* Arguments(uint32_t lead, const uint32_t* lists, size_t count)
{
    static uint64_t entries[2 * 512];
    uint32_t at = lead;
    memset(entries, 0, sizeof entries);
    for (size_t i = 0; i < count; i++)
    {
        entries[at * 2 + 1] = lists[i];
        at += 1 + lists[i];
    }
    return entries;
}

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
    co_ui_test_reset();
    co_registry_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_session_test_fill(g_fake.image);
    co_counts_test_fill(g_fake.image);
    co_registry_test_fill(g_fake.image);
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

static uint64_t Qword(uint32_t rva)
{
    uint64_t value;
    memcpy(&value, g_fake.image + rva, 8);
    return value;
}

/* The table's entry in each of the stand-in's registry lists leads to `table`; the other systems' entries stay. */
static int RegistryLeadsTo(uintptr_t table)
{
    const uint8_t* registry = g_fake.image + FAKE_REGISTRY;
    const uint64_t old = (uint64_t)(uintptr_t)(g_fake.image + FAKE_SYNCED_TABLE);
    uint64_t at[5];
    memcpy(&at[0], registry + 0x2078 + 8, 8);
    memcpy(&at[1], registry + 0x2078 + 8 + 16, 8);
    memcpy(&at[2], registry + 0x2078 + 8 + 32, 8);
    memcpy(&at[3], registry + 0x7230 + 8, 8);
    memcpy(&at[4], registry + 0x7248 + 8, 8);
    return at[0] == old + 0x100000u && at[2] == old + 0x300000u && at[1] == table && at[3] == table && at[4] == table;
}

/* Four entries as the game keeps them: a record of one byte value and the peer's id behind it. */
static void FillTable(uint8_t* table)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        const uint64_t peer = 0x7000u + i;
        memset(table + i * SYNCED_ENTRY, 0x40 + (int)i, 0x9F0);
        memcpy(table + i * SYNCED_ENTRY + 0x9F0, &peer, 8);
    }
}

static int TableHolds(const uint8_t* table)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        uint64_t peer;
        memcpy(&peer, table + i * SYNCED_ENTRY + 0x9F0, 8);
        if (peer != 0x7000u + i) return 0;
        for (uint32_t at = 0; at < 0x9F0; at++)
            if (table[i * SYNCED_ENTRY + at] != 0x40 + i) return 0;
    }
    return 1;
}

static int TableAsTheGameHasIt(void)
{
    const uintptr_t old = (uintptr_t)(g_fake.image + FAKE_SYNCED_TABLE);
    return co_counts_test_synced_table() == 0 && Qword(FAKE_SYNCED_POINTER) == old && RegistryLeadsTo(old);
}

/* Every byte of [from, to) holds `value`. */
static int AsTheGameHasIt(void)
{
    const uint8_t* image = g_fake.image;
    return image[LOOP_SITE + 2] == 4 && image[CLEAR_SITE] == 0x33 && image[CLEAR_SITE + 2] == 0x89 && image[SIZE_SITE] == 0x41 &&
           image[CAVE_SITE] == 0x48 && image[LAST_CAVE] == 0x48 && image[LAST_CAVE + 7] == 0 &&
           image[GUARD_SITE + 2] == 0xFF && image[GUARD_SITE + 4] == 0x84 && image[GUARD_NEXT + 2] == 0x30 &&
           image[GUARD_ROW] == 0xF2 && image[GUARD_CARD] == 0x8B && image[LAST_GUARD] == 0x41 && image[GUARD_ENTRY] == 0x48 &&
           image[GUARD_INPUT + 2] == 0xFF && image[EIGHT_INDEX + 2] == 4 && image[EIGHT_SOS + 2] == 4 && image[NET_SCORES] == 0x40 &&
           image[NET_SENDER] == 0x41 && image[TEST_SLOT] == 0x41 && image[TEST_INDEX] == 0x49 && image[MIRROR_CAP + 2] == 4 &&
           image[LOBBY_FULL + 2] == 4 && image[LOBBY_SIZE + 1] == 4 && image[LOBBY_FRIEND] == 0xBA && image[RECORD_COUNT] == 0x89 &&
           image[RECORD_POST + 3] == 0xD0 && image[RESULT_GATE + 2] == 3 && image[LOOT_INDEX] == 0x3B && image[ACTIVITY] == 0x48 &&
           image[ACTIVITY_MADE] == 0xFF &&
           image[CLAN_PAIR] == 0x40 && image[CLAN_OWN + 2] == 0xFF && image[CLAN_OWN + 3] == 0x74 &&
           co_counts_test_reviewed(image) && co_registry_test_reviewed(image) && TableAsTheGameHasIt();
}

/* The jump at a cave's site, the rest of the replaced bytes as nops; the cave it leads to. */
static const uint8_t* Cave(uint32_t site, uint32_t size)
{
    const uint8_t* image = g_fake.image;
    const uintptr_t caves = co_counts_test_caves();
    int32_t to;
    if (image[site] != 0xE9) return NULL;
    for (uint32_t i = 5; i < size; i++)
        if (image[site + i] != 0x90) return NULL;
    memcpy(&to, image + site + 1, 4);
    const uint8_t* cave = image + site + 5 + to;
    return (uintptr_t)cave >= caves && (uintptr_t)cave < caves + co_counts_test_room() ? cave : NULL;
}

/* A jump's or branch's four-byte distance at `at` leads to this place in the game. */
static int Leads(const uint8_t* at, uint32_t rva)
{
    int32_t to;
    memcpy(&to, at, 4);
    return at + 4 + to == g_fake.image + rva;
}

/* The stand-in has no code: the test puts its own where a cave goes back into the game, and makes it runnable. */
static void Landing(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* The post's original, as the test sees it: what the mod hands the game. */
static uint64_t g_posted[16];
static uint32_t g_postedCount;
static uint64_t Capture(uint64_t unused, uint64_t activity, uint64_t object, const uint64_t* ids, uint32_t count)
{
    g_postedCount = count;
    memcpy(g_posted, ids, (count < 16 ? count : 16u) * 8u);
    return 0;
}

int main(void)
{
    const int moves = (int)(co_session_test_sites() + co_session_test_stub_count());
    const int players = co_counts_test_players(), first = co_counts_test_first_slot();
    int writes;

    Start("the reviewed game");
    CHECK(g_fake.writes == 0 && AsTheGameHasIt());
    FillTable(g_fake.image + FAKE_SYNCED_TABLE);
    Init(CONTEXT);
    co_managers_test_force(1);
    co_ui_test_force(1);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1);
    CHECK(fake_host_logged("session: the session lists count to eight (143 code sites changed, 15 through caves)"));
    {
        static char line[96];
        snprintf(line, sizeof line, "players: %u accesses to four-entry tables of the game skip a player index above 3", co_counts_test_guards());
        CHECK(co_counts_test_guards() >= (players == 4 ? 77u : 29u) && fake_host_logged(line));
        snprintf(line, sizeof line, "session: %u guards keep the lists in messages inside the game's buffers", co_counts_test_net());
        CHECK(co_counts_test_net() >= 9 && fake_host_logged(line));
    }
    /* The build's player count: the log says what the join gate does, and the sites for eight are written with it. */
    CHECK((players == 4 || players == 8) && (first == 0 || players == 8));
    CHECK(fake_host_logged(players == 8 ? "; a session takes 8 players" : "; a session still takes 4 players"));
    CHECK(fake_host_logged("player indexes 4 to 7 are valid") == (players == 8));
    CHECK(g_fake.image[EIGHT_INDEX + 2] == players && g_fake.image[EIGHT_SOS + 2] == players && g_fake.image[EIGHT_SOS] == 0x83);
    CHECK(fake_host_logged("TEST BUILD") == (first != 0));
    CHECK((Cave(TEST_SLOT, 6) != NULL) == (first != 0) && (Cave(TEST_INDEX, 7) != NULL) == (first != 0));
    if (first == 0) CHECK(g_fake.image[TEST_SLOT] == 0x41 && g_fake.image[TEST_INDEX] == 0x49);
    /* The member lists fill eight in every build. The lobby closes, and is made, at the build's player count. */
    CHECK(g_fake.image[MIRROR_CAP] == 0x41 && g_fake.image[MIRROR_CAP + 2] == 8 && g_fake.image[MIRROR_COUNT + 6] == 8);
    CHECK(g_fake.image[LOBBY_FULL + 2] == players && g_fake.image[LOBBY_SIZE] == 0xB9 && g_fake.image[LOBBY_SIZE + 1] == players);
    CHECK(g_fake.image[LOBBY_POST] == (players == 8 ? 0xEB : 0x74));
    CHECK(fake_host_logged("and the lobby takes eight") == (players == 8));
    {
        /* mov edx,4; mov dword [r14+0x34C],8; jmp back: four inline members, eight places shown */
        static const uint8_t kFriend[] = {0xBA, 0x04, 0x00, 0x00, 0x00, 0x41, 0xC7, 0x86, 0x4C, 0x03, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xE9};
        const uint8_t* cave = Cave(LOBBY_FRIEND, 12);
        CHECK((cave != NULL) == (players == 8));
        if (cave != NULL) CHECK(memcmp(cave, kFriend, sizeof kFriend) == 0 && Leads(cave + 17, 0x103D129u));
        if (players != 8) CHECK(g_fake.image[LOBBY_FRIEND] == 0xBA);
    }
    {
        /* The own lobby's record, in every build: the post reads the members (+0x348); the cave stores the entries
         * (+0x5D0) and stops them at four. Run: a stub loads rbx and esi and jumps to the cave; where the cave goes
         * back, the test returns (pop rsi; pop rbx; ret). */
        static const uint8_t kPost[] = {0x44, 0x8B, 0x87, 0x48, 0x03, 0x00, 0x00};
        static const uint8_t kStore[] = {0x89, 0xB3, 0xD0, 0x05, 0x00, 0x00, 0x83, 0xFE, 0x04, 0x0F, 0x86};
        static const uint8_t kFour[] = {0xC7, 0x83, 0xD0, 0x05, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xE9};
        static const uint8_t kBack[] = {0x5E, 0x5B, 0xC3};
        /* push rbx; push rsi; mov rbx,rcx; mov esi,edx; jmp r8 */
        static const uint8_t kEnter[] = {0x53, 0x56, 0x48, 0x89, 0xCB, 0x89, 0xD6, 0x41, 0xFF, 0xE0};
        static const uint32_t kCounts[] = {0, 1, 3, 4, 5, 7, 8, 0x80000000u, 0xFFFFFFFFu};
        static uint8_t record[0x680];
        const uint8_t* cave = Cave(RECORD_COUNT, 6);
        CHECK(memcmp(g_fake.image + RECORD_POST, kPost, sizeof kPost) == 0 && cave != NULL);
        if (cave != NULL)
        {
            const RecordFn enter = (RecordFn)(uintptr_t)(g_fake.image + RECORD_ENTER);
            CHECK(memcmp(cave, kStore, sizeof kStore) == 0 && Leads(cave + 11, 0x133EF99u) && memcmp(cave + 15, kFour, sizeof kFour) == 0 &&
                  Leads(cave + 26, 0x133EF99u));
            Landing(0x133EF99u, kBack, sizeof kBack);
            Landing(RECORD_ENTER, kEnter, sizeof kEnter);
            for (size_t i = 0; i < sizeof kCounts / sizeof kCounts[0]; i++)
            {
                memset(record, 0xC5, sizeof record);
                *(uint32_t*)(record + 0x348) = kCounts[i];
                enter(record, kCounts[i], cave);
                CHECK(*(uint32_t*)(record + 0x5D0) == (kCounts[i] < 4 ? kCounts[i] : 4u) && *(uint32_t*)(record + 0x348) == kCounts[i]);
                CHECK(record[0x5CF] == 0xC5 && record[0x5D4] == 0xC5);
            }
        }
    }
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
        CHECK((uintptr_t)cave >= caves && (uintptr_t)cave < caves + co_counts_test_room() && cave[0] == 0x9C);
        /* behind the call: test rcx,rcx; jne game+0x108A426; jmp game+0x108A3FD; the callback's address */
        CHECK(cave[0x86] == 0x48 && cave[0x89] == 0x0F && cave[0x8F] == 0xE9);
        memcpy(&to, cave + 0x8B, 4);
        CHECK(cave + 0x8F + to == image + 0x108A426u);
        memcpy(&to, cave + 0x90, 4);
        CHECK(cave + 0x94 + to == image + 0x108A3FDu);
        CHECK(*(uint64_t*)(cave + 0x98) != 0 && (*(uint64_t*)(cave + 0x98) >> 40) != 0xC0DE00u >> 8);
    }
    {
        /* In place, in a build for four: cmp ecx,4; jae to where the game goes without an own index. A build for eight
         * keeps the game's test of -1 there. The slot loop goes on to its next slot in both. */
        static const uint8_t kOwn[] = {0x83, 0xF9, 0x04, 0x0F, 0x83}, kNone[] = {0x83, 0xF9, 0xFF, 0x0F, 0x84};
        const uint8_t* image = g_fake.image;
        const uint8_t* cave;
        CHECK(memcmp(image + GUARD_SITE, players == 4 ? kOwn : kNone, sizeof kOwn) == 0 && Leads(image + GUARD_SITE + 5, 0x1466F9Du));
        CHECK(image[GUARD_NEXT] == 0x0F && image[GUARD_NEXT + 1] == 0x83 && Leads(image + GUARD_NEXT + 2, 0x146C6B4u));

        /* cmp eax,4; jae the loop's next round; the replaced movsd; jmp back behind it */
        static const uint8_t kRow[] = {0x83, 0xF8, 0x04, 0x0F, 0x83}, kMoved[] = {0xF2, 0x41, 0x0F, 0x10, 0x45, 0x00, 0xE9};
        cave = Cave(GUARD_ROW, 6);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(memcmp(cave, kRow, sizeof kRow) == 0 && Leads(cave + 5, 0xB8F723u) && memcmp(cave + 9, kMoved, sizeof kMoved) == 0 &&
                  Leads(cave + 16, 0xB8F687u));

        /* Two ways out: back behind the replaced load for an index below the build's player count, into the loop's
         * tail (with r13d loaded again and eax zero) for the rest. The short jae leads to the second. A build for eight
         * reads the moved map. */
        static const uint8_t kTail[] = {0x44, 0x8B, 0xAC, 0x24, 0xE8, 0x00, 0x00, 0x00, 0x31, 0xC0, 0xE9};
        cave = Cave(GUARD_PEER, 10);
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            uint32_t map;
            memcpy(&map, cave + 12, 4);
            CHECK(cave[6] == players && map == (players == 8 ? 0x278968u : 0x27D8u));
            CHECK(cave[7] == 0x73 && cave + 9 + cave[8] == cave + 21 && cave[16] == 0xE9 && Leads(cave + 17, 0x146CC0Fu) &&
                  memcmp(cave + 21, kTail, sizeof kTail) == 0 && Leads(cave + 32, 0x146CF99u));
        }
        CHECK(Cave(LAST_GUARD, 10) != NULL);

        /* Run: the card function's cave with a ret where it goes back. An index below the build's player count arrives
         * there in eax, as the replaced mov leaves it; the rest get the function's "none" without going there. */
        static const uint8_t kRet[] = {0xC3};
        cave = Cave(GUARD_CARD, 5);
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            static const uint32_t kOver[] = {4, 5, 6, 7, 8, 9, 0x80000000u, 0xFFFFFFFFu};
            const CardFn card = (CardFn)(uintptr_t)cave;
            Landing(0x146D325u, kRet, sizeof kRet);
            for (uint32_t i = 0; i < (uint32_t)players; i++) CHECK(card(NULL, i) == i);
            for (size_t i = 0; i < sizeof kOver / sizeof kOver[0]; i++)
                if (kOver[i] >= (uint32_t)players) CHECK(card(NULL, kOver[i]) == 0xFFFFFFFFu);
        }

        /* Run: the cursor function's cave. Where it goes back the test undoes the replaced push and sub and notes the
         * index (add rsp,0x20; pop rbx; mov [rcx],edx; ret); 4 and more return before that. In a build for eight the
         * map's guards are co_ui.c's (tests/ui_test.c). */
        static const uint8_t kNote[] = {0x48, 0x83, 0xC4, 0x20, 0x5B, 0x89, 0x11, 0xC3};
        cave = Cave(GUARD_CURSOR, 6);
        CHECK((cave != NULL) == (players == 4));
        if (players == 8) CHECK(g_fake.image[GUARD_CURSOR] == 0x40);
        if (cave != NULL)
        {
            const CursorFn cursor = (CursorFn)(uintptr_t)cave;
            Landing(0x72F706u, kNote, sizeof kNote);
            for (uint32_t i = 0; i < 9; i++)
            {
                uint32_t seen = 99;
                cursor(&seen, i);
                CHECK(seen == (i < 4 ? i : 99));
            }
        }
        /* The two cursor writers that had no guard: index in edx and in r8d. Each returns before the replaced store
         * for 4 and more; behind it the test notes the index (mov [rcx],edx or r8d; ret). */
        static const uint8_t kEdx[] = {0x89, 0x11, 0xC3}, kR8[] = {0x44, 0x89, 0x01, 0xC3};
        const uint8_t* byEdx = Cave(CURSOR_EDX, 5);
        const uint8_t* byR8 = Cave(CURSOR_R8, 5);
        CHECK((byEdx != NULL) == (players == 4) && (byR8 != NULL) == (players == 4));
        /* The name tags' guard too: cmp dword [rsp+0x20],4 in a build for four; co_ui.c's in a build for eight. */
        const uint8_t* tags = Cave(0x18AF049u, 8);
        CHECK((tags != NULL) == (players == 4));
        if (tags != NULL) CHECK(tags[0] == 0x83 && tags[1] == 0x7C && tags[4] == 4 && Leads(tags + 7, 0x18AEDB9u));
        if (byEdx != NULL && byR8 != NULL)
        {
            Landing(CURSOR_EDX + 5u, kEdx, sizeof kEdx);
            Landing(CURSOR_R8 + 5u, kR8, sizeof kR8);
            for (uint32_t i = 0; i < 10; i++)
            {
                const uint32_t n = i == 9 ? 0xFFFFFFFFu : i;
                uint32_t seen[2] = {99, 99};
                ((CursorFn)(uintptr_t)byEdx)(&seen[0], n);
                ((EntryFn)(uintptr_t)byR8)(&seen[1], NULL, n, 0);
                CHECK(seen[0] == (n < 4 ? n : 99) && seen[1] == (n < 4 ? n : 99));
            }
        }
    }
    {
        /* A build for eight gives the loadout screen eight records and cards in a bigger allocation; a build for four
         * leaves the screen as the game has it. */
        const uint8_t* image = g_fake.image;
        static char line[96];
        uint32_t value;
        snprintf(line, sizeof line, "players: the loadout screen has eight records and cards (%u code sites changed)", co_counts_test_loadout());
        CHECK(co_counts_test_loadout() == 471u && fake_host_logged(line) == (players == 8));
        memcpy(&value, image + LOADOUT_SIZE + 1, 4);
        CHECK(image[LOADOUT_SIZE] == 0xBA && value == (players == 8 ? 0x36FA70u : 0x2739D0u));
        CHECK(image[LOADOUT_BOUND] == 0xBF && image[LOADOUT_BOUND + 1] == players);
        memcpy(&value, image + LOADOUT_MAP + 3, 4);
        CHECK(value == (players == 8 ? 0x278968u : 0x27D8u));
        /* mov edi,8; lea rbx,[rsi+the new cards]; jmp back behind the replaced lea */
        static const uint8_t kCount[] = {0xBF, 0x08, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x9E, 0xA8, 0x89, 0x27, 0x00, 0xE9};
        const uint8_t* cave = Cave(LOADOUT_CAVE, 7);
        CHECK((cave != NULL) == (players == 8));
        if (cave != NULL) CHECK(memcmp(cave, kCount, sizeof kCount) == 0 && Leads(cave + 13, 0x147184Fu));
        else CHECK(image[LOADOUT_CAVE] == 0x48);
    }
    {
        /* A build for eight gives the mission summary eight rows and cards in a bigger allocation and bounds the rows'
         * guards at eight; a build for four keeps the screen and bounds them at four. */
        const uint8_t* image = g_fake.image;
        static char line[96];
        uint32_t value;
        snprintf(line, sizeof line, "players: the mission summary has eight rows and cards (%u code sites changed)", co_counts_test_summary());
        CHECK(co_counts_test_summary() == 63u && fake_host_logged(line) == (players == 8));
        memcpy(&value, image + SUMMARY_SIZE + 1, 4);
        CHECK(image[SUMMARY_SIZE] == 0xBA && value == (players == 8 ? 0x120C70u : 0x8A540u));
        CHECK(image[SUMMARY_ROWS] == 0xBD && image[SUMMARY_ROWS + 1] == players);
        /* cmp eax,4 or 8; jae the row's skip; the replaced imul rdx,rax,0x24C; jmp back */
        static const uint8_t kImul[] = {0x48, 0x69, 0xD0, 0x4C, 0x02, 0x00, 0x00, 0xE9};
        const uint8_t* cave = Cave(SUMMARY_GUARD, 7);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(cave[0] == 0x83 && cave[1] == 0xF8 && cave[2] == players && cave[3] == 0x0F && cave[4] == 0x83 && Leads(cave + 5, 0x19943B7u) &&
                  memcmp(cave + 9, kImul, sizeof kImul) == 0 && Leads(cave + 17, 0x1994233u));
        CHECK((Cave(SUMMARY_CARDS, 7) != NULL) == (players == 8));
        if (players != 8) CHECK(image[SUMMARY_CARDS] == 0x83);
    }
    {
        /* The per-peer loadout table. A build for eight leads its pointer and its three registry entries to the mod's
         * room for eight, with the four entries copied and the other four empty; its memset and bounds say eight. A
         * build for four leaves all of it as the game has it. */
        const uint8_t* image = g_fake.image;
        const uintptr_t old = (uintptr_t)(image + FAKE_SYNCED_TABLE), table = co_counts_test_synced_table();
        static char line[96];
        uint32_t size;
        snprintf(line, sizeof line, "players: the per-peer loadout table moved to room for eight peers (%u code sites changed)", co_counts_test_synced());
        CHECK(co_counts_test_synced() == 12u && fake_host_logged(line) == (players == 8));
        memcpy(&size, image + SYNCED_CLEAR + 2, 4);
        CHECK(size == (players == 8 ? 0x4FC0u : 0x27E0u) && image[SYNCED_SEARCH + 2] == players && image[SYNCED_GONE + 2] == players);
        CHECK((table != 0) == (players == 8) && TableHolds(image + FAKE_SYNCED_TABLE));
        if (table != 0)
        {
            int empty = 1;
            for (uint32_t at = 4 * SYNCED_ENTRY; at < 8 * SYNCED_ENTRY; at++) empty &= ((const uint8_t*)table)[at] == 0;
            CHECK(Qword(FAKE_SYNCED_POINTER) == table && RegistryLeadsTo(table) && table % 16 == old % 16 && TableHolds((const uint8_t*)table) && empty);
        }
        else CHECK(Qword(FAKE_SYNCED_POINTER) == old && RegistryLeadsTo(old));
    }
    {
        /* The guards for player indexes 4 to 7 that came with the join limit. In place: cmp edi,4; jae (a build for
         * four; a build for eight leaves the map's to co_ui.c). */
        static const uint8_t kInput[] = {0x83, 0xFF, 0x04, 0x0F, 0x83};
        const uint8_t* image = g_fake.image;
        const uint8_t* cave;
        if (players == 4) CHECK(memcmp(image + GUARD_INPUT, kInput, sizeof kInput) == 0 && Leads(image + GUARD_INPUT + 5, 0x18C12DAu));
        else CHECK(image[GUARD_INPUT + 2] == 0xFF);

        /* Run: where each cave goes back the test notes the index (mov [rcx],r8d or edx; ret). 4 and more, -1 too,
         * return before that. */
        static const uint8_t kNoteR8[] = {0x44, 0x89, 0x01, 0xC3}, kNoteEdx[] = {0x89, 0x11, 0xC3};
        cave = Cave(GUARD_ENTRY, 5);
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            const EntryFn entry = (EntryFn)(uintptr_t)cave;
            Landing(0x139D305u, kNoteR8, sizeof kNoteR8);
            for (uint32_t i = 0; i < 9; i++)
            {
                uint32_t seen = 99;
                entry(&seen, NULL, i == 8 ? 0xFFFFFFFFu : i, 0);
                CHECK(seen == (i < 4 ? i : 99));
            }
        }
        cave = Cave(GUARD_MAP, 5);
        CHECK((cave != NULL) == (players == 4));
        if (cave != NULL)
        {
            const CursorFn map = (CursorFn)(uintptr_t)cave;
            Landing(0x14335F5u, kNoteEdx, sizeof kNoteEdx);
            for (uint32_t i = 0; i < 9; i++)
            {
                uint32_t seen = 99;
                map(&seen, i);
                CHECK(seen == (i < 4 ? i : 99));
            }
        }
        /* cmp eax,4; jb on; cmp eax,-1; jne the function's end; on: the two replaced loads; jmp back */
        static const uint8_t kBits[] = {0x83, 0xF8, 0x04, 0x72, 0x09, 0x83, 0xF8, 0xFF, 0x0F, 0x85};
        static const uint8_t kLoads[] = {0x44, 0x8B, 0x4D, 0x6F, 0x41, 0xB8, 0x04, 0x00, 0x00, 0x00, 0xE9};
        cave = Cave(GUARD_BITS, 10);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(memcmp(cave, kBits, sizeof kBits) == 0 && Leads(cave + 10, 0x13D173Eu) && memcmp(cave + 14, kLoads, sizeof kLoads) == 0 &&
                  Leads(cave + 25, 0x13D1123u));
    }
    {
        /* Message guards. Run: the scores' handler with a ret where its cave goes back (add rsp,0x50; pop rbx;
         * mov eax,0x77; ret undoes the replaced push and sub). Lists of up to five get there; a longer one returns
         * from the handler before that, and the mod says so once. */
        static const uint8_t kHandled[] = {0x48, 0x83, 0xC4, 0x50, 0x5B, 0xB8, 0x77, 0x00, 0x00, 0x00, 0xC3};
        const uint8_t* cave = Cave(NET_SCORES, 6);
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            const HandlerFn handler = (HandlerFn)(uintptr_t)cave;
            Landing(0xBAFF76u, kHandled, sizeof kHandled);
            for (uint32_t ids = 0; ids < 8; ids++)
                for (uint32_t scores = 0; scores < 8; scores++)
                {
                    const uint32_t lists[] = {ids, scores};
                    CHECK((handler(NULL, Arguments(0, lists, 2)) == 0x77) == (ids <= 5 && scores <= 5));
                }
            CHECK(fake_host_logged("session: a message with a list of 6 entries was ignored; its handler at game+0xBAFF70 holds 5"));
        }
        /* The lobby entry's handler: its cave is the first message guard, so its shape has number 0. */
        cave = Cave(NET_LOBBY, 5);
        CHECK(cave != NULL && cave[0] == 0xB8 && cave[1] == 0);
        {
            uint32_t lobby[] = {4, 4, 4, 4, 4, 4, 4, 8, 4};
            /* Two values stand between the seventh list and the eighth: the lists are given in two parts. */
            static uint64_t entries[2 * 128];
            for (int longer = -1; longer < 9; longer++)
            {
                uint32_t at = 12;
                memset(entries, 0, sizeof entries);
                if (longer >= 0) lobby[longer]++;
                for (int i = 0; i < 9; i++)
                {
                    if (i == 7) at += 2;
                    entries[at * 2 + 1] = lobby[i];
                    at += 1 + lobby[i];
                }
                CHECK(co_counts_test_fits(0, entries) == (longer < 0));
                if (longer >= 0) lobby[longer]--;
            }
            /* The mission result: a value, five lists of eight, four of fifteen, four values, then lists of four
             * with a value behind the first. The party lists: a value, then five peers. */
            const uint32_t fits[] = {8, 8, 8, 8, 8, 15, 15, 15, 15}, over[] = {8, 8, 9};
            const uint32_t five[] = {5}, six[] = {6}, items[] = {128, 4}, more[] = {128, 5};
            CHECK(co_counts_test_fits(1, Arguments(1, fits, 9)) && !co_counts_test_fits(1, Arguments(1, over, 3)));
            CHECK(co_counts_test_fits(2, Arguments(0, items, 2)) && !co_counts_test_fits(2, Arguments(0, more, 2)));
            CHECK(co_counts_test_fits(3, Arguments(1, five, 1)) && !co_counts_test_fits(3, Arguments(1, six, 1)));
            CHECK(co_counts_test_fits(4, Arguments(1, five, 1)) && !co_counts_test_fits(4, Arguments(1, six, 1)));
        }
        /* Where a handler reads its count: mov ecx,[rdx+0x18]; cmp ecx,5; ja the handler's end; the replaced store. */
        static const uint8_t kCount[] = {0x8B, 0x4A, 0x18, 0x83, 0xF9, 0x05, 0x0F, 0x87}, kStore[] = {0x0F, 0x11, 0x44, 0x24, 0x20, 0xE9};
        cave = Cave(NET_ACCEPTED, 8);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(memcmp(cave, kCount, sizeof kCount) == 0 && Leads(cave + 8, 0xBAC79Cu) && memcmp(cave + 12, kStore, sizeof kStore) == 0 &&
                  Leads(cave + 18, 0xBAC755u));
        /* The sender's member count: the load; cmp ecx,4; jbe back; mov ecx,4; jmp back */
        static const uint8_t kMembers[] = {0x41, 0x8B, 0x8F, 0xD0, 0x05, 0x00, 0x00, 0x83, 0xF9, 0x04, 0x76, 0x05, 0xB9, 0x04, 0x00, 0x00, 0x00, 0xE9};
        cave = Cave(NET_SENDER, 7);
        CHECK(cave != NULL);
        if (cave != NULL) CHECK(memcmp(cave, kMembers, sizeof kMembers) == 0 && Leads(cave + 18, 0x133931Au));
        /* The scores' sender: cmp edi,5; jae next peer; cmp esi,5; jae next peer; the replaced append */
        static const uint8_t kFull[] = {0x83, 0xFF, 0x05, 0x0F, 0x83}, kToo[] = {0x83, 0xFE, 0x05, 0x0F, 0x83};
        static const uint8_t kAppend[] = {0x89, 0xF8, 0x89, 0xF1, 0x48, 0x89, 0x9C, 0xC5, 0xB0, 0x01, 0x00, 0x00, 0xE9};
        cave = Cave(NET_APPEND, 12);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(memcmp(cave, kFull, sizeof kFull) == 0 && Leads(cave + 5, 0x108AC24u) && memcmp(cave + 9, kToo, sizeof kToo) == 0 &&
                  Leads(cave + 14, 0x108AC24u) && memcmp(cave + 18, kAppend, sizeof kAppend) == 0 && Leads(cave + 31, 0x108ABFEu));
    }
    {
        /* A joining peer's slot and player index on the host. Without the test option both are the lowest free ones
         * (slot 0 is the host's). With it they start at the option's number and go lower only when none is left. */
        static uint8_t sync[0x200];
        uint8_t flags[8] = {1, 0, 0, 0, 0, 0, 0, 0};
        const int start = first ? first : 1;
        memset(sync, 0, sizeof sync);
        *(uint64_t*)(sync + 0x20) = 11;
        CHECK(co_counts_test_free((uintptr_t)sync) == (uint32_t)(first ? 8 - first : players - 1));
        for (int i = 0; i < 7; i++)
        {
            const int expected = start + i < 8 ? start + i : start + i - 7;
            const int slot = co_counts_test_join_slot((uintptr_t)sync);
            CHECK(slot == expected && co_counts_test_join_index(flags) == (uint32_t)expected);
            if (slot > 0) *(uint64_t*)(sync + 0x20 + (size_t)slot * 0x38) = 100 + (uint64_t)i;
        }
        CHECK(co_counts_test_join_slot((uintptr_t)sync) == -1 && co_counts_test_join_index(flags) == 0xFFFFFFFFu);
        CHECK(co_counts_test_free((uintptr_t)sync) == 0);
        for (int i = 0; i < 8; i++) CHECK(flags[i] == 1);
        if (first != 0)
        {
            /* The slot cave: behind the call it loads r9 as the game's own search does and goes on where the slot is
             * filled. The index cave: the replaced store, the call, on to where the game stores the index. */
            static const uint8_t kSlot[] = {0x4C, 0x8B, 0x4D, 0xA7, 0xE9}, kIndex[] = {0x44, 0x89, 0x72, 0x24, 0x9C};
            const uint8_t* cave = Cave(TEST_SLOT, 6);
            if (cave != NULL) CHECK(cave[0] == 0x9C && memcmp(cave + 0x86, kSlot, sizeof kSlot) == 0 && Leads(cave + 0x8B, 0x108C3B0u));
            cave = Cave(TEST_INDEX, 7);
            if (cave != NULL) CHECK(memcmp(cave, kIndex, sizeof kIndex) == 0 && cave[0x8A] == 0xE9 && Leads(cave + 0x8B, 0x108C3F7u));
        }
    }
    {
        /* A stand-in host synchronizer: slot i at +0x20 + i * 0x38 with key, player index +0x14, state +0x20. */
        static uint8_t sync[0x200];
        const uintptr_t ext = co_session_test_extension();
        memset(sync, 0, sizeof sync);
        *(uint64_t*)(sync + 0x20) = 11; *(uint32_t*)(sync + 0x40) = 9;
        *(uint64_t*)(sync + 0x20 + 2 * 0x38) = 12; *(uint32_t*)(sync + 0x40 + 2 * 0x38) = 5; *(uint32_t*)(sync + 0x34 + 2 * 0x38) = 1;
        /* Free: the player count less the used slots; with the test option no more than the free slots it hands out. */
        const uint32_t two = (uint32_t)players - 2, three = (uint32_t)players - 3;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2);
        CHECK(co_counts_test_free((uintptr_t)sync) == (first && 8u - (uint32_t)first < two ? 8u - (uint32_t)first : two));
        *(uint64_t*)(sync + 0x20 + 5 * 0x38) = 13; *(uint32_t*)(sync + 0x40 + 5 * 0x38) = 4;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2);
        CHECK(co_counts_test_free((uintptr_t)sync) == (first && first <= 5 && 7u - (uint32_t)first < three ? 7u - (uint32_t)first : three));
        *(uint32_t*)(sync + 0x40 + 5 * 0x38) = 7; *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 0xFFFFFFFFu;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2);
        *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 3;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 3);
        /* A player index above 3 counts only in a build for eight. */
        *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 5;
        CHECK(co_counts_test_ready((uintptr_t)sync) == (players == 8 ? 3u : 2u));
        *(uint32_t*)(sync + 0x34 + 5 * 0x38) = 8;
        CHECK(co_counts_test_ready((uintptr_t)sync) == 2);

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
    {
        /* The join limit. A build for eight takes eight players while the managers have eight slots; without them it
         * takes four and says so once, when the session is full. A build for four takes four either way. */
        static uint8_t sync[0x200];
        const uint32_t open = first > 4 ? 8u - (uint32_t)first : 4u, alone = first ? 8u - (uint32_t)first : 7u;
        memset(sync, 0, sizeof sync);
        *(uint64_t*)(sync + 0x20) = 11;
        co_managers_test_force(0);
        CHECK(co_counts_test_free((uintptr_t)sync) == (alone < 3 ? alone : 3u));
        for (uint32_t i = 1; i < 4; i++) *(uint64_t*)(sync + 0x20 + i * 0x38) = 11 + i;
        CHECK(!fake_host_logged("the session is full at four players"));
        CHECK(co_counts_test_free((uintptr_t)sync) == 0);
        CHECK(fake_host_logged("session: the session is full at four players: the player managers have four slots") == (players == 8));
        co_managers_test_force(1);
        CHECK(co_counts_test_free((uintptr_t)sync) == (players == 8 ? open : 0u));
    }
    {
        /* The mission result takes player counts 1 to 8 in a build for eight. The score manager's loot guards, the
         * mission result's slot guard and the Activity record's caves are in every build; the result's load by score slot
         * is the managers' site, no jump. The loot rows' clears and sums are the managers' caves, not these. */
        static const struct { uint32_t rva, size; } kLoot[] = {
            {0x6311D4u, 8}, {0x631514u, 6}, {0x631693u, 7}, {LOOT_RESULT, 10}, {ACTIVITY, 5}, {0xA56E91u, 5}, {0x12E67B1u, 5}, {ACTIVITY_MADE, 6},
        };
        /* cmp eax,3; jae on to the next id: the second record maker's list of the peers whose answer it waits for; the
         * first's goes to its callback instead (cmp eax,3; jb the store) */
        static const uint8_t kPending[] = {0x83, 0xF8, 0x03, 0x0F, 0x83}, kWaitStore[] = {0x83, 0xF8, 0x03, 0x0F, 0x82};
        static const uint8_t kStore[] = {0x48, 0x89, 0x4C, 0xC5, 0xE8, 0xE9};
        CHECK(g_fake.image[RESULT_GATE] == 0x83 && g_fake.image[RESULT_GATE + 2] == (players == 8 ? 7 : 3));
        CHECK(fake_host_logged("players: the score manager's loot rows by index are bounded where the managers keep them (3 caves)"));
        for (size_t i = 0; i < sizeof kLoot / sizeof kLoot[0]; i++) CHECK(Cave(kLoot[i].rva, kLoot[i].size) != NULL);
        CHECK(g_fake.image[LOOT_LOAD] != 0xE9 && g_fake.image[0x62D24Fu] != 0xE9 && g_fake.image[0x6309B7u] != 0xE9);
        const uint8_t* cave = Cave(ACTIVITY_WAIT, 5);
        if (cave != NULL)
        {
            int32_t to;
            memcpy(&to, cave + 5, 4);
            CHECK(memcmp(cave, kWaitStore, sizeof kWaitStore) == 0 && memcmp(cave + 9 + to, kStore, sizeof kStore) == 0 && Leads(cave + 15 + to, 0xA56E96u));
        }
        cave = Cave(0x12E67B1u, 5);
        if (cave != NULL) CHECK(memcmp(cave, kPending, sizeof kPending) == 0 && Leads(cave + 5, 0x12E67BEu));
        /* the three replaced instructions, then cmp r13d,4; jb the load; test dl,dl; je the slot without row values */
        static const uint8_t kSlot[] = {0x4D, 0x01, 0xE9, 0x89, 0x44, 0x24, 0x3C, 0x41, 0x89, 0xC2, 0x41, 0x83, 0xFD, 0x04, 0x0F, 0x82};
        static const uint8_t kFlagged[] = {0x84, 0xD2, 0x0F, 0x84};
        cave = Cave(LOOT_RESULT, 10);
        if (cave != NULL)
            CHECK(memcmp(cave, kSlot, sizeof kSlot) == 0 && Leads(cave + 16, LOOT_LOAD) && memcmp(cave + 20, kFlagged, sizeof kFlagged) == 0 &&
                  Leads(cave + 24, 0x62F358u));
    }
    {
        /* The loot callbacks. Four rows of 0x18 by score slot at +0xC440 and four by player index at +0xC4A0 while the
         * managers keep four slots; eight of each at +0x37F20 and +0x37FE0 once they have moved. A row's counts are at
         * +8, +0xC and +0x10. Run as the game reaches them: the index guard's cave goes on for an index with a row and
         * skips the rest. The mission result's cave goes to the load for slots 0 to 3; for 4 and up it skips the row,
         * and a flagged slot adds its row's counts to the total at [rsp+0x34], which edx carries on. */
        static uint8_t manager[MANAGER_SIZE + 0x40];
        static const uint8_t kIndex[] = {0x89, 0xC8, 0xFF, 0xE2};                      /* mov eax,ecx; jmp rdx */
        static const uint8_t kSkip[] = {0x31, 0xC0, 0xC3}, kOn[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
        /* push r12; push r13; sub rsp,0x48; mov r12,rcx; mov r13d,edx; mov edx,r8d; mov [rsp+0x34],r9d; xor eax,eax;
         * jmp [the cave] */
        static uint8_t kResult[] = {0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x48, 0x49, 0x89, 0xCC, 0x41, 0x89, 0xD5, 0x44, 0x89, 0xC2,
                                    0x44, 0x89, 0x4C, 0x24, 0x34, 0x31, 0xC0, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
        /* the load: mov eax,0xE5; the slot without row values: mov al,0x58 and on to the end; the next slot: edx must be
         * [rsp+0x34], then eax = edx (else -1); the end: add rsp,0x48; pop r13; pop r12; ret */
        static const uint8_t kLoad[] = {0xB8, 0xE5, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x48, 0x41, 0x5D, 0x41, 0x5C, 0xC3};
        static const uint8_t kNoRow[] = {0xB0, 0x58, 0xEB, 0x0F};
        static const uint8_t kNext[] = {0x3B, 0x54, 0x24, 0x34, 0x75, 0x04, 0x89, 0xD0, 0xEB, 0x05, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0x48, 0x83, 0xC4, 0x48, 0x41, 0x5D, 0x41, 0x5C, 0xC3};
        const uint8_t* index = Cave(LOOT_INDEX, 8);
        const uint8_t* result = Cave(LOOT_RESULT, 10);
        CHECK(index != NULL && result != NULL);
        Landing(ENTER, kIndex, sizeof kIndex);
        Landing(0x63130Bu, kSkip, sizeof kSkip);
        Landing(0x6311DCu, kOn, sizeof kOn);
        if (result != NULL) memcpy(kResult + 30, &result, 8);
        Landing(ENTER + 0x40u, kResult, sizeof kResult);
        Landing(LOOT_LOAD, kLoad, sizeof kLoad);
        Landing(0x62F358u, kNoRow, sizeof kNoRow);
        Landing(0x62F35Cu, kNext, sizeof kNext);
        CHECK(0x62F358u + 4u + 0x0Fu == 0x62F35Cu + 15u);   /* kNoRow's jump lands on kNext's end */
        for (int moved = 0; moved < 2; moved++)
        {
            const uint32_t rows = moved ? 8u : 4u, slots = moved ? 0x37F20u : 0xC440u;
            co_managers_test_force(moved);
            /* row i counts i+1, 0x100 << i and 0x10000 at +8, +0xC and +0x10 where the rows are now; the old place's rows
             * after the fourth (the rows by index) hold 0x5000000 to show they are not read */
            memset(manager, 0, sizeof manager);
            for (uint32_t i = 0; i < 8; i++)
            {
                uint32_t* counts = (uint32_t*)(manager + slots + i * 0x18u + 8u);
                if (moved || i < 4) { counts[0] = i + 1; counts[1] = 0x100u << i; counts[2] = 0x10000u; }
                else counts[0] = counts[1] = counts[2] = 0x5000000u;
            }
            for (uint32_t slot = 0; slot < 10; slot++)
            {
                const uint32_t row = slot < rows ? (slot + 1) + (0x100u << slot) + 0x10000u : 0u;
                CHECK(co_counts_test_loot_row((uintptr_t)manager, slot) == row);
                if (result == NULL || slot >= 9) continue;
                const ResultFn run = (ResultFn)(uintptr_t)(g_fake.image + ENTER + 0x40u);
                CHECK(run(manager, slot, 0, 7) == (slot < 4 ? 0xE5u : 0x58u));
                CHECK(run(manager, slot, 1, 7) == (slot < 4 ? 0xE5u : 7u + row));
            }
            for (uint32_t i = 0; i < 11; i++)
            {
                const uint32_t n = i == 10 ? 0xFFFFFFFFu : i;
                CHECK(co_counts_test_loot_index(n, 0x246) == (0x246u | (n < rows)) && co_counts_test_loot_index(n, 0x247) == (0x246u | (n < rows)));
                if (index != NULL) CHECK(((IndexFn)(uintptr_t)(g_fake.image + ENTER))(n, index) == (n < rows));
            }
        }
        co_managers_test_force(1);
    }
    {
        /* Run: the Activity record's participants. Four ids fit, the count stands behind them: the cave stores ids 0
         * to 3 and skips the store and the count for the rest. The local peer (r14 is the session context, its id at
         * +0xB398) found after the fourth takes the fourth place. */
        /* push rbp; push r14; mov rbp,rcx; lea r14,[rcx-0xB398]; mov eax,edx; mov rdx,r8; jmp r9 */
        static const uint8_t kEnter[] = {0x55, 0x41, 0x56, 0x48, 0x89, 0xCD, 0x4C, 0x8D, 0xB1, 0x68, 0x4C, 0xFF, 0xFF, 0x89, 0xD0, 0x4C, 0x89, 0xC2, 0x41, 0xFF, 0xE1};
        /* pop r14; pop rbp; push 1; pop rax; ret  /  pop r14; pop rbp; xor eax,eax; ret */
        static const uint8_t kStored[] = {0x41, 0x5E, 0x5D, 0x6A, 0x01, 0x58, 0xC3}, kLeft[] = {0x41, 0x5E, 0x5D, 0x31, 0xC0, 0xC3};
        const uint8_t* cave = Cave(ACTIVITY, 5);
        static uint64_t frame[16];
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            Landing(ENTER + 0x20u, kEnter, sizeof kEnter);
            Landing(0xA56D8Du, kStored, sizeof kStored);
            Landing(0xA56D95u, kLeft, sizeof kLeft);
            for (uint32_t i = 0; i < 9; i++)
            {
                memset(frame, 0xC5, sizeof frame);
                frame[13] = 0x5006u;   /* rbp + 0: the local peer's id, where r14 + 0xB398 leads */
                const uint32_t stored = ((StoreFn)(uintptr_t)(g_fake.image + ENTER + 0x20u))((uint8_t*)frame + 0x68, i, 0x5000u + i, cave);
                CHECK(stored == (i < 4));
                for (uint32_t j = 0; j < 13; j++)
                    CHECK(frame[j] == (i < 4 && j == i ? 0x5000u + i : i == 6 && j == 3 ? 0x5006u : 0xC5C5C5C5C5C5C5C5u));
            }
        }
    }
    {
        /* The Activity record's post. A list of 1 to 8 ready ids, the local peer at each place: the record gets the first
         * four, the local peer among them; the post lists all of them once, the record's four first. A second post of the
         * record, a record outside the table, and a record with fewer than four list its own. */
        static uint8_t table[32 * 0x88 + 0x10];
        static uint64_t frame[16], context[0xB3A0 / 8];
        PostFn post = (PostFn)fake_host_detour(ACTIVITY_POST);
        const uint64_t local = 0x9000u;
        const uint64_t tableAt = (uint64_t)(uintptr_t)table;
        CHECK(post != NULL);
        memcpy(g_fake.image + ACTIVITY_TABLE, &tableAt, 8);
        co_counts_test_post_original(Capture);
        context[0xB398 / 8] = local;
        for (uint32_t n = 1; n <= 8 && post != NULL; n++)
            for (uint32_t at = 0; at < n; at++)
            {
                uint64_t ids[8];
                const uint32_t place = (n * 7 + at) % 32;
                uint8_t* record = table + place * 0x88u;
                for (uint32_t i = 0; i < n; i++) ids[i] = i == at ? local : 0x7000u + i;
                memset(frame, 0, sizeof frame);
                for (uint32_t i = 0; i < n; i++)
                {
                    co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, ids[i]);
                    if (i < 4) frame[i] = ids[i];
                }
                const uint32_t kept = n < 4 ? n : 4u;
                memset(record, 0, 0x88);
                *(uint64_t*)record = 0x1234500u + n * 16u + at;
                memcpy(record + 0x50, frame, kept * 8u);
                *(uint32_t*)(record + 0x70) = kept;
                co_counts_test_record((uintptr_t)table, place * 0x88u);
                CHECK(memcmp(record + 0x50, ids, (kept < 4 || at < 4 ? kept : 3u) * 8u) == 0 && (at < 4 || *(uint64_t*)(record + 0x68) == local));
                g_postedCount = 0;
                post(0, 0x7549A744u, 77, (const uint64_t*)(record + 0x50), kept);
                CHECK(g_postedCount == n);
                int once = 1;
                for (uint32_t i = 0; i < n; i++)
                {
                    uint32_t found = 0;
                    for (uint32_t j = 0; j < g_postedCount; j++) found += g_posted[j] == ids[i];
                    once &= found == 1;
                }
                CHECK(once && memcmp(g_posted, record + 0x50, kept * 8u) == 0);
                post(0, 0x7549A744u, 77, (const uint64_t*)(record + 0x50), kept);
                CHECK(g_postedCount == kept);
            }
        /* the same key again with three participants: nothing waits for it */
        {
            uint8_t* record = table;
            for (uint32_t i = 0; i < 6; i++) co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, 0x7100u + i);
            memset(record, 0, 0x88);
            *(uint64_t*)record = 0xABCDu;
            *(uint32_t*)(record + 0x70) = 4;
            co_counts_test_record((uintptr_t)table, 0);
            *(uint32_t*)(record + 0x70) = 3;
            co_counts_test_record((uintptr_t)table, 0);
            *(uint32_t*)(record + 0x70) = 4;
            post(0, 1, 2, (const uint64_t*)(record + 0x50), 4);
            CHECK(g_postedCount == 4);
            /* a list that is not a record of the table goes as it is */
            for (uint32_t i = 0; i < 6; i++) co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, 0x7200u + i);
            co_counts_test_record((uintptr_t)table, 0);
            post(0, 1, 2, (const uint64_t*)(record + 0x58), 4);
            CHECK(g_postedCount == 4 && g_posted[0] == *(uint64_t*)(record + 0x58));
            post(0, 1, 2, (const uint64_t*)(record + 0x50), 4);
            CHECK(g_postedCount == 6);
        }
        /* 31 records of the table keep their participants when a 32nd comes: it takes the entry of a record that left
         * the table without a post (here the 32nd record before it, at the same place). */
        for (uint32_t r = 0; r < 33 && post != NULL; r++)
        {
            const uint32_t place = r < 32 ? r : 31u;
            uint8_t* record = table + place * 0x88u;
            for (uint32_t i = 0; i < 5; i++) co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, 0x8000u + r * 8u + i);
            memset(record, 0, 0x88);
            *(uint64_t*)record = 0xC000u + r;
            *(uint32_t*)(record + 0x70) = 4;
            *(uint32_t*)(table + 0x1100) = place + 1u;
            co_counts_test_record((uintptr_t)table, place * 0x88u);
        }
        for (uint32_t place = 0; place < 32 && post != NULL; place++)
        {
            post(0, 1, 2, (const uint64_t*)(table + place * 0x88u + 0x50), 4);
            CHECK(g_postedCount == 5 && g_posted[4] == 0x8000u + (place < 31 ? place : 32u) * 8u + 4u);
        }
        memset(table, 0, sizeof table);
        /* Run: the cave behind the record's store counts the record and keeps the waiting participants under its key. */
        {
            /* push rsi; mov rsi,rcx; mov rcx,rdx; jmp r8  /  mov eax,[rsi+0x1100]; pop rsi; ret */
            static const uint8_t kMade[] = {0x56, 0x48, 0x89, 0xCE, 0x48, 0x89, 0xD1, 0x41, 0xFF, 0xE0};
            static const uint8_t kCounted[] = {0x8B, 0x86, 0x00, 0x11, 0x00, 0x00, 0x5E, 0xC3};
            const uint8_t* made = Cave(ACTIVITY_MADE, 6);
            uint8_t* record = table + 5 * 0x88u;
            CHECK(made != NULL);
            if (made != NULL && post != NULL)
            {
                Landing(ENTER + 0x60u, kMade, sizeof kMade);
                Landing(0xA56F14u, kCounted, sizeof kCounted);
                for (uint32_t i = 0; i < 7; i++) co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, 0x7300u + i);
                memset(record, 0, 0x88);
                *(uint64_t*)record = 0x5150u;
                *(uint32_t*)(record + 0x70) = 4;
                *(uint32_t*)(table + 0x1100) = 5;
                CHECK(((MadeFn)(uintptr_t)(g_fake.image + ENTER + 0x60u))(table, 5 * 0x88u, made) == 6);
                post(0, 1, 2, (const uint64_t*)(record + 0x50), 4);
                CHECK(g_postedCount == 7 && g_posted[4] == 0x7304u && g_posted[6] == 0x7306u);
            }
        }
        co_counts_test_post_original(NULL);
    }
    {
        /* Run: the record waits for the answers of the non-local peers 4 to 7 as for its own three. Eight ready peers,
         * the local one first: the record's list gets three, the mod's list the other four. An answer takes its peer
         * off either list; the game's count tests stay on "pending" (0x100) while the mod's list is not empty, the
         * scan of the game's list (0x300 + its count) goes on as before, and the last answer goes to state 3 and the
         * post (0x200), which lists all eight. Each of the three answer functions is run, by its own registers. */
        /* the ways in: rax/rsi, r10/r11/edx and rax/rdi from (record, peer, count, cave); a drop's rax; the list's rbp */
        static const uint8_t kEnterRsi[] = {0x56, 0x48, 0x89, 0xC8, 0x48, 0x89, 0xD6, 0x4D, 0x87, 0xC8, 0x41, 0xFF, 0xE0};
        static const uint8_t kEnterR10[] = {0x49, 0x89, 0xCA, 0x49, 0x89, 0xD3, 0x4C, 0x89, 0xC2, 0x41, 0xFF, 0xE1};
        static const uint8_t kEnterRdi[] = {0x57, 0x48, 0x89, 0xC8, 0x48, 0x89, 0xD7, 0x4D, 0x87, 0xC8, 0x41, 0xFF, 0xE0};
        static const uint8_t kEnterDrop[] = {0x48, 0x89, 0xC8, 0x41, 0xFF, 0xE0};
        static const uint8_t kEnterWait[] = {0x55, 0x48, 0x89, 0xCD, 0x89, 0xD0, 0x4C, 0x89, 0xC1, 0x41, 0xFF, 0xE1};
        /* where the caves go back: pop the saved register; mov eax,0x100 (pending) / 0x200 (post) / lea eax,[count+0x300] */
        static const uint8_t kNextRsi[] = {0x5E, 0xB8, 0x00, 0x01, 0x00, 0x00, 0xC3}, kPostRsi[] = {0x5E, 0xB8, 0x00, 0x02, 0x00, 0x00, 0xC3};
        static const uint8_t kScanRsi[] = {0x5E, 0x41, 0x8D, 0x81, 0x00, 0x03, 0x00, 0x00, 0xC3};
        static const uint8_t kNextRdi[] = {0x5F, 0xB8, 0x00, 0x01, 0x00, 0x00, 0xC3}, kPostRdi[] = {0x5F, 0xB8, 0x00, 0x02, 0x00, 0x00, 0xC3};
        static const uint8_t kScanRdi[] = {0x5F, 0x41, 0x8D, 0x81, 0x00, 0x03, 0x00, 0x00, 0xC3};
        static const uint8_t kNext[] = {0xB8, 0x00, 0x01, 0x00, 0x00, 0xC3}, kPost[] = {0xB8, 0x00, 0x02, 0x00, 0x00, 0xC3};
        static const uint8_t kScanEdx[] = {0x8D, 0x82, 0x00, 0x03, 0x00, 0x00, 0xC3}, kDropped[] = {0x89, 0xC8, 0xC3};
        static const uint8_t kStoredRbp[] = {0x5D, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3}, kSkippedRbp[] = {0x5D, 0x31, 0xC0, 0xC3};
        static uint8_t table[32 * 0x88 + 0x10];
        static uint64_t frame[16], pending[8], context[0xB3A0 / 8];
        const uint64_t local = 0x9000u, tableAt = (uint64_t)(uintptr_t)table;
        const PostFn post = (PostFn)fake_host_detour(ACTIVITY_POST);
        const uint8_t* wait = Cave(ACTIVITY_WAIT, 5);
        const uint8_t* key[3] = {Cave(ANSWER_A, 12), Cave(ANSWER_B, 8), Cave(ANSWER_C, 12)};
        const uint8_t* zero[3] = {Cave(ANSWER_A_ZERO, 5), Cave(ANSWER_B_ZERO, 8), Cave(ANSWER_C_ZERO, 5)};
        const uint8_t* drop = Cave(ACTIVITY_DROP, 6);
        const AnswerFn answer[3] = {(AnswerFn)(uintptr_t)(g_fake.image + ENTER + 0x100u), (AnswerFn)(uintptr_t)(g_fake.image + ENTER + 0x120u),
                                    (AnswerFn)(uintptr_t)(g_fake.image + ENTER + 0x140u)};
        static char line[128];
        snprintf(line, sizeof line, "session: an Activity record lists every participant and waits for each one's answer (%u code sites changed)",
                 co_counts_test_activity_sites());
        CHECK(co_counts_test_activity() == 1 && co_counts_test_activity_sites() == 8u && fake_host_logged(line));
        CHECK(post != NULL && wait != NULL && drop != NULL && Cave(ACTIVITY_MADE, 6) != NULL);
        for (int i = 0; i < 3; i++) CHECK(key[i] != NULL && zero[i] != NULL);
        if (post != NULL && wait != NULL && drop != NULL && key[0] && key[1] && key[2] && zero[0] && zero[1] && zero[2])
        {
            Landing(ENTER + 0x100u, kEnterRsi, sizeof kEnterRsi);
            Landing(ENTER + 0x120u, kEnterR10, sizeof kEnterR10);
            Landing(ENTER + 0x140u, kEnterRdi, sizeof kEnterRdi);
            Landing(ENTER + 0x160u, kEnterDrop, sizeof kEnterDrop);
            Landing(ENTER + 0x180u, kEnterWait, sizeof kEnterWait);
            Landing(0xB9A6ACu, kNextRsi, sizeof kNextRsi);
            Landing(0xB9A685u, kPostRsi, sizeof kPostRsi);
            Landing(0xB9A654u, kScanRsi, sizeof kScanRsi);
            Landing(0x12E6B8Au, kNext, sizeof kNext);
            Landing(0x12E6B5Fu, kPost, sizeof kPost);
            Landing(0x12E6B28u, kScanEdx, sizeof kScanEdx);
            Landing(0x12E6C79u, kNextRdi, sizeof kNextRdi);
            Landing(0x12E6C52u, kPostRdi, sizeof kPostRdi);
            Landing(0x12E6C21u, kScanRdi, sizeof kScanRdi);
            Landing(0x12E6A15u, kDropped, sizeof kDropped);
            Landing(0xA56E96u, kStoredRbp, sizeof kStoredRbp);
            Landing(0xA56E9Eu, kSkippedRbp, sizeof kSkippedRbp);
            memset(table, 0, sizeof table);
            memcpy(g_fake.image + ACTIVITY_TABLE, &tableAt, 8);
            co_counts_test_post_original(Capture);
            context[0xB398 / 8] = local;
            /* records 0 to 2 by answer function, 3 dropped, 4 left for the next context, 5 with four ready peers */
            for (uint32_t r = 0; r < 6; r++)
            {
                const uint32_t ready = r == 5 ? 4u : 8u, way = r < 3 ? r : 0u;
                uint8_t* record = table + r * 0x88u;
                uint64_t ids[8];
                uint32_t place = 0;
                for (uint32_t i = 0; i < ready; i++) ids[i] = i == 0 ? local : 0x7400u + r * 0x10u + i;
                memset(frame, 0, sizeof frame);
                memset(pending, 0, sizeof pending);
                for (uint32_t i = 0; i < ready; i++)
                {
                    co_counts_test_participant((uintptr_t)frame + 0x68u, (uintptr_t)context, i < 4 ? i : 4u, ids[i]);
                    if (i < 4) frame[i] = ids[i];
                }
                /* the game's loop leaves the local peer out and hands the cave the others with their place */
                for (uint32_t i = 1; i < ready; i++)
                {
                    const uint32_t stored = ((StoreFn)(uintptr_t)(g_fake.image + ENTER + 0x180u))((uint8_t*)pending + 0x18, place, ids[i], wait);
                    CHECK(stored == (place < 3));
                    place += stored;
                }
                CHECK(place == 3 && pending[0] == ids[1] && pending[2] == ids[3] && pending[3] == 0);
                memset(record, 0, 0x88);
                *(uint64_t*)record = 0x4B00u + r;
                *(uint32_t*)(record + 0x10) = 3;
                memcpy(record + 0x18, pending, 3 * 8u);
                memcpy(record + 0x50, frame, 4 * 8u);
                *(uint32_t*)(record + 0x70) = 4;
                *(uint32_t*)(record + 0x80) = 1;
                *(uint32_t*)(table + 0x1100) = r + 1u;
                co_counts_test_record((uintptr_t)table, r * 0x88u);
                CHECK(co_counts_test_waiting(0x4B00u + r) == (ready == 8 ? 4 : -1));
                if (r == 5)
                {
                    /* four ready peers: nothing waits in the mod's list, the count's tests go as the game's */
                    *(uint32_t*)(record + 0x10) = 0;
                    CHECK(answer[0](record, ids[1], 0, zero[0]) == 0x200u && answer[0](record, 0xDEADu, 0, key[0]) == 0x200u);
                    CHECK(answer[0](record, ids[1], 2, zero[0]) == 0x100u);
                    continue;
                }
                if (r == 3)
                {
                    /* dropped from the table: its lists go with it */
                    CHECK(((MadeFn)(uintptr_t)(g_fake.image + ENTER + 0x160u))(record, 7, drop) == 6u && co_counts_test_waiting(0x4B03u) == -1);
                    continue;
                }
                if (r == 4) continue;
                const AnswerFn run = answer[way];
                /* a further peer and a stranger answer while the game's list holds three: the game scans its list */
                CHECK(run(record, ids[5], 9, key[way]) == 0x303u && co_counts_test_waiting(0x4B00u + r) == 3);
                CHECK(run(record, 0xDEADu, 9, key[way]) == 0x303u && co_counts_test_waiting(0x4B00u + r) == 3);
                if (way == 1)
                {
                    /* the further peers first; the game's own last answer posts */
                    CHECK(run(record, ids[4], 9, key[way]) == 0x303u && run(record, ids[6], 9, key[way]) == 0x303u);
                    CHECK(run(record, ids[7], 9, key[way]) == 0x303u && co_counts_test_waiting(0x4B00u + r) == -1);
                    *(uint32_t*)(record + 0x10) = 1;
                    CHECK(run(record, ids[1], 1, zero[way]) == 0x100u);
                    *(uint32_t*)(record + 0x10) = 0;
                    g_postedCount = 0;
                    CHECK(run(record, ids[2], 0, zero[way]) == 0x200u && g_postedCount == 0);
                }
                else
                {
                    /* the game's three first: its count reaches 0 and the record still waits; the last further peer posts */
                    *(uint32_t*)(record + 0x10) = 2;
                    CHECK(run(record, ids[1], 2, zero[way]) == 0x100u);
                    *(uint32_t*)(record + 0x10) = 0;
                    CHECK(run(record, ids[2], 0, zero[way]) == 0x100u && co_counts_test_waiting(0x4B00u + r) == 3);
                    CHECK(run(record, ids[4], 0, key[way]) == 0x100u && co_counts_test_waiting(0x4B00u + r) == 2);
                    CHECK(run(record, 0xDEADu, 0, key[way]) == 0x100u && run(record, ids[4], 0, key[way]) == 0x100u);
                    CHECK(run(record, ids[6], 0, key[way]) == 0x100u && co_counts_test_waiting(0x4B00u + r) == 1);
                    CHECK(run(record, ids[7], 0, key[way]) == 0x200u && co_counts_test_waiting(0x4B00u + r) == -1);
                }
                /* the game's post block calls the post: all eight listed once */
                post(0, 1, 2, (const uint64_t*)(record + 0x50), 4);
                int once = g_postedCount == 8;
                for (uint32_t i = 0; i < 8; i++)
                {
                    uint32_t found = 0;
                    for (uint32_t j = 0; j < g_postedCount && j < 16; j++) found += g_posted[j] == ids[i];
                    once &= found == 1;
                }
                CHECK(once);
                /* a second answer after the post goes as the game's own */
                CHECK(run(record, ids[5], 0, key[way]) == 0x200u);
            }
            /* a post the game makes without the answers (the record's own post call) frees its list too */
            CHECK(co_counts_test_waiting(0x4B04u) == 4);
            post(0, 1, 2, (const uint64_t*)(table + 4 * 0x88u + 0x50), 4);
            CHECK(co_counts_test_waiting(0x4B04u) == -1 && g_postedCount == 8);
            /* record 4 again, left waiting for the next context */
            for (uint32_t i = 1; i < 8; i++) ((StoreFn)(uintptr_t)(g_fake.image + ENTER + 0x180u))((uint8_t*)pending + 0x18, i < 3 ? i : 3u, 0x7440u + i, wait);
            co_counts_test_record((uintptr_t)table, 4 * 0x88u);
            CHECK(co_counts_test_waiting(0x4B04u) == 4);
        }
        co_counts_test_post_original(NULL);
    }
    {
        /* The clan guards, in every build. In place: cmp ecx,4; jae to the getters' "none". Run: where each function's
         * cave goes back the test undoes what the replaced bytes did and notes the index; 4 and more, -1 too, return
         * before that. The row's second copy leaves by the function's own pop rbx; ret. */
        static const uint8_t kOwn[] = {0x83, 0xF9, 0x04, 0x73, 0x11}, kOwnName[] = {0x83, 0xF9, 0x04, 0x73, 0xEC};
        static const uint8_t kPair[] = {0x41, 0x5E, 0x5F, 0x5D, 0x44, 0x89, 0x01, 0xC3};   /* pop r14; pop rdi; pop rbp; mov [rcx],r8d; ret */
        static const uint8_t kNoteR8[] = {0x44, 0x89, 0x01, 0xC3}, kNoteEdx[] = {0x89, 0x11, 0xC3};
        static const uint8_t kEnter[] = {0x53, 0xFF, 0xE2}, kLeave[] = {0x5B, 0xC3}, kNoteAgain[] = {0x5B, 0x44, 0x89, 0x01, 0xC3};
        const uint8_t* pair = Cave(CLAN_PAIR, 5);
        const uint8_t* record = Cave(CLAN_RECORD, 5);
        const uint8_t* row = Cave(CLAN_ROW, 5);
        const uint8_t* again = Cave(CLAN_ROW_AGAIN, 7);
        CHECK(memcmp(g_fake.image + CLAN_OWN, kOwn, sizeof kOwn) == 0 && memcmp(g_fake.image + CLAN_OWN_NAME, kOwnName, sizeof kOwnName) == 0);
        CHECK(pair != NULL && record != NULL && row != NULL && again != NULL);
        if (pair != NULL && record != NULL && row != NULL && again != NULL)
        {
            Landing(0x12919A5u, kPair, sizeof kPair);
            Landing(0x1291AB5u, kNoteR8, sizeof kNoteR8);
            Landing(0xB65885u, kNoteEdx, sizeof kNoteEdx);
            Landing(ENTER + 0x40u, kEnter, sizeof kEnter);
            Landing(0xFEC437u, kLeave, sizeof kLeave);
            Landing(0xFEC3EFu, kNoteAgain, sizeof kNoteAgain);
            for (uint32_t i = 0; i < 10; i++)
            {
                const uint32_t n = i == 9 ? 0xFFFFFFFFu : i, want = n < 4 ? n : 99;
                uint32_t seen[4] = {99, 99, 99, 99};
                ((ClanFn)(uintptr_t)pair)(&seen[0], NULL, n);
                ((ClanFn)(uintptr_t)record)(&seen[1], NULL, n);
                ((RowFn)(uintptr_t)row)(&seen[2], n);
                ((RowAgainFn)(uintptr_t)(g_fake.image + ENTER + 0x40u))(&seen[3], again, n);
                CHECK(seen[0] == want && seen[1] == want && seen[2] == want && seen[3] == want);
            }
        }
    }
    /* Another context: counts and move are taken back and made again for it. The per-peer table goes back with
     * entries 0 to 3 and moves again; entry 5 has no place in the game's table. */
    {
        uint8_t* table = (uint8_t*)co_counts_test_synced_table();
        if (table != NULL) { table[2 * SYNCED_ENTRY + 7] = 0x99; table[5 * SYNCED_ENTRY + 7] = 0x98; }
        Init(CONTEXT + 0x100000u);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1 && g_fake.image[LOOP_SITE + 2] == 8 && g_fake.image[CAVE_SITE] == 0xE9);
        /* a record of the context before keeps no list: it posts as four */
        CHECK(co_counts_test_waiting(0x4B04u) == -1 && co_counts_test_activity() == 1 && Cave(ANSWER_B, 8) != NULL);
        CHECK(g_fake.image[CLAN_OWN + 2] == 4 && Cave(GUARD_ROW, 6) != NULL && Cave(LAST_GUARD, 10) != NULL);
        table = (uint8_t*)co_counts_test_synced_table();
        CHECK((table != NULL) == (players == 8));
        if (table != NULL)
            CHECK(Qword(FAKE_SYNCED_POINTER) == (uint64_t)(uintptr_t)table && RegistryLeadsTo((uintptr_t)table) && table[2 * SYNCED_ENTRY + 7] == 0x99 &&
                  table[5 * SYNCED_ENTRY + 7] == 0);
    }
    /* Undo: every site, the pointer and the registry entries as the game has them; the game's table holds what the
     * mod's held. */
    {
        uint8_t* table = (uint8_t*)co_counts_test_synced_table();
        if (table != NULL) table[3 * SYNCED_ENTRY + 9] = 0x97;
        co_counts_undo();
        CHECK(co_counts_test_on() == 0 && AsTheGameHasIt());
        if (players == 8) CHECK(g_fake.image[FAKE_SYNCED_TABLE + 2 * SYNCED_ENTRY + 7] == 0x99 && g_fake.image[FAKE_SYNCED_TABLE + 3 * SYNCED_ENTRY + 9] == 0x97);
    }

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

    Start("one changed guard site");
    g_fake.image[CLAN_OWN + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("session: the code at game+0x1291BEF is not the reviewed one; the session lists count to four as before"));
    CHECK(!fake_host_logged("accesses to four-entry tables"));

    Start("one changed guard cave site");
    g_fake.image[GUARD_ROW + 5] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("game+0xB8F681 is not the reviewed one") && Cave(GUARD_CARD, 5) == NULL && Cave(LOADOUT_CAVE, 7) == NULL);
    CHECK(TableAsTheGameHasIt());

    /* The loadout screen's guards are only the build for four's concern; its sites and the table's bounds only the
     * build for eight's. */
    {
        static const struct { const char* name; uint32_t rva, at; int eight; } kOwn[] = {
            {"one changed loadout guard", GUARD_SITE, 4, 0},
            {"one changed loadout site", LOADOUT_MAP, 1, 1},
            {"one changed loadout cave site", LOADOUT_CAVE, 2, 1},
            {"one changed site of the per-peer table", SYNCED_SEARCH, 1, 1},
            {"one changed mission summary site", SUMMARY_SIZE, 1, 1},
        };
        for (size_t i = 0; i < sizeof kOwn / sizeof kOwn[0]; i++)
        {
            static char line[96];
            const int concern = kOwn[i].eight == (players == 8);
            Start(kOwn[i].name);
            g_fake.image[kOwn[i].rva + kOwn[i].at] ^= 0x01;
            Init(CONTEXT);
            CHECK(co_session_test_state() == 1 && co_counts_test_on() == !concern);
            snprintf(line, sizeof line, "game+0x%X is not the reviewed one", kOwn[i].rva);
            CHECK(fake_host_logged(line) == concern);
            if (concern) CHECK(g_fake.writes == moves && TableAsTheGameHasIt());
        }
    }

    /* The per-peer table where the mod does not expect it: a build for eight leaves the counts at four and writes
     * nothing; a build for four does not look. */
    for (int how = 0; how < 4; how++)
    {
        static const char* const kHow[] = {"the per-peer table elsewhere", "a registry list without the table", "a registry list with the table twice",
                                           "no registry"};
        uint8_t* registry = g_fake.image + FAKE_REGISTRY;
        Start(kHow[how]);
        if (how == 0) { const uint64_t elsewhere = Qword(FAKE_SYNCED_POINTER) + 0x10u; memcpy(g_fake.image + FAKE_SYNCED_POINTER, &elsewhere, 8); }
        if (how == 1) memset(registry + 0x7248, 0, 4);
        if (how == 2) { const uint32_t two = 2; memcpy(registry + 0x7230, &two, 4); memcpy(registry + 0x7230 + 8 + 16, registry + 0x7230 + 8, 16); }
        if (how == 3) memset(g_fake.image + FAKE_REGISTRY_GLOBAL, 0, 8);
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == (players == 4) && co_counts_test_synced_table() == 0);
        CHECK(fake_host_logged("players: the per-peer loadout table is not where the game keeps it") == (players == 8));
        if (players == 8) CHECK(g_fake.writes == moves && fake_host_logged("session: the session lists count to four as before"));
    }

    /* A failed write while the table moves puts back the writes before it; one at a loadout site or a bound of the
     * table takes back every site and then the table. */
    for (int k = 0; k < co_counts_test_move_writes(); k++)
    {
        static char name[64];
        snprintf(name, sizeof name, "move write %d fails", k);
        Start(name);
        FillTable(g_fake.image + FAKE_SYNCED_TABLE);
        g_fake.failWriteAt = moves + k;
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt() && TableHolds(g_fake.image + FAKE_SYNCED_TABLE));
        CHECK(fake_host_logged("players: the per-peer loadout table could not be moved") && !fake_host_logged("a count site could not be written"));
    }
    {
        const uint32_t kEight[] = {LOADOUT_SIZE, LOADOUT_MAP, SYNCED_CLEAR, SYNCED_GONE, LOADOUT_CAVE, SUMMARY_SIZE, SUMMARY_CARDS};
        for (size_t i = 0; i < sizeof kEight / sizeof kEight[0]; i++)
        {
            static char name[64];
            const int at = co_counts_test_write_of(kEight[i]);
            CHECK((at >= 0) == (players == 8));
            if (at < 0) continue;
            snprintf(name, sizeof name, "the write at game+0x%X fails", kEight[i]);
            Start(name);
            FillTable(g_fake.image + FAKE_SYNCED_TABLE);
            g_fake.failWriteAt = moves + at;
            Init(CONTEXT);
            CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt() && TableHolds(g_fake.image + FAKE_SYNCED_TABLE));
            CHECK(fake_host_logged("a count site could not be written; the session lists count to four as before"));
        }
    }

    /* The loot guards, the Activity record's and the clan guards are in every build; the result gate only in a build for
     * eight. */
    {
        static const struct { const char* name; uint32_t rva, at; int eight; } kMission[] = {
            {"one changed loot index site", LOOT_INDEX, 6, 0},
            {"one changed site before the result's load", LOOT_RESULT, 9, 0},
            {"one changed Activity record site", ACTIVITY, 4, 0},
            {"one changed Activity record list", ACTIVITY_WAIT, 3, 0},
            {"one changed result gate", RESULT_GATE, 1, 1},
            {"one changed clan handler", CLAN_PAIR, 4, 0},
            {"one changed clan getter", CLAN_OWN_NAME, 2, 0},
            {"one changed clan station row", CLAN_ROW_AGAIN, 6, 0},
            {"one changed mission summary guard", SUMMARY_GUARD, 3, 0},
        };
        for (size_t i = 0; i < sizeof kMission / sizeof kMission[0]; i++)
        {
            static char line[96];
            const int concern = !kMission[i].eight || players == 8;
            Start(kMission[i].name);
            g_fake.image[kMission[i].rva + kMission[i].at] ^= 0x01;
            Init(CONTEXT);
            CHECK(co_session_test_state() == 1 && co_counts_test_on() == !concern);
            snprintf(line, sizeof line, "game+0x%X is not the reviewed one", kMission[i].rva);
            CHECK(fake_host_logged(line) == concern);
            if (concern) CHECK(g_fake.writes == moves && Cave(LOOT_INDEX, 8) == NULL && g_fake.image[RESULT_GATE + 2] == 3);
        }
        const uint32_t kWrites[] = {LOOT_INDEX, LOOT_RESULT, ACTIVITY, ACTIVITY_WAIT, 0x12E67B1u, ACTIVITY_MADE, ANSWER_A, ANSWER_C_ZERO, ACTIVITY_DROP,
                                    RESULT_GATE, CLAN_RECORD, CLAN_OWN, CLAN_ROW_AGAIN, SUMMARY_GUARD};
        for (size_t i = 0; i < sizeof kWrites / sizeof kWrites[0]; i++)
        {
            static char name[64];
            const int at = co_counts_test_write_of(kWrites[i]);
            CHECK((at >= 0) == (players == 8 || kWrites[i] != RESULT_GATE));
            if (at < 0) continue;
            snprintf(name, sizeof name, "the write at game+0x%X fails", kWrites[i]);
            Start(name);
            g_fake.failWriteAt = moves + at;
            Init(CONTEXT);
            CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
            CHECK(fake_host_logged("a count site could not be written; the session lists count to four as before"));
        }
    }

    Start("one changed message guard site");
    g_fake.image[NET_SENDER + 3] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("game+0x1339313 is not the reviewed one") && Cave(NET_SCORES, 6) == NULL);

    /* A site that only a build for eight writes is only that build's concern. */
    Start("one changed site of the build for eight");
    g_fake.image[EIGHT_SOS + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == (players == 8 ? 0 : 1));
    CHECK(fake_host_logged("game+0x66CA22 is not the reviewed one") == (players == 8));
    CHECK(g_fake.image[EIGHT_INDEX + 2] == 4);

    /* The member lists' caps are counts: a changed one leaves every build at four. */
    Start("one changed member list cap");
    g_fake.image[MIRROR_CAP + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("session: the code at game+0x1093CB0 is not the reviewed one; the session lists count to four as before"));

    /* The lobby's size is only the build for eight's concern, its cave too. */
    Start("one changed lobby site of the build for eight");
    g_fake.image[LOBBY_FRIEND + 6] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == (players == 8 ? 0 : 1));
    CHECK(fake_host_logged("game+0x103D11D is not the reviewed one") == (players == 8));
    CHECK(g_fake.image[LOBBY_FULL + 2] == 4 && g_fake.image[LOBBY_SIZE + 1] == 4);

    /* The own lobby's record is in every build: a changed byte there leaves the counts off. */
    Start("one changed site of the own lobby's record");
    g_fake.image[RECORD_COUNT + 2] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("game+0x133EF93 is not the reviewed one") && g_fake.image[RECORD_POST + 3] == 0xD0);

    /* A failed write at a member list's cap, a lobby site or the own lobby's record takes every count back. */
    {
        const uint32_t kLobby[] = {MIRROR_CAP, MIRROR_COUNT, LOBBY_FULL, LOBBY_SIZE, LOBBY_POST, LOBBY_FRIEND, RECORD_POST, RECORD_COUNT};
        for (size_t i = 0; i < sizeof kLobby / sizeof kLobby[0]; i++)
        {
            static char name[64];
            const int at = co_counts_test_write_of(kLobby[i]);
            const int always = kLobby[i] == MIRROR_CAP || kLobby[i] == MIRROR_COUNT || kLobby[i] == RECORD_POST || kLobby[i] == RECORD_COUNT;
            CHECK((at >= 0) == (players == 8 || always));
            if (at < 0) continue;
            snprintf(name, sizeof name, "the write at game+0x%X fails", kLobby[i]);
            Start(name);
            g_fake.failWriteAt = moves + at;
            Init(CONTEXT);
            CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
            CHECK(fake_host_logged("a count site could not be written; the session lists count to four as before"));
        }
    }

    /* The same for the test option's two places. */
    Start("one changed site of the test option");
    g_fake.image[TEST_INDEX + 1] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == (first ? 0 : 1));
    CHECK(fake_host_logged("game+0x108C3CA is not the reviewed one") == (first != 0) && Cave(TEST_SLOT, 6) == NULL);

    /* The last writes are the guards' caves: every one of the last eight fails once, and a sample of the rest. */
    for (int at = moves; at < writes; at += (at < moves + 2 || at >= writes - 9) ? 1 : 13)
    {
        static char name[64];
        const int moving = at < moves + co_counts_test_move_writes();
        snprintf(name, sizeof name, "write %d fails", at);
        Start(name);
        g_fake.failWriteAt = at;
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
        CHECK(fake_host_logged(moving ? "the per-peer loadout table could not be moved" : "a count site could not be written; the session lists count to four as before"));
    }

    Start("a function is other code");
    co_counts_test_reset();
    co_session_test_reset();
    {
        const HfApi* api = fake_host_reset(CO_GAME_BUILD);
        co_session_test_fill(g_fake.image);
        co_counts_test_fill(g_fake.image);
        co_registry_test_fill(g_fake.image);
        g_fake.noMatch[0] = 0x108B6B0u;
        hellforge_mod_init(api, g_mod);
        if (g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && AsTheGameHasIt());
        CHECK(fake_host_logged("the session lists count to four as before"));
    }

    /* The Activity record's further participants and answers need the post's hook: other code there, a failed hook or
     * a changed site of theirs leaves them out. The rest goes in, the record's and the clan guards too; the record keeps
     * four participants and three peers to wait for, and the log says why. */
    for (int how = 0; how < 5; how++)
    {
        static const char* const kHow[] = {"the post is other code", "the post cannot be hooked", "one changed Activity record count",
                                           "one changed answer site", "one changed drop site"};
        static const uint32_t kChanged[] = {0, 0, ACTIVITY_MADE + 2, ANSWER_B_ZERO + 5, ACTIVITY_DROP + 1};
        static char line[160];
        Start(kHow[how]);
        co_counts_test_reset();
        co_session_test_reset();
        const HfApi* api = fake_host_reset(CO_GAME_BUILD);
        co_session_test_fill(g_fake.image);
        co_counts_test_fill(g_fake.image);
        co_registry_test_fill(g_fake.image);
        if (how == 1) g_fake.noHook = ACTIVITY_POST;
        if (how == 0) g_fake.noMatch[0] = ACTIVITY_POST;
        if (how >= 2) g_fake.image[kChanged[how]] ^= 0x01;
        hellforge_mod_init(api, g_mod);
        if (g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
        CHECK((fake_host_detour(ACTIVITY_POST) != NULL) == (how >= 2));
        Init(CONTEXT);
        CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1 && co_counts_test_activity() == 0);
        CHECK(fake_host_logged("session: the session lists count to eight (143 code sites changed, 15 through caves)"));
        snprintf(line, sizeof line, "players: %u accesses to four-entry tables of the game skip a player index above 3", co_counts_test_guards());
        CHECK(fake_host_logged(line) && !fake_host_logged("an Activity record lists every participant"));
        if (how < 2)
            snprintf(line, sizeof line, "session: the Activity record's post at game+0x12C2940 %s; Activity records list and wait for four players",
                     how == 0 ? "is not the code the mod was made for (a game update or another mod)" : "could not be hooked");
        else
            snprintf(line, sizeof line, "session: the code at game+0x%X is not the reviewed one; Activity records list and wait for four players",
                     how == 2 ? ACTIVITY_MADE : how == 3 ? ANSWER_B_ZERO : ACTIVITY_DROP);
        CHECK(fake_host_logged(line));
        CHECK(Cave(CLAN_PAIR, 5) != NULL && Cave(CLAN_ROW, 5) != NULL && g_fake.image[CLAN_OWN + 2] == 4 && Cave(LOOT_INDEX, 8) != NULL);
        CHECK(Cave(ACTIVITY, 5) != NULL && Cave(ACTIVITY_WAIT, 5) != NULL && Cave(0x12E67B1u, 5) != NULL);
        CHECK(g_fake.image[ACTIVITY_MADE] == 0xFF && g_fake.image[ANSWER_A] == 0x44 && g_fake.image[ANSWER_B] == 0x41 && g_fake.image[ANSWER_C_ZERO] == 0x45 &&
              g_fake.image[ACTIVITY_DROP] == 0x8D);
        {
            /* the record's lists stay at their room and nothing waits in the mod's memory */
            static const uint8_t kEnterWait[] = {0x55, 0x48, 0x89, 0xCD, 0x89, 0xD0, 0x4C, 0x89, 0xC1, 0x41, 0xFF, 0xE1};
            static const uint8_t kStoredRbp[] = {0x5D, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3}, kSkippedRbp[] = {0x5D, 0x31, 0xC0, 0xC3};
            static uint8_t table[32 * 0x88 + 0x10];
            static uint64_t pending[8];
            const uint8_t* wait = Cave(ACTIVITY_WAIT, 5);
            if (wait != NULL)
            {
                Landing(ENTER + 0x180u, kEnterWait, sizeof kEnterWait);
                Landing(0xA56E96u, kStoredRbp, sizeof kStoredRbp);
                Landing(0xA56E9Eu, kSkippedRbp, sizeof kSkippedRbp);
                memset(pending, 0, sizeof pending);
                for (uint32_t i = 0; i < 7; i++)
                    CHECK(((StoreFn)(uintptr_t)(g_fake.image + ENTER + 0x180u))((uint8_t*)pending + 0x18, i < 3 ? i : 3u, 0x7500u + i, wait) == (i < 3));
                memset(table, 0, sizeof table);
                *(uint64_t*)table = 0x4C00u;
                *(uint32_t*)(table + 0x70) = 4;
                *(uint32_t*)(table + 0x1100) = 1;
                co_counts_test_record((uintptr_t)table, 0);
                CHECK(pending[2] == 0x7502u && pending[3] == 0 && co_counts_test_waiting(0x4C00u) == -1);
            }
        }
        co_counts_undo();
        if (how >= 2) g_fake.image[kChanged[how]] ^= 0x01;
        CHECK(co_counts_test_on() == 0 && AsTheGameHasIt());
    }

    printf("%s\n", g_failed ? "counts: failures" : "counts: all cases passed");
    return g_failed != 0;
}
