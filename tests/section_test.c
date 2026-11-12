/* A war-table preview cancelled inside its stamp section, then a mission's level init: the level init waits for the
 * section, so the section's write-back lands before the mission's own values, and the section sees the cancel (its
 * job's run flag cleared) or a newer level init. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLevelInit(uintptr_t level);
void CoXlTestSectionBegin(void);
int CoXlTestSectionGone(void);
void CoXlTestSectionEnd(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define PREVIEW_DISK 0x3F7C4B33u /* the complex level's disk (the tile marker the section holds and puts back) */
#define MISSION_DISK 0x3F2A0000u /* what the mission's level init writes at 0xF71721 */
static uint8_t* g_lg;
static uint8_t g_rec[16];
static volatile LONG g_initDone;

static void Put(uint32_t offset, uint32_t value) { memcpy(g_lg + offset, &value, 4); }
static uint32_t Get(uint32_t offset) { uint32_t v; memcpy(&v, g_lg + offset, 4); return v; }

/* The mission's level init: the mod's first step (WorldLevel), then the game's own write of the level's disk. */
static DWORD WINAPI MissionInit(void* unused)
{
    CoXlTestLevelInit((uintptr_t)g_lg);
    Put(0x2D7C, MISSION_DISK);
    InterlockedExchange(&g_initDone, 1);
    return 0;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL) { printf("section: no memory\n"); return 1; }
    const uintptr_t rec = (uintptr_t)g_rec;
    memcpy(g_lg + 0xF718, &rec, 8);

    g_case = "the preview's section, running";
    CoXlTestLevelInit((uintptr_t)g_lg);
    g_rec[9] = 1;
    CoXlTestSectionBegin();
    Put(0x2D7C, PREVIEW_DISK ^ 1u); /* held while the tiles run */
    CHECK(CoXlTestSectionGone() == 0);

    g_case = "the ship's leave cancels the preview";
    g_rec[9] = 0; /* the game's reset (0xF70D10) */
    CHECK(CoXlTestSectionGone() == 1);

    g_case = "the mission's level init waits for the section";
    HANDLE thread = CreateThread(NULL, 0, MissionInit, NULL, 0, NULL);
    CHECK(thread != NULL);
    CHECK(WaitForSingleObject(thread, 300) == WAIT_TIMEOUT);
    CHECK(g_initDone == 0);
    Put(0x2D7C, PREVIEW_DISK); /* the section's write-back at its end */
    CoXlTestSectionEnd();
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(g_initDone == 1);
    CHECK(Get(0x2D7C) == MISSION_DISK);

    g_case = "a section of an older level is gone";
    g_rec[9] = 1;
    CHECK(CoXlTestSectionGone() == 1); /* the level init began after it */
    CoXlTestSectionBegin();
    CHECK(CoXlTestSectionGone() == 0);
    CoXlTestSectionEnd();
    CoXlTestLevelInit((uintptr_t)g_lg);
    CHECK(CoXlTestSectionGone() == 1);

    g_case = "no job record";
    memset(g_lg + 0xF718, 0, 8);
    CoXlTestSectionBegin();
    CHECK(CoXlTestSectionGone() == 0);
    CoXlTestSectionEnd();

    if (g_failed) { printf("section: %d failed\n", g_failed); return 1; }
    printf("section: all passed\n");
    return 0;
}
