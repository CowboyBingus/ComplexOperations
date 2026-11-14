/* The roads' evidence lines (proto/difficulty_world_roads.c): which edges count as drawn, the width histogram, the
 * lengths along the points, the connected parts, the distances from places to the drawn paths and the playable disc;
 * then the lines over a fake generation block, each once per level and none for another op. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestGenLevel(float current);
void CoXlTestRoadsWidths(const uint8_t* recs, uint32_t n, char* out, size_t size);
void CoXlTestRoadsPoints(const uint8_t* recs, uint32_t n, uint32_t points, uint32_t counts[8], double km[2]);
void CoXlTestRoadsPaths(const uint8_t* recs, uint32_t n, const float* pts, uint32_t points, uint32_t out[2], double m[2]);
uint32_t CoXlTestRoadsParts(const uint8_t* recs, uint32_t n, const float* pts, uint32_t points, uint32_t comp[64], uint32_t largest[2], double* metres);
void CoXlTestRoadsReach(const float* xy, uint32_t n, const uint8_t* recs, uint32_t edges, const float* pts, uint32_t points, uint32_t* part, float out[8]);
DWORD CoXlTestTsvLength(char* line, size_t size, int len);
double CoXlTestRoadsDisc(uint32_t world, float fraction, double* radius);
void CoXlTestRoadsLevel(const uint8_t* ops, uint32_t n, uint32_t planned);
void CoXlTestRoadsAfterOp(uintptr_t block, uint64_t context, uint64_t record, uint32_t op, uint32_t before, int counted);

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
static void Nodes(uint32_t i, uint64_t a, uint64_t b) { memcpy(g_recs + 0x48u * i + 0x10, &a, 8); memcpy(g_recs + 0x48u * i + 0x18, &b, 8); }
static int Near(double a, double b) { return fabs(a - b) < 0.01; }

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at += strlen(text)) n++;
    return n;
}

static void Widths(void)
{
    char out[320];
    g_case = "widths: ascending, to the centimetre, NaN in its own bin";
    Edge(0, 0, 0, 1, 0, 2.0f, 3, 0, 0, 0); Edge(1, 0, 0, 1, 0, 10.0f, 3, 0, 0, 0x18); Edge(2, 0, 0, 1, 0, 2.004f, 3, 0, 0, 0);
    Edge(3, 0, 0, 1, 0, NAN, 3, 0, 0, 0); Edge(4, 0, 0, 1, 0, 6.5f, 3, 0, 0, 0); Edge(5, 0, 0, 1, 0, 2.0f, 3, 0, 0, 0);
    CoXlTestRoadsWidths(g_recs, 6, out, sizeof out);
    CHECK(strcmp(out, "unreadable x 1, 2.00 m x 3, 6.50 m x 1, 10.00 m x 1") == 0);

    g_case = "widths: past 8 the rest counted together";
    for (uint32_t i = 0; i < 11; i++) Edge(i, 0, 0, 1, 0, (float)(11 - i), 3, 0, 0, 0);
    CoXlTestRoadsWidths(g_recs, 11, out, sizeof out);
    CHECK(strcmp(out, "4.00 m x 1, 5.00 m x 1, 6.00 m x 1, 7.00 m x 1, 8.00 m x 1, 9.00 m x 1, 10.00 m x 1, 11.00 m x 1, other widths x 3") == 0);

    g_case = "widths: none, and a short buffer stays a string";
    CoXlTestRoadsWidths(g_recs, 0, out, sizeof out);
    CHECK(strcmp(out, "none") == 0);
    CoXlTestRoadsWidths(g_recs, 11, out, 12);
    CHECK(strlen(out) == 11 && strncmp(out, "4.00 m x 1,", 11) == 0);
}

static void Points(void)
{
    uint32_t c[8]; double km[2];
    g_case = "points: op 0x0E's edges, and which of them op 0x0F can stamp";
    Edge(0, 0, 0, 1000, 0, 2, 3, 0, 5, 0);          /* drawn */
    Edge(1, 0, 0, 1000, 0, 2, 3, 5, 5, 0);          /* skipped for room: an empty range */
    Edge(2, 0, 0, 0, 500, 2, 3, 7, 7, 8);           /* the skeleton's, empty */
    Edge(3, 0, 0, 1000, 0, 2, 2, 0, 0, 0);          /* type 2: not op 0x0E's */
    Edge(4, 0, 0, 1000, 0, 2, 8, 0, 0, 0);          /* type 8: not op 0x0E's */
    Edge(5, 0, 0, 1000, 0, 2, 3, 0, 0, 2);          /* flag 2: not op 0x0E's */
    Edge(6, 0, 0, 300, 400, 2, 3, 8, 70000, 8);     /* past the points written */
    Edge(7, 0, 0, 0, 2000, 2, 3, 9, 10, 0);         /* one point: no pair */
    Edge(8, 0, 0, 0, 1000, 2, 1, 5, 7, 8);          /* type 1 drawn: two points */
    Edge(9, 0, 0, 0, 1000, 2, 3, 6, 4, 0);          /* backwards */
    CoXlTestRoadsPoints(g_recs, 10, 10, c, km);
    CHECK(c[0] == 10 && c[1] == 7 && c[2] == 5 && c[3] == 2);
    CHECK(c[4] == 2 && c[5] == 1 && c[6] == 2);                 /* no point: 1, 2; one: 7; outside or backwards: 6, 9 */
    CHECK(c[7] == 202 + 102 + 102 + 402 + 202);                /* int(chord / 5 + 1) + 1 points each */
    CHECK(Near(km[0], 1 + 1 + 0.5 + 0.5 + 2 + 1 + 1) && Near(km[1], 1 + 0.5 + 0.5 + 2 + 1));
    g_case = "points: the end at the count is inside";
    CoXlTestRoadsPoints(g_recs, 1, 5, c, km);
    CHECK(c[1] == 1 && c[2] == 0);
    CoXlTestRoadsPoints(g_recs, 1, 4, c, km);
    CHECK(c[1] == 1 && c[2] == 1 && c[3] == 0 && c[6] == 1);
    g_case = "points: one point past the points written is a range outside them";
    CoXlTestRoadsPoints(g_recs + 7 * 0x48, 1, 10, c, km);
    CHECK(c[5] == 1 && c[6] == 0);
    CoXlTestRoadsPoints(g_recs + 7 * 0x48, 1, 9, c, km);
    CHECK(c[5] == 0 && c[6] == 1);
    g_case = "points: the count op 0x0E takes for a chord, rounded down";
    Edge(0, 0, 0, 5, 0, 2, 3, 0, 0, 0); Edge(1, 0, 0, 4.99f, 0, 2, 3, 0, 0, 0); Edge(2, 0, 0, 3.0e9f, 0, 2, 3, 0, 0, 0);
    CoXlTestRoadsPoints(g_recs, 1, 0, c, km);
    CHECK(c[7] == 3);
    CoXlTestRoadsPoints(g_recs + 0x48, 1, 0, c, km);
    CHECK(c[7] == 2);
    CoXlTestRoadsPoints(g_recs + 2 * 0x48, 1, 0, c, km);
    CHECK(c[7] == 0);
}

