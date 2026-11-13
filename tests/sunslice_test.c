/* The sun's last shadow slice. With every far part in (the exe's pins, the half-size hook, the levels hook and the snap
 * call's switched site), a world over 2048 m keeps the static slice at vanilla's +-1024 m and the box follows the camera,
 * with the whole slice drawn in the frame it moves. Without them (the cases on a fake game alone), mission start's
 * "use_static_last_slice = true" reaches the game as false on a world over 2048 m and as true on a 2048 m world.
 * The level is the one the game reads for the slice's half size: [settings - 0x7910270 + 0xAE288]. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

#include "xl/co_switch.h"

int CoXlTestSunInstall(uintptr_t game);
void CoXlTestSunRetry(void);
LONG CoXlTestSunDynamic(void);
int CoXlTestSunExePins(uint8_t* exe);
uint32_t CoXlTestSunExePin(int i);
int CoXlTestSunFar(void);
LONG CoXlTestSunFull(void);
int CoXlTestSunMission(void);
void CoXlTestSunSnap(uint64_t (*snap)(float*, const float*, uint64_t, uint64_t));
uint32_t CoXlTestSunLevels(uintptr_t back, uint32_t levels);
int CoXlTestSunCentre(float* c);
void CoXlTestSunWrite(int (*write)(uintptr_t, const void*, size_t));
void co_switch_test_reset(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SET_BOOL 0x10087C0u
#define SET_FLOAT 0x1008AE0u
#define HALF_CALL 0xAC6713u
#define SNAP_CALL 0x35FC12u
#define SNAP 0x359930u
#define LEVELS 0x35F140u
#define ENTER_AT 0xAC66D0u
#define HALF_AT 0xAC66F4u

typedef uint64_t (*SetBoolFn)(uint64_t settings, const char* name, uint64_t value);

/* What the game's writer was handed. */
static struct { int calls; uint64_t settings, value; const char* name; } g_got;
static uint64_t FakeSetBool(uint64_t settings, const char* name, uint64_t value)
{
    g_got.calls++; g_got.settings = settings; g_got.name = name; g_got.value = value;
    return 0x5E7;
}

/* The level the game reads at 0xAC66FB, and the world object whose +0xAE288 holds it. */
static uint8_t g_level[0x3000];
static uint64_t g_levelSlot;
static uint64_t Settings(void) { return (uint64_t)(uintptr_t)&g_levelSlot - 0xAE288u + 0x7910270u; }
static void Level(float size, float disk)
{
    memset(g_level, 0, sizeof g_level);
    memcpy(g_level + 0x1E0, &size, 4);
    memcpy(g_level + 0x2D7C, &disk, 4);
    g_levelSlot = (uint64_t)(uintptr_t)g_level;
}

static void Pins(uint8_t* image)
{
    static const uint8_t setBool[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
    static const uint8_t enter[] = {0x41, 0xB0, 0x01, 0x48, 0x8D, 0x15, 0x06, 0xE3, 0x78, 0x01, 0x49, 0x8D, 0x8F, 0x70, 0x02, 0x91, 0x07, 0xE8, 0xDA, 0x20,
        0x54, 0x00, 0x49, 0x8B, 0x87, 0x88, 0xE2, 0x0A, 0x00};
    static const uint8_t half[] = {0x49, 0x8D, 0x8F, 0x70, 0x02, 0x91, 0x07, 0xF3, 0x0F, 0x10, 0x90, 0xE0, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x90,
        0x7C, 0x2D, 0x00, 0x00};
    memcpy(image + SET_BOOL, setBool, sizeof setBool);
    memcpy(image + ENTER_AT, enter, sizeof enter);
    memcpy(image + HALF_AT, half, sizeof half);
}

/* The hook in, on the game's bytes; its detour, or NULL. */
static SetBoolFn Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    Pins(g_fake.image);
    g_fake.originals[0].target = (uintptr_t)g_fake.image + SET_BOOL;
    g_fake.originals[0].original = (void*)FakeSetBool;
    memset(&g_got, 0, sizeof g_got);
    if (CoXlTestSunInstall((uintptr_t)g_fake.image) != 1) return NULL;
    return (SetBoolFn)fake_host_detour(SET_BOOL);
}


/* ---- far shadows: every part in ---------------------------------------------------------------------------------- */

#define BACK1 0x363BDEu
#define BACK2 0x364235u

