/* A tile built in mission: op 0x2F (game 0xF44380) in budgeted slices (proto/difficulty_world_move.c MoveRun2F). Phase
 * A's batch bound (0x3FF at 0xF443FC) is set per call from the last call's time and phase B's gate (0xF4470C) jumps to
 * the "not done" exit while the game's code runs; phase B then runs as the code has it, a slice a frame. The test's
 * dispatcher stands for the case: it reads the bound and the gate from the image as the code would, and its own phase
 * B is a transcription of 0xF44712..0xF4484A. Every engine call is logged: the sliced run's calls, arguments and order
 * are the whole run's; the bytes are the game's again after each call. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMove2FBegin(const uint8_t* record, const uint8_t* ctx, void* dispatch);
int CoXlTestMove2FRun(void);
uint32_t CoXlTestMove2FBatch(void);
const uint8_t* CoXlTestMove2FRecord(void);
void CoXlTestMove2FPins(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define COUNT 3000u
static uint8_t* g_image;
static uint8_t g_locs[COUNT * 0xE0], g_ctx[0xE8], g_record[0x19B0];
static uint32_t g_count = COUNT, g_table[COUNT * 3];

/* the engine calls in order: kind, then up to three values (the float as its bits) */
typedef struct { int kind; uint64_t a, b, c; } Call;
static Call g_log[200000];
static int g_logN;
static void Log(int kind, uint64_t a, uint64_t b, uint64_t c) { if (g_logN < 200000) { g_log[g_logN].kind = kind; g_log[g_logN].a = a; g_log[g_logN].b = b; g_log[g_logN].c = c; } g_logN++; }
static void Spin(double us) { LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0); do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1e6 / (double)hz.QuadPart < us); }

static int g_worldOff; /* calls of the world check that say no first */
static uint64_t WorldCheck(uint64_t world) { Log(1, world, 0, 0); if (g_worldOff > 0) { g_worldOff--; return 0; } return 1; }
static uint32_t UnitId(uint64_t unit, uint32_t index) { Log(2, unit, index, 0); Spin(3.0); return index % 4 == 0 ? 0 : index + 1000; }
static char Live(uint32_t id) { Log(3, id, 0, 0); return (char)(id % 2 == 1); }
static void Touch(uint32_t id) { Log(4, id, 0, 0); }
static uint8_t g_proxy[0x40], g_flagsAt[0x40];
static uint64_t Obj(uint32_t id, uint32_t zero) { Log(5, id, zero, 0); return id == 1011 ? (uint64_t)(uintptr_t)g_proxy : (uint64_t)id * 16; }
static void SetIndex(uint64_t obj, uint32_t index) { Log(6, obj, index, 0); }
static uint64_t Mat(uint64_t obj, uint32_t hash) { Log(7, obj, hash, 0); return obj + 1; }
static void Scalar(uint64_t mat, uint32_t hash, float v) { uint32_t bits; memcpy(&bits, &v, 4); Log(8, mat, hash, bits); }

/* 0xF44712..0xF4484A transcribed (the reference) */
static int ReferencePhaseB(uint64_t pass, uint64_t context)
{
    (void)pass;
    uint64_t world = 0; memcpy(&world, (void*)(uintptr_t)(context + 0x40), 8);
    if (WorldCheck(world) == 0) return 1;
    for (uint32_t i = 0; i < g_count; i++)
    {
        const uint32_t* e = g_table + i * 3;
        if (e[0] == 0xFFFFFFFFu) continue;
        uint32_t n = e[1], base = e[2], id = 0, j = 0;
        if (n == 0) continue;
        uint64_t unit; memcpy(&unit, g_locs + (size_t)i * 0xE0 + 0xA8, 8);
        for (; j < n; j++) { id = UnitId(unit, j + base); if (id != 0 && Live(id)) break; }
        if (j == n) continue;
        Touch(id);
        uint64_t obj = Obj(id, 0);
        SetIndex(obj, i + 1);
        Scalar(Mat(obj, 0xF2760503u), 0xE0D994FAu, 0.0f);
    }
    return 0;
}

