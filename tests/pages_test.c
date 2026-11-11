/* The two value pages. First the audit's condition: every reader of the world's size has exactly one page, and none on
 * the generator's page is play's (the generator, scripts/gen_world_pages.py, works the pages out from the game's code
 * and stops when one is under the game update; here its table is held against the prototype's list). Then the writes,
 * on an image that holds the game's bytes at every instruction the install changes. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "xl/co_pages.h"
#include "xl/co_page_sites.h"
#include "xl/proto/world_sites.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define WORLD (sizeof kCoWorldReaders / sizeof kCoWorldReaders[0])
#define SINGLES (sizeof kCoPageReaders / sizeof kCoPageReaders[0])
#define IMMEDIATES (sizeof kCoPageImmediates / sizeof kCoPageImmediates[0])
/* Free places in the image, so every page is in a reader's reach as the mod's block near game.dll is. */
#define GEN_AT 0x3000000u
#define LIVE_AT 0x3000800u

static CoGenPage* g_gen;
static CoLivePage* g_live;

static uint32_t Bits(float value) { uint32_t b; memcpy(&b, &value, 4); return b; }
static uint8_t* At(uint32_t rva) { return g_fake.image + rva; }
/* Where an instruction's displacement points, as an offset in the image. */
static uint32_t Target(uint32_t rva, uint8_t length) { int32_t d; memcpy(&d, At(rva + length - 4u), 4); return rva + length + (uint32_t)d; }
static uint32_t Reads(uint32_t rva, uint8_t length) { uint32_t b; memcpy(&b, At(Target(rva, length)), 4); return b; }

/* The game: its bytes at every instruction, its constants where those instructions point. */
static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.failWriteAt = -1;
    co_pages_test_reset();
    g_gen = (CoGenPage*)At(GEN_AT); g_live = (CoLivePage*)At(LIVE_AT);
    for (size_t i = 0; i < WORLD; i++) memcpy(At(kCoWorldReaders[i].rva), kCoWorldReaders[i].bytes, kCoWorldReaders[i].length);
    for (int i = 0; i < 5; i++) memcpy(At(kCoWorldConstants[i]), &kCoWorldConstantBits[i], 4);
    for (size_t i = 0; i < SINGLES; i++)
    {
        memcpy(At(kCoPageReaders[i].rva), kCoPageReaders[i].bytes, kCoPageReaders[i].length);
        memcpy(At(Target(kCoPageReaders[i].rva, kCoPageReaders[i].length)), &kCoPageReaders[i].value, 4);
    }
    for (size_t i = 0; i < IMMEDIATES; i++) memcpy(At(kCoPageImmediates[i].rva), kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u);
}

/* Every instruction is the game's own, byte for byte. */
static int Untouched(void)
{
    for (size_t i = 0; i < WORLD; i++) if (memcmp(At(kCoWorldReaders[i].rva), kCoWorldReaders[i].bytes, kCoWorldReaders[i].length) != 0) return 0;
    for (size_t i = 0; i < SINGLES; i++) if (memcmp(At(kCoPageReaders[i].rva), kCoPageReaders[i].bytes, kCoPageReaders[i].length) != 0) return 0;
    for (size_t i = 0; i < IMMEDIATES; i++) if (memcmp(At(kCoPageImmediates[i].rva), kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u) != 0) return 0;
    return 1;
}

/* What every reader reads now, against the game's value times a scale per page. Motion and drop bounds are not a
 * product of the scale and are given. */
static int ReadersRead(float gen, float live)
{
    int ok = 1;
    for (size_t i = 0; i < WORLD; i++)
    {
        const CoWorldReader* s = &kCoWorldReaders[i];
        float game, scale = s->page == CO_PAGE_GEN ? gen : live;
        memcpy(&game, &kCoWorldConstantBits[s->which], 4);
        ok &= Reads(s->rva, s->length) == Bits(s->which == 2 ? game / scale : game * scale);
    }
    for (size_t i = 0; i < SINGLES; i++)
    {
        const CoPageReader* s = &kCoPageReaders[i];
        float game, scale = s->page == CO_PAGE_GEN ? gen : live, want;
        memcpy(&game, &s->value, 4);
        want = game * scale;
        if (s->field == CO_FIELD_DROP_HIGH) want = 1024.0f * scale - 25.0f;
        if (s->field == CO_FIELD_DROP_LOW) want = -(1024.0f * scale - 25.0f);
        if (s->field == CO_FIELD_MOTION_HIGH) want = 1900.0f + 1024.0f * (scale - 1.0f);
        if (s->field == CO_FIELD_MOTION_LOW) want = -(1900.0f + 1024.0f * (scale - 1.0f));
        if (s->field == CO_FIELD_CLEARING) want = game * scale * scale;
        ok &= Reads(s->rva, s->length) == Bits(want);
    }
    return ok;
}

