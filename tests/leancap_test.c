/* Four-corner units' lean on complex levels (game 0x624750, the rotation's limit on a unit's up): what the game leaves
 * is held within the drawn ground's slope and 25 degrees of the world's up, for the four-corner pose's call only and
 * where the corner ray read just before on this thread starts. The bone-ray modes' call, any other caller, a normal
 * level, a limit of 0, no reading, no live tile and a busy tile list keep the game's answer. The game's call is always
 * made as the game made it. The hooked function is entered from copies of the game's two call sites, so the detour
 * runs with the return addresses it tells them apart by.
 * Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestLeanInstall(uintptr_t game);
void CoXlTestLeanSet(float deg);
float CoXlTestLeanDeg(void);
int CoXlTestLeanHold(float* up, const float* w, float deg);
float CoXlTestLeanAllowed(float slope, float over);
int CoXlTestLeanSlope(float x, float y, float* deg);
void CoXlTestLeanAt(const float* origin);
void CoXlTestLeanTile(float cx, float cy, float z, float gx, float gy);
void CoXlTestLeanBehind(float cx, float cy);
void CoXlTestLeanTilesOff(void);
void CoXlTestLeanCounts(long* c); /* poses, the game's, ours, no ground, bone-ray calls, other calls */
void CoXlTestLeanNote(void);
void CoXlTestTiltTile(float cx, float cy, float z);
void CoXlTestTiltHold(int on);
void CoXlTestLpLevel(void);
void CoXlTestLpScan(void);
int CoXlTestLeanWatch(const wchar_t* path);
int CoXlTestLpSample(uint32_t id, const float* up, float tilt, float ground, float off, const float* last, float len);
void CoXlTestLpNote(void);
void CoXlTestFgWorld(float live);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define LIMIT 0x624750u
#define FOUR 0x622D6Bu /* the four-corner pose's call */
#define BONE 0x621EAFu /* the bone-ray modes' call */
#define UP 0x328CED0u
#define RAD 57.29578f

typedef float* (*LimitFn)(uintptr_t mgr, float* out, float* up);

/* The game's limit as the fake: what it was handed; its answer is the up it was given, written to out and returned. */
static struct { int calls; uintptr_t mgr; float *out, *up; } g_limit;
static float g_during = -1.0f; /* degrees set while the game's call runs, as the research switch may; under 0: none */
static float* FakeLimit(uintptr_t mgr, float* out, float* up)
{
    g_limit.calls++; g_limit.mgr = mgr; g_limit.out = out; g_limit.up = up;
    if (g_during >= 0.0f) CoXlTestLeanSet(g_during);
    memcpy(out, up, 12);
    return out;
}

static void Hex(uint8_t* out, const char* hex)
{
    unsigned v;
    while (sscanf(hex, " %2x", &v) == 1) { *out++ = (uint8_t)v; hex += hex[0] == ' ' ? 3 : 2; }
}

static const struct { uint32_t rva; const char* bytes; } g_pins[] = {
    {LIMIT, "48 8B C4 48 89 58 08 57 48 81 EC B0 00 00 00 F3 41 0F 10 58 04 49 8B D8"},
    {0x6247CAu, "F3 0F 10 05 06 24 DA 01 F3 0F 58 D1 0F 2F C2"},
    {0x23C6BD8u, "33 33 33 3F"},
    {0x6248CEu, "F2 0F 10 05 4A 27 DA 01"},
    {0x23C7020u, "00 00 00 60 66 66 E6 3F"},
    {0x6249AFu, "F3 44 0F 11 0B F3 0F 11 6B 04 F3 0F 11 53 08"},
    {0x6249DDu, "F2 0F 10 03 4C 8D 9C 24 B0 00 00 00 8B 4B 08 48 8B C7"},
    {0x622D7Au, "F2 44 0F 10 00 8B 58 08"},
    {0x6227A8u, "E8 63 E0 FF FF"},
    {FOUR, "E8 E0 19 00 00"},
    {BONE, "E8 9C 28 00 00"},
    {0x624738u, "DD 14 62 00 DD 14 62 00 F1 26 62 00"},
};
#define PINS (sizeof g_pins / sizeof g_pins[0])

static void Fresh(void)
{
    const float up[3] = {0, 0, 1};
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    for (size_t i = 0; i < PINS; i++) Hex(g_fake.image + g_pins[i].rva, g_pins[i].bytes);
    memcpy(g_fake.image + UP, up, sizeof up);
    g_fake.originals[0].target = (uintptr_t)g_fake.image + LIMIT;
    g_fake.originals[0].original = (void*)FakeLimit;
}

