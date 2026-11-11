/* The two value pages: where each reader of the world's size points, and when each page changes (co_pages.h). */
#include <string.h>

#include "co_pages.h"
#include "co_page_sites.h"

static struct
{
    uintptr_t game;
    CoGenPage* gen;
    CoLivePage* live;
    float genScale, liveScale;
} g_pages = {0, NULL, NULL, 1.0f, 1.0f};

void co_page_gen_values(CoGenPage* v, float scale)
{
    v->half = 1024.0f * scale;
    v->negHalf = -1024.0f * scale;
    v->full = 2048.0f * scale;
    v->negFull = -2048.0f * scale;
    v->inverse = 1.0f / (2048.0f * scale);
    /* the clearing points' chance: 2^-32 of the game's random number, less by the world's area so their count stays */
    v->clearing = 2.3283064365386963e-10f * scale * scale;
    v->quarter = 512.0f * scale;
    /* the pads' and flatten zones' falloff bands, in heightmap pixels and metres of a larger world; 1 is the game's */
    v->padPx = scale > 1.0f ? 1.5f / scale : 1.0f;
    v->padMetre = scale > 1.0f ? 1.5f / 2.0f : 1.0f;
    v->padQuad = scale > 1.0f ? scale / 2.0f : 1.0f;
    v->flatPx = scale > 1.0f ? 2.0f / scale : 1.0f;
}

void co_page_live_values(CoLivePage* v, float scale)
{
    v->half = 1024.0f * scale;
    v->negHalf = -1024.0f * scale;
    v->full = 2048.0f * scale;
    v->negFull = -2048.0f * scale;
    v->inverse = 1.0f / (2048.0f * scale);
    v->gateHigh = 2000.0f * scale;
    v->gateLow = -2000.0f * scale;
    v->dropHigh = 1024.0f * scale - 25.0f;
    v->dropLow = -(1024.0f * scale - 25.0f);
    v->maskSpan = 4096.0f * scale;
    v->goalHigh = 4096.0f * scale;
    v->goalLow = -4096.0f * scale;
    /* the game's margin past the world's edge is kept: 876 m */
    v->motionHigh = 1900.0f + 1024.0f * (scale - 1.0f);
    v->motionLow = -v->motionHigh;
}

static const float* WorldField(const CoWorldReader* s)
{
    if (s->page == CO_PAGE_GEN)
    {
        const float* fields[5] = {&g_pages.gen->half, &g_pages.gen->full, &g_pages.gen->inverse, &g_pages.gen->negHalf, &g_pages.gen->negFull};
        return fields[s->which];
    }
    const float* fields[5] = {&g_pages.live->half, &g_pages.live->full, &g_pages.live->inverse, &g_pages.live->negHalf, &g_pages.live->negFull};
    return fields[s->which];
}

static const float* PageField(const CoPageReader* s)
{
    const CoLivePage* l = g_pages.live;
    const float* fields[] = {&l->gateHigh, &l->gateLow, &l->dropHigh, &l->dropLow, &l->maskSpan, &l->goalHigh, &l->goalLow, &l->motionHigh, &l->motionLow,
        &g_pages.gen->quarter, &g_pages.gen->clearing};
    return fields[s->field];
}

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[16];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

/* The displacement that makes the instruction ending at `end` read `field`; 0 when it is out of a rel32's reach. */
static int Reach(uintptr_t end, const float* field, int32_t* rel)
{
    const int64_t distance = (int64_t)(uintptr_t)field - (int64_t)end;
    *rel = (int32_t)distance;
    return distance >= INT32_MIN && distance <= INT32_MAX;
}

/* Every instruction the install changes, against the game's bytes; NULL when all are the ones this mod was made for. */
const char* co_pages_check(uintptr_t game)
{
    for (size_t i = 0; i < sizeof kCoWorldReaders / sizeof kCoWorldReaders[0]; i++)
        if (!Same(game + kCoWorldReaders[i].rva, kCoWorldReaders[i].bytes, kCoWorldReaders[i].length))
            return "a reader of the world's size is not the game's code this mod was made for";
    for (size_t i = 0; i < sizeof kCoPageReaders / sizeof kCoPageReaders[0]; i++)
        if (!Same(game + kCoPageReaders[i].rva, kCoPageReaders[i].bytes, kCoPageReaders[i].length))
            return "a reader of a world bound is not the game's code this mod was made for";
    for (size_t i = 0; i < sizeof kCoPageImmediates / sizeof kCoPageImmediates[0]; i++)
        if (!Same(game + kCoPageImmediates[i].rva, kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u))
            return "a world size in the generator's code is not the game's code this mod was made for";
    return NULL;
}

/* A write that fails part-way leaves readers on pages that hold the game's values for good (no level changes a page
 * before the last write succeeded): the game reads what it read before. */
