/* A moving tile's build names every frame over 16.7 ms (proto/difficulty_world_move.c MoveSlowAdd), the mover's own
 * work and the wall clock apart, by the state the frame started in and the step's op: the cost lines' longest six left
 * the build's other slow frames unnamed. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestMoveSlow(int wall, double ms, int mv, int ns, int op, int conf);
void CoXlTestMoveCostNote(void);
void CoXlTestMoveWallClear(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { MV_PACKAGE = 1, MV_REPLAY = 6, MV_READY = 10 };

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;

    g_case = "frames counted by their state and op, the longest of each; 16.7 ms itself is not over";
    CoXlTestMoveWallClear();
    CoXlTestMoveSlow(0, 16.7, MV_REPLAY, 2, 0x3C, 0);
    CoXlTestMoveSlow(0, 40.0, MV_REPLAY, 2, 0x3C, 0);
    CoXlTestMoveSlow(0, 120.0, MV_REPLAY, 2, 0x3C, 0);
    CoXlTestMoveSlow(0, 30.0, MV_REPLAY, 2, 0x3C, 0); /* shorter than the longest so far: the longest stays */
    CoXlTestMoveSlow(0, 90.0, MV_PACKAGE, 7, 0x64, 0);
    CoXlTestMoveSlow(0, 25.0, MV_PACKAGE, 7, 0x64, 3); /* another conform phase: a state of its own */
    CoXlTestMoveSlow(0, 5.0, MV_READY, 6, -1, 0);
    CoXlTestMoveSlow(1, 17.0, MV_READY, 6, -1, 0);
    CoXlTestMoveSlow(1, 185.0, MV_REPLAY, 2, 0x63, 0);
    CoXlTestMoveSlow(1, 18.0, MV_REPLAY, 10, 0x64, 6);
    CoXlTestMoveCostNote();
    CHECK(fake_host_logged("the build's frames over 16.7 ms: the mover's own work 5: replay/near 2/op 0x3C/conform 0: 3, longest 120 ms; package/near 7/op 0x64/conform 0: 1, longest 90 ms; package/near 7/op 0x64/conform 3: 1, longest 25 ms;"));
    CHECK(fake_host_logged("the build's frames over 16.7 ms: wall clock 3: ready/near 6/op -/conform 0: 1, longest 17 ms; replay/near 2/op 0x63/conform 0: 1, longest 185 ms; replay/near 10/op 0x64/conform 6: 1, longest 18 ms;"));

    g_case = "a build's tallies start empty after its note";
    CoXlTestMoveCostNote();
    CHECK(fake_host_logged("the build's frames over 16.7 ms: the mover's own work 0:\n") || fake_host_logged("the build's frames over 16.7 ms: the mover's own work 0:"));

    g_case = "more states than room: the rest counted together";
    for (int k = 0; k < 14; k++) CoXlTestMoveSlow(0, 20.0 + k, MV_REPLAY, 2, k, 0);
    CoXlTestMoveCostNote();
    CHECK(fake_host_logged("the mover's own work 14:"));
    CHECK(fake_host_logged(" 2 more of other states;"));

    if (g_failed) { printf("slowframes: %d check(s) failed\n", g_failed); return 1; }
    printf("slowframes: all checks passed\n");
    return 0;
}
