/* The tree cutters' glue (src/xl/proto/living_world_cutters.c): the research switch's text, and the centres a tick is
 * given: every player at the streamer's place for it or, for 30 s, at the last one; on the host the online squads
 * that no player's reach holds, up to the set's limit. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_cutters.h"
#include "xl/co_shim.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int CoXlTestLwCutParse(const char* text, unsigned* v);
int CoXlTestLwCutClient(const char* text);
void CoXlTestLwCutSquad(int i, int state, int every, float x, float y);
uint32_t CoXlTestLwCutCentres(const uint64_t* players, int count, const uint64_t* peer, float (*place)[2], int streamed, int host, int none, uint64_t now, CoCutCentre* c, uint32_t* out);
uint32_t CoXlTestLwCutAsked(int i, uint64_t now);
int CoXlTestLwCutVerdict(int cut, uint32_t waitedMs);
enum { WAITS, IS_CUT, GAMES, LATE };
enum { OFFLINE = 1, PENDING = 2, ONLINE = 3 };
/* The switch as LwCutInit leaves it (0 off), a squad's wait made `ms` longer, the requests by verdict and the
 * longest wait, and LwOnlineGates for squad 0 with its placement and room answered here (living world test). */
void CoXlTestLwCutMode(int mode);
void CoXlTestLwCutWaited(int i, uint32_t ms);
void CoXlTestLwCutGate(uint32_t* five);
int CoXlTestLwGates(int held, int placeOk, int roomOk, int* places, int* rooms, int* retryMs);
/* The launch measurements (LwCutNavSlot, LwCutBodyStep, LwCutRepathStep). */
int CoXlTestLwCutNavSlot(uint32_t handle, uint32_t count, uint32_t mask, uint32_t live, uint32_t form, uint32_t* key, uint32_t* at);
int CoXlTestLwCutBodyStep(int reset, float x, float y, uint32_t goals, uint32_t state);
uint32_t CoXlTestLwCutRepathStep(uint32_t i, uint32_t handle, uint32_t count);

