/* The host's join gate (co.h). A machine that cannot make a complex mission must not be confirmed into a session whose
 * seed string is a complex mission's: the client generates from that string. The gate runs where the game confirms a
 * join, and holds, admits or refuses with the game's own slot state and messages, on the session update's thread. */
#include <string.h>
#include <windows.h>

#include "co.h"

#define SEED_TEXT 0x164B8u /* in the session object S = [L]: the generation seed string */
#define NO_JOINS 0x1F868u  /* in S: the game refuses every join while this byte is set */
#define SLOTS 0x20u        /* in L: the peer slots, 0x38 each: +0 peer, +8 reported id, +0x10 int, +0x20 state, +0x28 join id */
#define SLOT 0x38u
#define COUNT_AT 0x108C7EDu /* the slot search's count: 4, or the mod's own count for more players */

static const char kGatePin[] = "40 55 53 41 55 41 56 48 8D AC 24 78 FF FF FF 48 81 EC 88 01 00 00";
static const char kUpdatePin[] = "40 55 53 57 41 54 48 8D AC 24 A8 FD FF FF 48 81 EC 58 03 00 00";
static const char kRemovePin[] = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC E0 01 00 00 48 8D 99 08 01 00 00";
static const char kDeniedPin[] = "4C 8B DC 4D 89 4B 20 45 89 43 18 49 89 53 10 48 83 EC 68";
/* What the gate repeats or relies on inside the game's function: its refusals, its hold, and the update's re-call. */
static const struct { uint32_t rva; uint8_t size, bytes[27]; } kBody[] = {
    {0x108C784u, 4, {0x83, 0x79, 0x18, 0x01}},                                                 /* cmp dword [L+0x18], 1 */
    {0x108C7A9u, 14, {0x48, 0x8B, 0x05, 0x40, 0x07, 0x3F, 0x02, 0x80, 0xB8, 0x68, 0xF8, 0x01, 0x00, 0x00}}, /* S; cmp byte [S+0x1F868] */
    {0x108C7E0u, 13, {0x48, 0x39, 0x19, 0x74, 0x0D, 0xFF, 0xC0, 0x48, 0x83, 0xC1, 0x38, 0x83, 0xF8}},       /* the slot whose +0 is the peer */
    {0x108C815u, 10, {0x8B, 0x41, 0x20, 0xFF, 0xC8, 0x83, 0xF8, 0x03, 0x76, 0x1E}},            /* its state is 1..4 */
    {0x108C83Du, 27, {0x8B, 0x85, 0xD0, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x89, 0x41, 0x10, 0x0F, 0x11, 0x41, 0x28, 0xC7, 0x41, 0x20, 0x05, 0x00,
        0x00, 0x00, 0x4C, 0x89, 0x49, 0x08}},                                                  /* +0x10 the int, +0x28 0, +8 r9 */
    {0x108C858u, 9, {0x49, 0x8B, 0x06, 0x48, 0x05, 0xB8, 0x64, 0x01, 0x00}},                   /* [L] + 0x164B8 */
    {0x108C878u, 11, {0xC7, 0x41, 0x20, 0x04, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x69, 0x28}},      /* the hold: state 4, +0x28 the join id */
    {0x108AE77u, 22, {0x8B, 0x43, 0x08, 0x49, 0x8B, 0xCC, 0x4C, 0x8B, 0x0B, 0x4C, 0x8B, 0x43, 0x20, 0x48, 0x8B, 0x53, 0xF8, 0x89, 0x44, 0x24, 0x20,
        0xE8}}};                                                                               /* the re-call's arguments, from the slot */

typedef uint64_t (*GateFn)(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value);
typedef uint64_t (*UpdateFn)(uint64_t sync);
typedef void (*RemoveFn)(uint64_t sync, uint64_t peer);
typedef void (*DeniedFn)(uint64_t peer, uint64_t join, uint32_t reason, uint64_t extra);

static struct
{
    GateFn gate;
    UpdateFn update;
    RemoveFn remove;
    DeniedFn denied;
    uint32_t slots;
    struct { uint64_t peer, join, since; } held[8]; /* the joins this gate holds */
} g_join;