/* A call site made whole: sub rsp, 0x28; the game's call; add rsp, 0x28; ret. And the loader's jump at the limit's
 * entry: mov rax, detour; jmp rax. */
static LimitFn Site(uint32_t call) { Hex(g_fake.image + call - 4, "48 83 EC 28"); Hex(g_fake.image + call + 5, "48 83 C4 28 C3"); return (LimitFn)(g_fake.image + call - 4); }
static void Jump(void* detour)
{
    uint8_t* at = g_fake.image + LIMIT;
    at[0] = 0x48; at[1] = 0xB8; memcpy(at + 2, &detour, 8); at[10] = 0xFF; at[11] = 0xE0;
}

/* A unit vector `deg` from +z toward the azimuth `az` degrees. */
static void Lean(float* v, float deg, float az)
{
    v[0] = sinf(deg / RAD) * cosf(az / RAD); v[1] = sinf(deg / RAD) * sinf(az / RAD); v[2] = cosf(deg / RAD);
}
static int Is(const float* v, float deg, float az)
{
    float w[3];
    Lean(w, deg, az);
    return fabsf(v[0] - w[0]) < 1e-4f && fabsf(v[1] - w[1]) < 1e-4f && fabsf(v[2] - w[2]) < 1e-4f;
}

/* The same within 0.1 deg: a slope read from the tile's 16-bit heights. */
static int About(const float* v, float deg, float az)
{
    float w[3];
    Lean(w, deg, az);
    return fabsf(v[0] - w[0]) < 2e-3f && fabsf(v[1] - w[1]) < 2e-3f && fabsf(v[2] - w[2]) < 2e-3f;
}

static float g_out[3], g_up[3];
static const float g_at[3] = {103.0f, -205.0f, 60.0f}; /* where the unit's corner ray starts: on the test tile */
/* One call through a site with a unit `deg` from upright, after a corner reading at g_at: 1 when the game's call was
 * made once, as made, and the site got the game's pointer back. */
static int Through(LimitFn site, float deg, float az)
{
    CoXlTestLeanAt(g_at);
    memset(&g_limit, 0, sizeof g_limit);
    g_out[0] = g_out[1] = g_out[2] = 9.0f;
    Lean(g_up, deg, az);
    float* r = site(0x1234, g_out, g_up);
    return r == g_out && g_limit.calls == 1 && g_limit.mgr == 0x1234 && g_limit.out == g_out && g_limit.up == g_up;
}

/* The file with this text and a write time of its own: the watch reads on a new time only. */
static void Put(const wchar_t* path, const char* text)
{
    static FILETIME t = {0, 0x01DC0000};
    DWORD put;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, text, (DWORD)strlen(text), &put, NULL);
    t.dwLowDateTime += 20000000; SetFileTime(f, NULL, NULL, &t);
    CloseHandle(f);
}

