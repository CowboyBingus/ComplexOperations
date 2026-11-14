/* The roads' build (proto/difficulty_world_roadbuild.c): the point spacing op 0x0E's rule needs, the stub that gives
 * it to the level's own pass and the game's 5.0 to every other, and the arterial choice and its widths over a fake
 * generation block. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestGenLevel(float current);
uint32_t CoXlTestRoadsLeftOut(const uint8_t* recs, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, float spacing, uint32_t* total);
float CoXlTestRoadsSpacing(const uint8_t* recs, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, uint32_t* total, uint32_t* left);
void CoXlTestRoadSpacingBuild(uint8_t* stub, uintptr_t constant, uintptr_t back);
void CoXlTestRoadSpacingAt(uint8_t* stub);
const char* CoXlTestInstallRoadSpacing(uintptr_t game, uint8_t** stub);
void CoXlTestRoadsSpacingBegin(uintptr_t block, uint64_t record);
void CoXlTestRoadsSpacingEnd(void);
uint32_t CoXlTestRoadsArterials(const uint8_t* recs, uint32_t edges, const uint32_t* ends, const float* nodeXY, uint32_t nodes, const float* termXY, uint32_t terms,
    uint8_t* chosen, uint32_t* unreached);
void CoXlTestRoadsArterialsApply(uintptr_t block, const uint8_t* ops, uint32_t total);
void CoXlTestRoadsArterialsPoints(uintptr_t block);
void CoXlTestRoadsSpursBuild(uintptr_t block, uint64_t context, void (*capsule)(uint64_t, const float*, uint32_t));
uint32_t CoXlTestRoadsSpurs(uint32_t* reserve, const float** table);
float CoXlTestStampSegmentBox(const float* s, const float* lo, const float* hi);
void CoXlTestRoadsSpurTerminals(const float* xy, const float* gap, uint32_t terms);
int CoXlTestRoadsArterial(uint32_t edge);
void CoXlTestRoadsCorridor(uintptr_t block, const uint8_t* ops, uint32_t before, uint32_t total);
uint32_t CoXlTestStampCandidateSize(void);
void CoXlTestRoadsStamps(uint32_t first, void* cand, uint32_t planned);
void CoXlTestRoadsSlope(const float* h, uint32_t cols, uint32_t rows, float step, float water, const float* pts, uint32_t n, double* out);
void CoXlTestRoadsGround(float* h, uint32_t cols, uint32_t rows, int bodies, float water);
void CoXlTestRoadsSlopeNote(uintptr_t block);
void CoXlTestRoadsIndexNote(uint64_t context, const char* when);
void CoXlTestRoadsPointsWatch(uintptr_t block, uint32_t op);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t g_recs[64 * 0x48];
static void Edge(uint32_t i, float ax, float ay, float bx, float by, float width, uint8_t type, uint32_t first, uint32_t end, uint16_t flags)
{
    uint8_t* r = g_recs + 0x48u * i;
    memset(r, 0, 0x48);
    memcpy(r, &ax, 4); memcpy(r + 4, &ay, 4); memcpy(r + 8, &bx, 4); memcpy(r + 12, &by, 4);
    memcpy(r + 0x30, &width, 4); r[0x34] = type; memcpy(r + 0x38, &first, 4); memcpy(r + 0x3C, &end, 4); memcpy(r + 0x44, &flags, 2);
}
static uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at += strlen(text)) n++;
    return n;
}

static void Spacing(void)
{
    uint32_t total = 0, left = 0;
    g_case = "spacing: n = chord / spacing + 1 truncated, n + 1 points, n + 2 places asked";
    Edge(0, 0, 0, 100, 0, 2, 3, 0, 0, 0);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 10, 33, 5.0f, &total) == 0 && total == 32); /* n 21: 23 free asked, 22 used */
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 10, 32, 5.0f, &total) == 1 && total == 10);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 40, 32, 5.0f, &total) == 1 && total == 40); /* more used than places */
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 100, 8.0f, &total) == 0 && total == 14); /* 12.5 + 1 -> 13 */
    Edge(0, 0, 0, 3, 4, 2, 3, 0, 0, 0);                                                    /* chord 5: n 2, 3 points */
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 4, 5.0f, &total) == 0 && total == 3);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 3, 5.0f, &total) == 1);

    g_case = "spacing: a point edge and a chord that is no number take two points";
    Edge(0, 7, 7, 7, 7, 2, 3, 0, 0, 0);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 3, 5.0f, &total) == 0 && total == 2);    /* n 1 */
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 2, 5.0f, &total) == 1);
    Edge(0, NAN, 0, 100, 0, 2, 3, 0, 0, 0);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 1, NULL, 0, 2, 5.0f, &total) == 0 && total == 2);    /* n 0 */

    g_case = "spacing: only op 0x0E's edges without points, and not the first pass's";
    Edge(0, 0, 0, 100, 0, 2, 3, 0, 0, 0);
    Edge(1, 0, 0, 100, 0, 2, 3, 4, 9, 0);   /* has points */
    Edge(2, 0, 0, 100, 0, 2, 2, 0, 0, 0);   /* type 2 */
    Edge(3, 0, 0, 100, 0, 2, 8, 0, 0, 0);   /* type 8 */
    Edge(4, 0, 0, 100, 0, 2, 3, 0, 0, 2);   /* closed */
    Edge(5, 0, 0, 100, 0, 2, 1, 6, 6, 0);   /* type 1, an empty range: op 0x0E's */
    Edge(6, 0, 0, 100, 0, 2, 3, 0, 0, 8);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 7, NULL, 0, 1000, 5.0f, &total) == 0 && total == 66);
    static const uint8_t taken[7] = {0, 0, 0, 0, 0, 0, 1};
    CHECK(CoXlTestRoadsLeftOut(g_recs, 7, taken, 0, 1000, 5.0f, &total) == 0 && total == 44);
    CHECK(CoXlTestRoadsLeftOut(g_recs, 7, NULL, 0, 50, 5.0f, &total) == 1 && total == 44);  /* a later edge that fits is not left out */

    g_case = "spacing: the smallest step of 0.25 m that leaves no edge out";
    Edge(0, 0, 0, 1000, 0, 2, 3, 0, 0, 0); Edge(1, 0, 0, 0, 1000, 2, 3, 0, 0, 0);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 1000, &total, &left)) == Bits(5.0f) && total == 404 && left == 0);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 405, &total, &left)) == Bits(5.0f));
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 404, &total, &left)) == Bits(5.25f) && total == 384);
    /* 6.75: n 149, 150 used, 150 free < 151; 7.0: n 143, 144 used, 156 free */
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 300, &total, &left)) == Bits(7.0f) && total == 288 && left == 0);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 100, 400, &total, &left)) == Bits(7.0f) && total == 388);
    g_case = "spacing: no further than 10 m";
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 10, &total, &left)) == Bits(10.0f) && left == 2 && total == 0);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 110, &total, &left)) == Bits(10.0f) && left == 1 && total == 102); /* n 101 */
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 0, NULL, 0, 0, &total, &left)) == Bits(5.0f) && left == 0);

    g_case = "spacing: the merge's road test steps over no box on a longer pair";
    {
        /* 16 steps of 40 m fall at 2.5 and 5.0: 0.9 m from this box; a step every 0.3125 m meets its side at 0.5 m */
        static const float pair[4] = {0, 0, 40, 0}, lo[2] = {3.25f, 0.5f}, hi[2] = {4.25f, 1.0f};
        CHECK(Bits(CoXlTestStampSegmentBox(pair, lo, hi)) == Bits(0.5f));
        /* a route's own 5 m pair keeps its 16 steps (0.3125 is 0.0375 m to the side of this box; 8 steps or the true distance give another) */
        static const float five[4] = {0, 0, 5, 0}, lo5[2] = {0.35f, 1.0f}, hi5[2] = {0.45f, 2.0f};
        const float d = CoXlTestStampSegmentBox(five, lo5, hi5);
        CHECK(d > 1.0005f && d < 1.001f);
        static const float point[4] = {7, 7, 7, 7};
        CHECK(Bits(CoXlTestStampSegmentBox(point, lo, hi)) == Bits(sqrtf(2.75f * 2.75f + 6.0f * 6.0f)));
    }
}