static struct { int calls; float value; const char* name; } g_float;
static uint64_t FakeSetFloat(uint64_t settings, const char* name, float value)
{
    g_float.calls++; g_float.name = name; g_float.value = value;
    return 0xF1;
}
static uint32_t FakeLevels(void) { return 3; }

/* What the game's snap was handed: the light camera's translation as it stood, and its arguments. */
static struct { int calls; float t[3]; float* light; const float* box; uint64_t w, h; } g_snap;
static uint64_t FakeSnap(float* light, const float* box, uint64_t w, uint64_t h)
{
    g_snap.calls++; memcpy(g_snap.t, light + 12, sizeof g_snap.t);
    g_snap.light = light; g_snap.box = box; g_snap.w = w; g_snap.h = h;
    return 0x5A;
}

typedef uint64_t (*SnapFn)(float*, const float*, uint64_t, uint64_t);
typedef uint64_t (*SetFloatFn)(uint64_t, const char*, float);

/* The prepare's frame as 0x35F1A0 holds it at the snap call: rbp, the light rows at rbp+0x30 (U, D, R, T), the box at
 * rbp+8, the levels at rbp-0x38, the tiles a side at rbp+0x74, the context at rbp+0x4B8. */
static uint8_t g_stack[0x800];
static uint8_t g_ctx[0x100], g_camera[0x200], g_clock[0x200];
#define RBP (g_stack + 0x100)
static float* Light(void) { return (float*)(RBP + 0x30); }
static uint32_t Tiles(void) { uint32_t v; memcpy(&v, RBP + 0x74, 4); return v; }
static uint64_t Levels(void) { uint64_t v; memcpy(&v, RBP - 0x38, 8); return v; }

/* The sun straight down: U = x, D = -z, R = y; the game's translation -D x half. */
static void Frame(float x, float y, float z, uint32_t counter)
{
    memset(g_stack, 0, sizeof g_stack);
    const float light[16] = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1024, 1};
    const float box[8] = {0, -1024, 1024, -1024, 1024, 0, 0, 1};
    memcpy(RBP + 0x30, light, sizeof light);
    memcpy(RBP + 0x8, box, sizeof box);
    const uint32_t tiles = 8; const uint64_t levels = 3;
    memcpy(RBP + 0x74, &tiles, 4); memcpy(RBP - 0x38, &levels, 8);
    const uint64_t ctx = (uint64_t)(uintptr_t)g_ctx, camera = (uint64_t)(uintptr_t)g_camera, clock = (uint64_t)(uintptr_t)g_clock;
    memcpy(RBP + 0x4B8, &ctx, 8); memcpy(g_ctx + 0x38, &camera, 8); memcpy(g_ctx + 0x40, &clock, 8);
    const float cam[3] = {x, y, z};
    memcpy(g_camera + 0xE8, cam, 12);
    memcpy(g_clock + 0x130, &counter, 4);
}
static void Counter(uint32_t counter) { memcpy(g_clock + 0x130, &counter, 4); }

static int Near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }
/* The translation the snap was handed, and the one the game copies into its views after the call. */
static int T(float x, float y, float z)
{
    const float* after = Light() + 12;
    return Near(g_snap.t[0], x) && Near(g_snap.t[1], y) && Near(g_snap.t[2], z) && Near(after[0], x) && Near(after[1], y) && Near(after[2], z);
}

static uint8_t* g_exe;
static SnapFn g_wrap;
static SetFloatFn g_setFloat;
static SetBoolFn g_setBool;

static void FarPins(uint8_t* image)
{
    static const uint8_t setFloat[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30};
    static const uint8_t halfCall[] = {0xE8, 0xC8, 0x23, 0x54, 0x00};
    Pins(image);
    memcpy(image + SET_FLOAT, setFloat, sizeof setFloat);
    memcpy(image + HALF_CALL, halfCall, sizeof halfCall);
    g_exe = fake_host_exe();
    CoXlTestSunExePins(g_exe);
}

static uintptr_t ExeRva(uint32_t rva) { return (uintptr_t)g_exe + rva - (uintptr_t)g_fake.image; }

static void FarReset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    co_switch_test_reset();
    FarPins(g_fake.image);
    g_fake.originals[0].target = (uintptr_t)g_fake.image + SET_BOOL;
    g_fake.originals[0].original = (void*)FakeSetBool;
    g_fake.originals[1].target = (uintptr_t)g_fake.image + SET_FLOAT;
    g_fake.originals[1].original = (void*)FakeSetFloat;
    g_fake.originals[2].target = (uintptr_t)g_exe + LEVELS;
    g_fake.originals[2].original = (void*)FakeLevels;
    memset(&g_got, 0, sizeof g_got); memset(&g_float, 0, sizeof g_float); memset(&g_snap, 0, sizeof g_snap);
}

