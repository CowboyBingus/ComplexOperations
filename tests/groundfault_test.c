/* A tile slot's heights copy (proto/difficulty_world_move.c MoveGroundStart / MoveGroundWait): NearGroundOf copied
 * 32 MiB into a fresh buffer on the ready frame, every page faulted in on the game thread (14.8 ms in game, 1.2 ms
 * into touched memory). The buffer is made and touched on a thread of its own while the tile builds; the ready state
 * waits for it a frame at a time and hands it to the slot; a slot with its buffer starts nothing. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestGroundStart(int slot);
int CoXlTestGroundWait(int slot);
uint16_t* CoXlTestGroundBuf(int slot);
void CoXlTestGroundClear(int slot);
int CoXlTestGroundRunning(void);
void* CoXlTestGroundThread(void);
const char* CoXlTestReadyWait(int layerBusy, int groundBusy, long long age, long long* since);

typedef union { ULONG_PTR Flags; struct { ULONG_PTR Valid : 1; ULONG_PTR rest : 63; } b; } WsBlock;
typedef struct { PVOID VirtualAddress; WsBlock VirtualAttributes; } WsInfo;
BOOL WINAPI K32QueryWorkingSetEx(HANDLE process, PVOID info, DWORD size);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* the buffer's pages in memory: every 64th of its 8192 pages asked */
static int Resident(const uint16_t* buf)
{
    static WsInfo info[128];
    for (int k = 0; k < 128; k++) { info[k].VirtualAddress = (uint8_t*)buf + (size_t)k * 64 * 4096; info[k].VirtualAttributes.Flags = 0; }
    if (!K32QueryWorkingSetEx(GetCurrentProcess(), info, sizeof info)) return -1;
    int n = 0;
    for (int k = 0; k < 128; k++) n += info[k].VirtualAttributes.b.Valid ? 1 : 0;
    return n;
}

static int WaitDone(int slot)
{
    for (int k = 0; k < 5000; k++) { if (!CoXlTestGroundWait(slot)) return 1; Sleep(1); }
    return 0;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);

    g_case = "the control: a buffer only reserved and committed has no page in memory";
    {
        uint16_t* raw = (uint16_t*)VirtualAlloc(NULL, (size_t)4096 * 4096 * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK(raw != NULL && Resident(raw) == 0);
        VirtualFree(raw, 0, MEM_RELEASE);
    }

    g_case = "a slot without its buffer: made and touched on a thread, handed to the slot once done";
    CoXlTestGroundClear(2);
    CoXlTestGroundStart(2);
    CHECK(CoXlTestGroundRunning() == 1);
    CHECK(CoXlTestGroundWait(2) == 1 && CoXlTestGroundBuf(2) == NULL); /* 8192 pages to touch: the frame goes on, nothing waited for */
    void* first = CoXlTestGroundThread();
    CoXlTestGroundStart(2); /* a second start while one runs: nothing more */
    CHECK(CoXlTestGroundThread() == first);
    CHECK(WaitDone(2) && CoXlTestGroundRunning() == 0);
    uint16_t* buf = CoXlTestGroundBuf(2);
    CHECK(buf != NULL && Resident(buf) == 128);
    CHECK(buf != NULL && buf[0] == 0 && buf[4096 * 4096 - 1] == 0);

    g_case = "a slot with its buffer: nothing started, the buffer kept";
    CoXlTestGroundStart(2);
    CHECK(CoXlTestGroundRunning() == 0 && CoXlTestGroundWait(2) == 0 && CoXlTestGroundBuf(2) == buf);

    g_case = "the wait for another slot than the start's: the touched buffer goes to the slot that waits without one";
    CoXlTestGroundClear(3);
    CoXlTestGroundStart(3);
    CHECK(WaitDone(2) && CoXlTestGroundBuf(2) == buf); /* slot 2 has one: kept for the next */
    CHECK(CoXlTestGroundWait(3) == 0 && CoXlTestGroundBuf(3) != NULL && CoXlTestGroundBuf(3) != buf && Resident(CoXlTestGroundBuf(3)) == 128);

    CoXlTestGroundClear(2); CoXlTestGroundClear(3);

    g_case = "MV_READY's waits: a frame more while the layer fill or the prefault runs, a failure 60 s after the wait began";
    {
        long long since = 0;
        const char* why = CoXlTestReadyWait(0, 0, 90000, &since);
        CHECK(why == NULL && since == -1); /* both done: the wait's start cleared */
        why = CoXlTestReadyWait(1, 0, -1, &since);
        CHECK(why != NULL && why[0] == 0 && since == 0); /* a wait begins now, however long the state has lasted */
        why = CoXlTestReadyWait(1, 0, 59000, &since);
        CHECK(why != NULL && why[0] == 0 && since == 59000);
        why = CoXlTestReadyWait(0, 1, 60000, &since);
        CHECK(why != NULL && why[0] == 0);
        why = CoXlTestReadyWait(1, 1, 60001, &since);
        CHECK(why != NULL && strcmp(why, "the layer change did not finish") == 0); /* the layer's wait first */
        why = CoXlTestReadyWait(0, 1, 60001, &since);
        CHECK(why != NULL && strcmp(why, "the ground buffer did not finish") == 0);
    }
    if (g_failed) { printf("groundfault: %d check(s) failed\n", g_failed); return 1; }
    printf("groundfault: all checks passed\n");
    return 0;
}
