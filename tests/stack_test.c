/* The stack fixes against a stand-in game image: written with the counts in every build, those for eight avatars only
 * in a build for eight; a changed byte refuses them with the counts and a failed write takes all of it back. The game's
 * own code runs beside the patched code on the same stand-in avatars: the cover and hellpod culls' distance sums, the
 * reinforcement anchor's search and the encounter's shuffle and calls give the game's result bit for bit, also past four
 * avatars, where the game's code here writes past its arrays into the test's spare stack. An avatar without a motion
 * record is left out where the game would read record 0xFFFFFFFF. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_stack_code.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
int co_counts_test_reviewed(const uint8_t* image);
uintptr_t co_counts_test_caves(void);
unsigned co_counts_test_room(void);
int co_counts_test_write_of(uint32_t rva);
unsigned co_counts_test_stack(void);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_ui_test_reset(void);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define AVATARS_GLOBAL 0x3326D20u
#define MOTION_GLOBAL 0x3326558u
#define NULL_ID 0x3483C20u
#define ENC_STATE 0x3483C38u
#define ENC_OBJECT 0x33264F0u
#define EC_PHASE1 0x8C8DCBu         /* the cover cull: avatar positions into vec3[4] at rsp+0x60 */
#define EC_PHASE1_END 0x8C8EB3u
#define EC_SUM 0x8C904Bu            /* the sum for one candidate: xmm0 x and y, eax z; the sum in xmm6 */
#define EC_SUM_END 0x8C92CAu         /* behind the reload of r14 the patched cave returns to; no avatar comes here */
#define EC_SITE 0x8C9070u
#define HP_PHASE1 0x92F138u         /* the hellpod cull, the same at rsp+0x58 */
#define HP_PHASE1_END 0x92F20Fu
#define HP_SUM 0x92F614u
#define HP_SUM_END 0x92F89Cu
#define RA_END 0xACB951u            /* the anchor's end of one avatar (RA7) */
#define RA_LOOP 0xACB958u           /* jb to the next avatar */
#define RA_SEARCH 0xACB95Eu         /* the game's search for the best score */
#define RA_TAKE 0xACB971u           /* mov edx,r15d (RA8) */
#define RA_FOUND 0xACBA52u          /* the best avatar's index in edx */
#define ENC 0x9FAA30u               /* the encounter's stage change */
#define ENC_SITE 0x9FAAB5u
#define ENC_TABLE 0x4F8C00u         /* returns the stage table */
#define ENC_CALL 0x9FAC40u          /* one enemy call for the avatar index in edx */
#define COOKIE_CHECK 0x2088770u
#define SC_FILL 0x1843624u          /* SC1: the list's fill */
#define SC_FILL_END 0x1843630u
#define SC_STORE 0x18437DDu         /* SC2: a container id by player slot r13d */
#define SC_STORE_END 0x18437E2u
#define VO_SITE 0x4C3CC2u
#define DL_SITE 0x950B66u
#define LV_SITE 0xAD5466u
#define CP_SITE 0x874E80u          /* CP7: the customization frame's compare of a passive pair */

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The registers the stand-in's code starts with and ends with: rax rcx rdx rbx rsp rbp rsi rdi r8..r15, the flags,
 * xmm0..xmm15. RunAt jumps to code with them; a landing that jumps to RunExit keeps them and returns to the test. */
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
    " movups %xmm0, g_cpu+0x90(%rip)\n movups %xmm1, g_cpu+0xA0(%rip)\n movups %xmm2, g_cpu+0xB0(%rip)\n movups %xmm3, g_cpu+0xC0(%rip)\n"
    " movups %xmm4, g_cpu+0xD0(%rip)\n movups %xmm5, g_cpu+0xE0(%rip)\n movups %xmm6, g_cpu+0xF0(%rip)\n movups %xmm7, g_cpu+0x100(%rip)\n"
    " movups %xmm8, g_cpu+0x110(%rip)\n movups %xmm9, g_cpu+0x120(%rip)\n movups %xmm10, g_cpu+0x130(%rip)\n movups %xmm11, g_cpu+0x140(%rip)\n"
    " movups %xmm12, g_cpu+0x150(%rip)\n movups %xmm13, g_cpu+0x160(%rip)\n movups %xmm14, g_cpu+0x170(%rip)\n movups %xmm15, g_cpu+0x180(%rip)\n"
    " movq g_hostRsp(%rip), %rsp\n"
    " movaps 0x00(%rsp), %xmm6\n movaps 0x10(%rsp), %xmm7\n movaps 0x20(%rsp), %xmm8\n movaps 0x30(%rsp), %xmm9\n"
    " movaps 0x40(%rsp), %xmm10\n movaps 0x50(%rsp), %xmm11\n movaps 0x60(%rsp), %xmm12\n movaps 0x70(%rsp), %xmm13\n"
    " movaps 0x80(%rsp), %xmm14\n movaps 0x90(%rsp), %xmm15\n"
    " addq $0xA8, %rsp\n popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rsi\n popq %rdi\n popq %rbp\n popq %rbx\n ret\n");

