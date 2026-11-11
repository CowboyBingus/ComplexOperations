/* The session side of the complex part: what the machines tell each other and what the host does with it. The
 * decisions are asked case by case; the row, the message and the launch run against the fake host and an image. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"
#include "hellforge_net.h"

void co_peers_test_reset(void);
void co_peers_test_message(const void* data, uint32_t bytes);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define WAR 0x1000000u
#define SESSION 0x1200000u
#define STATE 0x1400000u
#define OWN 0x1111222233334444ull
typedef uint64_t (*LaunchFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static const uint8_t kLaunch[11] = {0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};

static void Put(uint32_t at, uint64_t value, size_t size) { memcpy(g_fake.image + at, &value, size); }
static uint32_t Seed(uint32_t i) { uint32_t s; memcpy(&s, g_fake.image + WAR + 0xF9A10u + i * 0x4Cu + 0x34u, 4); return s; }
static void Entry(uint32_t i, uint32_t placement, uint32_t seed, uint32_t difficulty)
{
    const uint32_t at = WAR + 0xF9A10u + i * 0x4Cu;
    Put(at + 0x2C, placement, 4); Put(at + 0x34, seed, 4); Put(at + 0x3C, difficulty, 4);
}

static uintptr_t Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_peers_test_reset();
    Put(CO_WAR_OBJECT, (uint64_t)(uintptr_t)(g_fake.image + WAR), 8);
    Put(CO_SESSION_OBJECT, (uint64_t)(uintptr_t)(g_fake.image + SESSION), 8);
    Put(CO_GAME_STATE, (uint64_t)(uintptr_t)(g_fake.image + STATE), 8);
    Put(SESSION + 0xB398u, OWN, 8);
    memcpy(g_fake.image + CO_LAUNCH_STRING, kLaunch, sizeof kLaunch);
    return (uintptr_t)g_fake.image;
}

int main(void)
{
    uintptr_t game = Start("the capability message");
    CoCapability c = {.protocol = CO_SHARE_PROTOCOL, .on = 1, .peer = 7};
    uint64_t peer = 0;
    CHECK(co_capability_read(&c, sizeof c, &peer) == CO_PEER_ON && peer == 7);
    c.on = 0; CHECK(co_capability_read(&c, sizeof c, &peer) == CO_PEER_OFF);
    c.protocol = 2; CHECK(co_capability_read(&c, sizeof c, &peer) == CO_PEER_UNKNOWN);
    c.protocol = CO_SHARE_PROTOCOL; c.peer = 0; CHECK(co_capability_read(&c, sizeof c, &peer) == CO_PEER_UNKNOWN);
    c.peer = 7; CHECK(co_capability_read(&c, sizeof c - 1, &peer) == CO_PEER_UNKNOWN && co_capability_read(&c, 0, &peer) == CO_PEER_UNKNOWN);

    g_case = "the reports";
    CoReports r = {0};
    CHECK(co_reports_get(&r, 7) == CO_PEER_UNKNOWN);
    co_reports_note(&r, 7, CO_PEER_ON); co_reports_note(&r, 8, CO_PEER_OFF); co_reports_note(&r, 0, CO_PEER_ON);
    CHECK(co_reports_get(&r, 7) == CO_PEER_ON && co_reports_get(&r, 8) == CO_PEER_OFF && co_reports_get(&r, 0) == CO_PEER_UNKNOWN && r.count == 2);
    co_reports_note(&r, 7, CO_PEER_OFF); CHECK(co_reports_get(&r, 7) == CO_PEER_OFF && r.count == 2);
    for (uint64_t p = 100; p < 100 + CO_PEERS_MAX; p++) co_reports_note(&r, p, CO_PEER_ON);
    CHECK(r.count == CO_PEERS_MAX && co_reports_get(&r, 100 + CO_PEERS_MAX - 1) == CO_PEER_UNKNOWN); /* no room: unknown, not able */

    g_case = "the launch";
    CoReports l = {0};
    const uint64_t slots[4] = {OWN, 7, 0, 8};
    co_reports_note(&l, 7, CO_PEER_ON);
    CHECK(co_launch_clears(&l, slots, 4, OWN, 1, 0) == 1);      /* 8 said nothing */
    co_reports_note(&l, 8, CO_PEER_OFF); CHECK(co_launch_clears(&l, slots, 4, OWN, 1, 0) == 1);
    co_reports_note(&l, 8, CO_PEER_ON); CHECK(co_launch_clears(&l, slots, 4, OWN, 1, 0) == 0);
    CHECK(co_launch_clears(&l, slots, 4, OWN, 0, 0) == 1);      /* the host's own part is off */
    CHECK(co_launch_clears(&l, slots, 4, OWN, 0, 1) == 0 && co_launch_clears(&l, slots, 2, OWN, 1, 1) == 0); /* the override text decides */
    const uint64_t alone[4] = {OWN, 0, 0, 0};
    CHECK(co_launch_clears(&l, alone, 4, OWN, 1, 0) == 0);

    g_case = "a join";
    CHECK(co_join_decision(0, CO_PEER_UNKNOWN, 0) == CO_JOIN_ADMIT && co_join_decision(0, CO_PEER_OFF, 99999) == CO_JOIN_ADMIT);
    CHECK(co_join_decision(1, CO_PEER_ON, 0) == CO_JOIN_ADMIT && co_join_decision(1, CO_PEER_OFF, 0) == CO_JOIN_DENY);
    CHECK(co_join_decision(1, CO_PEER_UNKNOWN, 0) == CO_JOIN_HOLD && co_join_decision(1, CO_PEER_UNKNOWN, CO_JOIN_LIMIT_MS - 1) == CO_JOIN_HOLD);
    CHECK(co_join_decision(1, CO_PEER_UNKNOWN, CO_JOIN_LIMIT_MS) == CO_JOIN_DENY);
    /* 15 s: a client sends once it holds the host's row, which HellForge may refuse for about 2 s after it joins */
    CHECK(CO_JOIN_LIMIT_MS == 15000u && co_join_decision(1, CO_PEER_UNKNOWN, 14999) == CO_JOIN_HOLD && co_join_decision(1, CO_PEER_UNKNOWN, 15000) == CO_JOIN_DENY);

    game = Start("the host's row");
    co_peers_start(game);
    CHECK(g_fake.subscribed == HF_ON_MESSAGE);
    g_fake.role = HF_SESSION_SOLO; co_peers_tick(1000); CHECK(g_fake.shareSets == 0 && g_fake.sends == 0);
    g_fake.role = HF_SESSION_HOST; g_fake.shareRefused = 1; co_peers_tick(2000); CHECK(g_fake.shareSets == 0 && g_fake.rowBytes == 0);
    co_peers_tick(2500); /* within the second: not asked */
    g_fake.shareRefused = 0; co_peers_tick(2999); CHECK(g_fake.shareSets == 0);
    co_peers_tick(3000); CHECK(g_fake.shareSets == 1 && g_fake.rowBytes == 2 && g_fake.row[0] == CO_SHARE_PROTOCOL && g_fake.row[1] == 0);
    co_peers_tick(4000); CHECK(g_fake.shareSets == 1); /* it holds: nothing to do */
    g_fake.rowBytes = 0; co_peers_tick(5000); CHECK(g_fake.shareSets == 2 && g_fake.rowBytes == 2); /* the table was cleared: set again */
    /* reports reach the host, and only the host */
    c.on = 1; c.peer = 7; co_peers_test_message(&c, sizeof c);
    CHECK(co_reports_get(co_peers_reports(), 7) == CO_PEER_ON);
    co_peers_test_message(&c, 3); c.peer = 9; c.protocol = 5; co_peers_test_message(&c, sizeof c); c.protocol = CO_SHARE_PROTOCOL;
    CHECK(co_peers_reports()->count == 1);
    g_fake.role = HF_SESSION_SOLO; co_peers_tick(6000); CHECK(co_peers_reports()->count == 0); /* the lobby ended */

    game = Start("a client's message");
    co_peers_start(game);
    g_fake.role = HF_SESSION_JOINER;
    co_peers_tick(1000); CHECK(g_fake.sends == 0 && g_fake.shareSets == 0); /* no row yet: nothing is said */
    c.peer = 9; co_peers_test_message(&c, sizeof c); CHECK(co_peers_reports()->count == 0);
    g_fake.row[0] = CO_SHARE_PROTOCOL; g_fake.row[1] = 1; g_fake.rowBytes = 2;
    g_fake.sendRefused = 1; co_peers_tick(2000); CHECK(g_fake.sends == 0);
    co_peers_tick(3000); CHECK(g_fake.sends == 0);
    g_fake.sendRefused = 0; co_peers_tick(4000);
    CoCapability sent; memcpy(&sent, g_fake.sent, sizeof sent);
    CHECK(g_fake.sends == 1 && g_fake.sentTo == HF_TO_HOST && g_fake.sentBytes == sizeof sent && sent.protocol == CO_SHARE_PROTOCOL && sent.on == 0 && sent.peer == OWN);
    co_peers_tick(5000); co_peers_tick(6000); CHECK(g_fake.sends == 1); /* once */
    g_fake.rowBytes = 0; co_peers_tick(7000); g_fake.rowBytes = 2; co_peers_tick(8000); CHECK(g_fake.sends == 2); /* a new table: said again */
    Put(SESSION + 0xB398u, 0, 8); g_fake.rowBytes = 0; co_peers_tick(9000); g_fake.rowBytes = 2; co_peers_tick(10000);
    CHECK(g_fake.sends == 2); /* no peer id of its own yet: nothing a host could match */

    game = Start("the launch detour");
    co_peers_start(game);
    CHECK(co_launch_install(game) == NULL && fake_host_detour(CO_LAUNCH_STRING) != NULL && g_fake.writes == 0);
    const LaunchFn launch = (LaunchFn)fake_host_detour(CO_LAUNCH_STRING);
    Put(WAR + 0xFFC08u, 4, 4);
    Entry(0, 5, 0x1001, 10); Entry(1, 6, 0x2001, 11); Entry(2, 6, 0x3001, 11); Entry(3, 7, 0x4001, 11); Entry(4, 8, 0x5001, 11);
    g_fake.role = HF_SESSION_HOST;
    /* this build's part is off: a host's launch of a level-11 mission drops the mark of the entry the game takes */
    launch(0, 0, 3, 0xAAAAAAAA00000006ull, 0xBBBBBBBB0000000Bull);
    CHECK(Seed(1) == 0x2000 && Seed(2) == 0x3001 && Seed(0) == 0x1001 && Seed(3) == 0x4001);
    launch(0, 0, 3, 5, 10); CHECK(Seed(0) == 0x1001);            /* other levels' seeds are the game's */
    launch(0, 0, 3, 8, 11); CHECK(Seed(4) == 0x5001);            /* past the table's count */
    launch(0, 0, 3, 9, 11); CHECK(Seed(3) == 0x4001);            /* no match */
    Put(STATE + 0xB0u, 'x', 1); launch(0, 0, 3, 7, 11); CHECK(Seed(3) == 0x4001); /* the override text is set */
    Put(STATE + 0xB0u, 0, 1);
    g_fake.role = HF_SESSION_JOINER; launch(0, 0, 3, 7, 11); CHECK(Seed(3) == 0x4001); /* a client's string is the host's */
    g_fake.role = HF_SESSION_SOLO; launch(0, 0, 3, 7, 11); CHECK(Seed(3) == 0x4000);
    Start("another game's code");
    g_fake.image[CO_LAUNCH_STRING + 7] ^= 1;
    CHECK(co_launch_install((uintptr_t)g_fake.image) != NULL && g_fake.hookCount == 0);

    if (g_failed) { printf("peers: %d checks failed\n", g_failed); return 1; }
    printf("peers: all cases passed\n");
    return 0;
}
