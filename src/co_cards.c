/* The loadout screen's cards in a build for eight: where each card sits and how large it is drawn, per frame, from the
 * screen's size. Eight cards go in two rows of four, shorter than the game's (498 of 780 reference units, the slots
 * collapsed and the figure drawn at 0.528) and scaled as one so both rows fit between the bottom line and the mode tabs.
 * An operation for four with at most four players keeps the game's four-card screen. Addresses are game build
 * 25480438's (DESIGN.md of the cards study, layout D and sections 9.5 and 9.7). */
#include <string.h>
#include <windows.h>

#include "co.h"
#include "co_tag.h"

#define CO_CARD_BLOCK 0x2789A8u   /* the eight cards in the screen (loadout_sites.NC), 0x1EE18 apart */
#define CO_CARD_STRIDE 0x1EE18u
#define CO_CARD_PLAYERS 0x278988u /* card -> player index, -1 for none (loadout_sites.NREV) */
#define CO_CARD_FIGURE 0x1FA8u    /* the card's figure widget */
#define CO_CANVAS_SIZE 0x294u     /* in the screen set ([screen + 0]): the root canvas's size in reference units */
#define CO_ENGINE 0x3326340u
#define CO_HUD_SCALE 0xAC4E0u     /* in the engine: the `hud_scale` option */
#define CO_SESSION_PLAYERS 0x16390u /* in the session context: the client sync's player count (read at 0x146CBD9) */
#define CO_TWEENS 0x347CD88u      /* the widgets' running animations: entries of 0x70 */
#define CO_SET_POSITION 0x14476A0u
#define CO_SET_SIZE 0x1447160u
#define CO_SET_SCALE 0x1447ED0u
#define CO_SET_ANCHOR 0x144F160u
#define CO_CARD_ASSIGN 0x189C780u /* (card, data, expanded, r9b): the slots panel open or collapsed, as the game assigns */
#define CO_CARD_DATA 0x1EDF0u
#define CO_CARD_EXPANDED 0x1EE0Cu
#define CO_CARD_FLAG 0x1EE0Eu
#define CO_CARD_PICKER 0x1EE11u   /* the slot list's state (0x189E2F0) */
#define CO_CARD_BOX 0x5958u       /* the slots box; +0x15EC4 set while it is busy */
#define CO_BOX_BUSY 0x15EC4u

/* The card in reference units: 498 is the tallest that keeps the cards at 0.75 of the game's size or more between the
 * tabs and the bottom line. The rows are 44 apart, the game's gap between columns, and the columns 464 apart. */
#define CO_CARD_TALL 780.0f
#define CO_CARD_SHORT 498.0f
#define CO_CARD_GAP 44.0f
#define CO_CARD_PITCH 464.0f
#define CO_CARD_BOTTOM 111.0f     /* the game's card bottom, above the 55-unit box at 56 */
/* The mode tabs (top bar L+0xF950, strip +0x930 at 83, list +0x1CC0 10 under it, tabs 63 tall: 0x17A9CB8, 0x17A9EB6,
 * 0x17AE5BA) end 156 under the container's top, which is the root's on every shape; the cards keep 31 from them. */
#define CO_TABS 187.0f
#define CO_CORNER 66.0f           /* the hud-scaled corner widgets reach 66 h below the top */
/* With the slots collapsed (stack top at 184), the figure at 0.528 shows the helmet to the belt, as the game's own
 * card does above its expanded slots: 64.5 % of the image above the stack in both. */
#define CO_FIGURE 0.528f
/* The figure image is 1.444 s f of its render tile's height; at most 0.692 keeps it from being magnified. */
#define CO_FIGURE_MOST 0.692f
#define CO_SCALE_LEAST 0.5f
/* A parked card's top is this far below the window's bottom edge, whatever the window's shape. */
#define CO_PARK_BELOW 1000.0f

/* How often an open four-card screen asks whether the pick is complex: the answer reads the mission table through
 * HellForge's guarded reads, each a system call. The player terms are read in place every update. */
#define CO_COMPLEX_EVERY_MS 250u

float co_card_height = CO_CARD_TALL;
static uintptr_t g_game, g_screen;
static int g_mode = CO_CARDS_GAME;
static int g_complex;
static uint64_t g_complexAt;
static unsigned g_complexReads;
static uint64_t (*g_clock)(void) = GetTickCount64;

