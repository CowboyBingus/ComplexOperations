/* With the density guards in, the living world's setup passes are switched sites: a complex mission keeps mission
 * locations' loose entities back, any other level runs the game's 44 and 65 bytes, moved with their calls and
 * rip-relative loads still on the game's targets. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_density.h"
#include "xl/co_switch.h"
#include "xl/co_patrols.h"

void CoXlTestLive(int complex);
void CoXlTestLwPins(uint8_t* image);
int CoXlTestLiving(uintptr_t game);
void co_density_test_fill(uint8_t* image);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define SETUP 0xFDAA8Au
#define SAMPLE 0x79D5EFu
#define SPAWN 0xFDC140u

static void Hex(uint8_t* out, const char* hex)
{
    unsigned v;
    while (sscanf(hex, " %2x", &v) == 1) { *out++ = (uint8_t)v; hex += hex[0] == ' ' ? 3 : 2; }
}

/* The path a switched site runs now: its jump, the room's jmp [rip+slot], the slot. */
static const uint8_t* PathOf(const uint8_t* site)
{
    int32_t rel;
    memcpy(&rel, site + 1, 4);
    const uint8_t* room = site + 5 + rel;
    memcpy(&rel, room + 2, 4);
    return *(const uint8_t* const*)(room + 6 + rel);
}
static uintptr_t Target(const uint8_t* field) { int32_t d; memcpy(&d, field, 4); return (uintptr_t)field + 4u + (uintptr_t)(intptr_t)d; }

int main(void)
{
    g_case = "the setup passes switched";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    CoXlTestLwPins(image);
    co_density_test_fill(image);
    memcpy(image + CO_PATROL_CMP, kCoPatrolCmp, sizeof kCoPatrolCmp); /* the director's patrol gate */
    memcpy(image + CO_PATROL_GATE, kCoPatrolGate, sizeof kCoPatrolGate);
    memcpy(image + CO_PATROL_NEXT, kCoPatrolNext, sizeof kCoPatrolNext);
    memcpy(image + CO_PATROL_SKIP, kCoPatrolSkip, sizeof kCoPatrolSkip);
    Hex(image + SETUP, "48 8B 90 C0 00 00 00 48 8B 02 48 BA 9B 7B 13 FB 7A 92 4D B0 4C 8B 48 18 41 FF D1 "
                       "4C 8D 4D B0 4C 8B C3 8B D0 49 8B CC E8 8A 16 00 00");
    Hex(image + SAMPLE, "48 8B 05 12 8D B8 02 4C 8D 45 98 48 8B 3D 97 E9 CC 02 48 8B 90 C0 00 00 00 48 8B 0F 48 8B 02 "
                        "48 BA 9B 7B 13 FB 7A 92 4D B0 4C 8B 48 18 41 FF D1 4C 8D 4D 90 4C 8B C3 8B D0 48 8B CF E8 10 EB 83 00");
    Hex(image + SPAWN, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18");
    uint8_t setup[44], sample[65];
    memcpy(setup, image + SETUP, sizeof setup);
    memcpy(sample, image + SAMPLE, sizeof sample);

    CHECK(CoXlTestLiving((uintptr_t)image) == 1);
    CHECK(co_density_on() == 1 && !fake_host_logged("no density guards"));
    CHECK(image[SETUP] == 0xE9 && image[SAMPLE] == 0xE9 && image[SETUP + 5] == 0xCC);

    CoXlTestLive(0); /* a normal mission or the ship: the game's bytes, their targets kept */
    CHECK(co_switch_path((uintptr_t)image + SETUP) == 0 && co_switch_path((uintptr_t)image + SAMPLE) == 0);
    const uint8_t* p = PathOf(image + SETUP);
    CHECK(memcmp(p, setup, 40) == 0 && p[39] == 0xE8 && Target(p + 40) == (uintptr_t)image + SPAWN);
    CHECK(p[44] == 0xE9 && Target(p + 45) == (uintptr_t)image + SETUP + 44);
    p = PathOf(image + SAMPLE);
    CHECK(memcmp(p, sample, 3) == 0 && Target(p + 3) == (uintptr_t)image + 0x3326308u);
    CHECK(memcmp(p + 7, sample + 7, 7) == 0 && Target(p + 14) == (uintptr_t)image + 0x346BF98u);
    CHECK(memcmp(p + 18, sample + 18, 43) == 0 && Target(p + 61) == (uintptr_t)image + SPAWN);
    CHECK(p[65] == 0xE9 && Target(p + 66) == (uintptr_t)image + SAMPLE + 65);

    CoXlTestLive(1); /* a complex mission: the call into the mod, then on behind the site */
    CHECK(co_switch_path((uintptr_t)image + SETUP) == 1 && co_switch_path((uintptr_t)image + SAMPLE) == 1);
    p = PathOf(image + SETUP);
    CHECK(p[0] == 0x49 && p[10] == 0x48 && p[11] == 0xB8 && p[20] == 0xFF && p[21] == 0xD0 && p[22] == 0xE9 && Target(p + 23) == (uintptr_t)image + SETUP + 44);
    CHECK(co_switch_path((uintptr_t)image + 0x952F53u) == 1);

    CoXlTestLive(0);
    CHECK(co_switch_path((uintptr_t)image + SETUP) == 0 && co_switch_path((uintptr_t)image + 0x952F53u) == 0);

    if (g_failed) { printf("livingdefer: %d checks failed\n", g_failed); return 1; }
    printf("livingdefer: all cases passed\n");
    return 0;
}
