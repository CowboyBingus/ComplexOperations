/* HellForge's partner rules over the game's mission, operation and objective tables (co_mix.h). The offsets are
 * HellForge's (native/mod-loader/difficulty.c, difficulty_missions.c); scripts/gen_mix_rows.py follows the same
 * source on the game's image and stops when a rule's line changes. */
#include <string.h>

#include "co_mix.h"
#include "co_mix_rows.h"

#define MISSIONS 0x3773420u
#define MISSION_ROW 0x380u
#define OPERATIONS 0x32FEF10u
#define OPERATION_ROWS 25u
#define OPERATION_ROW 0x490u
#define OPERATION_MISSIONS 32u
#define OBJECTIVES 0x32EF870u
#define OBJECTIVE_ROWS 153u
#define OBJECTIVE_ROW 0xA0u
#define ERADICATE 0xC5DD9A6650C23B4Aull
#define RADAR 0xF1969B14u
#define PARTNERS 16u
#define TOP 10u /* the game's highest level; HellForge's is the next */

typedef struct Entry { uint32_t id; float weight; uint32_t c, cls, e, f; } Entry;
static Entry g_entries[CO_MIX_ROWS][32];
static uint8_t g_listed[OPERATION_ROWS][CO_MIX_ROWS];

/* HellForge's own additions to a row, which its MixInstall did not see: a partner's step (weight 0, never drawn) and
 * the radar station as an optional entry. The game's table has neither form (gen_mix_rows.py). */
static int Step(const Entry* e) { return e->id != 0 && e->weight == 0.0f && e->c == 0 && e->cls == 1 && e->e == 0 && e->f == 0; }
static int Added(const Entry* e) { return Step(e) || (e->id == RADAR && e->weight == 0.0f && e->c == 0 && e->cls == 3 && e->e == 0 && e->f == 0xFFFFFFFFu); }

static int EntryClass(uint32_t b, uint32_t id)
{
    for (int k = 0; k < 32; k++) if (g_entries[b][k].id == id && !Added(&g_entries[b][k])) return (int)g_entries[b][k].cls;
    return -1;
}

const char* co_mix_bases(uintptr_t game, uint8_t base[CO_MIX_ROWS], int* mixing)
{
    static uint32_t ids[CO_MIX_ROWS], objective[OBJECTIVE_ROWS][3];
    static uint8_t faction[CO_MIX_ROWS], standard[CO_MIX_ROWS], eradicate[CO_MIX_ROWS];
    int steps = 0;
    _Static_assert(sizeof(Entry) == 0x18, "a mission row's objective entry");
    for (uint32_t r = 0; r < CO_MIX_ROWS; r++)
    {
        const uintptr_t row = game + MISSIONS + r * MISSION_ROW;
        uint8_t head[0x38];
        uint64_t delta = 1;
        uint32_t range[2];
        if (g_coApi->read(row, head, sizeof head) != 0 || g_coApi->read(row + 0x40, g_entries[r], sizeof g_entries[r]) != 0 ||
            g_coApi->read(row + 0x360, &delta, 8) != 0) return "the mission rows could not be read";
        memcpy(&ids[r], head, 4); memcpy(range, head + 0xC, 8);
        faction[r] = head[8];
        /* a row offered from HellForge's level on is one it made after its MixInstall: neither a base nor a partner */
        const int made = range[0] > TOP, enabled = head[4] != 0;
        standard[r] = !made && head[0x34] == 1 && delta == 0 && enabled && range[0] <= TOP + 1u && TOP + 1u <= range[1];
        /* an Eradicate mission partners unless the level leaves Eradicate out, and then its range was not widened */
        eradicate[r] = !made && delta == ERADICATE && enabled && range[1] >= TOP + 1u;
        for (int k = 0; k < 32; k++) steps += Step(&g_entries[r][k]);
    }
    memset(g_listed, 0, sizeof g_listed);
    for (uint32_t o = 0; o < OPERATION_ROWS; o++)
    {
        uint32_t list[OPERATION_MISSIONS][2];
        if (g_coApi->read(game + OPERATIONS + o * OPERATION_ROW + 0x38, list, sizeof list) != 0) return "the operation rows could not be read";
        for (uint32_t k = 0; k < OPERATION_MISSIONS; k++)
            for (uint32_t r = 0; list[k][0] != 0 && r < CO_MIX_ROWS; r++) if (ids[r] == list[k][0]) g_listed[o][r] = 1;
    }
    for (uint32_t d = 0; d < OBJECTIVE_ROWS; d++)
    {
        uint8_t row[0x2C];
        if (g_coApi->read(game + OBJECTIVES + d * OBJECTIVE_ROW, row, sizeof row) != 0) return "the objective rows could not be read";
        memcpy(&objective[d][0], row, 4); memcpy(&objective[d][1], row + 0x24, 8);
    }
    for (uint32_t b = 0; b < CO_MIX_ROWS; b++)
    {
        uint32_t partners = 0;
        for (uint32_t r = 0; standard[b] && r < CO_MIX_ROWS && partners < PARTNERS; r++)
        {
            int together = 0, mains = 0, conflict = 0;
            if (r == b || !(standard[r] || eradicate[r]) || faction[r] != faction[b]) continue;
            for (uint32_t o = 0; o < OPERATION_ROWS; o++) together |= g_listed[o][b] && g_listed[o][r];
            for (int k = 0; together && k < 32 && !conflict; k++)
            {
                const Entry* e = &g_entries[r][k];
                if (e->id == 0 || !(e->weight > 0.0f) || e->cls > 1) continue;
                const int in = EntryClass(b, e->id);
                if (e->cls == 1) { conflict = in >= 0 && in != 1; continue; }
                if (in >= 0) { conflict = 1; continue; }
                for (uint32_t d = 1; d < OBJECTIVE_ROWS; d++) /* the first definition of the id, as HellForge looks it up */
                    if (objective[d][0] == e->id) { mains += objective[d][1] <= TOP + 1u && TOP + 1u <= objective[d][2]; break; }
            }
            partners += together && !conflict && mains > 0;
        }
        base[b] = partners > 0;
    }
    *mixing = steps > 0;
    return NULL;
}

const char* co_mix_read(uintptr_t game, uint8_t base[CO_MIX_ROWS], int* mixing)
{
    const char* why = co_mix_bases(game, base, mixing);
    uint32_t next = 0;
    if (why != NULL) return why;
    for (uint32_t r = 0; r < CO_MIX_ROWS; r++)
    {
        const int expected = next < CO_MIX_BASES && kCoMixBases[next] == r;
        if (expected) next++;
        if (base[r] != expected) return "the missions difficulty 11 mixes are not the ones this mod was built for";
    }
    return NULL;
}