static int32_t Rel32(const uint8_t* at) { int32_t r; memcpy(&r, at, 4); return r; }

/* Every part in; the switched site's mod path read back to the function it calls, and the game's path checked. */
static int FarStart(const char* name)
{
    FarReset(name);
    g_wrap = NULL;
    if (CoXlTestSunInstall((uintptr_t)g_fake.image) != 1 || CoXlTestSunFar() != 1) return 0;
    g_setBool = (SetBoolFn)fake_host_detour(SET_BOOL);
    g_setFloat = (SetFloatFn)fake_host_detour(SET_FLOAT);
    if (g_setBool == NULL || g_setFloat == NULL || fake_host_detour(ExeRva(LEVELS)) == NULL) return 0;
    const uint8_t* site = g_exe + SNAP_CALL;
    if (site[0] != 0xE9) return 0;
    const uint8_t* room = site + 5 + Rel32(site + 1);
    if (room[0] != 0xFF || room[1] != 0x25) return 0;
    const uintptr_t* slot = (const uintptr_t*)(room + 6 + Rel32(room + 2));
    co_switch_live(CO_SWITCH_XL, 0);
    const uint8_t* game = (const uint8_t*)*slot;
    /* the game's path: its call of the snap, then back behind the site */
    if (game[0] != 0xE8 || game + 5 + Rel32(game + 1) != g_exe + SNAP || game[5] != 0xE9 || game + 10 + Rel32(game + 6) != site + 5) return 0;
    co_switch_live(CO_SWITCH_XL, 1);
    const uint8_t* mod = (const uint8_t*)*slot;
    if (mod[0] != 0x48 || mod[1] != 0xB8 || mod[10] != 0xFF || mod[11] != 0xD0 || mod[12] != 0xE9 || mod + 17 + Rel32(mod + 13) != site + 5) return 0;
    memcpy(&g_wrap, mod + 2, 8);
    CoXlTestSunSnap(FakeSnap);
    return g_wrap != NULL;
}

static int RefuseWrite(uintptr_t at, const void* data, size_t size) { return 1; }

static uint64_t Snap(void) { return g_wrap(Light(), (const float*)(RBP + 8), 1024, 512); }

