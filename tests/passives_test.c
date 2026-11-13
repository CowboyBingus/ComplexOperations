/* The customization frame's passive packages (0x874D80) against a stand-in game image. The game's own code runs, with
 * stand-ins for the functions it calls, and every package it takes or releases is logged. Before the counts it keeps the
 * pairs of four player indexes: a fifth lies on its stack cookie. With the counts, a build for eight keeps the pairs of
 * all eight indexes in the mod's lists and a build for four keeps the guard that skips indexes 4 and up; both take and
 * release exactly what the game's rule gives for their room. The game's four pairs move to the mod's lists with the
 * counts and back when they go. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_stack_code.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
const uint32_t* co_counts_test_passives(void);
void co_counts_undo(void);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_ui_test_reset(void);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define FRAME_FN 0x874D80u          /* the customization frame: rcx the manager */
#define FRAME_END 0x874F71u
#define APPLY 0x874520u             /* applies one slot's desired record: the stand-in's records are already applied */
#define INDEX_OF 0x606820u          /* (player manager, entity id): the player index */
#define COMPARE 0x21049D0u          /* memcmp */
#define RESOLVE 0x12689C0u          /* (out, package id): the package or 0 */
#define RELEASE 0x12FE4E0u          /* (resources, &package, 1) */
#define ACQUIRE 0x12FE350u
#define COOKIE_CHECK 0x2088770u
#define COOKIE 0x263C010u
#define CUSTOMIZATION 0x33264F8u
#define PLAYERS 0x3326468u
#define RESOURCES 0x347CEB0u
#define GAME_PAIRS 0x37CED80u       /* last frame's pairs: four player indexes */
#define RECORDS_SITE 0x874DFCu      /* lea rbx,[rdi+0x9A4]: an applied record's +0x38, disp32 at +3 */
#define GUARD_SITE 0x874E26u
#define PASSIVES 42u                /* the passive map at +0x30 of the manager */
#define NO_INDEX 0xFFFFFFFFu
#define COOKIE_VALUE 0x2B992DDFA232ull

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The registers the stand-in's code starts and ends with (as in stack_test.c); a landing on RunExit returns to the test. */
typedef struct Cpu { uint64_t gp[16]; uint64_t flags, pad; uint8_t xmm[16][16]; } Cpu;
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
Cpu g_cpu;
uint64_t g_hostRsp, g_target;
void RunAt(void* target);
void RunExit(void);
__asm__(
    ".text\n.globl RunAt\nRunAt:\n"
    " pushq %rbx\n pushq %rbp\n pushq %rdi\n pushq %rsi\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n subq $0xA8, %rsp\n"
    " movaps %xmm6, 0x00(%rsp)\n movaps %xmm7, 0x10(%rsp)\n movaps %xmm8, 0x20(%rsp)\n movaps %xmm9, 0x30(%rsp)\n"
    " movaps %xmm10, 0x40(%rsp)\n movaps %xmm11, 0x50(%rsp)\n movaps %xmm12, 0x60(%rsp)\n movaps %xmm13, 0x70(%rsp)\n"
    " movaps %xmm14, 0x80(%rsp)\n movaps %xmm15, 0x90(%rsp)\n"
    " movq %rsp, g_hostRsp(%rip)\n movq %rcx, g_target(%rip)\n leaq g_cpu(%rip), %rax\n"
    " movups 0x90(%rax), %xmm0\n movups 0xA0(%rax), %xmm1\n movups 0xB0(%rax), %xmm2\n movups 0xC0(%rax), %xmm3\n"
    " movups 0xD0(%rax), %xmm4\n movups 0xE0(%rax), %xmm5\n movups 0xF0(%rax), %xmm6\n movups 0x100(%rax), %xmm7\n"
    " movups 0x110(%rax), %xmm8\n movups 0x120(%rax), %xmm9\n movups 0x130(%rax), %xmm10\n movups 0x140(%rax), %xmm11\n"
    " movups 0x150(%rax), %xmm12\n movups 0x160(%rax), %xmm13\n movups 0x170(%rax), %xmm14\n movups 0x180(%rax), %xmm15\n"
    " movq 0x08(%rax), %rcx\n movq 0x10(%rax), %rdx\n movq 0x18(%rax), %rbx\n movq 0x28(%rax), %rbp\n movq 0x30(%rax), %rsi\n"
    " movq 0x38(%rax), %rdi\n movq 0x40(%rax), %r8\n movq 0x48(%rax), %r9\n movq 0x50(%rax), %r10\n movq 0x58(%rax), %r11\n"
    " movq 0x60(%rax), %r12\n movq 0x68(%rax), %r13\n movq 0x70(%rax), %r14\n movq 0x78(%rax), %r15\n"
    " movq 0x20(%rax), %rsp\n movq 0x00(%rax), %rax\n jmpq *g_target(%rip)\n"
    ".globl RunExit\nRunExit:\n"
    " movq %rax, g_cpu+0x00(%rip)\n movq %rcx, g_cpu+0x08(%rip)\n movq %rdx, g_cpu+0x10(%rip)\n movq %rbx, g_cpu+0x18(%rip)\n"
    " movq %rsp, g_cpu+0x20(%rip)\n movq %rbp, g_cpu+0x28(%rip)\n movq %rsi, g_cpu+0x30(%rip)\n movq %rdi, g_cpu+0x38(%rip)\n"
    " movq %r8, g_cpu+0x40(%rip)\n movq %r9, g_cpu+0x48(%rip)\n movq %r10, g_cpu+0x50(%rip)\n movq %r11, g_cpu+0x58(%rip)\n"
    " movq %r12, g_cpu+0x60(%rip)\n movq %r13, g_cpu+0x68(%rip)\n movq %r14, g_cpu+0x70(%rip)\n movq %r15, g_cpu+0x78(%rip)\n"
    " pushfq\n popq g_cpu+0x80(%rip)\n"
    " movq g_hostRsp(%rip), %rsp\n"
    " movaps 0x00(%rsp), %xmm6\n movaps 0x10(%rsp), %xmm7\n movaps 0x20(%rsp), %xmm8\n movaps 0x30(%rsp), %xmm9\n"
    " movaps 0x40(%rsp), %xmm10\n movaps 0x50(%rsp), %xmm11\n movaps 0x60(%rsp), %xmm12\n movaps 0x70(%rsp), %xmm13\n"
    " movaps 0x80(%rsp), %xmm14\n movaps 0x90(%rsp), %xmm15\n"
    " addq $0xA8, %rsp\n popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rsi\n popq %rdi\n popq %rbp\n popq %rbx\n ret\n");

