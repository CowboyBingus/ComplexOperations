/* The voice lines' log against a stand-in game image: the hooks go in only on the reviewed code, pass every argument and
 * the result through, and the lines carry what the game reads there: the applied voice at (record + k) * 0x44 with k
 * as the code holds it, the speaker's component value and prefix, the name the game built, and the manager's count. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_voice_test_reset(void);
void co_voice_test_lookups(uint64_t (*hash)(const char*, int, uint32_t), uint32_t (*event)(uint32_t));
void co_pods_test_reset(void);
void co_managers_test_reset(void);

#define DELTA 0x878260u
#define READ 0x878329u
#define NAME 0x139ECA0u
#define POST 0x13A1CD0u
#define SYSTEM 0x346D520u
#define EVENT 0xABCD1234u
#define CUSTOMIZATION 0x33264F8u
#define VOICE_MANAGER 0x3326C30u
#define ROWS 0x32F9518u
#define PREFIXES 0x21CB530u
#define CM 0x3600000u        /* the stand-in customization manager, in free room of the image */
#define CM_MAP 0x3601000u
#define VM 0x3610000u        /* the stand-in voice manager */
#define VM_MAP 0x3611000u
#define PREFIX_TEXT 0x3620000u
#define LIST 0x3630000u
#define ENTITY 77u
#define RECORD 2u
#define SLOT 3u

typedef uint64_t (*DeltaFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*NameFn)(uint64_t, uint64_t, uint64_t, uint64_t, char*, uint64_t);
typedef uint64_t (*PostFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint64_t g_seen[10];
static int g_hashLength;
static uint64_t g_playing;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint64_t TestDelta(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    g_seen[0] = a; g_seen[1] = b; g_seen[2] = c; g_seen[3] = d;
    return (uint64_t)(uintptr_t)(g_fake.image + LIST);
}

static uint64_t TestName(uint64_t a, uint64_t b, uint64_t c, uint64_t d, char* name, uint64_t f)
{
    g_seen[0] = a; g_seen[1] = b; g_seen[2] = c; g_seen[3] = d; g_seen[4] = (uint64_t)(uintptr_t)name; g_seen[5] = f;
    strcpy(name, b == 99 ? "reload_v01" : "vo_helldiver_soldier_female1_reload_v02"); /* 99 has no voice */
    return 0x101; /* al = 1; the rest of rax is whatever the game left */
}

static uint64_t TestPost(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i, uint64_t j)
{
    const uint64_t v[10] = {a, b, c, d, e, f, g, h, i, j};
    memcpy(g_seen, v, sizeof v);
    return g_playing;
}

/* The game's name hash and event map, as far as the test needs them: a prefixed name is an event, a bare one is not. */
static uint64_t TestHash(const char* name, int length, uint32_t seed)
{
    g_hashLength = length;
    return (uint64_t)(strncmp(name, "vo_helldiver_", 13) == 0 && seed == 0 ? 0x1111u : 0x2222u) << 32 | 0xDEADu;
}
static uint32_t TestEvent(uint32_t hash) { return hash == 0x1111u ? EVENT : 0; }

static void Install(void)
{
    co_voice_install((uintptr_t)g_fake.image);
    co_voice_test_lookups(TestHash, TestEvent);
}

static void Put32(uint32_t at, uint32_t v) { memcpy(g_fake.image + at, &v, 4); }
static void Put64(uint32_t at, uint64_t v) { memcpy(g_fake.image + at, &v, 8); }
static void PutPointer(uint32_t at, uint32_t to) { Put64(at, (uint64_t)(uintptr_t)(g_fake.image + to)); }

/* A map at `map` (data pointer, size 16, empty key 0xFFFFFFFF, multiplier 2) holding key -> value. */
static void Map(uint32_t map, uint32_t data, uint32_t key, uint32_t value)
{
    PutPointer(map, data);
    Put32(map + 8, 16); Put32(map + 12, 0xFFFFFFFFu); Put32(map + 16, 2);
    for (uint32_t i = 0; i < 16; i++) { Put32(data + 8 * i, 0xFFFFFFFFu); Put32(data + 8 * i + 4, 0xFFFFFFFFu); }
    const uint32_t at = (key * 2u) & 15u;
    Put32(data + 8 * at, key); Put32(data + 8 * at + 4, value);
}

/* k as the code holds it: 0x24 in the game, 0x30 once the applied records have moved. */
static void Start(const char* name, uint8_t k)
{
    static const uint8_t read[] = {0x48, 0x83, 0xC0, 0x24, 0x48, 0x89, 0x4C, 0x24, 0x28, 0x48, 0x6B, 0xC0, 0x44};
    const HfApi* api;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_voice_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    memset(g_seen, 0, sizeof g_seen);
    memcpy(g_fake.image + READ, read, sizeof read);
    g_fake.image[READ + 3] = k;
    g_fake.originals[0].target = (uintptr_t)(g_fake.image + DELTA);
    g_fake.originals[0].original = (void*)TestDelta;
    g_fake.originals[1].target = (uintptr_t)(g_fake.image + NAME);
    g_fake.originals[1].original = (void*)TestName;
    g_fake.originals[2].target = (uintptr_t)(g_fake.image + POST);
    g_fake.originals[2].original = (void*)TestPost;
    g_playing = 4242;
    PutPointer(SYSTEM, 0x3640000u);
    /* the customization manager: entity 77 is record 2 */
    PutPointer(CUSTOMIZATION, CM);
    Map(CM + 0x930, CM_MAP, ENTITY, RECORD);
    /* the four voices' ids */
    Put32(ROWS, 0x83BFB97Cu); Put32(ROWS + 0x38, 0xEF050987u); Put32(ROWS + 0x70, 0x09E450ACu); Put32(ROWS + 0xA8, 0x7ED81796u);
    /* the voice manager: entity 77 at slot 3 with value 6, two entities */
    PutPointer(VOICE_MANAGER, VM);
    Map(VM + 0x80, VM_MAP, ENTITY, SLOT);
    Put32(VM + 0x130, 2);
    Put32(VM + 0x134 + 4 * SLOT, 6);
    memcpy(g_fake.image + PREFIX_TEXT, "vo_helldiver_soldier_female1", 29);
    PutPointer(PREFIXES + 8 * 5, PREFIX_TEXT);
    /* the spawn's list: one delta */
    Put64(LIST, 1); Put64(LIST + 8, 0x02924B50B580D0B8ull);
    CHECK(hellforge_mod_init(api, g_mod) == 0);
}

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at++) n++;
    return n;
}