/* The game's stack for the stand-in's code: its frame in the middle, room below for the caves and the callbacks. */
static _Alignas(16) uint8_t g_stack[0x40000];
#define FRAME ((uintptr_t)(g_stack + 0x20000))

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

/* The session step moves its lists in the session init's hook and the counts follow. */
static void Init(void)
{
    typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
}

static void Put(uint32_t rva, const void* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* jmp [rip+0] to `to`: 14 bytes. */
static void JumpTo(uint32_t rva, const void* to)
{
    uint8_t jump[14] = {0xFF, 0x25};
    const uint64_t at = (uint64_t)(uintptr_t)to;
    memcpy(jump + 6, &at, 8);
    Put(rva, jump, sizeof jump);
}

static void Global(uint32_t rva, const void* value) { const uint64_t v = (uint64_t)(uintptr_t)value; memcpy(g_fake.image + rva, &v, 8); }

/* The game's code that the runs need, its landings and stand-ins for the functions it calls. */
static void Recorder(uint64_t unused, uint32_t avatar);
static void LoadGame(void)
{
    static const uint8_t kRet = 0xC3;
    for (size_t i = 0; i < sizeof kStackCode / sizeof kStackCode[0]; i++) Put(kStackCode[i].rva, kStackCode[i].code, kStackCode[i].size);
    for (size_t i = 0; i < 4; i++)
    {
        static const uint32_t kEnds[] = {EC_PHASE1_END, EC_SUM_END, HP_PHASE1_END, HP_SUM_END};
        JumpTo(kEnds[i], (const void*)RunExit);
    }
    JumpTo(RA_FOUND, (const void*)RunExit);
    JumpTo(ENC_CALL, (const void*)Recorder);
    Put(COOKIE_CHECK, &kRet, 1);
}

/* ---- Stand-in avatars: the avatar manager, its entities and the motion manager's hash of ids to positions ---- */
#define BUCKETS 64u
#define EMPTY 0xFFFFFFFFu
#define NONE_ID 0x7FFFFFF0u
static _Alignas(16) uint8_t g_avatars[0x200];
static uint8_t g_entities[16][0x10];
static _Alignas(16) uint8_t g_motion[0x4900];
static uint32_t g_buckets[2 * BUCKETS];
static uint8_t g_records[64 * 0x2C];

typedef struct Avatar { float x, y, z; int kind; } Avatar;   /* kind 0 with a record, 1 the id of no entity, 2 no record */

static void Avatars(const Avatar* avatars, uint32_t n, int keepInvalid)
{
    const uint32_t mult = 0x9E3779B1u, none = NONE_ID;
    uint32_t count = 0, records = 0;
    memset(g_avatars, 0, sizeof g_avatars);
    memset(g_motion, 0, sizeof g_motion);
    for (uint32_t i = 0; i < 2 * BUCKETS; i += 2) { g_buckets[i] = EMPTY; g_buckets[i + 1] = 0xBADu; }
    memset(g_records, 0x7F, sizeof g_records);
    for (uint32_t i = 0; i < n; i++)
    {
        if (avatars[i].kind != 0 && !keepInvalid) continue;
        const uint32_t id = avatars[i].kind == 1 ? none : 0x1000u + 0x35u * i;
        memcpy(g_entities[count] + 8, &id, 4);
        const uint64_t entity = (uint64_t)(uintptr_t)g_entities[count];
        memcpy(g_avatars + 0x110 + 8 * count, &entity, 8);
        count++;
        if (avatars[i].kind != 0) continue;
        /* records out of order, as the motion manager keeps them */
        const uint32_t record = (records * 7u + 3u) % 64u;
        records++;
        memcpy(g_records + record * 0x2Cu, &avatars[i].x, 12);
        for (uint32_t k = 0; k < BUCKETS; k++)
        {
            const uint32_t s = (k + mult * id) & (BUCKETS - 1u);
            if (g_buckets[2 * s] == EMPTY) { g_buckets[2 * s] = id; g_buckets[2 * s + 1] = record; break; }
        }
    }
    memcpy(g_avatars + 0x6C, &count, 4);
    {
        const uint32_t size = BUCKETS, empty = EMPTY;
        const uint64_t buckets = (uint64_t)(uintptr_t)g_buckets, recs = (uint64_t)(uintptr_t)g_records;
        memcpy(g_motion + 0x48A0, &buckets, 8);
        memcpy(g_motion + 0x48A8, &size, 4);
        memcpy(g_motion + 0x48AC, &empty, 4);
        memcpy(g_motion + 0x48B0, &mult, 4);
        memcpy(g_motion + 0x48D8, &recs, 8);
    }
    Global(AVATARS_GLOBAL, g_avatars);
    Global(MOTION_GLOBAL, g_motion);
    memcpy(g_fake.image + NULL_ID, &none, 4);
}

/* The cases: avatar sets and a candidate each. Positions repeat a candidate (distance 0), go negative and far. */
#define CASES 14
static const Avatar kSets[CASES][16] = {
    {{0}},
    {{1.5f, 2.0f, -3.0f, 0}},
    {{1.5f, 2.0f, -3.0f, 0}, {-120.25f, 33.0f, 7.5f, 0}},
    {{10, 0, 0, 0}, {0, 10, 0, 0}, {0, 0, 10, 0}},
    {{3.25f, -8.5f, 1e3f, 0}, {-77.0f, 0.125f, 4.0f, 0}, {5, 5, 5, 0}, {1e-3f, 2e4f, -6.0f, 0}},
    {{3.25f, -8.5f, 1e3f, 0}, {9, 9, 9, 1}, {5, 5, 5, 0}, {1e-3f, 2e4f, -6.0f, 0}},
    {{3.25f, -8.5f, 1e3f, 2}, {-77.0f, 0.125f, 4.0f, 0}, {5, 5, 5, 0}, {1e-3f, 2e4f, -6.0f, 2}},
    {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}, {-13, 14, -15, 0}},
    {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 1}, {10, 11, 12, 0}, {-13, 14, -15, 0}, {0.5f, 0.25f, 0.125f, 0}},
    {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}, {-13, 14, -15, 0}, {0.5f, 0.25f, 0.125f, 0}, {300, -300, 30, 0}, {1, 1, 1, 0}},
    {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}, {-13, 14, -15, 0}, {0.5f, 0.25f, 0.125f, 0}, {300, -300, 30, 0}, {1, 1, 1, 0},
     {2, 2, 2, 0}, {-2, -2, -2, 0}, {64, 0, 64, 2}, {0, 64, 0, 0}},
    {{1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0}, {-13, 14, -15, 0}, {0.5f, 0.25f, 0.125f, 0}, {300, -300, 30, 0}, {1, 1, 1, 0},
     {2, 2, 2, 0}, {-2, -2, -2, 0}, {64, 0, 64, 0}, {0, 64, 0, 0}, {11, 22, 33, 0}, {-44, 55, -66, 0}, {7.75f, 0, 1, 0}, {8, 8, 8, 0}},
    {{9, 9, 9, 1}},
    {{9, 9, 9, 2}, {1, 1, 1, 0}},
};
static const uint32_t kCounts[CASES] = {0, 1, 2, 3, 4, 4, 4, 5, 6, 8, 12, 16, 1, 2};
static const float kCandidates[CASES][3] = {
    {1, 1, 1}, {1.5f, 2.0f, -3.0f}, {0, 0, 0}, {10, 0, 0}, {-50.5f, 12.0f, 3.0f}, {5, 5, 5}, {0.1f, 0.2f, 0.3f},
    {4, 5, 6}, {-1, -1, -1}, {100, 100, 100}, {2, 2, 2}, {-3.5f, 7.25f, 0}, {9, 9, 9}, {0, 0, 0},
};

