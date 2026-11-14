/* The extra large world's parts that run on every level, where the game's own result differs only past a bound that is
 * level data: the route width runs the game's instructions off a complex level, and the task guard, the stamp jobs, the
 * Hive Lord labels and the scatter set each note when a level passes the bound. The stubs run as code. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestRouteWidthBuild(uint8_t* stub, uintptr_t flag, uintptr_t back);
void CoXlTestScatterSetBuild(uint8_t* stub, uintptr_t count, uintptr_t back);
void CoXlTestTreeClearBuild(uint8_t* stub, uintptr_t count, uintptr_t back);
void CoXlTestLeafClearBuild(uint8_t* stub, uintptr_t count);
void CoXlTestTreeClearNote(volatile LONG* a, volatile LONG* b, volatile LONG* c);
void CoXlTestScatterSetNote(volatile LONG* count);
void CoXlTestTaskNote(volatile LONG* count);
void CoXlTestWorldCurrent(float current);
uint64_t CoXlTestPlaceJob(uint64_t job, uint64_t (*original)(uint64_t, uint64_t, uint64_t, uint64_t));
void CoXlTestHiveLordNote(uint64_t b);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int32_t Rel(const uint8_t* at) { int32_t r; memcpy(&r, at, 4); return r; }
static void Reset(const char* name) { g_case = name; g_coApi = fake_host_reset(CO_GAME_BUILD); g_coMod = (HfMod*)&g_fake; }

/* The route width stub as 0xF53116 enters it: rbx the edge (width at -4, end index at +8), edi the index, xmm6 the
 * pixels per metre. The way on is a `ret` standing for 0xF53120's jae; `above` is its condition (CF clear). */
static float RunRouteWidth(uintptr_t stub, const uint8_t* edge, uint32_t* edi, float scale, uint8_t* above)
{
    float out = 0.0f;
    uint8_t ae = 0;
    __asm__ volatile("movss %[scale], %%xmm6\n\tcall *%[stub]\n\tsetae %[ae]\n\tmovss %%xmm7, %[out]"
        : [out] "=m" (out), [ae] "=m" (ae), "+D" (*edi)
        : [stub] "r" (stub), "b" (edge), [scale] "m" (scale)
        : "xmm6", "xmm7", "cc", "memory");
    *above = ae;
    return out;
}

/* The scatter set's entry as 0x1D49A0 is called: rcx the owner, rdx the cell (x low, y high). `back` (0x1D49A7) is a
 * `ret`. The registers and the flags come back as the stub leaves them there. */
typedef struct { uint64_t rax, rcx, rdx, r8, flagsIn, flagsOut; } SetRun;
static void RunScatterSet(uintptr_t stub, uint64_t owner, int32_t x, int32_t y, int carry, SetRun* run)
{
    uint64_t rax = 0x1122334455667788ull, rcx = owner, rdx = ((uint64_t)(uint32_t)y << 32) | (uint32_t)x;
    register uint64_t r8 __asm__("r8") = 0xFFFFFFFFFFFFFFFFull;
    uint64_t in = 0, out = 0;
    if (carry)
        __asm__ volatile("stc\n\tpushfq\n\tpop %[in]\n\tcall *%[stub]\n\tpushfq\n\tpop %[out]"
            : [in] "=&r" (in), [out] "=&r" (out), "+a" (rax), "+c" (rcx), "+d" (rdx), "+r" (r8) : [stub] "r" (stub) : "cc", "memory");
    else
        __asm__ volatile("xor %%r9d, %%r9d\n\tpushfq\n\tpop %[in]\n\tcall *%[stub]\n\tpushfq\n\tpop %[out]"
            : [in] "=&r" (in), [out] "=&r" (out), "+a" (rax), "+c" (rcx), "+d" (rdx), "+r" (r8) : [stub] "r" (stub) : "r9", "cc", "memory");
    run->rax = rax; run->rcx = rcx; run->rdx = rdx; run->r8 = r8; run->flagsIn = in; run->flagsOut = out;
}

/* The cell clear stub as exe 0x215B59 / 0x215F5D enter it: r10 the owner, ecx its width, rdx its bitset, r9d the bit's
 * index, eax 1. The way back is a `ret`. */
