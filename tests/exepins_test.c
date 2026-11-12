/* An exe pin over one of the tile stubs' sites (co_exe_code_matches, xl/co_shim.h): the nav job lever pins the engine's
 * point-to-cell step, which the stubs' jump covers once they are in. The jump into the trim's own reviewed block reads as
 * the engine's bytes; any other change, before or after the install, still fails. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_nav.h"
#include "xl/co_navtrim.h"
#include "xl/co_shim.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The nav job lever's pin at the step, as living_world_navjob.c has it. */
static const char kPin[] = "F3 0F 10 41 14 F3 0F 5C 41 0C F3 0F 5C 49 0C 0F B7 01 F3 0F 5C 51 10";

static uint8_t* Reset(const char* name)
{
    DWORD old;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_nav_test_reset();
    fake_stock_nav(g_fake.image);
    VirtualProtect(g_fake.exe, FAKE_EXE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    /* the host's check fails on the step, as it does on the jump the stubs write */
    g_fake.noMatch[0] = (uintptr_t)g_fake.exe + CO_NAVTRIM_CELL - (uintptr_t)g_fake.image;
    return g_fake.exe;
}

int main(void)
{
    uint8_t* exe = Reset("the engine's step, no stubs");
    const uintptr_t at = (uintptr_t)exe + CO_NAVTRIM_CELL;
    CHECK(co_exe_code_matches(at, kPin) == 1);

    exe = Reset("a changed byte, no stubs");
    exe[CO_NAVTRIM_CELL + 7] ^= 0xFF;
    CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 0);

    exe = Reset("the stubs' jump");
    CHECK(co_nav_install((uintptr_t)g_fake.image) == NULL && co_navtrim_refused() == NULL);
    CHECK(exe[CO_NAVTRIM_CELL] == 0xE9);
    CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 1);

    g_case = "a byte behind the jump changed";
    exe[CO_NAVTRIM_CELL + 7] ^= 0xFF;
    CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 0);
    exe[CO_NAVTRIM_CELL + 7] ^= 0xFF;

    g_case = "a jump somewhere else";
    {
        int32_t rel;
        memcpy(&rel, exe + CO_NAVTRIM_CELL + 1, 4);
        rel += 0x10;
        memcpy(exe + CO_NAVTRIM_CELL + 1, &rel, 4);
        CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 0);
        rel -= 0x10;
        memcpy(exe + CO_NAVTRIM_CELL + 1, &rel, 4);
        CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 1);
    }

    g_case = "the host's own check passes";
    g_fake.noMatch[0] = 0;
    CHECK(co_exe_code_matches((uintptr_t)exe + CO_NAVTRIM_CELL, kPin) == 1);

    if (g_failed) { printf("exepins: %d check(s) failed\n", g_failed); return 1; }
    printf("exepins: all checks passed\n");
    return 0;
}