static uint32_t Bits(const uint8_t* xmm) { uint32_t b; memcpy(&b, xmm, 4); return b; }

/* One candidate's sum as the cull's code leaves it in xmm6: phase 1 (the patched code skips it), then the sum. */
static uint32_t CullSum(uint32_t phase1, uint32_t sum, const float* c, int* untouched)
{
    uint8_t* frame = (uint8_t*)FRAME;
    memset(g_stack, 0xA5, sizeof g_stack);
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.gp[RSP] = FRAME;
    g_cpu.gp[RCX] = 0x5151000u;   /* the candidates' list: kept, not read here */
    RunAt(g_fake.image + phase1);
    if (untouched != NULL) *untouched = frame[0x58] == 0xA5 && frame[0x60] == 0xA5 && frame[0x8F] == 0xA5;
    CHECK(g_cpu.gp[RSP] == FRAME);
    /* what the code before the sum leaves: ebp 0, xmm6 and the zero register (xmm7, xmm10) 0 */
    g_cpu.gp[RBP] = 0;
    memset(g_cpu.xmm, 0, sizeof g_cpu.xmm);
    memcpy(g_cpu.xmm[0], c, 8);
    memcpy(&g_cpu.gp[RAX], c + 2, 4);
    g_cpu.gp[RAX] &= 0xFFFFFFFFu;
    RunAt(g_fake.image + sum);
    CHECK(g_cpu.gp[RSP] == FRAME);
    return Bits(g_cpu.xmm[6]);
}