static int Near(double a, double b, double by) { return fabs(a - b) <= by; }
static float g_ground[64 * 64];
/* 64 x 64 cells of 4 m, row 0 at +y: the ground rises with x at `east` and with y at `north` per metre, from cell centres */
static void Ground(float east, float north)
{
    for (int r = 0; r < 64; r++)
        for (int c = 0; c < 64; c++) g_ground[r * 64 + c] = east * (4.0f * (float)c + 2.0f) + north * (4.0f * (float)(63 - r) + 2.0f);
}
static void Slopes(void)
{
    double out[8];
    g_case = "slopes: the grade along a line and the cross slope beside it";
    Ground(0.5f, 0.0f); /* 26.57 degrees to the east */
    static const float east[4] = {20, 100, 120, 100}, north[6] = {100, 20, 100, 60, 100, 120}, flat[4] = {20, 20, 20, 20};
    CoXlTestRoadsSlope(g_ground, 64, 64, 4.0f, -1.0e30f, east, 2, out);
    CHECK(Near(out[0], 100, 1e-3) && Near(out[1], 100, 1e-3) && out[2] == 0 && out[3] == 0 && Near(out[4], 26.565, 0.01) && Near(out[5], 0, 0.01));
    CHECK(Near(out[6], 10, 1e-3) && Near(out[7], 60, 1e-3));
    CoXlTestRoadsSlope(g_ground, 64, 64, 4.0f, -1.0e30f, north, 3, out); /* across the slope, two pairs */
    CHECK(Near(out[0], 100, 1e-3) && out[1] == 0 && Near(out[4], 0, 0.01) && Near(out[5], 26.565, 0.01) && Near(out[6], 50, 1e-3) && Near(out[7], 50, 1e-3));
    CoXlTestRoadsSlope(g_ground, 64, 64, 4.0f, -1.0e30f, flat, 2, out); /* a pair of no length */
    CHECK(out[0] == 0 && out[4] == 0);
    g_case = "slopes: over the FRV's grade, and under water";
    Ground(0.0f, 0.7f); /* 34.99 degrees to the north */
    CoXlTestRoadsSlope(g_ground, 64, 64, 4.0f, 30.0f, north, 3, out); /* 14 m at y 20 to 84 m at y 120: wet to y 42.86, in pieces of 4 m */
    CHECK(Near(out[0], 100, 1e-3) && Near(out[1], 100, 1e-3) && Near(out[2], 100, 1e-3) && Near(out[3], 24, 1e-3) && Near(out[4], 34.992, 0.01));
    g_case = "slopes: past the grid's edge the nearest cells answer";
    static const float out_[4] = {-50, -50, 300, 300};
    Ground(0.0f, 0.0f);
    g_ground[0] = 7.0f;
    CoXlTestRoadsSlope(g_ground, 64, 64, 4.0f, -1.0e30f, out_, 2, out);
    CHECK(out[6] == 0 && out[7] == 0 && out[0] > 494 && out[0] < 496);
}

/* 0xF38D25 as the stub is entered: the pass at [rsp+0x78], the chord in xmm0; `back` stores the quotient. */
typedef void (*DivideFn)(uint64_t pass, float* value);
static DivideFn Site(uint8_t* at, const uint8_t* stub, uint8_t** back)
{
    static const uint8_t code[] = {
        0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, /*  0 sub rsp, 0x88 */
        0x48, 0x89, 0x4C, 0x24, 0x78,             /*  7 mov [rsp+0x78], rcx */
        0xF3, 0x0F, 0x10, 0x02,                   /* 12 movss xmm0, [rdx] */
        0x31, 0xC0, 0xF9,                         /* 16 xor eax, eax; stc */
        0xE9, 0, 0, 0, 0,                         /* 19 jmp the stub */
        0xF3, 0x0F, 0x11, 0x02,                   /* 24 movss [rdx], xmm0 (0xF38D2D) */
        0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00, /* 28 add rsp, 0x88 */
        0xC3};
    memcpy(at, code, sizeof code);
    const int32_t rel = (int32_t)(stub - (at + 24));
    memcpy(at + 20, &rel, 4);
    *back = at + 24;
    return (DivideFn)(void*)at;
}

static void Stub(void)
{
    uint8_t* page = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (page == NULL) { printf("roadbuild: no executable page\n"); g_failed++; return; }
    g_case = "the spacing stub";
    memset(page, 0xCC, 0x1000);
    uint8_t* stub = page, *constant = page + 0x100, *back;
    const float five = 5.0f;
    memcpy(constant, &five, 4);
    const DivideFn divide = Site(page + 0x200, stub, &back);
    CoXlTestRoadSpacingBuild(stub, (uintptr_t)constant, (uintptr_t)back);
    int32_t rel;
    CHECK(memcmp(stub, "\x48\x8B\x44\x24\x78\x48\x3B\x05\x24\x00\x00\x00\x75\x0D\xF3\x0F\x5E\x05\x22\x00\x00\x00\xE9", 23) == 0);
    memcpy(&rel, stub + 23, 4); CHECK(stub + 27 + rel == back);
    CHECK(memcmp(stub + 27, "\xF3\x0F\x5E\x05", 4) == 0);
    memcpy(&rel, stub + 31, 4); CHECK(stub + 35 + rel == constant);
    CHECK(stub[35] == 0xE9); memcpy(&rel, stub + 36, 4); CHECK(stub + 40 + rel == back && stub[40] == 0xCC);
    uint64_t key; float spacing;
    memcpy(&key, stub + 0x30, 8); memcpy(&spacing, stub + 0x38, 4);
    CHECK(key == 0 && Bits(spacing) == Bits(5.0f));

    const float chords[3] = {617.3f, 0.0f, 12345.678f};
    for (int c = 0; c < 3; c++)
    {
        float v;
        key = 0x1234500; spacing = 7.25f;
        memcpy(stub + 0x30, &key, 8); memcpy(stub + 0x38, &spacing, 4);
        v = chords[c]; divide(0x1234500, &v); CHECK(Bits(v) == Bits(chords[c] / 7.25f)); /* the level's pass */
        v = chords[c]; divide(0x1234508, &v); CHECK(Bits(v) == Bits(chords[c] / 5.0f));  /* another pass */
        v = chords[c]; divide(0x100001234500ull, &v); CHECK(Bits(v) == Bits(chords[c] / 5.0f)); /* all 64 bits compared */
        key = 0; memcpy(stub + 0x30, &key, 8);
        v = chords[c]; divide(0x1234500, &v); CHECK(Bits(v) == Bits(chords[c] / 5.0f));  /* no level's op 0x0E in flight */
    }
    VirtualFree(page, 0, MEM_RELEASE);
}

