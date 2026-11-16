/* A tile's op 0x41 setup (game 0xF4F9E0) in play: its seed lookups (0xFC7900) timed from the setup's four direct call
 * sites (0xF5036D, 0xF503CA, 0xF50552, 0xF5059E), which point at a stub for the setup's one call only
 * (proto/difficulty_world_terrain.c NearSeedPatch). The stub must hand every register back as the lookup left it, the
 * sites must be the game's again after the call, the job calls and a build not in play are not timed, and sites that are
 * not the reviewed call are left alone. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
uint8_t* CoXlTestNearMat41(int s, const uint8_t* record, uint8_t phase);
void CoXlTestMatOff(void);
int CoXlTestNearRunStep(int s, uint64_t context, void* dispatch);
void CoXlTestMoveBuilding(int on);
int CoXlTestSeed(uint32_t* calls, double* lookups, double* setup, int64_t* total);
void CoXlTestMatUnitsNoteNow(void);
void CoXlTestNearJobLevel(uintptr_t level, uint8_t* block);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const uint32_t kSites[4] = {0xF5036Du, 0xF503CAu, 0xF50552u, 0xF5059Eu};
#define LOOKUP 0xFC7900u
#define PROBE 0xF51000u

/* The lookup: eax from the point and rcx (which it never writes), a spin on r10 (0 after, as 0xFC7900 leaves it), the
 * other volatile registers set, a store to its own home area. */
static const uint8_t kLookup[] = {
    0x8B, 0x02,                         /* mov eax, [rdx] */
    0x01, 0xC8,                         /* add eax, ecx */
    0x41, 0xBA, 0x00, 0x2D, 0x31, 0x01, /* mov r10d, 20000000 */
    0x41, 0xFF, 0xCA,                   /* dec r10d */
    0x75, 0xFB,                         /* jnz dec */
    0xBA, 0x11, 0x11, 0x11, 0x11,       /* mov edx, 0x11111111 */
    0x41, 0xB8, 0x22, 0x22, 0x22, 0x22, /* mov r8d, 0x22222222 */
    0x41, 0xB9, 0x33, 0x33, 0x33, 0x33, /* mov r9d, 0x33333333 */
    0x41, 0xBB, 0x55, 0x55, 0x55, 0x55, /* mov r11d, 0x55555555 */
    0x48, 0x89, 0x44, 0x24, 0x08,       /* mov [rsp+8], rax */
    0xC3};
/* Probe(out, point, frame): calls the site's frame (sub rsp, 0x28; the site's E8 to the lookup; add rsp, 0x28; ret: a
 * caller with its own home area, as the setup is) with rcx 0x12345678 and r10 preset, then stores rax, rcx, rdx, r8,
 * r9, r10, r11 as the call left them. The lookup is entered with rsp 8 modulo 16, as from a caller. */
static const uint8_t kProbe[] = {
    0x53,                               /* push rbx */
    0x48, 0x89, 0xCB,                   /* mov rbx, rcx */
    0x4C, 0x89, 0xC0,                   /* mov rax, r8 */
    0xB9, 0x78, 0x56, 0x34, 0x12,       /* mov ecx, 0x12345678 */
    0x49, 0xBA, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, /* mov r10, 0x7777777777777777 */
    0x48, 0x83, 0xEC, 0x20,             /* sub rsp, 0x20 */
    0xFF, 0xD0,                         /* call rax */
    0x48, 0x83, 0xC4, 0x20,             /* add rsp, 0x20 */
    0x48, 0x89, 0x03,                   /* mov [rbx], rax */
    0x48, 0x89, 0x4B, 0x08,             /* mov [rbx+8], rcx */
    0x48, 0x89, 0x53, 0x10,             /* mov [rbx+0x10], rdx */
    0x4C, 0x89, 0x43, 0x18,             /* mov [rbx+0x18], r8 */
    0x4C, 0x89, 0x4B, 0x20,             /* mov [rbx+0x20], r9 */
    0x4C, 0x89, 0x53, 0x28,             /* mov [rbx+0x28], r10 */
    0x4C, 0x89, 0x5B, 0x30,             /* mov [rbx+0x30], r11 */
    0x5B,                               /* pop rbx */
    0xC3};

