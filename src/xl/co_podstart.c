/* A reinforcement hellpod's start on a complex level (co_podstart.h). */
#include <math.h>
#include <string.h>

#include <windows.h>

#include "co_podstart.h"

/* The start's prologue, its read of the drop point (params+0x38, +0x40) and the destroyer's lookup: the session's
 * ship table (engine+0x1FA58) and its node position (0x1001600). The guard rests on these; a patch moves them. */
static const char kEntryPin[] = "40 53 56 57 48 83 EC 50 48 8B 05 C1 21 D1 01 48 33 C4 48 89 44 24 40";
static const char kTargetPin[] = "8B 47 40 F2 0F 10 47 38 89 44 24 28 48 8B 47 48";
static const char kShipPin[] = "48 8B 0D 5D C3 9F 02 48 8D 54 24 30 48 81 C1 58 FA 01 00 44 8B C0 E8 09 76 6D 00";
#define TARGET_READ 0x929E78u
#define SHIP_LOOKUP 0x929FDCu

typedef float* (*StartFn)(void* manager, float* out, void* record, uint8_t* params);
static StartFn g_start;
static float (*g_scale)(void);
static volatile LONG g_noted;

int co_podstart_cap(float* start, const float* target, float reach)
{
    float dx = start[0] - target[0], dy = start[1] - target[1];
    float d = sqrtf(dx * dx + dy * dy);
    if (!isfinite(d) || d <= reach) return 0;
    float k = reach / d;
    start[0] = target[0] + dx * k;
    start[1] = target[1] + dy * k;
    return 1;
}

static float* Start(void* manager, float* out, void* record, uint8_t* params)
{
    float* start = g_start(manager, out, record, params);
    if (start == NULL || params == NULL || g_scale == NULL || !(g_scale() > 1.0f)) return start;
    float target[3], was[2] = {start[0], start[1]};
    memcpy(target, params + CO_PODSTART_TARGET, sizeof target);
    if (co_podstart_cap(start, target, CO_PODSTART_REACH))
        co_log("complex operations: reinforcement pod %ld started %.0f m from its drop point (%.0f, %.0f); it starts %.0f m away",
            (long)InterlockedIncrement(&g_noted), (double)hypotf(was[0] - target[0], was[1] - target[1]), (double)target[0], (double)target[1],
            (double)CO_PODSTART_REACH);
    return start;
}

const char* co_podstart_install(uintptr_t game, float (*scale)(void))
{
    if (g_start != NULL) return NULL;
    if (game == 0 || scale == NULL || g_coApi->code_matches(game + CO_PODSTART, kEntryPin) != 1 ||
        g_coApi->code_matches(game + TARGET_READ, kTargetPin) != 1 || g_coApi->code_matches(game + SHIP_LOOKUP, kShipPin) != 1)
        return "the hellpod's start is not the game's code this mod was made for";
    g_scale = scale;
    if (g_coApi->hook(g_coMod, game + CO_PODSTART, (void*)Start, (void**)&g_start) != 0) { g_start = NULL; return "the hellpod's start could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_podstart_test_reset(void) { g_start = NULL; g_scale = NULL; g_noted = 0; }
#endif
