/* The voice lines' log against a stand-in game image: the hooks go in only on the reviewed code, pass every argument and
 * the result through, and the lines carry what the game reads there: the applied voice at (record + k) * 0x44 with k
 * as the code holds it, the speaker's component value and prefix, the name the game built, the manager's count, and
 * the state gate a request met (the solo rule refuses a mask with bit 6 when the state has bit 0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_voice_test_reset(void);
void co_voice_test_lookups(uint64_t (*hash)(const char*, int, uint32_t), uint32_t (*event)(uint32_t));
void co_voice_test_game(const uint8_t* (*lineDef)(uint32_t, uint32_t, uint32_t), const char* (*lineText)(uint32_t, uint32_t),
    const uint8_t* (*record)(uint32_t), const uint32_t* (*voiceOf)(const uint8_t*));
uint32_t co_voice_verdict(uint32_t mask, uint32_t state, int haveLineDef, int queued);
void co_pods_test_reset(void);
void co_managers_test_reset(void);

#define DELTA 0x878260u
#define READ 0x878329u
#define NAME 0x139ECA0u
#define POST 0x13A1CD0u
#define REQUEST 0x13A0090u
#define SETS 0x32B4598u
#define SET 0x73917133u      /* the voice set of component value 6 here */
#define STRATAGEM 0x21043F75u /* call_stratagem_equip_vehicle's group: mask 0x9C0 */
#define LASTMAG 0x5C4397B3u  /* call_reload_lastmag_gen: mask 0 */
#define SPOKEN 0x5AE3F662u
#define NEEDED 0x12345678u   /* a stand-in group with mask 0xC06: needs bits 1 and 2, forbids 4 and 5 */
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
typedef uint64_t (*RequestFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t, uint64_t);

static int g_failed;
static const char* g_case;
static uint64_t g_seen[12];
static uint64_t g_queued;
static unsigned g_lineDefCalls;
static uint8_t g_record[16], g_stratagemDef[0x3C], g_lastmagDef[0x3C], g_neededDef[0x3C];
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

static uint64_t TestRequest(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i,
    uint64_t j, uint64_t k, uint64_t l)
{
    const uint64_t v[12] = {a, b, c, d, e, f, g, h, i, j, k, l};
    memcpy(g_seen, v, sizeof v);
    return g_queued;
}

/* The game's line-defs, line names, entity records and voice component, as far as the test needs them: entity 77's
 * record keys the component by [record + 8] = 5, not by 77 (the manager maps 5 to slot 3). */
