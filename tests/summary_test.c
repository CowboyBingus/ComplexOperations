/* The mission summary's row for eight: the rule (the game's four cards while no player is on cards 5 to 8, else only
 * the cards with a player), the scale and the row's place at 16:9, 21:9 and 32:9 for five to eight players, the
 * player lookup against the game's probe, and the callback on a stand-in screen: the shown bits, the scales, the
 * container's x, the layout node's mark only on a change, and a research build's fill of the cards without a player. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "co.h"
#include "harness/fake_host.h"

static uint64_t g_now = 5000;
static uint64_t Clock(void) { return g_now; }

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define CARDS 0x8DA28u
#define CARD 0x125C8u
#define ROWS 0x8A548u
#define BOX 0x23A8u
#define NODE 0x8A280u
#define SCREEN 0x120C70u

static uint8_t* g_screen;
static uint8_t g_set[0x300], g_manager[0x100], g_session[0x16400];
static uint32_t g_pairs[32];

static int Near(float a, float b) { return fabsf(a - b) < 0.01f; }
static uint8_t* Card(uint32_t i) { return g_screen + CARDS + i * CARD; }
static uint8_t* Row(uint32_t i) { return g_screen + ROWS + 8u + i * 0x24Cu; }
static float Field(const uint8_t* w, uint32_t at) { float v; memcpy(&v, w + at, 4); return v; }
static uint32_t Word(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static void Put32(void* at, uint32_t value) { memcpy(at, &value, 4); }
static void Put(void* at, uint64_t value) { memcpy(at, &value, 8); }

static int Count(const char* text)
{
    int n = 0;
    for (const char* at = strstr(g_fake.log, text); at != NULL; at = strstr(at + 1, text)) n++;
    return n;
}

static void Code(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* A player in the manager's table of 16 pairs, by the game's probe (factor 3). */
static void Player(uint32_t id)
{
    for (uint32_t i = 0; i < 16; i++)
    {
        uint32_t* pair = g_pairs + 2u * ((i + 3u * id) & 15u);
        if (pair[0] != 0xFFFFFFFCu) continue;
        pair[0] = id;
        pair[1] = 0;
        return;
    }
}

static void Screen(float canvas)
{
    const float one = 1.0f, x = 56.0f, y = -194.0f;
    memset(g_screen, 0, SCREEN);
    memset(g_session, 0, sizeof g_session);
    for (uint32_t i = 0; i < 32; i++) g_pairs[i] = i % 2 ? 0xFFFFFFFFu : 0xFFFFFFFCu;
    Put(g_manager + 0xD0, (uint64_t)(uintptr_t)g_pairs);
    Put32(g_manager + 0xD8, 16);
    Put32(g_manager + 0xDC, 0xFFFFFFFCu);
    Put32(g_manager + 0xE0, 3);
    Put(g_fake.image + 0x3326468u, (uint64_t)(uintptr_t)g_manager);
    Put(g_fake.image + 0x347CEF0u, (uint64_t)(uintptr_t)g_session);
    Put32(g_fake.image + 0x3483C20u, 0xFFFFFFFEu);
    Put32(g_fake.image + 0x348498Cu, 0xFFFFFFFDu);
    memcpy(g_set + 0x294, &canvas, 4);
    Put(g_fake.image + 0x347CE38u, (uint64_t)(uintptr_t)g_set);
    memcpy(g_screen + BOX + 0x4, &x, 4);
    memcpy(g_screen + BOX + 0x8, &y, 4);
    for (uint32_t i = 0; i < 8; i++)
    {
        Put32(Card(i), 0x10);
        memcpy(Card(i) + 0x14, &one, 4);
        memcpy(Card(i) + 0x18, &one, 4);
        Put(Card(i) + 0x12528, (uint64_t)(uintptr_t)Row(i));
        Put32(Card(i) + 0x11948 + 0x8E0, 0xFFFFFFFFu);
    }
    co_summary_test_reset();
}

/* The cards that are shown, as a mask, when every one of them has scale s and the rest is as the game left it. */
static uint32_t Shown(float s)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 8; i++)
    {
        const int on = (Word(Card(i)) & 0x10u) != 0;
        if (!Near(Field(Card(i), 0x14), s) || !Near(Field(Card(i), 0x18), s)) return 0xFFFFFFFFu;
        mask |= (uint32_t)on << i;
    }
    return mask;
}

