/* What the engine lays out once, as the game starts, for a world of 16384 m: the physics pools, the Havok broadphase
 * and the navigation grid's cells. They are written when game.dll is loaded, before its plugin setup reads them, on
 * what is known then; whether a mission is ever complex is decided later. A process that got them keeps them. */
#include <string.h>

#include <windows.h>

#include "co.h"
#include "co_early_sites.h"

static struct { int in; float* cell; } g_early;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[16];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* Every site holds what this was made for: HellForge's pool sizes (it writes them before the game-loaded event), the
 * game's own everywhere else. Reads only. */
const char* co_early_check(uintptr_t game, uintptr_t exe)
{
    uint64_t setup = 1;
    float size = 0.0f;
    if (g_coApi->read(game + CO_EARLY_SETUP, &setup, 8) != 0 || setup != 0) return "the game's set-up had begun";
    for (size_t i = 0; i < sizeof kCoEarlyPools / sizeof kCoEarlyPools[0]; i++)
        if (!Same(game + kCoEarlyPools[i].rva, kCoEarlyPools[i].stock, kCoEarlyPools[i].length))
            return "the physics pools are not the sizes HellForge gives them";
    if (!Same(game + CO_EARLY_BROADPHASE_LOAD, kCoEarlyBroadphaseLoad, 8) || g_coApi->read(game + CO_EARLY_BROADPHASE, &size, 4) != 0 || size != 3072.0f)
        return "the physics world's size is not the game's";
    for (size_t i = 0; i < 4; i++)
        if (!Same(game + kCoEarlyExtents[i].rva, kCoEarlyExtents[i].bytes, 7)) return "the navigation grid's extents are not the game's";
    if (!Same(game + CO_EARLY_CELL_READ, kCoEarlyCellRead, 8) || exe == 0 || !Same(exe + CO_EARLY_TILE_READ, kCoEarlyTileRead, 8))
        return "the navigation grid's cell size is not the game's";
    return NULL;
}

/* The game's or HellForge's value back at every site written so far: the setup has not read them yet. */
static void Undo(uintptr_t game, size_t pools, int broadphase, size_t extents)
{
    const float size = 3072.0f;
    for (size_t i = 0; i < pools; i++)
        g_coApi->write(game + kCoEarlyPools[i].rva + kCoEarlyPools[i].at, kCoEarlyPools[i].stock + kCoEarlyPools[i].at, 4);
    if (broadphase) g_coApi->write(game + CO_EARLY_BROADPHASE, &size, 4);
    for (size_t i = 0; i < extents; i++) g_coApi->write(game + kCoEarlyExtents[i].rva + 3u, kCoEarlyExtents[i].bytes + 3, 4);
}

const char* co_early_install(uintptr_t game, uintptr_t exe)
{
    const char* why;
    /* past the box a moving body leaves the world: 512 m around the map, as the game keeps around its own */
    const float size = CO_EARLY_WORLD + 1024.0f, extent = 1024.0f * (float)CO_EARLY_CELLS / 8.0f;
    size_t i;
    if (g_early.in) return NULL;
    if ((why = co_early_check(game, exe)) != NULL) return why;
    float* cell = (float*)co_alloc_near(game + CO_EARLY_CELL_READ, 0x1000u); /* within 1.75 GB: a 32-bit offset reaches it */
    if (cell == NULL) return "no memory near the game for the navigation grid's cell size";
    const int32_t rel = (int32_t)((int64_t)(uintptr_t)cell - (int64_t)(game + CO_EARLY_CELL_READ + 8u));
    *cell = 1024.0f / (float)CO_EARLY_CELLS;
    for (i = 0; i < sizeof kCoEarlyPools / sizeof kCoEarlyPools[0]; i++)
        if (g_coApi->write(game + kCoEarlyPools[i].rva + kCoEarlyPools[i].at, &kCoEarlyPools[i].own, 4) != 0)
        { Undo(game, i, 0, 0); VirtualFree(cell, 0, MEM_RELEASE); return "a physics pool could not be enlarged"; }
    if (g_coApi->write(game + CO_EARLY_BROADPHASE, &size, 4) != 0)
    { Undo(game, i, 0, 0); VirtualFree(cell, 0, MEM_RELEASE); return "the physics world could not be enlarged"; }
    /* the extents size the grid's block and the cell read lays the grid out: in this order it never outgrows the block */
    for (size_t e = 0; e < 4; e++)
        if (g_coApi->write(game + kCoEarlyExtents[e].rva + 3u, &extent, 4) != 0)
        { Undo(game, i, 1, e); VirtualFree(cell, 0, MEM_RELEASE); return "the navigation grid's extents could not be written"; }
    if (g_coApi->write(game + CO_EARLY_CELL_READ + 4u, &rel, 4) != 0)
    { Undo(game, i, 1, 4); VirtualFree(cell, 0, MEM_RELEASE); return "the navigation grid's cell size could not be pointed"; }
    g_early.cell = cell; g_early.in = 1;
    return NULL;
}

int co_early_in(void) { return g_early.in; }

#ifdef CO_TEST
void co_early_test_reset(void) { memset(&g_early, 0, sizeof g_early); }
const float* co_early_test_cell(void) { return g_early.cell; }
#endif
