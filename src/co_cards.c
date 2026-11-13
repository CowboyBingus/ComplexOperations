/* The loadout screen's cards in a build for eight: where each card sits and how large it is drawn, per frame, from the
 * screen's size. Eight cards go in two rows of four, shorter than the game's (498 of 780 reference units, the own
 * card's slots open as in the game, without their MISSION STRATAGEMS heading) and scaled as one so both rows fit
 * between the bottom line and the mode tabs.
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
#define CO_SET_CLIP_FROM 0x144F210u /* a widget's clip rect in its own rect, 0..1 from the bottom left (0x144A130) */
#define CO_SET_CLIP_SIZE 0x144F2F0u
#define CO_CARD_PART 0x1C30u      /* holds the figure and +0x1D40; the slots box is in +0x1D40's +0x5848 */
#define CO_CARD_SLOTS 0x1D40u
#define CO_CARD_BOX 0x5958u
#define CO_FIGURE_FRAME 0x110u    /* in the figure: 420 x 740, centred, clips the image */
#define CO_FIGURE_DISTANCE 0x8ECu /* in the figure: the camera, read by every render (0x18A139A, 0x18A13A5) */
#define CO_FIGURE_HEIGHT 0x8F0u
#define CO_SET_SHOWN 0x14508E0u   /* a widget's shown bit 0x10: off, the draw gives it and its children no colour (0x144BE00) */
#define CO_CARD_PANEL 0x5B78u     /* the slots panel X in the box: 480 x 140, plus the mission height when open */
#define CO_PANEL_OPEN 0xD978u     /* its MISSION STRATAGEMS block shown (0x1895A94) */
#define CO_PANEL_HEADING 0x220u   /* the block's heading, 420 x 40 at 31 under its top: hatch, backing and text */
#define CO_PANEL_MISSION 0x858u   /* the seven 55 x 55 mission slots, top-left at (30 + 60.8 i, -79) in the block */
#define CO_PANEL_SLOT 0x12A8u

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
/* The figure image is 1.444 s f of its render tile's height; at most 0.692 keeps it from being magnified. */
#define CO_FIGURE 0.8f
#define CO_FIGURE_MOST 0.692f
/* The figure's framing (DESIGN.md 9.7.10). The camera comes in to 4.4 f rootH / 1080 m, which gives the image the
 * game's 445.7 units a metre, and sits level at 1.567 m. With the figure 102 above the card's top, the image's centre
 * is at card y 288 and the helmet (1.877 m) 8 under the plate; the image is cut at the slots and the plate. */
#define CO_FIGURE_Y 102.0f
#define CO_CAMERA_DISTANCE 4.4f
#define CO_CAMERA_HEIGHT 1.567f
#define CO_PLATE 64.0f
/* The open panel's 113 for its mission stratagems: 31 to the heading, the heading's 40, 8 to the slots, the slots' 55
 * and 9 to the own slots, less the 30 a closed panel keeps above them (0x1893F30). Eight cards drop the heading and the
 * 8 under it: the slots go up to the heading's line, 31 under the panel's top, so nothing is left where it was. */
#define CO_PANEL_CLOSED 140.0f
#define CO_MISSION_TALL 113.0f
#define CO_MISSION_SHORT 65.0f
#define CO_MISSION_Y (-31.0f)
#define CO_SCALE_LEAST 0.5f
/* A parked card's top is this far below the window's bottom edge, whatever the window's shape. */
#define CO_PARK_BELOW 1000.0f

/* How often an open four-card screen asks whether the pick is complex: the answer reads the mission table through
 * HellForge's guarded reads, each a system call. The player terms are read in place every update. */
#define CO_COMPLEX_EVERY_MS 250u

float co_card_height = CO_CARD_TALL;
float co_card_mission = CO_MISSION_TALL;
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
    p->figureY = CO_FIGURE_Y;
    return 1;
}