/* The generation block: counts at +0x11A7108 (locations), +0x11A71B4 (nodes), +0x11A71CC (edges); the tables where
 * HellForge's layout and the hash pass's address put them. */
#define NODES_AT 0x11A8000u
#define EDGES_AT 0x11B0000u
#define POINTS_AT 0x11B8000u
#define LOCATIONS_AT 0x10000u
#define IMPORTANT_AT 0x40000u
#define BLOCK_SIZE 0x11C0000u
static uint8_t* g_block;
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Layout(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u; c->moved = 1;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x11A7280u + 0x100u * i; c->regions[i].places = 100u; }
    c->regions[HF_GEN_LOCATIONS_A].places = 65024u;
    c->regions[HF_GEN_IMPORTANT].at = IMPORTANT_AT;
    c->regions[HF_GEN_ROUTE_NODES].at = NODES_AT; c->regions[HF_GEN_ROUTE_EDGES].at = EDGES_AT; c->regions[HF_GEN_ROUTE_POINTS].at = POINTS_AT;
    g_fake.levelRow = 0x1000;
    co_capacity_init();
    co_capacity_poll(1);
    Put32(g_fake.image + 0xC176A7 + 3, LOCATIONS_AT + 0x90u);
    CoXlTestGame((uintptr_t)g_fake.image);
}
static void Edges(uint32_t n) { Put32(g_block + 0x11A71CCu, n); memcpy(g_block + EDGES_AT, g_recs, n * 0x48u); }

/* The install on the fake image: op 0x0E's site jumps into the stub, and the stub's page is code (the game faulted
 * executing a data page there). */
static void Install(void)
{
    static const uint8_t keep[5] = {0x48, 0x89, 0x4C, 0x24, 0x78};
    static const uint8_t site[31] = {0x41, 0x0F, 0x28, 0xC4, 0xF3, 0x0F, 0x5E, 0x05, 0x77, 0xE7, 0x48, 0x01, 0xF3, 0x44, 0x0F, 0x58, 0xF1, 0xF3, 0x41, 0x0F, 0x58,
        0xC5, 0xF3, 0x48, 0x0F, 0x2C, 0xF0, 0x8D, 0x46, 0x02, 0x3B};
    const float five = 5.0f;
    uint8_t* stub = NULL;
    g_case = "install: the stub is code and op 0x0E's division jumps to it";
    memcpy(g_fake.image + 0xF386AFu, keep, sizeof keep); memcpy(g_fake.image + 0xF38D21u, site, sizeof site); g_fake.image[0xF38D21u + 31] = 0xC8;
    CHECK(CoXlTestInstallRoadSpacing((uintptr_t)g_fake.image, &stub) != NULL); /* not the game's 5.0 there */
    memcpy(g_fake.image + 0x23C74A4u, &five, 4);
    const char* why = CoXlTestInstallRoadSpacing((uintptr_t)g_fake.image, &stub);
    CHECK(why == NULL);
    if (why != NULL || stub == NULL) return;
    MEMORY_BASIC_INFORMATION page;
    CHECK(VirtualQuery(stub, &page, sizeof page) == sizeof page && page.State == MEM_COMMIT && page.Protect == PAGE_EXECUTE_READWRITE);
    int32_t rel; memcpy(&rel, g_fake.image + 0xF38D26u, 4);
    CHECK(g_fake.image[0xF38D25u] == 0xE9 && g_fake.image + 0xF38D2Au + rel == stub && g_fake.image[0xF38D2Au] == 0x90 && g_fake.image[0xF38D2Cu] == 0x90);
    CHECK(stub[0] == 0x48 && stub[0x3F] == 0xCC ? 1 : stub[0] == 0x48); /* the stub's first instruction is there */
}