static const uint8_t* TestLineDef(uint32_t set, uint32_t group, uint32_t intensity)
{
    g_lineDefCalls++;
    if (intensity != SPOKEN) return NULL;
    if (set == 0x8BC32987u) return group == LASTMAG ? g_lastmagDef : NULL; /* the set of a speaker with no voice */
    if (set != SET) return NULL;
    return group == STRATAGEM ? g_stratagemDef : group == LASTMAG ? g_lastmagDef : group == NEEDED ? g_neededDef : NULL;
}
static const char* TestLineText(uint32_t set, uint32_t line) { return set == SET && line == 0x313876CCu ? "call_stratagem_equip_vehicle_a_spoken" : NULL; }
static const uint8_t* TestRecord(uint32_t entity) { return entity == ENTITY ? g_record : NULL; }
static const uint32_t* TestVoiceOf(const uint8_t* record)
{
    uint32_t key;
    memcpy(&key, record + 8, 4);
    return key == 5 ? (const uint32_t*)(g_fake.image + VM + 0x134 + 4 * SLOT) : NULL;
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
    co_voice_test_game(TestLineDef, TestLineText, TestRecord, TestVoiceOf);
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
    g_fake.originals[3].target = (uintptr_t)(g_fake.image + REQUEST);
    g_fake.originals[3].original = (void*)TestRequest;
    g_playing = 4242;
    g_queued = 0;
    memset(g_record, 0, sizeof g_record);
    g_record[8] = 5;
    memset(g_stratagemDef, 0, sizeof g_stratagemDef);
    memset(g_lastmagDef, 0, sizeof g_lastmagDef);
    g_stratagemDef[0x34] = 0xC0; g_stratagemDef[0x35] = 0x09;
    memset(g_neededDef, 0, sizeof g_neededDef);
    g_neededDef[0x34] = 0x06; g_neededDef[0x35] = 0x0C;
    Put32(SETS + 4 * 5, SET);
    PutPointer(SYSTEM, 0x3640000u);
    /* the customization manager: entity 77 is record 2 */
    PutPointer(CUSTOMIZATION, CM);
    Map(CM + 0x930, CM_MAP, ENTITY, RECORD);
    /* the four voices' ids */
    Put32(ROWS, 0x83BFB97Cu); Put32(ROWS + 0x38, 0xEF050987u); Put32(ROWS + 0x70, 0x09E450ACu); Put32(ROWS + 0xA8, 0x7ED81796u);
    /* the voice manager: entity 77 at slot 3 with value 6, two entities */
    PutPointer(VOICE_MANAGER, VM);
    Map(VM + 0x80, VM_MAP, 5, SLOT);
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
    CHECK(fake_host_detour(DELTA) != NULL && fake_host_detour(NAME) != NULL && fake_host_detour(POST) != NULL && fake_host_detour(REQUEST) != NULL);
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

    Start("another code at the request: nothing is hooked", 0x24);
    g_fake.noMatch[0] = REQUEST;
    co_voice_install((uintptr_t)g_fake.image);
    CHECK(fake_host_detour(REQUEST) == NULL && fake_host_detour(NAME) == NULL);

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
    Start("a line's name, value and prefix; the component keyed by the record's id", 0x30);
    Install();
    const NameFn name = (NameFn)fake_host_detour(NAME);
    if (name == NULL) { CHECK(0); return; }
    const uint64_t built = name(5, ENTITY, 0x1234, 2, buffer, 66);
    CHECK(built == 0x101);
    CHECK(g_seen[0] == 5 && g_seen[1] == ENTITY && g_seen[2] == 0x1234 && g_seen[3] == 2 && g_seen[4] == (uint64_t)(uintptr_t)buffer && g_seen[5] == 66);
    CHECK(fake_host_logged("voice: line 1 for entity 77: component slot 3 value 6 (vo_helldiver_soldier_female1), 2 voiced entities (room for 8); "
                           "name \"vo_helldiver_soldier_female1_reload_v02\", variant 2, event 0xABCD1234\n"));
    CHECK(g_hashLength == 39);
    name(5, 99, 0x1234, 2, buffer, 66);
    CHECK(fake_host_logged("voice: line 2 for entity 99: component slot -1 value -1 (-), 2 voiced entities"));
    CHECK(fake_host_logged("name \"reload_v01\", variant 2, event 0x00000000 (no such event: a request queued with it is dropped)"));
    name(5, 99, 0x1234, 0, buffer, 66);
    CHECK(fake_host_logged("name \"reload_v01\", variant 0, event 0x00000000 (variant 0: a base name, not an event; nothing dropped)"));
    CHECK(!fake_host_logged("drops the line"));
}

/* The gate of 0x13A0090 (0x13A01A5..0x13A01D0) as the log reads it. */
static void TestVerdict(void)
{
    g_case = "the state gate's verdicts";
    CHECK(co_voice_verdict(0x9C0, 0x1, 1, 0) == 2);  /* solo: bit 0 is forbidden by 0x9C0 >> 6 = 0x27 */
    CHECK(co_voice_verdict(0x40, 0x1, 1, 0) == 2);   /* 0x40 >> 6 = 1 */
    CHECK(co_voice_verdict(0x40, 0x2, 1, 1) == 0);   /* co-op: bit 0 clear, other bits do not matter to 0x40 */
    CHECK(co_voice_verdict(0x9C0, 0x8, 1, 1) == 0);  /* bit 3 is not in 0x27 */
    CHECK(co_voice_verdict(0x9C0, 0x20, 1, 0) == 2); /* bit 5 is */
    CHECK(co_voice_verdict(0x0, 0x3F, 1, 1) == 0);   /* mask 0 passes any state */
    CHECK(co_voice_verdict(0x0, 0x3F, 1, 0) == 4);
    CHECK(co_voice_verdict(0x0, 0x0, 0, 0) == 1);
    CHECK(co_voice_verdict(0x06, 0x02, 1, 0) == 3);  /* needs bits 1 and 2; only 1 set */
    CHECK(co_voice_verdict(0x06, 0x06, 1, 1) == 0);
    CHECK(co_voice_verdict(0x46, 0x07, 1, 0) == 2);  /* forbidden wins over needed */
    /* the game compares state & mask, all of it, with the mask's low six bits: a state bit 6 and up inside the mask refuses */
    CHECK(co_voice_verdict(0x9C0, 0x40, 1, 0) == 3);
    CHECK(co_voice_verdict(0x46, 0x86, 1, 1) == 0);  /* bit 7 is outside the mask */
    CHECK(co_voice_verdict(0x06, 0x106, 1, 1) == 0);
}