static int ImmediatesHold(float scale)
{
    for (size_t i = 0; i < IMMEDIATES; i++)
    {
        float game, now;
        memcpy(&game, &kCoPageImmediates[i].value, 4);
        memcpy(&now, At(kCoPageImmediates[i].rva + kCoPageImmediates[i].at), 4);
        if (Bits(now) != Bits(game * scale) || memcmp(At(kCoPageImmediates[i].rva), kCoPageImmediates[i].bytes, kCoPageImmediates[i].at) != 0) return 0;
    }
    return 1;
}

static void OnePagePerReader(void)
{
    g_case = "one page per reader";
    const size_t listed = sizeof kWorldSites / sizeof kWorldSites[0];
    size_t gen = 0, live = 0, pointerFunctions = 0;
    uint32_t last = 0;
    CHECK(WORLD == listed && WORLD == 215);
    for (size_t i = 0; i < listed; i++)
    {
        int found = 0;
        for (size_t k = 0; k < WORLD; k++)
            found += kCoWorldReaders[k].rva == kWorldSites[i].rva && kCoWorldReaders[k].length == kWorldSites[i].length && kCoWorldReaders[k].which == kWorldSites[i].which;
        if (found != 1) { printf("FAILED %s: reader 0x%X has %d rows\n", g_case, (unsigned)kWorldSites[i].rva, found); g_failed++; }
    }
    for (int i = 0; i < 5; i++) CHECK(kCoWorldConstants[i] == kWorldConstants[i]);
    for (size_t k = 0; k < WORLD; k++)
    {
        const CoWorldReader* s = &kCoWorldReaders[k];
        int32_t d;
        memcpy(&d, s->bytes + s->length - 4, 4);
        /* the row's bytes are a reader of the constant it names */
        if (s->which > 4 || s->rva + s->length + (uint32_t)d != kCoWorldConstants[s->which]) { printf("FAILED %s: 0x%X does not read its constant\n", g_case, (unsigned)s->rva); g_failed++; }
        if (k > 0 && s->rva <= kCoWorldReaders[k - 1].rva) { printf("FAILED %s: 0x%X is out of order or twice\n", g_case, (unsigned)s->rva); g_failed++; }
        if (s->page == CO_PAGE_GEN)
        {
            gen++;
            /* nothing on the generator's page was placed there by default or is shared with play */
            if (s->flags & (CO_SITE_POINTER | CO_SITE_SHARED)) { printf("FAILED %s: 0x%X is on the generation page and not the generator's alone\n", g_case, (unsigned)s->rva); g_failed++; }
        }
        else if (s->page == CO_PAGE_LIVE)
        {
            live++;
            if (s->flags & CO_SITE_OTHER_ROOTS) { printf("FAILED %s: 0x%X is on the live page with the generation page's flag\n", g_case, (unsigned)s->rva); g_failed++; }
            if ((s->flags & CO_SITE_POINTER) && s->function != last)
            {
                /* the table is in address order, so a function's readers are together */
                printf("live by default (called through a pointer only): the readers in 0x%X\n", (unsigned)s->function);
                pointerFunctions++; last = s->function;
            }
        }
        else { printf("FAILED %s: 0x%X has no page\n", g_case, (unsigned)s->rva); g_failed++; }
    }
    CHECK(gen == CO_WORLD_READERS_GEN && live == CO_WORLD_READERS_LIVE && gen + live == WORLD && gen == 38 && live == 177);
    CHECK(pointerFunctions == CO_WORLD_POINTER_FUNCTIONS);
    /* the bounds: play's on the live page, the generator's two on its own */
    for (size_t i = 0; i < SINGLES; i++)
        CHECK((kCoPageReaders[i].field == CO_FIELD_QUARTER || kCoPageReaders[i].field == CO_FIELD_CLEARING) == (kCoPageReaders[i].page == CO_PAGE_GEN));
}