static void Measures(void)
{
    uint32_t key = 0, at = 0;
    g_case = "a navigator handle's slot, as the engine's lookup";
    /* slots of 0x18 bytes, the handle at +0x10, the pointer at +0x08; index mask 0xFFFF; live bit 0x10000 */
    CHECK(CoXlTestLwCutNavSlot(0x80010003u, 8, 0xFFFFu, 0x10000u, 0x08100018u, &key, &at) == 1 && key == 3 * 0x18 + 0x10 && at == 3 * 0x18 + 0x08);
    CHECK(CoXlTestLwCutNavSlot(0x80010007u, 8, 0xFFFFu, 0x10000u, 0x08100018u, &key, &at) == 1 && key == 7 * 0x18 + 0x10);
    CHECK(CoXlTestLwCutNavSlot(0x80010008u, 8, 0xFFFFu, 0x10000u, 0x08100018u, &key, &at) == 0); /* past the pool's count */
    CHECK(CoXlTestLwCutNavSlot(0x80000003u, 8, 0xFFFFu, 0x10000u, 0x08100018u, &key, &at) == 0); /* not a live handle */
    CHECK(CoXlTestLwCutNavSlot(0xFFFFFFFFu, 0x10000, 0xFFFFu, 0x10000u, 0x08100018u, &key, &at) == 0); /* none */
    CHECK(CoXlTestLwCutNavSlot(0x80010103u, 8, 0xFFu, 0x10000u, 0x00040008u, &key, &at) == 1 && key == 3 * 8 + 4 && at == 3 * 8);

    CHECK(CoXlTestLwCutNavSlot(0x80010002u, 8, 0xFFFFu, 0x10000u, 0x08100118u, &key, &at) == 1 && key == 2 * 0x118 + 0x10 && at == 2 * 0x118 + 0x08);

    g_case = "a watched member is stuck after 5 s with a goal and no way made";
    CHECK(CoXlTestLwCutBodyStep(1, 0.3f, 0.0f, 1, 0) == 0);
    for (int i = 0; i < 3; i++) CHECK(CoXlTestLwCutBodyStep(0, 0.3f, 0.3f, 1, 0) == 0);
    CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.49f, 1, 0) == 1); /* the fifth second within 0.5 m of the spot */
    CHECK(CoXlTestLwCutBodyStep(0, 0.5f, 0.0f, 1, 0) == 0);  /* 0.5 m off: a new spot */
    for (int i = 0; i < 4; i++) CHECK(CoXlTestLwCutBodyStep(0, 0.5f, 0.4f, 1, 0) == 0);
    CHECK(CoXlTestLwCutBodyStep(0, 0.5f, 0.4f, 1, 0) == 1);  /* measured from the new spot, on both axes */
    g_case = "a member with no goal is never stuck, and its seconds start afresh";
    CHECK(CoXlTestLwCutBodyStep(1, 0.0f, 0.0f, 0, 0) == 0);
    for (int i = 0; i < 8; i++) CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 0, 2) == 0);
    for (int i = 0; i < 4; i++) CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 0) == 0);
    CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 0) == 1);
    g_case = "a moving member whose navigator stays in another state for 5 s is stuck";
    CHECK(CoXlTestLwCutBodyStep(1, 0.0f, 0.0f, 1, 0) == 0);
    for (int i = 1; i <= 4; i++) CHECK(CoXlTestLwCutBodyStep(0, 2.0f * (float)i, 0.0f, 1, 2) == 0);
    CHECK(CoXlTestLwCutBodyStep(0, 10.0f, 0.0f, 1, 1) == 1);
    CHECK(CoXlTestLwCutBodyStep(0, 12.0f, 0.0f, 1, 0) == 0); /* state 0 ends it */
    for (int i = 1; i <= 4; i++) CHECK(CoXlTestLwCutBodyStep(0, 12.0f + 2.0f * (float)i, 0.0f, 1, 2) == 0);
    for (int i = 0; i < 300; i++) CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 2); /* the counts hold at their top */
    CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 2) == 1);

    g_case = "the spot follows the member on both axes, and the seconds hold at their top";
    CHECK(CoXlTestLwCutBodyStep(1, 0.0f, 0.0f, 1, 0) == 0 && CoXlTestLwCutBodyStep(0, 0.0f, 2.0f, 1, 0) == 0);
    for (int i = 0; i < 4; i++) CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 2.0f, 1, 0) == 0);
    CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 2.0f, 1, 0) == 1);
    CHECK(CoXlTestLwCutBodyStep(1, 0.0f, 0.0f, 1, 2) == 0);
    for (int i = 1; i <= 254; i++) CoXlTestLwCutBodyStep(0, (float)i, 0.0f, 1, 2); /* walking, never in state 0 */
    CHECK(CoXlTestLwCutBodyStep(0, 255.0f, 0.0f, 1, 2) == 1);
    CHECK(CoXlTestLwCutBodyStep(1, 0.0f, 0.0f, 1, 0) == 0);
    for (int i = 0; i < 254; i++) CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 0);
    CHECK(CoXlTestLwCutBodyStep(0, 0.0f, 0.0f, 1, 0) == 1);

    g_case = "repaths between two reads of a record's navigator";
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010005u, 7) == 0);  /* the first read of it */
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010005u, 10) == 3);
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010005u, 10) == 0);
    CHECK(CoXlTestLwCutRepathStep(6, 0x80010006u, 4) == 0 && CoXlTestLwCutRepathStep(5, 0x80010005u, 11) == 1); /* each record its own */
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010009u, 30) == 0); /* another navigator in the record */
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010009u, 2) == 0);  /* a count that fell is a new navigator */
    CHECK(CoXlTestLwCutRepathStep(5, 0x80010009u, 3) == 1);
}

/* The engine as the cutters' hooks need it (cutters_test.c has the whole of it). */
static uint8_t g_pool[0x40];
static uint32_t g_makeAnswer;
static uint32_t EngineMake(const void* description) { return g_makeAnswer; }
static void EngineDestroy(const uint32_t* handles, uint32_t count) {}
static void EngineSwitch(const uint32_t* handles, uint32_t count, int on) {}
static void Hex(uint8_t* at, const char* hex)
{
    for (; *hex; hex++)
    {
        unsigned v = 0;
        if (*hex == ' ') continue;
        sscanf(hex, "%2x", &v);
        *at++ = (uint8_t)v;
        hex++;
    }
}
typedef struct Desc { uint32_t type, world, mask, add; const float* matrix; uint32_t flag, pad, body, end; } Desc;
static const float kMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 40.0f, -70.0f, 12.0f, 1.0f};

