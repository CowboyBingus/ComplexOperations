/* The mission summary's cards in a build for eight: which cards the row shows and how large, per frame, from the
 * players on them and the canvas's width. The game lays the row out itself (0x18F5E40): a card takes
 * (margins + 420) x its scale and a card that is not shown takes nothing, so a scale and a shown bit per card are
 * enough and the hit rectangles follow, being built from the same widget tree (0x178F970).
 * No player on cards 5 to 8: the game's four cards as they are, cards 5 to 8 not shown. Else only the cards with a
 * player, scaled as one so the row fits the canvas less the game's 56 either side, and centred.
 * Addresses are game build 25480438's (scripts/sites/summary_sites.py). */
#include <string.h>
#include <windows.h>

#include "co.h"

#define CO_SUM_CARDS 0x8DA28u     /* the eight cards in the screen (summary_sites.S_NC), 0x125C8 apart */
#define CO_SUM_CARD 0x125C8u
#define CO_SUM_ROWS 0x8A548u      /* the row block R1 (summary_sites.S_NR1): a qword, then rows of 0x24C by player index */
#define CO_SUM_ROW 0x24Cu
#define CO_SUM_BOX 0x23A8u        /* the cards' container, W+0x110: the row's layout root */
#define CO_SUM_NODE 0x8A280u      /* its layout node, W+0x87FE8: +0x28 set asks for a pass (0x18F5C89) */
#define CO_SUM_CARD_ROW 0x12528u  /* in a card: its row in R1 (0x198F090); the row's first dword is the player's id */
#define CO_SUM_FIGURE 0x11948u    /* in a card: the figure widget; +0x8E0 its record (-1 none), +0x8E4 its position */
#define CO_SUM_ROOT 0x347CE38u    /* the widgets' root object; its canvas widget is at +0x288 (0x178F9A9) */
#define CO_SUM_CANVAS 0x294u      /* in it: the root canvas's size in reference units (read at 0x14377DE) */
#define CO_SUM_PLAYERS 0x3326468u /* the player manager: +0xD0 pairs (id, value), +0xD8 their count, +0xDC the empty key,
                                     +0xE0 the hash's factor (0x1990210) */
#define CO_SUM_NO_ID 0x3483C20u
#define CO_SUM_UNSET_ID 0x348498Cu
#define CO_SUM_SET_POSITION 0x14476A0u
#define CO_SUM_SET_SCALE 0x1447ED0u
#define CO_SUM_SET_SHOWN 0x14508E0u
#define CO_SUM_STATE 0x12538u      /* in a card: its state (1 no player, 2 collapsed, 3 raised, 4 stats); +0xC the target */
#define CO_SUM_FIGURE_PLAYER 0x18A1600u /* (figure, player id): the figure takes a player, as the card's refresh calls it */
#define CO_SUM_UNLISTEN 0xB0B020u /* (entity, listener, -1): the listener leaves the entity's events, as 0x18A1630 calls it */
#define CO_SUM_SESSION 0x347CEF0u /* the session context: +0x16390 the peers' count, entries of 0x20 from +0x16398 with
                                     the figure position at +0x14 (0x18A18EC) */

/* The row as the game builds it (0x1992F58, 0x1992F5F, 0x198F1A6, 0x199297E): 420 wide cards, 20 either side of each
 * but the first's left, the container 56 from the 1920 window's left. */
#define CO_SUM_WIDE 420.0f
#define CO_SUM_PITCH 460.0f
#define CO_SUM_SIDE 56.0f
#define CO_SUM_WINDOW 1920.0f

typedef void (*SetFn)(uintptr_t widget, uint64_t pair);
typedef void (*ShownFn)(uintptr_t widget, uint8_t on);

static uintptr_t g_game;
static uint32_t g_logged = ~0u;
static int g_figures;
static uint64_t g_seen;
static uint64_t (*g_clock)(void) = GetTickCount64;

void co_summary_install(uintptr_t game) { g_game = game; }

