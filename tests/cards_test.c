/* The loadout cards' layout: eight cards in two rows at nine screen sizes, against the pixel places the design predicts
 * and the rules it keeps (31 under the mode tabs, on the game's bottom line, inside the 1920-wide container, the figure
 * never magnified); the figure's camera and cut; the hud scale's corner widgets; the game's four cards with cards 5 to 8
 * parked below the window; the mode's rule; and the callbacks on a stand-in screen: the mode at opening, the switch
 * from four to eight with the own card's slots left open, each figure's camera and clip, and the slots panels without
 * their MISSION STRATAGEMS heading in eight. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "co.h"
#include "harness/fake_host.h"

static uint64_t g_now = 1000;
static uint64_t Clock(void) { return g_now; }

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

typedef struct Rect { float left, top, right, bottom; } Rect;

/* The root canvas the game fits to a screen: k pixels a unit, 1080 tall on wide screens and 1920 wide on tall ones. */
static float Fit(float w, float h, float* rootW, float* rootH)
{
    const float k = w / 1920.0f < h / 1080.0f ? w / 1920.0f : h / 1080.0f;
    *rootW = w / k;
    *rootH = h / k;
    return k;
}

/* A card's drawn rectangle in pixels from the top left: 420 wide in its 480 root, 498 tall, at its scale. */
static Rect Drawn(float w, float h, float hud, uint32_t card, CoCardPlace* p)
{
    float rootW, rootH;
    const float k = Fit(w, h, &rootW, &rootH);
    co_cards_place(CO_CARDS_EIGHT, rootH, hud, card, p);
    const float centre = (rootW - 1920.0f) / 2.0f + p->x * 1920.0f, bottom = rootH - 1080.0f + p->y;
    return (Rect){(centre - 210.0f * p->scale) * k, (rootH - bottom - 498.0f * p->scale) * k, (centre + 210.0f * p->scale) * k,
                  (rootH - bottom) * k};
}

static int Near(float a, float b) { return fabsf(a - b) < 0.11f; }

/* The design's predictions (DESIGN.md 9.7.4): the rows' y and the columns' x in pixels. */
static int Predicted(float w, float h, const float rows[4], const float columns[8])
{
    int ok = 1;
    for (uint32_t i = 0; i < 8; i++)
    {
        CoCardPlace p;
        const Rect r = Drawn(w, h, 0.9f, i, &p);
        ok &= Near(r.top, rows[i < 4 ? 0 : 2]) && Near(r.bottom, rows[i < 4 ? 1 : 3]);
        ok &= Near(r.left, columns[2 * (i % 4)]) && Near(r.right, columns[2 * (i % 4) + 1]);
    }
    return ok;
}

/* ---- the callbacks on a stand-in screen ---------------------------------------------------------------------------- */

#define BLOCK 0x2789A8u
#define STRIDE 0x1EE18u
#define PLAYERS 0x278988u

static uint8_t* g_screen;
static uint8_t g_set[0x300], g_context[0x16400], g_tweens[0x70 * 8];
static uint8_t* g_engine;

static void Setter(uint32_t rva, uint8_t field)
{
    /* mov [rcx+field],rdx; ret: the field as a 32-bit displacement, so 0x80 and up are not negative */
    const uint8_t code[] = {0x48, 0x89, 0x91, field, 0x00, 0x00, 0x00, 0xC3};
    DWORD before;
    memcpy(g_fake.image + rva, code, sizeof code);
    CHECK(VirtualProtect(g_fake.image + rva, sizeof code, PAGE_EXECUTE_READWRITE, &before));
}

static void Put(void* at, uint64_t value) { memcpy(at, &value, 8); }
static void Put32(void* at, uint32_t value) { memcpy(at, &value, 4); }
static void Size(uint8_t* w, float x, float y) { memcpy(w + 0xC, &x, 4); memcpy(w + 0x10, &y, 4); }
static float Field(const uint8_t* w, uint32_t at) { float v; memcpy(&v, w + at, 4); return v; }
static uint8_t* Card(uint32_t i) { return g_screen + BLOCK + i * STRIDE; }
static uint8_t* Panel(uint32_t i) { return Card(i) + 0x5B78; }
static uint8_t* Mission(uint32_t i, uint32_t j) { return Panel(i) + 0x858 + j * 0x12A8; }
static uint32_t Flags(const uint8_t* w) { uint32_t v; memcpy(&v, w, 4); return v; }