const char* co_pages_install(uintptr_t game, CoGenPage* gen, CoLivePage* live)
{
    const size_t world = sizeof kCoWorldReaders / sizeof kCoWorldReaders[0], single = sizeof kCoPageReaders / sizeof kCoPageReaders[0];
    const char* why;
    int32_t rel;
    if (g_pages.game != 0) return "the world's readers are pointed already";
    if (game == 0 || gen == NULL || live == NULL) return "no page for the world's readers";
    if ((why = co_pages_check(game)) != NULL) return why;
    g_pages.gen = gen; g_pages.live = live;
    for (size_t i = 0; i < world; i++)
        if (!Reach(game + kCoWorldReaders[i].rva + kCoWorldReaders[i].length, WorldField(&kCoWorldReaders[i]), &rel))
            return "a page is out of reach of a reader of the world's size";
    for (size_t i = 0; i < single; i++)
        if (!Reach(game + kCoPageReaders[i].rva + kCoPageReaders[i].length, PageField(&kCoPageReaders[i]), &rel))
            return "a page is out of reach of a reader of a world bound";
    /* The game's values first: a reader pointed at a page reads what it read before. */
    gen->size = 0.0f; gen->capMul = 1;
    co_page_gen_values(gen, 1.0f);
    co_page_live_values(live, 1.0f);
    g_pages.genScale = g_pages.liveScale = 1.0f;
    for (size_t i = 0; i < world; i++)
    {
        const CoWorldReader* s = &kCoWorldReaders[i];
        Reach(game + s->rva + s->length, WorldField(s), &rel);
        if (g_coApi->write(game + s->rva + s->length - 4u, &rel, 4) != 0) return "a reader of the world's size could not be pointed at its page";
    }
    for (size_t i = 0; i < single; i++)
    {
        const CoPageReader* s = &kCoPageReaders[i];
        Reach(game + s->rva + s->length, PageField(s), &rel);
        if (g_coApi->write(game + s->rva + s->length - 4u, &rel, 4) != 0) return "a reader of a world bound could not be pointed at its page";
    }
    g_pages.game = game; /* only now may a level change a value: a reader left on the game's constant would not follow */
    return NULL;
}

/* The generator's immediates for a scale. One that holds neither the game's value nor the last one written is
 * somebody else's now and is left alone. They are code, changed while no pass runs: one generation at a time. */
static void Immediates(float from, float to)
{
    for (size_t i = 0; i < sizeof kCoPageImmediates / sizeof kCoPageImmediates[0]; i++)
    {
        const CoPageImmediate* s = &kCoPageImmediates[i];
        float game, now, was, value;
        memcpy(&game, &s->value, 4);
        was = game * from; value = game * to;
        if (g_coApi->read(g_pages.game + s->rva + s->at, &now, 4) != 0 || memcmp(&now, &was, 4) != 0) continue;
        g_coApi->write(g_pages.game + s->rva + s->at, &value, 4);
    }
}

int co_pages_level(float scale, int mission)
{
    int changed = 0;
    if (g_pages.game == 0 || !(scale >= 1.0f)) return 0;
    if (scale != g_pages.genScale)
    {
        co_page_gen_values(g_pages.gen, scale);
        Immediates(g_pages.genScale, scale);
        g_pages.genScale = scale;
        changed |= CO_PAGES_GEN;
    }
    if (mission && scale != g_pages.liveScale)
    {
        co_page_live_values(g_pages.live, scale);
        g_pages.liveScale = scale;
        changed |= CO_PAGES_LIVE;
    }
    return changed;
}

int co_pages_ship(void)
{
    if (g_pages.game == 0 || g_pages.liveScale == 1.0f) return 0;
    co_page_live_values(g_pages.live, 1.0f);
    g_pages.liveScale = 1.0f;
    return CO_PAGES_LIVE;
}

float co_pages_scale(int page) { return page == CO_PAGES_LIVE ? g_pages.liveScale : g_pages.genScale; }

#ifdef CO_TEST
void co_pages_test_reset(void) { memset(&g_pages, 0, sizeof g_pages); g_pages.genScale = g_pages.liveScale = 1.0f; }
const CoWorldReader* co_pages_test_readers(size_t* count) { *count = sizeof kCoWorldReaders / sizeof kCoWorldReaders[0]; return kCoWorldReaders; }
const CoPageReader* co_pages_test_singles(size_t* count) { *count = sizeof kCoPageReaders / sizeof kCoPageReaders[0]; return kCoPageReaders; }
const CoPageImmediate* co_pages_test_immediates(size_t* count) { *count = sizeof kCoPageImmediates / sizeof kCoPageImmediates[0]; return kCoPageImmediates; }
#endif