/* A request is refused before its placement while the trees at the squad's place are not cut in. */
static void Gate(void)
{
    static uint64_t scene[0x4C0 / 8]; static uint32_t queue[8], block[4];
    g_case = "a squad's request waits for its trees' cut, at most 5 s";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    g_fake.strictCode = 1;
    Hex(exe + CO_CUTTERS_DESTROY, "85 D2 0F 84 32 01 00 00 41 54 41 55 48 83 EC 38 48 89 5C 24 50 45 33 C0");
    Hex(exe + 0x8127F6u, "49 8B 9C C3 30 63 7C 02 66 90 41 8B 74 BD 00 8B CE 23 4B 28 39 4B 24");
    Hex(exe + CO_CUTTERS_MAKE, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57");
    Hex(exe + 0x812DBAu, "83 F8 FF 0F 85 3F 03 00 00 48 8B CF E8 45 9F 18 00 8B 85 10 08 00 00");
    Hex(exe + 0x7D7F60u, "41 89 48 24");
    Hex(exe + 0x7D8090u, "4F 89 84 EC 30 63 7C 02");
    Hex(exe + 0x75331u, "F0 41 FF 41 20");
    Hex(exe + 0x754A6u, "F0 FF 49 20");
    size_t pins = 0;
    const CoCuttersPin* pin = co_cutters_test_record_pins(&pins);
    for (size_t i = 0; i < pins; i++) Hex(exe + pin[i].rva, pin[i].pin);
    { const uint64_t at = (uint64_t)(uintptr_t)g_pool; const uint32_t mask = 0xFFFF, room = 65536; memcpy(exe + CO_CUTTERS_POOLS + 16u, &at, 8); memcpy(g_pool + CO_CUTTERS_MASK, &mask, 4); memcpy(g_pool + CO_CUTTERS_ROOM, &room, 4); }
    { const uint64_t at = (uint64_t)(uintptr_t)block, world = (uint64_t)(uintptr_t)scene; memcpy(queue, &at, 8); scene[CO_CUTTERS_QUEUE / 8] = (uint64_t)(uintptr_t)queue; memcpy(exe + CO_CUTTERS_AI_WORLDS + 0x80u * CO_CUTTERS_WORLD, &world, 8); }
    g_fake.originals[0].target = (uintptr_t)exe + CO_CUTTERS_DESTROY; g_fake.originals[0].original = (void*)EngineDestroy;
    g_fake.originals[1].target = (uintptr_t)exe + CO_CUTTERS_MAKE; g_fake.originals[1].original = (void*)EngineMake;
    co_cutters_test_reset();
    CHECK(co_cutters_install((uintptr_t)exe) == NULL && co_cutters_record_arm((uintptr_t)exe, CO_CUTSET_SLOTS, CO_CUTSET_ADDS) == NULL);
    co_cutters_test_switch(EngineSwitch); co_cutters_switch(1); co_cutters_live(1);

    CoCuttersTick t; CoCutCentre c[CO_CUTSET_CENTRES]; uint32_t out[5], g[5];
    int places = 0, rooms = 0, retry = 0;
    Desc d = {3, 2, 0xFFFFFFFFu, 1, kMatrix, 0, 0, 77};
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CoXlTestLwCutSquad(0, OFFLINE, 0, 40.0f, -60.0f);
    CHECK(co_cutters_tick(NULL, 0, GetTickCount(), 0, &t) == 1);
    g_makeAnswer = 0x81010501u;
    CHECK(co_cutters_test_make(&d, (uintptr_t)exe + CO_CUTTERS_FOREST_RETURN) == 0x81010501u);
    /* the switch off, or record only: the request goes on to its placement and room, and the squad is no centre */
    CoXlTestLwCutMode(0);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1 && places == 1 && rooms == 1);
    CoXlTestLwCutMode(1);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1 && places == 1 && rooms == 1);
    CoXlTestLwCutGate(g);
    CHECK(g[WAITS] == 0 && g[IS_CUT] == 0 && g[GAMES] == 0 && g[LATE] == 0);
    CHECK(CoXlTestLwCutCentres(NULL, 0, NULL, NULL, 0, 1, 0, GetTickCount64(), c, out) == 0);
    /* switching: a tree 10 m from the squad waits for its cutter, so the request is refused before anything is placed */
    CoXlTestLwCutMode(2);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 0 && places == 0 && rooms == 0 && retry == 0);
    CHECK(CoXlTestLwGates(1, 1, 1, &places, &rooms, &retry) == 0 && places == 0 && rooms == 0); /* one waiting at the band's edge too */
    CoXlTestLwCutGate(g);
    CHECK(g[WAITS] == 2 && g[IS_CUT] == 0 && g[GAMES] == 0 && g[LATE] == 0);
    /* the ask made the squad a centre: the next tick sends its tree's cutter */
    CHECK(CoXlTestLwCutCentres(NULL, 0, NULL, NULL, 0, 1, 0, GetTickCount64(), c, out) == 1 && c[0].x == 40.0f && c[0].y == -60.0f);
    CHECK(co_cutters_tick(c, 1, GetTickCount(), 0, &t) == 1 && t.adds == 1);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 0 && places == 0); /* sent, not cut in yet */
    /* a navmesh run began with the add and is not in flight: placed */
    block[2] = 1; co_cutters_swap((uintptr_t)scene);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1 && places == 1 && rooms == 1);
    CoXlTestLwCutGate(g);
    CHECK(g[WAITS] == 3 && g[IS_CUT] == 1 && g[LATE] == 0 && g[4] < 5000);
    /* another tree, and the wait runs out: the request goes as it would have, counted */
    g_makeAnswer = 0x81010502u; co_cutters_test_make(&d, (uintptr_t)exe + CO_CUTTERS_FOREST_RETURN);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 0);
    CoXlTestLwCutWaited(0, 5001);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1 && places == 1);
    CoXlTestLwCutGate(g);
    CHECK(g[WAITS] == 4 && g[IS_CUT] == 1 && g[LATE] == 1 && g[4] >= 5001 && g[4] < 10000);
    /* no tick for over a second: the cutters are the game's and nothing waits */
    CHECK(co_cutters_tick(c, 1, GetTickCount() - 1500u, 0, &t) == 1);
    CHECK(CoXlTestLwGates(0, 1, 1, &places, &rooms, &retry) == 1);
    CoXlTestLwCutGate(g);
    CHECK(g[GAMES] == 1 && g[WAITS] == 4);
    CoXlTestLwCutMode(0);
    co_cutters_test_reset();
}