static void FarCases(void)
{
    int ok = FarStart("far shadows: every part in");
    CHECK(ok);
    if (!ok) return;
    const uintptr_t b1 = (uintptr_t)g_exe + BACK1, b2 = (uintptr_t)g_exe + BACK2;

    g_case = "before a far mission the snap is the game's";
    Frame(100, 200, 30, 500);
    CHECK(Snap() == 0x5A && g_snap.calls == 1 && T(0, 0, 1024) && Tiles() == 8 && Levels() == 3);
    CHECK(g_snap.light == Light() && g_snap.box == (const float*)(RBP + 8) && g_snap.w == 1024 && g_snap.h == 512);
    CHECK(CoXlTestSunLevels(b1, 3) == 3 && CoXlTestSunFull() == 0);

    g_case = "a 16384 m world keeps the static slice, at vanilla's half size";
    Level(16384.0f, 0.984375f);
    CHECK(g_setBool(Settings(), "use_static_last_slice", 1) == 0x5E7 && g_got.value == 1 && CoXlTestSunDynamic() == 0);
    CHECK(!fake_host_logged("stays dynamic"));
    CHECK(g_setFloat(Settings(), "static_slice_half_size", 8064.0f) == 0xF1 && g_float.calls == 1 && g_float.value == 1024.0f);
    CHECK(strcmp(g_float.name, "static_slice_half_size") == 0 && CoXlTestSunMission() == 1);
    CHECK(fake_host_logged("far shadows on this 16384 m world") && fake_host_logged("+-1024 m about the camera") &&
        fake_host_logged("+-8064 m about the origin") && fake_host_logged("moved after 256 m"));
    g_setFloat(Settings(), "static_slice_min_object_size", 0.01f);
    CHECK(g_float.value == 0.01f && CoXlTestSunMission() == 1);

    g_case = "the first frame centres the box on the camera and draws the whole slice";
    Frame(100, 200, 30, 500);
    Snap();
    CHECK(T(100, 200, 1054) && Tiles() == 1 && Levels() == 0 && CoXlTestSunFull() == 1);
    /* the render task's two reads give 0 for that frame's counter, any other read or frame the game's */
    CHECK(CoXlTestSunLevels(b1, 3) == 0 && CoXlTestSunLevels(b2, 3) == 0 && CoXlTestSunLevels(b1 + 1, 3) == 3 &&
        CoXlTestSunLevels((uintptr_t)g_exe + 0x3663DFu, 3) == 3);
    Counter(501);
    CHECK(CoXlTestSunLevels(b1, 3) == 3 && CoXlTestSunLevels(b2, 2) == 2);

    g_case = "the next frame keeps the box and the game's tiles";
    Frame(100, 200, 30, 501);
    Snap();
    CHECK(T(100, 200, 1054) && Tiles() == 8 && Levels() == 3 && CoXlTestSunFull() == 1);
    Counter(500);
    CHECK(CoXlTestSunLevels(b1, 3) == 3); /* the whole frame's mark is gone */

    g_case = "within a quarter of the half size along each light axis the box stays";
    Frame(100 + 255, 200 - 255, 30 + 255, 502);
    Snap();
    CHECK(T(100, 200, 1054) && Tiles() == 8 && Levels() == 3 && CoXlTestSunFull() == 1);

    g_case = "past it along U the box moves";
    Frame(100 + 257, 200, 30, 503);
    Snap();
    CHECK(T(357, 200, 1054) && Tiles() == 1 && Levels() == 0 && CoXlTestSunFull() == 2);
    float c[3];
    CHECK(CoXlTestSunCentre(c) == 1 && Near(c[0], 357) && Near(c[1], 200) && Near(c[2], 30));

    g_case = "past it along R the box moves";
    Frame(357, 200 - 300, 30, 504);
    Snap();
    CHECK(T(357, -100, 1054) && Levels() == 0 && CoXlTestSunFull() == 3);

    g_case = "past it along the light the box moves";
    Frame(357, -100, 30 + 300, 505);
    Snap();
    CHECK(T(357, -100, 1354) && Levels() == 0 && CoXlTestSunFull() == 4);

    g_case = "a camera that is not a number keeps the box";
    Frame(NAN, -100, 330, 506);
    Snap();
    CHECK(T(357, -100, 1354) && Levels() == 3 && CoXlTestSunFull() == 4);

    g_case = "another half size draws the slice again";
    Frame(357, -100, 330, 507);
    ((float*)(RBP + 8))[2] = 512.0f;
    Snap();
    CHECK(Levels() == 0 && CoXlTestSunFull() == 5);

    g_case = "leaving the mission: the game's snap, and no whole frame";
    Frame(357, -100, 330, 508);
    Snap();
    CHECK(Levels() == 0 && CoXlTestSunFull() == 6);
    g_setBool(Settings(), "use_static_last_slice", 0);
    CHECK(CoXlTestSunMission() == 0 && CoXlTestSunLevels(b1, 3) == 3);
    Frame(357, -100, 330, 509);
    Snap();
    CHECK(T(0, 0, 1024) && Levels() == 3 && CoXlTestSunFull() == 6);

    g_case = "a mission whose first camera is not a number waits for one";
    g_setFloat(Settings(), "static_slice_half_size", 8064.0f);
    Frame(NAN, 0, 0, 510);
    Snap();
    CHECK(T(0, 0, 1024) && Levels() == 3 && CoXlTestSunFull() == 6);

    g_case = "the next mission draws the slice whole at its first frame, the camera where the box was";
    Frame(357, -100, 330, 510);
    Snap();
    CHECK(T(357, -100, 1354) && Levels() == 0 && CoXlTestSunFull() == 7);

    g_case = "a 2048 m world keeps the game's half size and the origin";
    Level(2048.0f, 0.998046875f);
    g_setBool(Settings(), "use_static_last_slice", 1);
    CHECK(g_got.value == 1);
    g_setFloat(Settings(), "static_slice_half_size", 1022.0f);
    CHECK(g_float.value == 1022.0f && CoXlTestSunMission() == 0);
    Frame(357, -100, 330, 511);
    Snap();
    CHECK(T(0, 0, 1024) && Levels() == 3 && Tiles() == 8);

    g_case = "just over 2048 m takes far shadows";
    Level(2049.0f, 1.0f);
    g_setFloat(Settings(), "static_slice_half_size", 1024.5f);
    CHECK(g_float.value == 1024.0f && CoXlTestSunMission() == 1);

    g_case = "no level in play: the game's half size";
    Level(16384.0f, 1.0f);
    g_levelSlot = 0;
    g_setFloat(Settings(), "static_slice_half_size", 8064.0f);
    CHECK(g_float.value == 8064.0f && CoXlTestSunMission() == 0);

    /* a part refused: no far part acts, the site stays the game's call, and the slice falls back to dynamic on XL */
    const int pins = CoXlTestSunExePins(g_exe);
    for (int i = 0; i < pins + 4; i++)
    {
        FarReset("a far part refused: the dynamic fallback");
        if (i < pins) g_exe[CoXlTestSunExePin(i) + 2] ^= 0xFF;
        else if (i == pins) g_fake.image[SET_FLOAT + 3] ^= 0xFF;
        else if (i == pins + 1) g_fake.image[HALF_CALL + 1] ^= 0xFF;
        else if (i == pins + 2) g_fake.noHook = ExeRva(LEVELS);
        else g_fake.noHook = SET_FLOAT;
        CHECK(CoXlTestSunInstall((uintptr_t)g_fake.image) == 1 && CoXlTestSunFar() == 0);
        CHECK(g_exe[SNAP_CALL] == 0xE8 && co_switch_path((uintptr_t)g_exe + SNAP_CALL) == -1);
        CHECK(fake_host_logged("the last slice stays dynamic on XL"));
        SetBoolFn set = (SetBoolFn)fake_host_detour(SET_BOOL);
        SetFloatFn setF = (SetFloatFn)fake_host_detour(SET_FLOAT);
        CHECK(set != NULL);
        if (set == NULL) continue;
        Level(16384.0f, 1.0f);
        set(Settings(), "use_static_last_slice", 1);
        CHECK((uint8_t)g_got.value == 0 && CoXlTestSunDynamic() == 1);
        if (setF != NULL) { setF(Settings(), "static_slice_half_size", 8064.0f); CHECK(g_float.value == 8064.0f && CoXlTestSunMission() == 0); }
        CoXlTestSunRetry(); /* foreign bytes or a refused hook: no second try */
        CHECK(CoXlTestSunFar() == 0 && g_exe[SNAP_CALL] == 0xE8);
    }

    g_case = "a thread inside the site: the hooks pass every value, and the next complex level tries again";
    FarReset(g_case);
    CoXlTestSunWrite(RefuseWrite);
    CHECK(CoXlTestSunInstall((uintptr_t)g_fake.image) == 1 && CoXlTestSunFar() == 0 && g_exe[SNAP_CALL] == 0xE8);
    CHECK(fake_host_logged("tried again at the next complex level"));
    {
        SetBoolFn set = (SetBoolFn)fake_host_detour(SET_BOOL);
        SetFloatFn setF = (SetFloatFn)fake_host_detour(SET_FLOAT);
        CHECK(set != NULL && setF != NULL);
        if (set != NULL && setF != NULL)
        {
            Level(16384.0f, 1.0f);
            set(Settings(), "use_static_last_slice", 1);
            CHECK((uint8_t)g_got.value == 0);
            setF(Settings(), "static_slice_half_size", 8064.0f);
            CHECK(g_float.value == 8064.0f && CoXlTestSunMission() == 0);
            CHECK(CoXlTestSunLevels((uintptr_t)g_exe + BACK1, 3) == 3);
            CoXlTestSunWrite(NULL);
            CoXlTestSunRetry();
            CHECK(CoXlTestSunFar() == 1 && g_exe[SNAP_CALL] == 0xE9);
            set(Settings(), "use_static_last_slice", 1);
            CHECK(g_got.value == 1);
            setF(Settings(), "static_slice_half_size", 8064.0f);
            CHECK(g_float.value == 1024.0f && CoXlTestSunMission() == 1);
        }
    }
    CoXlTestSunWrite(NULL);

    g_case = "no helldivers2.exe: the dynamic fallback";
    FarReset(g_case);
    g_fake.exe = NULL;
    CHECK(CoXlTestSunInstall((uintptr_t)g_fake.image) == 1 && CoXlTestSunFar() == 0);
}

