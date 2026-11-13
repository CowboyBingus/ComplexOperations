/* The SpeedTree forest's trees against its instance grid's room (co_forest.h). */
#include "co_forest.h"
#include "co_shim.h"

/* The unit's world and its forest (0x20BC80's caller), the spawn's id reads, the grid's offset at its layout and at the
 * crash's insert and write, the grid's room store, and the insert's "full" test. */
static const struct { uint32_t rva; const char* pin; } kForestPins[] = {
    {0xDC46Eu, "49 8B 4D 18"},
    {0xDC485u, "48 8B 89 B8 08 23 00"},
    {0x20BF00u, "8B 96 D8 00 00 00"},
    {0x20BF22u, "44 8B B6 F0 00 00 00"},
    {0x20ADEAu, "48 8D 8E 20 01 00 00"},
    {0x20CF2Cu, "48 8D 8E 20 01 00 00"},
    {0x20CF49u, "E8 92 06 5A 00 44 8B 4D B0 B9 FF FF FF FF 44 89 08"},
    {0x7AD36Du, "44 89 6F 1C"},
    {0x7AD60Cu, "41 83 FC FF"},
};

int co_forest_pins(uintptr_t exe)
{
    if (exe == 0) return 0;
    for (size_t i = 0; i < sizeof kForestPins / sizeof kForestPins[0]; i++)
        if (co_exe_code_matches(exe + kForestPins[i].rva, kForestPins[i].pin) != 1) return 0;
    return 1;
}

int co_forest_of_unit(uint64_t unit, uintptr_t* forest)
{
    uint64_t world = 0, f = 0;
    if (unit == 0 || co_safe_read((uintptr_t)unit + CO_FOREST_UNIT_WORLD, &world, 8) || world == 0 ||
        co_safe_read((uintptr_t)world + CO_FOREST_OF_WORLD, &f, 8) || f == 0) return 0;
    *forest = (uintptr_t)f;
    return 1;
}

int co_forest_trees(uintptr_t forest, uint32_t* trees, uint32_t* room)
{
    uint32_t made = 0, idle = 0, r = 0;
    if (forest == 0 || co_safe_read(forest + CO_FOREST_RECORDS, &made, 4) || co_safe_read(forest + CO_FOREST_FREE, &idle, 4) ||
        co_safe_read(forest + CO_FOREST_GRID + CO_FOREST_GRID_ROOM, &r, 4) ||
        r < CO_FOREST_ROOM_MIN || r > CO_FOREST_ROOM_MAX || made > CO_FOREST_ROOM_MAX || idle > made) return 0;
    *trees = made - idle;
    *room = r;
    return 1;
}

int co_forest_over(uint32_t trees, uint32_t room, float share) { return (float)trees >= share * (float)room; }

int co_forest_call_fits(uint32_t trees, uint32_t room, uint32_t most)
{
    uint64_t reserve = room / 10u;
    if (2ull * most > reserve) reserve = 2ull * most;
    return (uint64_t)trees + reserve <= (uint64_t)room;
}
