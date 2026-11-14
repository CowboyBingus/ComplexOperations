/* The tree cutters' glue (src/xl/proto/living_world_cutters.c): the research switch's text, and the centres a tick is
 * given: every player at the streamer's place for it or, for 30 s, at the last one; on the host the online squads
 * that no player's reach holds, up to the set's limit. */
#include <stdio.h>
#include <string.h>

#include "xl/co_cutset.h"
#include "xl/co_shim.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

int CoXlTestLwCutParse(const char* text, unsigned* v);
int CoXlTestLwCutClient(const char* text);
void CoXlTestLwCutSquad(int i, int state, int every, float x, float y);
uint32_t CoXlTestLwCutCentres(const uint64_t* players, int count, const uint64_t* peer, float (*place)[2], int streamed, int host, int none, uint32_t now, CoCutCentre* c, uint32_t* out);
enum { OFFLINE = 1, PENDING = 2, ONLINE = 3 };

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
        uint64_t many[CO_SHIM_MAX_PLAYERS + 2], manyPeer[CO_SHIM_MAX_PLAYERS];
        float manyPlace[CO_SHIM_MAX_PLAYERS][2];
        for (int i = 0; i < CO_SHIM_MAX_PLAYERS + 2; i++) many[i] = 0x500u + (uint64_t)i;
        for (int i = 0; i < CO_SHIM_MAX_PLAYERS; i++) { manyPeer[i] = 0x500u + (uint64_t)i; manyPlace[i][0] = 1000.0f * (float)i; manyPlace[i][1] = 0; }
        CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
        CHECK(CoXlTestLwCutCentres(many, CO_SHIM_MAX_PLAYERS + 2, manyPeer, manyPlace, CO_SHIM_MAX_PLAYERS, 1, 0, 1000, c, out) == CO_SHIM_MAX_PLAYERS);
        CHECK(out[0] == CO_SHIM_MAX_PLAYERS && c[CO_SHIM_MAX_PLAYERS - 1].x == 1000.0f * (float)(CO_SHIM_MAX_PLAYERS - 1));
    }
    g_case = "a client that adds none has no centre";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CHECK(CoXlTestLwCutCentres(players, 3, peer, place, 2, 0, 1, 1000, c, out) == 0 && out[0] == 0);

    g_case = "the host's online squads: one a player's reach holds takes no centre";
    CoXlTestLwCutSquad(-1, 0, 0, 0, 0);
    CoXlTestLwCutSquad(0, ONLINE, 1, 5000.0f, 5000.0f);  /* far from every player */
    CoXlTestLwCutSquad(1, ONLINE, 1, 130.0f, 50.0f);     /* 30 m from the first player: its 64 m disc is inside 160 m */
    CoXlTestLwCutSquad(2, OFFLINE, 1, -5000.0f, 0.0f);
    CoXlTestLwCutSquad(3, PENDING, 1, -6000.0f, 0.0f);
    CoXlTestLwCutSquad(4, ONLINE, 0, -7000.0f, 0.0f);    /* not one the squads' tick visits every frame */
    CoXlTestLwCutSquad(5, ONLINE, 1, 100.0f, 150.0f);    /* 100 m from the first player: its disc passes 160 m */
    CHECK(CoXlTestLwCutCentres(players, 2, peer, place, 2, 1, 0, 1000, c, out) == 4);
    CHECK(out[0] == 2 && out[2] == 2 && out[3] == 1 && out[4] == 0);
    CHECK(c[2].x == 5000.0f && c[2].y == 5000.0f && c[2].in == CO_CUTSET_SQUAD_IN && c[2].out == CO_CUTSET_SQUAD_OUT);
    CHECK(c[3].x == 100.0f && c[3].y == 150.0f);
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

    if (g_failed) { printf("lwcutters: %d check(s) failed\n", g_failed); return 1; }
    printf("lwcutters: all checks passed\n");
    return 0;
}
