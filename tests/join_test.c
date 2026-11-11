/* The host's join gate: a join into a session whose seed string is a complex mission's is held the way the game holds
 * one until the machine has said what it can do, let through to the game's own function when it can, and refused with
 * the game's "Denied" when it cannot or stays silent; every other join is the game's, untouched. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "hellforge_net.h"

void co_peers_test_reset(void);
void co_peers_test_message(const void* data, uint32_t bytes);
void co_capacity_test_on(int on);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SESSION 0x1200000u
#define SYNC (SESSION + 0x159B0u)
#define TEXT (SESSION + 0x164B8u)
#define MARKED "f3fb0000123500000000000b"
#define PLAIN "f3fb0000123400000000000b"

static struct { int gate, update, remove, denied, order; uint64_t args[5], removed[2], deny[4]; int removeAt, denyAt; } g_calls;
static uint64_t Gate(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value)
{
    const uint64_t args[5] = {sync, peer, join, reported, value};
    memcpy(g_calls.args, args, sizeof args); g_calls.gate++;
    return 0;
}
static uint64_t Update(uint64_t sync) { g_calls.update++; return 77; }
static void Remove(uint64_t sync, uint64_t peer)
{
    g_calls.removed[0] = sync; g_calls.removed[1] = peer; g_calls.remove++; g_calls.removeAt = ++g_calls.order;
    for (uint32_t i = 0; i < 8; i++) /* as the game: the peer's slot is free again */
    {
        uint8_t* slot = (uint8_t*)(uintptr_t)sync + 0x20u + i * 0x38u;
        uint64_t at; memcpy(&at, slot, 8);
        if (at == peer) memset(slot, 0, 0x38);
    }
}
static void Denied(uint64_t peer, uint64_t join, uint32_t reason, uint64_t extra)
{
    const uint64_t args[4] = {peer, join, reason, extra};
    memcpy(g_calls.deny, args, sizeof args); g_calls.denied++; g_calls.denyAt = ++g_calls.order;
}

static uint8_t* Slot(uint32_t i) { return g_fake.image + SYNC + 0x20u + i * 0x38u; }
static uint64_t At(const uint8_t* slot, uint32_t offset, size_t size) { uint64_t v = 0; memcpy(&v, slot + offset, size); return v; }
static void Peer(uint32_t i, uint64_t peer, uint32_t state) { memset(Slot(i), 0xEE, 0x38); memcpy(Slot(i), &peer, 8); memcpy(Slot(i) + 0x20, &state, 4); }
static void Says(uint64_t peer, int on) { CoCapability c = {.protocol = CO_SHARE_PROTOCOL, .on = (uint8_t)on, .peer = peer}; co_peers_test_message(&c, sizeof c); }

/* A host on its complex mission with one machine in slot 1, its join not yet confirmed (state 3). */
static uintptr_t Start(const char* name, const char* text)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_peers_test_reset(); co_join_test_reset();
    memset(&g_calls, 0, sizeof g_calls);
    uint8_t* image = g_fake.image;
    const uint64_t session = (uint64_t)(uintptr_t)(image + SESSION);
    const uint32_t hosting = 1;
    co_join_test_image(image, 4);
    memcpy(image + CO_SESSION_OBJECT, &session, 8);
    memcpy(image + SYNC, &session, 8); memcpy(image + SYNC + 0x18, &hosting, 4);
    memcpy(image + TEXT, text, strlen(text) + 1);
    Peer(1, 0xAAAA, 3);
    g_fake.role = HF_SESSION_HOST;
    co_capacity_test_on(1);
    co_peers_start((uintptr_t)image);
    co_join_test_calls((void*)Gate, (void*)Update, (void*)Remove, (void*)Denied, 4);
    return (uintptr_t)image + SYNC;
}

/* The same game before the gate is in. */
static void Bare(const char* name) { Start(name, MARKED); co_join_test_reset(); }

static int Untouched(const uint8_t* slot)
{
    for (int i = 8; i < 0x38; i++) if (i < 0x20 || i >= 0x24) { if (slot[i] != 0xEE) return 0; }
    return 1;
}