static _Alignas(16) uint8_t g_stack[0x40000];
#define FRAME ((uintptr_t)(g_stack + 0x20000))

/* ---- Stand-ins for what the frame calls, and the log of the packages ---- */
typedef struct Op { char what; uint64_t package; } Op;
static Op g_log[256];
static unsigned g_logged, g_cookieBroken, g_compares;
static uint32_t g_indexOf[16];      /* by slot: the player index 0x606820 gives for the slot's entity */
#define ENTITY_ID(slot) (0x5000u + (slot))
#define PACKAGE(id) (0xAB00000000ull + (id))

static void Log(char what, uint64_t package) { if (g_logged < sizeof g_log / sizeof g_log[0]) g_log[g_logged] = (Op){what, package}; g_logged++; }
static uint32_t IndexOf(uint64_t players, uint32_t id) { return id - ENTITY_ID(0) < 16u ? g_indexOf[id - ENTITY_ID(0)] : NO_INDEX; }
static int Compare(const void* a, const void* b, size_t size) { g_compares++; return memcmp(a, b, size); }
static uint64_t* Resolve(uint64_t* out, uint32_t id) { *out = id != 0 ? PACKAGE(id) : 0; return out; }
static void Release(uint64_t resources, const uint64_t* package, uint32_t count) { Log('R', *package); }
static void Acquire(uint64_t resources, const uint64_t* package, uint32_t count) { Log('A', *package); }
static void CookieCheck(uint64_t value) { if (value != COOKIE_VALUE) g_cookieBroken++; }