static void TestInstall(void)
{
    Start("the hooks go in on the reviewed code", 0x24);
    co_voice_install((uintptr_t)g_fake.image);
    CHECK(fake_host_detour(DELTA) != NULL && fake_host_detour(NAME) != NULL && fake_host_detour(POST) != NULL);
    CHECK(fake_host_logged("voice: logging the spawn's voice delta"));

    Start("another code at the voice read: nothing is hooked", 0x24);
    g_fake.noMatch[0] = READ;
    co_voice_install((uintptr_t)g_fake.image);
    CHECK(fake_host_detour(DELTA) == NULL && fake_host_detour(NAME) == NULL);
    CHECK(fake_host_logged("voice lines are not logged"));

    Start("another code at the post: nothing is hooked", 0x24);
    g_fake.noMatch[0] = POST;
    co_voice_install((uintptr_t)g_fake.image);
    CHECK(fake_host_detour(DELTA) == NULL && fake_host_detour(POST) == NULL);

    Start("a hook refused: the log says so", 0x24);
    g_fake.noHook = NAME;
    co_voice_install((uintptr_t)g_fake.image);
    CHECK(fake_host_logged("a hook could not be placed; the voice lines' log is incomplete"));
}

/* The voice is read where the patched code reads it: (2 + k) * 0x44 from the manager. */
static void TestDeltaRead(uint8_t k, uint32_t other)
{
    char want[96];
    Start(k == 0x24 ? "the spawn's voice, records where the game has them" : "the spawn's voice, records moved by 12", k);
    Put32(CM + (RECORD + 0x24u) * 0x44u, k == 0x24 ? 0xEF050987u : other); /* the old place */
    Put32(CM + (RECORD + 0x30u) * 0x44u, k == 0x30 ? 0xEF050987u : other); /* the new place */
    Install();
    const DeltaFn delta = (DeltaFn)fake_host_detour(DELTA);
    CHECK(delta != NULL);
    if (delta == NULL) return;
    const uint64_t list = delta(11, ENTITY, 33, 44);
    CHECK(list == (uint64_t)(uintptr_t)(g_fake.image + LIST));
    CHECK(g_seen[0] == 11 && g_seen[1] == ENTITY && g_seen[2] == 33 && g_seen[3] == 44);
    snprintf(want, sizeof want, "record 2, applied voice 0xEF050987 (voice 2 of 4; 0 none) read at +0x%X;", (unsigned)(k * 0x44u));
    CHECK(fake_host_logged(want));
    CHECK(fake_host_logged("1 delta(s), the first 0x02924B50B580D0B8"));
}

static void TestDeltaUnknown(void)
{
    Start("an entity with no record", 0x30);
    Install();
    const DeltaFn delta = (DeltaFn)fake_host_detour(DELTA);
    if (delta == NULL) { CHECK(0); return; }
    delta(0, 78, 0, 0);
    CHECK(fake_host_logged("spawn of entity 78: record -1, applied voice 0xFFFFFFFF (voice 0 of 4"));
}

