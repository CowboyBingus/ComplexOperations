/* A tile built in mission: the spawn's material readback (proto/difficulty_world_terrain.c NearMatReadAsync,
 * NearMatCopyStep). Its 64 MiB went into rpmalloc's freshly mapped huge block on the spawn frame, ~30 ms of page
 * faults. In play it is copied a slice a frame and dst and done are stored once all of it is there, the bytes the
 * same; the load keeps the copy whole; a free of the pixels or the terrain's end copies the rest first. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMoveBuilding(int on);
void CoXlTestMatCopySetup(uint64_t tex, uint32_t* pixels);
uint64_t CoXlTestMatReadAsync(uint64_t src, uint64_t alloc, uint64_t dst, uint64_t done);
int CoXlTestMatCopyStep(double budget);
void CoXlTestMatCopyFinish(void);
long long CoXlTestMatCopyAt(void);
void CoXlTestMoveFreeNow(uint32_t* pixels);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define BYTES ((size_t)4096 * 4096 * 4)
#define CHUNK ((size_t)1 << 20)

/* the world allocator: vtable +0x30 allocate(self, &out, bytes, align) */
static void* g_blocks[8];
static int g_allocs;
static void** Allocate(void* self, void** out, uint64_t bytes, uint64_t align)
{
    (void)self; (void)align;
    *out = VirtualAlloc(NULL, (size_t)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_allocs < 8) g_blocks[g_allocs] = *out;
    g_allocs++;
    return out;
}
static void* g_vtable[8];
static void* g_alloc[2] = {g_vtable, NULL};

static uint32_t* Pixels(uint32_t seed)
{
    uint32_t* p = (uint32_t*)VirtualAlloc(NULL, BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (size_t i = 0; p != NULL && i < BYTES / 4; i++) p[i] = (uint32_t)(i * 2654435761u) ^ seed;
    return p;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    g_vtable[6] = (void*)Allocate;
    const uint64_t tex = 0xBA0D;
    uint64_t dst = 0; uint8_t done[2] = {0, 0};
    const uint64_t alloc = (uint64_t)(uintptr_t)g_alloc, dstAt = (uint64_t)(uintptr_t)&dst, doneAt = (uint64_t)(uintptr_t)&done[1];

    g_case = "in play: a slice on the spawn frame, a slice a frame after; dst and done once all is there, the bytes the band's";
    {
        uint32_t* pixels = Pixels(0x1234);
        CoXlTestMoveBuilding(1);
        CoXlTestMatCopySetup(tex, pixels);
        dst = 0; done[1] = 0; g_allocs = 0;
        CHECK(CoXlTestMatReadAsync(tex, alloc, dstAt, doneAt) == 0 && g_allocs == 1);
        CHECK(dst == 0 && done[1] == 0 && CoXlTestMatCopyAt() >= (long long)CHUNK && CoXlTestMatCopyAt() < (long long)BYTES);
        const long long at = CoXlTestMatCopyAt();
        CHECK(CoXlTestMatCopyStep(0.0) == 1 && CoXlTestMatCopyAt() == at + (long long)CHUNK); /* one chunk at least, then the budget */
        CHECK(dst == 0 && done[1] == 0);
        int frames = 0;
        while (CoXlTestMatCopyStep(3.0) && frames < 1000) { CHECK(done[1] == 0 && dst == 0); frames++; }
        CHECK(CoXlTestMatCopyAt() == -1 && done[1] == 1 && dst == (uint64_t)(uintptr_t)g_blocks[0]);
        CHECK(g_blocks[0] != NULL && memcmp(g_blocks[0], pixels, BYTES) == 0);
        CHECK(fake_host_logged("the material pixels served to the spawn's readback: 64 MiB in"));
        CHECK(CoXlTestMatCopyStep(3.0) == 0); /* nothing pending */
        VirtualFree(g_blocks[0], 0, MEM_RELEASE); VirtualFree(pixels, 0, MEM_RELEASE);
    }

    g_case = "at load: the copy whole in the call, done at once";
    {
        uint32_t* pixels = Pixels(0x55);
        CoXlTestMoveBuilding(0);
        CoXlTestMatCopySetup(tex, pixels);
        dst = 0; done[1] = 0; g_allocs = 0;
        CHECK(CoXlTestMatReadAsync(tex, alloc, dstAt, doneAt) == 0 && CoXlTestMatCopyAt() == -1);
        CHECK(done[1] == 1 && dst == (uint64_t)(uintptr_t)g_blocks[0] && memcmp(g_blocks[0], pixels, BYTES) == 0);
        VirtualFree(g_blocks[0], 0, MEM_RELEASE); VirtualFree(pixels, 0, MEM_RELEASE);
    }

    g_case = "the terrain's end (a join): the rest copied now, dst and done stored";
    {
        uint32_t* pixels = Pixels(0x77);
        CoXlTestMoveBuilding(1);
        CoXlTestMatCopySetup(tex, pixels);
        dst = 0; done[1] = 0; g_allocs = 0;
        CoXlTestMatReadAsync(tex, alloc, dstAt, doneAt);
        CHECK(CoXlTestMatCopyAt() > 0 && done[1] == 0);
        CoXlTestMatCopyFinish();
        CHECK(CoXlTestMatCopyAt() == -1 && done[1] == 1 && memcmp(g_blocks[0], pixels, BYTES) == 0);
        VirtualFree(g_blocks[0], 0, MEM_RELEASE); VirtualFree(pixels, 0, MEM_RELEASE);
    }

    g_case = "the band's pixels freed with the copy under way: the rest copied from them first";
    {
        uint32_t* pixels = Pixels(0x99);
        uint32_t* keep = Pixels(0x99);
        CoXlTestMatCopySetup(tex, pixels);
        dst = 0; done[1] = 0; g_allocs = 0;
        CoXlTestMatReadAsync(tex, alloc, dstAt, doneAt);
        CHECK(CoXlTestMatCopyAt() > 0);
        CoXlTestMoveFreeNow(pixels);
        CHECK(CoXlTestMatCopyAt() == -1 && done[1] == 1 && memcmp(g_blocks[0], keep, BYTES) == 0);
        VirtualFree(g_blocks[0], 0, MEM_RELEASE); VirtualFree(keep, 0, MEM_RELEASE);
    }

    if (g_failed) { printf("matcopy: %d check(s) failed\n", g_failed); return 1; }
    printf("matcopy: all checks passed\n");
    return 0;
}