static void Put(uint32_t rva, const void* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

static void JumpTo(uint32_t rva, const void* to)
{
    uint8_t jump[14] = {0xFF, 0x25};
    const uint64_t at = (uint64_t)(uintptr_t)to;
    memcpy(jump + 6, &at, 8);
    Put(rva, jump, sizeof jump);
}

static uint32_t U32(uint32_t rva) { uint32_t v; memcpy(&v, g_fake.image + rva, 4); return v; }

/* ---- The stand-in customization manager: passives, slots and applied records ---- */
static _Alignas(16) uint8_t g_manager[0x1000];
static uint8_t g_passiveRecords[PASSIVES][0x40];
static uint64_t g_passiveList[PASSIVES];
static uint8_t g_entities[16][0x10];
static uint8_t g_players[0x40], g_resources[0x40];

/* Passive p is the list's entry p - 1 with package id 0x100 + p; 0 has no entry, and 7 one without a package. */
static int32_t Position(uint32_t passive) { return passive == 0 ? -1 : (int32_t)passive - 1; }
static uint32_t PackageId(uint32_t passive) { return passive == 7 ? 0 : 0x100u + passive; }

typedef struct Player { uint32_t index, helmet, armor; } Player;

static void Manager(const Player* players, uint32_t count)
{
    /* The records where the frame's code reads them. Without the managers' move, A0's slots 4 to 7 lie on record 0's
     * first bytes, not on the passives. */
    const uint32_t records = U32(RECORDS_SITE + 3) - 0x38u;
    const uint64_t list = (uint64_t)(uintptr_t)g_passiveList;
    memset(g_manager, 0, sizeof g_manager);
    memcpy(g_manager + 0x20, &list, 8);
    for (uint32_t p = 0; p < PASSIVES; p++)
    {
        const int32_t at = Position(p);
        const uint32_t id = PackageId(p);
        memcpy(g_manager + 0x30 + 4 * p, &at, 4);
        if (at < 0) continue;
        memset(g_passiveRecords[at], 0, sizeof g_passiveRecords[at]);
        memcpy(g_passiveRecords[at] + 0x30, &id, 4);
        g_passiveList[at] = (uint64_t)(uintptr_t)g_passiveRecords[at];
    }
    memcpy(g_manager + 0x8E8, &count, 4);
    for (uint32_t s = 0; s < count; s++)
    {
        const uint64_t entity = (uint64_t)(uintptr_t)g_entities[s];
        const uint32_t id = ENTITY_ID(s);
        memcpy(g_entities[s] + 8, &id, 4);
        memcpy(g_manager + 0x948 + 8 * s, &entity, 8);
        memcpy(g_manager + records + 0x44 * s + 0x38, &players[s].helmet, 4);
        memcpy(g_manager + records + 0x44 * s + 0x3C, &players[s].armor, 4);
        g_indexOf[s] = players[s].index;
    }
}

/* The game's rule for a room of `room` player indexes: this frame's pairs against last frame's, `last` updated. */
static unsigned g_wantCount;
static Op g_want[256];
static void Want(char what, uint32_t passive)
{
    const int32_t at = Position(passive);
    if (at >= 0 && PackageId(passive) != 0 && g_wantCount < 256) g_want[g_wantCount++] = (Op){what, PACKAGE(PackageId(passive))};
}

static void Rule(uint32_t* last, const Player* players, uint32_t count, uint32_t room)
{
    uint32_t now[16] = {0};
    for (uint32_t s = 0; s < count; s++)
        if (players[s].index < room) { now[2 * players[s].index] = players[s].armor; now[2 * players[s].index + 1] = players[s].helmet; }
    g_wantCount = 0;
    for (uint32_t i = 0; i < 2 * room; i++)
        if (now[i] != last[i]) { Want('R', last[i]); Want('A', now[i]); last[i] = now[i]; }
}

/* One frame of the game's code; the globals it reads lead to the stand-ins only while it runs. */
static void Frame(const Player* players, uint32_t count)
{
    static const uint64_t kKept[] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555};
    const uint64_t manager = (uint64_t)(uintptr_t)g_manager, cookie = COOKIE_VALUE;
    const uint64_t pp = (uint64_t)(uintptr_t)g_players, rp = (uint64_t)(uintptr_t)g_resources;
    uint64_t exit = (uint64_t)(uintptr_t)RunExit;
    Manager(players, count);
    memcpy(g_fake.image + CUSTOMIZATION, &manager, 8);
    memcpy(g_fake.image + PLAYERS, &pp, 8);
    memcpy(g_fake.image + RESOURCES, &rp, 8);
    memcpy(g_fake.image + COOKIE, &cookie, 8);
    memset(g_stack, 0xA5, sizeof g_stack);
    memcpy((void*)(FRAME - 8), &exit, 8);
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.gp[RSP] = FRAME - 8;
    g_cpu.gp[RCX] = manager;
    g_cpu.gp[RBX] = kKept[0]; g_cpu.gp[RBP] = kKept[1]; g_cpu.gp[RSI] = kKept[2]; g_cpu.gp[RDI] = kKept[3]; g_cpu.gp[R14] = kKept[4];
    g_logged = g_cookieBroken = g_compares = 0;
    RunAt(g_fake.image + FRAME_FN);
    CHECK(g_cpu.gp[RSP] == FRAME && g_compares == 1);
    CHECK(g_cpu.gp[RBX] == kKept[0] && g_cpu.gp[RBP] == kKept[1] && g_cpu.gp[RSI] == kKept[2] && g_cpu.gp[RDI] == kKept[3] && g_cpu.gp[R14] == kKept[4]);
    memset(g_fake.image + CUSTOMIZATION, 0, 8);
    memset(g_fake.image + PLAYERS, 0, 8);
    memset(g_fake.image + RESOURCES, 0, 8);
}