static void CheckRule(void)
{
    CoSummaryPlace p;
    static const uint8_t kNone[8] = {0}, kFour[8] = {1, 1, 1, 1}, kOwn[8] = {0, 1}, kFive[8] = {1, 1, 1, 1, 1};
    static const uint8_t kLate[8] = {0, 0, 0, 0, 0, 0, 1}, kEight[8] = {1, 1, 1, 1, 1, 1, 1, 1}, kMixed[8] = {1, 0, 0, 1, 0, 1, 1, 0};
    const uint8_t* const vanilla[] = {kNone, kFour, kOwn};
    for (size_t i = 0; i < 3; i++)
        for (float w = 1920.0f; w < 6000.0f; w += 660.0f)
        {
            co_summary_place(vanilla[i], w, &p);
            CHECK(!p.compact && p.cards == 4 && p.scale == 1.0f && p.x == 56.0f && memcmp(p.shown, kFour, 8) == 0);
        }
    /* The row of n cards is 460 n - 40 wide and keeps 56 either side of the canvas; 1920 is the 16:9 canvas, 2580 the
     * 21:9 one at 3440 x 1440, 3840 the 32:9 one. Its centre stays at the window's, 960. */
    static const struct { const uint8_t* present; float canvas; uint32_t cards; float scale; } kRows[] = {
        {kFive, 1920.0f, 5, 1808.0f / 2260.0f}, {kEight, 1920.0f, 8, 1808.0f / 3640.0f}, {kMixed, 1920.0f, 4, 1.0f},
        {kLate, 1920.0f, 1, 1.0f},              {kFive, 2580.0f, 5, 1.0f},               {kEight, 2580.0f, 8, 2468.0f / 3640.0f},
        {kEight, 3840.0f, 8, 1.0f},             {kEight, 100.0f, 8, 1808.0f / 3640.0f},  {kEight, 1.0e9f, 8, 1808.0f / 3640.0f},
        {kEight, NAN, 8, 1808.0f / 3640.0f},
    };
    for (size_t i = 0; i < sizeof kRows / sizeof kRows[0]; i++)
    {
        co_summary_place(kRows[i].present, kRows[i].canvas, &p);
        const float row = (460.0f * (float)kRows[i].cards - 40.0f) * p.scale;
        CHECK(p.compact && p.cards == kRows[i].cards && Near(p.scale, kRows[i].scale) && memcmp(p.shown, kRows[i].present, 8) == 0);
        /* The first shown card's left edge: the container's x, plus that card's own left margin when it is not card 1. */
        const float left = p.x + (kRows[i].present[0] ? 0.0f : 20.0f * p.scale);
        CHECK(Near(left + row / 2.0f, 960.0f) && p.scale <= 1.0f && p.scale > 0.49f);
        const float canvas = kRows[i].canvas >= 1920.0f && kRows[i].canvas <= 30720.0f ? kRows[i].canvas : 1920.0f;
        CHECK(left + (canvas - 1920.0f) / 2.0f > 55.99f && left + row + (canvas - 1920.0f) / 2.0f < canvas - 55.99f);
    }
    /* A row without card 1: cards 2 to 8 at 16:9 fill the room, so the container starts 20 x scale left of 56; card 7
     * alone is centred with its margin taken off. */
    static const uint8_t kNoFirst[8] = {0, 1, 1, 1, 1, 1, 1, 1};
    co_summary_place(kNoFirst, 1920.0f, &p);
    CHECK(Near(p.scale, 1808.0f / 3180.0f) && Near(p.x, 56.0f - 20.0f * p.scale));
    co_summary_place(kLate, 1920.0f, &p);
    CHECK(p.scale == 1.0f && Near(p.x, 960.0f - 210.0f - 20.0f));
    /* Six and seven at 16:9 and 21:9, the numbers the layout's study names. */
    static const uint8_t kSix[8] = {1, 1, 1, 1, 1, 1}, kSeven[8] = {1, 1, 1, 1, 1, 1, 1};
    co_summary_place(kSix, 1920.0f, &p);
    CHECK(Near(p.scale, 0.6647f));
    co_summary_place(kSeven, 1920.0f, &p);
    CHECK(Near(p.scale, 0.5686f));
    co_summary_place(kSix, 2580.0f, &p);
    CHECK(Near(p.scale, 0.9067f));
    co_summary_place(kSeven, 2580.0f, &p);
    CHECK(Near(p.scale, 0.7758f));
}