static void RunTreeClear(uintptr_t stub, const uint8_t* owner, uint32_t index)
{
    uint64_t rax = 1, rcx = 0, rdx = 0;
    memcpy(&rcx, owner + 0xA4, 4); memcpy(&rdx, owner + 0x28, 8);
    register uint64_t r8 __asm__("r8") = 0x5555555555555555ull;
    register uint64_t r9 __asm__("r9") = index;
    register uint64_t r10 __asm__("r10") = (uint64_t)(uintptr_t)owner;
    __asm__ volatile("call *%[stub]" : "+a" (rax), "+c" (rcx), "+d" (rdx), "+r" (r8), "+r" (r9), "+r" (r10) : [stub] "r" (stub) : "cc", "memory");
}
/* A scatter owner: width at +0xA4, the bitset's dwords at +0x20 and the bitset at +0x28 (all bits set, a guard dword
 * on each side); `destroyed` as exe 0x1D4150 leaves one: no dwords, no bitset, the width as it was. */
static uint32_t g_bits[2 + 8192];
static void Owner(uint8_t* owner, uint32_t width, uint32_t dwords, int destroyed)
{
    const uint64_t at = destroyed ? 0 : (uint64_t)(uintptr_t)(g_bits + 1);
    if (destroyed) dwords = 0;
    memset(owner, 0, 0xB0); memset(g_bits, 0xFF, sizeof g_bits);
    memcpy(owner + 0xA4, &width, 4); memcpy(owner + 0x20, &dwords, 4); memcpy(owner + 0x28, &at, 8);
}
static int Bit(uint32_t index) { return (int)((g_bits[1 + index / 32] >> (index % 32)) & 1u); }
static uint32_t BitsLeft(uint32_t dwords) { uint32_t n = 0; for (uint32_t i = 0; i < dwords * 32; i++) n += (uint32_t)Bit(i); return n; }

static uint32_t g_jobCount;
static float g_currentDuring;
/* The game's job: it leaves its instance count at +0x54, and a later level init may change the process's scale. */
static uint64_t TestJob(uint64_t a, uint64_t b, uint64_t c, uint64_t job)
{
    memcpy((void*)(uintptr_t)(job + 0x54), &g_jobCount, 4);
    CoXlTestWorldCurrent(g_currentDuring);
    return 7;
}
static int PlaceJob(float current, float during, uint32_t count)
{
    uint8_t job[0x80] = {0}; /* no context (+0x10) and no slice (+0x20): no region, no record walk */
    g_jobCount = count; g_currentDuring = during;
    CoXlTestWorldCurrent(current);
    const uint64_t r = CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
    CoXlTestWorldCurrent(1.0f);
    return (int)r;
}