static void TestRequestGate(void)
{
    Start("a request: its arguments, result and gate", 0x30);
    Install();
    const RequestFn request = (RequestFn)fake_host_detour(REQUEST);
    if (request == NULL) { CHECK(0); return; }
    const uint64_t s = (uint64_t)(uintptr_t)(g_fake.image + 0x3640000u);
    Put32(0x3640000u, 0x1); /* what 0x13A7020 leaves in solo */
    CHECK(request(s, 0x313876CCu, 1, STRATAGEM, ENTITY, 0x3F800000u, SPOKEN, 1, 9, 10, 11, 12) == 0);
    CHECK(g_seen[0] == s && g_seen[1] == 0x313876CCu && g_seen[2] == 1 && g_seen[3] == STRATAGEM && g_seen[4] == ENTITY &&
          g_seen[5] == 0x3F800000u && g_seen[6] == SPOKEN && g_seen[7] == 1 && g_seen[8] == 9 && g_seen[9] == 10 && g_seen[10] == 11 &&
          g_seen[11] == 12);
    CHECK(fake_host_logged("voice: request 1 for entity 77 (value 6): line \"call_stratagem_equip_vehicle_a_spoken\" 0x313876CC, "
                           "group 0x21043F75, spoken; refused: mask 0x9C0, state 0x1, forbidden bits 0x1 (solo: the game's rule for one player)\n"));
    g_queued = 0x55;
    CHECK(request(s, 0x7777, 2, LASTMAG, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0) == 0x55);
    CHECK(fake_host_logged("voice: request 2 for entity 77 (value 6): line \"?\" 0x00007777, group 0x5C4397B3, spoken; mask 0x0, state 0x1; queued\n"));
    g_queued = 0;
    request(s, 0x7777, 2, LASTMAG, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x5C4397B3, spoken; mask 0x0, state 0x1 passed the gate; not queued: the speaker check (0x13A7510, "
                           "0x13A2700) or the queue (slot +0x30 != 1 after 0x13A4F90) turned it down\n"));
    request(s, 0x7777, 2, 0xABCDEF01u, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0xABCDEF01, spoken; refused: no line-def for this group\n"));
    Put32(0x3640000u, 0x2); /* co-op: bit 0 clear */
    g_queued = 0x66;
    request(s, 0x313876CCu, 1, STRATAGEM, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x21043F75, spoken; refused: mask 0x9C0, state 0x2, forbidden bits 0x2\n"));
    Put32(0x3640000u, 0x8);
    request(s, 0x313876CCu, 1, STRATAGEM, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x21043F75, spoken; mask 0x9C0, state 0x8; queued\n"));
    /* a speaker with no voice uses the set 0x8BC32987, which the stand-in has no line-def for */
    request(s, 0x313876CCu, 1, STRATAGEM, 99, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("voice: request 7 for entity 99 (value -1): line \"?\" 0x313876CC, group 0x21043F75, spoken; refused: no line-def"));
    request(s, 0x7777, 1, LASTMAG, 99, 0, SPOKEN, 1, 0, 0, 0, 0);
    Put32(0x3640000u, 0x2);
    request(s, 0x7777, 1, NEEDED, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x12345678, spoken; refused: mask 0xC06, state 0x2, state & mask 0x2 is not the mask's low bits 0x6\n"));
    CHECK(fake_host_logged("voice: request 8 for entity 99 (value -1): line \"?\" 0x00007777, group 0x5C4397B3, spoken; mask 0x0, state 0x8; queued\n"));
    /* only the spoken and combat hashes are known; any other is shown as it is */
    request(s, 0x7777, 1, 0x0BADF00Du, ENTITY, 0, 0x53347FB1u, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x0BADF00D, intensity 0x53347FB1; refused: no line-def"));
    request(s, 0x7777, 1, 0x0BADF00Eu, ENTITY, 0, 0x802F7915u, 1, 0, 0, 0, 0);
    CHECK(fake_host_logged("group 0x0BADF00E, combat; refused: no line-def"));
}

/* Each (group, verdict, state) the first time and each 20th; their own budget, which names cannot use up. */
static void TestRequestBudget(void)
{
    char buffer[256];
    Start("the request lines a session gets", 0x30);
    Install();
    const RequestFn request = (RequestFn)fake_host_detour(REQUEST);
    const NameFn name = (NameFn)fake_host_detour(NAME);
    if (request == NULL || name == NULL) { CHECK(0); return; }
    const uint64_t s = (uint64_t)(uintptr_t)(g_fake.image + 0x3640000u);
    Put32(0x3640000u, 0x1);
    for (int i = 0; i < 600; i++) { Put32(VM + 0x130, 4 + (uint32_t)(i & 1)); name(5, ENTITY, 1, 1, buffer, 0); }
    CHECK(Lines("(the last voice line of this session)") == 1);
    g_fake.log[0] = 0; /* room in the fake log for the request lines */
    for (int i = 0; i < 40; i++) request(s, 0x313876CCu, 1, STRATAGEM, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(Lines("voice: request ") == 3);
    CHECK(fake_host_logged("voice: request 1 ") && fake_host_logged("voice: request 20 ") && fake_host_logged("voice: request 40 ") &&
          !fake_host_logged("voice: request 2 "));
    CHECK(fake_host_logged("(solo: the game's rule for one player) (again)"));
    /* 100 new groups, each refused for the first time: all logged, past the full kinds table too */
    for (uint32_t g = 0; g < 100; g++) request(s, 0x313876CCu, 1, 0x1000u + g, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(Lines("voice: request ") == 103);
    /* queued lines of a kind the full table has no room for: each 20th, until the budget is spent */
    g_queued = 0x55;
    for (int i = 0; i < 1000; i++) request(s, 0x7777, 2, LASTMAG, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(Lines("voice: request ") == 150);
    CHECK(Lines("(the request budget is spent; only each group's first refusal follows)") == 1);
    /* spent: a queued line and a group refused before cost no lookup and no line; a group's first refusal still gets one */
    unsigned calls = g_lineDefCalls;
    request(s, 0x7777, 2, LASTMAG, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    g_queued = 0;
    request(s, 0x313876CCu, 1, STRATAGEM, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    request(s, 0x313876CCu, 1, 0x1005u, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(g_lineDefCalls == calls && Lines("voice: request ") == 150);
    request(s, 0x313876CCu, 1, 0x2000u, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(g_lineDefCalls == calls + 1 && Lines("voice: request ") == 151 && fake_host_logged("group 0x00002000, spoken; refused: no line-def"));
    request(s, 0x313876CCu, 1, 0x2000u, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(g_lineDefCalls == calls + 1 && Lines("voice: request ") == 151);
    /* the first refusals have room for 128 groups: 101 before, this one, 26 more; then none */
    for (uint32_t g = 1; g <= 40; g++) request(s, 0x313876CCu, 1, 0x2000u + g, ENTITY, 0, SPOKEN, 1, 0, 0, 0, 0);
    CHECK(Lines("voice: request ") == 177);
    CHECK(Lines("(the request budget is spent; only each group's first refusal follows)") == 1);
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
    TestVerdict();
    TestRequestGate();
    TestRequestBudget();
    TestLinePost();
    TestBudget();
    if (g_failed == 0) printf("voice: all passed\n");
    return g_failed != 0;
}
