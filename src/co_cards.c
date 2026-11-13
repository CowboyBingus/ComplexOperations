/* The loadout screen's cards in a build for eight: where each card sits and how large it is drawn, per frame, from the
 * screen's size. Eight cards go in two rows of four, shorter than the game's (532 of 780 reference units, the figure
 * cut by the card's own clip and drawn at 0.8) and scaled as one so both rows fit between the bottom line and the top
 * bar. The game's four-card screen keeps its own places. Addresses are game build 25480438's (DESIGN.md of the cards
 * study, layout D). */
#include <string.h>

#include "co.h"

#define CO_CARD_BLOCK 0x2789A8u   /* the eight cards in the screen (loadout_sites.NC), 0x1EE18 apart */
#define CO_CARD_STRIDE 0x1EE18u
#define CO_CARD_FIGURE 0x1FA8u    /* the card's figure widget */
#define CO_CANVAS_SIZE 0x294u     /* in the screen set ([screen + 0]): the root canvas's size in reference units */
#define CO_ENGINE 0x3326340u
#define CO_HUD_SCALE 0xAC4E0u     /* in the engine: the `hud_scale` option */
#define CO_SET_POSITION 0x14476A0u
#define CO_SET_SCALE 0x1447ED0u
#define CO_SET_ANCHOR 0x144F160u

/* The card in reference units: the game's 780 cut to 532 leaves the name plate above the slots and the figure from the
 * helmet to the lower chest. The rows are 44 apart, the game's gap between columns, and the columns 464 apart. */
#define CO_CARD_SHORT 532.0f
#define CO_CARD_GAP 44.0f
#define CO_CARD_PITCH 464.0f
#define CO_CARD_BOTTOM 111.0f     /* the game's card bottom, above the 55-unit box at 56 */
#define CO_TOP_BAR 80.0f          /* the screen's top bar */
#define CO_CORNER 66.0f           /* the hud-scaled corner widgets reach 66 h below the top */
#define CO_FIGURE 0.8f
/* The figure image is 1.444 s f of its render tile's height; at most 0.692 keeps it from being magnified. */
#define CO_FIGURE_MOST 0.692f
#define CO_SCALE_LEAST 0.5f

float co_card_height = 780.0f;
static uintptr_t g_game;
static int g_mode = CO_CARDS_GAME;

typedef void (*SetFn)(uintptr_t widget, uint64_t pair);

void co_cards_install(uintptr_t game) { g_game = game; }

/* The game's own card centres, from its table at 0x21D8FB8. */
static const float kGameX[4] = {0.138f, 0.38f, 0.62f, 0.862f};

float co_cards_scale(float rootHeight, float hudScale)
{
    const float top = hudScale == hudScale && hudScale * CO_CORNER > CO_TOP_BAR ? hudScale * CO_CORNER : CO_TOP_BAR;
    float s = (rootHeight - CO_CARD_BOTTOM - top) / (2.0f * CO_CARD_SHORT + CO_CARD_GAP);
    if (s > 1.0f) s = 1.0f;
    if (s > CO_FIGURE_MOST / CO_FIGURE) s = CO_FIGURE_MOST / CO_FIGURE;
    return s < CO_SCALE_LEAST ? CO_SCALE_LEAST : s;
}

int co_cards_place(int mode, float rootHeight, float hudScale, uint32_t card, CoCardPlace* p)
{
    if (card >= 8u || (mode != CO_CARDS_FOUR && mode != CO_CARDS_EIGHT)) return 0;
    if (mode == CO_CARDS_FOUR)
    {
        *p = (CoCardPlace){card < 4u ? kGameX[card] : 0.5f, CO_CARD_BOTTOM, 1.0f, 1.0f, 20.0f, card < 4u};
        return 1;
    }
    const float s = co_cards_scale(rootHeight, hudScale);
    /* The container is 1080 tall at the top of the root: a taller root moves its bottom up by the difference. */
    const float bottom = CO_CARD_BOTTOM - (rootHeight - 1080.0f);
    p->x = (960.0f + ((float)(card % 4u) - 1.5f) * CO_CARD_PITCH * s) / 1920.0f;
    p->y = card < 4u ? bottom + (CO_CARD_SHORT + CO_CARD_GAP) * s : bottom;
    p->scale = s;
    p->figureScale = CO_FIGURE;
    /* The figure hangs from the card's top: scaled about it, it moves down so the helmet stays 61 under the plate. */
    p->figureY = 20.0f - 145.0f * (1.0f - CO_FIGURE);
    p->shown = 1;
    return 1;
}

static uint64_t Pair(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return (uint64_t)y << 32 | x;
}

/* Every frame L is updated, before its update. The setters return at once when a value is unchanged. */
void co_cards_layout(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rcx;
    if (g_mode == CO_CARDS_GAME || screen == 0 || g_game == 0) return;
    const uintptr_t set = *(const uintptr_t*)screen, engine = *(const uintptr_t*)(g_game + CO_ENGINE);
    if (set == 0 || engine == 0) return;
    const float rootHeight = *(const float*)(set + CO_CANVAS_SIZE + 4u), hudScale = *(const float*)(engine + CO_HUD_SCALE);
    const SetFn position = (SetFn)(g_game + CO_SET_POSITION), scale = (SetFn)(g_game + CO_SET_SCALE), anchor = (SetFn)(g_game + CO_SET_ANCHOR);
    for (uint32_t i = 0; i < 8u; i++)
    {
        CoCardPlace p;
        const uintptr_t card = screen + CO_CARD_BLOCK + i * CO_CARD_STRIDE;
        if (!co_cards_place(g_mode, rootHeight, hudScale, i, &p)) return;
        anchor(card, Pair(p.x, 0.0f));
        position(card, Pair(0.0f, p.y));
        scale(card, Pair(p.scale, p.scale));
        scale(card + CO_CARD_FIGURE, Pair(p.figureScale, p.figureScale));
        position(card + CO_CARD_FIGURE, Pair(0.0f, p.figureY));
    }
}

#ifdef CO_TEST
void co_cards_test_mode(int mode) { g_mode = mode; }
#endif
