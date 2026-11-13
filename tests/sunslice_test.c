/* The sun's last shadow slice: mission start's "use_static_last_slice = true" reaches the game as false on a world over
 * 2048 m, so the far slice stays dynamic, and as true on a 2048 m world. Every other setting and value passes as given.
 * The level is the one the game reads for the slice's half size: [settings - 0x7910270 + 0xAE288]. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestSunInstall(uintptr_t game);
LONG CoXlTestSunDynamic(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SET_BOOL 0x10087C0u
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

    if (g_failed) { printf("sunslice: %d failed\n", g_failed); return 1; }
    printf("sunslice: ok\n");
    return 0;
}