/* ---- The reinforcement anchor: scores per avatar ---- */
#define SCORE_SETS 8
static const float kScores[SCORE_SETS][16] = {
    {5}, {1, 2}, {2, 1}, {3, 3, 3}, {-1, -5, -0.5f, -0.5f}, {0, 7, 7, 6, 8, 8, 1}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
    {16, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 17},
};
static const uint32_t kScoreCounts[SCORE_SETS] = {1, 2, 2, 3, 4, 7, 16, 16};

static uint32_t SearchAsGame(const float* scores, uint32_t n)
{
    uint8_t* frame = (uint8_t*)FRAME;
    memset(g_stack, 0, sizeof g_stack);
    memset(&g_cpu, 0, sizeof g_cpu);
    memcpy(g_avatars + 0x6C, &n, 4);
    memcpy(frame + 0x400 - 0x80, scores, n * 4u);
    g_cpu.gp[RSP] = FRAME;
    g_cpu.gp[RBP] = FRAME + 0x400;
    g_cpu.gp[RDI] = (uint64_t)(uintptr_t)g_avatars;
    RunAt(g_fake.image + RA_SEARCH);
    return (uint32_t)g_cpu.gp[RDX];
}

/* The patched end of each avatar (RA7), then RA8's take and RA9's jump to the found index. */
static uint32_t SearchPatched(const float* scores, uint32_t n)
{
    uint8_t* frame = (uint8_t*)FRAME;
    memset(g_stack, 0, sizeof g_stack);
    memcpy(g_avatars + 0x6C, &n, 4);
    for (uint32_t i = 0; i < n; i++)
    {
        memset(&g_cpu, 0, sizeof g_cpu);
        memcpy(frame + 0x400 - 0x80, &scores[i], 4);
        g_cpu.gp[RSP] = FRAME;
        g_cpu.gp[RBP] = FRAME + 0x400;
        g_cpu.gp[RDI] = (uint64_t)(uintptr_t)g_avatars;
        g_cpu.gp[R12] = i;
        RunAt(g_fake.image + RA_END);
        CHECK(g_cpu.gp[R12] == i + 1 && ((g_cpu.flags & 1) != 0) == (i + 1 < n));
        CHECK(*(const uint32_t*)(frame + 0x400 - 0x80) == 0);
    }
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.gp[RSP] = FRAME;
    g_cpu.gp[RBP] = FRAME + 0x400;
    g_cpu.gp[RDI] = (uint64_t)(uintptr_t)g_avatars;
    g_cpu.gp[R15] = 0x55;   /* the game's register: no longer read */
    RunAt(g_fake.image + RA_TAKE);
    return (uint32_t)g_cpu.gp[RDX];
}

/* ---- The encounter's stage change ---- */
static uint32_t g_called[64], g_calls;
static void Recorder(uint64_t unused, uint32_t avatar)
{
    if (g_calls < 64) g_called[g_calls] = avatar;
    g_calls++;
}

/* The stage table: two stages, the second reached with `calls` enemy calls; the encounter at stage 0. */
static uint8_t g_table[0x40], g_encounter[0x60];
static void Stage(uint32_t calls, uint64_t seed)
{
    static const uint32_t kTable[] = {2, 0, 0, 0, 50, 0, 0};
    memcpy(g_table, kTable, sizeof kTable);
    memcpy(g_table + 24, &calls, 4);
    memset(g_encounter, 0, sizeof g_encounter);
    const uint32_t time = 100;
    memcpy(g_encounter + 0x44, &time, 4);
    Global(ENC_OBJECT, g_encounter);
    memcpy(g_fake.image + ENC_STATE, &seed, 8);
    g_calls = 0;
    memset(g_called, 0, sizeof g_called);
    {
        uint8_t table[11] = {0x48, 0xB8};   /* mov rax,table; ret */
        const uint64_t at = (uint64_t)(uintptr_t)g_table;
        memcpy(table + 2, &at, 8);
        table[10] = 0xC3;
        Put(ENC_TABLE, table, sizeof table);
    }
}

