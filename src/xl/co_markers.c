/* The map's marker slots as a complex mission needs them: the game's and HellForge's own (co_markers.h). */
#include <string.h>

#include "co_markers.h"
#include "co_marker_sites.h"

static int Read(uintptr_t at, uint8_t* out, size_t size) { return g_coApi->read(at, out, size) == 0; }
static uintptr_t Leads(uintptr_t rel, const uint8_t* bytes) { int32_t d; memcpy(&d, bytes, 4); return rel + 4u + (uintptr_t)(intptr_t)d; }

const char* co_markers_check(uintptr_t game)
{
    uint8_t now[8], stub[16 + 8];
    for (size_t i = 0; i < sizeof kCoMarkerLimits / sizeof kCoMarkerLimits[0]; i++)
    {
        const CoMarkerLimit* l = &kCoMarkerLimits[i];
        if (!Read(game + l->rva, now, l->length) || memcmp(now, l->bytes, l->length) != 0) return "the map's marker slots are not the game's 16 with 10 trackers";
    }
    for (size_t i = 0; i < sizeof kCoMarkerGuards / sizeof kCoMarkerGuards[0]; i++)
    {
        /* jmp stub; nops. The stub: cmp dword [rsp+0x40], 16; jae leave; the site's bytes; jmp behind the site */
        const CoMarkerGuard* g = &kCoMarkerGuards[i];
        const uint8_t head[7] = {0x83, 0x7C, 0x24, 0x40, (uint8_t)CO_MARKER_SLOTS, 0x0F, 0x83};
        const uintptr_t site = game + g->rva;
        const size_t n = g->length;
        if (!Read(site, now, n) || now[0] != 0xE9) return "difficulty 11's guard of the map's marker slots is not in place";
        for (size_t k = 5; k < n; k++) if (now[k] != 0x90) return "difficulty 11's guard of the map's marker slots is not in place";
        const uintptr_t at = Leads(site + 1, now + 1);
        if (!Read(at, stub, 16 + n)) return "difficulty 11's guard of the map's marker slots is not readable";
        if (memcmp(stub, head, sizeof head) != 0 || Leads(at + 7, stub + 7) != game + g->leave || memcmp(stub + 11, g->game, n) != 0 ||
            stub[11 + n] != 0xE9 || Leads(at + 12 + n, stub + 12 + n) != site + n)
            return "difficulty 11's guard of the map's marker slots is not the one this mod was built for";
    }
    return NULL;
}