static void TestLineName(void)
{
    char buffer[256] = {0};
    Start("a line's name, value and prefix", 0x30);
    Install();
    const NameFn name = (NameFn)fake_host_detour(NAME);
    if (name == NULL) { CHECK(0); return; }
    const uint64_t built = name(5, ENTITY, 0x1234, 2, buffer, 66);
    CHECK(built == 0x101);
    CHECK(g_seen[0] == 5 && g_seen[1] == ENTITY && g_seen[2] == 0x1234 && g_seen[3] == 2 && g_seen[4] == (uint64_t)(uintptr_t)buffer && g_seen[5] == 66);
    CHECK(fake_host_logged("voice: line 1 for entity 77: component slot 3 value 6 (vo_helldiver_soldier_female1), 2 voiced entities (room for 8); "
                           "name \"vo_helldiver_soldier_female1_reload_v02\", event 0xABCD1234\n"));
    CHECK(g_hashLength == 39);
    name(5, 99, 0x1234, 2, buffer, 66);
    CHECK(fake_host_logged("voice: line 2 for entity 99: component slot -1 value -1 (-), 2 voiced entities"));
    CHECK(fake_host_logged("name \"reload_v01\", event 0x00000000 (none: the game drops the line)"));
}

static void TestLinePost(void)
{
    Start("a post, its event and its result", 0x30);
    Install();
    const PostFn post = (PostFn)fake_host_detour(POST);
    if (post == NULL) { CHECK(0); return; }
    const uint64_t system = (uint64_t)(uintptr_t)(g_fake.image + 0x3640000u);
    CHECK(post(system, 0x1111, 0x55, 7, 9, ENTITY, 66, 88, 101, 102) == 4242);
    CHECK(g_seen[0] == system && g_seen[1] == 0x1111 && g_seen[2] == 0x55 && g_seen[3] == 7 && g_seen[4] == 9 && g_seen[5] == ENTITY &&
          g_seen[6] == 66 && g_seen[7] == 88 && g_seen[8] == 101 && g_seen[9] == 102);
    CHECK(fake_host_logged("voice: post 1: entity 77 unit 9, line 0x00000055, name hash 0x00001111, event 0xABCD1234; playing id 4242\n"));
    for (int i = 0; i < 30; i++) post(system, 0x1111, 0x55, 7, 9, ENTITY, 66, 88, 0, 0);
    CHECK(Lines("voice: post ") == 24);
    g_playing = 0;
    post(system, 0x1111, 0x55, 7, 9, ENTITY, 66, 88, 0, 0);
    CHECK(fake_host_logged("voice: post 32: entity 77 unit 9, line 0x00000055, name hash 0x00001111, event 0xABCD1234; playing id 0 (nothing"));
    post(system + 8, 0x2222, 0x56, 7, 9, ENTITY, 66, 88, 0, 0);
    CHECK(fake_host_logged("voice: post 33 (another system): entity 77 unit 9, line 0x00000056, name hash 0x00002222, event 0x00000000; "
                           "playing id 0 (nothing posted, or deferred to the next update)"));
}

/* The first 24 lines, then each 50th or a new value; never more than 200 voice lines. */
static void TestBudget(void)
{
    char buffer[256];
    Start("the lines a session gets", 0x30);
    Install();
    const NameFn name = (NameFn)fake_host_detour(NAME);
    if (name == NULL) { CHECK(0); return; }
    for (int i = 0; i < 200; i++) name(5, ENTITY, 1, 1, buffer, 0);
    CHECK(Lines("voice: line ") == 28);
    CHECK(fake_host_logged("voice: line 24 ") && !fake_host_logged("voice: line 25 ") && fake_host_logged("voice: line 50 "));
    Put32(VM + 0x130, 3); /* a third voiced entity: the next line says so */
    name(5, ENTITY, 1, 1, buffer, 0);
    CHECK(fake_host_logged("voice: line 201 for entity 77: component slot 3 value 6 (vo_helldiver_soldier_female1), 3 voiced"));
    for (int i = 0; i < 400; i++) { Put32(VM + 0x130, 4 + (uint32_t)(i & 1)); name(5, ENTITY, 1, 1, buffer, 0); }
    CHECK(Lines("voice: ") == 200 + 1); /* and the install's line */
    CHECK(Lines("(the last voice line of this session)") == 1);
}

int main(void)
{
    TestInstall();
    TestDeltaRead(0x24, 0x7ED81796u);
    TestDeltaRead(0x30, 0x7ED81796u);
    TestDeltaUnknown();
    TestLineName();
    TestLinePost();
    TestBudget();
    if (g_failed == 0) printf("voice: all passed\n");
    return g_failed != 0;
}