static int Complex(int fresh)
{
    const uint64_t now = g_clock();
    if (fresh || now - g_complexAt >= CO_COMPLEX_EVERY_MS)
    {
        g_complex = co_tag_picked_complex();
        g_complexAt = now;
        g_complexReads++;
    }
    return g_complex;
}

typedef void (*SetFn)(uintptr_t widget, uint64_t pair);
typedef void (*AssignFn)(uintptr_t card, uintptr_t data, uint8_t expanded, uint8_t flag);

void co_cards_install(uintptr_t game) { g_game = game; }

/* The game's own card centres, from its table at 0x21D8FB8. */
static const float kGameX[4] = {0.138f, 0.38f, 0.62f, 0.862f};

int co_cards_eight(int complex, uint32_t players, const int32_t cardPlayers[8])
{
    if (complex || players > 4u) return 1;
    for (uint32_t i = 4; cardPlayers != NULL && i < 8u; i++)
        if (cardPlayers[i] != -1) return 1;
    return 0;
}

float co_cards_scale(float rootHeight, float hudScale)
{
    const float top = hudScale == hudScale && hudScale * CO_CORNER > CO_TABS ? hudScale * CO_CORNER : CO_TABS;
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
        /* y is measured up from the 1080 container's bottom, and the window's bottom is at 1080 - rootHeight. */
        const float y = card < 4u ? CO_CARD_BOTTOM : -(rootHeight + CO_PARK_BELOW);
        *p = (CoCardPlace){card < 4u ? kGameX[card] : 0.5f, y, 1.0f, 1.0f, 20.0f};
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
    return 1;
}

static uint64_t Pair(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return (uint64_t)y << 32 | x;
}

int co_cards_size_target(uintptr_t widget, uintptr_t tweens, float out[2])
{
    memcpy(out, (const void*)(widget + 0xCu), 8);
    const uint32_t flags = *(const uint32_t*)widget, slot = (uint32_t)(*(const uint64_t*)(widget + 0xA8u) >> 11) & 0x7FFu;
    if (!(flags & 0x800000u) || slot == 0 || tweens == 0) return 0;
    /* 0x1446DD0 owns entry slot - 1 by +0x78 and 0x1439170 fills it: +0x20 the value it drives, +0x38 its target. */
    const uintptr_t entry = tweens + (slot - 1u) * 0x70u;
    if (*(const uintptr_t*)(entry + 0x78u) != widget || *(const uintptr_t*)(entry + 0x20u) != widget + 0xCu) return 0;
    memcpy(out, (const void*)(entry + 0x38u), 8);
    return 1;
}