int main(void)
{
    unsigned v[4];
    g_case = "the switch's text";
    CHECK(CoXlTestLwCutParse("", v) == 0 && CoXlTestLwCutParse("0", v) == 0 && CoXlTestLwCutParse("0,5", v) == 0);
    CHECK(CoXlTestLwCutParse("1", v) == 1 && v[0] == 1 && v[1] == CO_CUTSET_CAP && v[2] == CO_CUTSET_ADDS && v[3] == CO_CUTSET_REMOVES);
    CHECK(CoXlTestLwCutParse("1,1000,64,8", v) == 1 && v[0] == 1 && v[1] == 1000 && v[2] == 64 && v[3] == 8);
    CHECK(CoXlTestLwCutParse("3 2000 32 4", v) == 1 && v[0] == 3 && v[1] == 2000 && v[2] == 32 && v[3] == 4);
    /* mode 2 adds only: no cap and no removes, whatever was asked */
    CHECK(CoXlTestLwCutParse("2,1000,64,8", v) == 1 && v[0] == 2 && v[1] == CO_CUTSET_SLOTS && v[2] == 64 && v[3] == 0);
    CHECK(CoXlTestLwCutParse("3,1000,64,8", v) == 1 && v[1] == 1000 && v[3] == 8);
    /* the limits themselves pass, one past each does not */
    CHECK(CoXlTestLwCutParse("3,1048576,512,64", v) == 1 && v[1] == CO_CUTSET_SLOTS && v[2] == CO_CUTSET_ADDS && v[3] == CO_CUTSET_BATCH);
    CHECK(CoXlTestLwCutParse("4", v) == -1 && CoXlTestLwCutParse("x", v) == -1);
    CHECK(CoXlTestLwCutParse("3,1048577", v) == -1 && CoXlTestLwCutParse("3,10,513", v) == -1 && CoXlTestLwCutParse("3,10,10,65", v) == -1);
    g_case = "a client's part";
    CHECK(CoXlTestLwCutClient("") == 1 && CoXlTestLwCutClient("0") == 0 && CoXlTestLwCutClient("1") == 1 && CoXlTestLwCutClient("2") == 2);
    CHECK(CoXlTestLwCutClient("3") == 1 && CoXlTestLwCutClient("/") == 1 && CoXlTestLwCutClient("x") == 1);

    CoCutCentre c[CO_CUTSET_CENTRES + 1];
    uint32_t out[5];
    const uint64_t players[3] = {0xA1, 0xB2, 0xC3};
    /* the streamer's list in another order, without 0xC3 */
    uint64_t peer[2] = {0xB2, 0xA1};
    float place[2][2] = {{2000.0f, -500.0f}, {100.0f, 50.0f}};

    g_case = "players at the streamer's places, by peer";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    memset(c, 0, sizeof c);
    CHECK(CoXlTestLwCutCentres(players, 3, peer, place, 2, 1, 0, 1000, c, out) == 2);
    CHECK(out[0] == 2 && out[1] == 0 && out[2] == 0 && out[3] == 0 && out[4] == 0);
    CHECK(c[0].x == 100.0f && c[0].y == 50.0f && c[0].in == CO_CUTSET_PLAYER_IN && c[0].out == CO_CUTSET_PLAYER_OUT);
    CHECK(c[1].x == 2000.0f && c[1].y == -500.0f && c[1].in == CO_CUTSET_PLAYER_IN && c[1].out == CO_CUTSET_PLAYER_OUT);
    g_case = "a player the streamer no longer has keeps the last place for 30 s";
    CHECK(CoXlTestLwCutCentres(players, 3, peer, place, 1, 1, 0, 2000, c, out) == 2 && out[0] == 2 && out[1] == 1);
    CHECK(c[0].x == 100.0f && c[0].y == 50.0f && c[1].x == 2000.0f);
    CHECK(CoXlTestLwCutCentres(players, 3, peer, place, 1, 1, 0, 1000 + CO_CUTSET_HOLD_MS + 1, c, out) == 1 && out[0] == 1 && out[1] == 0 && c[0].x == 2000.0f);
    g_case = "only the players the manager lists, and none past the limit";
    CHECK(CoXlTestLwCutCentres(players, 1, peer, place, 2, 1, 0, 50000, c, out) == 1 && c[0].x == 100.0f);
    {
        uint64_t unnamed[2] = {0xA1, 0}; /* the streamer holds a place with no peer: no player past the list is it */
        CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
        CHECK(CoXlTestLwCutCentres(players, 1, unnamed, place, 2, 1, 0, 1000, c, out) == 1 && out[0] == 1 && c[0].x == 2000.0f);
    }
    {
        uint64_t many[CO_SHIM_MAX_PLAYERS + 2], manyPeer[CO_SHIM_MAX_PLAYERS];
        float manyPlace[CO_SHIM_MAX_PLAYERS][2];
        for (int i = 0; i < CO_SHIM_MAX_PLAYERS + 2; i++) many[i] = 0x500u + (uint64_t)(i % CO_SHIM_MAX_PLAYERS); /* the two past the limit would read */
        for (int i = 0; i < CO_SHIM_MAX_PLAYERS; i++) { manyPeer[i] = 0x500u + (uint64_t)i; manyPlace[i][0] = 1000.0f * (float)i; manyPlace[i][1] = 0; }
        CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
        CHECK(CoXlTestLwCutCentres(many, CO_SHIM_MAX_PLAYERS + 2, manyPeer, manyPlace, CO_SHIM_MAX_PLAYERS, 1, 0, 1000, c, out) == CO_SHIM_MAX_PLAYERS);
        CHECK(out[0] == CO_SHIM_MAX_PLAYERS && c[CO_SHIM_MAX_PLAYERS - 1].x == 1000.0f * (float)(CO_SHIM_MAX_PLAYERS - 1));
    }
    g_case = "a client that adds none has no centre";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CHECK(CoXlTestLwCutCentres(players, 3, peer, place, 2, 0, 1, 1000, c, out) == 0 && out[0] == 0);

    g_case = "the host's squads: online, pending, or asking to come online; one a player's reach holds takes no centre";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CoXlTestLwCutSquad(0, ONLINE, 1, 5000.0f, 5000.0f);  /* far from every player */
    CoXlTestLwCutSquad(1, ONLINE, 1, 130.0f, 50.0f);     /* 30 m from the first player: its 64 m disc is inside 160 m */
    CoXlTestLwCutSquad(2, OFFLINE, 1, -5000.0f, 0.0f);   /* offline and not asking */
    CoXlTestLwCutSquad(3, PENDING, 1, -6000.0f, 0.0f);   /* requested: its units are on their way */
    CoXlTestLwCutSquad(4, ONLINE, 0, -7000.0f, 0.0f);
    CoXlTestLwCutSquad(5, ONLINE, 1, 100.0f, 150.0f);    /* 100 m from the first player: its disc passes 160 m */
    CoXlTestLwCutSquad(6, 0, 1, -8000.0f, 0.0f);         /* a free slot */
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 100000, c, out) == 6);
    CHECK(out[0] == 2 && out[2] == 4 && out[3] == 1 && out[4] == 0);
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 3000, c, out) == 6); /* a squad that never asked, early in the clock */
    CHECK(c[2].x == 5000.0f && c[2].y == 5000.0f && c[2].in == CO_CUTSET_SQUAD_IN && c[2].out == CO_CUTSET_SQUAD_OUT);
    CHECK(c[3].x == -6000.0f && c[4].x == -7000.0f && c[5].x == 100.0f && c[5].y == 150.0f);
    g_case = "an offline squad is a centre from its ask to come online, for 5 s after its last";
    CHECK(CoXlTestLwCutAsked(2, 100000) == 0);
    CoXlTestLwCutAsked(6, 100000); /* a free slot that asked is none */
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 100000, c, out) == 7 && out[2] == 5 && c[3].x == -5000.0f);
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 105000, c, out) == 7);
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 105001, c, out) == 6 && c[4].x == -7000.0f);
    g_case = "a squad's wait runs from its first ask; asks over 5 s apart are separate waits";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CHECK(CoXlTestLwCutAsked(3, 1000) == 0 && CoXlTestLwCutAsked(3, 3000) == 2000 && CoXlTestLwCutAsked(3, 8000) == 7000);
    CHECK(CoXlTestLwCutAsked(3, 13001) == 0 && CoXlTestLwCutAsked(3, 13500) == 499);
    CHECK(CoXlTestLwCutAsked(4, 50000) == 0 && CoXlTestLwCutAsked(4, 55000) == 5000 && CoXlTestLwCutAsked(3, 55000) == 0);
    g_case = "what an ask gets";
    CHECK(CoXlTestLwCutVerdict(1, 0) == IS_CUT && CoXlTestLwCutVerdict(1, 60000) == IS_CUT);
    CHECK(CoXlTestLwCutVerdict(-1, 0) == GAMES && CoXlTestLwCutVerdict(-1, 60000) == GAMES);
    CHECK(CoXlTestLwCutVerdict(0, 0) == WAITS && CoXlTestLwCutVerdict(0, 5000) == WAITS && CoXlTestLwCutVerdict(0, 5001) == LATE);
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CoXlTestLwCutSquad(0, ONLINE, 1, 5000.0f, 5000.0f);
    CoXlTestLwCutSquad(1, ONLINE, 1, 130.0f, 50.0f);
    CoXlTestLwCutSquad(5, ONLINE, 1, 100.0f, 150.0f);
    g_case = "a client has no squads";
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 0, 0, 1000, c, out) == 2 && out[2] == 0 && out[3] == 0);
    g_case = "a squad beside another squad still takes its centre: only players' reaches count";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CoXlTestLwCutSquad(0, ONLINE, 1, 5000.0f, 5000.0f);
    CoXlTestLwCutSquad(1, ONLINE, 1, 5000.0f, 5000.0f);
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 1000, c, out) == 4 && out[2] == 2 && out[3] == 0);
    g_case = "squads past the set's centres are left out and counted";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    for (int i = 0; i < CO_CUTSET_CENTRES; i++) CoXlTestLwCutSquad(i, ONLINE, 1, 3000.0f + 200.0f * (float)(i % 16), 3000.0f + 200.0f * (float)(i / 16));
    c[CO_CUTSET_CENTRES].x = -1.0f;
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 1000, c, out) == CO_CUTSET_CENTRES);
    CHECK(out[0] == 2 && out[2] == CO_CUTSET_CENTRES - 2 && out[3] == 0 && out[4] == 2 && c[CO_CUTSET_CENTRES].x == -1.0f);
    CHECK(c[CO_CUTSET_CENTRES - 1].x == 3000.0f + 200.0f * (float)((CO_CUTSET_CENTRES - 3) % 16));

    Gate();
    Measures();
    if (g_failed) { printf("lwcutters: %d check(s) failed\n", g_failed); return 1; }
    printf("lwcutters: all checks passed\n");
    return 0;
}
