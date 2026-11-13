/* The moving tiles' wall-clock frame note (proto/difficulty_world_move.c MoveWallAdd): a build's longest three frames,
 * level update to level update, each with the mover state that ran in it, and how many took over 50 ms. A tile's
 * change held one frame for 1.5 s while the game-thread cost alone could not show the frames around it. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

void CoXlTestMoveWall(double ms, int mv);
void CoXlTestMoveWallRead(double* wall, int* mv, uint32_t* slow);
void CoXlTestMoveWallClear(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int main(void)
{
    double wall[3]; int mv[3]; uint32_t slow;

    g_case = "the longest three in order, each with its state; frames over 50 ms counted";
    CoXlTestMoveWallClear();
    const double ms[] = {16.0, 52.0, 1528.0, 17.0, 50.0, 236.0, 90.0};
    for (int k = 0; k < 7; k++) CoXlTestMoveWall(ms[k], 10 + k);
    CoXlTestMoveWallRead(wall, mv, &slow);
    CHECK(wall[0] == 1528.0 && mv[0] == 12);
    CHECK(wall[1] == 236.0 && mv[1] == 15);
    CHECK(wall[2] == 90.0 && mv[2] == 16);
    CHECK(slow == 4); /* 52, 1528, 236, 90: 50 itself is not over */

    g_case = "a frame equal to the third does not push it out";
    CoXlTestMoveWallClear();
    CoXlTestMoveWall(30.0, 1); CoXlTestMoveWall(20.0, 2); CoXlTestMoveWall(10.0, 3); CoXlTestMoveWall(10.0, 4);
    CoXlTestMoveWallRead(wall, mv, &slow);
    CHECK(wall[0] == 30.0 && wall[1] == 20.0 && wall[2] == 10.0 && mv[2] == 3 && slow == 0);

    g_case = "fewer than three frames: the rest stay 0";
    CoXlTestMoveWallClear();
    CoXlTestMoveWall(70.0, 6);
    CoXlTestMoveWallRead(wall, mv, &slow);
    CHECK(wall[0] == 70.0 && mv[0] == 6 && wall[1] == 0.0 && wall[2] == 0.0 && slow == 1);

    if (g_failed) { printf("movewall: %d check(s) failed\n", g_failed); return 1; }
    printf("movewall: all checks passed\n");
    return 0;
}