static void Paths(void)
{
    uint32_t n[2]; double m[2];
    g_case = "paths: the length along the points beside the chords";
    static const float pts[2 * 6] = {0, 0, 50, 0, 100, 0, 1000, 0, 1000, 100, 1000, 900};
    Edge(0, 0, 500, 100, 500, 2, 3, 0, 3, 0);
    Edge(1, 0, 200, 100, 200, 2, 3, 3, 3, 8);
    Edge(2, 1000, 0, 1000, 50, 2, 3, 3, 6, 8);
    CoXlTestRoadsPaths(g_recs, 3, pts, 6, n, m);
    CHECK(n[0] == 2 && n[1] == 6 && Near(m[0], 100 + 900) && Near(m[1], 100 + 50));
    CoXlTestRoadsPaths(g_recs, 3, pts, 5, n, m); /* edge 2's range passes the points */
    CHECK(n[0] == 1 && n[1] == 3 && Near(m[0], 100) && Near(m[1], 100));
    CoXlTestRoadsPaths(g_recs, 3, NULL, 6, n, m);
    CHECK(n[0] == 0 && n[1] == 0 && m[0] == 0 && m[1] == 0);
}

/* Seven edges over nodes A..F: 0 and 1 meet at B; 2 (no points) lies between C and D; 3, 5 and 6 meet at E; 4 has no
 * nodes, and 5's first node is none too. */