/* the case (0xC158AA): phase A up to the bound read from the image, in location order; the gate as the image has it */
static int g_calls, g_badBound;
static uint32_t g_lastBound;
static uint64_t Dispatch(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    (void)d; (void)f; (void)g; (void)h;
    uint64_t pass = record + 8;
    uint32_t bound = 0, idx = 0;
    memcpy(&bound, g_image + 0xF443FCu, 4);
    memcpy(&idx, (void*)(uintptr_t)(pass + 0x48), 4);
    const int gate = g_image[0xF4470Cu] == 0x0F && g_image[0xF4470Du] == 0x85;
    g_lastBound = bound; g_calls++;
    *(uint8_t*)(uintptr_t)phase = 1;
    uint32_t end = idx + bound < g_count ? idx + bound : g_count;
    for (uint32_t i = idx; i < end; i++)
    {
        Log(0, i, 0, 0); Spin(20.0);
        g_table[i * 3] = i % 5 == 0 ? 0xFFFFFFFFu : 1; g_table[i * 3 + 1] = i % 3 + 1; g_table[i * 3 + 2] = i * 10;
    }
    memcpy((void*)(uintptr_t)(pass + 0x48), &end, 4);
    if (end != g_count) return 0;
    if (!gate) return 0; /* the jump: phase B left for the mod */
    if (ReferencePhaseB(pass, context) == 0) *(volatile uint32_t*)(uintptr_t)index = 1;
    return 0;
}