static void SpacingGlue(void)
{
    static uint8_t stub[0x40], record[0x100];
    static uint64_t entries[8 * 2];
    static uint32_t used, firsts;
    static float nodeA[2] = {0, 0}, nodeB[2] = {0, 99};
    const uintptr_t block = (uintptr_t)g_block;
    uint64_t key; float spacing;
    uint8_t* pass = record + 8;
    const uint64_t edgesAt = block + EDGES_AT, usedAt = (uint64_t)(uintptr_t)&used, entriesAt = (uint64_t)(uintptr_t)entries, firstsAt = (uint64_t)(uintptr_t)&firsts;
    memcpy(pass + 0x30, &entriesAt, 8); memcpy(pass + 0x38, &edgesAt, 8); memcpy(pass + 0x50, &firstsAt, 8); memcpy(pass + 0x70, &usedAt, 8);
    CoXlTestRoadSpacingBuild(stub, 0, 0);
    CoXlTestRoadSpacingAt(stub);

    g_case = "spacing glue: the level's pass is the key, with the spacing its edges need";
    CoXlTestGenLevel(8.0f);
    Edge(0, 0, 0, 1000, 0, 2, 3, 0, 0, 0); Edge(1, 0, 0, 0, 1000, 2, 3, 0, 0, 0);
    Edges(2);
    used = 0; firsts = 0; Put32(pass + 0x78, 300);
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&key, stub + 0x30, 8); memcpy(&spacing, stub + 0x38, 4);
    CHECK(key == (uint64_t)(uintptr_t)pass && Bits(spacing) == Bits(7.0f));
    CHECK(Lines("road points every 7.00 m: 288 of 300 places used, 0 edges left without points (every 5.00 m: 1 edges left without)") == 1);
    CoXlTestRoadsSpacingEnd();
    memcpy(&key, stub + 0x30, 8);
    CHECK(key == 0);
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record); /* once a level */
    memcpy(&key, stub + 0x30, 8);
    CHECK(key == 0 && Lines("road points every") == 1);

    g_case = "spacing glue: the spurs' points are kept free";
    CoXlTestGenLevel(8.0f);
    { static const float at[4] = {0, 0, 9, 9}, gap[2] = {14.1f, 11.2f}; CoXlTestRoadsSpurTerminals(at, gap, 2); } /* 7 + 7 points */
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&spacing, stub + 0x38, 4);
    /* 286 places: 7.0 -> 144 used, 142 free < 145; 7.25 -> n 138, 139 used, 147 free */
    CHECK(Bits(spacing) == Bits(7.25f) && Lines("road points every 7.25 m: 278 of 286 places used") == 1);
    CoXlTestRoadsSpacingEnd();

    g_case = "spacing glue: the points in use count, and ids stay in 16 bits";
    CoXlTestGenLevel(8.0f);
    used = 65000; Put32(pass + 0x78, 70000);
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&spacing, stub + 0x38, 4);
    /* 535 free: 7.5 -> n 134, 135 + 135 = 270 used, 400 free at the second; 3.75 would fit but 5 is the least: 404 */
    CHECK(Bits(spacing) == Bits(5.0f) && Lines("every 5.00 m: 65404 of 65535 places used, 0 edges left without points (every 5.00 m: 0 ") == 1);
    CoXlTestRoadsSpacingEnd();
    CoXlTestGenLevel(8.0f);
    used = 65200;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&spacing, stub + 0x38, 4);
    CHECK(Bits(spacing) == Bits(6.25f)); /* 335 free: 6.0 -> n 167, 168 used, 167 free < 169; 6.25 -> n 161, 162 used, 173 free */
    CoXlTestRoadsSpacingEnd();

    g_case = "spacing glue: the first pass's edge and its points";
    CoXlTestGenLevel(8.0f);
    used = 0; firsts = 2; Put32(pass + 0x78, 300);
    entries[0] = (uint64_t)(uintptr_t)nodeA; entries[1] = (uint64_t)(uintptr_t)nodeB; entries[7] = edgesAt + 0x48; /* edge 1: ceil(49.5) + 1 points */
    entries[8] = (uint64_t)(uintptr_t)nodeA; entries[9] = (uint64_t)(uintptr_t)nodeB; entries[15] = 0;             /* no edge */
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&spacing, stub + 0x38, 4);
    CHECK(Bits(spacing) == Bits(5.0f) && Lines("every 5.00 m: 253 of 300 places used") == 1); /* 51 + 202 */
    CoXlTestRoadsSpacingEnd();
    CoXlTestGenLevel(8.0f);
    { const uint16_t kept = 0x20; memcpy(g_block + EDGES_AT + 0x44, &kept, 2); } /* edge 0 with flag 0x20: not the first pass's */
    entries[15] = edgesAt;
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    CHECK(Lines("every 5.00 m: 253 of 300 places used") == 1);
    CoXlTestRoadsSpacingEnd();
    { const uint16_t none = 0; memcpy(g_block + EDGES_AT + 0x44, &none, 2); }
    firsts = 0;

    g_case = "spacing glue: another block's pass, or none readable, keeps the game's";
    CoXlTestGenLevel(8.0f);
    { const uint64_t other = edgesAt + 0x48; memcpy(pass + 0x38, &other, 8); }
    spacing = 9.0f; memcpy(stub + 0x38, &spacing, 4);
    g_fake.log[0] = 0;
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&key, stub + 0x30, 8);
    CHECK(key == 0 && Lines("op 0x0E's pass is not readable; the game's 5.00 m") == 1);
    memcpy(pass + 0x38, &edgesAt, 8);
    CoXlTestGenLevel(8.0f);
    Put32(g_block + 0x11A71CCu, 70000);
    CoXlTestRoadsSpacingBegin(block, (uint64_t)(uintptr_t)record);
    memcpy(&key, stub + 0x30, 8);
    CHECK(key == 0 && Lines("the route edges are not readable; the game's 5.00 m") == 1);
    CoXlTestRoadSpacingAt(NULL);
}

