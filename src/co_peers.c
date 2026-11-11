/* The session side of the complex part: who the host is and what it said. The host's answer is one row of the mod's
 * shared table, which HellForge gives every machine of the lobby; no machine assumes an answer it does not hold. */
#include <string.h>

#include "co.h"
#include "hellforge_net.h"

#define HAS(call) (g_coApi != NULL && g_coApi->size >= offsetof(HfApi, call) + sizeof(void*))

int co_peers_is_host(void)
{
    HfSession s = {0};
    if (!HAS(session)) return 1;
    g_coApi->session(g_coMod, &s);
    return s.role != HF_SESSION_JOINER;
}

int co_peers_host_row(void)
{
    CoHostRow row = {0};
    if (co_peers_is_host()) return co_complex_on() ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF;
    /* another protocol's row is no answer this build can read */
    if (!HAS(share_get) || g_coApi->share_get(g_coMod, CO_SHARE_KEY, &row, sizeof row) != sizeof row || row.protocol != CO_SHARE_PROTOCOL)
        return CO_HOST_ROW_NONE;
    return row.on ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF;
}

#define HOST_SYNC 0x159B0u /* in the session object: the host's synchronizer, a peer slot of 0x38 each from +0x20 */
#define OWN_PEER 0xB398u   /* in the session object: this machine's peer id */
#define TABLE 0xF9A10u     /* in the war object: the mission table, entries of 0x4C; their count at +0xFFC08 */
#define TABLE_COUNT 0xFFC08u
#define TABLE_ENTRIES 0x14Au

static struct
{
    uintptr_t game;
    CoReports reports;
    uint32_t role;
    uint64_t next;
    int sent, sentOn; /* a client's message went, and what it said */
} g_peers;

static uint64_t Pointer(uintptr_t at) { uint64_t p = 0; return g_coApi->read(at, &p, 8) == 0 ? p : 0; }

static void OnMessage(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfMessageEvent* m = (const HfMessageEvent*)event;
    uint64_t peer = 0;
    (void)mod; (void)user;
    if (event == NULL || event->size < offsetof(HfMessageEvent, data) || m->bytes > HF_MESSAGE_MAX) return;
    const int capability = co_capability_read(m->data, m->bytes, &peer);
    if (capability != CO_PEER_UNKNOWN && co_peers_is_host()) co_reports_note(&g_peers.reports, peer, capability);
}

void co_peers_start(uintptr_t game)
{
    memset(&g_peers, 0, sizeof g_peers);
    g_peers.game = game;
    if (!HAS(subscribe) || !HAS(send) || !HAS(share_set) || g_coApi->subscribe(g_coMod, HF_ON_MESSAGE, OnMessage, NULL, 0) != 0)
        co_log("complex operations: this HellForge has no channel between machines; other machines stay unknown");
}

const CoReports* co_peers_reports(void) { return &g_peers.reports; }

void co_peers_tick(uint64_t now_ms)
{
    HfSession s = {0};
    if (g_peers.game == 0 || now_ms < g_peers.next || !HAS(session) || !HAS(send) || !HAS(share_set) || !HAS(share_get)) return;
    g_peers.next = now_ms + 1000u;
    g_coApi->session(g_coMod, &s);
    /* another lobby's reports and message are not this one's; no event says the host changed, so the role is asked */
    if (s.role != g_peers.role) { memset(&g_peers.reports, 0, sizeof g_peers.reports); g_peers.sent = 0; g_peers.role = s.role; }
    const int on = co_complex_on();
    if (s.role == HF_SESSION_HOST)
    {
        const CoHostRow want = {CO_SHARE_PROTOCOL, (uint8_t)on};
        CoHostRow have = {0};
        /* refused while HellForge still counts this machine a joiner: asked again next time */
        if (g_coApi->share_get(g_coMod, CO_SHARE_KEY, &have, sizeof have) != sizeof have || memcmp(&have, &want, sizeof want) != 0)
            g_coApi->share_set(g_coMod, CO_SHARE_KEY, &want, sizeof want);
    }
    else if (s.role == HF_SESSION_JOINER)
    {
        if (co_peers_host_row() == CO_HOST_ROW_NONE) { g_peers.sent = 0; return; } /* the message follows the row, never leads it */
        if (g_peers.sent && g_peers.sentOn == on) return;
        CoCapability message = {.protocol = CO_SHARE_PROTOCOL, .on = (uint8_t)on};
        const uint64_t session = Pointer(g_peers.game + CO_SESSION_OBJECT);
        if (session == 0 || g_coApi->read((uintptr_t)session + OWN_PEER, &message.peer, 8) != 0 || message.peer == 0) return;
        if (g_coApi->send(g_coMod, HF_TO_HOST, &message, sizeof message) == 0) { g_peers.sent = 1; g_peers.sentOn = on; }
    }
}

