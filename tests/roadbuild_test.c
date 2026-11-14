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
void CoXlTestRoadsSpacingBegin(uintptr_t block, uint64_t record);
void CoXlTestRoadsSpacingEnd(void);
uint32_t CoXlTestRoadsArterials(const uint8_t* recs, uint32_t edges, const uint32_t* ends, const float* nodeXY, uint32_t nodes, const float* termXY, uint32_t terms,
    uint8_t* chosen, uint32_t* unreached);
void CoXlTestRoadsArterialsApply(uintptr_t block, const uint8_t* ops, uint32_t total);
void CoXlTestRoadsArterialsPoints(uintptr_t block);
int CoXlTestRoadsArterial(uint32_t edge);

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
    g_case = "spacing: no further than 40 m";
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 10, &total, &left)) == Bits(40.0f) && left == 2 && total == 0);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 2, NULL, 0, 30, &total, &left)) == Bits(40.0f) && left == 1 && total == 27);
    CHECK(Bits(CoXlTestRoadsSpacing(g_recs, 0, NULL, 0, 0, &total, &left)) == Bits(5.0f) && left == 0);
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
    c->regions[HF_GEN_ROUTE_NODES].at = NODES_AT; c->regions[HF_GEN_ROUTE_EDGES].at = EDGES_AT; c->regions[HF_GEN_ROUTE_POINTS].at = POINTS_AT;
    g_fake.levelRow = 0x1000;
    co_capacity_init();
    co_capacity_poll(1);
    Put32(g_fake.image + 0xC176A7 + 3, LOCATIONS_AT + 0x90u);
    CoXlTestGame((uintptr_t)g_fake.image);
}
static void Edges(uint32_t n) { Put32(g_block + 0x11A71CCu, n); memcpy(g_block + EDGES_AT, g_recs, n * 0x48u); }

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
    CHECK(Lines("arterials: 3 of 5 open route edges (3.0 km; 2 with flag 0x8) join 2 objectives and outposts; 2 widened to 8.0 m, 0 wanted nodes not joined; "
        "the places lie median 14 m, at most 14 m from them; road set 253381B6") == 1);
    /* after op 0x0E: edge 0 has points, edges 1 and 2 none */
    Put32(g_block + 0x11A71D4u, 9);
    { const uint32_t range[2] = {0, 9}; memcpy(g_block + EDGES_AT + 0x38, range, 8); }
    CoXlTestRoadsArterialsPoints(block);
    CHECK(Lines("arterials after op 0x0E: 2 of 3 arterial edges without points (9 road points)") == 1);
    { uint16_t flags; memcpy(&flags, g_block + EDGES_AT + 0x48u * 2 + 0x44, 2); CHECK(flags == 0 && g_block[EDGES_AT + 0x48u * 2 + 0x34] == 3); }
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
    Stub();
    Arterials();
    g_block = (uint8_t*)VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_block == NULL) { printf("roadbuild: no memory\n"); return 1; }
    Layout();
    SpacingGlue();
    ArterialsGlue();
    VirtualFree(g_block, 0, MEM_RELEASE);
    if (g_failed) { printf("roadbuild: %d check(s) failed\n", g_failed); return 1; }
    printf("roadbuild: all checks passed\n");
    return 0;
}