static const float g_partPts[2 * 12] = {0, 0, 10, 0, 10, 0, 10, 20, 100, 0, 130, 0, 0, 500, 0, 540, 130, 0, 130, 50, 130, 50, 190, 50};
static void PartEdges(void)
{
    enum { A = 0x1000, B = 0x2000, C = 0x3000, D = 0x4000, E = 0x5000, F = 0x6000 };
    Edge(0, 0, 0, 0, 0, 2, 3, 0, 2, 0); Nodes(0, A, B);
    Edge(1, 0, 0, 0, 0, 2, 3, 2, 4, 0); Nodes(1, B, C);
    Edge(2, 0, 0, 0, 0, 2, 3, 4, 4, 0); Nodes(2, C, D);
    Edge(3, 0, 0, 0, 0, 2, 3, 4, 6, 0); Nodes(3, D, E);
    Edge(4, 0, 0, 0, 0, 2, 3, 6, 8, 0); Nodes(4, 0, 0);
    Edge(5, 0, 0, 0, 0, 2, 3, 8, 10, 0); Nodes(5, 0, E);
    Edge(6, 0, 0, 0, 0, 2, 3, 10, 12, 0); Nodes(6, E, F);
}
static void Parts(void)
{
    uint32_t comp[64], largest[2]; double metres;
    g_case = "parts: edges with points joined at their nodes; no node and no points join nothing";
    PartEdges();
    CHECK(CoXlTestRoadsParts(g_recs, 7, g_partPts, 12, comp, largest, &metres) == 3);
    CHECK(comp[0] == 0 && comp[1] == 0 && comp[2] == UINT32_MAX && comp[3] == 1 && comp[4] == 2 && comp[5] == 1 && comp[6] == 1);
    CHECK(largest[0] == 1 && largest[1] == 3 && Near(metres, 30 + 50 + 60));
    g_case = "parts: of equals the first is the largest";
    CHECK(CoXlTestRoadsParts(g_recs, 6, g_partPts, 12, comp, largest, &metres) == 3);
    CHECK(largest[0] == 0 && largest[1] == 2 && Near(metres, 10 + 20));
    g_case = "parts: none without points";
    CHECK(CoXlTestRoadsParts(g_recs, 7, g_partPts, 0, comp, largest, &metres) == 0);
    CHECK(comp[0] == UINT32_MAX && comp[6] == UINT32_MAX && largest[0] == UINT32_MAX && largest[1] == 0 && metres == 0);

    g_case = "parts: how many hold the places' nearest paths";
    float out[8]; uint32_t part[4];
    static const float xy[2 * 4] = {110, -4, 5, 3, 133, 30, 2, 520}; /* beside edges 3, 0, 5 and 4 */
    CoXlTestRoadsReach(xy, 3, g_recs, 7, g_partPts, 12, part, out);
    CHECK(out[7] == 2 && part[0] == 1 && part[1] == 0 && part[2] == 1);
    CoXlTestRoadsReach(xy, 4, g_recs, 7, g_partPts, 12, part, out);
    CHECK(out[7] == 3 && part[3] == 2);
    CoXlTestRoadsReach(xy, 4, g_recs, 7, NULL, 0, part, out);
    CHECK(out[7] == 0 && part[0] == UINT32_MAX && part[3] == UINT32_MAX);

    g_case = "parts: an edge whose box holds the place but whose path is farther is not its nearest";
    static const float cross[2 * 4] = {0, 0, 100, 0, 0, 100, 100, -100};
    static const float in[2] = {10, -5};
    Edge(0, 0, 0, 0, 0, 2, 3, 0, 2, 0); Nodes(0, 0x1000, 0x2000);
    Edge(1, 0, 0, 0, 0, 2, 3, 2, 4, 0); Nodes(1, 0x3000, 0x4000);
    CoXlTestRoadsReach(in, 1, g_recs, 2, cross, 4, part, out);
    CHECK(Near(out[1], 5) && part[0] == 0);
}