static void Arterials(void)
{
    uint8_t chosen[64]; uint32_t unreached = 0;
    /* nodes 0..3 along y = 0 every 100 m, 4 and 5 above 1 and 2, 6 past 3, 7 and 8 far off */
    static const float nodes[2 * 9] = {0, 0, 100, 0, 200, 0, 300, 0, 100, 100, 200, 100, 400, 0, 5000, 5000, 5100, 5000};
    static const uint32_t ends[2 * 10] = {0, 1, 1, 2, 2, 3, 1, 4, 4, 5, 5, 2, 3, 6, 0, 4, 7, 8, 2, 2};
    Edge(0, 0, 0, 100, 0, 2, 3, 0, 0, 8);
    Edge(1, 100, 0, 200, 0, 2, 3, 0, 0, 0);
    Edge(2, 200, 0, 300, 0, 2, 3, 0, 0, 8);
    Edge(3, 100, 0, 100, 100, 2, 3, 0, 0, 0);
    Edge(4, 100, 100, 200, 100, 2, 3, 0, 0, 0);
    Edge(5, 200, 100, 200, 0, 2, 3, 0, 0, 0);
    Edge(6, 300, 0, 400, 0, 2, 3, 0, 0, 2);      /* closed */
    Edge(7, 0, 0, 100, 100, 2, 1, 0, 0, 0);      /* a street's type */
    Edge(8, 5000, 5000, 5100, 5000, 2, 3, 0, 0, 8);
    Edge(9, 200, 0, 200, 0, 2, 3, 0, 0, 0);      /* both ends one node */

    g_case = "arterials: the flag-8 edges alone, joined by the cheapest edge";
    CHECK(CoXlTestRoadsArterials(g_recs, 8, ends, nodes, 9, NULL, 0, chosen, &unreached) == 3 && unreached == 0);
    CHECK(chosen[0] && chosen[1] && chosen[2] && !chosen[3] && !chosen[4] && !chosen[5] && !chosen[6] && !chosen[7]);

    g_case = "arterials: a terminal's nearest node is joined by the shortest way";
    static const float terms[2 * 2] = {210, 120, 420, 10}; /* node 5; node 6 has no usable edge: node 3 */
    CHECK(CoXlTestRoadsArterials(g_recs, 10, ends, nodes, 9, terms, 2, chosen, &unreached) == 5);
    CHECK(chosen[0] && chosen[1] && chosen[2] && !chosen[3] && !chosen[4] && chosen[5] && !chosen[6] && !chosen[7] && chosen[8] && !chosen[9]);
    CHECK(unreached == 2); /* the far flag-8 edge stays an arterial, its two nodes not joined */

    g_case = "arterials: an arterial edge is free to pass along";
    static const float above[2] = {100, 130}; /* node 4: 100 m from node 1, though node 1 is 100 m of arterial from the first node */
    CHECK(CoXlTestRoadsArterials(g_recs, 8, ends, nodes, 9, above, 1, chosen, &unreached) == 4 && chosen[3] && !chosen[4] && !chosen[5] && !chosen[7]);

    g_case = "arterials: no flag-8 edge: a tree over the terminals from the lowest wanted node";
    for (uint32_t i = 0; i < 9; i++) { uint16_t none = 0; memcpy(g_recs + 0x48u * i + 0x44, &none, 2); }
    { uint16_t closed = 2; memcpy(g_recs + 0x48u * 6 + 0x44, &closed, 2); }
    static const float three[2 * 3] = {290, 5, 110, 110, 5000, 5000}; /* nodes 3, 4 and 7 */
    CHECK(CoXlTestRoadsArterials(g_recs, 9, ends, nodes, 9, three, 3, chosen, &unreached) == 3 && unreached == 1);
    CHECK(!chosen[0] && chosen[1] && chosen[2] && chosen[3] && !chosen[4] && !chosen[5] && !chosen[8]);

    g_case = "arterials: equal ways: through the lower node";
    /* a square 0-1-3 and 0-2-3, a terminal at 3 and one at 0 */
    static const float square[2 * 4] = {0, 0, 100, 0, 0, 100, 100, 100};
    static const uint32_t sides[2 * 4] = {0, 2, 2, 3, 0, 1, 1, 3};
    Edge(0, 0, 0, 0, 100, 2, 3, 0, 0, 0); Edge(1, 0, 100, 100, 100, 2, 3, 0, 0, 0);
    Edge(2, 0, 0, 100, 0, 2, 3, 0, 0, 0); Edge(3, 100, 0, 100, 100, 2, 3, 0, 0, 0);
    static const float corners[2 * 2] = {0, 0, 100, 100};
    CHECK(CoXlTestRoadsArterials(g_recs, 4, sides, square, 4, corners, 2, chosen, &unreached) == 2 && chosen[2] && chosen[3] && !chosen[0] && !chosen[1]);

    g_case = "arterials: a flag-8 edge not yet joined is free too";
    /* 0 - 1 (100 m), 1 - 2 flag 8 (1000 m), 2 - 3 (100 m), and 0 - 3 straight (500 m): the way round costs 200 */
    static const float row[2 * 4] = {0, 0, 100, 0, 1100, 0, 1200, 0};
    static const uint32_t links[2 * 4] = {0, 1, 1, 2, 2, 3, 0, 3};
    Edge(0, 0, 0, 100, 0, 2, 3, 0, 0, 0); Edge(1, 100, 0, 1100, 0, 2, 3, 0, 0, 8);
    Edge(2, 1100, 0, 1200, 0, 2, 3, 0, 0, 0); Edge(3, 0, 0, 500, 0, 2, 3, 0, 0, 0);
    static const float tips[2 * 2] = {0, 0, 1200, 0};
    CHECK(CoXlTestRoadsArterials(g_recs, 4, links, row, 4, tips, 2, chosen, &unreached) == 3 && chosen[0] && chosen[1] && chosen[2] && !chosen[3]);
    Edge(0, 0, 0, 0, 100, 2, 3, 0, 0, 0); Edge(1, 0, 100, 100, 100, 2, 3, 0, 0, 0);
    Edge(2, 0, 0, 100, 0, 2, 3, 0, 0, 0); Edge(3, 100, 0, 100, 100, 2, 3, 0, 0, 0);

    g_case = "arterials: nothing to choose from";
    CHECK(CoXlTestRoadsArterials(g_recs, 4, sides, square, 4, NULL, 0, chosen, &unreached) == 0 && unreached == 0);
    CHECK(CoXlTestRoadsArterials(g_recs, 0, sides, square, 4, corners, 2, chosen, &unreached) == 0);
    CHECK(CoXlTestRoadsArterials(g_recs, 4, sides, square, 0, corners, 2, chosen, &unreached) == 0 && !chosen[2]);
    static const uint32_t loose[2 * 4] = {0, 9, 2, 3, 0xFFFFFFFFu, 1, 1, 3}; /* edges 0 and 2 name no node of the 4 */
    CHECK(CoXlTestRoadsArterials(g_recs, 4, loose, square, 4, corners, 2, chosen, &unreached) == 1 && chosen[3] && !chosen[0] && !chosen[2] && unreached == 0);
}