void co_cards_figure(float rootHeight, float cardHeight, float partHeight, float slotsTop, CoCardFigure* out)
{
    out->distance = CO_CAMERA_DISTANCE * CO_FIGURE * rootHeight / 1080.0f;
    out->height = CO_CAMERA_HEIGHT;
    /* The figure hangs from +0x1C30's top, scaled about it; its frame is 20 in from either end of its 780. */
    const float frame = partHeight + CO_FIGURE_Y - 760.0f * CO_FIGURE, tall = 740.0f * CO_FIGURE;
    const float from = slotsTop > frame ? slotsTop : frame;
    const float to = cardHeight - CO_PLATE < frame + tall ? cardHeight - CO_PLATE : frame + tall;
    out->clipY = (from - frame) / tall;
    out->clipHeight = to > from ? (to - from) / tall : 0.0f;
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

static float Height(uintptr_t widget) { return *(const float*)(widget + 0x10u); }

/* The figure's camera and its cut, from the parts' sizes as they are this frame: +0x1D40 hangs from +0x1C30's top
 * and the slots box stands on its bottom, so an animation of either moves the cut with it. */
static void Frame(uintptr_t card, float rootHeight)
{
    const uintptr_t figure = card + CO_CARD_FIGURE, part = card + CO_CARD_PART, slots = card + CO_CARD_SLOTS;
    const uintptr_t box = card + CO_CARD_BOX;
    const float slotsTop = Height(part) - Height(slots) + *(const float*)(box + 0x8u) + Height(box);
    CoCardFigure f;
    co_cards_figure(rootHeight, Height(card), Height(part), slotsTop, &f);
    *(float*)(figure + CO_FIGURE_DISTANCE) = f.distance;
    *(float*)(figure + CO_FIGURE_HEIGHT) = f.height;
    ((SetFn)(g_game + CO_SET_CLIP_FROM))(figure + CO_FIGURE_FRAME, Pair(0.0f, f.clipY));
    ((SetFn)(g_game + CO_SET_CLIP_SIZE))(figure + CO_FIGURE_FRAME, Pair(1.0f, f.clipHeight));
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
        if (g_mode == CO_CARDS_EIGHT) Frame(card, rootHeight);
    }
}

static uint32_t Players(void)
{
    const uintptr_t context = *(const uintptr_t*)(g_game + CO_SESSION_OBJECT);
    return context != 0 ? *(const uint32_t*)(context + CO_SESSION_PLAYERS) : 0u;
}

/* The heading hidden and the slots on its line. Only the panel's constructor places the slots or shows the heading, so
 * once per screen holds. An open panel is resized here; the next assign (0x189C780, every update through 0x146C040)
 * lays its box out from that size. */
static void Compact(uintptr_t screen)
{
    const SetFn position = (SetFn)(g_game + CO_SET_POSITION), size = (SetFn)(g_game + CO_SET_SIZE);
    const SetFn shown = (SetFn)(g_game + CO_SET_SHOWN);
    for (uint32_t i = 0; i < 8u; i++)
    {
        const uintptr_t panel = screen + CO_CARD_BLOCK + i * CO_CARD_STRIDE + CO_CARD_PANEL;
        shown(panel + CO_PANEL_HEADING, 0);
        for (uint32_t j = 0; j < 7u; j++)
        {
            const uintptr_t slot = panel + CO_PANEL_MISSION + j * CO_PANEL_SLOT;
            position(slot, Pair(*(const float*)(slot + 0x4u), CO_MISSION_Y));
        }
        if (*(const uint8_t*)(panel + CO_PANEL_OPEN)) size(panel, Pair(*(const float*)(panel + 0xCu), CO_PANEL_CLOSED + co_card_mission));
    }
}

/* The slot goes first, so every later store and tween of the game takes the short height. The size setter also ends a running
 * animation (0x14471A9..0x1447215, 0x14472C3..), so each part jumps to its end and none keeps a target of 780. */
void co_cards_to_eight(uintptr_t screen)
{
    static const uint32_t kParts[4] = {0x0u, 0x1C30u, 0x1D40u, 0x5848u};
    const uintptr_t tweens = *(const uintptr_t*)(g_game + CO_TWEENS);
    const SetFn size = (SetFn)(g_game + CO_SET_SIZE);
    co_card_height = CO_CARD_SHORT;
    co_card_mission = CO_MISSION_SHORT;
    for (uint32_t i = 0; i < 8u; i++)
        for (uint32_t j = 0; j < 4u; j++)
        {
            const uintptr_t w = screen + CO_CARD_BLOCK + i * CO_CARD_STRIDE + kParts[j];
            float to[2];
            const int running = co_cards_size_target(w, tweens, to);
            if (!running && to[1] != CO_CARD_TALL) continue;
            size(w, Pair(to[0], to[1] == CO_CARD_TALL ? CO_CARD_SHORT : to[1]));
        }
    Compact(screen);
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
    co_card_mission = g_mode == CO_CARDS_EIGHT ? CO_MISSION_SHORT : CO_MISSION_TALL;
    co_log("loadout cards: %s (complex operation %s, %u players)", g_mode == CO_CARDS_EIGHT ? "eight" : "the game's four",
        complex ? "yes" : "no", players);
}

/* Callback 13, behind the init's card loop (rdi = L): placed before anything can raise a card's opacity, including
 * the raisers outside L's update (DESIGN.md 9.5.5). */
void co_cards_ready(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rdi;
    if (screen == 0 || screen != g_screen || g_mode == CO_CARDS_GAME || g_game == 0) return;
    if (g_mode == CO_CARDS_EIGHT) Compact(screen);
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