static void Tsv(void)
{
    char line[16];
    g_case = "tsv: a line longer than its buffer is cut to it and keeps its newline";
    memset(line, 'x', sizeof line);
    CHECK(CoXlTestTsvLength(line, sizeof line, -1) == 0);
    CHECK(CoXlTestTsvLength(line, sizeof line, 15) == 15 && line[14] == 'x');
    CHECK(CoXlTestTsvLength(line, sizeof line, 16) == 15 && line[14] == '\n' && line[13] == 'x');
    line[14] = 'x';
    CHECK(CoXlTestTsvLength(line, sizeof line, 279) == 15 && line[14] == '\n');
}

static void Reach(void)
{
    float out[8];
    /* edge 0 along y = 0 through its points (its ends say otherwise), edge 1 the skeleton's straight line at y = 200
     * without points, edge 2 a skeleton edge with points at x = 1000 */
    static const float pts[2 * 6] = {0, 0, 50, 0, 100, 0, 1000, 0, 1000, 100, 1000, 900};
    Edge(0, 0, 500, 100, 500, 2, 3, 0, 3, 0);
    Edge(1, 0, 200, 100, 200, 2, 3, 3, 3, 8);
    Edge(2, 1000, 0, 1000, 50, 2, 3, 3, 6, 8);
    /* beside, past the end of and before the start of edge 0's points */
    static const float xy[2 * 4] = {50, 30, 150, 10, 50, -40, -30, 40};

    g_case = "reach: the nearest drawn path, along its points";
    CoXlTestRoadsReach(xy, 4, g_recs, 2, pts, 6, NULL, out);
    CHECK(out[0] == 4 && Near(out[1], 50) && Near(out[2], sqrt(50.0 * 50 + 10 * 10)));
    CHECK(out[3] == -1 && out[4] == -1);                                   /* no skeleton edge with points */
    CHECK(Near(out[5], 196.4688) && Near(out[6], 240));                     /* the skeleton's line at y = 200 */

    g_case = "reach: the skeleton along its points";
    CoXlTestRoadsReach(xy, 4, g_recs, 3, pts, 6, NULL, out);
    CHECK(Near(out[1], 50) && Near(out[2], sqrt(50.0 * 50 + 10 * 10)));
    CHECK(Near(out[3], sqrt(950.0 * 950 + 40 * 40)) && Near(out[4], 1030));
    CHECK(Near(out[5], 196.4688) && Near(out[6], 240));

    g_case = "reach: a farther box's edge does not hide a nearer path";
    static const float split[2 * 4] = {0, 0, 10, 0, -5000, 5000, 5000, 5000};
    Edge(0, 0, 0, 0, 0, 2, 3, 0, 2, 0);  /* the short one, read first: 4000 m */
    Edge(1, 0, 0, 0, 0, 2, 3, 2, 4, 0);  /* a long edge 1000 m away, though its first point is 5104 m away */
    static const float one[2] = {5, 4000};
    CoXlTestRoadsReach(one, 1, g_recs, 2, split, 4, NULL, out);
    CHECK(Near(out[1], 1000) && Near(out[2], 1000));

    g_case = "reach: an even count's median is the upper one; no places, no points";
    static const float four[2 * 4] = {10, 1, 20, 2, 30, 3, 40, 4};
    static const float line[2 * 2] = {0, 0, 100, 0};
    Edge(0, 0, 0, 0, 0, 2, 3, 0, 2, 0);
    CoXlTestRoadsReach(four, 4, g_recs, 1, line, 2, NULL, out);
    CHECK(Near(out[1], 3) && Near(out[2], 4));
    CoXlTestRoadsReach(four, 0, g_recs, 1, line, 2, NULL, out);
    CHECK(out[0] == 0 && out[1] == -1 && out[2] == -1 && out[5] == -1);
    CoXlTestRoadsReach(four, 4, g_recs, 1, NULL, 0, NULL, out);
    CHECK(out[1] == -1 && out[3] == -1);
    CoXlTestRoadsReach(four, 4, g_recs, 1, line, 1, NULL, out); /* the range past the points read */
    CHECK(out[1] == -1);
}