typedef struct EncResult { uint32_t called[64], calls; uint64_t state; } EncResult;
static void Encounter(uint32_t calls, uint64_t seed, EncResult* out)
{
    Stage(calls, seed);
    ((void (*)(void))(uintptr_t)(g_fake.image + ENC))();
    memcpy(out->called, g_called, sizeof g_called);
    out->calls = g_calls;
    memcpy(&out->state, g_fake.image + ENC_STATE, 8);
    CHECK(*(const uint32_t*)(g_encounter + 0x40) == 1 && *(const uint32_t*)(g_encounter + 0x44) == 105);
}

/* ---- The dormancy pass: one entity, its type's radius, and the players around it ---- */
#define DORMANCY 0xA04910u
#define DP_SITE 0xA04982u
#define PLAYERS_GLOBAL 0x3326468u   /* count at +0x84, a player's avatar handle at +0x3A8 + 0x20 * index */
#define UNITS_GLOBAL 0x3326508u     /* a hash of ids to records of 0x308, the position at +0x2E0 */
#define TYPES_GLOBAL 0x346BF98u     /* +0xF12660 points at 26 buckets {key, row}, then rows {flag, radius, interval} */
#define TYPES_AT 0xF12660u
#define SKIP_ID 0x3483C4Cu          /* a player with this id keeps the default position */
#define AVATAR_ID 0xFD9BA0u         /* (out, handle): the id of a player's avatar */
#define COOKIE 0x263C010u
#define NO_POSITION 0x37982A0u
#define DP_PLAYER 0x2000u
#define DP_ENTITY 0x3000u
#define DP_SKIPPED 0x7FFFFFF1u
static const uint64_t kCookie = 0x00C0FFEE5EEDF00Du;
static const float kHere[3] = {40.0f, -12.5f, 3.0f};
static int g_cookieKept;
static void CookieCheck(uint64_t value) { g_cookieKept = value == kCookie; }

static uint8_t g_players[0xA00], g_units[0x70], g_unitRecords[16 * 0x308], g_types[0x1A0 + 12], g_pass[0x60], g_dormant[0x18];
static uint32_t g_unitBuckets[2 * BUCKETS];
static uint64_t g_dormants[1], g_typesAt;
static float g_timers[1];

static void Unit(uint32_t id, uint32_t record, const float* position)
{
    const uint32_t mult = 0x9E3779B1u;
    memcpy(g_unitRecords + record * 0x308u + 0x2E0u, position, 12);
    for (uint32_t k = 0; k < BUCKETS; k++)
    {
        const uint32_t s = (k + mult * id) & (BUCKETS - 1u);
        if (g_unitBuckets[2 * s] == EMPTY) { g_unitBuckets[2 * s] = id; g_unitBuckets[2 * s + 1] = record; break; }
    }
}

/* The pass over one entity at kHere with a radius of 10 and `n` players: player `close` beside it, the rest far off;
 * player `skipped` has no avatar and so the default position, which is `nowhere`. 1 when the entity goes dormant. */