int main(void)
{
    SetBoolFn set = Start("a 16384 m world: mission start's true reaches the game as false");
    CHECK(set != NULL);
    if (set != NULL)
    {
        Level(16384.0f, 0.984375f);
        const LONG before = CoXlTestSunDynamic();
        CHECK(set(Settings(), "use_static_last_slice", 1) == 0x5E7);
        CHECK(g_got.calls == 1 && (uint8_t)g_got.value == 0);
        CHECK(g_got.settings == Settings() && g_got.name != NULL && strcmp(g_got.name, "use_static_last_slice") == 0);
        CHECK(CoXlTestSunDynamic() == before + 1);
        CHECK(fake_host_logged("the sun's last slice stays dynamic on this 16384 m world"));
        CHECK(fake_host_logged("+-8064 m"));

        /* r8b is the value: only its low byte is cleared, as the writer reads it (movzx r15d, r8b) */
        set(Settings(), "use_static_last_slice", 0xAB01);
        CHECK(g_got.value == 0xAB00);
    }

    set = Start("a 2048 m world keeps the game's true");
    if (set != NULL)
    {
        Level(2048.0f, 0.998046875f);
        const LONG before = CoXlTestSunDynamic();
        set(Settings(), "use_static_last_slice", 1);
        CHECK(g_got.calls == 1 && g_got.value == 1);
        CHECK(CoXlTestSunDynamic() == before);
        CHECK(!fake_host_logged("stays dynamic"));
    }

    set = Start("just over 2048 m is larger than vanilla's");
    if (set != NULL)
    {
        Level(2049.0f, 1.0f);
        set(Settings(), "use_static_last_slice", 1);
        CHECK((uint8_t)g_got.value == 0);
    }

    set = Start("other settings and false pass as given on a 16384 m world");
    if (set != NULL)
    {
        Level(16384.0f, 1.0f);
        const LONG before = CoXlTestSunDynamic();
        set(Settings(), "sun_shadows", 1);
        CHECK(g_got.value == 1);
        set(Settings(), "use_static_last_slice_x", 1);
        CHECK(g_got.value == 1);
        set(Settings(), "use_static_last_slice", 0);
        CHECK(g_got.calls == 3 && g_got.value == 0);
        CHECK(CoXlTestSunDynamic() == before);
        CHECK(!fake_host_logged("stays dynamic"));
    }

    set = Start("no level in play: the game's value");
    if (set != NULL)
    {
        Level(16384.0f, 1.0f);
        g_levelSlot = 0;
        set(Settings(), "use_static_last_slice", 1);
        CHECK(g_got.value == 1);
    }

    set = Start("the level is the one at settings - 0x7910270 + 0xAE288");
    if (set != NULL)
    {
        Level(16384.0f, 1.0f);
        /* a vanilla level one slot either side changes nothing */
        static uint64_t around[3];
        static uint8_t vanilla[0x3000];
        const float w = 2048.0f;
        memcpy(vanilla + 0x1E0, &w, 4);
        around[0] = around[2] = (uint64_t)(uintptr_t)vanilla;
        around[1] = (uint64_t)(uintptr_t)g_level;
        const uint64_t settings = (uint64_t)(uintptr_t)&around[1] - 0xAE288u + 0x7910270u;
        set(settings, "use_static_last_slice", 1);
        CHECK((uint8_t)g_got.value == 0);
        around[1] = (uint64_t)(uintptr_t)vanilla;
        around[0] = around[2] = (uint64_t)(uintptr_t)g_level;
        set(settings, "use_static_last_slice", 1);
        CHECK(g_got.value == 1);
    }

    /* foreign bytes at any pin: no hook, the game's slice on every world */
    static const uintptr_t pins[3] = {SET_BOOL + 18, ENTER_AT + 2, HALF_AT + 11};
    for (int i = 0; i < 3; i++)
    {
        g_case = "a foreign byte at a pin refuses the hook";
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        g_coMod = (HfMod*)&g_fake;
        g_fake.strictCode = 1;
        Pins(g_fake.image);
        g_fake.image[pins[i]] ^= 0xFF;
        CHECK(CoXlTestSunInstall((uintptr_t)g_fake.image) == -1);
        CHECK(fake_host_detour(SET_BOOL) == NULL);
        CHECK(fake_host_logged("the sun's last slice is the game's on every world"));
    }

    g_case = "a refused hook leaves the game's slice";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    Pins(g_fake.image);
    g_fake.noHook = SET_BOOL;
    CHECK(CoXlTestSunInstall((uintptr_t)g_fake.image) == -1);
    CHECK(fake_host_logged("could not be hooked"));

    FarCases();
    if (g_failed) { printf("sunslice: %d failed\n", g_failed); return 1; }
    printf("sunslice: ok\n");
    return 0;
}