static void Disc(void)
{
    double r;
    g_case = "disc: world x fraction / 2";
    CHECK(Near(CoXlTestRoadsDisc(16384, 0.8f, &r), 3.14159265358979 * 6553.6 * 6553.6 / 1e6) && Near(r, 6553.6));
    CHECK(Near(CoXlTestRoadsDisc(2048, 2.0f, &r), 3.14159265358979 * 2048.0 * 2048 / 1e6) && Near(r, 2048));
    g_case = "disc: not a world's values";
    CHECK(CoXlTestRoadsDisc(0, 0.8f, &r) == 0 && r == 0);
    CHECK(CoXlTestRoadsDisc(65537, 0.8f, &r) == 0 && CoXlTestRoadsDisc(65536, 0.8f, &r) > 0);
    CHECK(CoXlTestRoadsDisc(2048, 0.0f, &r) == 0 && CoXlTestRoadsDisc(2048, NAN, &r) == 0 && CoXlTestRoadsDisc(2048, 2.01f, &r) == 0);
}

/* The generation block: counts at +0x11A7108 (locations), +0x11A71CC (edges), +0x11A71D4 (points); the edges, the
 * points and the locations where HellForge's layout and the hash pass's address put them. */
#define EDGES_AT 0x11A8000u
#define POINTS_AT 0x11B0000u
#define LOCATIONS_AT 0x10000u
#define IMPORTANT_AT 0x20000u
#define BLOCK_SIZE 0x11C0000u
static uint8_t* g_block;
static uint8_t g_record[0x100], g_settings[0x100], g_region[0x940];
static uint64_t g_context[2];

static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Layout(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u; c->moved = 1;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x11A7280u + 0x1000u * i; c->regions[i].places = 100u; }
    c->regions[HF_GEN_LOCATIONS_A].places = 65024u;
    c->regions[HF_GEN_IMPORTANT].at = IMPORTANT_AT;
    c->regions[HF_GEN_ROUTE_EDGES].at = EDGES_AT; c->regions[HF_GEN_ROUTE_POINTS].at = POINTS_AT; c->regions[HF_GEN_ROUTE_POINTS].places = 65520u;
    g_fake.levelRow = 0x1000;
    co_capacity_init();
    co_capacity_poll(1);
    Put32(g_fake.image + 0xC176A7 + 3, LOCATIONS_AT + 0x90u);
    CoXlTestGame((uintptr_t)g_fake.image);
}

/* A level: 3 edges (the one between the others without points), 5 points, 5 locations (2 objectives, an outpost, one
 * of op 0x17 whose important record is an extraction's, one of op 0x2B), 120 planned */
static void Level(void)
{
    CoXlTestGenLevel(8.0f);
    Put32(g_block + 0x11A71CCu, 3); Put32(g_block + 0x11A71D4u, 5); Put32(g_block + 0x11A7108u, 5); Put32(g_block + 0x11A715Cu, 4);
    static const float pts[2 * 5] = {0, 0, 100, 0, 200, 0, 0, 1000, 0, 2000};
    memcpy(g_block + POINTS_AT, pts, sizeof pts);
    Edge(0, 0, 0, 100, 0, 2, 3, 0, 3, 0); Nodes(0, 0x1000, 0x2000); /* 200 m along its points */
    Edge(1, 0, 1000, 0, 2000, 2, 3, 3, 5, 8); Nodes(1, 0x3000, 0x4000);
    Edge(2, 0, 0, 0, 1000, 10, 3, 5, 5, 0x18); Nodes(2, 0x2000, 0x3000);
    memcpy(g_block + EDGES_AT, g_recs, 3 * 0x48);
    static const float xy[2 * 5] = {100, 30, 50, 1500, 150, -60, 120, 40, 3000, 3000};
    for (uint32_t i = 0; i < 5; i++) memcpy(g_block + LOCATIONS_AT + 0xE0u * i, &xy[2 * i], 8);
    memset(g_block + IMPORTANT_AT, 0, 5 * 0x108);
    g_block[IMPORTANT_AT + 3 * 0x108 + 0xDC] = 6;
    g_block[IMPORTANT_AT + 4 * 0x108 + 0xDC] = 6; /* past the 4 important records: not one */
    static const uint8_t ops[5] = {0x16, 0x16, 0x82, 0x17, 0x2B};
    CoXlTestRoadsLevel(ops, 5, 120);
}

