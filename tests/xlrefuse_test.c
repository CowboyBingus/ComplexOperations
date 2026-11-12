/* The extra large world's guards refuse cleanly: a destruction-record map whose multiplier is not the game's 1 (its
 * only writer, 0x13DF88C) is not changed, and the complex part does not go in without the records' room. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

const char* CoXlTestGuards(uintptr_t game);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int main(void)
{
    g_case = "the records' constructor is not the game's";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.noMatch[0] = 0x13DF88Cu;
    const char* why = CoXlTestGuards((uintptr_t)g_fake.image);
    CHECK(why != NULL && strstr(why, "destruction records") != NULL);
    CHECK(fake_host_detour(0x13E0200u) == NULL && fake_host_detour(0x13DF7D0u) == NULL && fake_host_detour(0x13E1730u) == NULL);
    CHECK(fake_host_logged("the destruction records are not the reviewed code"));
    if (g_failed) { printf("xlrefuse: %d checks failed\n", g_failed); return 1; }
    printf("xlrefuse: all cases passed\n");
    return 0;
}
