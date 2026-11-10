/* The session lists' counts against a stand-in game image: sites, flag clears and caves are written behind the move,
 * a failed write takes all of them back, and the mod's own slot and record code handles entries 4 to 7. The guards
 * of the game's four-entry tables go the same way; some of them are run with player indexes 0 to 7. The message
 * guards are run with lists that fit and lists that do not. The build's player count decides which sites belong to
 * it, and the test option's slot and index search is checked against a stand-in synchronizer. */
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
#define GUARD_SITE 0x14670B8u       /* cmp ecx,-1; je: the loadout screen's own index */
#define GUARD_NEXT 0x146C3D5u       /* jae to the game's fail-fast: the loadout screen's loop over manager slots */
#define GUARD_ROW 0xB8F681u         /* movsd xmm0,[r13]: a row of the game state's table, index in eax */
#define GUARD_PEER 0x146CC05u       /* the loadout screen's card for a peer, index in r13d */
#define GUARD_CARD 0x146D320u       /* a function: the card of the player index in edx, -1 for none */
#define GUARD_CURSOR 0x72F700u      /* a function: the map cursor of the player index in edx */
#define LAST_GUARD 0x1471D30u
#define GUARD_ENTRY 0x139D300u      /* a function: an entry of 0x1320 for the player index in r8d */
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

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint32_t (*CardFn)(void*, uint32_t);
typedef void (*CursorFn)(uint32_t*, uint32_t);
typedef void (*EntryFn)(uint32_t*, void*, uint32_t, uint32_t);
typedef uint32_t (*HandlerFn)(void*, const void*);

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
           image[CAVE_SITE] == 0x48 && image[LAST_CAVE] == 0x48 && image[LAST_CAVE + 7] == 0 &&
           image[GUARD_SITE + 2] == 0xFF && image[GUARD_SITE + 4] == 0x84 && image[GUARD_NEXT + 2] == 0x30 &&
           image[GUARD_ROW] == 0xF2 && image[GUARD_CARD] == 0x8B && image[LAST_GUARD] == 0x41 && image[GUARD_ENTRY] == 0x48 &&
           image[GUARD_INPUT + 2] == 0xFF && image[EIGHT_INDEX + 2] == 4 && image[EIGHT_SOS + 2] == 4 && image[NET_SCORES] == 0x40 &&
           image[NET_SENDER] == 0x41 && image[TEST_SLOT] == 0x41 && image[TEST_INDEX] == 0x49 && co_counts_test_reviewed(image);
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
    return (uintptr_t)cave >= caves && (uintptr_t)cave < caves + 0x2000u ? cave : NULL;
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