static uint64_t* Held(uint64_t peer, uint64_t join, int add)
{
    int spare = -1;
    for (int i = 0; i < 8; i++)
    {
        if (g_join.held[i].peer == peer && g_join.held[i].join == join && peer != 0) return &g_join.held[i].since;
        if (g_join.held[i].peer == 0 && spare < 0) spare = i;
    }
    if (!add || spare < 0) return NULL;
    g_join.held[spare].peer = peer; g_join.held[spare].join = join;
    return &g_join.held[spare].since;
}

static void Forget(uint64_t peer)
{
    for (int i = 0; i < 8; i++) if (g_join.held[i].peer == peer) memset(&g_join.held[i], 0, sizeof g_join.held[i]);
}

static uint8_t* SlotOf(uintptr_t sync, uint64_t peer)
{
    for (uint32_t i = 0; i < g_join.slots; i++)
    {
        uint64_t at = 0;
        const uintptr_t slot = sync + SLOTS + i * SLOT;
        if (g_coApi->read(slot, &at, 8) == 0 && at == peer) return (uint8_t*)slot;
    }
    return NULL;
}

int co_join_gate(uintptr_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value, uint64_t now_ms)
{
    uint64_t session = 0;
    uint32_t hosting = 0, state = 0;
    uint8_t closed = 1;
    char text[24];
    uint8_t* slot;
    /* the game's own refusals stay the game's: its function answers each with its own message */
    if (peer == 0 || g_coApi->read(sync, &session, 8) != 0 || session == 0 || g_coApi->read(sync + 0x18, &hosting, 4) != 0 || hosting != 1 ||
        g_coApi->read((uintptr_t)session + NO_JOINS, &closed, 1) != 0 || closed != 0 || (slot = SlotOf(sync, peer)) == NULL ||
        g_coApi->read((uintptr_t)slot + 0x20, &state, 4) != 0 || state < 1 || state > 4 ||
        g_coApi->read((uintptr_t)session + SEED_TEXT, text, sizeof text) != 0)
    { Forget(peer); return CO_JOIN_ADMIT; }
    const int marked = co_complex_on() && co_seed_text_marked(text);
    uint64_t* since = Held(peer, join, 0);
    const int decision = co_join_decision(marked, co_reports_get(co_peers_reports(), peer), since != NULL ? now_ms - *since : 0);
    if (decision != CO_JOIN_HOLD) { Forget(peer); return decision; }
    if (since == NULL)
    {
        Forget(peer); /* an earlier join of the same machine */
        if ((since = Held(peer, join, 1)) == NULL) return CO_JOIN_DENY;
    }
    if (*since == 0) *since = now_ms != 0 ? now_ms : 1;
    /* the game's hold, as it writes it; its update asks again through this gate while the host is on its ship */
    const uint64_t zero = 0;
    const uint32_t hold = 4;
    memcpy(slot + 0x10, &value, 4); memcpy(slot + 0x28, &join, 8); memcpy(slot + 0x30, &zero, 8);
    memcpy(slot + 0x08, &reported, 8); memcpy(slot + 0x20, &hold, 4);
    return CO_JOIN_HOLD;
}

static uint64_t Decide(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value)
{
    const int decision = co_join_gate((uintptr_t)sync, peer, join, reported, value, GetTickCount64());
    if (decision == CO_JOIN_ADMIT) return g_join.gate(sync, peer, join, reported, value);
    if (decision == CO_JOIN_DENY)
    {
        co_log("complex operations: a join was refused: the mission in play is a complex one and the machine %s",
            co_reports_get(co_peers_reports(), peer) == CO_PEER_OFF ? "cannot make it" : "did not say whether it can make it");
        g_join.remove(sync, peer);
        g_join.denied(peer, join, 1, 0);
    }
    return 0; /* no caller reads it */
}

static uint64_t GateDetour(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value) { return Decide(sync, peer, join, reported, value); }