/* The slots panels as the game builds them: the heading shown, the mission slots at (30 + 60.8 j, -79) in their block,
 * 140 tall, and 253 where the block is open. */
static void Panels(int ownOpen)
{
    for (uint32_t i = 0; i < 8; i++)
    {
        Put32(Panel(i) + 0x220, 0x10);
        Panel(i)[0xD978] = ownOpen && i == 1;
        Size(Panel(i), 480.0f, Panel(i)[0xD978] ? 253.0f : 140.0f);
        for (uint32_t j = 0; j < 7; j++)
        {
            const float x = 30.0f + 60.8f * (float)j, y = -79.0f;
            memcpy(Mission(i, j) + 0x4, &x, 4);
            memcpy(Mission(i, j) + 0x8, &y, 4);
        }
    }
}

/* Every panel in eight: the heading not shown, the slots 48 up with x kept, an open panel 140 + 65. */
static int Compacted(void)
{
    int ok = 1;
    for (uint32_t i = 0; i < 8; i++)
    {
        ok &= (Flags(Panel(i) + 0x220) & 0x10) == 0 && Field(Panel(i), 0x10) == (Panel(i)[0xD978] ? 205.0f : 140.0f);
        for (uint32_t j = 0; j < 7; j++) ok &= Field(Mission(i, j), 0x4) == 30.0f + 60.8f * (float)j && Field(Mission(i, j), 0x8) == -31.0f;
    }
    return ok;
}

/* The game's, in four: the heading shown, the slots at -79, the open panel at 253. */
static int AsBuilt(void)
{
    int ok = 1;
    for (uint32_t i = 0; i < 8; i++)
    {
        ok &= (Flags(Panel(i) + 0x220) & 0x10) != 0 && Field(Panel(i), 0x10) == (Panel(i)[0xD978] ? 253.0f : 140.0f);
        for (uint32_t j = 0; j < 7; j++) ok &= Field(Mission(i, j), 0x8) == -79.0f;
    }
    return ok;
}

/* A running size animation of w in entry `slot` (from 1) towards (x, y), as 0x1446DD0 and 0x1439170 leave it. */
static void Animate(uint8_t* w, uint32_t slot, float x, float y)
{
    uint8_t* entry = g_tweens + (slot - 1) * 0x70;
    uint32_t flags;
    memcpy(&flags, w, 4);
    Put32(w, flags | 0x800000u);
    Put(w + 0xA8, (uint64_t)slot << 11);
    Put(entry + 0x78, (uint64_t)(uintptr_t)w);
    Put(entry + 0x20, (uint64_t)(uintptr_t)(w + 0xC));
    memcpy(entry + 0x38, &x, 4);
    memcpy(entry + 0x3C, &y, 4);
}