static uint8_t g_record[0x19B0], g_ctx[0xE8], g_lg[0x200], g_list[0x2008];
static uint64_t g_direct[4][7], g_setupSeen[4][7], g_jobSeen[4][7];
static uint32_t g_point = 1000;
static int g_dispatches;
static void (*g_probe)(uint64_t* out, const uint32_t* point, uintptr_t site);

static void Spin(double us) { LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0); do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1e6 / (double)hz.QuadPart < us); }
static void ThroughSites(uint64_t out[4][7]) { for (int k = 0; k < 4; k++) { memset(out[k], 0, 56); g_probe(out[k], &g_point, (uintptr_t)(g_fake.image + kSites[k] - 4)); } }

/* The dispatcher as 0xC165F0 runs op 0x41: phase 0 the setup (here 3 ms of its own and a lookup through each site),
 * phase 1 after; a job's call goes through the sites too. */
static uint64_t Dispatch41(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    (void)record; (void)context; (void)index; (void)d; (void)f; (void)g; (void)h;
    g_dispatches++;
    if (*(uint8_t*)(uintptr_t)phase == 0) { Spin(3000.0); ThroughSites(g_setupSeen); *(uint8_t*)(uintptr_t)phase = 1; return 0; }
    ThroughSites(g_jobSeen);
    return 0;
}