int main(void)
{
    g_case = "the seed string";
    CHECK(co_seed_text_marked(MARKED) == 1 && co_seed_text_marked(PLAIN) == 0);
    CHECK(co_seed_text_marked("f3fb0000123500000000000a") == 0);  /* a level 10 mission: its seed's lowest bit says nothing */
    CHECK(co_seed_text_marked("f3fbFFFFFFFF00000000000B") == 1 && co_seed_text_marked("f3fbfffffffe12345678ff0b") == 0);
    CHECK(co_seed_text_marked("f3fc0000123500000000000b") == 0 && co_seed_text_marked("") == 0 && co_seed_text_marked("f3fb00001235") == 0);
    CHECK(co_seed_text_marked("f3fb0000123500000000 00b") == 0);

    uintptr_t sync = Start("the check", MARKED);
    const uintptr_t game = (uintptr_t)g_fake.image;
    CHECK(co_join_check(game) == NULL && g_fake.writes == 0);
    {
        const uint32_t bytes[] = {0x108C784u + 2u, 0x108C7A9u + 9u, 0x108C7E0u + 10u, 0x108C7EEu, 0x108C815u + 7u, 0x108C83Du + 11u, 0x108C858u + 5u,
            0x108C878u + 3u, 0x108AE77u + 12u};
        const uint32_t entries[] = {CO_JOIN_GATE, CO_HOST_UPDATE, CO_JOIN_REMOVE, CO_JOIN_DENIED};
        for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++)
        {
            Bare("the confirmation is another game's");
            g_fake.image[bytes[i]] ^= 0x40;
            CHECK(co_join_check(game) != NULL && co_join_install(game) != NULL && g_fake.hookCount == 0);
        }
        for (size_t i = 0; i < 4; i++)
        {
            Bare("a function is another game's");
            g_fake.noMatch[0] = entries[i];
            CHECK(co_join_check(game) != NULL && co_join_install(game) != NULL && g_fake.hookCount == 0);
        }
        Bare("the slot search's count");
        g_fake.image[0x108C7EDu] = 8; CHECK((co_join_check(game) == NULL) == (CO_PLAYERS == 8)); /* the mod's own count for eight */
        g_fake.image[0x108C7EDu] = 5; CHECK(co_join_check(game) != NULL);
        Bare("the install");
        CHECK(co_join_install(game) == NULL && g_fake.hookCount == 2 && g_fake.hooks[0].target == CO_HOST_UPDATE && g_fake.hooks[1].target == CO_JOIN_GATE);
        CHECK(co_join_install(game) == NULL && g_fake.hookCount == 2 && g_fake.writes == 0);
        Bare("no hook");
        g_fake.noHook = CO_JOIN_GATE; CHECK(co_join_install(game) != NULL);
        Bare("no hook of the update");
        g_fake.noHook = CO_HOST_UPDATE; CHECK(co_join_install(game) != NULL && g_fake.hookCount == 0); /* no gate without its second asker */
    }

    sync = Start("a mission that is not complex", PLAIN);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    CHECK(g_calls.gate == 1 && g_calls.args[0] == sync && g_calls.args[1] == 0xAAAA && g_calls.args[2] == 0x51 && g_calls.args[3] == 0xBBBB && g_calls.args[4] == 6);
    CHECK(Untouched(Slot(1)) && At(Slot(1), 0x20, 4) == 3 && g_calls.remove == 0 && g_calls.denied == 0);

    sync = Start("a machine whose complex part is off", MARKED);
    co_capacity_test_on(0);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    CHECK(g_calls.gate == 1 && Untouched(Slot(1)));

    sync = Start("a machine that can", MARKED);
    Says(0xAAAA, 1);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    CHECK(g_calls.gate == 1 && Untouched(Slot(1)) && g_calls.denied == 0);

    sync = Start("a machine that cannot", MARKED);
    Says(0xAAAA, 0);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    CHECK(g_calls.gate == 0 && g_calls.remove == 1 && g_calls.removed[0] == sync && g_calls.removed[1] == 0xAAAA && g_calls.removeAt == 1);
    /* "Denied" (reason 1, no extra), to the join it asked with, after its slot is gone */
    CHECK(g_calls.denied == 1 && g_calls.deny[0] == 0xAAAA && g_calls.deny[1] == 0x51 && g_calls.deny[2] == 1 && g_calls.deny[3] == 0 && g_calls.denyAt == 2);
    CHECK(fake_host_logged("a join was refused") && fake_host_logged("cannot make it"));

    sync = Start("a machine that has not said", MARKED);
    Peer(2, 0xCCCC, 4); /* a join the game itself holds */
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    CHECK(g_calls.gate == 0 && g_calls.remove == 0 && g_calls.denied == 0);
    /* the game's hold: the int, the join id, the reported id, state 4 */
    CHECK(At(Slot(1), 0, 8) == 0xAAAA && At(Slot(1), 8, 8) == 0xBBBB && At(Slot(1), 0x10, 4) == 6 && At(Slot(1), 0x20, 4) == 4 &&
        At(Slot(1), 0x28, 8) == 0x51 && At(Slot(1), 0x30, 8) == 0 && At(Slot(1), 0x14, 4) == 0xEEEEEEEEu && At(Slot(1), 0x18, 8) == 0xEEEEEEEEEEEEEEEEull);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6); /* the game asks again from its update */
    CHECK(co_join_test_update(sync) == 77 && g_calls.update == 1 && g_calls.gate == 0 && At(Slot(1), 0x20, 4) == 4);
    CHECK(At(Slot(2), 0x20, 4) == 4 && Untouched(Slot(2))); /* not this gate's to release */
    Says(0xAAAA, 1);
    co_join_test_update(sync); /* its message came: the game's function, with the slot's own values */
    CHECK(g_calls.gate == 1 && g_calls.args[0] == sync && g_calls.args[1] == 0xAAAA && g_calls.args[2] == 0x51 && g_calls.args[3] == 0xBBBB && g_calls.args[4] == 6);
    CHECK(g_calls.denied == 0 && g_calls.remove == 0);
    const uint32_t confirmed = 5; memcpy(Slot(1) + 0x20, &confirmed, 4);
    co_join_test_update(sync);
    CHECK(g_calls.gate == 1); /* released once */

    sync = Start("a held machine that says it cannot", MARKED);
    co_join_test_confirm(sync, 0xAAAA, 0x51, 0xBBBB, 6);
    Says(0xAAAA, 0);
    co_join_test_update(sync);
    CHECK(g_calls.gate == 0 && g_calls.remove == 1 && g_calls.denied == 1 && g_calls.deny[1] == 0x51 && At(Slot(1), 0, 8) == 0);
    co_join_test_update(sync);
    CHECK(g_calls.denied == 1);

    sync = Start("the limit", MARKED);
    CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_HOLD && co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000 + CO_JOIN_LIMIT_MS - 1) == CO_JOIN_HOLD);
    CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000 + CO_JOIN_LIMIT_MS) == CO_JOIN_DENY);
    /* refused is forgotten: the machine's next join waits its own time */
    Peer(1, 0xAAAA, 3);
    CHECK(co_join_gate(sync, 0xAAAA, 0x52, 0xBBBB, 6, 90000) == CO_JOIN_HOLD && co_join_gate(sync, 0xAAAA, 0x52, 0xBBBB, 6, 90000 + CO_JOIN_LIMIT_MS - 1) == CO_JOIN_HOLD);

    sync = Start("a held machine that went", MARKED);
    CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_HOLD);
    memset(Slot(1), 0, 0x38); /* the game removed it (a broken connection) */
    co_join_test_update(sync);
    Peer(3, 0xAAAA, 3);
    CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000 + CO_JOIN_LIMIT_MS) == CO_JOIN_HOLD && At(Slot(3), 0x20, 4) == 4); /* a new wait, in its new slot */

    {
        g_case = "the game's own refusals";
        const uint32_t zero = 0, five = 5;
        const uint8_t closed = 1;
        sync = Start("the game's own refusals", MARKED);
        memcpy(g_fake.image + SYNC + 0x18, &zero, 4);       /* not hosting */
        CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT && Untouched(Slot(1)));
        sync = Start("the game's own refusals", MARKED);
        memcpy(g_fake.image + SESSION + 0x1F868u, &closed, 1); /* joins closed */
        CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT && Untouched(Slot(1)));
        sync = Start("the game's own refusals", MARKED);
        CHECK(co_join_gate(sync, 0xDDDD, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT && Untouched(Slot(1)));  /* no slot */
        memcpy(Slot(1) + 0x20, &five, 4);                    /* a slot past the join */
        CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT && Untouched(Slot(1)));
        memcpy(Slot(1) + 0x20, &zero, 4);
        CHECK(co_join_gate(sync, 0xAAAA, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT && Untouched(Slot(1)));
        if (CO_PLAYERS == 4) { Peer(4, 0xFFFF, 3); CHECK(co_join_gate(sync, 0xFFFF, 0x51, 0xBBBB, 6, 1000) == CO_JOIN_ADMIT); } /* the game searches four */
    }

    if (g_failed) { printf("join: %d checks failed\n", g_failed); return 1; }
    printf("join: all cases passed\n");
    return 0;
}