int co_summary_player(const uint32_t* pairs, uint32_t count, uint32_t empty, uint32_t factor, uint32_t id)
{
    if (pairs == NULL || count == 0) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t* pair = pairs + 2u * ((i + factor * id) & (count - 1u));
        if (pair[0] == id) return pair[1] != 0xFFFFFFFFu;
        if (pair[0] == empty) return 0;
    }
    return 0;
}

void co_summary_place(const uint8_t present[8], float canvasWidth, CoSummaryPlace* p)
{
    uint32_t n = 0, late = 0;
    for (uint32_t i = 0; i < 8u; i++)
    {
        n += present[i] != 0;
        late += i >= 4u && present[i] != 0;
    }
    p->compact = late != 0;
    p->cards = late != 0 ? n : 4u;
    p->scale = 1.0f;
    p->x = CO_SUM_SIDE;
    for (uint32_t i = 0; i < 8u; i++) p->shown[i] = late != 0 ? present[i] != 0 : i < 4u;
    if (late == 0) return;
    /* A canvas is never narrower than the 1920 window; anything else is not a width. */
    const float canvas = canvasWidth >= CO_SUM_WINDOW && canvasWidth <= 16.0f * CO_SUM_WINDOW ? canvasWidth : CO_SUM_WINDOW;
    const float row = (float)n * CO_SUM_PITCH - (CO_SUM_PITCH - CO_SUM_WIDE), room = canvas - 2.0f * CO_SUM_SIDE;
    if (row > room) p->scale = room / row;
    p->x = (CO_SUM_WINDOW - row * p->scale) / 2.0f;
    /* Only card 1's node has no left margin (0x1992F53): a row that starts with another card starts 20 x scale in. */
    if (!present[0]) p->x -= (CO_SUM_PITCH - CO_SUM_WIDE) / 2.0f * p->scale;
}

static uint64_t Pair(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return (uint64_t)y << 32 | x;
}

static uintptr_t Card(uintptr_t screen, uint32_t i) { return screen + CO_SUM_CARDS + i * CO_SUM_CARD; }

static int Present(uintptr_t card)
{
    const uintptr_t row = *(const uintptr_t*)(card + CO_SUM_CARD_ROW), players = *(const uintptr_t*)(g_game + CO_SUM_PLAYERS);
    if (row == 0 || players == 0) return 0;
    const uint32_t id = *(const uint32_t*)row;
    if (id == *(const uint32_t*)(g_game + CO_SUM_NO_ID) || id == *(const uint32_t*)(g_game + CO_SUM_UNSET_ID)) return 0;
    return co_summary_player(*(const uint32_t* const*)(players + 0xD0u), *(const uint32_t*)(players + 0xD8u),
        *(const uint32_t*)(players + 0xDCu), *(const uint32_t*)(players + 0xE0u), id);
}

#ifdef CO_RESEARCH
/* HELLFORGE_CO_SUMMARY_FILL=2..8, alone in a session: cards 2 to that number take the local player's row, each with
 * a figure of its own, so one player sees the row of a full session. The figure is set up here with the peer's position changed for the call only, and loses its player
 * a frame later: the game reads the position again by that player on every update (0x18A10C0, 0x18A17D0) and would
 * move all of them to the peer's one record. The game raises cards once, as the summary opens (0x1993901), so a
 * card filled later takes the local card's target here, as 0x1993510 stores it. */
static int g_fill;
static uint8_t g_filled, g_fillDone;
static uint32_t g_fillFrom;
static uint64_t g_fillAsked;