static void Record(void)
{
    memset(g_record, 0, sizeof g_record); memset(g_list, 0, sizeof g_list);
    uint64_t v = (uint64_t)(uintptr_t)g_lg; memcpy(g_record + 8, &v, 8);
    const uint32_t t = 1; memcpy(g_lg + 0x124, &t, 4);
    v = (uint64_t)(uintptr_t)g_list; memcpy(g_record + 8 + 0x140, &v, 8);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    uint8_t* image = g_fake.image;
    DWORD old = 0;
    VirtualProtect(image + 0xF50000u, 0x2000, PAGE_EXECUTE_READWRITE, &old);
    VirtualProtect(image + 0xFC7000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    memcpy(image + LOOKUP, kLookup, sizeof kLookup);
    memcpy(image + PROBE, kProbe, sizeof kProbe);
    for (int k = 0; k < 4; k++)
    {
        uint8_t* site = image + kSites[k];
        const int32_t rel = (int32_t)(LOOKUP - (kSites[k] + 5));
        static const uint8_t enter[4] = {0x48, 0x83, 0xEC, 0x28}, leave[5] = {0x48, 0x83, 0xC4, 0x28, 0xC3};
        memcpy(site - 4, enter, 4); site[0] = 0xE8; memcpy(site + 1, &rel, 4); memcpy(site + 5, leave, 5);
    }
    FlushInstructionCache(GetCurrentProcess(), image + 0xF50000u, 0x2000);
    g_probe = (void (*)(uint64_t*, const uint32_t*, uintptr_t))(void*)(image + PROBE);
    uint8_t sites[4][5];
    for (int k = 0; k < 4; k++) memcpy(sites[k], image + kSites[k], 5);
    const uint64_t ctx = (uint64_t)(uintptr_t)g_ctx;
    static uint8_t level[0x400], gen[0x10]; /* the step reads the level's generation ([level+0x2C8]) first */
    { uint64_t v = (uint64_t)(uintptr_t)gen; memcpy(level + 0x2C8, &v, 8); CoXlTestNearJobLevel((uintptr_t)level, NULL); }
    uint32_t calls = 0; double lookups = 0, setup = 0; int64_t total = 0;

    g_case = "the lookups called straight: what every register holds after the call";
    LARGE_INTEGER hz, a, b; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&a);
    ThroughSites(g_direct);
    QueryPerformanceCounter(&b);
    const double direct = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)hz.QuadPart;
    CHECK(g_direct[0][0] == 1000u + 0x12345678u && g_direct[0][1] == 0x12345678u && g_direct[0][2] == 0x11111111u && g_direct[0][5] == 0);
    CHECK(direct > 0.2); /* the spin is long enough to time */

    g_case = "the setup in play: the lookups through the stub, every register as the lookup left it, four calls, their ms "
             "inside the setup's, the sites the game's again";
    CoXlTestMoveBuilding(1);
    Record();
    CoXlTestNearMat41(4, g_record, 0);
    g_dispatches = 0;
    CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 1);
    CHECK(memcmp(g_setupSeen, g_direct, sizeof g_direct) == 0);
    CHECK(CoXlTestSeed(&calls, &lookups, &setup, &total) == 1 && calls == 4 && total == 4);
    CHECK(setup >= 3.0 && lookups > 0.25 * direct && lookups <= setup - 2.5); /* the setup's own 3 ms are not the lookups' */
    for (int k = 0; k < 4; k++) CHECK(memcmp(image + kSites[k], sites[k], 5) == 0);
    g_fake.log[0] = 0;
    CoXlTestMatUnitsNoteNow();
    CHECK(fake_host_logged("op 0x41's setup: its seed lookups (game 0xFC7900) 4 calls, "));
    { char want[96]; snprintf(want, sizeof want, "4 calls, %.1f ms of its %.1f ms (", lookups, setup); CHECK(fake_host_logged(want)); }

    g_case = "a job's call (phase 1): through the game's sites, not timed";
    CoXlTestNearMat41(4, g_record, 1);
    CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 2);
    CHECK(memcmp(g_jobSeen, g_direct, sizeof g_direct) == 0);
    CHECK(CoXlTestSeed(&calls, &lookups, &setup, &total) == 0 && calls == 0 && total == 4);
    CoXlTestMatOff();

    g_case = "not in play (no build moving): the setup not timed, no line";
    CoXlTestMoveBuilding(0);
    CoXlTestNearMat41(4, g_record, 0);
    CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 3);
    CHECK(memcmp(g_setupSeen, g_direct, sizeof g_direct) == 0);
    CHECK(CoXlTestSeed(&calls, &lookups, &setup, &total) == 0 && total == 4);
    g_fake.log[0] = 0;
    CoXlTestMatUnitsNoteNow();
    CHECK(!fake_host_logged("seed lookups"));
    CoXlTestMatOff();

    g_case = "a site that is not the reviewed call (E8 to 0xFC7900): no site written, the line says why";
    CoXlTestMoveBuilding(1);
    {
        uint8_t* site = image + kSites[2];
        const int32_t elsewhere = (int32_t)(LOOKUP + 0x40 - (kSites[2] + 5));
        memcpy(site + 1, &elsewhere, 4);
        memcpy(image + LOOKUP + 0x40, kLookup, sizeof kLookup);
        uint8_t now[4][5];
        for (int k = 0; k < 4; k++) memcpy(now[k], image + kSites[k], 5);
        CoXlTestNearMat41(4, g_record, 0);
        CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0 && g_dispatches == 4);
        CHECK(CoXlTestSeed(&calls, &lookups, &setup, &total) < 0 && calls == 0 && total == 4);
        for (int k = 0; k < 4; k++) CHECK(memcmp(image + kSites[k], now[k], 5) == 0);
        g_fake.log[0] = 0;
        CoXlTestMatUnitsNoteNow();
        CHECK(fake_host_logged("op 0x41's setup: its seed lookups not timed (its call sites are not the reviewed ones)"));
        memcpy(site, sites[2], 5);
        CoXlTestMatOff();
    }

    g_case = "the sites reviewed again: timed again";
    CoXlTestNearMat41(4, g_record, 0);
    CHECK(CoXlTestNearRunStep(4, ctx, (void*)Dispatch41) == 0);
    CHECK(CoXlTestSeed(&calls, &lookups, &setup, &total) == 1 && calls == 4 && total == 8);
    CHECK(memcmp(g_setupSeen, g_direct, sizeof g_direct) == 0);
    CoXlTestMatOff();

    if (g_failed) { printf("seed41: %d check(s) failed\n", g_failed); return 1; }
    printf("seed41: all checks passed\n");
    return 0;
}