static struct { uint64_t index; float c[6]; uint32_t id; } g_capsules[256];
static uint32_t g_capsuleCount;
static void Capsule(uint64_t index, const float* capsule, uint32_t id)
{
    if (g_capsuleCount < 256) { g_capsules[g_capsuleCount].index = index; memcpy(g_capsules[g_capsuleCount].c, capsule, 24); g_capsules[g_capsuleCount].id = id; }
    g_capsuleCount++;
}
/* A stamp index as 0xC04EF0 reads it: the free list's head at +8 +0x2C, the place size at +8 +0x24, the places at +8 +0x30 */
static uint8_t g_index[0x60];
static uint32_t g_places[8 * 4];
static void Index(uint32_t free_)
{
    const uint64_t base = (uint64_t)(uintptr_t)g_places; const uint16_t stride = 16;
    memset(g_index, 0, sizeof g_index);
    for (uint32_t i = 0; i < 8; i++) g_places[4 * i] = i + 1 < free_ ? i + 1 : 0xFFFFFFFFu;
    Put32(g_index + 8 + 0x2C, free_ ? 0 : 0xFFFFFFFFu); memcpy(g_index + 8 + 0x24, &stride, 2); memcpy(g_index + 8 + 0x30, &base, 8);
}
static float Point(uint32_t i, int c) { float v; memcpy(&v, g_block + POINTS_AT + 8u * i + 4u * (uint32_t)c, 4); return v; }
static uint32_t Count32(uint32_t at) { uint32_t v; memcpy(&v, g_block + at, 4); return v; }
static float Width(uint32_t edge) { float w; memcpy(&w, g_block + EDGES_AT + 0x48u * edge + 0x30, 4); return w; }
static void ArterialsGlue(void)
{
    const uintptr_t block = (uintptr_t)g_block;
    static const float nodes[2 * 5] = {0, 0, 1000, 0, 2000, 0, 1000, 1000, 1000, 2000};
    static const uint32_t ends[2 * 5] = {0, 1, 1, 2, 1, 3, 3, 4, 0, 3};
    g_case = "arterials glue: the chosen edges take the width, never a narrower one";
    CoXlTestGenLevel(8.0f);
    Edge(0, 0, 0, 1000, 0, 2, 3, 0, 0, 8);
    Edge(1, 1000, 0, 2000, 0, 12, 3, 0, 0, 8);
    Edge(2, 1000, 0, 1000, 1000, 2, 3, 0, 0, 0);
    Edge(3, 1000, 1000, 1000, 2000, 2, 3, 0, 0, 0);
    Edge(4, 0, 0, 1000, 1000, 2, 3, 0, 0, 0);
    for (uint32_t i = 0; i < 5; i++)
        for (int q = 0; q < 2; q++) { const uint64_t p = block + NODES_AT + 0x98ull * ends[2 * i + q]; memcpy(g_recs + 0x48u * i + 0x10 + 8 * q, &p, 8); }
    Edges(5);
    Put32(g_block + 0x11A71B4u, 5);
    for (uint32_t v = 0; v < 5; v++) memcpy(g_block + NODES_AT + 0x98u * v, &nodes[2 * v], 8);
    /* an objective by node 3, an outpost by node 2, and op 0x8E's place by node 4: not a terminal */
    static const float xy[2 * 4] = {990, 1010, 2010, 5, 1000, 2010, 1000, 1990};
    static const uint8_t ops[4] = {0x16, 0x82, 0x8E, 0x17};
    for (uint32_t i = 0; i < 4; i++) memcpy(g_block + LOCATIONS_AT + 0xE0u * i, &xy[2 * i], 8);
    g_fake.log[0] = 0;
    CoXlTestRoadsArterialsApply(block, ops, 3);
    CHECK(Width(0) == 8.0f && Width(1) == 12.0f && Width(2) == 8.0f && Width(3) == 2.0f && Width(4) == 2.0f);
    CHECK(CoXlTestRoadsArterial(0) && CoXlTestRoadsArterial(1) && CoXlTestRoadsArterial(2) && !CoXlTestRoadsArterial(3) && !CoXlTestRoadsArterial(4));
    /* the objective (990, 1010) is 14 m from edge 2's end, the outpost (2010, 5) 11 m from edge 1's */
    CHECK(Lines("arterials: 3 of 5 open route edges (3.0 km; 2 with flag 0x8) join 2 objectives, outposts and op 0x17 places (0 an extraction); 2 widened to 8.0 m, 0 wanted nodes not joined; "
        "the places lie median 14 m, at most 14 m from them; road set 253381B6") == 1);
    /* after op 0x0E: edge 0 has points, edges 1 and 2 none */
    Put32(g_block + 0x11A71D4u, 9);
    { const uint32_t range[2] = {0, 9}; memcpy(g_block + EDGES_AT + 0x38, range, 8); }
    CoXlTestRoadsArterialsPoints(block);
    CHECK(Lines("arterials after op 0x0E: 2 of 3 arterial edges without points (9 road points)") == 1);
    { uint16_t flags; memcpy(&flags, g_block + EDGES_AT + 0x48u * 2 + 0x44, 2); CHECK(flags == 0 && g_block[EDGES_AT + 0x48u * 2 + 0x34] == 3); }

    g_case = "spurs: each place's straight way to the nearest arterial point, as points and index capsules";
    static uint64_t context[5];
    Index(8);
    context[4] = (uint64_t)(uintptr_t)g_index;
    uint32_t reserve = 0; const float* table = NULL;
    CHECK(CoXlTestRoadsSpurs(&reserve, &table) == 0 && reserve == 14); /* (14.1 + 40) / 10 -> 6 + 1, (11.2 + 40) / 10 -> 6 + 1 */
    /* edge 4 is no arterial: its points 5 m from the objective are not a spur's end */
    static const float pts[2 * 9] = {0, 0, 500, 0, 1000, 0, 1000, 0, 2000, 0, 1000, 0, 1000, 1000, 980, 1010, 985, 1010};
    memcpy(g_block + POINTS_AT, pts, sizeof pts);
    Put32(g_block + 0x11A71D4u, 9);
    { const uint32_t r[6] = {0, 3, 3, 5, 5, 7}; for (uint32_t i = 0; i < 3; i++) memcpy(g_block + EDGES_AT + 0x48u * i + 0x38, &r[2 * i], 8); }
    { const uint32_t r[2] = {7, 9}; memcpy(g_block + EDGES_AT + 0x48u * 4 + 0x38, r, 8); }
    g_fake.log[0] = 0; g_capsuleCount = 0;
    CoXlTestRoadsSpursBuild(block, (uint64_t)(uintptr_t)context, Capsule);
    CHECK(Count32(0x11A71D4u) == 15 && g_capsuleCount == 4 && CoXlTestRoadsSpurs(&reserve, &table) == 2);
    /* the objective (990, 1010) to edge 2's end (1000, 1000): 3 points; the outpost (2010, 5) to edge 1's end (2000, 0) */
    CHECK(Point(9, 0) == 990 && Point(9, 1) == 1010 && Point(10, 0) == 995 && Point(10, 1) == 1005 && Point(11, 0) == 1000 && Point(11, 1) == 1000);
    CHECK(Point(12, 0) == 2010 && Point(12, 1) == 5 && Point(14, 0) == 2000 && Point(14, 1) == 0 && Point(8, 0) == 985);
    CHECK(g_capsules[0].index == (uint64_t)(uintptr_t)g_index && g_capsules[0].id == (10u | (9u << 16)) && g_capsules[1].id == (11u | (9u << 16)));
    CHECK(g_capsules[0].c[0] == 990 && g_capsules[0].c[1] == 1010 && g_capsules[0].c[2] == 995 && g_capsules[0].c[3] == 1005 && g_capsules[0].c[4] == 4 && g_capsules[0].c[5] == 4);
    CHECK(g_capsules[1].c[0] == 995 && g_capsules[1].c[2] == 1000 && g_capsules[1].c[3] == 1000);
    CHECK(g_capsules[2].id == (13u | (5u << 16)) && g_capsules[3].id == (14u | (5u << 16)) && g_capsules[3].c[4] == 6); /* edge 1 is 12 m wide */
    CHECK(table[0] == 990 && table[1] == 1010 && table[2] == 1000 && table[3] == 1000);
    { uint32_t u[3]; memcpy(u, table + 4, 12); CHECK(u[0] == 2 && u[1] == 9 && u[2] == 12); }
    CHECK(Lines("spurs: 2 of 2 places joined to the arterials (0.0 km, the longest 14 m; 6 road points of 14 kept for them), 0 on a road already, 0 too far, 0 without room, 0 without a place in the stamp index (8 free after them)") == 1);
    g_fake.log[0] = 0;
    CoXlTestRoadsIndexNote((uint64_t)(uintptr_t)context, "after op 0x2B");
    CHECK(Lines("stamp index after op 0x2B: 8 places free") == 1);
    Index(0);
    CoXlTestRoadsIndexNote((uint64_t)(uintptr_t)context, "when full");
    CHECK(Lines("stamp index when full: 0 places free") == 1);
    Index(8);
    g_case = "spurs: the road point count is watched after the ops that could touch it";
    g_fake.log[0] = 0;
    CoXlTestRoadsPointsWatch(block, 0x0D); CoXlTestRoadsPointsWatch(block, 0x0E);
    CHECK(Lines("road points after op 0x0D: 15 (the spurs left 15)") == 1 && Lines("road points after") == 1 && Lines("CHANGED") == 0);
    Put32(g_block + 0x11A71D4u, 9);
    CoXlTestRoadsPointsWatch(block, 0x41);
    CHECK(Lines("road points after op 0x41: 9 (the spurs left 15): CHANGED") == 1);
    Put32(g_block + 0x11A71D4u, 15);
    g_case = "spurs: each place's straight way to the nearest arterial point, as points and index capsules";

    g_case = "corridor: what stands in the arterials' and the spurs' strips, by kind";
    {
        /* x, y, half side: the objective and the outpost (each at its own spur's start; the outpost's box 5 m from
         * edge 1's end, inside its 6 m), a place on edge 0, own stamps on edge 2, beside it and on the objective's
         * spur, an added stamp on edge 0, and one in three strips where the edges meet: 2.55 m from edge 0's
         * pair, 2.5 m from edge 1's, 0.5 m from edge 2's, the deepest */
        static const float at[8][3] = {{990, 1010, 5}, {2010, 5, 5}, {500, 2, 1}, {1003, 500, 1}, {1010, 500, 1}, {993, 1007, 0.5f}, {250, -3, 1}, {1001, 3, 0.5f}};
        static const uint8_t before[3] = {0x16, 0x82, 0x8E};
        static uint8_t plan[2 * 0x200];
        const uint32_t size = CoXlTestStampCandidateSize();
        for (uint32_t i = 0; i < 8; i++)
        {
            uint8_t* loc = g_block + LOCATIONS_AT + 0xE0u * i;
            const float box[4] = {at[i][0] + at[i][2], at[i][1] - at[i][2], at[i][0] - at[i][2], at[i][1] + at[i][2]}; /* corners in either order */
            memset(loc, 0, 0xE0); memcpy(loc, at[i], 8); memcpy(loc + 0xB0, box, 16);
            Put32(loc + 0xD8, 40u + i); loc[0xD4] = (uint8_t)(i + 1);
        }
        CHECK(size >= 0xE0 && size <= 0x200);
        memset(plan, 0, sizeof plan);
        { const float p[2] = {1500, 6.5f}, box[4] = {1499, 5, 1501, 8}; memcpy(plan, p, 8); memcpy(plan + 0xB0, box, 16); }       /* 5 m from edge 1: inside its 6 m */
        { const float p[2] = {1500, 7.5f}, box[4] = {1499, 6, 1501, 9}; memcpy(plan + size, p, 8); memcpy(plan + size + 0xB0, box, 16); } /* 6 m: not inside */
        CoXlTestRoadsStamps(6, plan, 2);
        g_fake.log[0] = 0;
        CoXlTestRoadsCorridor(block, before, 3, 8);
        CHECK(Lines("corridor: 3.0 km of arterials and 0.0 km of spurs (8 pairs): inside the strip 2 of the level's own op 0x2B stamps (2 centred, 1 on a spur), "
            "2 added stamps (2 centred, 0 on a spur), 1 of the plan's (0 centred, 0 on a spur), 2 places of other ops (1 centred, 0 on a spur); 1.65 stamps per km; "
            "clear across the middle: 0.0 m to the nearest of them; counted in ") == 1);
        CHECK(Lines("corridor: place 1 (op 0x82, type 41, flags 0x2, location +0xC8 0x0) at (-6182, -8187), box 10.0 x 10.0 m, 5.00 m from the middle of arterial edge 1's 6.0 m half width") == 1);
        CHECK(Lines("corridor: place 2 (op 0x8E, type 42, flags 0x3, location +0xC8 0x0) at (-7692, -8190), box 2.0 x 2.0 m, 1.00 m from the middle of arterial edge 0's 4.0 m half width, its centre inside") == 1);
        CHECK(Lines("corridor: own 3 (op 0x2B, type 43, flags 0x4, location +0xC8 0x0) at (-7189, -7692), box 2.0 x 2.0 m, 2.00 m from the middle of arterial edge 2's 4.0 m half width, its centre inside") == 1);
        CHECK(Lines("corridor: own 5 (op 0x2B, type 45, flags 0x6, location +0xC8 0x0) at (-7199, -7185), box 1.0 x 1.0 m, 0.00 m from the middle of the spur at edge 2's 4.0 m half width, its centre inside") == 1);
        CHECK(Lines("corridor: added 7 (op 0x2B, type 47, flags 0x8, location +0xC8 0x0) at (-7191, -8189), box 1.0 x 1.0 m, 0.50 m from the middle of arterial edge 2's 4.0 m half width, its centre inside") == 1);
        CHECK(Lines("corridor: added 6 (op 0x2B, type 46") == 1 && Lines("corridor: plan 0 (op 0x2B, type 0") == 1);
        CHECK(Lines("corridor: own 4 ") == 0 && Lines("corridor: place 0 ") == 0 && Lines("corridor: plan 1 ") == 0);
        /* without the mod's stamps: the level's own from `before` on, and no plan; the stamps on the spur and where the
         * edges meet away: 4 m clear (the place 1 m from the middle is no stamp) */
        CoXlTestRoadsStamps(0xFFFFFFFFu, NULL, 0);
        { const float off[2] = {5000, 5000}, box[4] = {4999, 4999, 5001, 5001}; memcpy(g_block + LOCATIONS_AT + 0xE0u * 5, off, 8); memcpy(g_block + LOCATIONS_AT + 0xE0u * 5 + 0xB0, box, 16); }
        g_fake.log[0] = 0;
        CoXlTestRoadsCorridor(block, before, 3, 7);
        CHECK(Lines("inside the strip 2 of the level's own op 0x2B stamps (2 centred, 0 on a spur), 0 added stamps (0 centred, 0 on a spur), 0 of the plan's (0 centred, 0 on a spur), "
            "2 places of other ops (1 centred, 0 on a spur); 0.66 stamps per km; clear across the middle: 4.0 m to the nearest of them") == 1);
        /* nothing in a strip */
        g_fake.log[0] = 0;
        CoXlTestRoadsCorridor(block, before, 1, 1); /* the objective alone: its own spur ends in it */
        CHECK(Lines("inside the strip 0 of the level's own op 0x2B stamps") == 1 && Lines("0 places of other ops (0 centred, 0 on a spur); 0.00 stamps per km; clear across the middle: the full width everywhere") == 1);
        for (uint32_t i = 0; i < 8; i++) memset(g_block + LOCATIONS_AT + 0xE0u * i, 0, 0xE0);
        for (uint32_t i = 0; i < 4; i++) memcpy(g_block + LOCATIONS_AT + 0xE0u * i, &xy[2 * i], 8);
    }

    g_case = "slopes: one line for the arterials and one for the spurs, once the capture is kept";
    {
        static float level[512 * 512]; /* 32 m cells over the 16 km level: flat at 3 m, the water at 5 m */
        for (uint32_t i = 0; i < 512 * 512; i++) level[i] = 3.0f;
        g_fake.log[0] = 0;
        CoXlTestRoadsSlopeNote(block);
        CHECK(Lines("slopes:") == 0); /* no capture yet */
        CoXlTestRoadsGround(level, 512, 512, 2, 5.0f);
        CoXlTestRoadsSlopeNote(block);
        CHECK(Lines("slopes: 3 arterial edges, 3.0 km on the 32 m height capture kept at op 0x65: steepest 0.0 deg (edge 0), 0 m over 18 deg in 0 of them, 0 m over 31 deg, "
            "cross slope at most 0.0 deg, 3000 m at or under water (the highest water at 5.0 m)") == 1);
        CHECK(Lines("slopes: 2 spurs, 0.0 km on the 32 m height capture kept at op 0x65: steepest 0.0 deg (spur 0), 0 m over 18 deg in 0 of them, 0 m over 31 deg, "
            "cross slope at most 0.0 deg, 25 m at or under water (the highest water at 5.0 m)") == 1);
        CoXlTestRoadsSlopeNote(block);
        CHECK(Lines("slopes:") == 2); /* once a level */
    }

    g_case = "spurs: a place on a road, one too far, and no index";
    static const float away[2 * 3] = {1000, 400, 1000, 2100, 3000, 0};
    static const uint8_t three[3] = {0x16, 0x16, 0x82};
    for (uint32_t i = 0; i < 3; i++) memcpy(g_block + LOCATIONS_AT + 0xE0u * i, &away[2 * i], 8);
    Put32(g_block + 0x11A71D4u, 9);
    CoXlTestRoadsArterialsApply(block, three, 3);
    g_fake.log[0] = 0; g_capsuleCount = 0;
    CoXlTestRoadsSpursBuild(block, (uint64_t)(uintptr_t)context, Capsule);
    /* (1000, 400) is on edge 2; (1000, 2100) is 1100 m from its end; (3000, 0) is 1000 m from edge 1's end: not under the most */
    CHECK(g_capsuleCount == 0 && Count32(0x11A71D4u) == 9 && Lines("spurs: 0 of 3 places joined") == 1 && Lines("1 on a road already, 2 too far, 0 without room, 0 without a place") == 1);
    /* (1000, -10) is 10 m from the point where edges 0, 1 and 2 meet: the lowest edge is the one joined */
    { static const float tie[2] = {1000, -10}; memcpy(g_block + LOCATIONS_AT, tie, 8); }
    CoXlTestRoadsArterialsApply(block, three, 1);
    g_capsuleCount = 0;
    CoXlTestRoadsSpursBuild(block, (uint64_t)(uintptr_t)context, Capsule);
    CHECK(g_capsuleCount == 1 && g_capsules[0].id == (10u | (1u << 16)) && Count32(0x11A71D4u) == 11);
    Put32(g_block + 0x11A71D4u, 9);
    /* a full index takes no pair: 0xFCADC0 would write through null */
    Index(0);
    Put32(g_block + 0x11A71D4u, 9);
    CoXlTestRoadsArterialsApply(block, three, 1);
    g_capsuleCount = 0; g_fake.log[0] = 0;
    CoXlTestRoadsSpursBuild(block, (uint64_t)(uintptr_t)context, Capsule);
    CHECK(g_capsuleCount == 0 && Count32(0x11A71D4u) == 9 && Lines("spurs: 0 of 1 places joined") == 1 && Lines("1 without a place in the stamp index (0 free after them)") == 1);
    Index(8);
    context[4] = 0;
    g_fake.log[0] = 0;
    CoXlTestRoadsSpursBuild(block, (uint64_t)(uintptr_t)context, Capsule);
    CHECK(Lines("spurs: the stamp index or the road points are not readable; no spur made") == 1);
    context[4] = (uint64_t)(uintptr_t)g_index;
    for (uint32_t i = 0; i < 4; i++) memcpy(g_block + LOCATIONS_AT + 0xE0u * i, &xy[2 * i], 8);

    g_case = "arterials glue: the extraction is a terminal whatever op appended it";
    static const uint8_t other[4] = {0x16, 0x82, 0x8E, 0x8B};
    memset(g_block + IMPORTANT_AT, 0, 5 * 0x108);
    g_block[IMPORTANT_AT + 3 * 0x108 + 0xDC] = 6;
    Put32(g_block + 0x11A715Cu, 3); /* the fourth record is past the important locations: not one */
    { const float two = 2.0f; memcpy(g_block + EDGES_AT + 0x48u * 3 + 0x30, &two, 4); } /* the case above widened it */
    CoXlTestRoadsArterialsApply(block, other, 4);
    CHECK(Width(3) == 2.0f && !CoXlTestRoadsArterial(3));
    Put32(g_block + 0x11A715Cu, 4);
    g_fake.log[0] = 0;
    CoXlTestRoadsArterialsApply(block, other, 4);
    CHECK(Width(3) == 8.0f && CoXlTestRoadsArterial(3) && Lines("join 3 objectives, outposts and op 0x17 places (1 an extraction);") == 1);
    Put32(g_block + 0x11A715Cu, 0);
    { const float two = 2.0f; memcpy(g_block + EDGES_AT + 0x48u * 3 + 0x30, &two, 4); }

    g_case = "arterials glue: an op 0x17 place is a terminal";
    CoXlTestRoadsArterialsApply(block, ops, 4); /* the fourth place is by node 4 */
    CHECK(Width(3) == 8.0f && CoXlTestRoadsArterial(3) && Width(4) == 2.0f);

    g_case = "arterials glue: the next level starts from none";
    CoXlTestGenLevel(8.0f);
    g_fake.log[0] = 0;
    CoXlTestRoadsArterialsPoints(block);
    CHECK(!CoXlTestRoadsArterial(0) && Lines("arterials after op 0x0E") == 0);

    g_case = "arterials glue: a preview's edges have no width and are left";
    for (uint32_t i = 0; i < 5; i++) { const float none = 0.0f; memcpy(g_recs + 0x48u * i + 0x30, &none, 4); }
    Edges(5);
    CoXlTestRoadsArterialsApply(block, ops, 4);
    CHECK(Width(0) == 0.0f && Width(2) == 0.0f && !CoXlTestRoadsArterial(0) && Lines("no edge widened") == 1);
    g_fake.log[0] = 0;
    for (uint32_t i = 0; i < 5; i++) { const float two = 2.0f; memcpy(g_recs + 0x48u * i + 0x30, &two, 4); }
    Edges(5);
    CoXlTestRoadsArterialsApply(block, ops, 2); /* a narrower level after it: only this level's edges are arterials */
    CHECK(CoXlTestRoadsArterial(0) && CoXlTestRoadsArterial(2) && !CoXlTestRoadsArterial(3));
    CoXlTestGenLevel(8.0f);

    g_case = "arterials glue: a structured map's edges are left";
    for (uint32_t i = 0; i < 5; i++) { g_recs[0x48u * i + 0x34] = 1; const float two = 2.0f; memcpy(g_recs + 0x48u * i + 0x30, &two, 4); }
    Edges(5);
    g_fake.log[0] = 0;
    CoXlTestRoadsArterialsApply(block, ops, 4);
    CHECK(Width(0) == 2.0f && Width(2) == 2.0f && !CoXlTestRoadsArterial(0) && Lines("no open type-3 route edge with a width (a structured map, or a preview); no edge widened") == 1);

    g_case = "arterials glue: nothing readable";
    Put32(g_block + 0x11A71B4u, 0);
    g_fake.log[0] = 0;
    CoXlTestRoadsArterialsApply(block, ops, 4);
    CHECK(Lines("the route graph is not readable; no edge widened") == 1);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    Spacing();
    Slopes();
    Stub();
    Arterials();
    g_block = (uint8_t*)VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_block == NULL) { printf("roadbuild: no memory\n"); return 1; }
    Layout();
    Install();
    SpacingGlue();
    ArterialsGlue();
    VirtualFree(g_block, 0, MEM_RELEASE);
    if (g_failed) { printf("roadbuild: %d check(s) failed\n", g_failed); return 1; }
    printf("roadbuild: all checks passed\n");
    return 0;
}