/* The seed string's build, 0x12D2CF0(_, out, planet, placement, difficulty): it takes the first entry of the mission
 * table whose +0x2C and +0x3C match and encodes it; all three callers send the string on at once. Dropping the mark in
 * that entry first is dropping it for every machine. */
static const uint8_t kLaunch[11] = {0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};
typedef uint64_t (*LaunchFn)(uint64_t unused, uint64_t out, uint64_t planet, uint64_t placement, uint64_t difficulty);
static LaunchFn g_launch;

static void LaunchClear(uint32_t placement, uint32_t difficulty)
{
    const uint64_t war = Pointer(g_peers.game + CO_WAR_OBJECT), session = Pointer(g_peers.game + CO_SESSION_OBJECT);
    const uint64_t state = Pointer(g_peers.game + CO_GAME_STATE);
    uint64_t present[8] = {0}, own = 0;
    uint32_t count = 0;
    uint8_t override = 1;
    HfSession s = {0};
    if (war == 0 || session == 0 || state == 0 || !HAS(session) || g_coApi->read((uintptr_t)war + TABLE_COUNT, &count, 4) != 0) return;
    g_coApi->session(g_coMod, &s);
    if (s.role == HF_SESSION_JOINER) return; /* a client's string is the host's */
    if (g_coApi->read((uintptr_t)state + 0xB0, &override, 1) != 0 || g_coApi->read((uintptr_t)session + OWN_PEER, &own, 8) != 0) return;
    const uint32_t slots = co_managers_moved() ? CO_PLAYERS : 4u;
    for (uint32_t i = 0; i < slots && i < 8u; i++) g_coApi->read((uintptr_t)session + HOST_SYNC + 0x20u + i * 0x38u, &present[i], 8);
    if (!co_launch_clears(&g_peers.reports, present, slots < 8u ? slots : 8u, own, co_complex_on(), override != 0)) return;
    if (count > TABLE_ENTRIES) count = TABLE_ENTRIES;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t* entry = (uint8_t*)(uintptr_t)war + TABLE + i * 0x4Cu;
        uint32_t fields[5]; /* +0x2C placement, +0x30, +0x34 seed, +0x38, +0x3C difficulty */
        if (g_coApi->read((uintptr_t)entry + 0x2C, fields, sizeof fields) != 0) return;
        if (fields[0] != placement || fields[4] != difficulty) continue;
        if (difficulty == CO_COMPLEX_LEVEL) { const uint32_t seed = co_seed_marked(fields[2], 0); memcpy(entry + 0x34, &seed, 4); }
        return; /* the first match is the one the game takes */
    }
}

static uint64_t LaunchDetour(uint64_t unused, uint64_t out, uint64_t planet, uint64_t placement, uint64_t difficulty)
{
    LaunchClear((uint32_t)placement, (uint32_t)difficulty);
    return g_launch(unused, out, planet, placement, difficulty);
}

const char* co_launch_install(uintptr_t game)
{
    uint8_t now[sizeof kLaunch];
    if (g_launch != NULL) return NULL;
    if (g_coApi->read(game + CO_LAUNCH_STRING, now, sizeof now) != 0 || memcmp(now, kLaunch, sizeof now) != 0)
        return "the seed string's build is not the game's code this mod was made for";
    g_peers.game = game;
    if (g_coApi->hook(g_coMod, game + CO_LAUNCH_STRING, (void*)LaunchDetour, (void**)&g_launch) != 0) { g_launch = NULL; return "the seed string's build could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_peers_test_reset(void) { memset(&g_peers, 0, sizeof g_peers); g_launch = NULL; }
void co_peers_test_message(const void* data, uint32_t bytes)
{
    HfMessageEvent m = {.info = {sizeof m, HF_ON_MESSAGE, 0}, .from = 99, .bytes = bytes};
    memcpy(m.data, data, bytes);
    OnMessage(g_coMod, &m.info, NULL);
}
#endif