static void CheckLookup(void)
{
    /* Four pairs, factor 5: id 6 starts at (5 * 6) & 3 = 2. */
    uint32_t pairs[8] = {9, 0, 0xC, 0xFFFFFFFFu, 6, 3, 0xFFFFFFFCu, 0xFFFFFFFFu};
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 6) == 1);
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 9) == 0);    /* 9 starts at 1, the empty key at 3 ends the probe */
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 0xC) == 0);  /* found, its value is -1 */
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 0) == 0);
    pairs[6] = 7;
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 9) == 1);    /* no empty key: the probe goes round to 0 */
    CHECK(co_summary_player(pairs, 4, 0xFFFFFFFCu, 5, 8) == 0);
    CHECK(co_summary_player(NULL, 4, 0xFFFFFFFCu, 5, 6) == 0 && co_summary_player(pairs, 0, 0xFFFFFFFCu, 5, 6) == 0);
}

static void CheckCallback(void)
{
    static const uint8_t kPairAt4[] = {0x48, 0x89, 0x51, 0x04, 0xC3}, kPairAt14[] = {0x48, 0x89, 0x51, 0x14, 0xC3};
    /* mov eax,[rcx]; and eax,~0x10; test dl,dl; je +3; or eax,0x10; mov [rcx],eax; ret */
    static const uint8_t kShown[] = {0x8B, 0x01, 0x83, 0xE0, 0xEF, 0x84, 0xD2, 0x74, 0x03, 0x83, 0xC8, 0x10, 0x89, 0x01, 0xC3};
    Code(0x14476A0u, kPairAt4, sizeof kPairAt4);
    Code(0x1447ED0u, kPairAt14, sizeof kPairAt14);
    Code(0x14508E0u, kShown, sizeof kShown);
    CoRegs regs;
    memset(&regs, 0, sizeof regs);
    regs.rdi = (uint64_t)(uintptr_t)g_screen;

    /* Not installed, no screen, a screen whose cards are not built: nothing is touched. */
    Screen(1920.0f);
    co_summary_install(0);
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0xFFu && g_screen[NODE + 0x28] == 0);
    co_summary_install((uintptr_t)g_fake.image);
    Put(Card(0) + 0x12528, 0);
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0xFFu && g_screen[NODE + 0x28] == 0);

    /* Players on cards 1 and 3: the game's four, cards 5 to 8 out of the row, one pass asked for and no second. */
    Screen(2580.0f);
    Player(0x101);
    Player(0x303);
    Put32(Row(0), 0x101);
    Put32(Row(2), 0x303);
    Player(0xFFFFFFFEu);      /* the ids of no entity are no player, whatever the table holds */
    Player(0xFFFFFFFDu);
    Put32(Row(1), 0xFFFFFFFEu);
    Put32(Row(3), 0xFFFFFFFDu);
    Put32(Row(5), 0x505);      /* an id that is no player */
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x0Fu && g_screen[NODE + 0x28] == 1 && Field(g_screen + BOX, 0x4) == 56.0f && Field(g_screen + BOX, 0x8) == -194.0f);
    CHECK(fake_host_logged("mission summary: no player on cards 5 to 8: the game's four cards"));
    g_screen[NODE + 0x28] = 0;
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x0Fu && g_screen[NODE + 0x28] == 0);
    /* The same row a second later is a new summary: named again, and still no pass asked for. */
    const int named = Count("mission summary: no player on cards 5 to 8");
    g_now += 16;
    co_summary_layout(&regs);
    CHECK(Count("mission summary: no player on cards 5 to 8") == named);
    g_now += 1001;
    co_summary_layout(&regs);
    CHECK(Count("mission summary: no player on cards 5 to 8") == named + 1 && g_screen[NODE + 0x28] == 0);

    /* A player joins card 6 at 21:9: three cards at full size, centred; then five more at 16:9: eight at 0.497. */
    Player(0x505);
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x25u && g_screen[NODE + 0x28] == 1 && Near(Field(g_screen + BOX, 0x4), 960.0f - 1340.0f / 2.0f));
    CHECK(fake_host_logged("mission summary: 3 cards with a player in one row at scale 1.000, the row from x 290.0 (canvas 2580.0 wide, cards 1-3--6--)"));
    Put32(g_screen + CARDS + 5 * CARD + 0x11948 + 0x8E0, 9);
    Put32(g_screen + CARDS + 5 * CARD + 0x11948 + 0x8E4, 5);
    co_summary_layout(&regs);
    CHECK(fake_host_logged("mission summary: card 6: figure position 5, record 9"));
    const float narrow = 1920.0f;
    memcpy(g_set + 0x294, &narrow, 4);
    for (uint32_t i = 0; i < 8; i++)
    {
        Player(0x900 + i);
        Put32(Row(i), 0x900 + i);
    }
    g_screen[NODE + 0x28] = 0;
    co_summary_layout(&regs);
    CHECK(Shown(1808.0f / 3640.0f) == 0xFFu && g_screen[NODE + 0x28] == 1 && Near(Field(g_screen + BOX, 0x4), 56.0f));
    CHECK(Field(g_screen + BOX, 0x8) == -194.0f);
    /* Back to players on the first four only: the game's row again. */
    for (uint32_t i = 4; i < 8; i++) Put32(Row(i), 0);
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x0Fu && Field(g_screen + BOX, 0x4) == 56.0f);

