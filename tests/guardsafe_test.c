/* SafeRead inside a guarded call (proto/difficulty_world.c WorldTileFault, proto/safe_read.c SafeReadFault). Both are
 * vectored handlers added first in line, the guard's later, so it sees a fault first: a fault inside SafeRead's in-place
 * copy must be passed on to SafeReadFault (a failed read, 1) and not taken as the guarded call's own. A plain read that
 * faults there is still the call's fault. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_shim_install(void);
LONG CoXlTestGuardedRead(uintptr_t address, int safe, int* read);
int CoXlTestSafeOwns(uintptr_t rip);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_shim_install(); /* SafeRead's handler first, as at the mod's load; the guard's is added on its first call */
    uint8_t* reserved = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_NOACCESS); /* above 0x10000, never committed */
    static uint8_t mapped[16] = {0x5A};
    int read = 0;

    g_case = "a fault elsewhere is not SafeRead's";
    CHECK(CoXlTestSafeOwns((uintptr_t)main) == 0 && CoXlTestSafeOwns(0) == 0);

    g_case = "a SafeRead of a readable byte in the guard: read, no fault";
    CHECK(CoXlTestGuardedRead((uintptr_t)mapped, 1, &read) == 0 && read == 0);

    g_case = "a SafeRead that faults in the guard: its failure return, the call not faulted";
    CHECK(reserved != NULL && CoXlTestGuardedRead((uintptr_t)reserved + 0x100, 1, &read) == 0 && read == 1);

    g_case = "a plain read that faults in the guard: still the call's fault";
    if (!g_failed) CHECK( /* not when a fault elsewhere was taken for SafeRead's: nothing would resume it */
        CoXlTestGuardedRead((uintptr_t)reserved + 0x100, 0, &read) == (LONG)EXCEPTION_ACCESS_VIOLATION && read == -1);

    VirtualFree(reserved, 0, MEM_RELEASE);
    if (g_failed) { printf("guardsafe: %d check(s) failed\n", g_failed); return 1; }
    printf("guardsafe: all checks passed\n");
    return 0;
}
