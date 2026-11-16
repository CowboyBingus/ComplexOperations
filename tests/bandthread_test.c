/* A tile built in mission: its band's copy and pass on a thread of their own (proto/difficulty_world_terrain.c
 * NearBandStart). The conform's material frame held the game thread 172-195 ms. The thread's texels are the
 * synchronous pass's to the bit; the conform waits for it a frame at a time and then makes the texture; a pass cut
 * short is dropped without the game thread waiting; the level's end waits for it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestLwNavLevel(long level);
void CoXlTestBandInputs(float cx, float cy, uint32_t* level);
uint32_t CoXlTestBandSync(uint32_t* tile);
void CoXlTestBandStart(const void* data);
int CoXlTestBandState(void);
uint32_t* CoXlTestBandCollect(int keep, uint32_t* taken);
void CoXlTestBandAbandon(void);
void CoXlTestBandPretend(int running);
void CoXlTestNearReplayEnd(void);
void CoXlTestWorldShip(void);
void CoXlTestBandJobTick(void);
void CoXlTestBandJoin(void);
int CoXlTestConformStep(int phase, int building, int* after, uint32_t** pixels);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define TEXELS ((size_t)4096 * 4096)
static uint32_t* Map(void) { return (uint32_t*)VirtualAlloc(NULL, TEXELS * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }

/* a material word: three 5-bit ids and two weights, varied over the map */
static uint32_t Word(uint32_t r, uint32_t c, uint32_t salt)
{
    uint32_t h = (r * 2654435761u) ^ (c * 40503u) ^ salt;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    uint32_t a = 1 + (h & 7), b = 1 + ((h >> 3) & 7), d = 1 + ((h >> 6) & 7), wb = (h >> 9) & 127, wc = (h >> 16) & 127;
    return (a << 26) | (b << 21) | (d << 16) | (wb << 8) | wc;
}

static void Wait(void) { while (CoXlTestBandState() & 2) Sleep(5); }