/* Away from the ship the game does not ask again about a held join: the gate asks itself, behind each update. */
static uint64_t UpdateDetour(uint64_t sync)
{
    const uint64_t result = g_join.update(sync);
    for (uint32_t i = 0; i < g_join.slots; i++)
    {
        struct { uint64_t peer, reported; uint32_t value, pad[3], state, pad2; uint64_t join; } slot;
        if (g_coApi->read((uintptr_t)sync + SLOTS + i * SLOT, &slot, sizeof slot) != 0 || slot.peer == 0 || slot.state != 4 ||
            Held(slot.peer, slot.join, 0) == NULL) continue;
        Decide(sync, slot.peer, slot.join, slot.reported, slot.value);
    }
    /* a held peer whose slot is gone or went on without the gate is held no more */
    for (int i = 0; i < 8; i++)
    {
        uint32_t state = 0;
        const uint8_t* slot = g_join.held[i].peer != 0 ? SlotOf((uintptr_t)sync, g_join.held[i].peer) : NULL;
        if (g_join.held[i].peer != 0 && (slot == NULL || g_coApi->read((uintptr_t)slot + 0x20, &state, 4) != 0 || state != 4))
            memset(&g_join.held[i], 0, sizeof g_join.held[i]);
    }
    return result;
}

/* The slot search's count, or 0 when the search is not the game's. */
static uint32_t Slots(uintptr_t game)
{
    uint8_t count = 0, tail[2];
    if (g_coApi->read(game + COUNT_AT, &count, 1) != 0 || g_coApi->read(game + COUNT_AT + 1u, tail, 2) != 0 || tail[0] != 0x72 || tail[1] != 0xF0) return 0;
    return count == 4 || count == CO_PLAYERS ? count : 0;
}

const char* co_join_check(uintptr_t game)
{
    if (g_coApi->code_matches(game + CO_JOIN_GATE, kGatePin) != 1 || g_coApi->code_matches(game + CO_HOST_UPDATE, kUpdatePin) != 1 ||
        g_coApi->code_matches(game + CO_JOIN_REMOVE, kRemovePin) != 1 || g_coApi->code_matches(game + CO_JOIN_DENIED, kDeniedPin) != 1 || Slots(game) == 0)
        return "the host's join confirmation is not the game's code this mod was made for";
    for (size_t i = 0; i < sizeof kBody / sizeof kBody[0]; i++)
    {
        uint8_t now[sizeof kBody[0].bytes];
        if (g_coApi->read(game + kBody[i].rva, now, kBody[i].size) != 0 || memcmp(now, kBody[i].bytes, kBody[i].size) != 0)
            return "the host's join confirmation is not the game's code this mod was made for";
    }
    return NULL;
}

const char* co_join_install(uintptr_t game)
{
    const char* why;
    if (g_join.gate != NULL) return NULL;
    if ((why = co_join_check(game)) != NULL) return why;
    memset(&g_join, 0, sizeof g_join);
    g_join.slots = Slots(game);
    g_join.remove = (RemoveFn)(game + CO_JOIN_REMOVE);
    g_join.denied = (DeniedFn)(game + CO_JOIN_DENIED);
    /* the update first: a gate without it could hold a join in a mission and never ask again */
    if (g_coApi->hook(g_coMod, game + CO_HOST_UPDATE, (void*)UpdateDetour, (void**)&g_join.update) != 0) { g_join.update = NULL; return "the host's session update could not be hooked"; }
    if (g_coApi->hook(g_coMod, game + CO_JOIN_GATE, (void*)GateDetour, (void**)&g_join.gate) != 0) { g_join.gate = NULL; return "the host's join confirmation could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_join_test_reset(void) { memset(&g_join, 0, sizeof g_join); }
/* The game's bytes the check reads, into a test image; `slots` is the slot search's count. */
void co_join_test_image(uint8_t* image, uint8_t slots)
{
    const uint8_t tail[3] = {slots, 0x72, 0xF0};
    for (size_t i = 0; i < sizeof kBody / sizeof kBody[0]; i++) memcpy(image + kBody[i].rva, kBody[i].bytes, kBody[i].size);
    memcpy(image + COUNT_AT, tail, sizeof tail);
}
void co_join_test_calls(void* gate, void* update, void* remove, void* denied, uint32_t slots)
{
    g_join.slots = slots;
    g_join.gate = (GateFn)gate; g_join.update = (UpdateFn)update; g_join.remove = (RemoveFn)remove; g_join.denied = (DeniedFn)denied;
}
uint64_t co_join_test_confirm(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value) { return GateDetour(sync, peer, join, reported, value); }
uint64_t co_join_test_update(uint64_t sync) { return UpdateDetour(sync); }
#endif