/* Every frame L is updated, and once at the end of its init: the setters return at once when a value is unchanged. */
static void Layout(uintptr_t screen)
{
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

static uint32_t Players(void)
{
    const uintptr_t context = *(const uintptr_t*)(g_game + CO_SESSION_OBJECT);
    return context != 0 ? *(const uint32_t*)(context + CO_SESSION_PLAYERS) : 0u;
}

/* The slot goes first, so every later store and tween of the game takes the short height. The size setter also ends a running
 * animation (0x14471A9..0x1447215, 0x14472C3..), so each part jumps to its end and none keeps a target of 780. */
void co_cards_to_eight(uintptr_t screen)
{
    static const uint32_t kParts[4] = {0x0u, 0x1C30u, 0x1D40u, 0x5848u};
    const uintptr_t tweens = *(const uintptr_t*)(g_game + CO_TWEENS);
    const SetFn size = (SetFn)(g_game + CO_SET_SIZE);
    co_card_height = CO_CARD_SHORT;
    for (uint32_t i = 0; i < 8u; i++)
        for (uint32_t j = 0; j < 4u; j++)
        {
            const uintptr_t w = screen + CO_CARD_BLOCK + i * CO_CARD_STRIDE + kParts[j];
            float to[2];
            const int running = co_cards_size_target(w, tweens, to);
            if (!running && to[1] != CO_CARD_TALL) continue;
            size(w, Pair(to[0], to[1] == CO_CARD_TALL ? CO_CARD_SHORT : to[1]));
        }
    /* The own card was assigned open; the cave at 0x189CA88 collapses the later ones. This is the game's own assign
     * call with the same data, so the panel, its box and the hover test (0x1894C92) all see the collapse. */
    const AssignFn assign = (AssignFn)(g_game + CO_CARD_ASSIGN);
    for (uint32_t i = 0; i < 8u; i++)
    {
        const uintptr_t card = screen + CO_CARD_BLOCK + i * CO_CARD_STRIDE;
        if (*(const uint8_t*)(card + CO_CARD_EXPANDED) == 0) continue;
        /* The assign skips the box's resize while the box is busy or animating (0x189C7B2..0x189C7E9): logged, so a
         * switch with the picker open shows which way it went. */
        const uintptr_t box = card + CO_CARD_BOX;
        const unsigned busy = *(const uint8_t*)(box + CO_BOX_BUSY), picker = *(const uint8_t*)(card + CO_CARD_PICKER);
        const unsigned animating = (*(const uint32_t*)box & 0x800000u) && ((*(const uint32_t*)(box + 0xA8u) >> 11) & 0x7FFu);
        const float before = *(const float*)(box + 0x10u);
        assign(card, *(const uintptr_t*)(card + CO_CARD_DATA), 0, *(const uint8_t*)(card + CO_CARD_FLAG));
        co_log("loadout cards: card %u's slots collapsed (picker %u, box busy %u, animating %u, box height %.1f, then %.1f)",
            i + 1u, picker, busy, animating, before, *(const float*)(box + 0x10u));
    }
    g_mode = CO_CARDS_EIGHT;
}

/* Callback 12, before the init's card loop (rdi = L). The cards are built after it, so they take the slot's height. The
 * card maps are cleared later in the init (0x1466F26), so only the count is read here. */
void co_cards_open(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rdi;
    if (screen == 0 || g_game == 0) return;
    const int complex = Complex(1);
    const uint32_t players = Players();
    g_screen = screen;
    g_mode = co_cards_eight(complex, players, NULL) ? CO_CARDS_EIGHT : CO_CARDS_FOUR;
    co_card_height = g_mode == CO_CARDS_EIGHT ? CO_CARD_SHORT : CO_CARD_TALL;
    co_log("loadout cards: %s (complex operation %s, %u players)", g_mode == CO_CARDS_EIGHT ? "eight" : "the game's four",
        complex ? "yes" : "no", players);
}

/* Callback 13, behind the init's card loop (rdi = L): placed before anything can raise a card's opacity, including
 * the raisers outside L's update (DESIGN.md 9.5.5). */
void co_cards_ready(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rdi;
    if (screen == 0 || screen != g_screen || g_mode == CO_CARDS_GAME || g_game == 0) return;
    Layout(screen);
}

/* Callback 11, before every update of L (rcx = L). Four cards go to eight when the operation turns complex, a fifth
 * player is in, or a card past the fourth has a player: six places assign one, two outside this update (DESIGN.md
 * 9.5.2), so such a card stays parked below the window until here. Never back to four while L is open. */
void co_cards_layout(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rcx;
    if (g_mode == CO_CARDS_GAME || screen == 0 || screen != g_screen || g_game == 0) return;
    if (g_mode == CO_CARDS_FOUR)
    {
        int32_t cards[8];
        memcpy(cards, (const void*)(screen + CO_CARD_PLAYERS), sizeof cards);
        const int complex = Complex(0);
        const uint32_t players = Players();
        if (co_cards_eight(complex, players, cards))
        {
            co_cards_to_eight(screen);
            co_log("loadout cards: four to eight on the open screen (complex operation %s, %u players, cards 5 to 8: %d %d %d %d)",
                complex ? "yes" : "no", players, cards[4], cards[5], cards[6], cards[7]);
        }
    }
    Layout(screen);
}

#ifdef CO_TEST
void co_cards_test_mode(int mode, uintptr_t screen) { g_mode = mode; g_screen = screen; }
int co_cards_test_get_mode(void) { return g_mode; }
void co_cards_test_clock(uint64_t (*clock)(void)) { g_clock = clock != NULL ? clock : GetTickCount64; }
unsigned co_cards_test_complex_reads(void) { return g_complexReads; }
#endif