static uint32_t g_ready = 1;
static uint32_t Readback(uint64_t resource) { (void)resource; return 77; }
static char Ready(uint32_t id) { (void)id; return (char)g_ready; }
static const void* g_data;
static const uint8_t* Desc(uint8_t* out, uint32_t id)
{
    (void)id;
    uint32_t w = 4096, f = 1; uint64_t at = (uint64_t)(uintptr_t)g_data;
    memcpy(out, &w, 4); memcpy(out + 4, &w, 4); memcpy(out + 8, &f, 4); memcpy(out + 16, &at, 8);
    return out;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    CoXlTestGame((uintptr_t)image);
    CoXlTestLwNavLevel(3);
    uint32_t* level = Map(), *tile = Map(), *sync = Map();
    for (uint32_t r = 0; r < 4096; r++)
        for (uint32_t c = 0; c < 4096; c++) { level[(size_t)r * 4096 + c] = Word(r, c, 0x51u); tile[(size_t)r * 4096 + c] = Word(r, c, 0x1234u); }
    CoXlTestBandInputs(1500.0f, -700.0f, level);

    g_case = "the thread's texels are the synchronous pass's, and so is its count";
    memcpy(sync, tile, TEXELS * 4);
    const uint32_t takenSync = CoXlTestBandSync(sync);
    CoXlTestBandStart(tile);
    CHECK(CoXlTestBandState() & 1);
    Wait();
    uint32_t taken = 0;
    uint32_t* out = CoXlTestBandCollect(1, &taken);
    CHECK(out != NULL && out != tile && out != sync);
    CHECK(taken == takenSync && takenSync > 0);
    CHECK(out != NULL && memcmp(out, sync, TEXELS * 4) == 0);
    CHECK(memcmp(tile, sync, TEXELS * 4) != 0); /* the readback itself is only read */
    CHECK(CoXlTestBandState() == 0);
    if (out) VirtualFree(out, 0, MEM_RELEASE);

    g_case = "a pass cut short: dropped once done, without anyone waiting; nothing left";
    CoXlTestBandStart(tile);
    CoXlTestBandAbandon();
    CHECK(CoXlTestBandState() & 4);
    Wait();
    CoXlTestBandJobTick();
    CHECK(CoXlTestBandState() == 0);

    g_case = "a pass not cut short is kept for the conform, not dropped by the frame's tick";
    CoXlTestBandStart(tile);
    Wait();
    CoXlTestBandJobTick();
    CHECK(CoXlTestBandState() == 1);
    out = CoXlTestBandCollect(1, &taken);
    CHECK(out != NULL);
    if (out) VirtualFree(out, 0, MEM_RELEASE);

    g_case = "the level's end waits for a pass under way and drops it";
    CoXlTestBandStart(tile);
    CoXlTestBandJoin();
    CHECK(CoXlTestBandState() == 0);
    CHECK(fake_host_logged("the level ended with a tile's band pass under way"));
    CoXlTestBandJoin(); /* none: nothing */

    g_case = "the conform of a tile built in mission: phase 6 starts the thread, phase 11 waits, then the texture";
    {
        /* the readback API ([api+0x10]+0x1A8 / +0x1D0 / +0x1D8) and the engine's format table (exe+0x1658990: format 1
         * is 32 bits a texel), on the test's own image */
        static uint8_t api[0x20], table[0x700];
        uint64_t tableAt = (uint64_t)(uintptr_t)table, apiAt = (uint64_t)(uintptr_t)api;
        memcpy(api + 0x10, &tableAt, 8);
        memcpy(image + 0x3326308u, &apiAt, 8);
        uint32_t (*rb)(uint64_t) = Readback; char (*rd)(uint32_t) = Ready; const uint8_t* (*ds)(uint8_t*, uint32_t) = Desc;
        memcpy(table + 0x1A8, &rb, 8); memcpy(table + 0x1D0, &rd, 8); memcpy(table + 0x1D8, &ds, 8);
        uint8_t* formats = (uint8_t*)GetModuleHandleW(NULL) + 0x1658990u;
        /* the test's image reaches past it (its embedded code and data): the page is the image's, made writable */
        DWORD old = 0;
        uint8_t* page = VirtualProtect(formats, 8, PAGE_READWRITE, &old) ? formats : NULL;
        CHECK(page != NULL);
        if (page != NULL)
        {
            uint32_t bits = 32; memcpy(formats + 4, &bits, 4);
            g_data = tile;
            int after = 0; uint32_t* pixels = NULL;
            g_ready = 0;
            CHECK(CoXlTestConformStep(6, 1, &after, &pixels) == 1 && after == 6 && CoXlTestBandState() == 0); /* not read back yet */
            g_ready = 1;
            CHECK(CoXlTestConformStep(6, 1, &after, &pixels) == 1 && after == 11 && (CoXlTestBandState() & 1));
            int frames = 0, r = 1;
            while (r == 1 && frames < 100000) { r = CoXlTestConformStep(after, 1, &after, &pixels); frames++; if (r == 1) Sleep(1); }
            CHECK(r == 0 && frames >= 1 && CoXlTestBandState() == 0);
            CHECK(fake_host_logged("material band: copied and passed on its own thread in"));
            /* no texture interface here: the band is not made and its pixels go, as the synchronous path did */
            CHECK(fake_host_logged("the material band's texture was not created") && pixels == NULL);

            g_case = "a readback that is not 4096 x 4096 x 32 bits: no thread, the conform ends";
            uint32_t bits16 = 16; memcpy(formats + 4, &bits16, 4);
            CHECK(CoXlTestConformStep(6, 1, &after, &pixels) == 0 && CoXlTestBandState() == 0);
            CHECK(fake_host_logged("the tile's material map read back as 4096 x 4096 format 1"));
            memcpy(formats + 4, &bits, 4);

            g_case = "no level map (or the band off): no thread, the conform ends";
            CoXlTestBandInputs(1500.0f, -700.0f, NULL);
            CHECK(CoXlTestConformStep(6, 1, &after, &pixels) == 0 && CoXlTestBandState() == 0);
            CoXlTestBandInputs(1500.0f, -700.0f, level);

            g_case = "a cut-short pass still running holds the next conform's phase 6";
            CoXlTestBandStart(tile);
            CoXlTestBandAbandon();
            CHECK(CoXlTestConformStep(6, 1, &after, &pixels) == 1 && after == 6);
            Wait();
            int k = 0;
            while (k++ < 10 && (r = CoXlTestConformStep(6, 1, &after, &pixels)) == 1 && after == 6) {}
            CHECK(after == 11);
            Wait();
            while (r == 1 && k++ < 100) r = CoXlTestConformStep(after, 1, &after, &pixels);
            CHECK(r == 0 && CoXlTestBandState() == 0);
        }
    }

    g_case = "phase 11 with the pass under way: the frame goes on at once, the phase stays";
    {
        int after = 0; uint32_t* pixels = NULL;
        CoXlTestBandPretend(1);
        for (int k = 0; k < 3; k++) CHECK(CoXlTestConformStep(11, 1, &after, &pixels) == 1 && after == 11 && CoXlTestBandState() == 3);
        CoXlTestBandPretend(0);
        CHECK(CoXlTestConformStep(11, 1, &after, &pixels) == 0 && CoXlTestBandState() == 0 && pixels == NULL);
    }

    g_case = "the replay's end cuts a pass under way short";
    CoXlTestBandStart(tile);
    CoXlTestNearReplayEnd();
    CHECK(CoXlTestBandState() & 4);
    Wait();
    CoXlTestBandJobTick();
    CHECK(CoXlTestBandState() == 0);

    g_case = "the ship's set-up (the level's end) waits for a pass under way";
    CoXlTestBandStart(tile);
    CoXlTestWorldShip();
    CHECK(CoXlTestBandState() == 0);

    if (g_failed) { printf("bandthread: %d check(s) failed\n", g_failed); return 1; }
    printf("bandthread: all checks passed\n");
    return 0;
}