/* The frame took and released what the rule gives, in its order, and left the stack cookie as it was. */
static int AsRule(void)
{
    if (g_logged != g_wantCount || g_cookieBroken != 0) return 0;
    for (unsigned i = 0; i < g_logged; i++)
        if (g_log[i].what != g_want[i].what || g_log[i].package != g_want[i].package) return 0;
    return 1;
}

static void LoadGame(void)
{
    static const uint8_t kRet = 0xC3;
    for (size_t i = 0; i < sizeof kStackCode / sizeof kStackCode[0]; i++)
        if (kStackCode[i].rva == FRAME_FN) Put(FRAME_FN, kStackCode[i].code, kStackCode[i].size);
    CHECK(g_fake.image[FRAME_FN] == 0x48 && FRAME_FN + 0x1F1u == FRAME_END);
    Put(APPLY, &kRet, 1);
    JumpTo(INDEX_OF, (const void*)IndexOf);
    JumpTo(COMPARE, (const void*)Compare);
    JumpTo(RESOLVE, (const void*)Resolve);
    JumpTo(RELEASE, (const void*)Release);
    JumpTo(ACQUIRE, (const void*)Acquire);
    JumpTo(COOKIE_CHECK, (const void*)CookieCheck);
}

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
    LoadGame();
}

static void Init(void)
{
    typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
}

static int GamePairs(const uint32_t* want, uint32_t n) { return memcmp(g_fake.image + GAME_PAIRS, want, 4 * n) == 0; }

/* Slots and indexes differ; passives 0 (no entry) and 7 (no package) are among them. */
static const Player kFour[] = {{3, 4, 5}, {1, 0, 9}, {0, 7, 7}, {2, 12, 13}};
static const Player kFourChanged[] = {{3, 4, 6}, {1, 0, 9}, {2, 12, 13}};
static const Player kEight[] = {{6, 20, 21}, {3, 4, 6}, {1, 0, 9}, {7, 22, 23}, {0, 30, 31}, {5, 24, 25}, {2, 12, 13}, {4, 26, 27}};
static const Player kEightHelmet[] = {{6, 28, 21}, {3, 4, 6}, {1, 0, 9}, {7, 22, 23}, {0, 30, 31}, {5, 24, 25}, {2, 12, 13}, {4, 26, 27}};
static const Player kSevenLeft[] = {{6, 28, 21}, {3, 4, 6}, {1, 0, 9}, {7, 22, 23}, {0, 30, 31}, {2, 12, 13}, {4, 26, 27}};
static const Player kStranger[] = {{6, 28, 21}, {3, 4, 6}, {NO_INDEX, 40, 41}, {7, 22, 23}, {0, 30, 31}, {2, 12, 13}, {4, 26, 27}};
static const Player kSix[] = {{3, 4, 5}, {1, 0, 9}, {0, 7, 7}, {2, 12, 13}, {4, 26, 27}, {5, 24, 25}};
#define N(a) ((uint32_t)(sizeof a / sizeof a[0]))

