/* The loadout cards' layout: eight cards in two rows at nine screen sizes, against the pixel places the design predicts
 * and the rules it keeps (under the top bar, on the game's bottom line, inside the 1920-wide container, the figure never
 * magnified); the hud scale's corner widgets; the game's four cards. */
#include <math.h>
#include <stdio.h>

#include "co.h"

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

/* A card's drawn rectangle in pixels from the top left: 420 wide in its 480 root, 532 tall, at its scale. */
static Rect Drawn(float w, float h, float hud, uint32_t card, CoCardPlace* p)
{
    float rootW, rootH;
    const float k = Fit(w, h, &rootW, &rootH);
    co_cards_place(CO_CARDS_EIGHT, rootH, hud, card, p);
    const float centre = (rootW - 1920.0f) / 2.0f + p->x * 1920.0f, bottom = rootH - 1080.0f + p->y;
    return (Rect){(centre - 210.0f * p->scale) * k, (rootH - bottom - 532.0f * p->scale) * k, (centre + 210.0f * p->scale) * k,
                  (rootH - bottom) * k};
}

static int Near(float a, float b) { return fabsf(a - b) < 0.11f; }

/* The design's predictions (DESIGN.md 7): the rows' y and the columns' x in pixels. */
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
        /* 0.802 wherever the height binds, 0.865 on 16:10, where the figure would be magnified past it. */
        CHECK(Near(p[0].scale * 1000.0f, tall ? 865.0f : 802.35f));
        for (uint32_t i = 0; i < 8; i++)
        {
            CHECK(p[i].scale == p[0].scale && p[i].figureScale == 0.8f && Near(p[i].figureY, -9.0f) && p[i].shown);
            CHECK(1.444f * p[i].scale * p[i].figureScale <= 1.0f);
            /* Inside the container and the screen; the top row under the top bar, the bottom row on the game's line. */
            CHECK(r[i].left >= ((rootW - 1920.0f) / 2.0f) * k && r[i].right <= ((rootW + 1920.0f) / 2.0f) * k);
            CHECK(r[i].top >= 80.0f * k - 0.01f && r[i].bottom <= h);
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
        static const float kWide[4] = {106.7f, 675.8f, 722.9f, 1292.0f};
        static const float kWideX[8] = {750.8f, 1200.1f, 1247.2f, 1696.5f, 1743.5f, 2192.8f, 2239.9f, 2689.2f};
        static const float kSmall[4] = {53.3f, 337.9f, 361.4f, 646.0f};
        static const float kSmallX[8] = {155.4f, 380.0f, 403.6f, 628.2f, 651.8f, 876.4f, 900.0f, 1124.6f};
        static const float kTall[4] = {130.6f, 590.8f, 628.8f, 1089.0f};
        static const float kTallX[8] = {176.3f, 539.6f, 577.7f, 941.0f, 979.0f, 1342.3f, 1380.4f, 1743.7f};
        CHECK(Predicted(3440, 1440, kWide, kWideX));
        CHECK(Predicted(1280, 720, kSmall, kSmallX));
        CHECK(Predicted(1920, 1200, kTall, kTallX));
    }

    g_case = "the hud scale";
    {
        /* Up to 80 / 66 the corner widgets stay in the top bar; above, the top row stays under them; 0.5 at the least. */
        CHECK(co_cards_scale(1080.0f, 0.9f) == co_cards_scale(1080.0f, 1.2f) && co_cards_scale(1080.0f, NAN) == co_cards_scale(1080.0f, 0.9f));
        CoCardPlace p;
        co_cards_place(CO_CARDS_EIGHT, 1080.0f, 2.0f, 0, &p);
        CHECK(Near(p.y + 532.0f * p.scale, 1080.0f - 132.0f) && p.scale < co_cards_scale(1080.0f, 0.9f));
        CHECK(co_cards_scale(1080.0f, 100.0f) == 0.5f && co_cards_scale(1080.0f, -5.0f) == co_cards_scale(1080.0f, 0.9f));
    }

    g_case = "the game's four";
    {
        static const float kX[4] = {0.138f, 0.38f, 0.62f, 0.862f};
        for (uint32_t i = 0; i < 8; i++)
        {
            CoCardPlace p;
            CHECK(co_cards_place(CO_CARDS_FOUR, 1200.0f, 2.0f, i, &p) && p.y == 111.0f && p.scale == 1.0f && p.figureScale == 1.0f);
            CHECK(p.figureY == 20.0f && p.shown == (i < 4) && (i >= 4 || p.x == kX[i]));
        }
        CoCardPlace p;
        CHECK(!co_cards_place(CO_CARDS_GAME, 1080.0f, 0.9f, 0, &p) && !co_cards_place(CO_CARDS_EIGHT, 1080.0f, 0.9f, 8, &p));
    }

    g_case = "the callback";
    {
        /* The game's layout until a screen asks for one: nothing is read or written. */
        CoRegs regs = {0};
        regs.rcx = 0x10;
        co_cards_layout(&regs);
        CHECK(co_card_height == 780.0f);
    }

    if (g_failed) { printf("cards: %d failures\n", g_failed); return 1; }
    printf("cards: layout at nine screens, the hud scale and the game's four checked\n");
    return 0;
}