static void FillCards(uintptr_t screen, uintptr_t session)
{
    const uintptr_t players = *(const uintptr_t*)(g_game + CO_SUM_PLAYERS);
    const uint32_t unset = *(const uint32_t*)(g_game + CO_SUM_UNSET_ID);
    uint32_t from = 8;
    for (uint32_t i = 0; i < 8u && from == 8u; i++)
        if (Present(Card(screen, i))) from = i;
    if (from == 8u) return;
    /* A figure without a player must not be found as one (0x18A17D0's lookup). */
    if (co_summary_player(*(const uint32_t* const*)(players + 0xD0u), *(const uint32_t*)(players + 0xD8u),
            *(const uint32_t*)(players + 0xDCu), *(const uint32_t*)(players + 0xE0u), unset))
    {
        if (g_fillFrom != 9u) co_log("mission summary: research fill: refused, the unset id is a player");
        g_fillFrom = 9;
        return;
    }
    uint32_t* position = (uint32_t*)(session + 0x163ACu);
    const uint32_t own = *position;
    g_fillFrom = from;
    for (uint32_t i = 0; i < (uint32_t)g_fill; i++)
    {
        const uintptr_t card = Card(screen, i), row = screen + CO_SUM_ROWS + 8u + i * CO_SUM_ROW, figure = card + CO_SUM_FIGURE;
        if (i == from || Present(card) || *(const uintptr_t*)(card + CO_SUM_CARD_ROW) != row) continue;
        memcpy((void*)row, (const void*)(screen + CO_SUM_ROWS + 8u + from * CO_SUM_ROW), CO_SUM_ROW);
        /* Not hidden: the game's refresh then leaves the figure's start to its update, which reads no position. */
        *(uint8_t*)(figure + 0x8D8u) = 0;
        *position = i == own ? from : i;
        ((void (*)(uintptr_t, uint32_t))(g_game + CO_SUM_FIGURE_PLAYER))(figure, *(const uint32_t*)row);
        *position = own;
        ((void (*)(uint32_t, uintptr_t, uint32_t))(g_game + CO_SUM_UNLISTEN))(*(const uint32_t*)row, figure, 0xFFFFFFFFu);
        g_filled |= (uint8_t)(1u << i);
        co_log("mission summary: research fill: card %u shows the player of card %u (figure position %d, record %d)", i + 1u,
            from + 1u, *(const int32_t*)(figure + 0x8E4u), *(const int32_t*)(figure + 0x8E0u));
    }
}

static void Fill(uintptr_t screen)
{
    const uintptr_t session = *(const uintptr_t*)(g_game + CO_SUM_SESSION);
    if (session == 0 || *(const uint32_t*)(session + 0x16390u) != 1u) return;
    if (g_filled == 0)
    {
        /* Read while the summary is open, four times a second: a script sets it once the game's row is taken. */
        const uint64_t now = g_clock();
        if (now - g_fillAsked < 250u) return;
        g_fillAsked = now;
        char text[8];
        const DWORD n = GetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", text, sizeof text);
        g_fill = n == 1 && text[0] >= '2' && text[0] <= '8' ? text[0] - '0' : 0;
        if (g_fill != 0) FillCards(screen, session);
        return;
    }
    const uint32_t target = *(const uint32_t*)(Card(screen, g_fillFrom) + CO_SUM_STATE + 0xCu);
    for (uint32_t i = 0; i < 8u; i++)
    {
        const uintptr_t card = Card(screen, i), figure = card + CO_SUM_FIGURE;
        if (!(g_filled >> i & 1u)) continue;
        if (!(g_fillDone >> i & 1u) && *(const uint8_t*)(figure + 0x8D9u) != 0)
        {
            *(uint32_t*)(figure + 0x8DCu) = *(const uint32_t*)(g_game + CO_SUM_UNSET_ID);
            g_fillDone |= (uint8_t)(1u << i);
        }
        uint32_t* state = (uint32_t*)(card + CO_SUM_STATE);
        if (state[0] == 2u && state[3] == 2u && (target == 3u || target == 4u))
        {
            state[3] = target;
            co_log("mission summary: research fill: card %u raised with card %u (target %u)", i + 1u, g_fillFrom + 1u, target);
        }
    }
}
#endif

/* Callback 21, before every update of the summary's widget (rdi = the screen). The setters
 * return at once on an unchanged value; the row's node is marked only when something changed. */