int main(void)
{
    const int moves = (int)(co_session_test_sites() + co_session_test_stub_count());
    const int players = co_counts_test_players(), first = co_counts_test_first_slot();
    int writes;

    Start("the reviewed game");
    CHECK(g_fake.writes == 0 && AsTheGameHasIt());
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1);
    CHECK(fake_host_logged("session: the session lists count to eight (134 code sites changed, 14 through caves)"));
    {
        static char line[96];
        snprintf(line, sizeof line, "players: %u accesses to four-entry tables of the game skip a player index above 3", co_counts_test_guards());
        CHECK(co_counts_test_guards() >= 70 && fake_host_logged(line));
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
        /* In place: cmp ecx,4; jae to where the game goes without an own index. The slot loop goes on to its next slot. */
        static const uint8_t kOwn[] = {0x83, 0xF9, 0x04, 0x0F, 0x83};
        const uint8_t* image = g_fake.image;
        const uint8_t* cave;
        CHECK(memcmp(image + GUARD_SITE, kOwn, sizeof kOwn) == 0 && Leads(image + GUARD_SITE + 5, 0x1466F9Du));
        CHECK(image[GUARD_NEXT] == 0x0F && image[GUARD_NEXT + 1] == 0x83 && Leads(image + GUARD_NEXT + 2, 0x146C6B4u));

        /* cmp eax,4; jae the loop's next round; the replaced movsd; jmp back behind it */
        static const uint8_t kRow[] = {0x83, 0xF8, 0x04, 0x0F, 0x83}, kMoved[] = {0xF2, 0x41, 0x0F, 0x10, 0x45, 0x00, 0xE9};
        cave = Cave(GUARD_ROW, 6);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(memcmp(cave, kRow, sizeof kRow) == 0 && Leads(cave + 5, 0xB8F723u) && memcmp(cave + 9, kMoved, sizeof kMoved) == 0 &&
                  Leads(cave + 16, 0xB8F687u));

        /* Two ways out: back behind the replaced load for 0 to 3, into the loop's tail (with r13d loaded again and
         * eax zero) for the rest. The short jae leads to the second. */
        static const uint8_t kTail[] = {0x44, 0x8B, 0xAC, 0x24, 0xE8, 0x00, 0x00, 0x00, 0x31, 0xC0, 0xE9};
        cave = Cave(GUARD_PEER, 10);
        CHECK(cave != NULL);
        if (cave != NULL)
            CHECK(cave[7] == 0x73 && cave + 9 + cave[8] == cave + 21 && cave[16] == 0xE9 && Leads(cave + 17, 0x146CC0Fu) &&
                  memcmp(cave + 21, kTail, sizeof kTail) == 0 && Leads(cave + 32, 0x146CF99u));
        CHECK(Cave(LAST_GUARD, 10) != NULL);

        /* Run: the card function's cave with a ret where it goes back. 0 to 3 arrive there with the index in eax, as
         * the replaced mov leaves it; the rest get the function's "none" without going there. */
        static const uint8_t kRet[] = {0xC3};
        cave = Cave(GUARD_CARD, 5);
        CHECK(cave != NULL);
        if (cave != NULL)
        {
            static const uint32_t kNone[] = {4, 5, 6, 7, 8, 0x80000000u, 0xFFFFFFFFu};
            const CardFn card = (CardFn)(uintptr_t)cave;
            Landing(0x146D325u, kRet, sizeof kRet);
            for (uint32_t i = 0; i < 4; i++) CHECK(card(NULL, i) == i);
            for (size_t i = 0; i < sizeof kNone / sizeof kNone[0]; i++) CHECK(card(NULL, kNone[i]) == 0xFFFFFFFFu);
        }

        /* Run: the cursor function's cave. Where it goes back the test undoes the replaced push and sub and notes the
         * index (add rsp,0x20; pop rbx; mov [rcx],edx; ret); 4 and more return before that. */
        static const uint8_t kNote[] = {0x48, 0x83, 0xC4, 0x20, 0x5B, 0x89, 0x11, 0xC3};
        cave = Cave(GUARD_CURSOR, 6);
        CHECK(cave != NULL);
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
    }
    {
        /* The guards for player indexes 4 to 7 that came with the join limit. In place: cmp edi,4; jae. */
        static const uint8_t kInput[] = {0x83, 0xFF, 0x04, 0x0F, 0x83};
        const uint8_t* image = g_fake.image;
        const uint8_t* cave;
        CHECK(memcmp(image + GUARD_INPUT, kInput, sizeof kInput) == 0 && Leads(image + GUARD_INPUT + 5, 0x18C12DAu));

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
        CHECK(cave != NULL);
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
    /* Another context: counts and move are taken back and made again for it. */
    Init(CONTEXT + 0x100000u);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1 && g_fake.image[LOOP_SITE + 2] == 8 && g_fake.image[CAVE_SITE] == 0xE9);
    CHECK(g_fake.image[GUARD_SITE + 2] == 4 && Cave(GUARD_ROW, 6) != NULL && Cave(LAST_GUARD, 10) != NULL);

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
    g_fake.image[GUARD_SITE + 4] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("session: the code at game+0x14670B8 is not the reviewed one; the session lists count to four as before"));
    CHECK(!fake_host_logged("accesses to four-entry tables"));

    Start("one changed guard cave site");
    g_fake.image[GUARD_ROW + 5] ^= 0x01;
    Init(CONTEXT);
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 0 && g_fake.writes == moves);
    CHECK(fake_host_logged("game+0xB8F681 is not the reviewed one") && Cave(GUARD_CARD, 5) == NULL);

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