static void CheckCallbacks(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    uint8_t* image = g_fake.image;
    g_screen = VirtualAlloc(NULL, BLOCK + 8 * STRIDE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_engine = VirtualAlloc(NULL, 0xAC4E8, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(g_screen != NULL && g_engine != NULL);
    if (g_screen == NULL || g_engine == NULL) return;
    Setter(0x1447160u, 0x0C);      /* size */
    Setter(0x14476A0u, 0x04);      /* position */
    Setter(0x1447ED0u, 0x14);      /* scale */
    Setter(0x144F160u, 0x2C);      /* anchor */
    Setter(0x144F210u, 0xC0);      /* clip from */
    Setter(0x144F2F0u, 0xC8);      /* clip size */
    {
        /* shown: and dword [rcx],~0x10; test dl,dl; je +3; or dword [rcx],0x10; ret */
        static const uint8_t kShown[] = {0x83, 0x21, 0xEF, 0x84, 0xD2, 0x74, 0x03, 0x83, 0x09, 0x10, 0xC3};
        DWORD before;
        memcpy(image + 0x14508E0u, kShown, sizeof kShown);
        CHECK(VirtualProtect(image + 0x14508E0u, sizeof kShown, PAGE_EXECUTE_READWRITE, &before));
    }
    Put(image + 0x3326340u, (uint64_t)(uintptr_t)g_engine);
    Put(image + 0x347CEF0u, (uint64_t)(uintptr_t)g_context);
    Put(image + 0x347CD88u, (uint64_t)(uintptr_t)g_tweens);
    Put(g_screen, (uint64_t)(uintptr_t)g_set);
    const float rootH = 1080.0f, hud = 0.9f;
    memcpy(g_set + 0x298, &rootH, 4);
    memcpy(g_engine + 0xAC4E0, &hud, 4);
    memset(g_screen + PLAYERS, 0xFF, 32);
    Put32(g_context + 0x16390, 3);
    co_cards_install((uintptr_t)image);

    CoRegs regs = {0};
    regs.rdi = regs.rcx = (uintptr_t)g_screen;

    /* Four at opening: the game's height and places, cards 5 to 8 parked, the panels as the game builds them. */
    Panels(1);
    co_cards_open(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_FOUR && co_card_height == 780.0f && co_card_mission == 113.0f);
    co_cards_ready(&regs);
    CHECK(AsBuilt());
    CHECK(Field(Card(1), 0x2C) == 0.38f && Field(Card(1), 0x8) == 111.0f && Field(Card(1), 0x14) == 1.0f);
    CHECK(Field(Card(5), 0x2C) == 0.5f && Field(Card(5), 0x8) == -2080.0f && Field(Card(5) + 0x1FA8, 0x8) == 20.0f);
    /* Four cards: the figure's camera and clip are the game's. */
    CHECK(Field(Card(1) + 0x1FA8, 0x8EC) == 0.0f && Field(Card(1) + 0x1FA8 + 0x110, 0xC8) == 0.0f);
    CHECK(fake_host_logged("loadout cards: the game's four (complex operation no, 3 players)"));

    /* Another screen's update changes nothing. */
    CoRegs other = {0};
    other.rcx = (uintptr_t)g_screen + 8;
    co_cards_layout(&other);
    CHECK(co_cards_test_get_mode() == CO_CARDS_FOUR);

    /* Four or fewer present and no player past card 4: four stays. The complex term is asked at most every 250 ms;
     * the opening asked it already. */
    co_cards_test_clock(Clock);
    const unsigned reads = co_cards_test_complex_reads();
    Put32(g_screen + PLAYERS, 0);
    co_cards_layout(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_FOUR && co_card_height == 780.0f);
    for (int i = 0; i < 20; i++) { g_now += 10; co_cards_layout(&regs); }
    CHECK(co_cards_test_complex_reads() == reads + 1);
    g_now += 250;
    co_cards_layout(&regs);
    CHECK(co_cards_test_complex_reads() == reads + 2 && co_cards_test_get_mode() == CO_CARDS_FOUR);
    CHECK(AsBuilt() && co_card_mission == 113.0f);

    /* Card parts as the game leaves them: roots at 780, one collapsed +0x1C30 at 42, card 1's +0x1D40 animating to 780
     * and its +0x5848 to 128; card 4's +0x1D40 has an entry that drives another widget. */
    for (uint32_t i = 0; i < 8; i++)
    {
        Size(Card(i), 480.0f, 780.0f);
        Size(Card(i) + 0x1C30, 480.0f, i == 2 ? 42.0f : 780.0f);
        Size(Card(i) + 0x1D40, 480.0f, 780.0f);
        Size(Card(i) + 0x5848, 480.0f, 780.0f);
    }
    Size(Card(0) + 0x1D40, 480.0f, 300.0f);
    Animate(Card(0) + 0x1D40, 1, 480.0f, 780.0f);
    Size(Card(0) + 0x5848, 480.0f, 400.0f);
    Animate(Card(0) + 0x5848, 2, 480.0f, 128.0f);
    Size(Card(3) + 0x1D40, 480.0f, 206.0f);
    Animate(Card(3) + 0x1D40, 3, 480.0f, 780.0f);
    Put(g_tweens + 2 * 0x70 + 0x78, 0x1234);
    /* The own player on card 2, assigned with the slots open: the box at the open panel's 253 - 40, the others at
     * 140 - 40, all at y 42. */
    Card(1)[0x1EE0C] = 1;
    for (uint32_t i = 0; i < 8; i++)
    {
        Size(Card(i) + 0x5958, 480.0f, i == 1 ? 213.0f : 100.0f);
        const float y = 42.0f;
        memcpy(Card(i) + 0x5958 + 0x8, &y, 4);
    }

    /* A player on card 6 with three present (a card freed late): eight before that card is next placed. */
    Put32(g_screen + PLAYERS + 5 * 4, 2);
    co_cards_layout(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_EIGHT && co_card_height == 498.0f && co_card_mission == 65.0f);
    CHECK(fake_host_logged("loadout cards: four to eight on the open screen (complex operation no, 3 players, cards 5 to 8: -1 2 -1 -1)"));
    CHECK(!fake_host_logged("slots collapsed"));
    /* Every panel without its heading, the own card's open one at 205; its box follows at the game's next assign. */
    CHECK(Compacted() && Panel(1)[0xD978] == 1);
    for (uint32_t i = 0; i < 8; i++)
    {
        CoCardPlace p;
        co_cards_place(CO_CARDS_EIGHT, rootH, hud, i, &p);
        CHECK(Field(Card(i), 0x10) == 498.0f && Field(Card(i) + 0x1C30, 0x10) == (i == 2 ? 42.0f : 498.0f));
        CHECK(Field(Card(i), 0x2C) == p.x && Field(Card(i), 0x8) == p.y && Field(Card(i), 0x14) == p.scale);
        CHECK(Field(Card(i) + 0x1FA8, 0x14) == 0.8f && Field(Card(i) + 0x1FA8, 0x8) == 102.0f);
        /* The camera at 3.52 m and 1.567 m; the clip from the slots' top (255 open, 142 collapsed) to the plate (434),
         * in the frame from card y -8 to 584. Card 3 is closed (+0x1C30 at 42): its frame is 456 lower. Card 4's
         * +0x1D40 stays at 206, so its slots' top is at the plate. */
        const uint8_t* frame = Card(i) + 0x1FA8 + 0x110;
        const float bottom = i == 2 ? -464.0f : -8.0f, from = i == 1 ? 255.0f : i == 3 ? 434.0f : 142.0f;
        CHECK(Near(Field(Card(i) + 0x1FA8, 0x8EC), 3.52f) && Field(Card(i) + 0x1FA8, 0x8F0) == 1.567f);
        CHECK(Field(frame, 0xC0) == 0.0f && Field(frame, 0xC8) == 1.0f);
        if (i != 2) CHECK(Near(Field(frame, 0xC4) * 592.0f, from - bottom) && Near(Field(frame, 0xCC) * 592.0f, 434.0f - from));
    }
    CHECK(Card(1)[0x1EE0C] == 1);
    CHECK(Field(Card(0) + 0x1D40, 0x10) == 498.0f && Field(Card(0) + 0x5848, 0x10) == 128.0f);
    CHECK(Field(Card(3) + 0x1D40, 0x10) == 206.0f && Field(Card(1) + 0x5848, 0x10) == 498.0f);
    float to[2];
    CHECK(co_cards_size_target((uintptr_t)(Card(0) + 0x5848), (uintptr_t)g_tweens, to) && to[1] == 128.0f);
    CHECK(!co_cards_size_target((uintptr_t)(Card(3) + 0x1D40), (uintptr_t)g_tweens, to) && to[1] == 206.0f);

    /* The game's next assign lays the own card's box out from the panel: 205 - 40. The cut comes down to 207, the
     * EQUIPMENT bar's foot. */
    Size(Card(1) + 0x5958, 480.0f, 165.0f);
    co_cards_layout(&regs);
    CHECK(Near(Field(Card(1) + 0x1FA8 + 0x110, 0xC4) * 592.0f, 207.0f + 8.0f) && Near(Field(Card(1) + 0x1FA8 + 0x110, 0xCC) * 592.0f, 434.0f - 207.0f));

    /* Never back to four while open: the player leaves, eight stays. */
    memset(g_screen + PLAYERS, 0xFF, 32);
    co_cards_layout(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_EIGHT && co_card_height == 498.0f);

    /* The next opening with five players: eight from the start, so the constructor builds at 498 and opens the panel
     * at 140 + 65 (the open comes after the init). The ready callback leaves out the headings before the first draw. */
    Put32(g_context + 0x16390, 5);
    co_card_height = 780.0f;
    co_card_mission = 113.0f;
    Panels(0);
    co_cards_open(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_EIGHT && co_card_height == 498.0f && co_card_mission == 65.0f);
    CHECK(fake_host_logged("loadout cards: eight (complex operation no, 5 players)"));
    co_cards_ready(&regs);
    CHECK(Compacted());

    /* A fifth player joining a four-card screen: switched in the first update after, with the complex term not due. */
    Put32(g_context + 0x16390, 3);
    Panels(1);
    co_cards_open(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_FOUR && co_card_height == 780.0f && co_card_mission == 113.0f);
    co_cards_ready(&regs);
    CHECK(AsBuilt());
    const unsigned asked = co_cards_test_complex_reads();
    Put32(g_context + 0x16390, 5);
    co_cards_layout(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_EIGHT && co_card_height == 498.0f);
    CHECK(co_cards_test_complex_reads() == asked);
    co_cards_test_clock(NULL);

    /* The slot list open on card 2: +0x1D40 at 206 hangs from the top, so the box and the cut move up 292. */
    Size(Card(1) + 0x1D40, 480.0f, 206.0f);
    co_cards_layout(&regs);
    CHECK(Compacted());
    CHECK(Near(Field(Card(1) + 0x1FA8 + 0x110, 0xC4) * 592.0f, 207.0f + 292.0f + 8.0f) && Field(Card(1) + 0x1FA8 + 0x110, 0xCC) == 0.0f);

    /* No session context: four. */
    Put(image + 0x347CEF0u, 0);
    co_cards_open(&regs);
    CHECK(co_cards_test_get_mode() == CO_CARDS_FOUR);

    co_cards_test_mode(CO_CARDS_GAME, 0);
    co_card_height = 780.0f;
    co_card_mission = 113.0f;
}

int main(void)
{
    static const float kScreens[9][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {1280, 800}, {1920, 1200}, {2560, 1600},
                                         {2560, 1080}, {3440, 1440}, {5120, 1440}};

    g_case = "nine screens";
    for (int n = 0; n < 9; n++)
    {
        const float w = kScreens[n][0], h = kScreens[n][1];
        float rootW, rootH;
        const float k = Fit(w, h, &rootW, &rootH);
        const int tall = rootH > 1080.0f;
        CoCardPlace p[8];
        Rect r[8];
        for (uint32_t i = 0; i < 8; i++) r[i] = Drawn(w, h, 0.9f, i, &p[i]);
        /* 0.752 wherever the height is 1080, 0.865 on 16:10 (the figure's limit): 0.75 of the game's size or more. */
        CHECK(Near(p[0].scale * 1000.0f, tall ? 865.0f : 751.92f) && p[0].scale >= 0.75f);
        for (uint32_t i = 0; i < 8; i++)
        {
            CHECK(p[i].scale == p[0].scale && p[i].figureScale == 0.8f && p[i].figureY == 102.0f);
            CHECK(1.444f * p[i].scale * p[i].figureScale <= 1.0f);
            /* Inside the container and the screen; the top row 31 under the tabs' 156, the bottom row on the game's line. */
            CHECK(r[i].left >= ((rootW - 1920.0f) / 2.0f) * k && r[i].right <= ((rootW + 1920.0f) / 2.0f) * k);
            CHECK(r[i].top >= 187.0f * k - 0.01f && r[i].bottom <= h);
            if (i < 4) CHECK(Near(r[i].top, (tall ? 189.4f : 187.0f) * k));
            if (i >= 4) CHECK(Near(r[i].bottom, h - 111.0f * k));
            /* Rows and columns 44 s apart, the same pitch everywhere. */
            if (i % 4) CHECK(Near((r[i].left - r[i - 1].right) / k, 44.0f * p[i].scale));
            if (i < 4) CHECK(Near((r[i + 4].top - r[i].bottom) / k, 44.0f * p[i].scale) && Near(r[i].left, r[i + 4].left));
        }
        /* Symmetric about the container's centre. */
        CHECK(Near(r[0].left + r[3].right, rootW * k));
    }

    g_case = "predictions";
    {
        static const float kWide[4] = {249.3f, 748.6f, 792.7f, 1292.0f};
        static const float kWideX[8] = {811.7f, 1232.8f, 1276.9f, 1697.9f, 1742.1f, 2163.1f, 2207.2f, 2628.3f};
        static const float kSmall[4] = {124.7f, 374.3f, 396.4f, 646.0f};
        static const float kSmallX[8] = {185.8f, 396.4f, 418.4f, 629.0f, 651.0f, 861.6f, 883.6f, 1094.2f};
        static const float kNormal[4] = {187.0f, 561.5f, 594.5f, 969.0f};
        static const float kNormalX[8] = {278.8f, 594.6f, 627.6f, 943.5f, 976.5f, 1292.3f, 1325.4f, 1641.2f};
        static const float kTall[4] = {189.4f, 620.2f, 658.2f, 1089.0f};
        static const float kTallX[8] = {176.3f, 539.6f, 577.7f, 941.0f, 979.0f, 1342.3f, 1380.4f, 1743.7f};
        CHECK(Predicted(3440, 1440, kWide, kWideX));
        CHECK(Predicted(1280, 720, kSmall, kSmallX));
        CHECK(Predicted(1920, 1080, kNormal, kNormalX));
        CHECK(Predicted(1920, 1200, kTall, kTallX));
    }

    g_case = "the figure";
    {
        /* The image (624 units at 0.8) has the game's 445.7 units a metre at every height of the root; its centre is at
         * card y 288, so the helmet top (1.877 m) is 8 under the plate. The cut: the slots' top to the plate. */
        CoCardFigure f;
        co_cards_figure(1080.0f, 498.0f, 498.0f, 255.0f, &f);
        const float perMetre = 624.0f / (2.0f * f.distance * tanf(0.3927f / 2.0f));
        CHECK(Near(f.distance * 100.0f, 352.0f) && f.height == 1.567f && Near(perMetre, 445.6f));
        CHECK(fabsf(498.0f + 102.0f - 312.0f + (1.877f - f.height) * perMetre - 426.0f) < 0.5f);
        CHECK(Near(f.clipY * 592.0f, 263.0f) && Near(f.clipHeight * 592.0f, 179.0f));
        co_cards_figure(1080.0f, 498.0f, 498.0f, 142.0f, &f);
        CHECK(Near(f.clipY * 592.0f, 150.0f) && Near(f.clipHeight * 592.0f, 292.0f));
        co_cards_figure(1200.0f, 498.0f, 498.0f, 142.0f, &f);
        CHECK(Near(624.0f * 1200.0f / 1080.0f / (2.0f * f.distance * tanf(0.3927f / 2.0f)), 445.6f));
        /* Slots above the plate leave nothing; slots under the frame cut from its bottom. */
        co_cards_figure(1080.0f, 498.0f, 498.0f, 450.0f, &f);
        CHECK(f.clipHeight == 0.0f);
        co_cards_figure(1080.0f, 498.0f, 498.0f, -50.0f, &f);
        CHECK(f.clipY == 0.0f && Near(f.clipHeight * 592.0f, 442.0f));
        /* Never magnified: 1.444 s f at most 1 on every shape. */
        CHECK(1.444f * co_cards_scale(1200.0f, 0.9f) * 0.8f <= 1.0f && 1.444f * co_cards_scale(1080.0f, 0.9f) * 0.8f <= 1.0f);
    }

    g_case = "the hud scale";
    {
        /* Up to 187 / 66 the corner widgets stay above the tabs' limit; above, the top row stays under them; 0.5 at the
         * least. */
        CHECK(co_cards_scale(1080.0f, 0.9f) == co_cards_scale(1080.0f, 2.8f) && co_cards_scale(1080.0f, NAN) == co_cards_scale(1080.0f, 0.9f));
        CoCardPlace p;
        co_cards_place(CO_CARDS_EIGHT, 1080.0f, 3.0f, 0, &p);
        CHECK(Near(p.y + 498.0f * p.scale, 1080.0f - 198.0f) && p.scale < co_cards_scale(1080.0f, 0.9f));
        CHECK(co_cards_scale(1080.0f, 100.0f) == 0.5f && co_cards_scale(1080.0f, -5.0f) == co_cards_scale(1080.0f, 0.9f));
    }

    g_case = "the game's four";
    {
        /* Cards 5 to 8 below the window's bottom (1080 - rootH in the container) on any shape, 9:16 portrait included. */
        static const float kX[4] = {0.138f, 0.38f, 0.62f, 0.862f};
        static const float kShapes[4][2] = {{1920, 1080}, {3440, 1440}, {1920, 1200}, {1080, 1920}};
        for (int n = 0; n < 4; n++)
        {
            float rootW, rootH;
            Fit(kShapes[n][0], kShapes[n][1], &rootW, &rootH);
            if (n == 3) CHECK(Near(rootH, 3413.33f));
            for (uint32_t i = 0; i < 8; i++)
            {
                CoCardPlace p;
                CHECK(co_cards_place(CO_CARDS_FOUR, rootH, 2.0f, i, &p) && p.scale == 1.0f && p.figureScale == 1.0f && p.figureY == 20.0f);
                if (i < 4) CHECK(p.x == kX[i] && p.y == 111.0f);
                else CHECK(p.x == 0.5f && p.y == -(rootH + 1000.0f) && p.y + 780.0f < 1080.0f - rootH - 200.0f);
            }
        }
        CoCardPlace p;
        CHECK(!co_cards_place(CO_CARDS_GAME, 1080.0f, 0.9f, 0, &p) && !co_cards_place(CO_CARDS_EIGHT, 1080.0f, 0.9f, 8, &p));
    }

    g_case = "the rule";
    {
        /* complex or more than four players; a player on cards 5 to 8 forces eight even with four or fewer present */
        int32_t cards[8] = {0, 1, 2, 3, -1, -1, -1, -1};
        CHECK(!co_cards_eight(0, 4, NULL) && co_cards_eight(1, 4, NULL) && co_cards_eight(0, 5, NULL) && co_cards_eight(1, 5, NULL));
        CHECK(!co_cards_eight(0, 0, NULL) && co_cards_eight(0, 8, NULL));
        CHECK(!co_cards_eight(0, 4, cards) && co_cards_eight(1, 4, cards) && co_cards_eight(0, 5, cards));
        for (int i = 4; i < 8; i++)
        {
            cards[i] = 1;
            CHECK(co_cards_eight(0, 2, cards));
            cards[i] = -1;
        }
        cards[0] = cards[1] = cards[2] = cards[3] = -1;
        CHECK(!co_cards_eight(0, 1, cards));
    }

    g_case = "the callbacks";
    CheckCallbacks();

    if (g_failed) { printf("cards: %d failures\n", g_failed); return 1; }
    printf("cards: layout at nine screens, the figure, the hud scale, the game's four, the rule and the switch checked\n");
    return 0;
}