static int Dormant(uint32_t n, int close, int skipped, const float* nowhere)
{
    static const uint8_t kAvatarId[] = {0x89, 0x11, 0x48, 0x89, 0xC8, 0xC3};   /* mov [rcx],edx; mov rax,rcx; ret */
    static const uint32_t kFloats[][2] = {{0x23C6538u, 0x2F800000u}, {0x23C75E8u, 0x41A00000u}, {0x23C7EF8u, 0x7F7FFFFFu}};
    const uint32_t size = BUCKETS, empty = EMPTY, mult = 0x9E3779B1u, none = NONE_ID, skip = DP_SKIPPED, one = 1, key = 5, entity = DP_ENTITY;
    const uint64_t buckets = (uint64_t)(uintptr_t)g_unitBuckets, records = (uint64_t)(uintptr_t)g_unitRecords;
    const uint64_t list = (uint64_t)(uintptr_t)g_dormants, timers = (uint64_t)(uintptr_t)g_timers;
    const float radius = 10.0f, interval = 2.0f, dt = 1.0f;
    uint64_t* stack = (uint64_t*)FRAME;

    memset(g_players, 0, sizeof g_players);
    memset(g_units, 0, sizeof g_units);
    memset(g_unitRecords, 0, sizeof g_unitRecords);
    memset(g_types, 0, sizeof g_types);
    memset(g_pass, 0, sizeof g_pass);
    memset(g_dormant, 0, sizeof g_dormant);
    for (uint32_t i = 0; i < 2 * BUCKETS; i += 2) { g_unitBuckets[i] = EMPTY; g_unitBuckets[i + 1] = 0xBADu; }
    memcpy(g_players + 0x84, &n, 4);
    for (uint32_t i = 0; i < n; i++)
    {
        const uint32_t id = (int)i == skipped ? skip : DP_PLAYER + i;
        const float away[3] = {kHere[0] + 100.0f + 10.0f * (float)i, kHere[1], kHere[2] - 50.0f};
        const float beside[3] = {kHere[0] + 1.0f, kHere[1] - 2.0f, kHere[2] + 0.5f};
        memcpy(g_players + 0x3A8 + 0x20 * i, &id, 4);
        if ((int)i != skipped) Unit(id, i, (int)i == close ? beside : away);
    }
    Unit(DP_ENTITY, 15, kHere);
    memcpy(g_units + 0x40, &buckets, 8);
    memcpy(g_units + 0x48, &size, 4);
    memcpy(g_units + 0x4C, &empty, 4);
    memcpy(g_units + 0x50, &mult, 4);
    memcpy(g_units + 0x68, &records, 8);
    memcpy(g_types + 16 * 5, &key, 4);
    memcpy(g_types + 0x1A0 + 4, &radius, 4);
    memcpy(g_types + 0x1A0 + 8, &interval, 4);
    memcpy(g_pass + 0x4, &one, 4);
    memcpy(g_pass + 0x20, &one, 4);
    memcpy(g_pass + 0x48, &list, 8);
    memcpy(g_pass + 0x58, &timers, 8);
    memcpy(g_dormant, &key, 4);
    memcpy(g_dormant + 8, &entity, 4);
    g_dormants[0] = (uint64_t)(uintptr_t)g_dormant;
    g_timers[0] = 0.0f;

    Global(PLAYERS_GLOBAL, g_players);
    Global(UNITS_GLOBAL, g_units);
    g_typesAt = (uint64_t)(uintptr_t)g_types;
    Global(TYPES_GLOBAL, (const void*)((uintptr_t)&g_typesAt - TYPES_AT));
    memcpy(g_fake.image + NULL_ID, &none, 4);
    memcpy(g_fake.image + SKIP_ID, &skip, 4);
    memcpy(g_fake.image + COOKIE, &kCookie, 8);
    memcpy(g_fake.image + NO_POSITION, nowhere, 12);
    for (size_t i = 0; i < sizeof kFloats / sizeof kFloats[0]; i++) memcpy(g_fake.image + kFloats[i][0], &kFloats[i][1], 4);
    Put(AVATAR_ID, kAvatarId, sizeof kAvatarId);
    JumpTo(COOKIE_CHECK, (const void*)CookieCheck);

    memset(g_stack, 0xA5, sizeof g_stack);
    memset(&g_cpu, 0, sizeof g_cpu);
    stack[-1] = (uint64_t)(uintptr_t)RunExit;
    g_cpu.gp[RSP] = FRAME - 8;
    g_cpu.gp[RCX] = (uint64_t)(uintptr_t)g_pass;
    memcpy(g_cpu.xmm[1], &dt, 4);
    memset(g_cpu.xmm[11], 0x3C, 16);
    g_cookieKept = -1;
    RunAt(g_fake.image + DORMANCY);
    CHECK(g_cpu.gp[RSP] == FRAME);
    {
        static const uint8_t kRet = 0xC3;
        Put(COOKIE_CHECK, &kRet, 1);
    }
    /* the pass's saved xmm11 lies behind the cookie */
    if (g_cookieKept == 1) for (int i = 0; i < 16; i++) g_cookieKept &= g_cpu.xmm[11][i] == 0x3C;
    return (*(const uint32_t*)(g_dormant + 0x14) & 2u) != 0;
}

/* Every player in turn beside the entity, and none: dormant only with none. `players` is the most the pass has room for. */
static void DormancyCases(uint32_t players)
{
    static const float kFar[3] = {9000.0f, 9000.0f, 9000.0f};
    for (uint32_t n = 0; n <= players; n++)
        for (int close = -1; close < (int)n; close++)
        {
            const int dormant = Dormant(n, close, -1, kFar);
            CHECK(dormant == (close < 0) && g_cookieKept == 1);
            if (dormant != (close < 0) || g_cookieKept != 1) printf("  %u players, %d beside: dormant %d, cookie %d\n", n, close, dormant, g_cookieKept);
        }
    /* a player without an avatar stands at the default position: far away, then at the entity */
    for (uint32_t n = 1; n <= players; n++)
        CHECK(Dormant(n, -1, (int)n - 1, kFar) == 1 && g_cookieKept == 1 && Dormant(n, -1, (int)n - 1, kHere) == 0 && g_cookieKept == 1);
}