int main(void)
{
    uint8_t* page = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (page == NULL) { printf("xlbounds: no executable page\n"); return 1; }

    Reset("the route width stub");
    {
        uint8_t* stub = page, *flag = page + 0x100, *back = page + 0x200;
        memset(page, 0xCC, 0x300);
        *back = 0xC3;
        CoXlTestRouteWidthBuild(stub, (uintptr_t)flag, (uintptr_t)back);
        /* movss; cmp byte [flag], 0; je over both mulss; inc; cmp; jmp: the flags are set again before the jae */
        CHECK(memcmp(stub, "\xF3\x0F\x10\x7B\xFC\x80\x3D", 7) == 0 && stub[11] == 0x00 && stub + 12 + Rel(stub + 7) == flag);
        CHECK(stub[12] == 0x74 && 14 + stub[13] == 26 && memcmp(stub + 26, "\xFF\xC7\x3B\x7B\x08\xE9", 6) == 0);
        CHECK(memcmp(stub + 14, "\xF3\x0F\x59\xFE\xF3\x0F\x59\x3D", 8) == 0 && stub + 26 + Rel(stub + 22) == stub + 0x30);
        CHECK(stub + 36 + Rel(stub + 32) == back && Bits(*(float*)(stub + 0x30)) == Bits(0.5f) && stub[36] == 0xCC);

        uint8_t edge[16] = {0};
        const float widths[3] = {7.25f, 1.0e-3f, 123456.7f};
        const uint32_t ends[2] = {5, 4};
        for (int w = 0; w < 3; w++)
            for (int k = 0; k < 2; k++)
            {
                uint8_t above = 0;
                uint32_t edi;
                memcpy(edge, &widths[w], 4);
                memcpy(edge + 12, &ends[k], 4); /* [rbx+8] with rbx = edge + 4 */
                *flag = 0;                       /* not complex: the width bit for bit, whatever xmm6 is */
                edi = 3;
                CHECK(Bits(RunRouteWidth((uintptr_t)stub, edge + 4, &edi, 0.25f, &above)) == Bits(widths[w]));
                CHECK(edi == 4 && above == (4 >= ends[k]));
                edi = 3;
                CHECK(Bits(RunRouteWidth((uintptr_t)stub, edge + 4, &edi, 2.0f, &above)) == Bits(widths[w]));
                *flag = 1;                       /* complex: w x xmm6 / 2 */
                edi = 3;
                CHECK(Bits(RunRouteWidth((uintptr_t)stub, edge + 4, &edi, 0.25f, &above)) == Bits(widths[w] * 0.25f * 0.5f));
                CHECK(edi == 4 && above == (4 >= ends[k]));
                edi = 3;
                CHECK(Bits(RunRouteWidth((uintptr_t)stub, edge + 4, &edi, 2.0f, &above)) == Bits(widths[w]));
            }
    }

    Reset("the scatter set stub");
    {
        uint8_t* stub = page + 0x400, *back = page + 0x500;
        volatile LONG* count = (volatile LONG*)(page + 0x600);
        uint8_t owner[0xB0] = {0};
        const uint32_t w = 512;
        memset(page + 0x400, 0xCC, 0x300);
        *back = 0xC3;
        memcpy(owner + 0xA4, &w, 4);
        CoXlTestScatterSetBuild(stub, (uintptr_t)count, (uintptr_t)back);
        CHECK(stub[0] == 0x9C && stub[31] == 0xF0 && stub + 38 + Rel(stub + 34) == (uint8_t*)count && stub[38] == 0x58 && stub[39] == 0x9D);
        CHECK(memcmp(stub + 40, "\x44\x8B\x81\xA4\x00\x00\x00\xE9", 8) == 0 && stub + 52 + Rel(stub + 48) == back && stub[52] == 0xCC);
        /* the box's edges: x -31 and 30 in, -32 and 31 out; y -32 and 31 in, -33 and 32 out */
        static const struct { int32_t x, y; int counted; } cells[] = {
            {0, 0, 0}, {-31, 0, 0}, {30, 0, 0}, {-32, 0, 1}, {31, 0, 1}, {0, -32, 0}, {0, 31, 0}, {0, -33, 1}, {0, 32, 1},
            {-31, -32, 0}, {30, 31, 0}, {-32, -33, 1}, {31, 32, 1}, {-100000, 0, 1}, {0, 2147483647, 1}, {(int32_t)0x80000000, 0, 1}};
        LONG expected = 0;
        *count = 0;
        for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++)
            for (int carry = 0; carry < 2; carry++)
            {
                SetRun run;
                RunScatterSet((uintptr_t)stub, (uint64_t)(uintptr_t)owner, cells[i].x, cells[i].y, carry, &run);
                expected += cells[i].counted;
                CHECK(*count == expected);
                CHECK(run.r8 == w && run.rcx == (uint64_t)(uintptr_t)owner && run.rax == 0x1122334455667788ull);
                CHECK(run.rdx == (((uint64_t)(uint32_t)cells[i].y << 32) | (uint32_t)cells[i].x));
                CHECK((run.flagsIn & 0x8D5u) == (run.flagsOut & 0x8D5u) && (run.flagsIn & 1u) == (uint64_t)carry);
            }
    }

    Reset("the cell clear stubs: a clear inside the owner's bitset is made, one outside it or of a destroyed owner is skipped and counted");
    {
        uint8_t* stub = page + 0x400, *back = page + 0x500, *leaf = page + 0x440;
        volatile LONG* counts = (volatile LONG*)(page + 0x600);
        #define count counts[0]
        #define leafCount counts[1]
        uint8_t owner[0xB0];
        memset(page + 0x400, 0xCC, 0x300);
        *back = 0xC3;
        count = leafCount = 0;
        CoXlTestTreeClearBuild(stub, (uintptr_t)&count, (uintptr_t)back);
        CoXlTestLeafClearBuild(leaf, (uintptr_t)&leafCount);
        CHECK(stub[36] == 0xE9 && stub + 41 + Rel(stub + 37) == back && stub[41] == 0xF0 && stub + 48 + Rel(stub + 44) == (uint8_t*)&count &&
            stub[48] == 0xE9 && stub + 53 + Rel(stub + 49) == back && stub[53] == 0xCC);
        CHECK(leaf[79] == 0xC3 && leaf[80] == 0xF0 && leaf + 87 + Rel(leaf + 83) == (uint8_t*)&leafCount && leaf[87] == 0xC3 && leaf[88] == 0xCC);
        Owner(owner, 512, 8192, 0); /* the game's own size: w x w bits */
        RunTreeClear((uintptr_t)stub, owner, 0); RunTreeClear((uintptr_t)stub, owner, 37); RunTreeClear((uintptr_t)stub, owner, 262143);
        CHECK(!Bit(0) && !Bit(37) && !Bit(262143) && Bit(1) && Bit(36) && Bit(262142) && BitsLeft(8192) == 262144 - 3 && count == 0);
        RunTreeClear((uintptr_t)stub, owner, 262144); RunTreeClear((uintptr_t)stub, owner, 0xFFFFFFFFu); RunTreeClear((uintptr_t)stub, owner, 0x80000000u);
        CHECK(count == 3 && BitsLeft(8192) == 262144 - 3 && g_bits[0] == 0xFFFFFFFFu && g_bits[8193] == 0xFFFFFFFFu);
        Owner(owner, 512, 4, 0); /* fewer dwords than the width asks for: the dwords bound, not w x w */
        RunTreeClear((uintptr_t)stub, owner, 127); RunTreeClear((uintptr_t)stub, owner, 128); RunTreeClear((uintptr_t)stub, owner, 5000);
        CHECK(count == 5 && !Bit(127) && Bit(128) && Bit(5000) && BitsLeft(8192) == 262144 - 1);
        Owner(owner, 512, 8192, 1); /* destroyed: a null bitset, which the game's code wrote through */
        RunTreeClear((uintptr_t)stub, owner, 0); RunTreeClear((uintptr_t)stub, owner, 0x36B8u * 8); RunTreeClear((uintptr_t)stub, owner, 262143);
        CHECK(count == 8);

        typedef void (*LeafFn)(const uint8_t* owner, uint64_t cell);
        #define CELL(x, y) (((uint64_t)(uint32_t)(y) << 32) | (uint32_t)(x))
        Owner(owner, 8, 2, 0); /* 8 x 8 cells: bit (x + 4) * 8 + y */
        ((LeafFn)leaf)(owner, CELL(-4, 0)); ((LeafFn)leaf)(owner, CELL(3, 7)); ((LeafFn)leaf)(owner, CELL(0, 3));
        CHECK(!Bit(0) && !Bit(63) && !Bit(35) && BitsLeft(2) == 61 && leafCount == 0);
        ((LeafFn)leaf)(owner, CELL(4, 0)); ((LeafFn)leaf)(owner, CELL(-5, 0)); ((LeafFn)leaf)(owner, CELL(-100000, 7));
        CHECK(leafCount == 3 && BitsLeft(2) == 61 && Bit(64) && g_bits[0] == 0xFFFFFFFFu);
        Owner(owner, 8, 2, 1);
        ((LeafFn)leaf)(owner, CELL(0, 0)); ((LeafFn)leaf)(owner, CELL(-4, 0));
        CHECK(leafCount == 5);
        #undef CELL
        #undef count
        #undef leafCount

        static volatile LONG a, b, c;
        CoXlTestTreeClearNote(&a, &b, &c);
        CHECK(!fake_host_logged("cell clears outside the owner's bitset"));
        a = 2; c = 1;
        CoXlTestTreeClearNote(&a, &b, &c);
        CHECK(fake_host_logged("difficulty: scatter: cell clears outside the owner's bitset skipped (a destroyed owner's has no cells; the write would go through null): "
            "2 removes, 0 re-owns, 1 leaf clears so far"));
        Reset("the cell clears' note, unchanged");
        CoXlTestTreeClearNote(&a, &b, &c);
        CHECK(!fake_host_logged("cell clears outside"));
        b = 4;
        CoXlTestTreeClearNote(&a, &b, &c);
        CHECK(fake_host_logged("2 removes, 4 re-owns, 1 leaf clears so far"));
    }

    Reset("the scatter set's note");
    {
        volatile LONG n = 0;
        CoXlTestScatterSetNote(&n);
        CHECK(!fake_host_logged("owner cell sets outside the game's bitset box"));
        n = 3;
        CoXlTestScatterSetNote(&n);
        CHECK(fake_host_logged("difficulty: scatter: owner cell sets outside the game's bitset box (x -31..30, y -32..31): 3 so far"));
        Reset("the scatter set's note, unchanged");
        CoXlTestScatterSetNote(&n);
        CHECK(!fake_host_logged("owner cell sets outside"));
    }

    Reset("the task guard's note");
    {
        volatile LONG n = 0;
        CoXlTestTaskNote(&n);
        CHECK(!fake_host_logged("AI guard points left out"));
        n = 2;
        CoXlTestTaskNote(&n);
        CHECK(fake_host_logged("difficulty: extra large maps: AI guard points left out at mission load (1792 of 2048 tasks in use): 2"));
        Reset("the task guard's note, unchanged");
        CoXlTestTaskNote(&n);
        CHECK(!fake_host_logged("AI guard points left out"));
        n = 5;
        CoXlTestTaskNote(&n);
        CHECK(fake_host_logged("tasks in use): 5"));
    }

    Reset("a stamp job at the game's bound");
    CHECK(PlaceJob(1.0f, 1.0f, 0xC00u) == 7 && !fake_host_logged("past the game's 3072"));
    Reset("a stamp job past the game's bound");
    CHECK(PlaceJob(1.0f, 1.0f, 0xC01u) == 7 && fake_host_logged("difficulty: extra large maps: op 0x2B job past the game's 3072 instances on a normal level: 3073"));
    Reset("a complex level's stamp job");
    CHECK(PlaceJob(8.0f, 8.0f, 0x5000u) == 7 && !fake_host_logged("past the game's 3072"));
    Reset("a normal job while a later init sets a complex level");
    CHECK(PlaceJob(1.0f, 8.0f, 0x5000u) == 7 && fake_host_logged("on a normal level: 20480"));
    Reset("a complex job while a later init sets a normal level");
    CHECK(PlaceJob(8.0f, 1.0f, 0x5000u) == 7 && !fake_host_logged("past the game's 3072"));

    Reset("the Hive Lord graph");
    {
        static uint8_t context[0xF730], graph[0x30];
        uint32_t sizes[255] = {0};
        uint64_t b[4] = {0}, at;
        at = (uint64_t)(uintptr_t)context; memcpy(&b[3], &at, 8);
        at = (uint64_t)(uintptr_t)graph; memcpy(context + 0xF728, &at, 8);
        at = (uint64_t)(uintptr_t)sizes; memcpy(graph + 0x20, &at, 8);
        sizes[1] = 40; sizes[254] = 3;
        CoXlTestHiveLordNote((uint64_t)(uintptr_t)b);
        CHECK(!fake_host_logged("Hive Lord graph"));
        sizes[0] = 9;
        CoXlTestHiveLordNote((uint64_t)(uintptr_t)b);
        CHECK(fake_host_logged("difficulty: extra large maps: Hive Lord graph past 255 regions on a normal level: 9 nodes with label 0"));
        Reset("the Hive Lord graph, none");
        memset(context + 0xF728, 0, 8);
        CoXlTestHiveLordNote((uint64_t)(uintptr_t)b);
        CHECK(!fake_host_logged("Hive Lord graph"));
    }

    VirtualFree(page, 0, MEM_RELEASE);
    if (g_failed) { printf("xlbounds: %d checks failed\n", g_failed); return 1; }
    printf("xlbounds: all cases passed\n");
    return 0;
}