void co_summary_layout(CoRegs* regs)
{
    const uintptr_t screen = (uintptr_t)regs->rdi, set = g_game != 0 ? *(const uintptr_t*)(g_game + CO_SUM_ROOT) : 0;
    if (g_game == 0 || set == 0 || screen == 0 || *(const uintptr_t*)(Card(screen, 0) + CO_SUM_CARD_ROW) == 0) return;
    /* The summary is updated every frame it is open: a second without one is a new summary, named in the log again. */
    const uint64_t now = g_clock();
    if (now - g_seen > 1000u)
    {
        g_logged = ~0u;
        g_figures = 0;
#ifdef CO_RESEARCH
        g_filled = g_fillDone = 0;
        g_fillAsked = 0;
        g_fillFrom = 0;
#endif
    }
    g_seen = now;
#ifdef CO_RESEARCH
    Fill(screen);
#endif
    uint8_t present[8];
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 8u; i++)
    {
        present[i] = (uint8_t)Present(Card(screen, i));
        mask |= (uint32_t)present[i] << i;
    }
    CoSummaryPlace p;
    co_summary_place(present, *(const float*)(set + CO_SUM_CANVAS), &p);
    const SetFn position = (SetFn)(g_game + CO_SUM_SET_POSITION), scale = (SetFn)(g_game + CO_SUM_SET_SCALE);
    const ShownFn shown = (ShownFn)(g_game + CO_SUM_SET_SHOWN);
    const uintptr_t box = screen + CO_SUM_BOX;
    int changed = *(const float*)(box + 0x4u) != p.x;
    for (uint32_t i = 0; i < 8u; i++)
    {
        const uintptr_t card = Card(screen, i);
        const int on = (*(const uint32_t*)card & 0x10u) != 0;
        changed |= on != p.shown[i] || *(const float*)(card + 0x14u) != p.scale || *(const float*)(card + 0x18u) != p.scale;
        shown(card, p.shown[i]);
        scale(card, Pair(p.scale, p.scale));
    }
    position(box, Pair(p.x, *(const float*)(box + 0x8u)));
    if (changed) *(uint8_t*)(screen + CO_SUM_NODE + 0x28u) = 1;
    /* The figures are assigned in the update behind this callback, so their line comes a frame after the row's. */
    if (g_figures)
        for (uint32_t i = 4; i < 8u; i++)
            if (present[i])
                co_log("mission summary: card %u: figure position %d, record %d", i + 1u,
                    *(const int32_t*)(Card(screen, i) + CO_SUM_FIGURE + 0x8E4u), *(const int32_t*)(Card(screen, i) + CO_SUM_FIGURE + 0x8E0u));
    g_figures = 0;
    const uint32_t key = p.compact ? mask | (uint32_t)(p.scale * 65535.0f) << 8 : 0u;
    if (key == g_logged) return;
    g_logged = key;
    if (!p.compact)
    {
        co_log("mission summary: no player on cards 5 to 8: the game's four cards (screen 0x%llX)", (unsigned long long)screen);
        return;
    }
    g_figures = 1;
    co_log("mission summary: %u cards with a player in one row at scale %.3f, the row from x %.1f (canvas %.1f wide, cards %c%c%c%c%c%c%c%c) (screen 0x%llX)",
        p.cards, (double)p.scale, (double)p.x, (double)*(const float*)(set + CO_SUM_CANVAS), present[0] ? '1' : '-',
        present[1] ? '2' : '-', present[2] ? '3' : '-', present[3] ? '4' : '-', present[4] ? '5' : '-', present[5] ? '6' : '-',
        present[6] ? '7' : '-', present[7] ? '8' : '-', (unsigned long long)screen);
}

#ifdef CO_TEST
void co_summary_test_reset(void)
{
    g_logged = ~0u;
    g_figures = 0;
    g_seen = 0;
#ifdef CO_RESEARCH
    g_fill = 0;
    g_fillFrom = 0;
    g_filled = g_fillDone = 0;
    g_fillAsked = 0;
#endif
}
void co_summary_test_clock(uint64_t (*clock)(void)) { g_clock = clock != NULL ? clock : GetTickCount64; }
#endif