static void Record(void)
{
    memset(g_record, 0, sizeof g_record); memset(g_table, 0, sizeof g_table);
    g_record[0] = 0x2F;
    uint64_t v = (uint64_t)(uintptr_t)g_locs; memcpy(g_record + 8 + 0x08, &v, 8);
    v = (uint64_t)(uintptr_t)&g_count; memcpy(g_record + 8 + 0x28, &v, 8);
    v = (uint64_t)(uintptr_t)g_table; memcpy(g_record + 8 + 0x50, &v, 8);
}
static void GameBytes(void)
{
    static const uint8_t bound[7] = {0x41, 0x8D, 0x8B, 0xFF, 0x03, 0x00, 0x00}, gate[6] = {0x0F, 0x85, 0x3A, 0x01, 0x00, 0x00};
    DWORD old; VirtualProtect(g_image + 0xF44000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_image + 0xF443F9u, bound, 7); memcpy(g_image + 0xF4470Cu, gate, 6);
}
static int Restored(void) { static const uint8_t bound[7] = {0x41, 0x8D, 0x8B, 0xFF, 0x03, 0x00, 0x00}, gate[6] = {0x0F, 0x85, 0x3A, 0x01, 0x00, 0x00};
    return memcmp(g_image + 0xF443F9u, bound, 7) == 0 && memcmp(g_image + 0xF4470Cu, gate, 6) == 0; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    CoXlTestGame((uintptr_t)g_image);
    for (uint32_t i = 0; i < COUNT; i++) { uint64_t unit = 0x5000 + i * 7; memcpy(g_locs + (size_t)i * 0xE0 + 0xA8, &unit, 8); }
    uint64_t world = 0xAB0; memcpy(g_ctx + 0x40, &world, 8);
    /* the engine table [game+0x3326308]: +0x08, +0x18, +0x28, +0x50, +0x110 */
    static uint64_t api[0x30], t8[0x10], t18[0x100], t28[2], t50[0x10], t110[4];
    t8[0x48 / 8] = (uint64_t)(uintptr_t)WorldCheck; t50[0x30 / 8] = (uint64_t)(uintptr_t)UnitId;
    t18[0x720 / 8] = (uint64_t)(uintptr_t)Live; t18[0x450 / 8] = (uint64_t)(uintptr_t)Touch; t18[0x460 / 8] = (uint64_t)(uintptr_t)Obj;
    t110[0x18 / 8] = (uint64_t)(uintptr_t)SetIndex; t110[0x10 / 8] = (uint64_t)(uintptr_t)Mat; t28[0] = (uint64_t)(uintptr_t)Scalar;
    api[1] = (uint64_t)(uintptr_t)t8; api[3] = (uint64_t)(uintptr_t)t18; api[5] = (uint64_t)(uintptr_t)t28; api[10] = (uint64_t)(uintptr_t)t50; api[0x22] = (uint64_t)(uintptr_t)t110;
    uint64_t apiAt = (uint64_t)(uintptr_t)api; memcpy(g_image + 0x3326308u, &apiAt, 8);
    GameBytes();
    { uint64_t at = (uint64_t)(uintptr_t)g_flagsAt; memcpy(g_proxy + 0x38, &at, 8); uint32_t flags = 0x83; memcpy(g_flagsAt + 0x20, &flags, 4); }

    g_case = "the reference: the game's call whole (phase B in the same call), its calls logged";
    Record();
    g_logN = 0;
    { uint32_t zero = 0; memcpy(g_record + 8 + 0x48, &zero, 4); }
    uint8_t phase = 0; volatile uint32_t index = 0;
    while (index == 0) Dispatch((uint64_t)(uintptr_t)g_record, (uint64_t)(uintptr_t)g_ctx, (uint64_t)(uintptr_t)&index, 1, (uint64_t)(uintptr_t)&phase, 0, 0, 0);
    static Call ref[200000];
    const int refN = g_logN;
    memcpy(ref, g_log, sizeof(Call) * (size_t)(refN < 200000 ? refN : 200000));
    CHECK(refN > COUNT && refN < 200000);

    g_case = "in slices: batches from the last call's time, phase B a slice a frame; the same calls, arguments and order";
    Record();
    g_logN = 0; g_calls = 0; g_badBound = 0; g_worldOff = 2;
    CoXlTestMove2FBegin(g_record, g_ctx, (void*)Dispatch);
    int frames = 0, r = 0, phaseA = 0, slicesB = 0, restored = 1, boundOk = 1;
    while ((r = CoXlTestMove2FRun()) == 0 && frames < 100000)
    {
        frames++;
        restored &= Restored();
        if (g_lastBound == 0 || g_lastBound > 1023) boundOk = 0;
        uint32_t idx = 0; memcpy(&idx, CoXlTestMove2FRecord() + 8 + 0x48, 4);
        if (idx < COUNT) phaseA++; else slicesB++;
    }
    CHECK(r == 1 && restored && boundOk && Restored());
    CHECK(g_calls >= 2 && phaseA >= 2); /* 3000 locations at 20 us: a 3 ms batch is ~150 */
    CHECK(g_lastBound < 1023 && CoXlTestMove2FBatch() >= 1 && CoXlTestMove2FBatch() <= 1023);
    CHECK(slicesB >= 4); /* two world checks that said no, then more than one slice */
    /* the world check: twice more here (it said no twice); every other call the same, in the same order */
    {
        int same = 1, k = 0, m = 0, checksRef = 0, checks = 0;
        for (;; k++, m++)
        {
            while (k < refN && ref[k].kind == 1) { k++; checksRef++; }
            while (m < g_logN && g_log[m].kind == 1) { m++; checks++; }
            if (k >= refN || m >= g_logN) break;
            if (ref[k].kind != g_log[m].kind || ref[k].a != g_log[m].a || ref[k].b != g_log[m].b || ref[k].c != g_log[m].c) { same = 0; break; }
        }
        CHECK(same && k >= refN && m >= g_logN && checksRef == 1 && checks == 3);
    }
    CHECK(fake_host_logged("op 0x2F in") && fake_host_logged("for 3000 locations"));
    CHECK(fake_host_logged("the first stamp terrain's render flags 0x83 (bits 0/1 1/1, bit 7 1)"));

    g_case = "in slices with the world there at once: the calls the reference's to the one";
    Record();
    g_logN = 0; g_calls = 0; g_worldOff = 0;
    CoXlTestMove2FBegin(g_record, g_ctx, (void*)Dispatch);
    frames = 0; slicesB = 0;
    while ((r = CoXlTestMove2FRun()) == 0 && frames < 100000)
    {
        frames++;
        uint32_t idx = 0; memcpy(&idx, CoXlTestMove2FRecord() + 8 + 0x48, 4);
        if (idx >= COUNT) slicesB++;
    }
    CHECK(r == 1 && Restored() && g_logN == refN && slicesB >= 2);
    {
        int same = g_logN == refN;
        for (int k = 0; same && k < refN; k++)
            if (ref[k].kind != g_log[k].kind || ref[k].a != g_log[k].a || ref[k].b != g_log[k].b || ref[k].c != g_log[k].c) same = 0;
        CHECK(same);
    }

    g_case = "not the reviewed code: the game's call whole, a call a frame, the bytes untouched";
    g_fake.strictCode = 1;
    CoXlTestMove2FPins();
    Record();
    g_logN = 0; g_calls = 0;
    CoXlTestMove2FBegin(g_record, g_ctx, (void*)Dispatch);
    frames = 0;
    while ((r = CoXlTestMove2FRun()) == 0 && frames < 100) frames++;
    CHECK(r == 1 && g_lastBound == 0x3FF && g_calls == 3 && Restored() && g_logN == refN);
    CHECK(fake_host_logged("op 0x2F is not the reviewed code"));
    g_fake.strictCode = 0;

    if (g_failed) { printf("stamps2f: %d check(s) failed\n", g_failed); return 1; }
    printf("stamps2f: all checks passed\n");
    return 0;
}