int main(void)
{
    uint32_t last[16] = {0};

    /* The game's code alone: four indexes, by its own rule, then a fifth on the stack cookie. */
    Start("the game's frame");
    Frame(kFour, N(kFour));
    Rule(last, kFour, N(kFour), 4);
    CHECK(AsRule() && g_logged == 5 && GamePairs(last, 8));
    Frame(kFour, N(kFour));
    Rule(last, kFour, N(kFour), 4);
    CHECK(AsRule() && g_logged == 0);
    Frame(kFourChanged, N(kFourChanged));
    Rule(last, kFourChanged, N(kFourChanged), 4);
    CHECK(AsRule() && g_logged == 2 && GamePairs(last, 8));
    {
        const Player fifth[] = {{3, 4, 6}, {4, 26, 27}};
        Frame(fifth, N(fifth));
        CHECK(g_cookieBroken == 1);
    }

    /* With the counts. The game's pairs carry over: the same players change nothing. */
    Start("the frame with the counts");
    memset(last, 0, sizeof last);
    Frame(kFourChanged, N(kFourChanged));
    Rule(last, kFourChanged, N(kFourChanged), 4);
    CHECK(AsRule() && g_logged == 5);
    Init();
    CHECK(co_counts_test_on() == 1);
    CHECK((g_fake.image[GUARD_SITE] == 0xE9) && (g_fake.image[0x874E73 + 1] == (CO_PLAYERS == 8 ? 0x10 : 0x08)));
    CHECK((g_fake.image[0x874E80] == 0xE9) == (CO_PLAYERS == 8) && (g_fake.image[0x874F30] == 0xE9) == (CO_PLAYERS == 8));
    if (CO_PLAYERS == 8)
    {
        const uint32_t* mod = co_counts_test_passives();
        static const uint32_t kZero[24];
        uint32_t carried[8];
        memcpy(carried, last, sizeof carried);
        CHECK(memcmp(mod, last, 32) == 0 && memcmp(mod + 8, kZero, sizeof kZero) == 0);
        Frame(kFourChanged, N(kFourChanged));
        Rule(last, kFourChanged, N(kFourChanged), 8);
        CHECK(AsRule() && g_logged == 0);
        /* Eight indexes: every pair is held, the game's place keeps what came over and the cookie stays. */
        Frame(kEight, N(kEight));
        Rule(last, kEight, N(kEight), 8);
        CHECK(AsRule() && g_logged == 10 && memcmp(mod, last, 64) == 0 && GamePairs(carried, 8));
        Frame(kEight, N(kEight));
        Rule(last, kEight, N(kEight), 8);
        CHECK(AsRule() && g_logged == 0);
        /* Only index 6 changes: its helmet's package goes and the new one comes. */
        Frame(kEightHelmet, N(kEightHelmet));
        Rule(last, kEightHelmet, N(kEightHelmet), 8);
        CHECK(AsRule() && g_logged == 2 && g_log[0].package == PACKAGE(0x114) && g_log[1].package == PACKAGE(0x11C));
        /* Index 5 leaves: both its packages go. */
        Frame(kSevenLeft, N(kSevenLeft));
        Rule(last, kSevenLeft, N(kSevenLeft), 8);
        CHECK(AsRule() && g_logged == 2 && g_log[0].what == 'R' && g_log[1].what == 'R' && memcmp(mod, last, 64) == 0);
        /* An entity without an index holds nothing; index 1's packages go. */
        Frame(kStranger, N(kStranger));
        Rule(last, kStranger, N(kStranger), 8);
        CHECK(AsRule() && g_logged == 1 && g_log[0].what == 'R');
        /* The counts go: the game's four pairs are the mod's, and the game's code goes on from them. */
        co_counts_undo();
        CHECK(co_counts_test_on() == 0 && g_fake.image[GUARD_SITE] == 0x8D && GamePairs(last, 8));
        Frame(kFourChanged, N(kFourChanged));
        Rule(last, kFourChanged, N(kFourChanged), 4);
        CHECK(AsRule() && GamePairs(last, 8));
    }
    else
    {
        /* A build for four: the guard skips indexes 4 and 5, the cookie stays and the mod's lists are not used. */
        static const uint32_t kZero[32];
        Frame(kSix, N(kSix));
        Rule(last, kSix, N(kSix), 4);
        CHECK(AsRule() && g_logged == 2 && GamePairs(last, 8));
        CHECK(memcmp(co_counts_test_passives(), kZero, sizeof kZero) == 0);
        co_counts_undo();
        CHECK(GamePairs(last, 8));
    }

    printf("%s\n", g_failed ? "passives: failures" : "passives: all cases passed");
    return g_failed != 0;
}