int main(void)
{
    const float z[3] = {0, 0, 1}, y[3] = {0, 1, 0};
    float v[3], slope;
    long c[6];

    g_case = "the rule";
    Lean(v, 24.9f, 70.0f);
    CHECK(!CoXlTestLeanHold(v, z, 25.0f) && Is(v, 24.9f, 70.0f)); /* inside: left */
    Lean(v, 25.1f, 70.0f);
    CHECK(CoXlTestLeanHold(v, z, 25.0f) && Is(v, 25.0f, 70.0f));  /* just past: held, toward the same side */
    Lean(v, 46.0f, -130.0f);
    CHECK(CoXlTestLeanHold(v, z, 25.0f) && Is(v, 25.0f, -130.0f));
    Lean(v, 120.0f, 10.0f);
    CHECK(CoXlTestLeanHold(v, z, 10.0f) && Is(v, 10.0f, 10.0f));  /* past level */
    Lean(v, 46.0f, 30.0f); v[0] *= 0.7f; v[1] *= 0.7f; v[2] *= 0.7f;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f) && fabsf(v[2] - 0.7f * cosf(46.0f / RAD)) < 1e-5f); /* not a unit vector: left */
    Lean(v, 46.0f, 30.0f); v[0] *= 1.02f; v[1] *= 1.02f; v[2] *= 1.02f;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f));
    v[0] = 0; v[1] = 0; v[2] = 0.7f;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f) && v[2] == 0.7f);        /* the game's answer to no up at all */
    v[0] = 0; v[1] = 0; v[2] = -1;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f) && v[2] == -1.0f);       /* straight down: no side to lean to */
    v[0] = v[1] = v[2] = 0;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f) && v[2] == 0.0f);
    v[0] = NAN; v[1] = 0; v[2] = 0.5f;
    CHECK(!CoXlTestLeanHold(v, z, 25.0f));
    v[0] = 1; v[1] = 0; v[2] = 0;
    CHECK(CoXlTestLeanHold(v, y, 30.0f) && fabsf(v[0] - 0.5f) < 1e-4f && fabsf(v[1] - cosf(30.0f / RAD)) < 1e-4f && fabsf(v[2]) < 1e-4f); /* about the up given */

    g_case = "the lean allowed";
    CHECK(CoXlTestLeanAllowed(0.0f, 25.0f) == 25.0f && CoXlTestLeanAllowed(10.0f, 25.0f) == 35.0f);
    CHECK(CoXlTestLeanAllowed(20.5f, 25.0f) == 45.5f);
    CHECK(fabsf(CoXlTestLeanAllowed(20.6f, 25.0f) - 45.573f) < 1e-4f && fabsf(CoXlTestLeanAllowed(60.0f, 25.0f) - 45.573f) < 1e-4f); /* never past the game's */

    g_case = "the degrees asked for";
    CoXlTestLeanSet(25.0f); CHECK(CoXlTestLeanDeg() == 25.0f);
    CoXlTestLeanSet(0.0f); CHECK(CoXlTestLeanDeg() == 0.0f);
    CoXlTestLeanSet(-3.0f); CHECK(CoXlTestLeanDeg() == 0.0f);
    CoXlTestLeanSet(NAN); CHECK(CoXlTestLeanDeg() == 0.0f);
    CoXlTestLeanSet(2.0f); CHECK(CoXlTestLeanDeg() == 5.0f);
    CoXlTestLeanSet(60.0f); CHECK(CoXlTestLeanDeg() == 45.0f);

    g_case = "not the reviewed code";
    for (size_t i = 0; i < PINS; i++)
    {
        Fresh();
        g_fake.image[g_pins[i].rva + 1] ^= 0x40;
        CHECK(!CoXlTestLeanInstall((uintptr_t)g_fake.image));
        CHECK(fake_host_detour(LIMIT) == NULL);
        CHECK(fake_host_logged("the rotation's lean limit is not the reviewed code"));
        if (g_failed) { printf("  (pin %u at 0x%X)\n", (unsigned)i, g_pins[i].rva); break; }
    }

    g_case = "the hook refused";
    Fresh();
    g_fake.noHook = LIMIT;
    CHECK(!CoXlTestLeanInstall((uintptr_t)g_fake.image));
    CHECK(fake_host_logged("the rotation's lean limit could not be hooked"));

    g_case = "hooked";
    Fresh();
    CHECK(CoXlTestLeanInstall((uintptr_t)g_fake.image));
    CHECK(CoXlTestLeanDeg() == 25.0f);
    CHECK(fake_host_logged("four-corner units lean at most their ground's slope and 25 deg from upright on complex levels"));
    void* detour = fake_host_detour(LIMIT);
    CHECK(detour != NULL);
    if (detour == NULL) return 1;
    DWORD old;
    if (!VirtualProtect(g_fake.image + 0x621000u, 0x4000, PAGE_EXECUTE_READWRITE, &old)) { printf("FAILED: the image's code could not be made to run\n"); return 1; }
    const LimitFn four = Site(FOUR), bone = Site(BONE);
    Jump(detour);
    FlushInstructionCache(GetCurrentProcess(), g_fake.image + 0x621000u, 0x4000);

    g_case = "the drawn ground's slope";
    CoXlTestFgWorld(8.0f);
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, 0.0f, 0.0f);
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope) && fabsf(slope) < 0.1f);
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, tanf(10.0f / RAD), 0.0f);
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope) && fabsf(slope - 10.0f) < 0.1f);
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, 0.0f, -tanf(15.0f / RAD));
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope) && fabsf(slope - 15.0f) < 0.1f);
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, 0.3f, 0.4f);
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope) && fabsf(slope - atanf(0.5f) * RAD) < 0.1f); /* both ways at once */
    CHECK(!CoXlTestLeanSlope(100.0f + 40.0f, -200.0f, &slope)); /* in its square, past its heights */
    CHECK(!CoXlTestLeanSlope(100.0f + 1030.0f, -200.0f, &slope)); /* outside its square */
    CoXlTestTiltHold(1);
    CHECK(!CoXlTestLeanSlope(103.0f, -205.0f, &slope)); /* a writer holds the tiles */
    CoXlTestTiltHold(0);
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope));
    CoXlTestLeanBehind(5000.0f, 5000.0f);
    CHECK(CoXlTestLeanSlope(103.0f, -205.0f, &slope) && fabsf(slope - atanf(0.5f) * RAD) < 0.1f); /* the tile it is on, not the first live one */
    CHECK(CoXlTestLeanSlope(5003.0f, 5005.0f, &slope) && fabsf(slope) < 0.1f);
    CoXlTestLeanTilesOff();
    CHECK(!CoXlTestLeanSlope(103.0f, -205.0f, &slope)); /* moving tiles off: the list is not this level's to read */
    CoXlTestTiltTile(0, 0, NAN);
    CHECK(!CoXlTestLeanSlope(103.0f, -205.0f, &slope)); /* no tiles */

    g_case = "complex level, flat ground, the four-corner call, past the limit";
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, 0.0f, 0.0f);
    CHECK(Through(four, 40.0f, 200.0f));
    CHECK(Is(g_out, 25.0f, 200.0f) && Is(g_up, 25.0f, 200.0f)); /* both of the game's answers */
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[1] == 0 && c[2] == 1 && c[3] == 0 && c[4] == 0 && c[5] == 0);

    g_case = "past the game's limit";
    CHECK(Through(four, 50.0f, 15.0f)); /* the fake leaves it; the game would hand 45.57 deg on */
    CHECK(Is(g_out, 25.0f, 15.0f) && Is(g_up, 25.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[1] == 1 && c[2] == 1);

    g_case = "inside the limit";
    CHECK(Through(four, 24.0f, 15.0f));
    CHECK(Is(g_out, 24.0f, 15.0f) && Is(g_up, 24.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[1] == 0 && c[2] == 0 && c[3] == 0);

    g_case = "on a slope: its angle more";
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, tanf(10.0f / RAD), 0.0f);
    CHECK(Through(four, 34.0f, 90.0f) && Is(g_out, 34.0f, 90.0f)); /* whichever way it leans */
    CHECK(Through(four, 40.0f, 90.0f) && About(g_out, 35.0f, 90.0f) && About(g_up, 35.0f, 90.0f) && !About(g_out, 35.3f, 90.0f));
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, tanf(30.0f / RAD), 0.0f);
    CHECK(Through(four, 45.5f, 0.0f) && Is(g_out, 45.5f, 0.0f)); /* a steep slope: the game's limit alone */
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 3 && c[2] == 1 && c[3] == 0);

    g_case = "not a unit vector: the game's";
    CoXlTestLeanTile(100.0f, -200.0f, 60.0f, 0.0f, 0.0f);
    CoXlTestLeanAt(g_at);
    g_up[0] = 0; g_up[1] = 0.6f; g_up[2] = 0.3f;
    CHECK(four(0x1234, g_out, g_up) == g_out && g_out[1] == 0.6f && g_out[2] == 0.3f && g_up[1] == 0.6f);
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[2] == 0 && c[3] == 0);

    g_case = "no corner reading before it: the game's";
    memset(&g_limit, 0, sizeof g_limit);
    Lean(g_up, 40.0f, 15.0f);
    CHECK(four(0x1234, g_out, g_up) == g_out && g_limit.calls == 1 && Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f)); /* the last pose took the reading */
    CoXlTestLeanAt(g_at); CoXlTestLeanAt(NULL); /* a reading that did not read */
    Lean(g_up, 40.0f, 15.0f);
    CHECK(four(0x1234, g_out, g_up) == g_out && Is(g_out, 40.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 2 && c[2] == 0 && c[3] == 2);

    g_case = "a bone-ray pose takes the reading with it";
    CHECK(Through(bone, 40.0f, 15.0f));
    CHECK(Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f));
    Lean(g_up, 40.0f, 15.0f);
    CHECK(four(0x1234, g_out, g_up) == g_out && Is(g_out, 40.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[2] == 0 && c[3] == 1 && c[4] == 1 && c[5] == 0);

    g_case = "another caller: the game's";
    CoXlTestLeanAt(g_at);
    memset(&g_limit, 0, sizeof g_limit);
    Lean(g_up, 40.0f, 15.0f);
    CHECK(((LimitFn)detour)(0x1234, g_out, g_up) == g_out && g_limit.calls == 1 && Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 0 && c[2] == 0 && c[4] == 0 && c[5] == 1);

    g_case = "off the live tile, or its list busy: the game's";
    const float away[3] = {100.0f + 1030.0f, -200.0f, 60.0f};
    CoXlTestLeanAt(away);
    Lean(g_up, 40.0f, 15.0f);
    CHECK(four(0x1234, g_out, g_up) == g_out && Is(g_out, 40.0f, 15.0f));
    CoXlTestTiltHold(1);
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f));
    CoXlTestTiltHold(0);
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 25.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 3 && c[2] == 1 && c[3] == 2);

    g_case = "any other level: the game's";
    CoXlTestFgWorld(1.0f);
    CHECK(Through(four, 40.0f, 15.0f));
    CHECK(Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f));
    CHECK(Through(four, 50.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 2 && c[1] == 1 && c[2] == 0 && c[3] == 0); /* counted all the same */

    g_case = "0 degrees asked for: the game's";
    CoXlTestFgWorld(8.0f);
    CoXlTestLeanSet(0.0f);
    CHECK(Through(four, 40.0f, 15.0f));
    CHECK(Is(g_out, 40.0f, 15.0f) && Is(g_up, 40.0f, 15.0f));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 1 && c[2] == 0 && c[3] == 0);

    g_case = "other degrees";
    CoXlTestLeanSet(15.0f);
    CHECK(Through(four, 20.0f, 15.0f));
    CHECK(Is(g_out, 15.0f, 15.0f));
    CoXlTestLeanCounts(c);

    g_case = "the degrees change while a pose is under way: the pose keeps the ones it began with";
    CoXlTestLeanSet(25.0f);
    g_during = 0.0f;
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 25.0f, 15.0f) && CoXlTestLeanDeg() == 0.0f);
    g_during = 10.0f;
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 40.0f, 15.0f) && CoXlTestLeanDeg() == 10.0f); /* began with none */
    g_during = 25.0f;
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 10.0f, 15.0f));
    g_during = -1.0f;
    CoXlTestLeanCounts(c);

    g_case = "the 30 s line";
    CoXlTestLeanSet(25.0f);
    CoXlTestLeanNote();
    CHECK(!fake_host_logged("lean (30 s)")); /* nothing posed: no line */
    CHECK(Through(four, 50.0f, 15.0f) && Through(four, 30.0f, 15.0f) && Through(four, 3.0f, 15.0f) && Through(bone, 3.0f, 15.0f));
    Lean(g_up, 40.0f, 15.0f);
    four(0x1234, g_out, g_up);
    CoXlTestLeanNote();
    CHECK(fake_host_logged("lean (30 s): 4 four-corner poses, 1 held at the game's 45.6 deg, 2 at their ground's slope and 25 deg (0: none), "
        "1 left with no drawn ground read; the limit's other calls: 1 bone-ray, 0 from elsewhere"));
    CoXlTestLeanCounts(c);
    CHECK(c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0 && c[5] == 0); /* the counts start again */
    CoXlTestFgWorld(1.0f);
    CHECK(Through(four, 50.0f, 15.0f));
    CoXlTestLeanNote();
    CHECK(fake_host_logged("lean (30 s): 1 four-corner poses, 1 held at the game's 45.6 deg, 0 at their ground's slope and 0 deg"));

    g_case = "research: units at their limit";
    const float hit[4] = {0.5f, 0.6f, 0.4f, 0.5f}, flipped[4] = {0.5f, -0.2f, 4.0f, 0.5f};
    float a[3], b[3];
    CoXlTestFgWorld(8.0f);
    CoXlTestLpLevel();
    CoXlTestLpNote();
    CHECK(!fake_host_logged("lean probe (30 s)")); /* no sample: no line */
    Lean(a, 25.0f, 0.0f); Lean(b, 25.0f, 90.0f); /* 34.8 deg apart */
    CoXlTestLpScan();
    CHECK(CoXlTestLpSample(7, a, 25.0f, 0.0f, 25.0f, hit, 4.0f) == 1);  /* at its limit */
    CHECK(CoXlTestLpSample(8, a, 23.9f, 0.0f, 23.9f, hit, 4.0f) == 0);  /* over 1 deg under it */
    CHECK(CoXlTestLpSample(9, a, 27.1f, 0.0f, 27.1f, hit, 4.0f) == 1);  /* over it by more than 2 */
    CHECK(CoXlTestLpSample(12, a, 26.5f, 0.0f, 26.5f, hit, 4.0f) == 1); /* over it by less: the hold's own slack */
    CHECK(CoXlTestLpSample(10, a, 43.9f, 20.0f, 30.0f, hit, 4.0f) == 0); /* steep ground: the limit rises; 30 off it */
    CHECK(CoXlTestLpSample(11, a, 45.0f, 30.0f, 31.0f, hit, 4.0f) == 1); /* the game's limit; 30 off ground of 30 */
    CoXlTestLpScan();
    CHECK(CoXlTestLpSample(7, b, 25.0f, 0.0f, 25.0f, flipped, 4.0f) == 1); /* again: turned, two readings changed kind */
    CHECK(CoXlTestLpSample(8, a, 25.0f, 0.0f, 25.0f, hit, 4.0f) == 1);     /* at it now, not before: no pair */
    CHECK(CoXlTestLpSample(9, a, 10.0f, 0.0f, 10.0f, hit, 4.0f) == 0);
    CoXlTestLpScan();
    CHECK(CoXlTestLpSample(9, a, 25.0f, 0.0f, 25.0f, hit, 4.0f) == 1);     /* left the limit between: no pair */
    CoXlTestLpScan();
    CoXlTestLpScan();
    CHECK(CoXlTestLpSample(7, a, 25.0f, 0.0f, 25.0f, hit, 4.0f) == 1);     /* not seen last scan: no pair */
    CoXlTestLpNote();
    CHECK(fake_host_logged("lean probe (30 s): 11 four-corner samples on read ground, 1 over their limit by 2 deg or more (ground slope + 25, at most 45.6), 8 at it; "
        "30 deg or more off their ground 2, 1 of them on ground of 30 deg or more; at their limit two scans running 1: up turned 34.8 deg on average, 34.8 at most, 2 corner readings changed kind"));
    CoXlTestLeanSet(0.0f); /* a control run measures against the default */
    CoXlTestLpScan();
    CHECK(CoXlTestLpSample(20, a, 30.0f, 0.0f, 30.0f, hit, 4.0f) == 1);
    CoXlTestLpNote();
    CHECK(fake_host_logged("lean probe (30 s): 1 four-corner samples on read ground, 1 over their limit by 2 deg or more (ground slope + 25, at most 45.6), 1 at it; 30 deg or more off their ground 1, 0 of them"));

    g_case = "research: the degrees from a file, in play";
    wchar_t file[MAX_PATH];
    GetTempPathW(MAX_PATH, file); wcscat(file, L"xl-lean-test.txt");
    DeleteFileW(file);
    CoXlTestFgWorld(8.0f);
    CoXlTestLeanSet(25.0f);
    CoXlTestLeanCounts(c);
    CHECK(!CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 25.0f); /* no file: as it was */
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 25.0f, 15.0f));
    Put(file, "0\n");
    CHECK(CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 0.0f);
    CHECK(fake_host_logged("lean (30 s): 1 four-corner poses, 0 held at the game's 45.6 deg, 1 at their ground's slope and 25 deg")); /* the counts under 25, before */
    CHECK(fake_host_logged("lean: now 0 deg over the slope (0: none), from xl-lean.txt"));
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 40.0f, 15.0f)); /* the game's answer, untouched */
    CHECK(!CoXlTestLeanWatch(file)); /* not written again: not read again */
    Put(file, "twelve");
    CHECK(!CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 0.0f);
    CHECK(fake_host_logged("lean: xl-lean.txt \"twelve\" is not degrees; still 0"));
    Put(file, "-3");
    CHECK(!CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 0.0f);
    Put(file, "12");
    CHECK(CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 12.0f);
    CHECK(fake_host_logged("lean (30 s): 1 four-corner poses, 0 held at the game's 45.6 deg, 0 at their ground's slope and 0 deg"));
    CHECK(fake_host_logged("lean: now 12 deg over the slope"));
    CHECK(Through(four, 40.0f, 15.0f) && Is(g_out, 12.0f, 15.0f));
    DeleteFileW(file);
    CHECK(!CoXlTestLeanWatch(file) && CoXlTestLeanDeg() == 12.0f); /* taken away: the last degrees stay */

    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("leancap: all checks passed\n");
    return 0;
}