int main(void)
{
    OnePagePerReader();

    Start("the install");
    CHECK(co_pages_check((uintptr_t)g_fake.image) == NULL && g_fake.writes == 0);
    CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, g_live) == NULL);
    CHECK(g_fake.writes == (int)(WORLD + SINGLES));
    for (size_t i = 0; i < WORLD; i++)
    {
        const CoWorldReader* s = &kCoWorldReaders[i];
        /* the five sizes are the first five values of either page, in the constants' order */
        const uint32_t want = (s->page == CO_PAGE_GEN ? GEN_AT : LIVE_AT) + 4u * s->which;
        if (Target(s->rva, s->length) != want || memcmp(At(s->rva), s->bytes, s->length - 4u) != 0) { printf("FAILED %s: 0x%X is not on its page\n", g_case, (unsigned)s->rva); g_failed++; }
    }
    CHECK(&g_gen->half == (float*)At(GEN_AT) && &g_gen->negFull == (float*)At(GEN_AT + 16) && &g_live->half == (float*)At(LIVE_AT) && &g_live->negFull == (float*)At(LIVE_AT + 16));
    for (size_t i = 0; i < SINGLES; i++)
    {
        const CoPageReader* s = &kCoPageReaders[i];
        const uint32_t at = Target(s->rva, s->length), page = s->page == CO_PAGE_GEN ? GEN_AT : LIVE_AT;
        const uint32_t size = s->page == CO_PAGE_GEN ? sizeof(CoGenPage) : sizeof(CoLivePage);
        if (at < page + 20u || at + 4u > page + size || memcmp(At(s->rva), s->bytes, s->length - 4u) != 0) { printf("FAILED %s: 0x%X is not on its page\n", g_case, (unsigned)s->rva); g_failed++; }
    }
    CHECK((float*)At(Target(0x953642u, 8)) == &g_live->gateHigh && (float*)At(Target(0x5A0199u, 8)) == &g_live->motionLow);
    CHECK((float*)At(Target(0xF5CEE0u, 9)) == &g_gen->quarter && (float*)At(Target(0xF42D1Eu, 8)) == &g_gen->clearing);
    /* pointed at the pages, every reader reads the game's value, bit for bit; the immediates are not written at all */
    CHECK(ReadersRead(1.0f, 1.0f) && ImmediatesHold(1.0f));
    CHECK(g_gen->size == 0.0f && g_gen->capMul == 1 && g_gen->padPx == 1.0f && g_gen->padMetre == 1.0f && g_gen->padQuad == 1.0f && g_gen->flatPx == 1.0f);
    CHECK(co_pages_scale(CO_PAGES_GEN) == 1.0f && co_pages_scale(CO_PAGES_LIVE) == 1.0f);
    CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, g_live) != NULL); /* once */

    g_case = "a preview";
    int writes = g_fake.writes;
    CHECK(co_pages_level(8.0f, 0) == CO_PAGES_GEN);
    CHECK(ReadersRead(8.0f, 1.0f) && ImmediatesHold(8.0f) && g_fake.writes == writes + (int)IMMEDIATES);
    CHECK(co_pages_scale(CO_PAGES_GEN) == 8.0f && co_pages_scale(CO_PAGES_LIVE) == 1.0f);
    CHECK(g_live->half == 1024.0f && g_live->gateHigh == 2000.0f && g_live->dropHigh == 999.0f && g_live->motionHigh == 1900.0f && g_live->maskSpan == 4096.0f);
    CHECK(g_gen->half == 8192.0f && g_gen->full == 16384.0f && g_gen->quarter == 4096.0f && g_gen->padPx == 1.5f / 8.0f && g_gen->flatPx == 0.25f);
    CHECK(co_pages_level(8.0f, 0) == 0 && g_fake.writes == writes + (int)IMMEDIATES); /* the same again writes nothing */

    g_case = "a mission";
    CHECK(co_pages_level(8.0f, 1) == CO_PAGES_LIVE);
    CHECK(ReadersRead(8.0f, 8.0f) && ImmediatesHold(8.0f));
    CHECK(g_live->half == 8192.0f && g_live->inverse == 1.0f / 16384.0f && g_live->gateHigh == 16000.0f && g_live->gateLow == -16000.0f);
    CHECK(g_live->dropHigh == 8167.0f && g_live->dropLow == -8167.0f && g_live->motionHigh == 9068.0f && g_live->goalLow == -32768.0f);

    g_case = "the ship";
    writes = g_fake.writes;
    CHECK(co_pages_ship() == CO_PAGES_LIVE && co_pages_ship() == 0);
    CHECK(ReadersRead(8.0f, 1.0f) && ImmediatesHold(8.0f) && g_fake.writes == writes); /* the generator's side is the next level's to set */
    CHECK(co_pages_level(1.0f, 0) == CO_PAGES_GEN);
    CHECK(ReadersRead(1.0f, 1.0f) && ImmediatesHold(1.0f));
    for (size_t i = 0; i < IMMEDIATES; i++) CHECK(memcmp(At(kCoPageImmediates[i].rva), kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u) == 0);
    CHECK(co_pages_level(1.0f, 1) == 0 && co_pages_level(4.0f, 1) == (CO_PAGES_GEN | CO_PAGES_LIVE) && ReadersRead(4.0f, 4.0f) && ImmediatesHold(4.0f));
    CHECK(co_pages_level(0.5f, 1) == 0 && ReadersRead(4.0f, 4.0f)); /* no world is smaller than the game's */

    /* An immediate that holds neither the game's value nor the mod's last one is somebody else's. */
    Start("an immediate changed by another");
    CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, g_live) == NULL);
    const float other = 3000.0f;
    memcpy(At(kCoPageImmediates[2].rva + kCoPageImmediates[2].at), &other, 4);
    CHECK(co_pages_level(8.0f, 0) == CO_PAGES_GEN);
    float now;
    memcpy(&now, At(kCoPageImmediates[2].rva + kCoPageImmediates[2].at), 4);
    CHECK(now == 3000.0f);
    memcpy(&now, At(kCoPageImmediates[3].rva + kCoPageImmediates[3].at), 4);
    CHECK(now == 16384.0f);

    /* One byte that is not the game's, anywhere: nothing is written, and no level changes anything afterwards. */
    const uint32_t wrong[] = {kCoWorldReaders[0].rva + 1u, kCoWorldReaders[WORLD - 1].rva + 5u, kCoWorldReaders[100].rva, kCoPageReaders[6].rva + 2u,
        kCoPageReaders[12].rva + 7u, kCoPageImmediates[0].rva + 3u, kCoPageImmediates[12].rva + 10u};
    for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; i++)
    {
        Start("another game's code");
        *At(wrong[i]) ^= 0x10;
        CHECK(co_pages_check((uintptr_t)g_fake.image) != NULL);
        CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, g_live) != NULL);
        CHECK(g_fake.writes == 0);
        *At(wrong[i]) ^= 0x10;
        CHECK(Untouched());
        CHECK(co_pages_level(8.0f, 1) == 0 && co_pages_ship() == 0 && g_fake.writes == 0 && Untouched());
        CHECK(co_pages_scale(CO_PAGES_GEN) == 1.0f && co_pages_scale(CO_PAGES_LIVE) == 1.0f);
    }

    /* A page no 32-bit offset reaches: refused before a write, and the far page is never touched. */
    Start("a page out of reach");
    CHECK(co_pages_install((uintptr_t)g_fake.image, (CoGenPage*)(g_fake.image + 0x180000000ull), g_live) != NULL && g_fake.writes == 0 && Untouched());
    Start("a page out of reach");
    CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, (CoLivePage*)(g_fake.image + 0x180000000ull)) != NULL && g_fake.writes == 0 && Untouched());
    CHECK(co_pages_install(0, g_gen, g_live) != NULL && co_pages_install((uintptr_t)g_fake.image, NULL, g_live) != NULL);
    CHECK(co_pages_level(8.0f, 1) == 0 && g_fake.writes == 0);

    /* A write that fails part-way: refused; the readers already pointed read the game's values and no level changes
     * them, so the game is the game it was. */
    const int failAt[] = {0, 100, (int)WORLD, (int)(WORLD + SINGLES) - 1};
    for (size_t i = 0; i < sizeof failAt / sizeof failAt[0]; i++)
    {
        Start("a write that fails");
        g_fake.failWriteAt = failAt[i];
        CHECK(co_pages_install((uintptr_t)g_fake.image, g_gen, g_live) != NULL);
        CHECK(g_fake.writes == failAt[i] + 1);
        g_fake.failWriteAt = -1;
        CHECK(ReadersRead(1.0f, 1.0f) && ImmediatesHold(1.0f));
        CHECK(co_pages_level(8.0f, 1) == 0 && co_pages_ship() == 0 && ReadersRead(1.0f, 1.0f) && ImmediatesHold(1.0f) && g_fake.writes == failAt[i] + 1);
    }

    if (g_failed) { printf("pages: %d checks failed\n", g_failed); return 1; }
    printf("pages: all cases passed\n");
    return 0;
}