#ifdef CO_RESEARCH
    /* The fill: one player in the session, on card 2; cards 1 and 3 to 7 take that row once the variable is set,
     * each figure set up under its own position (the peer's own, 0, is card 2's: card 1 takes 1) and the
     * peer's position its own again behind each. The stand-in for the figure's player keeps the id at +0x8DC and the
     * session's position at +0x8E4: mov [rcx+0x8DC],edx; mov rax,position; mov eax,[rax]; mov [rcx+0x8E4],eax; ret. */
    uint8_t player[] = {0x89, 0x91, 0xDC, 0x08, 0x00, 0x00, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0x8B, 0x00,
        0x89, 0x81, 0xE4, 0x08, 0x00, 0x00, 0xC3};
    Put(player + 8, (uint64_t)(uintptr_t)(g_session + 0x163AC));
    Code(0x18A1600u, player, sizeof player);
    /* the listener's removal keeps its entity and key at the figure's +0x8E8 and +0x8EC: mov [rdx+0x8E8],ecx; mov [rdx+0x8EC],r8d; ret */
    static const uint8_t kUnlisten[] = {0x89, 0x8A, 0xE8, 0x08, 0x00, 0x00, 0x44, 0x89, 0x82, 0xEC, 0x08, 0x00, 0x00, 0xC3};
    Code(0xB0B020u, kUnlisten, sizeof kUnlisten);
    SetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", "7");
    Screen(1920.0f);
    Player(0x77);
    Put32(Row(1), 0x77);
    Put32(Row(1) + 0x248, 0xABCD);
    Put32(g_session + 0x16390, 2);
    Put32(g_session + 0x163AC, 0);
    for (uint32_t i = 0; i < 8; i++)
    {
        Card(i)[0x11948 + 0x8D8] = 1;
        Put32(Card(i) + 0x12538, 1);
        Put32(Card(i) + 0x12544, 1);
    }
    Put32(Card(1) + 0x12538, 2);
    Put32(Card(1) + 0x12544, 2);
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x0Fu && Word(Row(4)) == 0);      /* two peers: no fill */
    Put32(g_session + 0x16390, 1);
    SetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", NULL);
    g_now += 300;
    co_summary_layout(&regs);
    CHECK(Shown(1.0f) == 0x0Fu && Word(Row(4)) == 0 && Word(Row(0)) == 0);      /* not set: the game's row */
    CHECK(fake_host_logged("mission summary: no player on cards 5 to 8: the game's four cards"));
    SetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", "7");
    g_now += 100;
    co_summary_layout(&regs);
    CHECK(Word(Row(4)) == 0);                              /* asked again only a quarter of a second later */
    g_now += 200;
    co_summary_layout(&regs);
    CHECK(Shown(1808.0f / 3180.0f) == 0x7Fu && Word(g_session + 0x163AC) == 0);
    for (uint32_t i = 0; i < 8; i++)
    {
        const uint8_t* figure = Card(i) + 0x11948;
        const int filled = i < 7 && i != 1;
        CHECK(Word(Row(i)) == (i < 7 ? 0x77u : 0u) && Word(Row(i) + 0x248) == (i < 7 ? 0xABCDu : 0u));
        CHECK(Word(figure + 0x8E4) == (filled ? (i == 0 ? 1u : i) : 0u) && figure[0x8D8] == (filled ? 0 : 1));
        CHECK(Word(figure + 0x8DC) == (filled ? 0x77u : 0u));
        CHECK(Word(figure + 0x8E8) == (filled ? 0x77u : 0u) && Word(figure + 0x8EC) == (filled ? 0xFFFFFFFFu : 0u));
    }
    CHECK(fake_host_logged("mission summary: research fill: card 7 shows the player of card 2 (figure position 6, record -1)"));
    CHECK(fake_host_logged("mission summary: research fill: card 1 shows the player of card 2 (figure position 1, record -1)"));
    /* Behind the fill: a figure loses its player once it is live, and not before; a collapsed card takes the local
     * card's target once that is raised or on its stats, never while the card has no player. */
    Card(4)[0x11948 + 0x8D9] = 1;
    Card(1)[0x11948 + 0x8D9] = 1;                          /* the local card's figure keeps its player */
    Put32(Card(4) + 0x12538, 2);
    Put32(Card(4) + 0x12544, 2);
    Put32(Card(5) + 0x12544, 2);
    co_summary_layout(&regs);
    CHECK(Word(Card(4) + 0x11948 + 0x8DC) == 0xFFFFFFFDu && Word(Card(5) + 0x11948 + 0x8DC) == 0x77u);
    CHECK(Word(Card(4) + 0x12544) == 2u);                  /* the local card is not raised yet */
    Put32(Card(1) + 0x12544, 3);
    co_summary_layout(&regs);
    CHECK(Word(Card(4) + 0x12544) == 3u && Word(Card(5) + 0x12544) == 2u && Word(Card(7) + 0x12544) == 1u);
    CHECK(Word(Card(1) + 0x11948 + 0x8DC) == 0u);
    Put32(Card(4) + 0x12538, 3);
    Put32(Card(5) + 0x12538, 2);
    Put32(Card(1) + 0x12544, 4);
    co_summary_layout(&regs);
    CHECK(Word(Card(4) + 0x12544) == 3u && Word(Card(5) + 0x12544) == 4u);
    Put32(Card(4) + 0x12538, 2);                           /* collapsed on its way up: the game's target stays */
    co_summary_layout(&regs);
    CHECK(Word(Card(4) + 0x12544) == 3u);
    Put32(Card(1) + 0x12544, 5);
    Put32(Card(6) + 0x12538, 2);
    Put32(Card(6) + 0x12544, 2);
    co_summary_layout(&regs);
    CHECK(Word(Card(6) + 0x12544) == 2u);
    /* A new summary (a second without an update) is filled again. */
    for (uint32_t i = 0; i < 8; i++)
        if (i != 1) Put32(Row(i), 0);
    g_now += 2000;
    co_summary_layout(&regs);
    CHECK(Word(Row(0)) == 0x77u && Word(Row(6)) == 0x77u && Word(Row(7)) == 0u);
    /* The unset id as a player: no fill. */
    Screen(1920.0f);
    Player(0x77);
    Player(0xFFFFFFFDu);
    Put32(Row(1), 0x77);
    Put32(g_session + 0x16390, 1);
    co_summary_layout(&regs);
    CHECK(Word(Row(0)) == 0 && fake_host_logged("mission summary: research fill: refused, the unset id is a player"));
    /* The number is the last card filled: 3 fills cards 1 and 3 beside the player's. */
    SetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", "3");
    Screen(1920.0f);
    Player(0x77);
    Put32(Row(1), 0x77);
    Put32(g_session + 0x16390, 1);
    co_summary_layout(&regs);
    CHECK(Word(Row(0)) == 0x77u && Word(Row(2)) == 0x77u && Word(Row(3)) == 0u);
    SetEnvironmentVariableA("HELLFORGE_CO_SUMMARY_FILL", NULL);
#endif
    co_summary_install(0);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_screen = VirtualAlloc(NULL, SCREEN, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (g_screen == NULL) { printf("summary: no memory for the stand-in screen\n"); return 1; }
    co_summary_test_clock(Clock);
    g_case = "the rule and the row";
    CheckRule();
    g_case = "the player lookup";
    CheckLookup();
    g_case = "the callback";
    CheckCallback();
    if (g_failed) { printf("summary: %d failures\n", g_failed); return 1; }
    printf("summary: the rule, the row at three canvases, the player lookup and the callback checked\n");
    return 0;
}