#define ENC_COUNTS 9
#define ENC_CALLS 5
static const uint32_t kEncCalls[ENC_CALLS] = {0, 1, 3, 7, 10};

int main(void)
{
    static uint32_t ec[CASES], hp[CASES], search[SCORE_SETS];
    static EncResult enc[ENC_COUNTS][ENC_CALLS];
    const unsigned stack = CO_PLAYERS == 8 ? 43u : 19u;

    Start("the reviewed game");
    LoadGame();
    /* The game's code first, as it is. Cases with an avatar the game cannot read leave that avatar out here. */
    for (int i = 0; i < CASES; i++)
    {
        Avatars(kSets[i], kCounts[i], 0);
        ec[i] = CullSum(EC_PHASE1, EC_SUM, kCandidates[i], NULL);
        hp[i] = CullSum(HP_PHASE1, HP_SUM, kCandidates[i], NULL);
    }
    Avatars(kSets[11], 16, 0);
    for (int i = 0; i < SCORE_SETS; i++) search[i] = SearchAsGame(kScores[i], kScoreCounts[i]);
    /* Up to eight avatars: past that the game's shuffle writes over the stand-in function's saved registers. */
    for (uint32_t n = 0; n < ENC_COUNTS; n++)
        for (int c = 0; c < ENC_CALLS; c++)
        {
            Avatars(kSets[11], n, 0);
            Encounter(kEncCalls[c], 0x0123456789ABCDEFu + n * 31u + (uint64_t)c, &enc[n][c]);
        }
    /* The dormancy pass has room for four players; a fifth's position lies on its stack cookie. */
    DormancyCases(4);
    (void)Dormant(5, 4, -1, kHere);
    CHECK(g_cookieKept == 0);
    CHECK(search[0] == 0 && search[2] == 0 && search[3] == 0 && search[5] == 4 && search[6] == 15 && search[7] == 15);
    CHECK(enc[0][0].calls == 1 && enc[0][0].called[0] == 0xFFFFFFFFu && enc[3][3].calls == 7);

    Init();
    CHECK(co_counts_test_on() == 1 && co_counts_test_stack() == stack);
    {
        static char line[96];
        snprintf(line, sizeof line, "players: %u code sites keep the game's stack arrays for avatars and players inside their room", stack);
        CHECK(fake_host_logged(line));
    }
    /* The fixes for eight avatars only in a build for eight; SC2's cave in place of the guard it replaces. */
    CHECK((g_fake.image[VO_SITE] == 0xE9) == (CO_PLAYERS == 8) && (g_fake.image[DL_SITE + 1] == 0xC0) == (CO_PLAYERS == 8));
    CHECK(g_fake.image[LV_SITE] == 0xE9 && g_fake.image[EC_SITE] == 0xE9 && g_fake.image[ENC_SITE] == 0xE9);
    for (int i = 0; i < CASES; i++)
    {
        int ecClean = 0, hpClean = 0;
        Avatars(kSets[i], kCounts[i], 1);
        const uint32_t ecSum = CullSum(EC_PHASE1, EC_SUM, kCandidates[i], &ecClean), hpSum = CullSum(HP_PHASE1, HP_SUM, kCandidates[i], &hpClean);
        CHECK(ecSum == ec[i] && hpSum == hp[i] && ecClean && hpClean);
        if (ecSum != ec[i] || hpSum != hp[i]) printf("  case %d: %08X %08X, the game %08X %08X\n", i, ecSum, hpSum, ec[i], hp[i]);
    }
    {
        Avatars(kSets[11], 16, 0);
        JumpTo(RA_LOOP, (const void*)RunExit);
        for (int i = 0; i < SCORE_SETS; i++) CHECK(SearchPatched(kScores[i], kScoreCounts[i]) == search[i]);
    }
    for (uint32_t n = 0; n < ENC_COUNTS; n++)
        for (int c = 0; c < ENC_CALLS; c++)
        {
            EncResult got;
            Avatars(kSets[11], n, 0);
            Encounter(kEncCalls[c], 0x0123456789ABCDEFu + n * 31u + (uint64_t)c, &got);
            CHECK(got.calls == enc[n][c].calls && got.state == enc[n][c].state && memcmp(got.called, enc[n][c].called, sizeof got.called) == 0);
        }
    /* Sixteen avatars: every one is called before any twice. */
    {
        EncResult got;
        uint32_t seen = 0;
        Avatars(kSets[11], 16, 0);
        Encounter(16, 42, &got);
        for (uint32_t i = 0; i < 16 && got.calls == 16; i++) seen |= 1u << got.called[i];
        CHECK(got.calls == 16 && seen == 0xFFFFu);
    }
    /* The dormancy pass counts every player of a build for eight. */
    CHECK((g_fake.image[DP_SITE] == 0xE9) == (CO_PLAYERS == 8));
    DormancyCases(CO_PLAYERS);
    /* SC1 fills sixteen entries at rbp-0x70 with the game's pattern; SC2 stores slots 0 to 15 there and drops the rest. */
    {
        uint8_t* frame = (uint8_t*)FRAME;
        JumpTo(SC_FILL_END, (const void*)RunExit);
        JumpTo(SC_STORE_END, (const void*)RunExit);
        memset(g_stack, 0, sizeof g_stack);
        memset(&g_cpu, 0, sizeof g_cpu);
        g_cpu.gp[RSP] = FRAME;
        g_cpu.gp[RBP] = FRAME + 0x100;
        g_cpu.gp[RAX] = 0x3C3C3C3Cu;
        RunAt(g_fake.image + SC_FILL);
        for (uint32_t k = 0; k < 16; k++) CHECK(*(const uint32_t*)(frame + 0x100 - 0x70 + 4 * k) == 0x3C3C3C3Cu);
        CHECK(*(const uint32_t*)(frame + 0x100 - 0x74) == 0 && *(const uint32_t*)(frame + 0x100 - 0x30) == 0 && *(const uint32_t*)(frame + 0x140) == 0);
        for (uint32_t slot = 0; slot < 20; slot++)
        {
            memset(&g_cpu, 0, sizeof g_cpu);
            g_cpu.gp[RSP] = FRAME;
            g_cpu.gp[RBP] = FRAME + 0x100;
            g_cpu.gp[R13] = slot;
            g_cpu.gp[RAX] = 0x900u + slot;
            RunAt(g_fake.image + SC_STORE);
        }
        for (uint32_t k = 0; k < 16; k++) CHECK(*(const uint32_t*)(frame + 0x100 - 0x70 + 4 * k) == 0x900u + k);
        CHECK(*(const uint32_t*)(frame + 0x100 - 0x30) == 0 && *(const uint32_t*)(frame + 0x140) == 0 && *(const uint32_t*)(frame + 0x150) == 0);
    }

    /* A changed byte at a fix refuses the counts with it; a fix of the build for eight is not read in a build for four. */
    {
        static const uint32_t kChanged[] = {EC_SITE + 1, ENC_SITE + 3, SC_STORE + 4, LV_SITE + 2, 0xACB6A7u + 5, DL_SITE + 1, VO_SITE, DP_SITE + 1, 0xA04D46u + 2,
                                            CP_SITE + 1};
        for (size_t i = 0; i < sizeof kChanged / sizeof kChanged[0]; i++)
        {
            const int eightOnly = kChanged[i] == DL_SITE + 1 || kChanged[i] == VO_SITE || kChanged[i] == DP_SITE + 1 || kChanged[i] == 0xA04D46u + 2 ||
                                  kChanged[i] == CP_SITE + 1;
            Start("one changed stack site");
            g_fake.image[kChanged[i]] ^= 0x01;
            Init();
            if (eightOnly && CO_PLAYERS == 4) CHECK(co_counts_test_on() == 1);
            else CHECK(co_counts_test_on() == 0 && fake_host_logged("is not the reviewed one; the session lists count to four as before"));
        }
    }
    /* A failed write at a fix takes back every site written before it. The session step's writes come first. */
    {
        const int moves = (int)(co_session_test_sites() + co_session_test_stub_count());
        static const uint32_t kWrites[] = {0x8C8DF8u, EC_SITE, ENC_SITE, 0xACB9B9u, SC_STORE, 0x1843624u, DL_SITE};
        for (size_t i = 0; i < sizeof kWrites / sizeof kWrites[0]; i++)
        {
            Start("a stack site's write fails");
            const int at = co_counts_test_write_of(kWrites[i]);
            if (kWrites[i] == DL_SITE && CO_PLAYERS == 4) { CHECK(at == -1); continue; }
            CHECK(at >= 0);
            g_fake.failWriteAt = moves + at;
            Init();
            CHECK(co_counts_test_on() == 0 && co_counts_test_reviewed(g_fake.image));
            CHECK(fake_host_logged("session: a count site could not be written; the session lists count to four as before"));
        }
    }

    printf("%s\n", g_failed ? "stack: failures" : "stack: all cases passed");
    return g_failed != 0;
}