static void Glue(void)
{
    g_block = (uint8_t*)VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_block == NULL) { printf("roads: no memory\n"); g_failed++; return; }
    Layout();
    const uintptr_t block = (uintptr_t)g_block;
    const uint64_t record = (uint64_t)(uintptr_t)g_record, context = (uint64_t)(uintptr_t)g_context;
    Put32(g_record + 8 + 0x78, 65520);
    g_context[1] = (uint64_t)(uintptr_t)g_settings;
    const uint64_t region = (uint64_t)(uintptr_t)g_region;
    memcpy(g_settings + 0xB0, &region, 8); Put32(g_settings + 0x2C, 16384); { float f = 0.8f; memcpy(g_settings + 0x50, &f, 4); }
    g_region[0x678] = 1;
    for (uint32_t t = 0; t < 9; t++) Put32(g_region + 0x5C + 4 * t, 0xA0000000u + t);
    { float f = 0.25f; memcpy(g_region + 0x938, &f, 4); }

    g_case = "lines: each op's, once a level";
    Level();
    g_fake.log[0] = 0;
    CoXlTestRoadsAfterOp(block, context, record, 0x86, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x86, 0, 1);
    CHECK(Lines("after op 0x86: edge widths 2.00 m x 2, 10.00 m x 1 (3 edges, 2 with flag 0x8)") == 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0E, 0, 1);
    CHECK(Lines("after op 0x0E: 5 road points (the pass's capacity 65520, the block's room 65520, point ids reach 65535); 3 edges, 3 drawable "
        "(2.1 km), 1 of them without a point pair (1.0 km; 1 with flag 0x8): 1 with no point, 0 with one, 0 with a range outside the points; "
        "at op 0x0E's spacing they take 202 points") == 1);
    CHECK(Lines("after op 0x0E: 2 edges with a point pair hold 5 points over 1.2 km along the points, 1.1 km as straight lines: "
        "4.2 points per km along, 4.5 per km straight") == 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0F, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0F, 0, 1);
    CHECK(Lines("after op 0x0F: edge widths") == 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x05, 0, 1);
    CHECK(Lines("roads: ") == 4);
    CoXlTestRoadsAfterOp(block, context, record, 0x0D, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0D, 0, 1);
    CHECK(Lines("after op 0x0D: edge widths 2.00 m x 2, 10.00 m x 1 (3 edges, 2 with flag 0x8)") == 1 && Lines("roads: ") == 5);
    CoXlTestRoadsAfterOp(block, context, record, 0x2B, 4, 1);
    CHECK(Lines("after op 0x2B: edge widths 2.00 m x 2, 10.00 m x 1 (3 edges, 2 with flag 0x8)") == 1);
    CHECK(Lines("region: +0x678 = 1 (op 0xB7 levels routes at 2), +0x938 = 0.250; ribbon material by edge type: 0:A0000000 1:A0000001 2:A0000002 "
        "3:A0000003 4:A0000004 5:A0000005 6:A0000006 7:A0000007 8:A0000008") == 1);
    /* objectives (100, 30) and (50, 1500): paths 30 and 50 m; the skeleton's points 975 and 50 m; its lines 100 and 50 m */
    CHECK(Lines("reach: 2 objectives (op 0x16): nearest edge with points median 50 m, max 50 m; flag-0x8 edges along points median 975 m, max 975 m, "
        "as straight lines median 100 m, max 100 m; their nearest paths lie in 2 connected parts") == 1);
    /* the outpost (150, -60): 60 m from edge 0, 1071 m from edge 1's points, 162 m from edge 2's line */
    CHECK(Lines("reach: 1 outposts (op 0x82): nearest edge with points median 60 m, max 60 m; flag-0x8 edges along points median 1071 m, max 1071 m, "
        "as straight lines median 162 m, max 162 m; their nearest paths lie in 1 connected parts") == 1);
    /* (120, 40), by op 0x17 and as the extraction: 40 m from edge 0, 967 m from edge 1's points, 120 m from edge 2's line */
    CHECK(Lines("reach: 1 important locations (op 0x17): nearest edge with points median 40 m, max 40 m; flag-0x8 edges along points median 967 m, "
        "max 967 m, as straight lines median 120 m, max 120 m; their nearest paths lie in 1 connected parts") == 1);
    CHECK(Lines("reach: 1 extractions (important kind 6 or its definition's bit 0x80): nearest edge with points median 40 m, max 40 m; ") == 1);
    /* edges 0 and 1 do not meet: edge 2 between them has no points; three of the four places are nearest to edge 0 */
    CHECK(Lines("network: 2 edges with points in 2 connected parts (the largest: 1 edges, 0.2 km along its points); the nearest paths of 4 places "
        "(objectives, outposts, op 0x17) lie in 2 parts, 3 of the places at the largest") == 1);
    CHECK(Lines("density: op 0x2B placed 1 locations (4 -> 5) and 120 in the stamp plan on the whole map; against the playable disc (world 16384 m, "
        "radius 6554 m, 134.93 km2) that is 0.9 per km2") == 1);
    CHECK(Lines("cost: the lines after op 0x2B took ") == 1 && Lines(" ms of that op's noted time") == 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x2B, 4, 1);
    CHECK(Lines("region: ") == 1 && Lines("reach: ") == 4 && Lines("network: ") == 1 && Lines("density: ") == 1 && Lines("cost: ") == 1);
    CHECK(Lines("after op 0x2B: edge widths") == 1);

    g_case = "lines: the next level has its own; not counted, no reach";
    Level();
    g_fake.log[0] = 0;
    CoXlTestRoadsAfterOp(block, context, record, 0x86, 0, 0);
    CoXlTestRoadsAfterOp(block, context, record, 0x2B, 4, 0);
    CHECK(Lines("after op 0x86: edge widths") == 1 && Lines("region: ") == 1 && Lines("reach: ") == 0 && Lines("density: ") == 1);
    CHECK(Lines("network: 2 edges with points in 2 connected parts (the largest: 1 edges, 0.2 km along its points); the nearest paths of 0 places "
        "(objectives, outposts, op 0x17) lie in 0 parts, 0 of the places at the largest") == 1);

    g_case = "lines: nothing readable";
    Level();
    g_fake.log[0] = 0;
    g_context[1] = 0;
    Put32(g_block + 0x11A71CCu, 70000);
    CoXlTestRoadsAfterOp(block, context, record, 0x86, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0E, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x2B, 4, 1);
    CHECK(Lines("after op 0x86: the route edges are not readable") == 1 && Lines("after op 0x0E: the route edges are not readable") == 1);
    CHECK(Lines("after op 0x2B: the route edges are not readable") == 1 && Lines("network: the route edges are not readable") == 1);
    CHECK(Lines("region: not readable") == 1 && Lines("reach: the route edges or the locations are not readable") == 1);
    CHECK(Lines("density: op 0x2B placed 1 locations (4 -> 5) and 120 in the stamp plan on the whole map; the playable disc is not readable") == 1);

    g_case = "lines: the points not readable";
    Level();
    g_fake.log[0] = 0;
    Put32(g_block + 0x11A71D4u, (1u << 20) + 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x0E, 0, 1);
    CoXlTestRoadsAfterOp(block, context, record, 0x2B, 4, 1);
    CHECK(Lines("after op 0x0E: the road points are not readable") == 1);
    CHECK(Lines("network: 0 edges with points in 0 connected parts (the largest: 0 edges, 0.0 km along its points); the nearest paths of 4 places "
        "(objectives, outposts, op 0x17) lie in 0 parts, 0 of the places at the largest") == 1);
    VirtualFree(g_block, 0, MEM_RELEASE);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    Widths();
    Points();
    Paths();
    Parts();
    Tsv();
    Reach();
    Disc();
    Glue();
    if (g_failed) { printf("roads: %d check(s) failed\n", g_failed); return 1; }
    printf("roads: all checks passed\n");
    return 0;
}
